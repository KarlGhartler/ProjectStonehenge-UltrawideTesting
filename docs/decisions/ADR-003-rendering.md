# ADR-003: Rendering architecture

Status: **Accepted** (owner decision, 2026-10-11): **Option C, own Vulkan renderer for the AC04 GS subset.** Addendum decided 2026-10-11: VU0 micro-mode skinning is a C++ rewrite of the 9 entry points, checked against PCSX2 test vectors (later phase). The renderer has an internal scale factor from day one, fixed at 1× (`../port/internal-resolution.md`).

## Context

From `../subsystems/rendering.md`, `../subsystems/vu-programs.md`, `../platform/gs-subset.md`, `../formats/MISSION.md`:
- **Transport** is centralised: ordering-table buckets and DMA tag helpers at 0x00103FD8–0x00104438, two chains per frame (VIF1 with VU1; GIF PATH3), kicked by the main loop one frame late, plus the DrawEnv/DispEnv packet and an interlace field copy.
- **Content** is written by about 233 functions (GIF tags, A+D register writes, primitive streams, VIF codes). Loaded data also contains ready-to-send packets (mission blocks 0, 2, 23, 25, 26; aircraft blocks 4, 5).
- **VU1**: 5 microprograms in `.vutext`, re-sent inline every frame via DMA `ref` tags. One VU0 micro program (VIF0 DMA, 0x0029C200). VU0 macro math in ~311 functions is ordinary C++ math (`vu0_math.h`), not a rendering issue.
- **No direct GS privileged-register access in game code**; libgraph does that.
- **GS subset used** (EE side, `../platform/gs-subset.md`):
  - both drawing contexts;
  - general blend `(A−B)·C+D` incl. destination alpha, FIX, subtractive; COLCLAMP;
  - alpha test (incl. AFAIL ZB_ONLY); destination alpha test (DATE); depth GEQUAL/GREATER/ALWAYS with ZMSK;
  - textures PSMT8, PSMT4, PSMT8H, PSMCT32, PSMCT16S, PSMZ24-as-texture; CLUTs in CT32; region clamp/repeat; bilinear without mipmaps;
  - small render targets; FBMSK;
  - sprites, lines/line strips (some anti-aliased), triangles/strips; PACKED/REGLIST/IMAGE.
  - **The 3D path drawn by VU1 is still unknown** (needs PCSX2 GS dumps).
- Output: NTSC 640×448 interlaced; field mode in game/menus, frame mode for movies.
- Guest memory is the accepted memory model (ADR-001), so the renderer can read packets straight from guest RAM.

## Options

### A. Rewrite the game's rendering against a new high-level API
Replace packet building in the ≈233 functions with calls like `DrawMesh/DrawSprite`.
- \+ Clean modern renderer; easy resolution and widescreen work.
- − Huge manual rewrite with no mechanical check. Every function's GS semantics must be fully understood first, and loaded data packets would still need a GS interpreter.
- − Changes game code, which goes against "translate, don't redesign", and gives up packet comparison with PCSX2.
- **Rejected.**

### B. Extract PCSX2's GS renderer into the port
Embed PCSX2's GS (GIF/GS state, texture cache, Vulkan hardware renderer) and feed it the game's GIF/VIF output.

