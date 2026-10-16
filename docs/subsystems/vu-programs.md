# VU programs (`.vutext`, `.vudata`)

The VU1 and VU0 microcode blocks, how the game uploads them, and who uses them. Packet transport and bucket order: `rendering.md`. Rules for microcode analysis: `../../.claude/rules/vu-microcode.md` (never create functions or disassemble inside `.vutext`, `.vudata` or `.DVP.overlay.*` in Ghidra). Section addresses: `../platform/elf-sections.md`.

## Blocks

Each `.vutext` block is a ready DMA packet: a `ret` (or `cnt`) tag holding the qword count, then `FLUSHE`, `MPG` (load address) and the microcode. Only the headers have been read; the microcode itself is not analysed.

| Address | QW | MPG address | Program | Used by |
|---|---|---|---|---|
| 0x00297C00 | 0xEA | 0 | VU1 A: generic model renderer, C's lit pass only (section below) | view buckets 6 and 9 (`func_00138BB0`, ref to 0x00297C10); menu 3D objects with lighting mode 1 |
| 0x00298AC0 | 0xD4 | 0 | VU1 B: model program, unlit (section below) | ground objects (`func_00138C28`, ref to 0x00298AD0, bucket 0), shadows (`DrawAircraftShadow` 0x001418C8), menu lighting mode 0 |
| 0x00299820 | 0x10F | 0 | VU1 C: model program, lit + reflection pass | aircraft view mode 3, menu lighting mode 2 |
| 0x0029A930 | 0x7A | 0 (242 pairs) | VU1 D: glow fins | half-resolution glow pass `func_00142460` (untextured Gouraud output) |
| 0x0029B0F0 | 0x10B | 0 | VU1 E: program C with Z × 16 (section below) | aircraft view mode 2 |
| 0x0029C200 | 0x6C (`cnt`) | 0xD8 | **VU0** micro program | cutscene character skinning (`rendering.md`, sequence character engine) |

`.vudata` 0x004021F0 is not data: it is a sixth VU1 program, the **terrain program T** (section below). Its own DMA tag (0x60000108, `ret`, 0x108 qwords) is followed at 0x00402200 by FLUSH and three MPGs (256 pairs at 0, 256 at 0x100, 12 at 0x200); `func_00138C10` sends 0x00402200 with a `ref` of the tag's low halfword (0x108 qwords) into terrain bucket 0. (Corrected 2026-10-16: earlier listed as "VU1 data".)

## How the programs are uploaded

| Caller | Address | How |
|---|---|---|
| `func_00138BB0` | 0x00297C10, 0xEA qwords, buckets 6 and 9 | `ref` via `func_001042D0` (from `BuildFlightDrawLists` 0x0010BD68) |
| `func_00138C28` | 0x00298AD0, 0xD4 qwords, bucket 0 | `ref` via `func_001042D0` |
| `func_00138C10` | `.vudata` 0x00402200, size `*0x004021F0` | `ref` via `func_001042D0` (VU data) |
| `DrawAircraftShadow` 0x001418C8, `func_001483A0`, `func_00142460`, `func_0014B280` | 0x00298AC0, 0x0029B0F0, 0x00299820 | `ref` via `func_001042D0` |
| `func_0014AE60` | 0x00297C00 / 0x00298AC0 / 0x00299820 by lighting mode 1/0/2 | stores the microcode pointer in a render object (menu 3D models, caller `func_00231000`) |
| `UploadVu0Microcode` 0x00225408 | 0x0029C200 | **direct VIF0 DMA**: `D0_QWC = 0`, `D0_TADR = 0x0029C200`, `D0_CHCR = 0x145` (chain mode, start), then `sceDmaSync`; clears `D_STAT`. The only VU0 micro-mode upload |
| `.data` 0x003B29E0, 0x003C9BB0, 0x003C9CD0 | 0x00299910, 0x0029A300 | values inside program C, so probably not microcode pointers (H) |

`.vutext` is laid out as ready-to-DMA VIF packets. The game re-sends microcode inside the VIF1 chain whenever a renderer needs it: there is no one-time upload. A port that replaces VU1 with GPU shaders can treat each `ref` to a `.vutext` address as "bind pipeline X".

## VU0 micro mode

