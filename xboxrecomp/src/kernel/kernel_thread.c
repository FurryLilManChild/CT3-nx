/*
 * kernel_thread.c - Xbox Threading Subsystem
 *
 * Implements Xbox thread creation, termination, delays, and priority
 * management using Win32 threading APIs.
 *
 * Xbox threading model:
 *   - PsCreateSystemThreadEx creates kernel-mode threads (→ CreateThread)
 *   - Thread start routines are __stdcall with a single PVOID context
 *   - Time intervals use NT 100-nanosecond units (negative = relative)
 *   - Thread priorities use NT KPRIORITY increments
 */

#include <stdio.h>
#include "kernel.h"
#include "xbox_memory_layout.h"   /* XBOX_WORKER_STACK_* + worker-stack decls */

/* ============================================================================
 * Thread Start Wrapper
 *
 * Xbox start routines are __stdcall void(*)(PVOID), but Win32 CreateThread
 * expects DWORD WINAPI (*)(LPVOID). We wrap the Xbox routine to bridge
 * the calling convention and return type.
 * ============================================================================ */

typedef struct _XBOX_THREAD_START_INFO {
    PXBOX_SYSTEM_ROUTINE StartRoutine;
    PVOID                StartContext;
} XBOX_THREAD_START_INFO;

static DWORD WINAPI xbox_thread_wrapper(LPVOID lpParameter)
{
    XBOX_THREAD_START_INFO info = *(XBOX_THREAD_START_INFO*)lpParameter;

    /* Free the start info before calling the routine - the routine may
     * never return (calling PsTerminateSystemThread instead) */
    HeapFree(GetProcessHeap(), 0, lpParameter);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD, "Thread %u starting at %p",
        GetCurrentThreadId(), info.StartRoutine);

    info.StartRoutine(info.StartContext);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD, "Thread %u returned normally",
        GetCurrentThreadId());

    return 0;
}

/* ============================================================================
 * PsCreateSystemThreadEx
 *
 * Xbox signature:
 *   PsCreateSystemThreadEx(
 *     OUT PHANDLE ThreadHandle,
 *     IN ULONG ThreadExtraSize,      // extra bytes in thread object (ignored)
 *     IN ULONG KernelStackSize,      // stack size (0 = default)
 *     IN ULONG TlsDataSize,          // TLS data size (Xbox-specific, ignored)
 *     OUT PULONG ThreadId,           // optional thread ID
 *     IN PVOID StartContext1,        // context passed to StartRoutine
 *     IN PVOID StartContext2,        // alternate context (unused by game code)
 *     IN BOOLEAN CreateSuspended,
 *     IN BOOLEAN DebugStack,         // debug stack (ignored)
 *     IN PXBOX_SYSTEM_ROUTINE StartRoutine
 *   )
 *
 * Maps to: CreateThread with a wrapper for calling convention adaptation.
 * ============================================================================ */

NTSTATUS __stdcall xbox_PsCreateSystemThreadEx(
    PHANDLE ThreadHandle,
    ULONG ThreadExtraSize,
    ULONG KernelStackSize,
    ULONG TlsDataSize,
    PULONG ThreadId,
    PVOID StartContext1,
    PVOID StartContext2,
    BOOLEAN CreateSuspended,
    BOOLEAN DebugStack,
    PXBOX_SYSTEM_ROUTINE StartRoutine)
{
    XBOX_THREAD_START_INFO* info;
    HANDLE hThread;
    DWORD dwThreadId;
    DWORD dwCreationFlags;

    (void)ThreadExtraSize;
    (void)TlsDataSize;
    (void)StartContext2;
    (void)DebugStack;

    if (!ThreadHandle || !StartRoutine)
        return STATUS_INVALID_PARAMETER;

    /* Allocate start info - freed by the wrapper thread */
    info = (XBOX_THREAD_START_INFO*)HeapAlloc(GetProcessHeap(), 0, sizeof(XBOX_THREAD_START_INFO));
    if (!info)
        return STATUS_NO_MEMORY;

    info->StartRoutine = StartRoutine;
    info->StartContext = StartContext1;

    dwCreationFlags = CreateSuspended ? CREATE_SUSPENDED : 0;

    /* Use default stack size if 0 (Xbox default is 64KB) */
    if (KernelStackSize == 0)
        KernelStackSize = 65536;

    hThread = CreateThread(NULL, KernelStackSize, xbox_thread_wrapper, info,
                           dwCreationFlags, &dwThreadId);
    if (!hThread) {
        HeapFree(GetProcessHeap(), 0, info);
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "PsCreateSystemThreadEx: CreateThread failed (error %u)", GetLastError());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *ThreadHandle = hThread;
    if (ThreadId)
        *ThreadId = dwThreadId;

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_THREAD,
        "PsCreateSystemThreadEx: created thread %u (handle=%p, routine=%p, suspended=%d)",
        dwThreadId, hThread, StartRoutine, CreateSuspended);

    return STATUS_SUCCESS;
}

