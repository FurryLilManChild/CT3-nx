# Progress log, branch xbe-recomp (newest first)

The xboxrecomp version's log (2026-10-28 to 2026-11-09) is `docs/xboxrecomp-era/progress-log.md`.

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
