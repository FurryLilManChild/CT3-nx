/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* The generated register model (g_eax/g_esp are thread-local there) and the
 * XBOX_PTR/MEM32 accessors; the register names below are its macros. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

/* ── Library hooks (XDK 4531) ─────────────────────────────────
 *
 * CT3-nx. Crazy Taxi 3 links XDK 1.0.4531 (Sonic Heroes 5659, CoD-nx and
 * Burnout 3 5849), so none of the earlier hooks carry over by address. The
 * sheroes-nx versions are in docs/hooks-from-sheroes-5659.c.txt; each one
 * below was found again in this XBE's D3D and DSOUND sections with capstone
 * on 2026-11-05 (kickoff), with its evidence. Same library functions as
 * 5659, at older addresses and, in D3D, with device and display-block fields
 * moved by a few bytes.
 *
 * Not needed: CoD's per-frame DSP heartbeat. 4531's DSOUND has no 0xCCCCCC
 * anywhere; its only 10 ms stall (push 0x2710 at 0x0010E202, in
 * sub_0010E1F0) is reached once, from DirectSound init (caller 0x0010E69B). */

/* ── The idle thread (0x000B33D0) ───────────────────────────
 *
 * Thread #4 of boot (PsCreateSystemThreadEx ctx 0x000B33D0): while the quit
 * flag 0x002E00DC is 0, add 1 to the counter 0x002E00C4 (by the look of it a
 * CPU idle measure next to CRI's sound threads 0x000B3410 and 0x000B3480);
 * then set 0x002E00E0 and exit (0x000DE1E4, push 0xF0000001). The loop makes
 * no call, so it never hands the guest lock over: on the Xbox the scheduler
 * pre-empts it, here nothing did. PC boot test 6 (2026-11-05): this thread
 * (tid 224644) held the guest lock for the whole run, its host stack at
 * sub_000B33D0+0xEE with nothing deeper, while the main thread, the timer
 * thread's interrupt and two other guest threads waited in gil_lock. Same
 * loop, with the runtime's pre-emption check on each pass: one load when
 * nobody waits, a hand-over (and the point where a pending NtSuspendThread
 * from the sound lock 0x000B3330 stops it) when someone does. */
extern void sub_000DE1E4(void);

void sub_000B33D0(void)
{
    eax = MEM32(0x002E00DCu);
    while (eax == 0) {
        MEM32(0x002E00C4u) = MEM32(0x002E00C4u) + 1;
        RECOMP_PREEMPT();
        eax = MEM32(0x002E00DCu);
    }
    PUSH32(esp, 0xF0000001u);
    MEM32(0x002E00E0u) = 1;
    PUSH32(esp, 0x000B3408u); RECOMP_ABI_CALL(0x000DE1E4u, sub_000DE1E4);
}

/* ── CRI's sound lock and server passes (0x000B32F0, 0x000B3330, 0x000B97E0)
 *
 * The lock 0x000B32F0 / unlock 0x000B3330 (count at 0x002E00BC) keeps the
 * sound server threads out while the main thread changes shared sound state.
 * On the Xbox it is built from priorities: the servers (0x000B3410 at +1,
 * 0x000B3480 at -2) run their registered jobs through 0x000B97E0 (groups 2
 * and 5, from 0x000B9840 and 0x000B9850); the +1 server pre-empts the main
 * thread, so the main thread never gets in halfway through a pass; and while
 * the lock is held the +2 idle thread is resumed, so no server runs either.
 * The guest lock here hands over in turn at every yield point, whatever the
 * priority, so the main thread did get in mid-pass. PC play test 12: CRI's
 * ring-buffer check in 0x000B8420 found the read and write positions out of
 * step and called its error callback 0x000B8120 (`jmp $`) with -3, on the
 * +1 server's stack; the game then called a wild pointer and crashed during
 * NOW LOADING.
 *
 * The lock itself (disassembled): on 0 -> 1 it saves the caller's priority,
 * raises it to [0x002E00A8] and resumes the idle thread [0x0034ED60]; the
 * unlock on 1 -> 0 suspends the idle thread and restores the priority. So
 * while any thread holds it no server can run, and a thread that is not a
 * server only gets in during a pass when the server blocks. The two servers'
 * passes can overlap (the +1 pre-empts the -2 mid-pass).
 *
 * So: taking the lock fresh (count 0) outside a pass waits for every running
 * pass to end (giving up after 1 s, logged; a server blocked mid-pass lets
 * the main thread in on the Xbox too). A pass started outside a pass waits
 * while a thread other than this one holds the lock. Nested calls never
 * wait. PC play test 13: the first version waited on the most recent pass of
 * either server, even from inside the other server's own pass, so the servers
 * waited on each other 100 ms at a time and the logos crawled. Every 5 s a
 * [CRI] stats line gives the counts and the waits (RECOMP_CRI_EXCL=0 turns
 * the waits off; the stats stay). */
extern void sub_000B32F0_gen(void);
extern void sub_000B3330_gen(void);
extern void sub_000B97E0_gen(void);
/* Thread identity without host headers (this file builds for every host):
 * the address of a thread-local variable is unique per thread. */
static RECOMP_TLS char t_cri_me;
static RECOMP_TLS int  t_cri_pass_depth;   /* this thread's nesting in 0x000B97E0 */
#define CRI_ME ((uintptr_t)&t_cri_me)
static uintptr_t s_cri_lock_owner;         /* thread that took the count from 0 */
static uint32_t  s_cri_lock_caller;        /* its return address */
static uint64_t  s_cri_lock_since;
static int       s_cri_active;             /* threads inside a pass */
static uintptr_t s_cri_lock_key;           /* xbox_thread_key() of the holder */
static uintptr_t s_cri_pass_key[2];        /* the threads inside a pass now */
static uint32_t  s_cri_lock_stack[48];     /* its guest stack when it took the lock */
extern uintptr_t xbox_thread_key(void);
extern void xbox_gil_nopreempt(int delta);
extern void xbox_wait_yield_ms(unsigned ms);
extern unsigned xbox_gil_switched_in(void);
extern unsigned long xbox_gil_prev_other(void);
extern void xbox_gil_watch(int delta);          /* kernel_bridge.c: log intrusions into a pass */
extern void xbox_gil_sched_report(void);
static uint64_t cri_now_ms(void);
void xbox_thread_state_print(uintptr_t key, const char *who);
uintptr_t xbox_thread_key(void);
static RECOMP_TLS unsigned t_cri_pass_switch0;
static RECOMP_TLS uint64_t t_cri_pass_t0;

/* The test for the SJ ring crash (0x000B8420 -> error callback 0x000B8120
 * with -3): if other guest code ran during the server pass the error came
 * from, the guest lock was handed over mid-pass (what RECOMP_CRI_ATOMIC is
 * meant to stop); if none did, the ring was already wrong before the pass
 * and the cause is elsewhere. Called from recomp_icall_fail_log. */
static void cri_ring_error_report(void)
{
    fprintf(stderr, "[CRI] SJ ring error -3: this thread %s a server pass (depth %d, %llu ms in); "
            "other guest threads ran %u time(s) during it (last: host thread %lu); "
            "sound lock count %u, passes running %d\n",
            t_cri_pass_depth ? "is inside" : "is NOT inside", t_cri_pass_depth,
            t_cri_pass_depth ? (unsigned long long)(cri_now_ms() - t_cri_pass_t0) : 0ull,
            t_cri_pass_depth ? xbox_gil_switched_in() - t_cri_pass_switch0 : 0u,
            xbox_gil_prev_other(), MEM32(0x002E00BCu), s_cri_active);
    xbox_thread_state_print(xbox_thread_key(), "[CRI]   this thread");
}
extern void xbox_thread_state_print(uintptr_t key, const char *who);

static void cri_holder_report(void)
{
    int k, n = 0;
    xbox_thread_state_print(s_cri_lock_key, "[CRI]   the holder");
    xbox_thread_state_print(xbox_thread_key(), "[CRI]   this waiter");
    fprintf(stderr, "  [CRI]   the holder's guest stack when it took the lock (code addresses):");
    for (k = 0; k < 48 && n < 16; k++) {
        uint32_t v = s_cri_lock_stack[k];
        if (v >= 0x00011000u && v < 0x00123440u) { fprintf(stderr, " %08X", v); n++; }
    }
    fprintf(stderr, "\n");
}

/* The guest's KeTickCount (kernel data + 0x20), which xbox_memory_layout.c's
 * tick thread writes every millisecond. (Not xbox_KeTickCount: that host
 * copy is only updated on demand and stays 0.) */
extern uint32_t g_xbox_kernel_data_base;
static uint64_t cri_now_ms(void) { return MEM32(g_xbox_kernel_data_base + 0x20u); }

