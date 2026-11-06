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
extern int xbox_gil_others_waiting(void);

/* PC boot test 8: RECOMP_PREEMPT was not enough. The thread runs at +2 (boot
 * log: KeSetBasePriorityThread of its KTHREAD, 2), and the guest lock only
 * flags waiters that outrank the holder, so the main thread waiting for the
 * loader got the lock about three times a second (3 fps after the logos;
 * dump: this thread the holder with g_gil_contended 0 and four waiting). So it
 * hands over whenever anyone is queued. The counter 0x002E00C4 is written
 * here and read nowhere (capstone over every function), so how fast it counts
 * does not matter. Trade-off: on the Xbox this busy loop also kept the lower
 * sound threads (0x000B3410 at +1, 0x000B3480 at -2) off the CPU while the
 * sound lock is held; here they can take a turn then. */
void sub_000B33D0(void)
{
    eax = MEM32(0x002E00DCu);
    while (eax == 0) {
        MEM32(0x002E00C4u) = MEM32(0x002E00C4u) + 1;
        if (xbox_gil_others_waiting())
            recomp_spin_yield();
        else
            RECOMP_PREEMPT();
        eax = MEM32(0x002E00DCu);
    }
    PUSH32(esp, 0xF0000001u);
    MEM32(0x002E00E0u) = 1;
    PUSH32(esp, 0x000B3408u); RECOMP_ABI_CALL(0x000DE1E4u, sub_000DE1E4);
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
void recomp_icall_fail_log(uint32_t va)
{
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
