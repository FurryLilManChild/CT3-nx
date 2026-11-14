# CT3-nx: notes for Claude (branch xbe-recomp)

Native PC and Switch port of Crazy Taxi 3: High Roller (original Xbox, NTSC-U, title 53450004, XDK 4531), on this
branch rebuilt on TGP482's XboxXBE-Recomp (`sdk/`, MIT) by way of sheroes-nx's `xbe-recomp` branch (Sonic Heroes,
60 fps at stock clocks), itself built on xsaja/brextreme-nx (Bloody Roar Extreme, GPL-3), street-nx and mvp-nx, whose
Switch layer comes from gerisson1996/BLACK-NX-PORT (no license; Mark: fine to build on). The x86 code is translated
to C++ (`xbe_recomp`), and Direct3D 8 and DirectSound are replaced at the API level (`sdk/runtime/xdk`): no NV2A
command stream, a render thread replays recorded draws with deko3d or Vulkan.

Why (Mark, 2026-11-14): the xboxrecomp version on `main` reached 30 to 47 fps after many rounds; the same move took
Sonic Heroes from about 42 fps to 60 at stock clocks. main's notes are in `docs/xboxrecomp-era/` (the knowledge base
for this game: CRI middleware, the D3D addresses, the profiles), sheroes-nx's in `docs/sheroes/`.

License: GPL-3 (`LICENSE`), with the components listed in `LICENSES.txt`. Upstream docs: `docs/brextreme/`.
Comments marked `// STREET:`, `// BREXTREME:` and `sheroes-nx:` say which port made a change; keep them. Mark ours
`CT3-nx:`.

XDK 4531 is older than every build the SDK knew (5233, 5659, 5849). `sdk/xdk/4531/` was made for this game:
`layout.toml` (every value proven at a CT3 address) and `signatures.toml` (signed from `game/config/game.toml`).
Function names first came from Cxbx-Reloaded's XbSymbolDatabase (MIT code, ODbL data: its 4531 signatures read as a
reference by our own script, none of its code run) and by hand. 4531 numbers its render states differently: the
`RenderState_*` layout keys map them (d3d8.cpp `GuestRs`/`HostRs`).

Related ports: CTDC-nx (Crazy Taxi 1 and 2, Dreamcast) settled the game-side decisions for this series: 60 fps with
every drawn frame shown evenly and no "smooth" frame-skip option, no trigger ramp (a digital trigger is full at once),
and the smallest overclock step (1224 MHz) tried first.

## Who is working on this

The owner (Mark) is very tech savvy but has almost no coding experience. Claude writes the code and scripts. Mark
runs builds on his own machines, tests on the PC and his Switch, and sends logs. Instructions for him must be numbered,
click by click, with copy-paste commands. Never use em dashes in anything written for him or in the docs. No
decorative emoji.

His hardware: a Windows PC (Lenovo Legion Go), an unpatched V1 Switch on Atmosphere CFW (sys-clk), an Xbox 360 with
Aurora. His disc image is `C:\CT3-nx\assets\CRAZY TAXI 3 High Roller.iso` (a trimmed XISO: the game partition starts
at offset 0); the extracted disc is `C:\CT3-nx\assets\game_root`. A cloud session reaches both through the device
bridge. He moves files to the Switch's SD card himself over USB; he does not want Claude to control the Switch's
folders. He drops Switch logs in `C:\CT3-nx\out\sd\switch\switchlogs`.

Workflow: Mark builds on his PC (`tools\win\build_pc.ps1`), runs boot tests and play tests, Claude reads the logs
through the bridge and pushes fixes. PC first, then the Switch. Do not start cloud test runs that run the game on
your own; build checks are fine. Present findings and the next build's plan, and wait for his go-ahead before each
test build. Mark's design choices are binding (2026-11-08): every build proposal names the plan step it serves;
anything off the plan is raised with him first and waits for his yes before code is written. Goal: 60 fps at stock
clocks. Priority: playability first.
Deliver each Switch test build by replacing the NRO in `C:\CT3-nx\out\sd\switch\ct3\` (not a new folder per test),
with every file the test needs beside it.

Git: commit as `FurryLilManChild <346934328+FurryLilManChild@users.noreply.github.com>`. History rewrites and force
pushes need his explicit consent.

## Hard rules

- Never commit game data or anything derived from it: the disc, `default.xbe`, `assets/`, `game/assets/`,
  `game/generated/`, `out/`, frame dumps, shader caches, `switch_sd/`. Check `git status` before every commit.
- Every hand-named XDK function in `game/config/game.toml` and every value in `sdk/xdk/4531/layout.toml` carries its
  evidence (the 4531 address and instruction that proves it).
- Log each session in `docs/progress-log.md` (newest first).
- Add every mistake or wrong conclusion to `docs/pitfalls.md` as it happens; review it with Mark when wrapping up,
  then fold it into the port skills.

## Layout

| Where | What |
|---|---|
| `sdk/` | XboxXBE-Recomp: `tools/recomp` (xbe_recomp), `tools/symbols` (xbe_symbols), `runtime/` (kernel, D3D8, DSound, render thread), `xdk/<build>/` (signatures and layouts; 4531 is ours) |
| `game/config/game.toml` | the recompiler's input for Crazy Taxi 3: sections, seeds, `[symbols]`, `[data]` |
| `game/src/` | game hooks (`X86_FUNC(sub_...)` replaces a recompiled function) and `game_info.cpp` |
| `pc/` | the Windows host (D3D12 and Vulkan exes) |
| `switch/` | the Switch host (deko3d and Vulkan renderers, audio, input, profiler, PGO, port menu) |
| `tools/build_pc.cmd`, `tools/win/build_pc.ps1` | the Windows build: tools, recompile into game/generated, PC exes |
| `switch/build.sh` | the Switch NRO |

## The game

- Disc: `C:\CT3-nx\assets\CRAZY TAXI 3 High Roller.iso`, a trimmed XISO. 401 files, 394 MB.
- `default.xbe`: base 0x00010000, entry 0x000E0C34, kernel thunks 0x0012A560. Sections .text (0x11000), PSGSFD00,
  PSGSFD_I, PSGSFD_B, PSGSFD_P (CRI Sofdec), D3D (0xF3800), XGRPH, DSOUND (0x1059E0), XPP, .rdata, .data, DOLBY,
  $$XTIMAGE. Libraries XAPILIB, D3D8, XGRAPHC, DSOUND, XBOXKRNL, LIBCMT, all 1.0.4531. SHA-256
  eceb4aa071ce9b883566decd70c9ff3c3853003a0bbb4099972232c4edbdb87c.
- D3D: device at 0x1026D0 (pointer 0x1051D0), render states 0x102460, deferred texture states 0x102260, dirty flags
  0x102258, streams 0x1051D8. The engine draws through SetModelView, SetVertexBlendModelView and
  SetVertexShaderInput (HLEs added for 4531), BeginPush/EndPush blocks, DrawVerticesUP and DrawIndexedVertices.
- Switch paths: `sdmc:/switch/ct3/` (the NRO, `ct3-settings.toml`, `logs/`), the game (the .iso, recommended, or the
  extracted disc) in `sdmc:/switch/ct3/game/`.