static int cri_excl(void)
{
    static int on = -1;
    /* Off by default since Switch test 8c: with the guest lock following the
     * Xbox's scheduling rules (RECOMP_GIL_PRIO) these waits are extra, and
     * the -2 server rarely ran to the end of its pass, so every lock waited
     * the full 1 s. RECOMP_CRI_EXCL=1 turns them on. */
    if (on < 0) { const char *e = getenv("RECOMP_CRI_EXCL"); on = e && *e == '1'; }
    return on;
}

static struct {
    uint64_t next;
    unsigned passes, locks, lock_waits, lock_giveups, pass_waits;
    uint64_t lock_wait_ms, pass_wait_ms, max_hold_ms;
} s_cri_st;

static void cri_stats(uint64_t now)
{
    if (!s_cri_st.next) { s_cri_st.next = now + 5000; return; }
    if (now < s_cri_st.next) return;
    fprintf(stderr, "[CRI] stats 5 s: passes %u, locks %u, lock waits %u (%llu ms, %u gave up), "
            "pass waits %u (%llu ms), longest hold %llu ms, now count %u holder caller %08X\n",
            s_cri_st.passes, s_cri_st.locks, s_cri_st.lock_waits, (unsigned long long)s_cri_st.lock_wait_ms,
            s_cri_st.lock_giveups, s_cri_st.pass_waits, (unsigned long long)s_cri_st.pass_wait_ms,
            (unsigned long long)s_cri_st.max_hold_ms, MEM32(0x002E00BCu),
            MEM32(0x002E00BCu) ? s_cri_lock_caller : 0u);
    xbox_gil_sched_report();
    memset(&s_cri_st, 0, sizeof s_cri_st);
    s_cri_st.next = now + 5000;
}

void sub_000B32F0(void)                 /* lock */
{
    uintptr_t me = CRI_ME;
    uint32_t caller = MEM32(esp);
    uint64_t t0 = cri_now_ms();

    cri_stats(t0);
    s_cri_st.locks++;
    if (cri_excl() && MEM32(0x002E00BCu) == 0 && t_cri_pass_depth == 0 && s_cri_active > 0) {
        s_cri_st.lock_waits++;
        while (s_cri_active > 0) {
            /* Switch test 4: the PC crash of play test 12 again (the SJ ring
             * check 0x000B8420 calling its error callback with -3 on the +1
             * server's stack), on a run with eight 100 ms give-ups (only the
             * first five shown). Giving up is the one way left for the main
             * thread to get in mid-pass, so wait longer (1 s) and say what
             * each pass thread is doing when it does happen. */
            if (cri_now_ms() > t0 + 1000) {
                static unsigned shown;
                int k;
                s_cri_st.lock_giveups++;
                if (shown++ < 20) {
                    fprintf(stderr, "[CRI] lock from %08X: a server pass still running after 1 s; "
                            "taking the lock anyway\n", caller);
                    for (k = 0; k < 2; k++)
                        if (s_cri_pass_key[k])
                            xbox_thread_state_print(s_cri_pass_key[k], "[CRI]   a pass thread");
                }
                break;
            }
            xbox_wait_yield_ms(1);
        }
        s_cri_st.lock_wait_ms += cri_now_ms() - t0;
    }
    if (MEM32(0x002E00BCu) == 0) {
        int k;
        s_cri_lock_owner = me;
        s_cri_lock_caller = caller;
        s_cri_lock_since = cri_now_ms();
        s_cri_lock_key = xbox_thread_key();
        /* The guest stack as it is now: the return addresses in it say which
         * code path took the lock (caller is only the callback wrapper at
         * 0x000B9470). Printed if a pass waits on this hold for 2 s. */
        for (k = 0; k < 48; k++)
            s_cri_lock_stack[k] = MEM32(esp + 4u * (uint32_t)k);
    }
    sub_000B32F0_gen();
}

void sub_000B3330(void)                 /* unlock */
{
    sub_000B3330_gen();
    if (MEM32(0x002E00BCu) == 0 && s_cri_lock_owner) {
        uint64_t held = cri_now_ms() - s_cri_lock_since;
        if (held > s_cri_st.max_hold_ms) s_cri_st.max_hold_ms = held;
        s_cri_lock_owner = 0;
    }
}

/* ── ADX's outer lock counter, 0x000B5B70 / 0x000B5B90 ─────────
 *
 * Switch test 5 (the looping-audio freeze): the stuck-lock report showed the
 * sound lock (count 1) taken on the path 0x000B3820 (the CRI thread's
 * vblank loop) -> 0x000B9B60 -> 0x000B2A30 -> 0x000B5B70, with that thread
 * long past it, back in its vblank wait. 0x000B5B70 is "if [0x002E0254] is
 * 0, take the sound lock; then [0x002E0254]++", and 0x000B5B90 is "--; at 0,
 * release it". 17 callers (the ADX calls the main thread makes, and that
 * loop). Between the test and the increment the lock call runs, and here it
 * can hand the guest lock over (its own kernel calls, or the wait above), so
 * a second thread finds the counter still 0 and takes the lock a second
 * time; the two releases then drop the counter to 0 once and the lock count
 * stays at 1 for good. On the Xbox the first thread is at priority 15 by
 * then and nobody else runs. So the counter is given an owner: another thread
 * waits while it is held. */
extern void sub_000B5B70_gen(void);
extern void sub_000B5B90_gen(void);
static uintptr_t s_adx_outer_owner;

void sub_000B5B70(void)
{
    uintptr_t me = CRI_ME;
    if (s_adx_outer_owner != me) {
        uint64_t t0 = cri_now_ms();
        int noted = 0;
        while (s_adx_outer_owner && s_adx_outer_owner != me) {
            if (!noted && cri_now_ms() > t0 + 2000) {
                fprintf(stderr, "[CRI] ADX outer lock: waiting over 2 s (counter %u)\n", MEM32(0x002E0254u));
                noted = 1;
            }
            xbox_wait_yield_ms(1);
        }
        s_adx_outer_owner = me;
    }
    sub_000B5B70_gen();
}

void sub_000B5B90(void)
{
    sub_000B5B90_gen();
    if (MEM32(0x002E0254u) == 0)
        s_adx_outer_owner = 0;
}

void sub_000B97E0(void)                 /* one server pass over a job group */
{
    uintptr_t me = CRI_ME;
    uint64_t t0 = cri_now_ms();

    cri_stats(t0);
    if (t_cri_pass_depth == 0) {
        s_cri_st.passes++;
        if (cri_excl() && MEM32(0x002E00BCu) > 0 && s_cri_lock_owner && s_cri_lock_owner != me) {
            int noted = 0;
            s_cri_st.pass_waits++;
            while (MEM32(0x002E00BCu) > 0 && s_cri_lock_owner && s_cri_lock_owner != me) {
                /* Faithful (no server runs while the lock is held), so no
                 * give-up, but a long wait is logged with the holder. */
                if (!noted && cri_now_ms() > t0 + 2000) {
                    fprintf(stderr, "[CRI] pass: waiting over 2 s for the sound lock (count %u, taken from %08X, %llu ms ago)\n",
                            MEM32(0x002E00BCu), s_cri_lock_caller,
                            (unsigned long long)(cri_now_ms() - s_cri_lock_since));
                    cri_holder_report();
                    noted = 1;
                }
                xbox_wait_yield_ms(1);
            }
            s_cri_st.pass_wait_ms += cri_now_ms() - t0;
        }
        s_cri_active++;
        {   /* note this pass thread for the give-up report */
            int k; uintptr_t key = xbox_thread_key();
            for (k = 0; k < 2; k++) if (!s_cri_pass_key[k]) { s_cri_pass_key[k] = key; break; }
        }
    }
    t_cri_pass_depth++;
    /* Switch tests 4 and 6 (and PC play test 12): the SJ ring check 0x000B8420
     * (put a chunk back: its position must follow the ring's) failed with -3
     * on the +1 server's stack, the second time with no lock give-up at all.
     * The SJ ring calls have no lock of their own (0x000BCC50 / 0x000BCC70 only
     * count): they rely on the server's priority, so nothing below it runs in
     * the middle of one. Here the guest lock could still be handed to another
     * thread at any function entry in the pass (RECOMP_PREEMPT), so a lower
     * thread could work on the same ring half-way through. A pass now gives
     * the lock up only in kernel calls (RECOMP_CRI_ATOMIC=1; see below). */
    {
        static int atomic = -1;
        /* Off by default since RECOMP_GIL_PRIO (main.c): with the lock handed over
         * by priority nothing below the server gets in mid-pass anyway, and
         * this would also keep out the threads above it (ISRs, DPCs, the
         * holder of the sound lock). RECOMP_CRI_ATOMIC=1 turns it on. */
        if (atomic < 0) { const char *e = getenv("RECOMP_CRI_ATOMIC"); atomic = e && *e == '1'; }
        if (atomic) xbox_gil_nopreempt(1);
        if (t_cri_pass_depth == 1) {
            t_cri_pass_switch0 = xbox_gil_switched_in();
            t_cri_pass_t0 = cri_now_ms();
        }
        xbox_gil_watch(1);
        sub_000B97E0_gen();
        xbox_gil_watch(-1);
        if (atomic) xbox_gil_nopreempt(-1);
    }
    if (--t_cri_pass_depth == 0) {
        int k; uintptr_t key = xbox_thread_key();
        for (k = 0; k < 2; k++) if (s_cri_pass_key[k] == key) s_cri_pass_key[k] = 0;
        s_cri_active--;
    }
}