/* ============================================================================
 * PsTerminateSystemThread
 *
 * Terminates the calling thread. On Xbox this is the standard way for
 * system threads to exit. Maps directly to ExitThread.
 * ============================================================================ */

NTSTATUS __stdcall xbox_PsTerminateSystemThread(NTSTATUS ExitStatus)
{
    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "PsTerminateSystemThread: thread %u exiting with status 0x%08X",
        GetCurrentThreadId(), ExitStatus);

    ExitThread((DWORD)ExitStatus);

    /* ExitThread never returns, but the compiler needs this */
    return STATUS_SUCCESS;
}

/* ============================================================================
 * KeDelayExecutionThread
 *
 * Delays the current thread. The interval uses NT 100-nanosecond units:
 *   - Negative values = relative delay (most common)
 *   - Positive values = absolute time (rare)
 *   - Zero = yield
 *
 * Maps to: SleepEx (for alertable waits) or Sleep
 * ============================================================================ */

NTSTATUS __stdcall xbox_KeDelayExecutionThread(
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Interval)
{
    DWORD ms;

    (void)WaitMode;

    if (!Interval)
        return STATUS_INVALID_PARAMETER;

    if (Interval->QuadPart == 0) {
        /* Zero interval = yield the thread's time slice */
        SwitchToThread();
        return STATUS_SUCCESS;
    }

    if (Interval->QuadPart < 0) {
        /* Negative = relative time in 100ns units. Convert to milliseconds. */
        LONGLONG relative_100ns = -Interval->QuadPart;
        ms = (DWORD)(relative_100ns / 10000);
        /* Ensure at least 1ms for very short intervals */
        if (ms == 0 && relative_100ns > 0)
            ms = 1;
    } else {
        /* Positive = absolute time. Calculate relative delay from now. */
        LARGE_INTEGER now;
        GetSystemTimeAsFileTime((LPFILETIME)&now);
        LONGLONG diff = Interval->QuadPart - now.QuadPart;
        if (diff <= 0)
            return STATUS_SUCCESS; /* Already past */
        ms = (DWORD)(diff / 10000);
    }

    xbox_kernel_busy(-1);               /* sleeping holds nothing: a safe point */
    if (Alertable) {
        DWORD result = SleepEx(ms, TRUE);
        xbox_kernel_busy(1);
        if (result == WAIT_IO_COMPLETION)
            return STATUS_ALERTED;
    } else {
        Sleep(ms);
        xbox_kernel_busy(1);
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * Thread Priority
 *
 * Xbox uses NT KPRIORITY base priority increments relative to the process.
 * We map these to Win32 thread priority levels.
 * ============================================================================ */

/*
 * Map Xbox priority increment to Win32 priority level.
 * Xbox base priorities typically range from -2 to +2 for game threads.
 */
static int xbox_priority_to_win32(LONG increment)
{
    if (increment <= -15)       return THREAD_PRIORITY_IDLE;
    else if (increment <= -2)   return THREAD_PRIORITY_LOWEST;
    else if (increment == -1)   return THREAD_PRIORITY_BELOW_NORMAL;
    else if (increment == 0)    return THREAD_PRIORITY_NORMAL;
    else if (increment == 1)    return THREAD_PRIORITY_ABOVE_NORMAL;
    else if (increment <= 2)    return THREAD_PRIORITY_HIGHEST;
    else                        return THREAD_PRIORITY_TIME_CRITICAL;
}

static LONG win32_priority_to_xbox(int priority)
{
    switch (priority) {
        case THREAD_PRIORITY_IDLE:          return -15;
        case THREAD_PRIORITY_LOWEST:        return -2;
        case THREAD_PRIORITY_BELOW_NORMAL:  return -1;
        case THREAD_PRIORITY_NORMAL:        return 0;
        case THREAD_PRIORITY_ABOVE_NORMAL:  return 1;
        case THREAD_PRIORITY_HIGHEST:       return 2;
        case THREAD_PRIORITY_TIME_CRITICAL: return 15;
        default:                            return 0;
    }
}

LONG __stdcall xbox_KeSetBasePriorityThread(PVOID Thread, LONG Increment)
{
    HANDLE hThread = (HANDLE)Thread;
    LONG previous;

    /* Get previous priority before setting new one */
    int prev_win32 = GetThreadPriority(hThread);
    previous = win32_priority_to_xbox(prev_win32);

    int new_win32 = xbox_priority_to_win32(Increment);
    SetThreadPriority(hThread, new_win32);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "KeSetBasePriorityThread: thread=%p, increment=%d (win32=%d), prev=%d",
        Thread, Increment, new_win32, previous);

    return previous;
}

LONG __stdcall xbox_KeQueryBasePriorityThread(PVOID Thread)
{
    HANDLE hThread = (HANDLE)Thread;
    int win32_priority = GetThreadPriority(hThread);
    return win32_priority_to_xbox(win32_priority);
}

/* ============================================================================
 * KeAlertThread
 *
 * Sends an alert to a thread, which can wake it from an alertable wait.
 * On Xbox, this sets the alerted flag on the thread object.
 * We approximate this with QueueUserAPC using a no-op APC routine.
 * ============================================================================ */

static VOID CALLBACK xbox_alert_apc(ULONG_PTR dwParam)
{
    (void)dwParam;
    /* No-op - the purpose is just to wake the thread from alertable wait */
}

NTSTATUS __stdcall xbox_KeAlertThread(PVOID Thread, KPROCESSOR_MODE AlertMode)
{
    HANDLE hThread = (HANDLE)Thread;

    (void)AlertMode;

    if (!QueueUserAPC(xbox_alert_apc, hThread, 0)) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_THREAD,
            "KeAlertThread: QueueUserAPC failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtYieldExecution
 *
 * Yields the current thread's remaining time slice.
 * Maps directly to SwitchToThread.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtYieldExecution(void)
{
    SwitchToThread();
    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtDuplicateObject
 *
 * Duplicates a kernel handle. On Xbox this is simpler than Win32 since
 * there's only one process. Maps to DuplicateHandle within the same process.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtDuplicateObject(
    HANDLE SourceHandle,
    PHANDLE TargetHandle,
    ULONG Options)
{
    HANDLE hProcess = GetCurrentProcess();
    DWORD dwOptions = 0;

    if (!TargetHandle)
        return STATUS_INVALID_PARAMETER;

    /* Xbox DUPLICATE_CLOSE_SOURCE = 0x1, same as Win32 */
    if (Options & 0x1)
        dwOptions |= DUPLICATE_CLOSE_SOURCE;
    /* Xbox DUPLICATE_SAME_ACCESS = 0x2, same as Win32 */
    if (Options & 0x2)
        dwOptions |= DUPLICATE_SAME_ACCESS;

    if (!DuplicateHandle(hProcess, SourceHandle, hProcess, TargetHandle,
                         0, FALSE, dwOptions)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtDuplicateObject: DuplicateHandle failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtDuplicateObject: source=%p → target=%p (options=0x%X)",
        SourceHandle, *TargetHandle, Options);

    return STATUS_SUCCESS;
}

/* Safe points for NtSuspendThread.
 *
 * On the Xbox a suspended thread stops in title code or in a kernel wait; the
 * kernel never leaves one frozen holding a kernel lock. Here a host
 * SuspendThread can land anywhere, including inside a bridge call holding a
 * bridge lock, the CRT's stderr lock or the heap lock -- and then every other
 * thread that needs that lock stops too. CRI's movie/stream scheduler suspends
 * and resumes its threads constantly, and this froze the game at boot and
 * mid-level.
 *
 * Each thread counts how deep it is in the kernel (kernel_thunk_dispatch), and
 * the host waits step out of the count while they block (they hold nothing).
 * NtSuspendThread suspends, waits for the suspension to take effect, and if
 * the thread is inside the kernel resumes it, yields and tries again, so it
 * returns with the thread stopped at a safe point, as on hardware.
 *
 * ponytail: a fixed table keyed by host thread id, linear probe; 256 threads
 * is far past what a title creates. Gives up after 100 ms and suspends anyway.
 */
#define KBUSY_SLOTS 256
typedef struct {
    volatile LONG tid;
    volatile LONG busy;          /* kernel depth + raised IRQL */
    volatile LONG coop;          /* CT3-nx: suspend count set by another thread */
    HANDLE volatile coop_ev;     /* set by NtResumeThread */
} KThreadSlot;
static KThreadSlot g_kbusy[KBUSY_SLOTS];
static XBOX_THREAD_LOCAL KThreadSlot *t_kslot;

static KThreadSlot *kbusy_slot(DWORD tid, int create)
{
    unsigned i, h = (tid >> 2) % KBUSY_SLOTS;
    for (i = 0; i < KBUSY_SLOTS; i++) {
        unsigned s = (h + i) % KBUSY_SLOTS;
        if ((DWORD)g_kbusy[s].tid == tid)
            return &g_kbusy[s];
        if (!g_kbusy[s].tid) {
            if (!create)
                return NULL;
            if (InterlockedCompareExchange(&g_kbusy[s].tid, (LONG)tid, 0) == 0)
                return &g_kbusy[s];
            if ((DWORD)g_kbusy[s].tid == tid)
                return &g_kbusy[s];
        }
    }
    return NULL;
}

void xbox_kernel_busy(int delta)
{
    if (!t_kslot)
        t_kslot = kbusy_slot(GetCurrentThreadId(), 1);
    if (t_kslot)
        t_kslot->busy += delta;
}

/* CT3-nx: suspension of another guest thread is cooperative.
 *
 * The kernel call lets go of the guest lock, so the thread being suspended
 * may be holding it (it took it while this one was in the kernel) or waiting
 * for its ticket. Frozen by a real SuspendThread there, it never lets go, and
 * the caller waits for the lock forever: Crazy Taxi 3's sound lock (CRI's,
 * 0x000B32F0 and 0x000B3330) suspends and resumes its server thread around
 * every locked section, and the PC boot test hung with the main thread in
 * that call (watchdog, 2026-11-05). So the suspend count is kept here and
 * the thread stops itself at xbox_thread_suspend_point, which the guest lock
 * calls just before taking the lock back after a kernel call, a poll-loop
 * yield or a preemption: a point where it holds neither the lock nor a
 * ticket, and is not in the kernel. As on the Xbox, where suspension is an
 * APC, the call returns before the thread has stopped; a thread that is
 * running stops at its next such point. A thread suspending itself, or one
 * that has never called the kernel (no slot: created suspended), takes the
 * old path. */
static volatile LONG g_coop_any;   /* cooperative suspensions outstanding, all threads */

void xbox_thread_suspend_point(void)
{
    KThreadSlot *k;

    if (!g_coop_any)
        return;
    /* The suspender makes the slot, so a thread that has never been in the
     * kernel (no t_kslot yet) still finds its count. */
    if (!t_kslot)
        t_kslot = kbusy_slot(GetCurrentThreadId(), 0);
    k = t_kslot;
    if (!k || k->coop <= 0)
        return;
    {   /* CT3-nx: stopping here holds nothing (RECOMP_GIL_PRIO) */
        extern void xbox_gil_resv_drop(int why);
        xbox_gil_resv_drop(5);
    }
    while (k->coop > 0)
        WaitForSingleObject(k->coop_ev, 5);
}

NTSTATUS __stdcall xbox_NtSuspendThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev, tid = GetThreadId(ThreadHandle);
    KThreadSlot *k = tid != GetCurrentThreadId() ? kbusy_slot(tid, 0) : NULL;
    ULONGLONG give_up = GetTickCount64() + 100;

    /* CT3-nx: every other live thread, not only those that have been in the
     * kernel: CT3's sound server is suspended again before its first kernel
     * call, and the slot-less path below froze it holding the guest lock
     * (PC boot test 4: the same hang). */
    if (!k && tid && tid != GetCurrentThreadId())
        k = kbusy_slot(tid, 1);
    if (k) {
        if (!k->coop_ev) {
            HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
            if (InterlockedCompareExchangePointer((PVOID volatile *)&k->coop_ev, ev, NULL) != NULL)
                CloseHandle(ev);
        }
        prev = (DWORD)(InterlockedIncrement(&k->coop) - 1);
        InterlockedIncrement(&g_coop_any);
        {
            /* CT3-nx: on the Xbox a suspended thread stops at once. Here it
             * stops at its next suspend point; if it holds the guest lock and
             * runs guest code without kernel calls, that point only comes at a
             * function-entry yield, so ask for one (test 8b: the CRI sound
             * idle thread kept running after the lock's unlock suspended it). */
            extern volatile int g_gil_contended;
            g_gil_contended = 1;
        }
        {
            static unsigned shown;
            if (shown++ < 3)
                fprintf(stderr, "  [KERNEL] NtSuspendThread: tid %lu suspends tid %lu (cooperative)\n",
                        GetCurrentThreadId(), tid);
        }
        if (PreviousSuspendCount)
            *PreviousSuspendCount = (ULONG)prev;
        return STATUS_SUCCESS;
    }

#ifndef _WIN32
    /* CT3-nx: a thread suspending itself blocks until it is resumed, as on
     * the Xbox (see w32_wait_while_suspended), with the guest lock released. */
    if (tid == GetCurrentThreadId()) {
        extern int xbox_gil_suspend(void);
        extern void xbox_gil_resume(int depth);
        int d;
        prev = SuspendThread(ThreadHandle);
        if (prev == (DWORD)-1)
            return STATUS_UNSUCCESSFUL;
        d = xbox_gil_suspend();
        {
            extern void xbox_gil_resv_drop(int why);
            xbox_gil_resv_drop(5);
        }
        w32_wait_while_suspended(ThreadHandle);
        xbox_gil_resume(d);
        if (PreviousSuspendCount)
            *PreviousSuspendCount = (ULONG)prev;
        return STATUS_SUCCESS;
    }
#endif

    {
        static unsigned shown;
        if (shown++ < 3)
            fprintf(stderr, "  [KERNEL] NtSuspendThread: handle %p tid %lu, real suspension\n",
                    ThreadHandle, tid);
    }

    /*
     * SuspendThread returns the previous suspend count, or (DWORD)-1 on
     * failure. The Xbox call reports that count through an out-parameter, so
     * the two are not interchangeable: -1 must become an error status rather
     * than a suspend count of 0xFFFFFFFF.
     */
    for (;;) {
#ifdef _WIN32
        CONTEXT c;
#endif
        prev = SuspendThread(ThreadHandle);
        if (prev == (DWORD)-1) {
            xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
                "NtSuspendThread: SuspendThread failed (error %u)", GetLastError());
            return STATUS_UNSUCCESSFUL;
        }
        if (prev || !k)
            break;                      /* already stopped, or never in the kernel */
#ifdef _WIN32
        c.ContextFlags = CONTEXT_CONTROL;
        GetThreadContext(ThreadHandle, &c);   /* returns once it has really stopped */
#endif
        if (k->busy <= 0 || GetTickCount64() > give_up)
            break;
        ResumeThread(ThreadHandle);
        SwitchToThread();
    }

    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtSuspendThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtResumeThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev;
    DWORD tid = GetThreadId(ThreadHandle);
    KThreadSlot *k = tid != GetCurrentThreadId() ? kbusy_slot(tid, 0) : NULL;

    /* CT3-nx: the cooperative suspensions first (see xbox_thread_suspend_point). */
    if (k && k->coop > 0) {
        prev = (DWORD)InterlockedDecrement(&k->coop) + 1;
        InterlockedDecrement(&g_coop_any);
        if (k->coop_ev)
            SetEvent(k->coop_ev);
        if (PreviousSuspendCount)
            *PreviousSuspendCount = (ULONG)prev;
        return STATUS_SUCCESS;
    }

    /* Mirror of NtSuspendThread: (DWORD)-1 is failure, not a count. */
    prev = ResumeThread(ThreadHandle);
    if (prev == (DWORD)-1) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtResumeThread: ResumeThread failed (error %u)", GetLastError());
        {   /* diagnostic: which handle, and is it a thread at all */
            static int shown;
            if (shown++ < 10)
                fprintf(stderr, "  [KERNEL] NtResumeThread(%p) failed: error %lu, tid %lu\n",
                        ThreadHandle, GetLastError(), GetThreadId(ThreadHandle));
        }
        return STATUS_UNSUCCESSFUL;
    }
    {   /* diagnostic: a resume loop that never wakes anything */
        static XBOX_THREAD_LOCAL HANDLE last;
        static XBOX_THREAD_LOCAL unsigned idle;
        if (prev == 0 && ThreadHandle == last) {
            if (++idle == 20000)
                fprintf(stderr, "  [KERNEL] NtResumeThread: tid %lu resumes tid %lu (handle %p)"
                                " 20000 times; it is never suspended\n",
                        GetCurrentThreadId(), GetThreadId(ThreadHandle), ThreadHandle);
        } else {
            idle = 0;
            last = ThreadHandle;
        }
    }
    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtResumeThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}


