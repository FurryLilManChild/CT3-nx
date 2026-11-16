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

14. **Get the error code before naming a cause.** Test 10: the failed reads' offsets were all multiples of 512 but
    not 4096, so I blamed unbuffered I/O and shipped a fix. The Win32 error was already being logged, as a warning,
    but to out/pc/xbox_kernel.log, not the game log; it said error 6, invalid handle (the handle was -1): a cache
    file the game never managed to open, because rename was unimplemented. A pattern that fits is not evidence.
    Rule: find the real error first, and know where every log goes (the game log and xbox_kernel.log).

15. **Check how a runtime variable is kept up to date before using it as a clock.** Test 12 fix: I first timed
    the CRI lock's give-up with the host's xbox_KeTickCount, which kernel_hal.c only updates when its own getter
    runs, so the timeout could never have fired. Caught at the compile check. Rule: read the writer of any value
    used for timing; here the live tick is the guest's KeTickCount at kernel data + 0x20.

16. **Check what the game actually asked to draw before calling something missing.** Test 12: from the
    screenshot I listed "D-pad and A button icons missing" in the text. The frame trace shows the plain letter D
    drawn from the font, and the font holds no button pictures. Rule: a "missing" element needs a trace showing a
    draw that failed or was skipped; otherwise ask how the original looks.

17. **Model every thread a lock involves before writing an exclusion.** Test 13: I wrote the CRI exclusion with
    one shared "current pass" and owner, thinking of "the server" as one thread. There are two, and their passes
    overlap on the Xbox, so the servers waited on each other 100 ms at a time and the logos crawled. Rule: list
    the threads on each side (here, both servers and the main thread), what the original lets overlap, keep
    nesting per thread, and give any new wait a counter so its cost shows in the log.

18. **Every stub line right before a crash is a suspect, whatever its "entered from" says.** Test 10: I waved
    off `[STUB] unresolved 0x000800AF entered from 0x002B4CC0` as a call through a garbage pointer, because the
    return address was in .data. It was the cause: a function cut in half, its branch to the epilogue sent to a
    stub. It cost tests 11 to 14 their crash diagnosis. Rule: disassemble every stubbed address seen in a run and
    say what it is (here, two seconds of work: 0x000800AF is `pop ebx; add esp, 0x18; ret`).

19. **Know the resolution of the clock a short timeout uses.** Test 18 fix: the lock handover waited "2 ms"
    with GetTickCount64, which advances in 15.6 ms steps on Windows, so the wait was anything from 0 to 16 ms.
    Rule: sub-frame timeouts use the performance counter.

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

21. **Rebrand every generated asset at kickoff, not only the code.** The Switch icon and loading-screen logo
    (tools/make_brand.py, switch/icon.jpg, src/ct3_logo.h) still said SONIC HEROES until the first Switch build.
    Caught before shipping. Rule: grep the copied repo for the old game's name in tools and assets too, and
    regenerate their outputs.

22. **A syntax check with -w hides the errors the Switch compiler makes.** The lock handover's `volatile long`
    counter passed my Windows and Linux checks (both with -w) and failed the Switch build at step 35: on the
    Switch LONG is 32-bit, and GCC 16 makes an incompatible pointer an error. Rule: check changed runtime files
    with -Werror=incompatible-pointer-types at least, and use LONG with Interlocked*.

23. **A fix that fits one log is not proven until the failure comes back without it.** Switch test 4: I blamed
    the CRI lock's 100 ms give-ups for the SJ ring crash because a give-up had happened earlier in the run, and
    raised the limit. Test 6 crashed the same way twice with no give-up at all. Rule: when a log shows the
    suspected trigger only somewhere in the run, say "possible", and look for a mechanism that is present in
    every failing run (here: pre-emption inside the server pass).

24. **"Like the console" has to mean every rule, not the main one.** Switch test 7: the priority guest lock copied
    the Xbox's "highest priority runs", but every kernel call still handed the lock to whoever waited, lower
    threads included, and a 50 ms rule of mine could force a yield. Neither is on the Xbox, and the sound pass
    was entered 8 times (quitting a shift: the same SJ ring crash). Mark asked why we did not simply do what the
    Xbox does. Rule: when copying console behaviour, list each place the port still differs and say why; a
    safety net should log, not change what the game sees, unless the alternative is a certain hang.