Used only by the sequence character engine: entries 0x00/0x10/0x30/0x40/0x50/0x70/0x80/0x90/0xB0 called with `vcallms` (`func_002250E8`…`func_00225270`), data moved through scratchpad 0x70002200 by DMA channels 8/9. All other VU0 use is macro mode (`vu0_math.h`).

Which entry runs depends on the part type (+0x140) in `FUN_002236A0`: type 0 (from the character data) → `FUN_00224180` → loop 0x002250E8 (entry 0x00); type 1 → `FUN_00224410` → loop 0x00225110 (entry 0x10); type 3 (morph track active, set by `FUN_00223F58`) → `FUN_00224848` → loop 0x00225140 (entry 0x30). The rigid path `FUN_00223830` → `FUN_00224D50` also uses 0x00225110. Each loop: `ctc2` a1 → VI01 and a2 → VI03 (VU0 data-memory pointers), then a3 times `vcallms`, `qmfc2.I`/`sqc2 vf1` to the buffer in a0 (one quadword per call).

Test vectors: path 0x00225110 captured in PCSX2 (`../pcsx2/sessions-05.md`, states `vu0_00225110_before/after`); inputs are the VU0 registers (`tools/p2s_read.py STATE vu0`), `vu0Memory.bin`, scratchpad and EE RAM, output the a0 buffer. Paths 0x002250E8 and 0x00225140 are pending (`../pcsx2/procedures.md` check 1).

## Model programs B (0x00298AC0) and C (0x00299820)

Disassembly: `python tools/research/vu_disasm.py 298AC0` (B, 0x1A4 pairs) and `299820` (C, 0x21A pairs); listings in `out/vu/` (never committed). Uploaded per menu/aircraft object by `UploadMenuObjectProgram` 0x0014B280 (`ref` to the block); run once per mesh strip by the `MSCNT` at the end of each strip packet (`../formats/SM-model.md`). The first `MSCAL` stops at the E bit of pair 0; every later `MSCNT` enters at pair 2 (`b 0x002` after the final E bit). C is B plus lighting, a second (reflection) pass and backface rejection.

### Input (VU1 data memory, relative to TOP = `xtop`)
| QW | Written by | Content |
|---|---|---|
| 0–3 | `EmitModelInstance` (UNPACK 12 at 0) | world→screen × node world matrix (`M_screen`) |
| 4–7 | same | world→clip × node world (`M_clip`) |
| 8–11 | same | normal matrix: light frame × node rotation (`M_normal`) |
| 12–15 | `WriteLightSetPacket` (UNPACK 9 at 0x0C) | light directions as rows: qword c = (L0.c, L1.c, L2.c, L3.c) for c = x, y, z, w |
| 16–19 | same | light colours as columns (colour / 128) |
| 20 | same | ambient colour (/128) |
| 21 | `EmitModelInstance` (0x15) | GIF tag, pass 1, TRIFAN (clipped polygons): PRE, PRIM = 5 (+ABE for translucent parts) \| IIP\|TME\|FGE, NREG 3, REGS STQ RGBAQ XYZF2 |
| 22 | same (0x16) | GIF tag, pass 1, TRISTRIP (strips) |
| 23 | mesh packet (UNPACK V4-8 at 0x17) | x vertex count, y flags (bit 0 backface test, bit 1 lit colour, bit 2 second pass), z backface parity |
| 25 | `EmitModelInstance` (0x19) | GIF tag, pass 2 (CTXT 2 + ABE), TRISTRIP |
| 26 | menu/light set | GIF A+D tag (NLOOP 1) before each pass |
| 27 | light set texture | TEX0 for pass 2 (reflection texture) |
| 28 | `WriteMenuObjectTexture` (0x1C) | TEX0 for pass 1 |
| 29 | strip header area | x backface sense, y clipping enable |
| 30 | `WriteMenuObjectCamera` (0x1E) | fog: x = fog distance, y = fog scale |
| 31 | `EmitModelInstance` (0x1F) | GIF tag, pass 2, TRIFAN |
| 32 + 4i | mesh packet | vertex i: ST (4.12), RGBA (0–255), position (f32, w = 1), normal (4.12) |

