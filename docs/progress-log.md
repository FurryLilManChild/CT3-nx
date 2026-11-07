# Progress log (newest first)

## 2026-11-07: Switch test 8g, a resumed higher thread runs first

- Switch test 8f: black screen, though the same build reached the title in the cloud. The [GIL] snapshot: the -2
  server (1012) held the lock at priority 15 for 30 s, calling job 0x000C5F50 2.6 million times a second; the main
  thread waited at 0 the whole time. The server's loop (0x000B3480) only suspends itself when its pass returns 0
  or the wake flag 0x002E00C0 is set, and only the main thread's wake routine (0x000B3360) puts it back to -2. My
  reading: the main thread resumed the raised server and read the flag before the server had run (on the Xbox it
  would have switched to the server inside the resume), went round again and raised the server after it had
  finished, leaving it at 16 with nothing to stop it.
- Fixes: NtResumeThread waits (up to 10 ms) until a resumed thread above the caller has asked for the guest lock
  (RECOMP_RESUME_WAIT=0 turns it off); a thread back from a kernel call no longer takes the lock past a higher
  waiter (its priority can have dropped in the call). kernel_thread.c lacked <stdlib.h>, so getenv's pointer was
  cut to an int (the cloud run crashed with RECOMP_RESUME_WAIT set).
- The cloud did not reproduce 8f's hang, on 2 cores or 1, with or without a test wake-up delay
  (RECOMP_TEST_WAKE_DELAY_US, CLOUD_CPUS in tools/linux/cloud_boot.sh): it boots to the title either way.

## 2026-11-07: cloud boot test; 8f reaches the title screen there

- Mark: use a cloud boot test until a build reaches the title screen, then send him an NRO. tools/linux/cloud_boot.sh
  (lavapipe through mesa-vulkan-drivers, Xvfb, out/build-vk-linux; frames every 60 presents to out/cloud). The
  disc came up as one tar (C:\CT3-nx\assets\cloud_game_root.tar) into assets/linux_root (ignored), with the save
  from Mark's PC game_root (without one the game stops on "Unable to load game", waiting for A).
- 8f in the cloud: Sega, Hitmaker, the title screen and the attract demo (city drawn) within 150 s. Seen: one
  sound pass entered by a function-entry yield (equal priority: the +1 server and thread 1013 both at 1), and the
  loader-style wait on 0x002D83C4 (thread 1016, 17 s, held by the main thread from 0x000A0804) still there.

## 2026-11-06: Switch test 8f, no suspension while holding a guest lock

- Test 8e: black screen, no logos. [CS] reports: threads 1012 (the -2 server) and 1013 (the CRI file thread)
  waited 47 s on guest lock 0x00214620, held since 14.5 s by 1010 (the idle thread), taken at 0x000DDFFD in its
  thread startup; the [GIL] snapshot never showed 1010 queued, so it was stopped (suspended). The main thread ran
  at 100% (CRI's group 5 wake loop, waiting on the server). Caused by test 8d's check: the idle thread stopped on
  returning from RtlEnterCriticalSection. Now a cooperative suspension waits until the thread holds no guest
  critical section (kernel_rtl.c counts them). Pitfall 27.
- 8e's other change did what it should: the queued priority updates show in the log ("now at priority 15 while
  queued").

## 2026-11-06: Switch test 8e, priority changes reach queued threads

- 8c with RECOMP_CRI_EXCL=0 (ct3_env.txt): 2 to 3 fps on the logos, then 0 fps at the Hitmaker logo for good
  (watchdog at 80 s). The main thread was looping in CRI's "run the -2 server now" (0x000B3360, group 5's wake
  function, registered at 0x000B369C): set flag 0x002E00C0, raise the server (handle [0x0034ED68]) to
  [0x002E00A8], resume it, poll the flag (up to 200,000,000 times), then put it back to [0x002E00B8]. Its kernel
  calls 246/143/250/231/224 ran 110,000 a second.
- Likely cause (code): a thread waiting for the guest lock kept the priority it queued with, so the raised server
  stayed at -2 behind the main thread at 0. Now KeSetBasePriorityThread updates a queued thread, a reservation
  and the holder (xbox_gil_prio_changed), and asks the holder to yield when a waiter is now above it.
- New: the 5 s [GIL] report includes the lock's holder, waiters (priority, time waiting) and reservations.

## 2026-11-06: Switch test 8d, a suspended thread no longer runs

- Test 8c: priorities right at last (the main thread at 15 while it holds the CRI sound lock), and no sound pass
  entered by a thread at or below it. But the logos crawled at 0 fps: every sound lock waited the full 1 s for
  the -2 server's pass ("a server pass still running after 1 s"). With RECOMP_CRI_EXCL=0 in ct3_env.txt (no
  build) 2 to 3 fps, stuck at the Hitmaker logo.
- Cause found in the code: the unlock suspends the idle thread (0x000B33D0, +2, a busy loop) and drops the main
  thread to 0; the idle thread, already waiting for the guest lock, took it (the suspension is only checked
  before a thread queues) and spun until an interrupt asked for a turn. The watchdog showed it: the main thread at
  0 waiting with 1010 (+2) holding. Now xbox_gil_resume checks again once it has the lock, and lets go and stops
  if a suspension came meanwhile.
- The CRI exclusion waits are off by default (RECOMP_CRI_EXCL=1 for them).

## 2026-11-06: Switch test 8c, three scheduling bugs from test 8b

- Test 8b showed the Sega logo, then froze. Its log: the main thread still waited at priority 0 behind the sound
  idle thread (1010, +2) right after setting itself to 16; and the sound pass (1012, priority 15) was entered by
  1010 during its NtSuspendThread (ordinal 231) about 30 times a second.