/* ── D3D fence wait, BlockOnTime (0x000F86C0) ─────────────────
 *
 * Stdcall (fence at [esp+4], flags at [esp+8], ret 8). The device is the
 * global 0x001051D0 (94 reads in D3D; its +0 and +4 are the push buffer's
 * put and end pointers, as in sub_000F71C0). The body reads [dev+0x34]
 * (pointer to the GPU's completion semaphore) and [dev+0x30] (write
 * sequence): 5659 had them at +0x30 and +0x2C. It spins on the semaphore
 * (0x000F8770-0x000F8778) or waits on the event at dev+0x2458 with
 * KeWaitForSingleObject (0x000F8799). Callers 0x000F5240, 0x000F87B0,
 * 0x000F8990, 0x000F89B0, 0x000F94B0. */
#define CT3_D3D_DEVICE_PTR 0x001051D0u
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_000F86C0_gen(void);

void sub_000F86C0(void)
{
    static int exec = -1;
    uint32_t dev = MEM32(CT3_D3D_DEVICE_PTR);
    uint32_t sem = dev ? MEM32(dev + 0x34) : 0;

    if (exec < 0)
        exec = getenv("RECOMP_PB_EXEC") != NULL;
    if (exec) {
        static uint32_t registered;
        if (sem && sem != registered) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
            registered = sem;
        }
        sub_000F86C0_gen();
        return;
    }
    if (sem)
        MEM32(sem) = MEM32(dev + 0x30);     /* "GPU" is always caught up */
    eax = 0;
    esp += 4 + 8;                           /* return address, fence, flags */
}

/* ── DirectSound DSP command post (0x00109068) ───────────────
 *
 * sheroes-nx's sub_0034175A, the same body: thiscall, ebx =
 * ***(this + 8 + 0x10) is the GP DSP's scratch area (0x00109073-0x00109079),
 * the command (3) goes to scratch + 0x810 and it spins until the DSP zeroes
 * it (0x001091C5: add ebx,0x810; 0x001091CD: mov [ebx],eax; 0x001091CF:
 * cmp [ebx],0; jne 0x001091CF). There is no DSP, so the word is registered
 * with the runtime's completion list first. Found by searching DSOUND for
 * 0x810 (one hit). One caller, 0x001092D5. */
extern int xbox_ApuDspAckWord(uint32_t va);
extern void sub_00109068_gen(void);

void sub_00109068(void)
{
    uint32_t obj = MEM32(ecx + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
    sub_00109068_gen();
}

/* ── DirectSound AC'97 channel reset (0x0010E9E8) ────────────
 *
 * sheroes-nx's sub_00346F4D, the same body. Found by searching DSOUND for
 * the NABM control byte 0xFEC0010B: it sets RR (2) in the channel's control
 * byte (0xFEC0010B + table[0x00122BA8 + 4 * channel], 0x0010EA05), re-reads
 * it once and spins on the copy (0x0010EA1B-0x0010EA20: and cl,2; test cl,cl;
 * jne 0x0010EA1E), so with no controller it never leaves. This is the same
 * function with the reset completing at once. Lock: the stack object's
 * constructor 0x0010947B (ecx = frame - 8); then 0x0010E7F2 (four pushes)
 * and the destructor 0x001094AE. Callers 0x001097FC, 0x0010EA5A,
 * 0x0010EAA8, 0x0010EB02. */
void sub_0010E9E8(void)
{
    uint32_t ebp_saved = g_ebp, frame, chan, nabm;

    PUSH32(esp, ebp_saved);
    frame = esp;
    g_ebp = frame;
    g_seh_ebp = frame;
    esp -= 8;
    MEM32(frame - 4) = 0;
    PUSH32(esp, esi);
    esi = ecx;
    chan = esi;

    ecx = frame - 8;
    PUSH32(esp, 0x0010E9FCu); RECOMP_ABI_CALL(0x0010947Bu, sub_0010947B);

    nabm = MEM32(MEM32(chan) * 4 + 0x00122BA8u);
    MEM8(nabm + 0xFEC0010Bu) = 2;
    MEM8(nabm + 0xFEC0010Bu) &= (uint8_t)~2u;   /* reset complete */
    eax = nabm;
    SET_LO8(ecx, 0);

    MEM32(nabm + 0xFEC00100u) = MEM32(chan + 0x1C);
    if (MEM32(chan) == 1)
        MEM32(0xFEC0017Cu) = MEM32(chan + 0x28);

    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = chan;
    PUSH32(esp, MEM8(chan + 0x24));
    PUSH32(esp, MEM8(chan + 0x25));
    eax = MEM8(chan + 0x25);
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x0010EA4Fu); RECOMP_ABI_CALL(0x0010E7F2u, sub_0010E7F2);

    ecx = frame - 8;
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x0010EA57u); RECOMP_ABI_CALL(0x001094AEu, sub_001094AE);

    POP32(esp, esi);
    esp = frame;
    POP32(esp, ebp_saved);
    g_ebp = ebp_saved;
    esp += 4;
}

/* ── D3D vblank and PGRAPH handlers (0x000FBF50, 0x000FC470) ──
 *
 * sheroes-nx's hooks for its 0x003324D0 / 0x00332A40. D3D init sub_000FCD4D
 * calls KeInitializeDpc (0x000FCD64) with the DPC 0x000FC6E0 and
 * KeInitializeInterrupt (0x000FCDEC) with the routine 0x000FBDB0 on vector 3
 * (both seeded: only their addresses are pushed). The DPC reads PMC_INTR
 * ([ebp+0x100]) and calls the PGRAPH handler 0x000FC470 (bit 0x1000,
 * 0x000FC702) and the vblank handler 0x000FBF50 (bit 0x1000000, 0x000FC733),
 * both thiscall with ecx = the display block whose +0 is the register base.
 * 4531's block differs from 5659's: the flip retire count is +0x1F4 (5659
 * +0x1BC; incremented by the flip-retire routine 0x000FBED0 at 0x000FBF27),
 * the vblank count +0x1F8 (0x000FBF9F), the pending flips at +0x1B4 +
 * 8 * slot. Both run at DISPATCH_LEVEL so they cannot interleave with D3D's
 * own busy-waits; the PGRAPH handler (reads NSOURCE [0x400108] at
 * 0x000FC490, as 5659) takes the executor's trap under its lock; retired
 * flips go to the executor's FLIP_STALL. */
#define CT3_FLIP_RETIRED 0x1F4u
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);
extern void nv2a_pb_flip_retired(unsigned n);
extern void nv2a_pb_trap_lock(void);
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);
extern void sub_000FBF50_gen(void);
extern void sub_000FC470_gen(void);

static uint8_t d3d_isr_enter(void)
{
    uint8_t old = xbox_CurrentIrql();

    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    return old;
}

static void d3d_isr_leave(uint8_t old)
{
    if (old < 2)
        xbox_KfLowerIrql(old);
}

void sub_000FBF50(void)
{
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + CT3_FLIP_RETIRED);

    xbox_Nv2aVblankTaken();
    sub_000FBF50_gen();
    nv2a_pb_flip_retired(MEM32(blk + CT3_FLIP_RETIRED) - head);
    d3d_isr_leave(old);
}