/* ================================================================
 * Worker stack slices + game-thread tracking (host-tick-driven titles)
 * ================================================================
 *
 * Ported from the Burnout 3 fork as the runtimes reunite. A host-driven title
 * returns from its entry after spawning an init thread and expects the host's
 * own thread to drive the per-frame tick; to call recompiled code from there it
 * needs a guest stack, which a worker slice provides. Additive: a default-model
 * title never calls any of this, so it is inert for Halo, Crimson Skies, etc.
 * See docs/technical/burnout3-reunification.md and XBOX_WORKER_STACK_* in
 * xbox_memory_layout.h.
 */

/* The game's own thread, kept so a wedged boot can be inspected from the host
 * watchdog. Set when a title's game thread is spawned; NULL under the default
 * inline model, where there is no separate thread to sample. */
static HANDLE g_game_thread = NULL;

void  xbox_set_game_thread(void *h) { g_game_thread = (HANDLE)h; }
void *xbox_thread_debug_handle(void) { return (void *)g_game_thread; }

/* One bit per worker stack slice. Interlocked because a host-driven title can
 * allocate a slice from the host thread while recompiled code allocates one for
 * a spawned worker, so the allocator itself must be thread-safe. */
static volatile LONG g_worker_stack_used[XBOX_WORKER_STACK_COUNT];

int xbox_worker_stack_alloc(void)
{
    int i;
    for (i = 0; i < XBOX_WORKER_STACK_COUNT; i++) {
        if (InterlockedCompareExchange(&g_worker_stack_used[i], 1, 0) == 0)
            return i;
    }
    return -1;  /* all slices in use */
}

void xbox_worker_stack_free(int slot)
{
    if (slot >= 0 && slot < XBOX_WORKER_STACK_COUNT)
        InterlockedExchange(&g_worker_stack_used[slot], 0);
}
