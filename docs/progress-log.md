# Progress log (newest first)

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