void sub_000FC470(void)
{
    volatile uint32_t *nv = (volatile uint32_t *)XBOX_PTR(0xFD000000u);
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + CT3_FLIP_RETIRED);
    uint32_t nsource;

    nv2a_pb_trap_lock();
    nsource = nv[0x400108 / 4];
    sub_000FC470_gen();
    nv2a_pb_flip_retired(MEM32(blk + CT3_FLIP_RETIRED) - head);
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
    d3d_isr_leave(old);
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
static void cri_ring_error_report(void);
void recomp_icall_fail_log(uint32_t va)
{
    if (va == 0x000B8120u) {
        static unsigned n;
        if (n++ < 5) cri_ring_error_report();
    }
    /* CT3-nx: the call site (the return address the call pushed, at
     * [esp]) and the stack words above it, rate-limited per site: the first PC
     * stage run logged 19,765 null calls at 18 lines each (2026-10-28). */
    enum { SITES = 64 };
    static uint32_t site_ra[SITES], site_n[SITES];
    uint32_t ra = g_esp ? MEM32(g_esp) : 0;
    int k;
    for (k = 0; k < SITES && site_ra[k] && site_ra[k] != ra; k++)
        ;
    if (k < SITES) {
        site_ra[k] = ra;
        if (++site_n[k] > 3) {
            if ((site_n[k] & (site_n[k] - 1)) == 0)
                fprintf(stderr, "[ICALL] 0x%08X from 0x%08X: %u times\n", va, ra, site_n[k]);
            return;
        }
    }
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu), return address 0x%08X, "
            "stack %08X %08X %08X %08X %08X %08X, ecx %08X\n",
            va, (unsigned long long)g_icall_count, ra,
            MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp + 16),
            MEM32(g_esp + 20), MEM32(g_esp + 24), g_ecx);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* ── D3D fixed-function light setup, 0x000FE210 (memoised) ─────────────
 *
 * The Direct3D path, step 1 (docs/d3d-path.md). XDK 4531's lazy state flush
 * (0x000FE7D0 -> 0x000FE750) calls this whenever dirty bit 0x1000 is set:
 * it walks the enabled lights (list at [dev+0x398], next at +0x8C), moves
 * their directions into eye space with the view matrix at dev+0x750
 * (0x000F9770, x87), normalises them (0x000F9A60), multiplies colours
 * (0x000FDF30, 0x000FDE30) and writes the light registers 0x1000-0x13FC plus
 * 0x294 / 0x314 / 0x17C4 / 0x3B8 / 0x298 / 0x3BC into the pushbuffer. In a
 * shift it ran about every other draw (Switch test 9a: 10% of the main
 * thread; the cloud scan: about 26% of all pushbuffer words), from inputs
 * that change once a frame at most.
 *
 * So the output is kept with a copy of every input it reads, and a call
 * whose inputs are byte-identical writes the kept words instead of running
 * the maths. Inputs: the D3D globals listed below, dev+8, dev+0x2018, dev+0x9F0 (material),
 * the view matrix, [dev+0x380] and the word at +4 of it, and each light's
 * 0x90 bytes. RECOMP_LIGHT_MEMO=0 turns it off; RECOMP_LIGHT_MEMO=2 runs the
 * original on every hit too and logs any difference (the cloud check). */
extern void sub_000FE210_gen(void);
extern void sub_000F8930(void);
extern void sub_000F8930_gen(void);
static uint32_t s_lm_makespace;     /* calls of D3D's make-room routine, 0x000F8930 */

/* D3D's "make room in the pushbuffer" (0x000F8930: from [0x001056D0], via
 * 0x000F87B0). It can put its own words (a fence, method 0x1D70) in the
 * middle of the light setup's output; a kept result must never include them
 * (the cloud check's only mismatches), so its calls are counted. */
void sub_000F8930(void)
{
    s_lm_makespace++;
    sub_000F8930_gen();
}
#define LM_KEY_MAX 4096
#define LM_OUT_MAX 1024   /* (unused since the table) */
/* 64 kept results, chosen by a hash of the key: each frame draws with a
 * handful of materials under the same lights and view, so one kept result
 * alone (24% reused in the cloud) is not enough. */
#define LM_SLOTS 64
#define LM_SLOT_KEY 1536
#define LM_SLOT_OUT 160
static uint8_t  s_lm_cur[LM_KEY_MAX];
static struct { uint32_t hash, klen, olen, used; uint8_t key[LM_SLOT_KEY]; uint32_t out[LM_SLOT_OUT]; } s_lm_tab[LM_SLOTS];
static uint32_t s_lm_tick;
/* the slot this call uses (found or chosen), as the old single-entry names */
static uint8_t  *s_lm_key;
static uint32_t s_lm_klen, *s_lm_out, s_lm_olen;
static uint64_t s_lm_hits, s_lm_miss, s_lm_bad, s_lm_next;
static uint32_t lm_cur_slot;

static int lm_mode(void)
{
    static int m = -1;
    if (m < 0) { const char *e = getenv("RECOMP_LIGHT_MEMO"); m = e ? atoi(e) : 1; }
    return m;
}

/* One run of the key: its address, then its bytes, copied straight from
 * guest RAM (Switch test 10a: reading it word by word and hashing it byte by
 * byte cost 8.2% of the main thread, about what the light setup itself did;
 * pitfall 33). Every run here is in ordinary RAM (D3D's globals, the
 * device, the lights), never the tiled aperture the fold is for. */
static uint32_t lm_add(uint32_t n, uint32_t va, uint32_t bytes)
{
    if (n + bytes + 4 > LM_KEY_MAX || va - 0xF0000000u < 0x08000000u)
        return LM_KEY_MAX + 1;
    memcpy(s_lm_cur + n, &va, 4);
    memcpy(s_lm_cur + n + 4, (const void *)XBOX_PTR(va), bytes);
    return n + 4 + bytes;
}

/* The key's hash: 8 bytes at a time in four independent lanes. */
static uint32_t lm_hash(const uint8_t *k, uint32_t n)
{
    uint64_t h0 = 0x9E3779B97F4A7C15ull, h1 = 0xC2B2AE3D27D4EB4Full, h2 = 0x165667B19E3779F9ull,
             h3 = 0x27D4EB2F165667C5ull, w[4];
    uint32_t i = 0;
    for (; i + 32 <= n; i += 32) {
        memcpy(w, k + i, 32);
        h0 = (h0 ^ w[0]) * 0x100000001B3ull;
        h1 = (h1 ^ w[1]) * 0x100000001B3ull;
        h2 = (h2 ^ w[2]) * 0x100000001B3ull;
        h3 = (h3 ^ w[3]) * 0x100000001B3ull;
    }
    for (; i < n; i++)
        h0 = (h0 ^ k[i]) * 0x100000001B3ull;
    h0 ^= h1 * 31 ^ h2 * 37 ^ h3 * 41 ^ n;
    h0 ^= h0 >> 29;
    return (uint32_t)(h0 ^ (h0 >> 32));
}


/* ── Direct3D path: the fixed-function light setup in native code ─────────
 * docs/d3d-path.md, step 3 (entry points by cost). 0x000FE210, D3D's light
 * setup in the lazy flush (0x000FE7D0 -> 0x000FE750), and its helpers
 * 0x000FD310, 0x000FDF30, 0x000FDE30, 0x000F9770, 0x000F9A60/0x000F99B0,
 * 0x000F9740 and 0x000F9710, written from their disassembly as native C.
 * Switch test 10c profile: the light setup with its memo and helpers was about
 * 5.5% of the main thread. The x87 parts are done in double precision, in the
 * x87 code's own order, as the lifted code does with the control word the main
 * thread uses (precision 53 bits); with any other precision the lifted code
 * runs instead. The SSE part (0x000F9770) is single precision, never fused.
 * 0x000FE0D0 (specular power, transcendental helpers) and 0x000F8930 (make
 * room) are called as lifted code, at the same points as the original.
 * RECOMP_D3D_NATIVE_LIGHT=0: the memo path as before (RECOMP_LIGHT_MEMO);
 * =2: the native result is built aside, the lifted code runs, and the two
 * are compared word for word. */
extern void sub_000FE0D0(void);

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

static inline uint32_t nl_g32(uint32_t a) { uint32_t v; memcpy(&v, (const void *)XBOX_PTR(a), 4); return v; }
static inline float nl_gf(uint32_t a) { float v; memcpy(&v, (const void *)XBOX_PTR(a), 4); return v; }
static inline uint32_t nl_fbits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
static inline float nl_bitsf(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }

/* Where the words go: the pushbuffer, or (check mode) a buffer aside that
 * stands for it; put values stay guest addresses either way. */
static uint32_t *s_nl_buf, s_nl_base;
static inline void nl_w(uint32_t a, uint32_t v)
{
    if (s_nl_buf) s_nl_buf[(a - s_nl_base) / 4] = v;
    else memcpy((void *)XBOX_PTR(a), &v, 4);
}
static inline void nl_wf(uint32_t a, float f) { nl_w(a, nl_fbits(f)); }

static int s_nl_aside_overflow;
static uint32_t nl_makeroom(uint32_t ret)
{
    if (s_nl_buf) { s_nl_aside_overflow = 1; return nl_g32(nl_g32(0x001051D0u)); }
    PUSH32(esp, ret);
    sub_000F8930();
    return eax;
}

/* 0x000F9770: out.xyz = v.x*M0 + v.y*M1 + v.z*M2 + w*M3 (SSE, float) */
static void nl_xform(float o[3], uint32_t v, float w, uint32_t m)
{
    float x = nl_gf(v), y = nl_gf(v + 4), z = nl_gf(v + 8);
    int c;
    for (c = 0; c < 3; c++) {
        float p0 = x * nl_gf(m + 4 * c), p1 = y * nl_gf(m + 16 + 4 * c);
        float p2 = z * nl_gf(m + 32 + 4 * c), p3 = w * nl_gf(m + 48 + 4 * c);
        float s = p0 + p1;
        s = s + p2;
        o[c] = s + p3;
    }
}