| Concern | Finding |
|---|---|
| License | PCSX2 is GPL-3.0-or-later overall, so it is compatible with this repo's GPL-3.0. **Must be checked per file**: some files carry other or older headers, and bundled third-party code (e.g. under `3rdparty/`) has its own licenses |
| Dependencies | The GS code depends on PCSX2's `common/` library (logging, config/settings, SIMD helpers, file system, threading), the `Host` interface, its shader cache and its own Vulkan loader/device code. Extraction means carrying or stubbing a large part of `common/` |
| Threading | GS runs on PCSX2's MTGS thread with its ring buffer and EE↔GS synchronisation; that must be kept or re-plumbed |
| Vulkan device handling | PCSX2 owns instance/device/swapchain creation; sharing a device with SDL3 windowing or our own presentation needs adaptation |
| VU1 | **Not part of the GS.** VU1 is in PCSX2's EE core (microVU recompiler / interpreter). Extraction does not solve the VU1 problem; we would still need our own VU1 handling, or would have to pull in a VU interpreter as well |
| Maintenance | A fork of a fast-moving codebase: upstream fixes must be merged by hand, or the fork drifts |
| CPU requirements | PCSX2 targets modern x86-64 with SSE4.1 (its software paths dispatch to AVX2) plus ARM64 in recent versions. Exact minimum requirements must be verified for the extracted parts |
| Quality | Proven accuracy across thousands of games, including texture cache and render-target edge cases |
| Scope | Far larger than AC04 needs (all GS features, all games' hacks and CRC fixes) |

### C. Own lightweight Vulkan renderer for the AC04 GS subset, at the packet level (owner's proposal)
Game code is unchanged; it builds the same packets in guest memory. At the DMA kick, our renderer walks the chains (DMA tags → VIF1 → VU1 HLE → GIF) and draws with Vulkan, implementing **only the GS features AC04 uses** (`../platform/gs-subset.md`, completed by GS dumps for the VU1 path).

Effort estimate per feature (one experienced developer, working days, rough):

| Feature | Days | Notes |
|---|---|---|
| DMA chain walker (`cnt/next/ref/refs/call/ret/end`, SPR flag, `& 0x0FFFFFFF`) | 2–3 | reads guest RAM directly |
| VIF1 interpreter: `UNPACK` (modes used by the game), `STCYCL`, `MPG` (= program id by source address), `MSCAL/MSCNT`, `DIRECT/DIRECTHL`, `FLUSH*` | 5–8 | only the formats seen in captures |
| GIF parser: PACKED, REGLIST, IMAGE; A+D | 3–5 | |
| GS state, two contexts, PRIM/PRMODE handling, vertex queue and kick rules | 3–4 | |
| Primitives: sprite, line, line strip, triangle, strip, fan; Gouraud; UV/STQ | 4–6 | GS rasterisation rules (top-left, pixel centre) as far as visible |
| Anti-aliased lines/triangles (AA1) | 2 | approximate (coverage → alpha) |
| Blend `(A−B)·C+D` incl. Ad, FIX, subtractive; 0x80 = 1.0 alpha scaling; COLCLAMP | 4–6 | most cases map to fixed-function blend; rare ones need a shader path |
| Alpha test incl. AFAIL (ZB_ONLY etc.) | 2 | shader discard + separate colour/depth writes |
| Destination alpha test (DATE) | 3 | stencil from destination alpha bit |
| Depth: Z24/Z32, ZMSK, GEQUAL/GREATER/ALWAYS | 2 | |
| **GS local memory model**: page/block swizzling for the formats used (CT32, CT16S, T8, T4, T8H, Z24), uploads (IMAGE), CLUT loads, render-target ↔ texture reuse, small targets | 10–15 | the largest item; needed for render-to-texture and depth-as-texture |
| Region clamp/repeat, bilinear without mipmaps, TEXA | 2–3 | shader-side addressing |
| FBMSK, fog (FGE/FOGCOL) | 2 | |
| Interlace field copy / display (field and frame modes, `DrawDisplayTransfer` (H) 0x001049B0) | 2 | |
| VU1 programs (≈6), see below | 10–20 | depends on program complexity |
| Test harness: GS-dump replay, frame diff against PCSX2 | 5 | |
| **Total** | **≈ 60–90 days** | plus unknowns from the 3D path |

- \+ Small, readable and fully owned; implements exactly what AC04 needs; no fork.
- \+ Game code and packets are unchanged, so the renderer can be tested with GS dumps from PCSX2 and frames compared against PCSX2.
- \+ Integrates naturally with guest memory (ADR-001) and SDL3/Vulkan.
- − We must get every used GS edge case right ourselves; PCSX2's GS code may be read as a reference and used as a test oracle, but not copied.
- − The local-memory model is real work, unless captures show the game never reinterprets VRAM between formats (then a simpler per-target texture cache is enough).

## VU1 microprograms

Options:
1. **CPU HLE in C++** (one function per program, operating on the VIF-unpacked input and producing GIF packets or direct vertex lists).
   - \+ Exact float semantics (EE/VU float rules can be emulated in C++).
   - \+ Debuggable; output comparable with PCSX2's VU1 memory/GIF output.
   - − CPU cost. Small for AC04's vertex counts on modern CPUs (H).
2. **Vulkan compute** port of each program.
   - \+ Fast.
   - − Harder to make bit-exact (VU float behaviour, clamping), harder to debug, and still needs the same reverse engineering.
3. **VU1 interpreter** running the original microcode.
   - \+ No per-program understanding needed.
   - − It is emulation; needs the microcode from `.vutext` at runtime (fine under the asset rule) and a VU interpreter (own or third-party).

Approach: **CPU HLE in C++ first** (option 1), each program checked against PCSX2 output. Move a program to compute only if profiling shows a need. The vu-microcode rules apply: analyse the microcode as data, never disassemble `.vutext` in Ghidra.

## Decision

**Option C** (accepted 2026-10-11): own Vulkan renderer for the AC04 GS subset at the packet level, with CPU-HLE VU1 programs.
- PCSX2's GS code is used only as a reference for understanding and as a test oracle (frame and GS-dump comparison), never copied.
- **Vulkan 1.2 as the minimum.**
  - It provides timeline semaphores, descriptor indexing (many small textures), scalar block layout, 8/16-bit storage, on all target platforms including MoltenVK.
  - Nothing found so far needs 1.3: dynamic rendering and sync2 are nice-to-have and may be used when present.
- Option B (PCSX2 extraction) stays the fallback if the 3D path turns out to need large parts of general GS emulation (e.g. heavy VRAM format reinterpretation), or if option C's effort grows beyond the estimate.

Input: eight PCSX2 GS dumps of whole frames, including the 3D path (`../platform/gs-subset.md`, "Measured in GS dumps").

## Consequences

- `src/platform/render/`: DMA walker, VIF1, VU1 HLE, GIF, GS state, Vulkan backend, GS-dump replay tool.
- `tools/`: a research parser for `.gs` dump files (to list the registers, values and primitives actually used).
- Output scaling to the screen is a baseline feature (ADR-006). Widescreen stays in the modding layer; MSAA is "later" (`port-baseline.md`).
- The renderer carries an **internal scale factor from the start**, fixed at 1× until each draw path is validated at higher values (`../port/internal-resolution.md`, a proposal; higher internal resolution is not approved yet).

## Research findings

- **Output.** The renderer draws at native resolution (512×448 game, 640×448 movies). The final output pass that scales to the screen is ADR-006 (Accepted).
- **Interpolation (ADR-002, baseline feature).** The renderer records a per-step draw list and replays it at presentation time with interpolated matrices. So the **VU1 HLE runs at presentation time**, and its matrix inputs must be identifiable per draw. The matrix source and object identity of every draw path are in `../subsystems/rendering.md`.
- **One GIF path.** Live code sends graphics only through `sceDmaSend` chains. The scratchpad → GIF ring (`FlushSprWorkBuffer`, DMAC `D_RBOR`/`D_RBSR`) is set up at boot but only dead code uses it (`../subsystems/rendering.md`), so the renderer does not need it.
- **GS dumps (3D path).** The VU1 output uses triangle fans and strips with Gouraud, STQ perspective and fog; textures include CT24 and Z24-as-texture; 512×512 terrain-like geometry uses **trilinear mipmapping with automatic mip addresses** (TEX1 MTBA). The game never copies local → local and never reads GS memory back: every transfer is a host → local upload. That keeps the local-memory model to uploads, render targets reused as textures (off-screen targets at FBP 0x120–0x1C0) and depth sampled as a texture (`../platform/gs-subset.md`).
