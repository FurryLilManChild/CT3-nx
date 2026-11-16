# CT3-nx (branch xbe-recomp)

**Crazy Taxi 3: High Roller** (original Xbox, USA, 2002) as a native Nintendo Switch homebrew program, statically
recompiled from the original Xbox executable. Not an emulator: the game's x86 code is translated to C++ and compiled
for the Switch, and Direct3D 8 and DirectSound are replaced by the Switch's own graphics (deko3d) and sound.

You need your own copy of the game. No game files are in this repository or its releases.

## Status

**0.5.0: playable.** Every shift in all three cities (West Coast, Small Apple, Glitter Oasis) plays from start to
finish. At the Switch's stock clocks (CPU 1020 MHz, handheld GPU 307 MHz) it runs at or near 60 fps most of the time,
with dips into the 50s in the busiest parts of Small Apple. For a fully smooth 60 fps, raise the GPU clock (with
sys-clk, for example; the docked clock, 768 MHz, holds 60 in handheld).

This branch moved the port from xboxrecomp (the `main` branch: 30 to 47 fps on the Switch) onto XboxXBE-Recomp, the
toolchain that runs Sonic Heroes (sheroes-nx). Crazy Taxi 3 uses an older Xbox SDK (4531) than any game the toolchain
had run, so this branch adds support for it.

Known issues in 0.5.0:
- The picture behind the pause menu shakes in Glitter Oasis and Small Apple (not in West Coast).
- Docked play has not been measured yet; its resolution starts at 67% (720 of 1080 lines) to be safe.
- The first time an effect appears, its shader is built and the game stalls briefly. Shaders are saved on the SD card
  (`switch/ct3/shadercache/`), so this fades after the first plays.

Session-by-session notes: [docs/progress-log.md](docs/progress-log.md).

## Install (Switch)

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

The Resolution row is kept separately for handheld (default 83%: the game drawn at 800x600) and docked (default 67%);
it changes the one for the mode you are in. Lower is faster. Controls: ZR is gas, ZL is brake.

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
