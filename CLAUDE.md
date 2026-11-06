# CT3-nx: notes for Claude

Native PC and Switch port of Crazy Taxi 3: High Roller (original Xbox, NTSC-U, title 53450004, XDK 4531) via
xboxrecomp static recompilation (x86 to C). The repo started as a copy of sheroes-nx (FurryLilManChild/sheroes-nx,
Sonic Heroes, 0.2.0), itself a copy of CoD-nx (FurryLilManChild/cod-nx), both built on antoxa2584x/burnout3-sw (MIT):
its vendored xboxrecomp (the NFS Switch fork: NV2A Vulkan and OpenGL renderers, Horizon support), `src/main.c`,
`src/switch_nx.c`, `src/switch_saltynx.c`, `switch/build.sh` and `tools/regen.sh`. Do not copy code from nfsu1-sw or
nfsu2-sw: they have no license.

The big difference from the earlier Xbox ports: Crazy Taxi 3 links XDK 1.0.4531, older than Sonic Heroes (5659) and
CoD-nx and Burnout 3 (5849). None of their D3D and DirectSound hooks carry over by address; sheroes-nx's set is kept
for reference in `docs/hooks-from-sheroes-5659.c.txt`, to be found again here. The kernel may need an ordinal remap
(`xboxrecomp/tools/kernel_audit/gen_ordinal_remap.py`; Halo's 3911 and Crimson Skies' 5659 needed none). The engine
is Hitmaker's own, not RenderWare, so expect more missed-function seeds than sheroes-nx. CRI middleware is present
(ADX audio in `Media/ADX`, `adx0.afs`; the PSGSFD sections are probably Sofdec).

Related ports: CTDC-nx (Crazy Taxi 1 and 2, Dreamcast, dream-recomp) settled the game-side decisions for this series:
60 fps with every drawn frame shown evenly and no "smooth" frame-skip option, no trigger ramp (a digital trigger is
full at once), and the smallest overclock step (1224 MHz) tried first.

Reference for the game's behaviour (not code to copy): sdiraimondo/ct3-recomp, a recompilation of the 2004 PC port
(Win32, D3D9 through dxvk-native; no license chosen), and stashymane's taxipatch and CT3Tweaks (PC patches).

## Who is working on this

The owner (Mark) is very tech savvy but has almost no coding experience. Claude writes the code and scripts. Mark
runs builds on his own machines, tests on the PC and his Switch, and sends logs. Instructions for him must be numbered,
click by click, with copy-paste commands. Never use em dashes in anything written for him or in the docs. No
decorative emoji.

His hardware: a Windows PC (Lenovo Legion Go), an unpatched V1 Switch on Atmosphere CFW (sys-clk), an Xbox 360 with
Aurora. His disc image is `C:\CT3-nx\assets\CRAZY TAXI 3 High Roller.iso` (a trimmed XISO:
the game partition starts at offset 0); the extracted disc is `C:\CT3-nx\assets\game_root`. A cloud session
reaches both through the device bridge. He moves files to the Switch's SD card himself over USB; he does not want
Claude to control the Switch's folders. He drops Switch logs in `C:\CT3-nx\out\sd\switch\switchlogs`.