### Per strip
1. Output buffer: TOP + 0x7E, or TOP + 0xE6 when (count − 3) is odd (two output buffers for the XGKICK of the previous strip).
2. Clipping disabled (29.y = 0): the whole strip goes to the draw loop.
3. Else per triangle t (vertices t, t+1, t+2): positions × `M_clip`. If every vertex has w > 8192 it is drawn without clipping. Otherwise, with no clip flag set it is drawn; if all three vertices are outside the same plane it is culled (an empty GIF tag with EOP is kicked); otherwise it is clipped (Sutherland–Hodgman) against +x, −x, +y, −y, +z, −z (|c| ≤ w) in that order. Each edge: d = ±c − w for both ends; inside when d < 0. The intersection factor is t = |dA / (dB − dA)| (C adds 0.001); ST, colour, position and normal are interpolated linearly and the clip position is recomputed from the interpolated position. Scratch polygons alternate between absolute VU addresses 0x20 and 0x48 (5 qwords per vertex); the result is converted back to the input format and drawn as a TRIFAN (tag 21 / 31) with its vertex count. Fewer than 3 vertices: culled.
4. Draw loop (count vertices), per vertex:
   - p = `M_screen` × position; q = 1 / p.w; XYZ = p.xyz × q; fog = clamp((30.x − p.w) × 30.y, 0, 255); XYZF2 = ftoi4(XYZ, fog) (the fog lands in the F field).
   - STQ = (ST × q, q) from the 4.12 ST.
   - B: RGBAQ = vertex colour (unlit).
   - C, colour: n = `M_normal` × normal (w = 0); per light i: Σ_c max(L_i.c × n.c, 0) (each product clamped separately, as the code does); colour = Σ_i I_i × colour_i + ambient; RGB = clamp(colour × vertex RGB, 0, 255), A = vertex A. Flag bit 1 chooses lit (this) or unlit colour for pass 1.
   - C, pass 2 (flag bit 2): STQ = ((1 − n.x) / 2, (n.z + 1) / 2) × q; colour (128, 128, 128, 128), or (0, 0, 0, 128) when n.y < 0.
   - C, backface (flag bit 0): the screen-space cross product of the last three vertices (kept at VU 0 and 1) decides; a rejected vertex gets fog + 2048 before ftoi4, which sets the ADC bit of XYZF2 (vertex kicked without drawing). The sense alternates along the strip (29.x, 23.z, vertex parity).
5. Output: [A+D tag (26), TEX0 (28)], GIF tag (count, EOP unless the second pass follows), count × {STQ, RGBAQ, XYZF2}; pass 2 the same with TEX0 (27) and tag 25/31. XGKICK.

## Generic model program A (0x00297C00) and aircraft program E (0x0029B0F0)

Analysed 2026-10-16 (`s5-plan.md` P3) by an instruction-level diff against B and C (`out/vu/A.txt`, `E.txt`; branch targets masked). Both use the input layout and strip handling of B/C above.
- **A** (0x1D0 pairs; view buckets 6 and 9: terrain and most world objects, menu lighting mode 1):
  - frame, clip set-up and clip-flag dispatch identical to C (the "all w > 8192" fast path included);
  - clipper identical to B: the intersection factor has **no** +0.001 (C adds it); the clipped polygon is copied without the `+ vf05` (zero) of C, so −0 components are not turned into +0;
  - draw loop = C's pass 1 only: lit colour exactly as C (rows 12–15, clamp per product, colour matrix 16–19, clamp, + ambient 20, × vertex RGB, clamp 0–255, A = vertex A) when 23.y bit 1 is set, else the vertex colour; fog and STQ as B/C; the backface sense is computed (29.x, 23.z) but **not used** (no cross product, no ADC);
  - output: one GIF tag (21 or 22 with NLOOP = count, EOP) with **no** A+D/TEX0 prefix, then count × {STQ, RGBAQ, XYZF2}; XGKICK. 23.y bit 2 (second pass) is ignored.
- **E** (0x212 pairs; aircraft view mode 2): C with two differences: no +0.001 in the clipper (as A/B), and the screen Z is multiplied by 16 before `ftoi4` (`muli.z vf21, vf21, 16.0` after the clamp of the fog), for both passes.

## Terrain program T (`.vudata` 0x00402200)