/* 0x000F99B0: approximate 1/sqrt(f), x87; the result stays unrounded */
static double nl_rsqrt(float f)
{
    uint32_t b = (uint32_t)((int32_t)(0xBE800000u - nl_fbits(f)) >> 1);
    double c1f = (double)nl_gf(0x00214B38u) * (double)f;
    double y0 = (double)nl_bitsf(b);
    double y02 = y0 * y0;
    double t = y02 * c1f;
    double d = (double)nl_gf(0x00214B3Cu) - t;
    double y1 = d * y0;
    float y1f = (float)y1;
    double a = (double)nl_bitsf(nl_fbits(y1f) & 0x7FFFFFFFu);
    double r = a * a;
    r = r * (double)f;
    r = (double)nl_gf(0x0012E36Cu) - r;
    r = r * a;
    r = r * (double)nl_gf(0x0012AD14u);
    return r;
}

/* 0x000F9A60: normalise in place */
static void nl_norm(float v[3])
{
    double x = v[0], y = v[1], z = v[2];
    double s = x * x;
    s = s + y * y;
    s = s + z * z;
    {
        double r = nl_rsqrt((float)s);
        v[0] = (float)(r * x);
        v[1] = (float)(r * y);
        v[2] = (float)(r * z);
    }
}

/* 0x000FD310: which of the four lights' colour sources come from vertices */
static uint32_t nl_fd310(void)
{
    uint32_t e = 0, c, a;
    if (!nl_g32(0x001025E0u)) return 0;
    for (a = 0x001025E4u; a <= 0x00102600u; a += 4)
        e = (a == 0x001025E4u ? nl_g32(a) : (e << 2) | nl_g32(a));
    c = nl_g32(nl_g32(nl_g32(0x001051D0u) + 0x380) + 4);
    if (!(c & 0x400)) e &= 0xFFFFFFAAu;
    if (!(c & 0x800)) e &= 0xFFFFFF55u;
    if (!(c & 0x1000)) e &= 0xFFFFAAFFu;
    if (!(c & 0x2000)) e &= 0xFFFF55FFu;
    return e;
}

/* 0x000FDF30: scene ambient and emissive, front (and back) */
static uint32_t nl_fdf30(uint32_t out, uint32_t mask)
{
    uint32_t dev = nl_g32(0x001051D0u), sides = nl_g32(0x0010264Cu);
    uint32_t col = nl_g32(0x00102608u), mat = dev + 0x9F0;
    double k = nl_gf(0x0012E540u);
    nl_w(out, 0xC0A10); nl_w(out + 0x10, 0xC03A8); nl_w(out + 0x20, 0x403B4);
    for (;;) {
        double R = (double)(int32_t)((col >> 16) & 0xFF) * k;
        double G = (double)(int32_t)((col >> 8) & 0xFF) * k;
        float Gf = (float)G;
        float Bf = (float)((double)(int32_t)(col & 0xFF) * k);
        float l10, l14, l18;
        double o4, o8;
        float oc;
        if (mask & 0xC) {
            l10 = (float)R; l14 = Gf; l18 = Bf;
            o4 = nl_gf(mat + 0x30); o8 = nl_gf(mat + 0x34); oc = nl_gf(mat + 0x38);
        } else if (mask & 3) {
            o4 = R * (double)nl_gf(mat + 0x10);
            o8 = (double)Gf * (double)nl_gf(mat + 0x14);
            oc = (float)((double)Bf * (double)nl_gf(mat + 0x18));
            l10 = l14 = l18 = 1.0f;
        } else {
            o4 = R * (double)nl_gf(mat + 0x10) + (double)nl_gf(mat + 0x30);
            o8 = (double)Gf * (double)nl_gf(mat + 0x14) + (double)nl_gf(mat + 0x34);
            oc = (float)((double)Bf * (double)nl_gf(mat + 0x18) + (double)nl_gf(mat + 0x38));
            l10 = l14 = l18 = 0.0f;
        }
        nl_wf(out + 0xC, oc);
        nl_wf(out + 4, (float)o4);
        nl_wf(out + 0x14, l10);
        nl_wf(out + 8, (float)o8);
        nl_wf(out + 0x18, l14);
        nl_wf(out + 0x1C, l18);
        nl_w(out + 0x24, nl_g32(mat + 0xC));
        out += 0x28;
        if (!sides) break;
        sides--;
        col = nl_g32(0x00102604u);
        nl_w(out, 0xC17A0); nl_w(out + 0x10, 0xC17B0);
        mask >>= 8;
        mat = dev + 0xA34;
        nl_w(out + 0x20, 0x417AC);
    }
    return out;
}

/* 0x000FDE30: one light's colours times the material, front (and back) */
static uint32_t nl_fde30(uint32_t out, uint32_t lt, uint32_t idx, uint32_t mask)
{
    uint32_t dev = nl_g32(0x001051D0u), sides = nl_g32(0x0010264Cu), mat = dev + 0x9F0;
    nl_w(out, (idx + 0x4820) << 7);
    for (;;) {
        int i;
        for (i = 0; i < 3; i++)
            nl_w(out + 4 + 4 * i, (mask & 0xC) ? nl_g32(lt + 0x24 + 4 * i)
                 : nl_fbits((float)((double)nl_gf(mat + 0x10 + 4 * i) * (double)nl_gf(lt + 0x24 + 4 * i))));
        for (i = 0; i < 3; i++)
            nl_w(out + 0x10 + 4 * i, (mask & 0x30) ? nl_g32(lt + 4 + 4 * i)
                 : nl_fbits((float)((double)nl_gf(lt + 4 + 4 * i) * (double)nl_gf(mat + 4 * i))));
        for (i = 0; i < 3; i++)
            nl_w(out + 0x1C + 4 * i, (mask & 0xC0) ? nl_g32(lt + 0x14 + 4 * i)
                 : nl_fbits((float)((double)nl_gf(mat + 0x20 + 4 * i) * (double)nl_gf(lt + 0x14 + 4 * i))));
        out += 0x28;
        if (!sides) break;
        sides--;
        nl_w(out, (idx + 0x9030) << 6);
        mask >>= 8;
        mat = dev + 0xA34;
    }
    return out;
}