- Bugs found in the code: (1) host threads with no TIB of their own share the main thread's KTHREAD (0x4000), so
  KeSetBasePriorityThread(0x4000) could change one of them instead of the main thread (now: a thread naming its
  own KTHREAD means itself, and the main thread's entry wins); (2) the guest lock's fast path ignored kernel-call
  reservations; (3) a cooperative NtSuspendThread left a thread that holds the lock running until its next
  kernel call (now it is asked to yield at its next function entry, where it stops).
- KeSetBasePriorityThread now logs the caller and the thread's resulting priority (first 40).

## 2026-11-06: Switch test 8b, the main thread's priority

- Test 8 went to a black screen on both boots: at 14.5 s the main thread took the CRI sound lock (raising itself
  to 16) and resumed the sound idle thread (+2), which then held the guest lock at 99% of a core; the watchdog
  showed the main thread waiting at priority 0. On the Switch the main thread had no thread object in
  win32_compat.c, so its priority changes were lost; test 7's 50 ms rule had been letting it in. Fixed (w32_self:
  any thread gets an object when it first asks for itself). Pitfall 25.

## 2026-11-06: Xbox scheduling rules for the guest lock, for Switch test 8

- Switch test 7 (priority lock): booted and played (about 30 fps in a shift). Quitting a shift to the menu
  crashed with the SJ ring error -3 inside a sound server pass; the report said other guest threads ran 8 times
  during that pass (last: host thread 1006, priority 0). The pass's only lock releases were kernel calls
  (KfRaiseIrql 160, KfLowerIrql 161, RtlLeaveCriticalSection 294, from the ICALL history). Not proven which route
  let thread 1006 in: those calls released the lock to any waiter, and the 50 ms rule could force a yield.
- Mark: do what the Xbox does. RECOMP_GIL_PRIO now follows its rules (kernel_bridge.c): a kernel call keeps the
  lock reserved for its caller against equal and lower threads until it really blocks (a wait whose object is
  not signalled (kernel_sync.c, kdisp_wait), a sleep or delay, file I/O, a yield, thread exit, a busy guest lock
  (kernel_rtl.c), a suspension (kernel_thread.c)); higher threads and DPCs still get in. The 50 ms rule is gone;
  poll-loop yields wait behind everyone. Watchdogs log only (waits over 50 ms, reservations over 2 ms); a
  reservation held 1 s is dropped as a deadlock and logged loudly.
- New log lines: "[GIL] sound pass interrupted" (who ran, how the lock got free) and a "[GIL] 5 s" summary with
  the [CRI] stats.
- Known difference left: equal-priority threads still take turns after 1 ms (the Xbox's time slice is longer;
  value not checked).

## 2026-11-06: the guest lock by guest priority (RECOMP_GIL_PRIO), for Switch test 7

- Mark's question: if the Xbox keeps the sound code safe by thread priority, why not do the same. So the guest
  lock (kernel_bridge.c) now hands over by guest priority under RECOMP_GIL_PRIO=1 (main.c sets it): the next
  holder is the highest-priority waiter (oldest first among equals); a waiter above the holder asks it to yield at
  once (function entries); threads at DISPATCH rank first; equal priorities share after 1 ms; a waiter left out
  50 ms is let in first and logged ("[GIL] priority: ... waited"), so a difference from the console cannot hang
  the game for good.
- With it: RECOMP_CRI_ATOMIC (the no-pre-emption pass) is off by default (it would also keep out the threads above
  the server); the hand-written CRI waits sleep 1 ms with the lock given up (xbox_wait_yield_ms) instead of a
  yield that would hand it straight back to a higher-priority waiter.
- The test: "[CRI] SJ ring error -3" (if the crash comes back) says whether other guest threads ran during the
  failing pass. "[GIL] priority" lines say whether the 50 ms safety net had to step in, and for which thread.

## 2026-11-06: Switch test 6: the SJ ring crash, twice, with no give-ups

- Mark: both tries crashed a second into NOW LOADING. Both logs: [ICALL] 0x000B8120 from 0x000B84CC with -3 on the
  +1 server's stack (esp 0x01FB9F64), then the crash: the same as test 4 and PC test 12. No "taking the lock
  anyway" line in either run, so the give-up theory of test 4 was wrong.
- 0x000B8420 (put a chunk back into an SJ ring) checks that the chunk's position follows the ring's read or write
  position (mod the size) and reports -3 otherwise. The SJ calls have no lock of their own (0x000BCC50 and
  0x000BCC70 only count): they rely on the +1 server's priority, so nothing lower runs in the middle of one. Our
  guest lock can be handed to another thread at any function entry (RECOMP_PREEMPT) and in every kernel call, so a
  lower thread could work on the same ring half-way through a pass.
- A theory, not proven: a server pass (0x000B97E0) is now not pre-empted at function entries
  (xbox_gil_nopreempt; it still yields in kernel calls; RECOMP_CRI_ATOMIC=0: as before).
- The test for it: when the ring error fires, "[CRI] SJ ring error -3" says whether the failing thread is inside a
  pass, and how many times other guest threads ran during that pass (kernel_bridge.c counts, per thread, takings
  of the guest lock after another thread held it). Other threads ran during the pass: the mid-pass hand-over
  theory holds (and kernel calls inside the pass are the remaining way in). None ran: the ring was wrong before
  the pass began and the theory is wrong.

## 2026-11-06: Switch test 5: in game with the city; the looping-audio freeze found

- Mark: one boot froze with looping audio before NOW LOADING; the next got into a shift with the city drawn.
- Cache rename: "done on the second try (first errno 5)": the park-and-retry fix works. In a shift at 1785 MHz:
  30 to 34 fps (menus 56 to 60).