25. **Before removing a safety net, read what it was rescuing.** Switch test 8 removed the 50 ms rule and went to a
    black screen. Test 7's own log had the answer: the 50 ms rule kept letting the main thread in at
    "priority 0" while it held the CRI sound lock, which raises it to 16. On the Switch the main thread had no
    thread object, so priority changes to it did nothing (win32_compat.c), and the sound idle thread at +2 then
    kept it out for good. Rule: when a fallback fires, explain each firing before taking it away.

26. **A new rule has to be enforced on every path.** Test 8's reservation was checked when the lock was handed
    over, but not on the fast path (lock free, nobody queued), so a lower thread walked straight into the sound
    pass. Rule: when adding a condition to a lock, list every place the lock can be taken and check each.

27. **Making a stop point stricter changes where threads stop.** Test 8d made a suspended thread stop as soon as
    it got the guest lock back, which is right in general, but that point can be the return from
    RtlEnterCriticalSection: the CRI idle thread stopped holding XAPI's thread-startup lock and two other threads
    waited on it for good (test 8e, black screen). Rule: a cooperative stop point must not be inside a guest lock.

28. **The cloud boot test passing does not mean the Switch boots.** 8f reached the title in the cloud and hung on
    the Switch: a race between threads that depends on how fast a host thread wakes. Rule: tell Mark plainly that
    the cloud only catches deterministic hangs; timing races still need his Switch.

29. **Include what you call.** kernel_thread.c used getenv without <stdlib.h>; C took it as returning int and
    cut the pointer, which crashed only when the variable was set. Rule: build changed files with
    -Werror=implicit-function-declaration (the Linux build shows the warning; read it).

30. **Read the code before describing it to Mark.** I told him regs_changed compared all 2,048 GPU registers on
    every draw; it already skips unchanged 64-byte blocks. Rule: name a cost from the profile and the code, not
    from the function's name.

## Working with Mark

20. **Confirm a new route works before telling Mark to drop the old one.** Switch toolchain: I asked for a
    devkitPro zip, then told him to skip it for a downloaded toolchain image before checking I could use it; the
    sandbox blocked running it, and I had to ask for the zip again. He got contradictory messages. Rule: try the
    new route first, then change the plan in one message.

## Repo hygiene

12. **Scope global renames.** Kickoff: a global "Sonic Heroes" to "Crazy Taxi 3" replace in CLAUDE.md also
    rewrote the lines saying where the repo came from. Rule: rename paths and identifiers only, then read the
    diff.