/* 0x000FE210 itself. Returns 0 when the lifted code must run instead. */
static int nl_light(uint32_t dev)
{
    uint32_t spec, t, put, edx, maskv, esi, lt, idx, bits = 0, ldx;
    t = nl_g32(dev + 0x380);
    spec = (nl_g32(0x001025D8u) != 0 || (nl_g32(dev + 8) & 0x40)) ? 1 : 0;
    if ((nl_g32(t + 4) & 0x12) || !nl_g32(0x001025D4u) || (nl_g32(dev + 0x2018) & ~0x10u) == 1) {
        put = nl_g32(dev);
        if (put >= nl_g32(dev + 4)) put = nl_makeroom(0x000FE50Bu);
        nl_w(put, 0x40314); nl_w(put + 4, 0); nl_w(put + 8, 0x403B8); nl_w(put + 0xC, spec);
        nl_w(put + 0x10, 0x40294); nl_w(put + 0x14, 0x20001);
        nl_w(put + 0x18, 0x417C4); nl_w(put + 0x1C, nl_g32(0x0010264Cu));
        if (!s_nl_buf) { MEM32(dev) = put + 0x20; eax = put + 0x20; }
        return 1;
    }
    put = nl_g32(dev);
    if (put >= nl_g32(dev + 4)) put = nl_makeroom(0x000FE26Eu);
    edx = put;
    esi = 1;
    if (spec) {
        if (nl_g32(0x001025DCu) && nl_g32(dev + 0x398)) esi = 0x10001;
        if (s_nl_buf) return 0;                 /* (check mode: the lifted helper writes the pushbuffer) */
        PUSH32(esp, put);
        PUSH32(esp, 0x000FE295u);
        sub_000FE0D0();
        edx = eax;
    }
    nl_w(edx, 0x40294); nl_w(edx + 4, esi); nl_w(edx + 8, 0x40314); nl_w(edx + 0xC, 1);
    nl_w(edx + 0x10, 0x417C4); nl_w(edx + 0x14, nl_g32(0x0010264Cu));
    nl_w(edx + 0x18, 0x403B8); nl_w(edx + 0x1C, 1);
    maskv = nl_fd310();
    nl_w(edx + 0x20, 0x40298); nl_w(edx + 0x24, maskv);
    esi = nl_fdf30(edx + 0x28, maskv);
    lt = nl_g32(dev + 0x398);
    for (idx = 0, ldx = 0x41024; lt; ) {
        float a[3], b[3];
        if (!s_nl_buf) MEM32(dev) = esi;
        if (esi >= nl_g32(dev + 4)) esi = nl_makeroom(0x000FE312u);
        esi = nl_fde30(esi, lt, idx, maskv);
        if (nl_g32(lt) == 3) {                  /* directional */
            bits |= 1u << (idx * 2);
            nl_xform(a, lt + 0x6C, 0.0f, dev + 0x750);
            nl_norm(a);
            {
                int i;
                for (i = 0; i < 3; i++)
                    b[i] = (float)((double)a[i] + (double)nl_gf(0x00102170u + 4 * i));
            }
            nl_norm(b);
            nl_w(esi, ldx); nl_w(esi + 4, 0x7149F2CA); nl_w(esi + 8, ldx + 0x140004);
            nl_wf(esi + 0xC, b[0]); nl_wf(esi + 0x10, b[1]); nl_wf(esi + 0x14, b[2]);
            nl_wf(esi + 0x18, a[0]); nl_wf(esi + 0x1C, a[1]); nl_wf(esi + 0x20, a[2]);
            esi += 0x24;
        } else {
            float p[3];
            nl_wf(esi + 4, (float)(double)nl_gf(lt + 0x4C));
            nl_w(esi, ldx);
            nl_xform(p, lt + 0x34, 1.0f, dev + 0x750);
            nl_w(esi + 8, ldx + 0x140038);
            nl_wf(esi + 0xC, p[0]); nl_wf(esi + 0x10, p[1]); nl_wf(esi + 0x14, p[2]);
            nl_w(esi + 0x18, nl_g32(lt + 0x54)); nl_w(esi + 0x1C, nl_g32(lt + 0x58));
            nl_w(esi + 0x20, nl_g32(lt + 0x5C));
            esi += 0x24;
            if (nl_g32(lt) == 1) {              /* point */
                bits |= 2u << (idx * 2);
            } else {                            /* spot */
                double s;
                int i;
                bits |= 3u << (idx * 2);
                nl_xform(a, lt + 0x6C, 0.0f, dev + 0x750);
                nl_norm(a);
                s = nl_gf(lt + 0x84);
                for (i = 0; i < 3; i++) a[i] = (float)(s * (double)a[i]);
                nl_w(esi, ldx + 0x18001C);
                nl_w(esi + 4, nl_g32(lt + 0x78)); nl_w(esi + 8, nl_g32(lt + 0x7C));
                nl_w(esi + 0xC, nl_g32(lt + 0x80));
                nl_wf(esi + 0x10, a[0]); nl_wf(esi + 0x14, a[1]); nl_wf(esi + 0x18, a[2]);
                nl_w(esi + 0x1C, nl_g32(lt + 0x88));
                esi += 0x20;
            }
        }
        ldx += 0x80;
        idx++;
        if (ldx == 0x41424) break;
        lt = nl_g32(lt + 0x8C);
    }
    nl_w(esi, 0x403BC); nl_w(esi + 4, bits);
    esi += 8;
    if (!s_nl_buf) { MEM32(dev) = esi; eax = esi; }
    else s_nl_base = esi;                       /* (check mode: where it ended) */
    return 1;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

static int nl_mode(void)
{
    static int m = -1;
    if (m < 0) { const char *e = getenv("RECOMP_D3D_NATIVE_LIGHT"); m = e && *e ? *e - '0' : 1; }
    return m;
}
static uint64_t s_nl_calls, s_nl_lifted, s_nl_checked, s_nl_bad;

/* Called first by sub_000FE210; returns 1 when it handled the call. */
static int nl_entry(uint32_t dev)
{
    int m = nl_mode();
    if (!m || !dev) return 0;
    if (((s_nl_calls + s_nl_checked + s_nl_lifted) & 0x3FFFF) == 0x3FFFF)
        fprintf(stderr, "[D3D] native light setup: %llu calls native, %llu checked (%llu different), %llu left to the lifted code\n",
                (unsigned long long)s_nl_calls, (unsigned long long)s_nl_checked,
                (unsigned long long)s_nl_bad, (unsigned long long)s_nl_lifted);
    if ((recomp_fpcw_ld() & 0x300u) == 0) { s_nl_lifted++; return 0; }   /* single precision: lifted */
    if (m == 1) {
        if (!nl_light(dev)) return 0;
        esp += 8;                               /* return address, dev (ret 4) */
        s_nl_calls++;
        return 1;
    }
    {   /* check: aside, then the lifted code, then compare */
        static uint32_t aside[1024];
        uint32_t put0 = nl_g32(dev), end_aside, put1, n, i, ok;
        s_nl_buf = aside; s_nl_base = put0; s_nl_aside_overflow = 0;
        memset(aside, 0, sizeof aside);
        ok = (uint32_t)nl_light(dev);
        end_aside = s_nl_base;
        s_nl_buf = NULL;
        {
            uint32_t ms0 = s_lm_makespace;
            sub_000FE210_gen();
            put1 = MEM32(dev);
            if (!ok || s_nl_aside_overflow || s_lm_makespace != ms0) { s_nl_lifted++; return 1; }
        }
        /* the "disabled" path leaves s_nl_base at put0: its length is 8 words */
        n = (end_aside == put0) ? 8 : (end_aside - put0) / 4;
        s_nl_checked++;
        if (put1 - put0 != 4 * n || n > 1024) {
            if (s_nl_bad++ < 20)
                fprintf(stderr, "[D3D] native light setup: %u words, the lifted code wrote %u\n", n, (put1 - put0) / 4);
            return 1;
        }
        for (i = 0; i < n; i++)
            if (aside[i] != MEM32(XBOX_DEV_FOLD(put0 + 4 * i))) break;
        if (i < n && s_nl_bad++ < 20)
            fprintf(stderr, "[D3D] native light setup differs at word %u of %u: native %08X, lifted %08X (word before %08X)\n",
                    i, n, aside[i], MEM32(XBOX_DEV_FOLD(put0 + 4 * i)), i ? aside[i - 1] : 0);
        return 1;
    }
}

void sub_000FE210(void)
{
    uint32_t dev = MEM32(esp + 4), n = 0, l, k, put0, put1, mode = (uint32_t)lm_mode();
    int hit;

    if (nl_entry(dev)) return;                  /* the native light setup (above) */
    if (!mode || !dev) { sub_000FE210_gen(); return; }
    /* D3D globals read by it and its helpers: 0x00102170 (0x000FE210),
     * 0x001025D4-0x0010260B (0x000FE210, 0x000FD310, 0x000FDF30),
     * 0x0010264C (all of them). A wider block (0x00102100-0x001026FF)
     * never matched: 0x001026D0 changes on every call, the dirty mask
     * 0x00102258 and others often (RECOMP_LIGHT_MEMO_WHY=1 counted them). */
    n = lm_add(n, 0x00102170u, 12);
    n = lm_add(n, 0x001025D4u, 0x38);
    n = lm_add(n, 0x0010264Cu, 4);
    n = lm_add(n, dev + 0x9F0, 0x88);           /* the material(s) (0x9F0-0xA77; 0xA78 moves on every call and is not read): 0x000FDF30, 0x000FDE30, 0x000FE0D0 */
    n = lm_add(n, dev + 8, 4);
    n = lm_add(n, dev + 0x2018, 4);
    n = lm_add(n, dev + 0x750, 64);
    n = lm_add(n, dev + 0x380, 4);
    if (MEM32(dev + 0x380))
        n = lm_add(n, MEM32(dev + 0x380), 8);
    n = lm_add(n, dev + 0x398, 4);
    for (l = MEM32(dev + 0x398), k = 0; l && k < 8 && n <= LM_KEY_MAX; l = MEM32(l + 0x8C), k++)
        n = lm_add(n, l, 0x90);
    {
        uint32_t h = n <= LM_KEY_MAX ? lm_hash(s_lm_cur, n) : 0, i, slot = 0, best = 0xFFFFFFFFu;
        hit = 0;
        for (i = 0; i < LM_SLOTS; i++) {
            if (s_lm_tab[i].olen && s_lm_tab[i].hash == h && s_lm_tab[i].klen == n
                && !memcmp(s_lm_tab[i].key, s_lm_cur, n)) { slot = i; hit = 1; break; }
            if (s_lm_tab[i].used < best) { best = s_lm_tab[i].used; slot = i; }
        }
        s_lm_tab[slot].used = ++s_lm_tick;
        if (!hit) { s_lm_tab[slot].hash = h; }
        s_lm_key = s_lm_tab[slot].key;
        s_lm_out = s_lm_tab[slot].out;
        s_lm_klen = s_lm_tab[slot].klen;
        s_lm_olen = s_lm_tab[slot].olen;
        if (n > LM_SLOT_KEY) hit = 0;
        if (!hit) s_lm_tab[slot].olen = 0;      /* refilled below if it can be kept */
        lm_cur_slot = slot;
    }

    if (hit && mode != 2) {
        uint32_t put = MEM32(dev), i;
        if (put + 4 * s_lm_olen >= MEM32(dev + 4)) {   /* make room for the whole block */
            PUSH32(esp, 0x000FE26Eu);
            sub_000F8930();
            put = eax;
        }
        if (put - 0xF0000000u >= 0x08000000u)   /* the pushbuffer is plain RAM: one copy */
            memcpy((void *)XBOX_PTR(put), s_lm_out, 4 * s_lm_olen);
        else
            for (i = 0; i < s_lm_olen; i++)
                MEM32(XBOX_DEV_FOLD(put + 4 * i)) = s_lm_out[i];
        MEM32(dev) = put + 4 * s_lm_olen;
        eax = MEM32(dev);
        esp += 8;                               /* return address, dev (ret 4) */
        s_lm_hits++;
        goto stats;
    }

    {
        uint32_t ms0 = s_lm_makespace;
        put0 = MEM32(dev);
        sub_000FE210_gen();
        put1 = MEM32(dev);
        if (s_lm_makespace != ms0) {            /* its words may be in there: keep nothing */
            if (!hit) { s_lm_miss++; s_lm_olen = 0; }
            goto stats;
        }
    }
    if (hit) {                                  /* mode 2: compare */
        uint32_t i, len = put1 >= put0 ? (put1 - put0) / 4 : 0;
        int same = len == s_lm_olen;
        for (i = 0; same && i < len; i++)
            same = MEM32(XBOX_DEV_FOLD(put0 + 4 * i)) == s_lm_out[i];
        s_lm_hits++;
        if (!same && s_lm_bad++ < 20) {
            uint32_t d0 = 0;
            while (d0 < len && d0 < s_lm_olen && MEM32(XBOX_DEV_FOLD(put0 + 4 * d0)) == s_lm_out[d0]) d0++;
            fprintf(stderr, "[D3D] light memo MISMATCH: %u words now, %u kept; first difference at word %u:"
                    " now %08X kept %08X (words before: %08X %08X %08X)\n", len, s_lm_olen, d0,
                    d0 < len ? MEM32(XBOX_DEV_FOLD(put0 + 4 * d0)) : 0, d0 < s_lm_olen ? s_lm_out[d0] : 0,
                    d0 >= 3 ? s_lm_out[d0 - 3] : 0, d0 >= 2 ? s_lm_out[d0 - 2] : 0, d0 >= 1 ? s_lm_out[d0 - 1] : 0);
        }
    } else {
        s_lm_miss++;
        if (getenv("RECOMP_LIGHT_MEMO_WHY") && n == s_lm_klen) {   /* every key word that moved, counted */
            static uint32_t chg[LM_KEY_MAX / 4], calls;
            uint32_t w;
            for (w = 0; w < n / 4; w++)
                if (memcmp(s_lm_cur + 4 * w, s_lm_key + 4 * w, 4)) chg[w]++;
            if (++calls % 20000 == 0) {
                uint32_t top, best, t;
                fprintf(stderr, "[D3D] light memo key words that changed (key offset: count):");
                for (t = 0; t < 12; t++) {
                    best = 0; top = 0;
                    for (w = 0; w < n / 4; w++) if (chg[w] > best) { best = chg[w]; top = w; }
                    if (!best) break;
                    fprintf(stderr, " %u:%u", 4 * top, best);
                    chg[top] = 0;
                }
                fprintf(stderr, "\n");
                memset(chg, 0, sizeof chg);
            }
        }
        if (0) {
            static int shown;
            uint32_t i, va = 0, at = 0, j = 0;
            for (i = 0; i < n && shown < 40; ) {
                uint32_t hv, bytes;
                memcpy(&hv, s_lm_cur + i, 4);
                j = i + 4;
                /* each run is (va, bytes...) and the next run starts at the next va; find this run's end */
                (void)hv;
                break;
            }
            for (i = 0; i < n; i++)
                if (s_lm_cur[i] != s_lm_key[i]) { at = i; break; }
            if (i < n && shown++ < 40) {
                /* walk the runs to name the address */
                uint32_t p2 = 0, runva = 0, runoff = 0;
                uint32_t sizes[] = { 0x600, 4, 4, 64, 4, 8, 4 };
                uint32_t si = 0;
                while (p2 < n) {
                    uint32_t sz = si < 7 ? sizes[si] : 0x90;
                    if (si == 5 && !MEM32(dev + 0x380)) { si++; continue; }
                    memcpy(&runva, s_lm_cur + p2, 4);
                    if (at < p2 + 4 + sz) { runoff = at < p2 + 4 ? 0 : at - p2 - 4; break; }
                    p2 += 4 + sz; si++;
                }
                va = runva + runoff;
                fprintf(stderr, "[D3D] light memo: input 0x%08X changed (run base 0x%08X, dev 0x%08X)\n", va, runva, dev);
            }
            (void)j;
        }
        s_lm_olen = 0;
        /* Keep it only when the words went out in one run (no wrap). */
        if (n <= LM_SLOT_KEY && put1 > put0 && (put1 - put0) / 4 <= LM_SLOT_OUT) {
            uint32_t i;
            s_lm_olen = (put1 - put0) / 4;
            for (i = 0; i < s_lm_olen; i++)
                s_lm_out[i] = MEM32(XBOX_DEV_FOLD(put0 + 4 * i));
            memcpy(s_lm_key, s_lm_cur, n);
            s_lm_klen = n;
            s_lm_tab[lm_cur_slot].klen = n;
            s_lm_tab[lm_cur_slot].olen = s_lm_olen;
        }
    }
stats:
    {
        uint64_t now = cri_now_ms();
        if (now >= s_lm_next) {
            if (s_lm_next)
                fprintf(stderr, "[D3D] light setup 10 s: %llu kept results reused, %llu computed, %llu mismatches\n",
                        (unsigned long long)s_lm_hits, (unsigned long long)s_lm_miss,
                        (unsigned long long)s_lm_bad);
            s_lm_hits = s_lm_miss = 0;
            s_lm_next = now + 10000;
        }
    }
}

/* ── Native vector and matrix maths (CT3-nx, Direct3D path) ────────────────
 * Two of the game's own SSE helpers (linked from Microsoft's XGraphics
 * library; 245 and 207 callers), done here in native code. Switch test 10a
 * profile: lifted, they were 2.2% and 1.1% of the main thread, every SSE
 * register a thread-local through the lifter's RecompXmm.
 * Same results to the bit as the SSE code: single-precision products and
 * sums in its exact order, never fused (fp-contract off; each step its own
 * statement for compilers without the pragma). RECOMP_NATIVE_MATH=0 runs the
 * lifted code. */
extern void sub_000E4E73_gen(void);
extern void sub_000E4EB9_gen(void);

static int native_math_on(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("RECOMP_NATIVE_MATH"); on = e && *e ? *e - '0' : 1; }
    return on;
}
/* RECOMP_NATIVE_MATH=2: the lifted code runs too and the two are compared */
static uint64_t s_nm_checked, s_nm_bad;
static void nm_check(const char *what, uint32_t out, const float *mine, int n)
{
    s_nm_checked++;
    if (memcmp((const void *)XBOX_PTR(out), mine, 4 * n)) {
        if (s_nm_bad++ < 20)
            fprintf(stderr, "[MATH] %s differs from the lifted code (%llu of %llu)\n", what,
                    (unsigned long long)s_nm_bad, (unsigned long long)s_nm_checked);
    } else if ((s_nm_checked & 0xFFFFF) == 0)
        fprintf(stderr, "[MATH] %llu native results checked, %llu different\n",
                (unsigned long long)s_nm_checked, (unsigned long long)s_nm_bad);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
/* v (4 floats) times the rows of m: ((x*m0 + y*m1) + z*m2) + w*m3 */
static void nm_vec4_rows(float o[4], const float v[4], const float m[16])
{
    int c;
    for (c = 0; c < 4; c++) {
        float p0 = v[0] * m[c], p1 = v[1] * m[4 + c], p2 = v[2] * m[8 + c], p3 = v[3] * m[12 + c];
        float s = p0 + p1;
        s = s + p2;
        o[c] = s + p3;
    }
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

/* 0x000E4E73: XGVec4Transform(out, v, m) (stdcall, ret 0xC; eax = out).
 * movups loads, so no alignment is assumed. */
void sub_000E4E73(void)
{
    uint32_t out = MEM32(esp + 4), v = MEM32(esp + 8), m = MEM32(esp + 12);
    float fv[4], fm[16], fo[4];
    if (!native_math_on()) { sub_000E4E73_gen(); return; }
    memcpy(fv, (const void *)XBOX_PTR(v), 16);
    memcpy(fm, (const void *)XBOX_PTR(m), 64);
    nm_vec4_rows(fo, fv, fm);
    if (native_math_on() == 2) { sub_000E4E73_gen(); nm_check("XGVec4Transform", out, fo, 4); return; }
    memcpy((void *)XBOX_PTR(out), fo, 16);
    eax = out;
    esp += 16;                                  /* return address and 3 arguments */
}

/* 0x000E4EB9: XGMatrixMultiply(out, a, b) (stdcall, ret 0xC; eax = ecx = out).
 * Every row is computed before any is stored, so out may be a or b. */
void sub_000E4EB9(void)
{
    uint32_t out = MEM32(esp + 4), a = MEM32(esp + 8), b = MEM32(esp + 12);
    float fa[16], fb[16], fo[16];
    int r;
    if (!native_math_on()) { sub_000E4EB9_gen(); return; }
    memcpy(fa, (const void *)XBOX_PTR(a), 64);
    memcpy(fb, (const void *)XBOX_PTR(b), 64);
    for (r = 0; r < 4; r++)
        nm_vec4_rows(fo + 4 * r, fa + 4 * r, fb);
    if (native_math_on() == 2) { sub_000E4EB9_gen(); nm_check("XGMatrixMultiply", out, fo, 16); return; }
    memcpy((void *)XBOX_PTR(out), fo, 64);
    eax = out;
    ecx = out;
    esp += 16;
}

/* ── Direct3D path: SetTexture (0x000F61D0) in native code ────────────────
 * docs/d3d-path.md step 3. 0x000F61D0(stage, texture), stdcall: the game's
 * SetTexture (12 callers, e.g. 0x000A49E0, 0x000A6040). Switch test 10c
 * profile: 1.7% of the main thread, the costliest D3D call after the light
 * setup. Integer work only: the stage's texture slot (dev+0xA78), the
 * texture's use count (+0, in steps of 0x80000) and fence (+8, from dev+0x30),
 * the NV2A texture offset/format/control words (0x1B00 + 64*stage ...), the
 * stage's format cache (dev+0xC) and D3D's dirty mask (0x00102258).
 * 0x000F4C00 (frees a texture whose count reaches zero) and 0x000F8930 (make
 * room) are called as lifted code at the same points.
 * RECOMP_D3D_NATIVE_TEX=0: the lifted code; =2: both run on the same state and
 * every word they write is compared (the native result is the one kept). */
extern void sub_000F61D0_gen(void);
extern void sub_000F4C00(void);

static uint32_t *s_nt_buf, s_nt_base;     /* check mode: pushbuffer words go aside */
static int s_nt_overflow;
static inline void nt_w(uint32_t a, uint32_t v)
{
    if (s_nt_buf && a >= s_nt_base && a < s_nt_base + 256) { s_nt_buf[(a - s_nt_base) / 4] = v; return; }
    memcpy((void *)XBOX_PTR(a), &v, 4);
}

static void nt_settexture(uint32_t stage, uint32_t tex, uint32_t ret_base)
{
    uint32_t dev = nl_g32(0x001051D0u), old = nl_g32(dev + stage * 4 + 0xA78), put, p, c, fmt;
    (void)ret_base;
    if (old) {
        uint32_t v = nl_g32(old) + 0xFFF80000u;
        nt_w(old + 8, nl_g32(dev + 0x30));
        nt_w(old, v);
        if (!(v & 0x78FFFFu)) {
            PUSH32(esp, old);
            PUSH32(esp, 0x000F6213u);
            sub_000F4C00();
        }
    }
    nt_w(dev + stage * 4 + 0xA78, tex);
    put = nl_g32(dev);
    if (!tex) {
        if (put >= nl_g32(dev + 4)) {
            if (s_nt_buf) { s_nt_overflow = 1; return; }
            PUSH32(esp, 0x000F622Eu); sub_000F8930(); put = eax;
        }
        nt_w(put, (stage << 6) + 0x41B0C);
        nt_w(put + 4, 0);
        nt_w(dev, put + 8);
        nt_w(dev + stage * 4 + 0xC, 0xFFFFFFFFu);
        nt_w(0x00102258u, nl_g32(0x00102258u) | 0x4800);
        return;
    }
    nt_w(tex, nl_g32(tex) + 0x80000);
    if (put >= nl_g32(dev + 4)) {
        if (s_nt_buf) { s_nt_overflow = 1; return; }
        PUSH32(esp, 0x000F6279u); sub_000F8930(); put = eax;
    }
    nt_w(put, (stage + 0x206C) << 6);
    nt_w(put + 4, nl_g32(tex + 4));
    nt_w(put + 8, nl_g32(tex + 0xC));
    p = put + 0xC;
    c = nl_g32(tex + 0x10);
    if (c) {
        uint32_t w = (c & 0xFFF) + 1, h = ((c >> 12) & 0xFFF) + 1;
        uint32_t pitch = (uint32_t)(*(const uint8_t *)XBOX_PTR(tex + 0x13)) + 1;
        nt_w(p, (stage << 6) + 0x41B10);
        nt_w(p + 4, (pitch << 6) << 16);
        nt_w(p + 8, (stage << 6) + 0x41B1C);
        nt_w(p + 0xC, (w << 16) | h);
        p += 0x10;
    }
    fmt = nl_g32(tex + 0xC) & 0xF4;
    if (nl_g32(dev + stage * 4 + 0xC) != fmt) {
        nt_w(dev + stage * 4 + 0xC, fmt);
        nt_w(0x00102258u, nl_g32(0x00102258u) | 0x4000);
        if (!old) {
            nt_w(p, (stage << 6) + 0x41B0C);
            nt_w(p + 4, nl_g32(dev + stage * 4 + 0x360));
            p += 8;
            nt_w(0x00102258u, nl_g32(0x00102258u) | 0x800);
        }
    }
    nt_w(dev, p);
}

static uint64_t s_nt_calls, s_nt_checked, s_nt_bad, s_nt_skip;
void sub_000F61D0(void)
{
    static int m = -1;
    uint32_t stage = MEM32(esp + 4), tex = MEM32(esp + 8), dev;
    if (m < 0) { const char *e = getenv("RECOMP_D3D_NATIVE_TEX"); m = e && *e ? *e - '0' : 1; }
    if (!m || stage >= 4) { sub_000F61D0_gen(); return; }
    dev = nl_g32(0x001051D0u);
    if (m == 2) {
        /* every word either may touch, before; the lifted code; its words;
         * the words back; the native code; compare */
        uint32_t old = nl_g32(dev + stage * 4 + 0xA78), put0 = nl_g32(dev);
        uint32_t addr[12], before[12], lifted[12], pb_l[64], n = 0, i, put1, ms0 = s_lm_makespace;
        int same = 1;
        addr[n++] = dev; addr[n++] = dev + stage * 4 + 0xA78; addr[n++] = dev + stage * 4 + 0xC;
        addr[n++] = 0x00102258u;
        if (old) { addr[n++] = old; addr[n++] = old + 8; }
        if (tex) addr[n++] = tex;
        for (i = 0; i < n; i++) before[i] = nl_g32(addr[i]);
        if (old && !((nl_g32(old) + 0xFFF80000u) & 0x78FFFFu)) {   /* frees a texture: no second run */
            sub_000F61D0_gen(); s_nt_skip++; return;
        }
        {
            uint32_t sp = esp;
            sub_000F61D0_gen();
            (void)sp;
        }
        put1 = nl_g32(dev);
        if (s_lm_makespace != ms0 || put1 < put0 || put1 - put0 > 256) { s_nt_skip++; return; }
        for (i = 0; i < n; i++) lifted[i] = nl_g32(addr[i]);
        for (i = 0; i < (put1 - put0) / 4; i++) pb_l[i] = nl_g32(put0 + 4 * i);
        for (i = 0; i < n; i++) memcpy((void *)XBOX_PTR(addr[i]), &before[i], 4);
        nt_settexture(stage, tex, 0);
        s_nt_checked++;
        if (nl_g32(dev) != put1) same = 0;
        for (i = 0; same && i < n; i++) if (nl_g32(addr[i]) != lifted[i]) same = 0;
        for (i = 0; same && i < (put1 - put0) / 4; i++) if (nl_g32(put0 + 4 * i) != pb_l[i]) same = 0;
        if (!same && s_nt_bad++ < 20)
            fprintf(stderr, "[D3D] native SetTexture(%u, %08X) differs from the lifted code (put %08X->%08X, native end %08X)\n",
                    stage, tex, put0, put1, nl_g32(dev));
        if ((s_nt_checked & 0xFFFF) == 0)
            fprintf(stderr, "[D3D] native SetTexture: %llu checked, %llu different, %llu not checked\n",
                    (unsigned long long)s_nt_checked, (unsigned long long)s_nt_bad, (unsigned long long)s_nt_skip);
        return;                                 /* (the lifted code already popped its arguments) */
    }
    nt_settexture(stage, tex, 0);
    esp += 12;                                  /* return address, stage, texture (ret 8) */
    s_nt_calls++;
}
