# Pitfalls to avoid (running list)

Mistakes and wrong conclusions from this port, kept for review with Mark at the end of the session and then folded
into the port skills (switch-port-base, xbox-switch-port). Newest last. Each entry: what happened, what it cost,
the rule.

## Reading test results

1. **Check that the fix under test actually ran before reading anything else.** PC boot test 3 and 4: the
   cooperative NtSuspendThread fix only covered threads that had already made a kernel call, so the sound thread
   took the old path every time. The fix printed a "cooperative" line when it ran, and that line was missing from
   both logs; I read other things first and lost a test. Rule: every fix logs which path it took, and the first
   check on the next log is that line.

2. **Check that every test switch reached the game.** Test 3: `-Env "A=1","B=1"` through `powershell -File`
   arrives as one string, so only the first setting applied; the watchdog then exited the game at 40 s and it
   looked like a crash. Rule: the launcher echoes each switch ("test switch:" lines); confirm all of them before
   interpreting the run.

3. **One snapshot cannot tell slow from stuck.** Test 3: a single watchdog dump caught the main thread in a DCT and
   I called it progress; it was the same hang caught a moment earlier. Rule: take repeated snapshots and compare
   the icall counter and the stack between them.

4. **Read the waiting thread's own stack before blaming whoever holds a lock.** Test 8: the dump showed the idle
   thread holding the guest lock, and I blamed the 3 fps on it starving the main thread and pushed a change. The
   main thread's own stack showed it in SleepEx inside ObReferenceObjectByHandle (a 200 ms wait for an ended
   thread), not in gil_lock. The change was wrong and had a real trade-off; it was reverted. Rule: the lock holder
   is only the cause if the waiter is actually waiting on that lock.

5. **Disassemble a function before naming it.** Test 3: I called 0x000D2E60 a sprintf-style formatter from the shape
   of its caller; it was an 8x8 DCT. Rule: no label for a function until its body has been read.

## Code generation and seeds

6. **A sweep category that finds nothing is a red flag.** Kickoff: seed_sweep's immediate-reference sweep found 0,
   because capstone's one-pass section sweep stops at the first undecodable byte and the address regex wanted 6+
   hex digits (this XBE's code is below 0x100000). Rule: check each sweep against a known positive (here, D3D's
   DPC, whose address is pushed) before trusting an empty result.

7. **Seed clean-start tail-jump targets up front.** Kickoff: I left the tail-jump targets with clean starts out as
   "probably rarely run". 0x00036180 (test 1) and 0x000C5EE0 (test 7) then ran as empty stubs and each cost a test.
   Rule: aligned, after padding, a clean prologue, inside no known function: seed it at kickoff.

8. **Stubs must not be silent.** Kickoff: xboxrecomp's unresolved-call stubs returned without a trace, so skipped
   game code was invisible. Rule: make every stub log once (done in this repo's translator) before the first run.

13. **"Inside another function" is not a reason to drop a candidate start; check what precedes it.** Kickoff: I
    left out 0x00071540 and 0x000C1BE0 because the translator put them inside other functions. Both follow a ret
    and NOP padding, so the containers were simply measured too long. 0x00071540 then ran as a stub right after
    Start (test 9, a white screen). Rule: a start after `ret; padding` is a function whatever the container says
    (tools/retpad_sweep.py), while a start reached by a jump from just above it (0x00049100) is not.

## Runtime

9. **Assume a title may suspend another thread, not only itself.** Test 2: the Windows path froze a thread with a
   real SuspendThread while it could hold the guest lock (the caller had released it for the kernel call). Sonic
   Heroes only ever suspended itself, so it never showed. Rule: suspension of another thread is cooperative.

10. **Check how the runtime treats threads that have ended.** Test 8: an ended thread was dropped from the thread
    table, so asking for its object waited 200 ms and returned 0; XAPI's GetExitCodeThread then reported
    STILL_ACTIVE forever.

11. **A busy loop with no call never hands over the guest lock.** Test 6: the idle thread's counting loop held the
    lock for good, and the translator's poll-loop marking skipped it because it counts instead of only polling.
    Rule: look for call-free loops in thread entry functions.

## Repo hygiene

12. **Scope global renames.** Kickoff: a global "Sonic Heroes" to "Crazy Taxi 3" replace in CLAUDE.md also
    rewrote the lines saying where the repo came from. Rule: rename paths and identifiers only, then read the
    diff.