Analysed 2026-10-16 (`out/vu/T.txt`, `tools/research/vu_disasm.py 0x004021F0`; 0x20C pairs). Uploaded by `func_00138C10` into terrain bucket 0 (`rendering.md` "Cell draw"); MSCAL 0 stops at the E bit of pair 0, every cell's MSCNT continues at pair 2 (the end at 0x208 is followed by `b 0x002`). Fixed block per frame (TOP-relative, from `func_0013CDC0`): 0–3 world → screen matrix (cam+0xE0), 4–7 world → clip matrix (cam+0x120), 13 (x: 1 / (r.z − r.y), y: r.y; the ring radii squared), 14 camera position, 15 A+D GIF tag (NLOOP 2), 16 TEX0_1 A+D pair, 17 GIF tag of pass 1 (PRIM 0x7D/0x3D fan, NREG 3: ST, RGBAQ, XYZF2), 18 GIF tag of pass 2 (context 2, PRIM 0x27D), 22.x pass-2 flag (R+0xC0 bit 2: cloud shadow), 30 (x: far, y: 255 / (far − near)). Per cell (`func_0013C740` / `func_0013C1D0`): 12 cell origin (x, 0, z, 1), 19 CLAMP_1 A+D pair, 20 (signed bytes: x − cx + 0x20, z − cz + 0x20, 1, 0), 23 (bytes: x = vertex count n (10 fan / 4 quad), y = cell class c), records from 32: n × 4 qwords of bytes (UNPACK V4-8): q0 (U, V, 1, …), q1 colour RGBA, q2 (x, height, z, …) in half-cell units, q3 second UV.