31. **Keep asking whether we are still doing native static recompilation.** (Mark, 2026-11-08.) The goal is code
    that runs natively on the Switch, not an emulation of the Xbox doing the work. Along the way, ask again and
    again: are we doing this because upstream (burnout3-sw, xboxrecomp) does it, and is that keeping us from the
    speed we need? Are we off the path of true native recompilation? Here the renderer read back the Xbox GPU
    command stream the game had just written (upstream's design), the x87 maths ran through an in-memory stack
    and a per-operation precision check, and it took a profile and Mark's push to look at either. Upstream
    informs and is there to leverage; we choose the best path for each project.


32. **Check a chained build's exit status before trusting what follows it.** (2026-11-08.) The first Switch
    build of test 10a failed (the new [PBCALL] counter in nv2a_pb_scan.c called GetTickCount64, which only the
    Linux build declares there), but the chained commands after it ran anyway: they listed the old NRO and
    copied the old ELF as out/elf/ct3_10a.elf. Rule: chain the steps after a build with `&&`, and compare the
    NRO's time with the build's start before delivering it.

33. **Measure what a cache costs, not just how often it hits.** (Switch test 10a, 2026-11-08.) The light setup
    memo was checked in the cloud for hits (81%) and exactness (0 mismatches), and I expected 1 to 2 ms a frame
    from it. On the Switch, building and hashing its key (up to about 1 KB read word by word through the
    MMIO-checked accessor, then hashed byte by byte) cost 8.2% of the main thread against about 10% for the
    original light setup: well under 1 ms saved. Rule: before promising a gain from a cache, time the lookup
    itself against the work it replaces.

34. **Separate movable work from waits before estimating a split.** (Switch test 10c, 2026-11-08.) I counted all
    samples under the Vulkan driver (about 16% of the graphics thread) as work a recording thread would take
    away, and estimated the graphics thread at about 15 ms. But part of that 16% was fence waits, swapchain
    acquire and present, which stay on the renderer; the queue added about 3 to 4% of its own; and with the
    recording gone, the renderer then waited longer on the GPU fence (8% of its samples, from 3%). The heaviest
    scenes stayed near 30 fps. Rule: sum only the samples that actually move, subtract the cost of the new
    mechanism, and measure GPU time before assuming the CPU is the only limit.

35. **Keep to the agreed plan, or say so when changing it.** (2026-11-08.) Mark chose the Direct3D path (native
    replacements for D3D's entry points, docs/d3d-path.md). After step 1 I moved to trimming the existing
    NV2A renderer (10b, 10c) without saying that this was a change of direction. The trims helped, but the
    profile shows Direct3D is still about a third of the main thread's work, untouched.

36. **A technique's reported speedup is relative to its own baseline.** (Switch test 12a, 2026-11-09.)
    OpenTrickyNX reports about 3x for guest registers in CPU registers, against a translator that kept every
    register in thread-local memory. Ours already kept them in C locals inside a function. I first quoted their
    figure, then corrected it to 1.25 to 1.4x; the measured gain on lifted code was about 1.15x (31.8% of main
    thread samples against 36.6%). Rule: before quoting another port's gain, compare its baseline with ours, and
    give the estimate as a range with what it rests on.

## XboxXBE-Recomp move (branch xbe-recomp)

37. **Name an XDK function by what it does, not by what it is called near.** (2026-11-14.) I named 0xFE7D0
    CDevice_KickOff because BeginPush and a tiny wrapper call it before pushing. XbSymbolDatabase put KickOff at
    0xF8520 instead, and DrawIndexedVertices passes it the base vertex: 0xFE7D0 is the per-draw state flush (the
    same role as 5659's 0x334FF0). Had the name stood, the runtime's no-op KickOff HLE would have replaced the flush.
    Rule: before naming by hand, read the function's arguments at two or more call sites and what it writes, and
    cross-check against an independent source (a signature database, the same function in another XDK build).

38. **Check a parser of someone else's data format against its definition file first.** (2026-11-14.) My reader of
    XbSymbolDatabase's signatures took only the `{offset, byte}` pairs and missed the `OV_MATCH(offset, bytes...)`
    macro, which most of the 4531 signatures use; a signature left with only its few pairs matched the wrong
    function (0xFE7D0 again). Rule: list every macro the data uses (`grep -o` over the files) and handle each, or
    reject entries that use one the parser does not know.

39. **No `rm -rf` with a glob after a `cd`.** (2026-11-14.) Clearing the new worktree with `cd dir; rm -rf ./*` was
    blocked by the safety check, rightly: if the `cd` failed it would have emptied the wrong folder. `git rm -r` in
    the worktree did the same job safely.

40. **The list of names the runtime needs is every SYM_ it uses, not the nearest build's signatures file.** (2026-11-14.)
    I built CT3's game.toml `[symbols]` from 5659's signatures.toml. Sonic Heroes had named IDirectSoundBuffer_SetConeAngles,
    SetEG and SetPitch by hand in its game.toml, so they were never in that file, and CT3's game.toml left them out even
    though their addresses were known (SetEG was in my own notes). The first PC boot crashed in SetConeAngles' original
    code. Rule: take the names from the runtime itself (every `SYM(x)`, `HAS_x`, `HLE_*(x)`, `BUFFER(x)` in sdk/runtime,
    switch and game/src) and check each one against game.toml.

41. **Moving a port to a new runtime: go through the old port's fixes first.** (2026-11-14.) The city bank rename
    that kept CT3's city from loading was found and fixed on main on 2026-11-05 (play test 11); on the new runtime
    it cost a test round to find again. Rule: before the first boot on a new runtime, list every runtime-level fix
    in the old port's progress log (files, threads, timing, middleware) and check each one against the new runtime.

42. **Don't assume the SD card is FAT32.** (2026-11-14.) I told Mark his 4.7 GB disc image could not go on the card
    and steered him to the extracted disc; his card takes files over 4 GB. On this runtime the disc image is the
    faster setup on the Switch (one host file, no per-file opens: Sonic Heroes' fix). Rule: recommend the disc image
    first; mention the 4 GB limit only as a fallback for a FAT32 card.

43. **A weak database signature can name the wrong function.** (2026-11-14.) XbSymbolDatabase's 3911 XNetStartup
    signature is 8 byte pairs with no xrefs. It matched CT3's Sleep (0x0E0BB7), whose few bytes look alike, and the
    runtime's XNetStartup HLE replaced every Sleep with an instant return: a sound thread spun a whole Switch core and
    the PC hid it (more cores). Rule: treat any matched signature with 12 or fewer pairs and no xrefs as unproven;
    check its callers and disassembly before keeping the name, and on the first Switch profile look for guest threads
    that never sleep.

44. **Read the runtime's waits before blaming the renderer.** (2026-11-15.) Two Switch rounds went to the render
    thread while the title thread was also sleeping a flat 16 ms a frame in an upstream BlockUntilVerticalBlank HLE
    (fine for games that never call it). The first profile already showed the title thread asleep in it. Rule: on
    every profile, list where the title thread sleeps and check each wait's HLE against what the console does.

45. **Count before naming a cause in a build.** (2026-11-15.) Build 5 stopped buffer locks from moving the texture
    dirty epoch, on the guess that they were why only 60% of texture stages were remembered. The counter added in
    the same build showed 0 to 0.1 buffer locks a frame: the real cause was a memo of one bind per stage. Harmless,
    but a build spent. Rule: when a counter can be added first, ship the counter with the other changes and keep the
    guessed fix until it confirms.

46. **Check the other ports of the same toolchain before tuning alone.** (2026-11-15.) Five Switch rounds tuned the
    render thread while it shared every core with 13 guest threads, some at higher priority, because core_layout
    was off (it had measured no gain in another game). Mark had to point at robzilla10001/SwitchPorts:
    ShaolinMonks-NX, on the same toolchain, keeps its renderer alone on a core. Rule: when the render or title
    thread is the limit, read the thread placement in the log first, and read the sibling ports' READMEs at the
    first profile, not after the fifth.

47. **A renderer swap drops the old renderer's game fixes.** (2026-11-15.) Main fixed the shaking pause screen in its
    NV2A executor (RECOMP_PAUSE_HOLD); this branch's deko3d backend never got it, and Mark found it again. Same lesson
    as pitfall 41, for the renderer: when moving to a new backend, list the old one's game-specific fixes (main's
    progress log, "Fix (nv2a_...)") and carry each over or show why it is not needed.

48. **Read the clocks line of every log against the test's conditions.** (2026-11-15.) Every Switch log of this
    branch began with "clocks: now cpu 1020, gpu 768, mem 1331 MHz": 768 MHz is the docked GPU clock, set in handheld
    by Mark's sys-clk. I read past it for ten runs and called the results "stock clocks". The CPU work was real, but
    the GPU was never at its handheld clock (307 MHz), where it turned out to be the limit. Rule: before reading any
    speed number, check CPU, GPU and memory clocks and docked or handheld against what the test claims, and say so
    when they differ.

49. **Settle a build's contents with Mark before building it.** (2026-11-15.) Build 12 was compiled while its plan
    was still open; Mark then dropped ZCULL, I rebuilt, he kept it, and it was rebuilt again. Mark: do not build
    anything until what goes in the build is final. Rule: present the plan, wait until he agrees on every item, then
    build.

50. **Hook a diagnostic on the path the log already showed.** (2026-11-16.) Build 13's pause log started in SyncLate
    (the late read-back path), but run 12's log showed "read-back" blocks, which only the synchronous Sync path makes;
    the log never ran and the run gave no pause answer. Rule: before placing a diagnostic, find which code path the
    existing log lines prove is taken, and hook that one (or both).