- The freeze run's report: the sound lock (count 1) was taken on the path 0x000B3820 (the CRI thread's vblank
  loop) -> 0x000B38F0 -> 0x000B9B60 -> 0x000B2A30 -> 0x000B5B70, and that thread was back in its vblank wait
  (ordinal 159 from 0x000F5273), long past it. 0x000B5B70 / 0x000B5B90 are an outer counter at 0x002E0254
  ("if 0, take the sound lock; ++" and "--; at 0, release"), 17 callers (ADX calls from the main thread and that
  loop). The lock call between the test and the increment can hand over the guest lock, so two threads can both
  see 0 and both take the lock; two releases then bring the counter to 0 once and the lock count stays 1. On the
  Xbox the first thread is already at priority 15. Fix: overrides give the counter an owner (others wait).
  LIFT_ONLY regen: recomp_0011.c and one more changed (the _gen wrappers); the PC build needs -Regen.

## 2026-11-06: Switch test 4 (1785 MHz): crash after character select

- Mark: "an error happened" after character select. The log: the PC play test 12 crash again. [ICALL] 0x000B8120
  (the SJ ring check 0x000B8420's error callback, `jmp $`) called from 0x000B84CC with -3, on the +1 server's
  stack (esp 0x01FB9F64), then a wild call to 0x2400 and the crash.
- Earlier in the run the CRI lock gave up on a running pass eight times (four lines shown, the cap was five)
  around 15 to 18 s. A give-up is the one way left for the main thread to take the sound lock mid-pass, which
  is what corrupted the ring in test 12. Whether one happened just before the crash is not in the log (the
  5 s stats window that would show it had not closed).
- Change: wait up to 1 s (not 100 ms) for a pass before giving up, show up to 20 give-ups, and with each, what the
  pass threads are doing (their current kernel call). Not proven to be the cause; the next log says.

## 2026-11-06: Switch test 3: in game, but the city has no geometry

- Mark: into a shift this time (no freeze, so the CRI holder report did not fire). The taxi, traffic and people
  float in the sky: no city geometry.
- Log: `[FILE] rename to Z:\_bank10.mdl (replace): 0xC0000001`. The POSIX rename failed (FAT does not rename over
  an existing file, and the Switch will not rename an open one), so the game read the old _bank10.mdl the first
  Switch run left on the SD card: the city model bank. In-game 24 to 29 fps at 1224 MHz (menus 58 to 60).
- Fix (kernel_file.c, win32_compat.c): on failure, remove the old target (when replacing), close the handle's
  descriptor for now (w32_handle_park; reopened at the same position on next use), retry, and log the outcome.

## 2026-11-06: Switch test 2: freezes after character select (looping audio)

- Mark: both tries froze right after character select, before NOW LOADING; the second with the "broken record"
  audio. (Switch test 1's first try did this too, with the old blocking lock wait, so it is not new.)
- Both logs: from about 40 s the CRI stats read "passes 0 ... now count 1 holder caller 000B9482": the CRI sound
  lock is held and never released, so the server passes stop (the looping audio), and "[CRI] pass: waiting over
  2 s for the sound lock". The idle thread (priority +2, which runs while the lock is held) then takes the CPU
  (38% of the main thread's lock waits, one thread at 86%), 26 then 11 fps. 0x000B9482 is only the lock
  callback wrapper (0x000B9470 calls [0x002E0750]), so the holder is unknown. No guest-lock wait over 2 s was
  reported, so the holder is not stuck in RtlEnterCriticalSection.
- Diagnostic: the CRI lock override saves the holder's thread and guest stack when the count goes 0 to 1; a pass
  waiting 2 s prints them and what the holder is doing in the kernel right now (kernel_bridge.c keeps, per
  thread, the current or last kernel ordinal, its guest return address and since when).

## 2026-11-06: Switch test 1: menus at full speed, NOW LOADING stalls

- Mark: menus and character select at full speed (58 to 60 fps at 1224 MHz). Loading into a shift stuck twice on
  NOW LOADING; one earlier try froze before it with looping audio (that run's log was overwritten).
- The log: the loader worker opened Partition5\tmp (the bank10.mdl cache copy) at 67.8 s and nothing followed for
  40 s while the game drew at 60 fps: the same stall as PC test 18, so the lock handover did not fix it.
- Next: the contended guest-lock path polls (yield 5 ms, then 1 ms sleeps) instead of a blocking host wait, and
  past 2 s names the lock, the holder, where it was taken and the waiter's guest stack (RECOMP_CS_POLL=0: the old
  wait). The Switch keeps the last two runs' logs (ct3_log_1.txt, ct3_log_2.txt).

## 2026-11-06: first Switch build

- Toolchain on this cloud machine: Mark's C:\devkitPro (zipped into out\, 1.4 GB, staged in four parts) under
  Wine (setup_switch_wine.sh; the zip lost the .exe execute bits, chmod +x); NVK's libvulkan.a and headers plus
  libexpat.a and libzstd.a from ghcr.io/danfromtico/switch-dev-legacy-vulkan (static libraries only, nothing from
  it run here; Mark agreed); glslang 15.4.0 built with the toolchain into /opt/glslang-switch (5 min).
- Build: 92 steps, about 70 minutes on 2 cores (the machine restarted once and the build started over). One
  Switch-only error: the lock handover's `volatile long` with InterlockedIncrement (LONG is 32-bit there).
  switch/build.sh takes NINJA_ARGS (-k 0 to see every error in one pass).
- Branding: the icon and loading logo still said SONIC HEROES (from the kickoff copy); now CRAZY TAXI 3.
- Delivered to C:\CT3-nx\out\sd\switch\ct3\: ct3.nro (39 MB), ct3_env.txt (NFSU2_CPU_MHZ=1224, the smallest
  overclock step first, as on CTDC-nx) and game\ (the extracted disc).

## 2026-11-06: PC play test 19: pause fixed

- Mark: the pause picture holds still (and on his original Xbox it does too, so the jitter was ours). He also saw
  none of the slight tearing from earlier runs (not explained by anything here; noted).
- Log: three "RECOMP_PAUSE_HOLD" copies (three pauses); one load, cache copy and rename done; lock 0x002D83C4
  handed to a waiter once ("a waiter took it"); lock 0x00122C08 twice "no taker in 2 ms". 57 to 59 fps in play.
- The handover's 2 ms was timed with GetTickCount64, which ticks every 15.6 ms on Windows; now the performance
  counter.

## 2026-11-06: PC play test 18: stuck NOW LOADING again (thread dumps), and the pause

- Stuck run, two thread dumps resolved with ct3_recomp.map: the loader worker (sub_000DE29D running
  sub_000A05E0, then 0x000AE030, 0x000AB300, 0x000AB130, 0x000ACA40) waits in RtlEnterCriticalSection, in the
  host EnterCriticalSection, in both dumps. The main thread is in its per-frame wait (sub_000F5250, D3D's event
  at device + 0x2448) from 0x000A72F0, drawing NOW LOADING at 59 fps. The good runs' [CS] lines show main and the
  worker trading lock 0x002D83C4 (main takes it at 0x000A0804). Reading: a host critical section wakes a waiter on
  release but does not give it the lock, so main, which releases and re-takes it every frame, can win every time
  and the worker starves. (Which lock the worker waits on is not in the dump; 0x002D83C4 is the likely one.)
  Fix (kernel_rtl.c): a release with waiters waits up to 2 ms for a waiter to take the lock ("[CS] lock ...
  handed over" lines, the first three).
- Pause ([FSUM]): flip 2367 drew the full scene (1095 draws, colour cleared) into 80564000, flip 2366 into
  80690000; from 2368 every frame is 51 draws with no colour clear, alternating buffers. So the menu is drawn
  over two different frames in turn. Fix (nv2a_pb_exec.c): on the first frame that clears only depth/stencil
  after a full frame on the other buffer, the last full frame is copied into this buffer first, so both show the
  same picture. RECOMP_PAUSE_HOLD=0 turns it off.

## 2026-11-06: PC play test 17: the visual fixes hold; the pause screen

- Mark: the counters, the character and car shadows and the hailing circles are right now.
- Pause (three traces): each paused frame clears only depth and stencil (clear 0x03, no colour) and draws only
  the 48 quads of the pause menu, alternating between the two buffers 80564000 and 80690000. So the picture
  behind the menu is whatever each buffer last held: if those are two different frames, the screen alternates
  between them (the jitter). Whether the game draws the scene into both buffers on the way into the pause (so
  they match on the Xbox) is not in these traces. Added [FSUM] lines: when a frame's draw count falls under a
  quarter of the last one, the six frames before and the ten after are printed (draws, colour clears, surface).

## 2026-11-06: PC play test 16: four frame traces in a shift

- First run stuck on NOW LOADING; the second was fine. The stuck run opened Partition5\tmp (the bank10.mdl cache
  copy, which every run makes) and never renamed it; the loader worker (0x000DE29D, ctx 0x000A05E0) made no kernel
  call after that while the main thread kept drawing at 59 fps. No thread dump, so the cause is open; next time
  it sticks, a dump.
- Pink in the counters: the only undecodable format logged was 0x1A (A8L8, swizzled), texture 81BDB000 32x32.
  The executor's decoder had no A8L8: added A8L8 (0x1A, via LIN_A8L8 0x20) and AL8 (0x01, via LIN_AL8 0x1B).
- Black squares under the characters: the same 81BDB000 texture, drawn as blended ground quads with a near-black
  vertex colour (FF000019, constant alpha 0x33): magenta times black is a black square. The A8L8 decoder fixes it.
- The car's big dark trapezoid: a stencil shadow volume (1920 indices, INCR_WRAP front / DECR_WRAP back), then a
  full-screen pass with colour writes off, stencil EQUAL 0x80 and z-pass op 0, then a dark quad (A0000000) where
  the stencil is not 0. Op 0 is GL_ZERO; nv2a_vk took it as KEEP (nv2a_gl likewise), so every receiver under the
  car stayed 0x80 and went dark. Both now map 0 to ZERO.
- Hailing circles: two inline line strips of 13 vertices (colours CFBF0000 and CF68B808, ONE / INV_SRC_ALPHA)
  with world positions in fixed-function mode. The executor treats inline vertices as already on screen unless
  RECOMP_INLINE_FFP=1, so they landed at x 1162, y 1510. This title's other inline draws use vertex programs, so
  main.c now sets RECOMP_INLINE_FFP=1 by default. No destination-marker draw of that kind in the frame after the
  pickup; to check once the circles show.
- Pause: Mark sees the picture jitter. No trace yet.

## 2026-11-06: PC play test 15: in game

- Mark played: past NOW LOADING into a shift, driving, about 57 fps. The disasm fix held (no stub besides the lone
  ret 0x000C0946, no crash).
- His list: (1) pink blocks in the TOTAL EARNED counter, (2) the circles around hailing customers and the
  destination markers missing, (3) black squares for the characters' shadows, (4) A/B and X/Y swapped.
- (4): ct3_menu.c's default "Xbox positions" layout swaps A/B and X/Y, right for the Switch (which reports buttons
  by label) and wrong on the PC, where XInput and SDL already report Xbox positions. The tables are now per
  platform (the PC's "Xbox positions" is the identity).
- (1): magenta is the Vulkan renderer's fill for a texture format it cannot decode; it now names each such
  format once in the log, trace or not.
- (2) and (3): no frame trace in this run; the next one, taken in a shift, covers them.

## 2026-11-06: PC play test 14: character select fixed, the old crash at the start of a shift

- Mark: the character select draws Axel whole now (the skinning fix). The game then crashed exactly where test 10
  did, as the shift was about to begin.
- Sound: the [CRI] stats lines are healthy (hundreds of passes and thousands of locks per 5 s, no give-ups, the
  longest hold 16 ms). The rename ran: `[FILE] rename to Z:\_bank10.mdl (replace)`, and no read failed.
- The crash is the same as test 10's to the byte (read of guest 0xB254FB34 in sub_0007F9D0, its argument a float
  used as an object), with the same `[STUB] unresolved 0x000800AF` line just before it. So not the file reads.
  0x000800AF is the epilogue of 0x0007FF70 (a city object's update, reached only through the table at 0x001D15D0).
  disasm's imm_ref_target pass had made 0x00080000 a function (an immediate somewhere equals it), before 0x0007FF70
  was known, so the gap test passed. 0x0007FF70 was cut at 0x00080000; its `jne 0x800AF` went to a stub that
  returns at once, skipping `pop ebx; add esp, 0x18; ret`, and the next object update ran on a broken stack.
- Fix (xboxrecomp/tools/disasm/functions.py): an immediate target that the instruction before it falls into is not
  a function, unless it probes as a prologue; test added. The regen: 0x0007FF70 now runs to 0x000800C0 with
  0x800AF an inner label; one other imm start dropped (0x003570F8); 22 function ends changed (the 0x0004B6E0
  group too). 186 unresolved stubs, down from 188.

## 2026-11-06: PC play test 13: logos crawl, stuck at the Hitmaker logo

- Mark: slow through the SEGA and other logos, then the Hitmaker logo stayed (frozen, or too slow to tell).
  The renderer kept going (about 56 fps after the first seconds).
- The log: five "[CRI] lock: a server pass still running after 100 ms" lines (the cap) in the first seconds, and
  one "[CRI] pass: waiting over 2 s for the sound lock (count 1)".
- Cause of the crawl: test 12's exclusion tracked one "current pass" for both sound servers. Their passes overlap
  on the Xbox (the +1 server pre-empts the -2 one mid-pass), so a server taking the lock inside its own pass
  waited on the other server's pass, 100 ms at a time.
- The lock, read in full this time: on 0 to 1 it raises the caller to priority 15 ([0x002E00A8], set at
  0x000B3591) and resumes the idle thread; the unlock undoes both. Rework: only a fresh lock taken outside a pass
  waits for running passes (100 ms give-up); a pass started outside a pass waits while another thread holds the
  lock; nested calls never wait. The pass nesting is per thread.
- Not explained yet: the 2 s pass wait (someone held the lock that long). The new [CRI] stats line every 5 s
  (passes, locks, waits and their time, give-ups, the longest hold, the current holder's caller) answers it next
  run. RECOMP_CRI_EXCL=0 turns the waits off.

## 2026-11-05: PC play test 12: crash moved into NOW LOADING; the character-select bodies

- Mark: same run (West Coast, Arcade, a character); the character select still had Axel's torso and legs missing,
  and the game crashed earlier, in the middle of NOW LOADING. He took a frame trace on the character select and a
  screenshot.
- No "[FILE] rename to" lines: the crash came before the cache copy, so the rename fix is not verified yet.
- Crash: CRI's ring-buffer check in 0x000B8420 (on the +1 sound server's stack) found the read and write positions
  out of step and called its error callback 0x000B8120 with -3. That callback is `jmp $` (an endless loop on the
  Xbox, an unresolved indirect call here), and the game then called a wild pointer (0x2FC0). Cause: CRI's sound lock
  0x000B32F0 / 0x000B3330 (count at 0x002E00BC) relies on thread priorities (the +1 server pre-empts the main
  thread, and the +2 idle thread runs while the lock is held). Our guest lock hands over in turn whatever the
  priority, so the main thread got in halfway through a server pass (0x000B97E0).
- Fix: overrides of the lock, the unlock and the pass make the exclusion explicit (a pass waits while another
  thread holds the lock; taking the lock waits for another thread's pass, giving up after 100 ms with a log line;
  a pass waiting over 2 s is logged). The clock is the guest's KeTickCount at kernel data + 0x20, written every
  millisecond by the tick thread.
- Character select, from the frame trace: every draw of Axel's torso and legs is skinned (SET_SKIN_MODE 1 or 5),
  and for those D3D 4531 loads the composite matrix with the projection and viewport only (the same matrix for
  every skinned draw), while the unskinned draws (head, hands, sleeves) get model-view 0 x projection x viewport.
  The Vulkan vertex shader took the blended eye position back to object space through inverse model-view 0 before
  the composite; here that matrix's fourth row is zero, so w came out 0 and nothing drew. The shader now applies
  the composite to the blended eye position directly (RECOMP_VK_SKIN_OBJ=1 for the old way). The frame trace now
  also prints a skinned draw's vertex layout and first weights ("sattr").
- The "missing button icons": the trace shows the game drawing the plain letter D from its font (texture
  808FA000, decoded: letters, digits, arrows, no button pictures) and nothing skipped there. So no evidence of a
  missing icon; to be checked against the original.

## 2026-11-05: PC play test 11: same crash; the real cause is a missing rename

- Character select and NOW LOADING were very fast now (the read logging). The character-select glitches stayed. The
  game crashed at exactly the same spot, with the same six failed reads.
- My test-10 diagnosis was wrong (pitfalls 14). The warning the new code writes went to out/pc/xbox_kernel.log:
  `NtReadFile FAILED: handle=FFFFFFFFFFFFFFFF ... err=6`, invalid handle, on all six. The only failed open of the run:
  Partition5\_bank10.mdl (err 2). Just before it: Partition5\tmp written, then `NtSetInformationFile: unhandled
  class 10`. The game copies a cache file to Partition5\tmp and renames it (FileRenameInformation) to its real
  name; with no rename, _bank10.mdl never existed, its open returned -1, and the city streamed from that handle.
- Fix: FileRenameInformation, decoded in bridge_NtSetInformationFile (+0 ReplaceIfExists, +4 RootDirectory,
  +8 OBJECT_STRING name), done by xbox_RenameOpenFile: Windows by handle (SetFileInformationByHandle
  FileRenameInfo), then MoveFileEx, then a copy; POSIX rename() with the handle's stored path updated
  (w32_handle_set_path). Each rename is logged. The FILE_FLAG_NO_BUFFERING change stays, with its comment corrected.

## 2026-11-05: PC play test 10: menus, character select, the city, a crash

- First play.ps1 run. Mark: main menu, West Coast, Arcade, character select (graphical glitches, slow), a character
  picked, then a very slow NOW LOADING; the car and the world began to draw, then the game crashed. 45 to 50 fps
  through most of the run; a 1.3 s hitch with shader compiles and 102 texture uploads as the city loaded.
- The ended-thread and retpad fixes held (no hang; one stub left besides the lone ret: 0x000800AF "from 0x002B4CC0",
  a return address inside .data, so a call through a garbage pointer).
- Crash: `[CRASH] access violation ... read guest VA 0xB254FB34`, host sub_0007F9D0+0x2CD (map), registers full of
  float-looking values (eax 3D72688C, ebx 3B84056E): data read as pointers.
- Cause: six positioned reads failed with STATUS_UNSUCCESSFUL and 0 bytes just before it (offsets 0, 16896,
  4729344, 5257216, 5284864, 5308416; lengths 5632 to 58880; around Partition5 _taxi2gen.bin and the 48 MB
  _taxi1.bin cache). kernel_file.c passed FILE_NO_INTERMEDIATE_BUFFERING on as FILE_FLAG_NO_BUFFERING; Windows then
  wants offsets and lengths in the host drive's sectors (4096 on Mark's SSD), the game aligns to the Xbox's 512.
  Every failed offset is a multiple of 512 and not of 4096. The world was built from empty buffers.
- Fix: the flag is no longer passed on (it is a hint on the Xbox); a failed read now logs a warning with the Win32
  error instead of a compiled-out trace.
- Slow loading: the dump taken during NOW LOADING has the loader thread (sub_000A05E0's) in fprintf under
  bridge_NtReadFile: every read was logged on an unbuffered log, 144,143 lines this run. Now the first 200 reads
  and every failure (end of file aside); RECOMP_READ_LOG=1 for all. The other fprintf waiter was nv2a_ack_thread's
  DMA_PUT line (about 50 a second), left as is for now.
- Not looked at yet: the character-select glitches and slowness (shader compiles and texture uploads show in the
  [hitch] lines).

## 2026-11-05: PC boot test 9: title screen, white screen after Start

- The ended-thread fix works: "thread tid 228536 has ended (exit 0x00000000): signalled KTHREAD" and the game
  went on past the fade: Mark reached the title screen and pressed Start on the Legion Go's pad. The next
  screen was white, with audio playing; buttons seemed to do nothing. Back to 47 to 55 fps there.
- Files after Start: Media/Sprites_us/mSprites.art and tSprites.art. Then `[STUB] unresolved 0x00071540 entered
  from 0x0005EE2C`: a real function after `ret; nop` inside sub_00071520, a tail_jump_alias measured too long,
  which I had left out at kickoff (see pitfalls 13).
- New tools/retpad_sweep.py (aligned starts after a ret and padding inside a known function): 4 candidates, all
  clean, all seeded: 0x00071540, 0x000C1030, 0x000C1080, 0x000C1BE0. Regen: 5,557 functions, 188 unresolved.
- From now on play tests use tools\win\play.ps1 (no time limit; boot_test closes the game at 60 s).

## 2026-11-05: PC boot test 8: logos, then the loader starves

- Mark saw the logos: Sega, a couple of others, Hitmaker. Then a black screen to the end of the run. New files
  read: Media/ADX/adx0.afs and Sprites_us/gSprites.art. 29.7 fps to frame 301, then 2.6 to 3.2 fps.
- Main thread: 0x00011ACE in sub_00011A98, the frame loop that polls GetExitCodeThread (0x000DE1F6) on the thread
  at 0x002C1EF8 until it stops being STILL_ACTIVE (0x103): it waits for the loader.
- Frame trace (taken during the black screen): a clear to FF000000 and two full-screen inline quads of colour
  A0000000 (texture 80C55000 bound), 0.5,0.5 to 640.5,480.5: the game's own fade, not a renderer fault.
- Cause (found after a first, wrong fix): the thread at 0x002C1EF8 is boot thread #2 (ctx 0x0005E400), which ended
  before the logos (PsTerminateSystemThread right after it started). guest_thread_forget drops an ended thread
  from the runtime's table, so ObReferenceObjectByHandle waited its full 200 ms for the thread to register and then
  returned object 0 with STATUS_SUCCESS; XAPI's GetExitCodeThread (0x000DE1F6) read SignalState at 0+4, saw 0,
  returned STILL_ACTIVE. Every frame: 200 ms, so 3 fps, and the wait never ends.
- First fix, withdrawn: the dump also showed the idle thread holding the guest lock with contended 0, and I read
  the 3 fps as the +2 idle thread starving the main thread; commit 7b4de4c made it hand over to any waiter. That
  was not the cause (the main thread spent its time in the 200 ms wait, not in gil_lock) and carried a real
  trade-off, so it is reverted: sub_000B33D0 is back to RECOMP_PREEMPT.
- Fix: an ended thread gets a signalled stand-in KTHREAD from the guest heap (Header.SignalState +4 = 1,
  ExitStatus +0x120 = its exit code; one per thread id), and no 200 ms wait (kernel_bridge.c). Works on POSIX too
  (win32_compat tracks exits). Linux and Windows checks clean.

## 2026-11-05: PC boot test 7: the game runs, black window

- Past the boot hangs: Vulkan up (AMD Radeon, 1280x720 swapchain), the game loop running at about 53 fps for the
  rest of the minute, icalls from 190 million to 843 million, the main thread in D3D (0x00104B18 < 0x000A7361 <
  0x0005EB16). A window opened about 20 to 30 s in (Mark), black; all 35 frame dumps black.
- The executor: about one clear and one 4-index quad per frame (3,348 draws by the end), 0 textured, "x 0.0..0.0
  y 0.0..0.0" in the CPU stats; two surfaces (0x00564000, 0x00690000, 640x480, pitch 2560) alternating; 238
  distinct unhandled methods. One FLIP_STALL at start.
- Two stubs ran: 0x000C5EE0 (a real function, from the callback runner sub_000B97E0, table 0x002E0760) and
  0x000C0946 (a lone `ret`, which the stub already does). 0x000C5EE0 and the other 14 tail-jump targets with clean
  starts outside any function are now seeded (190 unresolved, from 205).
- The watchdog's first periodic line now also arms one frame trace (RECOMP_FRAME_TRACE, [FTRACE] lines), as
  trace_frame.ps1 does by hand.

## 2026-11-05: PC boot test 6 (all-threads dump): the idle thread

- The dump answered it. [GIL] holder tid 224644 for the whole run (tickets next 37003, serving 36999: four
  waiting). Host stacks resolved with out/pc/ct3_recomp.map:
  - main (230348): gil_lock < kernel_thunk_dispatch < sub_000DE12F (NtSuspendThread wrapper) < sub_000B3330 (the
    sound unlock) < sub_000B9490 < sub_000BF5A0 < sub_0003B5F0 < sub_0005E420. The suspension returned; it waits
    for the lock.
  - 224644: sub_000B33D0+0xEE with nothing deeper: the holder, running guest code.
  - the timer thread (kernel_raise_interrupt), sub_000A1400's thread and sub_000B3480's thread (in its own
    NtSuspendThread, real path) also in gil_lock; sub_000B3410's thread in KeWaitForSingleObject.
- 0x000B33D0 is an idle thread: `while (![0x002E00DC]) [0x002E00C4]++;` then exit. No call in the loop, so it never
  passes a pre-emption check, a spin yield or the cooperative stop point: with the lock it runs forever. The
  translator's poll-loop marking skips it (it counts, it does not just poll). On the Xbox the scheduler pre-empts it.
- Fix: a hand-written sub_000B33D0 (src/recomp_manual.c), the same loop with RECOMP_PREEMPT on each pass. Linux and
  Windows syntax checks clean.

## 2026-11-05: PC boot test 5

- The cooperative path runs: "tid 226988 suspends tid 214952 (cooperative)" three times; one self-suspension by
  tid 200132 took the real path (Windows self-suspension inside the kernel, holding nothing).
- Still the same stop: main thread at esp 0x0097FE88 (inside 0x000DE12F, NtSuspendThread, from the sound unlock
  0x000B3330), icalls frozen at 35,660 from 25 s to 55 s. With the suspension now returning at once, the main
  thread is most likely waiting for the guest lock or for something another thread holds; one thread's view
  cannot say which.
- The watchdog's periodic line now also prints the guest lock's holder ([GIL]) and, once, triggers main.c's
  all-threads dump (RECOMP_THREAD_DUMP: each thread's host stack, resolved with out/pc/ct3_recomp.map).

## 2026-11-05: PC boot test 4 (periodic watchdog)

- All three test switches arrived. The main thread sat at esp 0x0097FE88 from 25 s to 55 s with the icall count
  frozen at 35,652: the same NtSuspendThread hang as test 2 (chain 0x000B3343, 0x000B94A2, 0x000BF6C8,
  0x000BF310, 0x0003B653, ...). Test 3's DCT snapshot was the same boot caught before the hang.
- Why the fix did not apply: no "cooperative" line in tests 3 and 4. It only covered threads with a kernel slot,
  and a slot was made at a thread's first kernel call; the sound server thread is suspended again before it ever
  calls the kernel, so it took the old path (real SuspendThread) every time.
- Fix: the suspender creates the target's slot; the target looks it up at the stop point if it has none yet
  (guarded by a global count, so the check is one load when nothing is suspended). The old path now logs
  "real suspension" so a miss shows in the log.

## 2026-11-05: PC boot test 3

- The suspend fix holds: the main thread got past the sound lock (no hang in NtSuspendThread). It loaded
  bank00/bank25 .art and .mdl, Partition5 _fcvdata.bin, C_TAXI03.SYS from the save, and the same sound setup.
- "Closed by itself" was the watchdog's own _exit at 40 s: RECOMP_WATCHDOG_KEEP never arrived, because through
  `powershell -File` an array argument "A=1","B=2" reaches the script as one string "A=1,B=2"
  (run_game.ps1 now splits it again).
- Watchdog snapshot: main thread esp 0x0097F9FC with 0x000D2FD4 on top: it was calling 0x000D2FC0, a wrapper of
  0x000D2E60, an 8x8 double-precision DCT (table at 0x00333398): a JPEG or MPEG style decode, not a hang in itself.
  Neither movie (Media/Movies/movie_LV.mov, movie_SA.mov) was opened. Still no GPU commands. Nearby, 0x000CBBD0
  has a deliberate `jmp $` (0x000CBBF4) when an init call (0x000CE110) returns an error other than 0xFF03FF05.
- One snapshot cannot tell slow work from a loop: RECOMP_WATCHDOG_EVERY=N now adds a line every N seconds with the
  main thread's esp, icall count and stack return addresses.

## 2026-11-05: PC boot test 2 (watchdog), a suspend deadlock

- Boot test with RECOMP_WATCHDOG_SECS=40: no failed indirect calls and no stubs any more; still no window.
- Watchdog stack: the main thread inside NtSuspendThread (0x000DE12F), called from 0x000B333E in 0x000B3330, the
  unlock half of a lock pair (0x000B32F0 resumes the thread whose handle is at 0x0034ED60 and raises the caller's
  priority; 0x000B3330 suspends it and restores the priority), reached through the callback pointer at 0x002E0758
  (0x000B9490). The call chain continues 0x000BF6C8, 0x000BF310, 0x0003B653, 0x0005E75D. By the pattern, CRI's
  sound-server lock.
- Cause, in the runtime: a kernel call lets go of the guest lock, so the thread being suspended could take it (or
  queue a ticket for it) while the caller was in NtSuspendThread; the Windows path then froze it with a real
  SuspendThread while it held the lock, and the caller waited for the lock forever. sheroes-nx never met it: its CRI
  thread only suspends itself.
- Fix (kernel_thread.c, kernel_bridge.c): another thread's suspension is a count; the suspended thread stops itself
  in xbox_gil_resume, just before it takes the guest lock back, where it holds neither the lock nor a ticket (not
  while it holds the dispatch lock). NtResumeThread undoes those counts first. Self-suspension and threads with no
  kernel slot keep the old path. Linux syntax check and tools/linux/wincheck.sh nongen: clean.

## 2026-11-05: PC boot test 1

- Built and ran on Mark's PC (60 s boot test). No window: the GPU got no commands (DMA_PUT stayed at 0x00363000,
  0 draws), so nothing was ever presented.
- The game got well into boot: TDATA/UDATA save folders, SaveMeta.xbx and saveimage.xbx read, Media/SE/taxi1.bin
  copied to the cache partition (Partition5), DirectSound up (interrupts on vectors 5 and 6, APU started by the
  title, the DSP command word acknowledged at once by the 0x00109068 hook), taxidsp.bin loaded, four worker threads
  started. After that the main thread made no more kernel calls for the rest of the minute (only the timer
  thread's KeInsertQueueDpc and KeSetEvent): a loop in game code, location unknown from this log.
- Five failed indirect calls from 0x000F3916: D3D SetRenderState (sub_000F38C0) dispatches states 0x76 and up
  through a 29-entry table at 0x00100E30, and 15 of its handlers were unknown. All 15 seeded. A scan of .rdata,
  .data and the library sections for pointer runs into library code found no other real table.
- One stub ran: 0x00036180, from 0x000E49AC (a tail jump target after padding). Seeded.
- `wbinvd` at 0x000FB7F2 reached (no-op, harmless). The kernel bridge read one entry past the thunk table
  (0x0012A52C, "1 stub"); harmless so far.
- Regen: 5,537 functions, 0 failed, 205 unresolved.
- Next test: the same boot test with RECOMP_WATCHDOG_SECS=40, which dumps the main thread's guest call stack.

## 2026-11-05: first regen, the XDK 4531 hooks, seeds

- Mark's OK (2026-11-05) to run xboxrecomp in the cloud.
- First regen (tools/regen.sh, about a minute on 2 cores): 5,470 functions, 0 failed, 900,000 lines of C in 22
  files (sheroes-nx: 22,527 and 2.57 million); 207 unresolved call targets stubbed, 39 spin loops marked.
- Kernel: `gen_ordinal_remap.py`: all 121 imports identity, no remap.
- Hooks found again at 4531 addresses with capstone (evidence in src/recomp_manual.c): BlockOnTime 0x000F86C0
  (device global 0x001051D0, semaphore pointer dev+0x34 and write sequence dev+0x30, 5659's +0x30/+0x2C), DSP
  command post 0x00109068, AC'97 channel reset 0x0010E9E8 (rewritten, as in sheroes-nx), vblank 0x000FBF50 and
  PGRAPH 0x000FC470 handlers (DPC 0x000FC6E0, ISR 0x000FBDB0, vector 3). The display block's flip retire count is
  +0x1F4 (5659 +0x1BC). No per-frame DSP heartbeat (no 0xCCCCCC in DSOUND); the one 10 ms stall is init only.
- tools/seed_sweep.py fixed for this XBE: the [imm] sweep found nothing, because capstone's one-pass section sweep
  stops at the first undecodable byte (now walks each known function) and the address regex wanted 6+ hex digits
  (code here is below 0x100000). 73 candidates; 47 kept after review (26 imm, 11 data, 6 gap, 4 ret), among them
  D3D's DPC and ISR, which only have their addresses pushed. Left out: data read as code and mid-function starts
  (jump-table cases). tools/table_sweep.py: section names for this XBE; nothing new beyond seed_sweep.
- Second regen: 5,521 functions, 0 failed, 206 unresolved. Every one is reached by a jump from a
  tail_jump_alias copy: the static pass cut long functions short and made later blocks second entries, and a
  jump from one alias into another lands on a stub. Where the alias also sits inside a real body (827 of 1,347)
  that copy rarely runs; 520 aliases are the only copy of their code, so some stubs may be live. The coalescer
  refuses alias interior starts (they count as strong entries), so the toolkit now makes each stub log once:
  `[STUB] unresolved 0x... entered from 0x...` (translator.py). The first PC run says which ones matter.

## 2026-11-05: kickoff

- Prior art: no Switch port of Crazy Taxi 3 found. sdiraimondo/ct3-recomp recompiles the 2004 PC port to Linux
  (boots and drives; movies stubbed, a crash after 20 to 30 minutes; no license chosen). The Internet Archive "PC
  recompile" that made the news is the PC game with ct3tweaks applied, not a recompilation. Chose the Xbox version:
  the proven xboxrecomp Switch stack (CoD-nx, sheroes-nx) and the original soundtrack on the disc.
- Disc: Mark's dump is a trimmed XISO (XDVDFS at offset 0), SHA-1 82803ecd7ab0412ea6bc4d9402a8d39e5499c407, so no
  Redump check. Extracted on Mark's PC into `assets/game_root` with a small XDVDFS walker: 401 files, 394 MB.
- `default.xbe`: title 53450004, region 1, entry 0x000E0C34, image end 0x0035BAC0. Every library is 1.0.4531, the
  oldest XDK on any port so far (sheroes-nx 5659, CoD-nx 5849), so no library hook carries over by address. A
  small binary: .text is 0xDE780 bytes (sheroes-nx 0x319E8C).
- Repo started as a copy of sheroes-nx at 2f27b39 (0.2.0): app renamed to `ct3`, entry point, empty seed list;
  sheroes-nx's 5659 hooks moved out of `src/recomp_manual.c` into `docs/hooks-from-sheroes-5659.c.txt`, to be
  found again at this game's addresses. The Sonic Heroes icon and logo are still in place.