1. **Unpack** (pairs 0x002–0x026), in place for the n records: q0 = itof0, xy × 1/16; q1 = itof0; q2 = itof0, xz × 1024, y × −80, xyz + origin (12); q3 = itof0, xy × 1/128 + (20.xy as itof0 × 1/64), z = 1.0.
2. **Class** c = 23.y (16-bit): c = 4 → output; c = 1 → clip each fan triangle; any other value → morph, then output. The culling (`func_0013A538`, `rendering.md` terrain step 2) produces 1, 2 or 4: near ring (fan, clipped), middle ring (fan, morphed), far ring (quad).
3. **Morph** (0x031–0x07B, fan cells): with p_k the position of record k: m0 = (p1 + p5) · 0.5, m2 = (p1 + p3) · 0.5, m4 = (p3 + p5) · 0.5, m6 = (p5 + p7) · 0.5, m8 = (p7 + p1) · 0.5 (all four components); for v in p0, p2, p4, p6, p8 with its m: d = v − cam (14), s = ((d.x² + d.y²) + d.z² − 13.y) · 13.x clamped to 0…1, v := (m − v) · s + v. Far cells flatten to the coarse quad.
4. **Clip** (c = 1; 0x07C–0x1A0), per fan triangle t = 0 … n − 3 with A = record 0, B = record t + 1, C = record t + 2: clip positions with matrix 4–7, CLIP each (order A, B, C, so C's flags are bits 0–5). No flag → the triangle's 12 qwords (A, B, C, q0–q3) go to absolute VU address 0 and are output. All three outside the same plane (`fcor` masks 0xFFEFBE … 0xFDF7DF) → skipped. Otherwise Sutherland–Hodgman over +x, −x, +y, −y, +z, −z (sign s = +1, −1 alternating) on 5-qword vertices (q0–q3, clip position) in the absolute buffers 0x20 and 0x48 (alternating; with 9 vertices they overlap, the order of reads and writes is kept): for each edge P → Q, dP = s·P.c − P.w and dQ likewise; a point is outside when the sign bit of d is clear (MAC Sx, read four pairs after each `subw`). Both inside → emit P; P inside, Q outside → emit P and X = P + (Q − P) · |a / (b − a)|; P outside, Q inside → emit X = Q + (P − Q) · |b / (a − b)|; both outside → count − 1; here a = (s·P.w) · −1 + P.c, b likewise for Q, X's clip position = matrix 4–7 · X.q2. After each plane: fewer than 3 vertices → skipped. The result is compacted to stride 4 into the other buffer (0x48) and output.
5. **Output** (0x1B1–0x204), per output (one per cell, or per clipped triangle): buffer TOP + 0xD2 and TOP + 0x7E alternately (from 0xD2 at each MSCNT). Packet 1: 15, 19 (CLAMP_1), 16 (TEX0_1), tag 17 with NLOOP = count (`mfir`, so the word is 0xFFFF8000 | count with EOP when 22.x = 0, else count), then per vertex ST = q0 · Q, RGBAQ = ftoi0(q1), XYZF2 = ftoi4(screen xyz · Q, F = min(max((far − w) · k, 0), 255)) with Q = 1 / w of matrix 0–3 · q2. Packet 2 right after (tag 18 with 0xFFFF8000 | count): ST = q3 · Q, RGBAQ = ftoi0(128, 128, 128, q1.a / 255 · F), the same XYZF2. XGKICK of the buffer: with 22.x = 0 the EOP of packet 1 ends the transfer and packet 2 is never drawn.

## Glow fin program D (0x0029A930)

Uploaded by `DrawGlowPass` 0x00142460 (MPG of 242 pairs at address 0). The packet calls MSCAL 0 (both VIF codes it builds are 0x14000000), which stops at the E bit of pair 0; every strip's MSCNT resumes at pair 0xDE (after the E bit at 0xDC), which branches to pair 2. Input from absolute VU addresses written by the glow pass packet: 32–35 projection matrix, 44 (vf15) light direction, 45 (vf21) extrusion offset, 46–48 GIF tags, 49.x mode; per strip at TOP: 0–3 node matrix, 23.x vertex count, 23.z winding, vertices from 32 (4 qwords).
- **Pass 1** (pairs 0x011–0x034, per vertex): p = node matrix × position (q2); p' = p + offset (xyz); q2 := p, q3 := p'; q0 := projection × p / w, q1 := projection × p' / w (two `div` by w).
- **Pass 2** (pairs 0x048–0x0D4, per triangle, vertices v0 v1 v2; `vf17` = 0 is added to the loaded values, turning −0 into +0):
  - winding sign s = −1 or +1, alternating per triangle from 23.z;
  - face normal from the world points: n = (v0.q2 − v1.q2) × (v2.q2 − v1.q2) (`opmula/opmsub`), n̂ = n × s / |n| (`sqrt`, `div`), d = n̂ · light (44);
  - for each edge k = (v0, v1), (v1, v2), (v2, v0) with the projected points a = vk.q0, a' = vk.q1, b = v(k+1).q0: z of (a − a') × (b − a'), times s, times d (`muly.z`);
  - `fmeq vi, 0x20` four pairs after each product (VU1 MAC flag latency) sets f_k = 1 when that z is strictly negative (sign set, zero clear);
  - for each edge with f_k ≠ 49.x: tag 46, RGBAQ (1, 1, 1, 128), XYZ2 of ftoi4(a), ftoi4(a'), ftoi4(b), ftoi4(b') (a quad as a 4-vertex strip);
  - when 49.x = 0: tag 47, RGBAQ, ftoi4 of the three extruded points v0.q1, v1.q1, v2.q1 (the cap);
  - after the last triangle: tag 48, XGKICK of TOP + 0x7E.
- **Entry 0xE0** (black triangles (0, 0, 0, 128) from consecutive projected points, stride 4 qwords, tag 47, ending at 0xD6): it reads `vi03`, `vi04` and `vi06` without setting them and no EE packet code issues MSCAL 0xE0; HYPOTHESIS: unreachable from the game's packets (to confirm by scanning the pre-built packets on the disc).

## Port consequences

- 5 VU1 programs become GPU shaders (or a CPU VU1 interpreter) selected by the `ref` address.
- The VU0 program 0x0029C200 needs either a VU0 micro-mode interpreter or a C++ rewrite of its nine entry points, checked against PCSX2.
- The microcode is read from the ISO at runtime like the rest of the executable's data; it is never copied into `src/`.

## Open points

- Programs A (0x00297C00) and E (0x0029B0F0): analysed by diff (section above) and implemented 2026-10-16 as variants of the B/C HLE (`src/platform/render/vu1/vu1_programs.cpp`); the terrain program T is implemented there too. A, E and T are not yet compared with a PCSX2 GS dump (batch S5-A). B, C, D: analysed 2026-10-15 (sections above); their exact float behaviour (VU rounding) is not needed for RAM state (GS output only) but matters for pixel-exact screenshots.
- VIF1 BASE = OFST = TOPS = 0 in the S4 PCSX2 states (`eeHwRegs.bin` at 0x3CA0/0x3CB0/0x3CC0): no double buffering, TOP is always 0. The model programs' clipper scratch at absolute 0x20/0x48 therefore overlaps the strip's own vertices (later triangles of a clipped strip read overwritten data); the HLE keeps this by working on a VU1 memory image.
