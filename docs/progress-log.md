# Progress log, branch xbe-recomp (newest first)

The xboxrecomp version's log (2026-10-28 to 2026-11-09) is `docs/xboxrecomp-era/progress-log.md`.

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
