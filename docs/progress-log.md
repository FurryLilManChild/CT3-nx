# Progress log, branch xbe-recomp (newest first)

The xboxrecomp version's log (2026-10-28 to 2026-11-09) is `docs/xboxrecomp-era/progress-log.md`.

## 2026-11-16: thirteenth Switch run, and 0.5.0

- Build 13 (PGO from session-02, triggers on ZL and ZR) at stock clocks (cpu 1020, gpu 307): 5.5 min, mostly 57 to
  60 fps, lowest 49.5 to 55 near the end (Small Apple, by Mark's account). Mark: "speed is great", controls right.
- Pause: the hold fired at both pauses (81.6 s, 160.3 s), yet Mark saw the shake in Glitter Oasis and Small Apple,
  not West Coast. The pause log never ran: the game reads its surfaces through BlockOnResource (Sync, synchronous),
  not the late path the log watched (pitfall 50). At each pause the game reads a surface back just before the hold
  copies (81.541 read, 81.555 hold): likely it darkens the frozen picture on the CPU from a buffer that still held
  the older frame, and that upload replaces the hold's copy. Proposed fix (not built): do the hold's copy when the
  game reads a not yet drawn surface at the start of a frame, before the read.
- Mark: release 0.5.0 now; the pause fix and a PGO recording over all three cities can wait. 0.5.0 is build 13
  with the version (switch-0.5.0, NACP 0.5.0) and the README's status, notes saying it plays mostly at 60 fps at
  stock CPU and GPU, with a raised GPU clock recommended for a fully smooth 60.
- Mark: the icon looked wrong. It was Sonic Heroes' (switch/icon.jpg came over with the sheroes-nx tree at the
  branch's first commit; main had the Crazy Taxi 3 one since c7b3cf6). Now main's icon; the settings file's clock note
  no longer speaks of Sonic Heroes. 0.5.0 rebuilt and repackaged (pitfall 51).

## 2026-11-16: twelfth Switch run (PGO recording), the pause shake, controls

- Build 12 (PGO=generate, ZCULL on, 83% handheld) played 5.5 min at stock clocks (cpu 1020, gpu 307) for
  session-02.stream (session-01 on the card was build 9's and is not used).
- Mark: the pause screen shakes again. The log shows the pause hold firing once (336 s) but not at the pause at
  117 s, and at the moment of each pause the game reads a render target back (read-back blocks made at 117.5 s, as
  at 109.8 s in run 9). The deko3d backend delivers such reads late (a copy from a frame or two earlier,
  DeliverLate), so the game's frozen copy may be an older frame than the other buffer holds. Unproven: build 13
  logs the 20 frames after each read-back (read-backs and which frame they came from, uploads, clears, presents).
- Mark: the triggers should be gas and brake. Default maps now put ZL and ZR on the Xbox triggers and L and R on
  white and black (menu.cpp kByPosition, kByName).
- Build 13: PGO=use from session-02, the controls, the pause log.

## 2026-11-15 (late night): eleventh Switch run, the handheld GPU sweep

- Handheld GPU clock (307 MHz) set at 9.4 s and kept for the sweep; CPU 1020. ZCULL on the whole run; Mark reported
  nothing wrong on screen.
- GPU ms a frame in the city by phase (15 s each, draws a frame in brackets):
  - as set (960x720, anisotropic 2): 17.4 (1,075), 18.2 (1,264), 17.5 (1,028). Run 10 at the same clock with ZCULL
    off: 22.1 to 22.8. ZCULL cut the GPU's time by about a fifth.
  - anisotropic off: 18.0 (1,277), 16.4 (669), 17.8 (894): no gain.
  - render 83% (800x600): 9.3 (1,281), 9.3 (923): about half, more than the pixel count (0.69) would give.
  - render 67% (640x480): 4.7 (907), 4.7 (684).
  At 83% the frame rate was 56.5 to 58 with the render thread the limit again (deko.cpp lost part of its PGO
  profile to the sweep's changes: about 15 ms at 1,300 draws).
- Decision for the next builds: ZCULL stays on; the resolution has a handheld and a docked setting (render_percent,
  render_percent_docked), defaults 83% handheld and 67% docked (720 of 1,080 lines: the pixels of the handheld 100%
  picture, until docked is measured); the menu's Resolution row edits the current mode's. Sweep off. Then a new PGO
  session with this final code: build 12 records, build 13 is the result.
- Mark then chose to keep ZCULL on after all (it was on for the whole of run 11, which showed nothing wrong), and
  asked that no build start until its contents are settled (pitfall 49).

## 2026-11-15 (night): tenth Switch run (PGO), and the GPU clock

- PGO worked on the CPU side: render thread about 11 us a draw (12.4 ms at 1,051 draws, 12.7 at 1,112), 58 to 60 fps
  in the city with the GPU at 768 MHz.
- Mark found sys-clk had been holding the GPU at 768 MHz (the docked clock) in handheld for many of our runs. Every
  log said so at start ("clocks: now cpu 1020, gpu 768, mem 1331 MHz") and I never flagged it (pitfall 48). He set
  the handheld clock (307 MHz) twice in this run: GPU 22.1 to 22.8 ms a frame at 920 to 1,230 draws, 40 to 42 fps;
  at 768 MHz the same scenes take 8.3 to 9.6 ms. So at stock handheld clocks the GPU, not the CPU, is the limit,
  and it needs to drop about a third.
- Next build (handheld GPU): ZCULL on (the GPU's coarse depth test), and a sweep that, once the city is drawn,
  cycles every 15 s through: as set, anisotropic filtering off, render 83%, render 67% (the Xbox's 640x480), with a
  line per phase (GPU ms, fps, draws). Mark plays it at the handheld GPU clock.

## 2026-11-15 (night): eighth Switch run

- Mark: "definitely the closest we've been". At stock clocks the shift ran 54.5 to 60 fps (10 s windows: 55.6, 54.5,
  59.9, 59.8, 59.8, 57.6, 57.7, 60.1); render thread 11.6 to 16.6 ms at 820 to 1,190 draws. No shader compiled in
  the whole run: the card's cache (223 modules) now holds everything this route uses, so the hitches are gone.
- Draws by kind in the city: 450 to 560 unlit, 550 to 630 lit, 12 to 30 vertex program, about 1 blended. The unlit
  trim cut uploads from 2.9 to 2.0 MB a frame.
- Instancing census: 0 draws repeat the one before but for their placement. Each model comes from its own push
  data or vertex shader input, so Cold Fear's technique (same vertex pointers, same state) has nothing to merge
  here. The census is taken out again.
- Next (Switch speed, the brextreme-nx and sa2-nx last step): PGO. Build 9 is the instrumented NRO (PGO=generate,
  counters to sdmc:/switch/ct3/pgo/ every minute); Mark plays 10 minutes or more through every city; the merged
  profile builds build 10 (PGO=use). brextreme-nx measured about 6% from PGO.
- Mark: the pause screen shakes again on the Switch (fixed on main in PC play tests 17 to 19). Cause as on main:
  the pause menu is drawn over the last picture without a colour clear, into each of the two buffers in turn,
  which hold the last two frames of play. Fix in deko.cpp Clear (main's RECOMP_PAUSE_HOLD): on the first frame that
  clears only depth or stencil, before any draw, after a full frame shown from the other buffer, that frame is
  copied into this buffer first. Build 9 rebuilt with it before Mark played it, so the PGO session matches.
- PGO session (build 9): 447 s of play, one 1.1 MB stream, 111 objects (64 of them generated code); the pause hold
  fired once (03EC2000 into 03D96000). Merged in the cloud with devkitA64's gcov-tool merge-stream (the Wine paths in
  the stream land in build-switch), pgo.gcda removed, objects deleted, PGO=use build: no coverage-mismatch warning.
  Build 10 is the result.

## 2026-11-15 (evening): seventh Switch run

- Sound fine as far as Mark could tell (low volume). Threads placed as planned (title core 0, render core 1).
- At stock clocks the busiest stretches still dip: 49.9 fps at 1,463 draws (render thread 16.5 ms), 58.3 at 1,264
  (14.4 ms). Per draw 11.2 to 11.4 us against 11.7 to 12.1 in the same kind of scene in run 6: the core layout and
  the texture signatures gave about 5%, not the jump hoped for.
- Mark turned CPU boost on from 112 s to 156 s: 1785 MHz, render thread 7.5 to 8.8 ms at 950 to 1,090 draws, 58 to
  60 fps (60.0 in one 10 s window, the misses being shader compiles).
- Shader compiles: 23 (1.9 s) at the start of the shift and about 50 in the run, though the card's cache keeps
  them (174 SPIR-V modules at start, 145 in run 3). Not explained yet: the next build logs each new shader by name.
- Next build (Switch speed): an unlit draw that does not blend sends 1,088 of the 2,528 bytes of draw constants
  (DrawConstantBytes; the shader layout is unchanged, so the card's cache still holds); a perf line counts draws by
  kind (vertex program, unlit, lit, blended); new shaders logged by name.
- Mark: stock clocks stay the target (Sonic Heroes reached 60 at stock); take promising techniques from the other
  ports. COLD-FEAR-NX-PORT (MIT, sp00nz's xboxrecomp, nv2a_vk.c "Instancing"): a run of draws that repeat the one
  before but for the transform constants becomes one instanced draw (a quarter of NFSU2's draws). Build 8 also
  counts CT3's such runs first (g_placed_generation: the state generation not moved by the model view or world
  matrix; perf line "repeating the one before but for its placement"), to size the work before writing it.

## 2026-11-15: sixth Switch run, and a look at the other Xbox ports

- Mark: still slowdowns into the 40s. The shift: 47.4 to 59.6 fps; texture stages remembered 68 to 91% (was 61 to
  64%), but the render thread still 15 to 17 ms a frame at 1,100 to 1,430 draws, texture binding 2.7 to 3.6 ms.
- Mark asked for a look at robzilla10001/SwitchPorts' Xbox ports. Read (READMEs and, for ShaolinMonks-NX, its source
  as a reference): ShaolinMonks-NX (XboxXBERecomp, Vulkan, 60 fps at full speed) pins every guest thread to one core
  and its renderer alone on another (port/nx/threads.cpp); it hashes textures with independent multiply chains (about
  four times faster); it marks state pieces written so the hand-off compares only those (port/common/state_dirty.h).
  COLD-FEAR-NX-PORT (xboxrecomp) instances repeated draws (RECOMP_VK_INSTANCE). doa2u/doa3-nx keep the renderer
  on core 1 and audio on core 2. WOTW-NX, nights-nx, OpenTrickyNX, conker: nothing new for this.
- Our port had core_layout off: all 13 guest threads, the sound and vertical blank threads and the render thread on
  any core at once, the title's time critical threads above the render thread's priority. Next build (Switch
  speed): core_layout on by default with the render thread alone on core 1 (title core 0, every other thread core
  2), and texture signatures through four independent multiply chains (2x on x86 in a quick test, likely more on the
  Switch's cores).

## 2026-11-15: fifth Switch run

- Mark: "still making progress". The shift ran 50.6 to 59.7 fps (10 s windows: 57.9, 50.6, 59.7, 58.3, 58.0, 53.9,
  48.1 at a busy corner), the render thread 12 to 16.8 ms a frame at 870 to 1,230 draws (was 16 to 21 ms):
  constants 2.2 to 2.5 ms (were 3.2 to 4.1), textures still 2.7 to 3.5 ms.
- The new lock counter shows 0 to 0.1 locks of buffers a frame: buffer locks were not why texture stages were
  forgotten (pitfall 45). The stage memo kept only the last bind of each stage, and Crazy Taxi 3 changes texture
  most draws, so it matched 61 to 64%.
- Mark asked what Sonic Heroes had that this has not: its settings are the same as ours except the profiler, and
  everything it needed is in (disc image, sampler key, draw_copy, lazy opens). The step left was its last one:
  diagnostics off before the measurement run. Sonic Heroes needed no render thread work because it drew about 380
  times a frame; this city draws 1,100 to 1,300.
- Next build (Switch speed): each stage remembers its last 64 binds, not one; the profiler (profile = false) and the
  file report (kFileReport) off for a clean measurement.

## 2026-11-15: fourth Switch run

- Mark: "definite improvement". The vertical blank fix took the shift from 31 to 42 fps to 47 to 58 fps (10 s
  windows: 52.1, 42.9, 57.6, 58.0, 57.2, 49.0, 48.0, 49.6, 47.3), menus 55 to 60. The title thread now sleeps 25% of
  the time; the render thread is the limit: 12 to 21 ms a frame at 680 to 1,280 draws (GPU 8.4 to 9.6 ms).
- Hitches of 200 to 400 ms every 10 to 20 s: first-time shaders (1 or 2 compiled in those windows, 124 to 250 ms
  each). They should fade as the shader cache on the card fills.
- Render thread by source line (new scratch tool prof_incl.py: each sample charged to the line of Draw it was under):
  Draw is 84% of the thread; texture binding 14.6% (Hash 4.7%), transform constants 10.9%, the draw constant compare
  7.5% (memmove 4.6%), Assemble 6.5%, BindState 5.7%, samplers 3.5%, static vertices 2.8%.
- Next build (Switch speed, render thread): transform constants build only what the draw's shader reads (normal
  matrices when lit or texgen from normals, blend matrices when blending, texture transforms per stage); draw
  constants compared with the copy already in the upload ring (CPU cached) instead of a second copy, the transform
  first; locks of vertex and index buffers no longer move the texture dirty epoch (which made every remembered
  texture stage look again; 60% remembered), with a new perf line counting locks of each kind.

## 2026-11-15: third Switch run

- The two spinning threads are gone (no busy samples outside the title and render threads). The shift still ran
  31 to 42 fps, but the render thread fell from 25 to 28 ms to 14 to 21 ms a frame at 900 to 1,430 draws (GPU 8.5 to
  9.6 ms, recording 2.3 to 3.5 ms). Menus 53 to 58 fps.
- The title thread now sets the pace: 57% of its time asleep in BlockUntilVerticalBlank (0x0F5250). The game ends
  every frame with Swap then BlockUntilVerticalBlank (0x0A7357, 0x0A735C), and the runtime's HLE slept a flat 16 ms:
  about 12 ms of work plus 16 ms made 28 ms frames, about 35 fps. The same code runs on the PC, which likely explains
  its 45 to 49 fps there. Fixed: wait for the vertical blank thread's next tick (the console's behaviour), or the
  next 16.667 ms step of the steady clock.
- Next build (Switch speed): that fix alone. Then the render thread (14 to 21 ms) is the limit: constants 2.7 to 4.3
  ms (reused only 60%), textures 2.8 to 3.9 ms, upload 1.9 to 3.1 ms (2 to 3.4 MB a frame), assemble 1 to 1.7 ms.

## 2026-11-14 (late night): second Switch run

- The static vertex cache worked: the title thread's recording fell from about 6 ms to about 3 ms a frame. But the
  shift still ran 31 to 38 fps, render thread 25 to 28 ms, about 3 MB a frame still uploaded (constants and indices).
- The profile (prof.bin) shows why the render thread is slow: two guest threads each burned a whole core for the
  whole run, leaving the render and title threads one and a bit of the Switch's three cores.
  - guest_24, the game's sound command thread (0xA1400), calls Sleep(20) each pass (0xA16E3: push 0x14, call
    0x0E0BB7). 0x0E0BB7 is Sleep (push 0, push ms, call SleepEx 0x0E0B53), but a weak 3911 signature from
    XbSymbolDatabase (8 pairs, no xrefs) named it XNetStartup, so the runtime's XNetStartup HLE returned at once and
    the 20 ms sleeps vanished. game.toml now sets XNetStartup = 0 (gen_gametoml.py force_zero); signatures.toml
    re-signed (140 functions). A check of every other weak match (12 or fewer pairs, no xrefs) still named: 29 are
    inside the D3D, DSOUND and XPP sections (library code only, and the PC run draws and sounds right); the one in
    game code, XLaunchNewImageA, has no HLE, so its original runs either way.
  - guest_28 spins in CRI's idle loop 0x000B33D0 (counts free CPU until a flag is set). On the Xbox it only ran
    when nothing else wanted the CPU. Hook in game/src/game/hooks.cpp: the same loop with a 1 ms sleep each pass.
  - Render thread by function: waiting 24%, DekoBackend::Draw 11%, memmove 6% (constants), gpu::Hash 5% (most of it
    from Draw), FillTransformConstants 5%, Describe 3%, Assemble 2.5%. Per-draw costs are the next target once the
    cores are free.
- Next build (Switch speed): the two fixes above, nothing else, so the run shows what freeing the cores gives.

## 2026-11-14 (night): first Switch run

- Into a shift with the ISO in the game folder. Stock clocks (CPU 1020 MHz): menus 51 to 54 fps, the shift 29.7 to
  30.3 fps (10 s windows). The render thread is the limit: 26 to 31 ms a frame at 1,130 to 1,170 draws (assemble
  1.7, textures 3.6, constants 4.2, upload 4.0 with 6 to 7 MB of vertices a frame, bind 1.0, samplers 1.0, record
  1.2, acquire and present 2 to 5), GPU 9.3 ms, the title thread 6 to 7 ms recording (8 to 9 MB a frame of vertex
  copies, draw_copy). Sonic Heroes: ~380 draws, render thread 11 to 14 ms: about the same cost a draw, three times
  the draws.
- Freezes of up to 5 s as the city first loaded: 100 shaders compiled by uam (shadercache on the card from now on).
- Next build: the model draws EndPush makes skip the title thread's copy (gpu::g_draw_static) and deko3d keeps
  their vertices in a 48 MB static vertex cache (a span uploaded once; head and tail compared at every use, hashed
  again every 8 frames; a full cache starts over after the GPU finishes); the upload ring 32 to 24 MB a slot;
  kPushReport off; profile = true in ct3-settings.toml for the render thread's breakdown by function.

## 2026-11-14 (night): fourth PC boot

- Cars, traffic and characters draw now (the EndPush change); in a shift 400 to 500 draws a frame, 50 fps on the
  Legion Go (render thread 5 ms, GPU 4 ms; one 10 s stretch at 35 fps with 9.5 ms a frame of pipeline creation: first
  time shaders, cached afterwards). The 12 EndPush reports were used up by the boot screens (inline vertices only).
- No city: a black void under the cars. hdd/Z holds a 10.9 MB `tmp` and the log has `missing Z:\_bank11.mdl`. As on
  main (2026-11-05, play test 11): the game copies each city model bank to Z:\tmp and renames it while it is open.
  The runtime's rename (FileRenameInformation) was there, but Windows refuses to rename a file this process holds
  open (and FAT refuses to rename over an existing file). Fixed in NtSetInformationFile: on failure close the file,
  remove the target if asked to replace, rename, and reopen it; each rename goes to the log.
- To check on this runtime later (fixed on main for CRI middleware threads, may or may not apply here): critical
  section hand-over to the oldest waiter (main 2026-11-08), NtResumeThread ordering, a stand-in KTHREAD for ended
  threads, suspension counts, the CRI lock spin loop at 0xB33D0 (main's progress log).

## 2026-11-14 (night): third PC boot

- Reached the game, but no 3D at all: in the menus and character select the characters never appear, and in a
  shift only the HUD timer draws. The perf lines show 17 to 19 draws a frame in a shift.
- Cause (likely, from the code): the models are not drawn through D3D draw calls. The title copies each model's own
  push data between BeginPush and EndPush (0xA4790, from 0xA4885 and four more; character code 0xA8A00, 0xA8B60,
  0xA8CD0, 0xA8E70) inside a begin/end pair. The runtime's EndPush drew only inline vertices (0x1818); indices into
  the vertex buffers (ARRAY_ELEMENT16/32, 0x1800/0x1808) and vertex ranges (DRAW_ARRAYS 0x1810) were taken as
  render states. EndPush now draws all four kinds, and reports what the first 12 blocks hold (kPushReport, on).

## 2026-11-14 (evening): second PC boot

- Saves work: U: and T: are mapped (`files: link \??\U: -> ...\UDATA\53450004`), and the game wrote its title
  image, title meta and a save folder under hdd/E/UDATA/53450004. The menus run at the 60 fps cap (55.9 fps over
  the first 10 s including loading), render thread 2 ms, GPU 1.4 ms a frame on the Legion Go.
- Crash at character select: read of guest 0x4 at 0xFD89F < 0xFE795 (the lazy state setters 0xFE750) < 0xFE7F8
  (CDevice_SetStateVB 0xFE7D0) < 0xF963D (0xF9630) < the title's 0xA479B, right before a BeginPush block. The
  library's per-draw state push ran on state the runtime never fills. Fixed: CDevice_SetStateVB named (XbSymbolDatabase
  agrees) and replaced by a no-op HLE; the runtime sends state at its own draws.
- The `files: missing Z:\_*.bin` lines at start are the game probing its cache partition (Z:) for files it writes
  there later; expected on a fresh hdd folder.

## 2026-11-14 (evening): first PC boot

- Mark's PC build compiled (Vulkan and D3D12 exes). The game showed the company logos, then crashed; before the logos
  it said it could not create a save.
- Crash (build-pc/logs/ct3.log): read of guest 0xB4 under 0x107090 < 0x107484 < 0xA0D02. The game's call 0xA0CFD goes
  to IDirectSoundBuffer_SetConeAngles 0x107463, whose original code ran on the runtime's buffer object: game.toml
  had no entry for it. Cause: [symbols] was built from 5659's signatures.toml, which lacks the names Sonic Heroes
  added by hand (pitfall 40). Fixed: the generator now takes every name the runtime uses; SetConeAngles 0x107463,
  SetEG 0x106678 and SetPitch 0x10665C added with evidence (signatures: 140 functions).
- Save message: XapiInitProcess (0xE451C) checks a disc that only Xbox DVDs may run from (certificate AllowedMedia 2)
  with IOCTL_SCSI_PASS_THROUGH_DIRECT, MODE SENSE(10) of the DVD security page 0x3E (0xE442D), and wants it
  authenticated (data +0xB = 1, +0xA and +0xC non-zero). The runtime returned zeros, the check failed five times and
  XapiBootToDash was called, so XapiSetupPerTitleDriveLetters (0xE4373) never mapped U: and T:. No UDATA folder was
  created under hdd/E. Fixed in NtDeviceIoControlFile: the page is answered as an authenticated Xbox DVD.
- File diagnostics (kFileReport in files.cpp, on for now): missing files, failed opens, drive links and device
  controls go to the release log.

## 2026-11-14: branch xbe-recomp started

- Mark: review the Sonic Heroes move (sheroes-nx `xbe-recomp`, 60 fps at stock clocks on 2026-11-14) and the updated
  xbox-switch-port skill, and make it happen for Crazy Taxi 3.
- Branch `xbe-recomp`: sheroes-nx's xbe-recomp tree (5abfcf1, Version 0.5.0), renamed to ct3 (`ct3.nro`,
  `ct3-settings.toml`, `CT3_*`, `sdmc:/switch/ct3/`), title ID 53450004. main's docs in `docs/xboxrecomp-era/`,
  `docs/pitfalls.md` carried on.
- XDK 4531 is not in the SDK. With the 5233, 5659 and 5849 signatures xbe_symbols named 45 functions. Cxbx-Reloaded's
  XbSymbolDatabase (MIT; its signature data read by our own script, `scratchpad` only, nothing of it run or copied)
  named 298 functions and derived 50 data addresses, many of which confirm hand findings (render target and depth
  at device +0x2070/+0x2074, textures +0xA78, palettes +0xA88, pixel and vertex shader +0x370/+0x380, the vertical
  blank event +0x2448, render states 0x102460, deferred texture states 0x102260, the device pointer 0x1051D0).
- Call scan: game code calls 111 functions in D3D, XGRPH, DSOUND and XPP; all named now. 25 by hand, each with its
  evidence in game.toml (the database agrees on 9 of them).
- `sdk/xdk/4531/layout.toml`: 43 values, each proven at a CT3 address. 4531's CDevice differs from the other builds
  almost everywhere (render target 0x2070 against 5659's 0x1A04). Its miniport keeps 5849's field order 0x38 further
  in (embedded at device +0x227C).
- `sdk/xdk/4531/signatures.toml` signed from the new game.toml (137 of 250 functions, 12 data); xbe_symbols run again
  from scratch finds the same 137.
- Gaps found and filled in the runtime (all marked `CT3-nx:`):
  - D3DRENDERSTATETYPE: 4531 numbers deferred states from 83 (5659: 92) and complex ones from 118 (136), without
    D3DRS_SAMPLEALPHA. New layout keys RenderState_DeferredFirst, _ComplexFirst, _HasSampleAlpha (identity by
    default); SyncDeferred, SetComplex and SetRenderStateNotInline map through GuestRs/HostRs. Texture stage state
    numbering is the same (ColorOp is 12 in both: SetTextureStageStateNotInline 0xF4337).
  - The Hitmaker engine hands over its own matrices: HLEs for D3DDevice_SetModelView, SetVertexBlendModelView and
    GetProjectionViewportMatrix (the last after D3D_UpdateProjectionViewportTransform 0xF3A50: projection times the
    viewport scale, without the viewport's X and Y). The backends (gfx_common.h, d3d12.cpp) use them while set,
    converting the console's screen space to clip space.
  - D3DDevice_SetVertexShaderInput (stream buffers with offsets instead of SetStreamSource): HLE, and a per-stream
    offset in State::Stream.
  - IDirectSoundBuffer_SetConeOrientation, SetConeOutsideVolume, SetMode and SetRolloffFactor (per buffer): HLEs.
- game.toml: 2,105 seeds (main's function starts the image points to, plus main's 82 hand seeds). xbe_recomp: 6,151
  functions, 311,116 instructions. Two candidates dropped after the first Switch compile check stopped at a goto to
  an undefined label: 0x000DA210 and 0x000DDBEC are jump table data in .text that main's disassembler took for
  functions.
- Cloud compile check of the Switch NRO (devkitA64 GCC 16, deko3d and NVK from /opt/nvk-sdk): builds, 308 steps, no
  errors; the NRO is 28.2 MB. Not sent: the PC boot comes first. (d3d12.cpp, the PC's D3D12 renderer, is not part of
  the Switch build; Mark's PC build checks it.)
