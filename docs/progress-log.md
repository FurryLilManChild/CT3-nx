# Progress log (newest first)

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