Workflow (Mark's choice, 2026-10-28): as on the Xbox 360 ports. Mark builds on his PC in PowerShell
(`tools\win\build_pc.ps1`), runs boot tests and play tests, Claude reads `C:\CT3-nx\out\logs` through the
bridge and pushes fixes. Get the game working on the PC first, then the Switch. Avoid long cloud test runs (the cloud
Linux build on lavapipe needs about 7 minutes to reach a stage).
Mark's standing rule (2026-10-28, repeated after a slip): do not start cloud test runs on your own. Build checks
(tools/linux/wincheck.sh) are fine; anything that runs the game goes to Mark. If the cloud would genuinely be
more efficient for a step, say so and ask him first.
Priority (carried over from sheroes-nx): playability first. Get a shift running and playable before cosmetic
issues.
Confirm with Mark before starting any build, including quick fix builds. Deliver each Switch test build by
replacing the NRO in `C:\CT3-nx\out\sd\switch\ct3\` (not a new folder per test), with every file the test
needs beside it. For A/B diagnostics, make one build cycle through the settings in a single session.

Git: commit as `FurryLilManChild <346934328+FurryLilManChild@users.noreply.github.com>`. History rewrites and force
pushes need his explicit consent.

## Hard rules

- Never commit game data or anything derived from it: the disc image, `default.xbe`, `assets/`, the generated C
  (`out/gen/`), `*_analysis.json`, the toolkit's `xboxrecomp/tools/*/output/`, frame dumps, shader caches,
  `switch_sd/`. `.gitignore` covers these; check `git status` before every commit.
- Every hand-written override in `src/recomp_manual.c` and every seed in `config/seed_functions.json` carries its
  evidence (which run or tool found it, callers, addresses).
- Log each session in `docs/progress-log.md` (newest first).
- Add every mistake or wrong conclusion to `docs/pitfalls.md` as it happens (Mark, 2026-11-05); review it with
  him when wrapping up, then fold it into the port skills.

## Layout

| Where | What |
|---|---|
| `xboxrecomp/` | vendored toolkit (burnout3-sw's copy via CoD-nx, MIT); its tools write to `xboxrecomp/tools/*/output/` (ignored) |
| `src/main.c` | boot: memory layout, kernel, APU, USB, renderer, entry point |
| `src/recomp_manual.c` | hand-written overrides (`recomp_lookup_manual`) |
| `src/ct3_menu.c` | the port menu (+ and -) and `ct3_options.txt` |
| `src/switch_nx.c` | Horizon: log, `ct3_env.txt`, loading screen, profiler, clocks |
| `config/seed_functions.json` | entry points the static pass misses |
| `tools/regen.sh` | XBE to C: disasm, func_id, abi_analysis, recomp (into `out/gen/`) |
| `switch/build.sh` | the Switch NRO (Vulkan with NVK by default; `VULKAN=0` for OpenGL) |
| `tools/win/*.ps1` | the Windows PC build on Mark's PC (same Vulkan renderer as the Switch): `build_pc.ps1`, `boot_test.ps1`, `play.ps1`; logs in `out\logs\` (docs/windows-build.md) |
| `tools/linux/setup_switch_wine.sh` | devkitPro's Windows install under Wine on a cloud machine |
| `assets/game_root/` | the extracted disc (ignored) |

## The game

- Disc: `C:\CT3-nx\assets\CRAZY TAXI 3 High Roller.iso`, a trimmed XISO (XDVDFS descriptor at 0x10000), so it
  cannot be checked against Redump. Disc SHA-1 82803ecd7ab0412ea6bc4d9402a8d39e5499c407 (4,699,979,776 bytes).
  401 files, 394 MB, extracted to `assets/game_root` (`default.xbe` and `Media/`).
- `default.xbe`: title "CRAZY TAXI 3 High Roller", 53450004, region 1 (North America), version 1. Base 0x00010000,
  entry 0x000E0C34, kernel thunks 0x0012A560, image end 0x0035BAC0. Sections .text (0x11000, 0xDE780 bytes),
  PSGSFD00, PSGSFD_I, PSGSFD_B, PSGSFD_P, D3D (0xF3800), XGRPH, DSOUND (0x1059E0), XPP, .rdata, .data (0x1BB980,
  0x1967BC), DOLBY, $$XTIMAGE. Libraries XAPILIB, D3D8, XGRAPHC, DSOUND, XBOXKRNL, LIBCMT, all 1.0.4531.
  SHA-256 eceb4aa071ce9b883566decd70c9ff3c3853003a0bbb4099972232c4edbdb87c.
- Switch paths: `sdmc:/switch/ct3/` (the NRO, `ct3_env.txt`, `ct3_log.txt`), the extracted disc in
  `sdmc:/switch/ct3/game/`.
