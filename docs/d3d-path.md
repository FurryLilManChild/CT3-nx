# The Direct3D path (design, 2026-11-08)

Goal (Mark, 2026-11-08): full speed (60 fps) on the Switch, a requirement. Native static recompilation:
the Switch does the work itself instead of re-enacting the Xbox doing it.

## Why

Switch test 9a, a shift at about 31 fps, per frame:

- Main thread about 19 ms of work: the game's own code about 8 ms, Microsoft's Direct3D (the XDK 4531 D3D
  section, 0x000F3800 to 0x00105700, lifted with MMIO-checked memory accesses) about 7 ms, CRT and XAPI about 2 ms.
- Graphics thread about 23 ms: reading the NV2A pushbuffer back word by word (nv2a_pb_scan, nv2a_pb_exec_method)
  about 6 ms, our per-draw translation (vk_draw_raw: vertex copies, uniform blocks, descriptors) about 10 ms,
  NVK a few ms.

Both must be under 16.7 ms. The pushbuffer is built by one thread and parsed by the other only because the Xbox
GPU needed it; on the Switch it is pure cost on both sides.

## Survey facts

- 59 D3D entry points are called from outside the D3D section (xrefs.json); 133 calls go to 0x000F3890 (a 39-byte
  function, likely the table-driven SetRenderState).
- No pushbuffer subroutine calls in a whole shift ([PBCALL] counter): the game does not use RunPushBuffer, so every
  GPU command it issues comes from these entry points.
- The costliest D3D work on the main thread is the lazy state flush 0x000FE7D0 (through the per-draw wrapper
  0x000F9630, 13.5% of main-thread samples). It dispatches on the dirty mask at 0x00102258 to group handlers
  (0x000FD3B0, 0x000FD520, 0x00100A30, 0x000FD6A0, 0x000FD890, 0x000FDA70, 0x000FE210, 0x000FE550). 0x000FE210 is
  the fixed-function light setup (10%): vector times matrix 0x000F9770, normalise 0x000F9A60, colours
  0x000FDF30 / 0x000FDE30, all x87 or SSE.

First identification from the NV2A methods each writes (tools: scratch d3dmap.py; to be confirmed one by one):

| Entry | Calls | Methods written | Likely |
|---|---|---|---|
| 0x000F9630 | 1 | (flush, then 0x1760) | per-draw lazy state flush |
| 0x000F7AB0 | 6 | 0x1760 x16, 0x17FC, 0x1D70 | DrawVertices |
| 0x000F7C10 | 2 | 0x1760 x16, 0x17FC, 0x1808, 0x1D70 | DrawIndexedVertices (inline indices, 0x1808) |
| 0x000F7470 | 5 | 0x1720 x16, 0x1760 x16 | stream / vertex array setup |
| 0x000F84F0 | 5 | 0x1760 x16 | vertex format |
| 0x000F8000 | 12 | 0x260, 0xA60, 0x1E40 ... | pixel shader (register combiners) |
| 0x000F6420 | 4 | 0x0580 x12 | SetTransform |
| 0x000F4760 / F7310 / F5DA0 / F4A60 | 26 / 12 / 9 / 1 | 0x394, 0xA20, 0xAF0, 0xB80 | viewport and vertex shader constants |
| 0x000F5AA0 | 2 | 0x300 | alpha / render states |
| 0x000F94B0 | 1 | 0x100, 0x12C, 0x16EC ... | Present (Swap) |
| 0x000F68E0 | 1 | many | device init / reset |

## Approach: a hybrid, migrated one entry point at a time

1. Our native replacement for a D3D entry point records what the call means (the draw, its vertex
   arrays and indices as guest pointers, the state it changes) in a compact record, and puts one private
   command into the pushbuffer that points at it. Ordering with everything not yet migrated stays exact,
   because the record is consumed in pushbuffer order.
2. The graphics thread, at that private command, takes the record as it is: no NV2A method decoding, no
   inline index words.
3. Entry points move over in order of cost: the draw calls and the lazy flush first (with the light setup
   computed natively, or on the GPU from the raw D3D lights), then the state setters with the most calls.
   Each step is tested in the cloud (pixel comparison against the old path, RECOMP_D3D_NATIVE=0 to switch
   back) and on the Switch with the profiler.
4. When everything is migrated, the pushbuffer carries only private commands, and the reader can be dropped
   for this game.

Later, independent of this: the graphics thread's per-draw work (cached uniform blocks, fewer descriptor
writes) and, if needed, a second graphics thread.

## The plan from 2026-11-09 (Mark's choice after the review of other Xbox Switch ports)

Switch test 11a: the GPU needs 7.5 to 9.9 ms a frame (12.1 at most), so 60 fps is a CPU question. Heaviest
scenes: main thread about 15.7 ms (three quarters of it lifted code), graphics thread about 17.8 ms.

1. Strategy 1, guest registers in CPU registers (in progress). As OpenTrickyNX (SSX Tricky, 60 fps, same
   toolkit): g_eax..g_esp are GCC global register variables in w19-w25, the x87 stack top in w27, and the rest of
   the per-thread guest state (x87 stack and control word, ebp, the SEH frame, SSE and MMX registers) in one
   struct reached through x26 (xboxrecomp/src/kernel/xbox_guest_regs.h). Our translator already kept registers
   in C locals inside a function; what goes is the store and reload of all of them around every call, and the
   thread-pointer calls. Estimate: lifted code 1.25 to 1.4 times faster, main thread about 13 ms in heavy scenes.
2. Strategy 2, Direct3D replaced at the API level (next). As TGP482/XboxXBE-Recomp (MIT; Black, Shaolin Monks,
   FIFA Street at 60 fps): the game's D3D8 functions replaced as a whole by native ones that keep the state and
   feed the renderer directly, with no NV2A command stream to encode on the main thread or decode on the graphics
   thread. This completes the entry-point migration above in one piece, and is what the graphics thread needs.
