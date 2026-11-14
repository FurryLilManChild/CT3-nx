# CT3-nx (branch xbe-recomp)

**Crazy Taxi 3: High Roller** (original Xbox, USA, 2002) as a native Nintendo Switch homebrew program, statically
recompiled from the original Xbox executable. Not an emulator: the game's x86 code is translated to C++ and compiled
for the Switch, and Direct3D 8 and DirectSound are replaced by the Switch's own graphics (deko3d) and sound.

You need your own copy of the game. No game files are in this repository or its releases.

## Status

**In development, not yet booted on this branch.** This branch moves the port from xboxrecomp (the `main` branch:
plays, 30 to 47 fps on the Switch) onto XboxXBE-Recomp, the toolchain that took Sonic Heroes (sheroes-nx) to 60 fps
at the Switch's stock clocks. Crazy Taxi 3 uses an older Xbox SDK (4531) than any game the toolchain had run, so this
branch adds support for it.

Session-by-session notes: [docs/progress-log.md](docs/progress-log.md).

## Install (Switch, once there is a build)

1. Copy `ct3.nro` and `ct3-settings.toml` to `switch/ct3/` on the SD card.
2. Put your game in `switch/ct3/game/`: the trimmed disc image (`.iso`, recommended) or the extracted disc
   (`default.xbe` and `Media`). If both are there, the image is used.
3. Hold **R** while opening any game to get the homebrew menu with full memory, and start **Crazy Taxi 3 (Xbox
   recomp)**.

The supported `default.xbe` (title 53450004) has this SHA-256:
`eceb4aa071ce9b883566decd70c9ff3c3853003a0bbb4099972232c4edbdb87c`.

Saves go to `switch/ct3/hdd/`; logs to `switch/ct3/logs/`.

## The port menu

Press **+ and -** together to open it. Up and down choose a row, left and right change it (A too), B saves and
closes. Settings are saved to `ct3-settings.toml` next to the program.

## Building

The game's code is generated from your own `default.xbe`, so there is nothing to build without the game. The Switch
build is `switch/build.sh` (devkitPro); the PC build is `tools\win\build_pc.ps1`. [docs/brextreme/](docs/brextreme/)
describes how the toolchain works.

## License and credits

GPL-3 ([LICENSE](LICENSE)); the components and their licenses are listed in [LICENSES.txt](LICENSES.txt).

Credits: TGP482 (XboxXBE-Recomp), xsaja (brextreme-nx, street-nx, mvp-nx), maddog847 (MVP05 XBE Recomp),
gerisson1996 (BLACK-NX-PORT), Daniel Hepper (font8x8), the Cxbx-Reloaded team (XbSymbolDatabase, used as a reference
to name the XDK 4531 functions), and the projects listed in LICENSES.txt.

No affiliation with SEGA, Hitmaker, Microsoft or Nintendo.
