# Progress log (newest first)

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
