# GS feature subset used by AC04

Input for ADR-003 (option C: GS/VIF replay renderer): the GS registers, primitive types, blend/test modes and pixel formats the game uses, so the renderer only implements what is needed. Two sources: a static scan of the **EE-side** packet code, and PCSX2 GS dumps of whole frames (section "Measured in GS dumps"), which include the VU1 3D path.

## Method and limits

Script: `tools/research/gs_scan.py` (register ids; register values and GIF tags were collected with the same constant propagation), run over game code 0x001000D8–0x002616E7 with simple constant propagation:
- An `sd` of a constant GS register id to a qword's upper half (`offset % 16 == 8`) counts as an A+D write of that register. The constant `sd` to the lower half of the same qword (if known) is its value.
- A 64-bit constant `sd` to a qword's lower half that has the GIF tag shape (NLOOP > 0, bits 16–45 zero, FLG/NREG set) counts as a GIF tag.

Limits:
- **This covers the EE-built packets**: display/draw environment, 2D (HUD, menus, text, sprites), screen effects, texture uploads, and the GS state set before VU1 draws.
- **3D geometry is drawn by the 5 VU1 programs** (`../subsystems/vu-programs.md`), which emit their own GIF tags. Those tags are neither in EE code nor visible as plain templates in DATA.CDP: a scan of the extracted DATA entries produced only random matches. The 3D primitive set comes from PCSX2 GS dumps (section "Measured in GS dumps").
- About half of the register values are computed at runtime ("computed" below), so the value lists are lower bounds.
- Function counts use approximate boundaries; a few hits can be false positives (a small constant stored at `+8` for a non-GS reason).

## Registers written through A+D (number of functions)

| Register | Funcs | Register | Funcs | Register | Funcs |
|---|---|---|---|---|---|
| ALPHA_2 | 49 | ALPHA_1 | 47 | TEXFLUSH | 41 |
| XYOFFSET_2 | 38 | TEX0_1 | 33 | TEST_1 | 29 |
| ZBUF_1 | 27 | FRAME_2 | 22 | TEST_2 | 21 |
| SCISSOR_2 | 20 | ZBUF_2 | 18 | TEX1_1 | 15 |
| CLAMP_1 | 14 | TEX0_2 | 14 | TEX1_2 | 13 |
| SCISSOR_1 | 12 | TRXPOS | 12 | FRAME_1 | 11 |
| XYOFFSET_1 | 9 | TRXDIR | 9 | CLAMP_2 | 9 |
| RGBAQ | 8 | XYZ2 | 8 | TEXA | 8 |
| BITBLTBUF | 6 | TRXREG | 6 | COLCLAMP | 5 |
| TEX2_1 / TEX2_2 | 4 / 4 | PRMODECONT, DTHE, FOGCOL | 3 each | XYZ3, FBA_2, XYZF2 | 1 each |

**Not seen:** MIPTBP1/2 (no mipmap levels), SCANMSK, PABE, FBA_1, DIMX, TEXCLUT, LABEL/SIGNAL/FINISH, HWREG (no host↔local transfers through HWREG; uploads use IMAGE GIF packets).

**Both drawing contexts are used heavily** (context 2 registers in 49 functions). Context 2 typically carries render-to-texture passes and effects, while context 1 draws the main scene (H).

## Values found

### Alpha blending: `ALPHA = (A − B) · C + D`

| Formula | Uses | Typical meaning |
|---|---|---|
| `(Cs − Cd)·As + Cd`, FIX 0x80 | 45 + 8 + 5 | normal alpha blend |
| `(Cs − 0)·As + Cd` | 12 + 4 + 2 | additive (alpha-scaled) |
| `(Cs − 0)·FIX + Cd`, FIX = 0x80 / 0x01 | 15 + 1 | additive (full strength) / near-transparent add |
| `(Cs − Cd)·FIX + Cd`, FIX = 0x10 / 0x40 / 0x80 | 3 | fixed-ratio crossfade |
| `(Cd − 0)·As + Cd` | 4 | brighten destination |
| `(Cs − 0)·Ad + Cd`, `(Cs − Cd)·Ad + Cd` | 4 + 2 | **destination-alpha** blends |
| `(0 − Cs)·As + Cd` | 1 | **subtractive** |
| `(0 − 0)·As + Cd` | 1 | write nothing (colour masked by blend) |

The renderer needs the general GS blend equation, including destination alpha (Ad), FIX, the subtractive form and `COLCLAMP` (=1, clamp; set in 5 functions). Plain fixed-function blending covers most cases. Ad-based and subtractive forms need dual-source or shader blending.

### Tests (TEST_1/2)

- Depth: ZTE=1 always; ZTST = ALWAYS, GEQUAL, GREATER (no NEVER).
- Alpha test: off in most cases; GREATER with AREF 0 or 0x40, ALWAYS; AFAIL = KEEP (0) and **ZB_ONLY (2)** (one case).
- **Destination alpha test (DATE)** is used in 2–3 places (stencil-like masking with the framebuffer alpha bit).

### Textures (TEX0, TEX1, CLAMP, TEXA)

| PSM | Name | Seen in |
|---|---|---|
| 0x13 | **PSMT8** (8-bit CLUT) | most common (30+ constants), 256×256 and 512×512 |
| 0x14 | **PSMT4** (4-bit CLUT) | 64×128 … 512×512 |
| 0x1B | **PSMT8H** (8-bit index in the alpha byte of a 32-bit page) | 2 |
| 0x00 | PSMCT32 | framebuffer-as-texture, 1024×1024 (screen copies) |
| 0x0A | PSMCT16S | 16-bit render target sampled as a texture |
| 0x31 | **PSMZ24** | the **depth buffer sampled as a texture** |

- CLUT format CPSM = 0 (PSMCT32 palettes) in all constants. CSM/CLD come from TEX0 or TEX2 (TEX2 written in 4+4 functions).
- TFX: MODULATE (0) and DECAL (1); TCC = 0/1.
- TEX1: MMAG = MMIN = LINEAR in all constants (bilinear), MXL = 0 (no mipmaps); LCM 0/1.
- CLAMP: CLAMP (1), **REGION_CLAMP (2)** and **REGION_REPEAT (3)** with masks (sub-rectangle texture atlases). REGION_REPEAT needs shader emulation.
- TEXA: AEM = 0, TA0 = 0x80 / TA1 = 0x80 (for 16-bit and 24-bit textures).

### Framebuffers (FRAME, ZBUF)

- FRAME PSM: PSMCT32 (0x00) and **PSMCT16S (0x0A)**. Widths FBW 8 (512 px, the main buffers) and FBW 1/2 (64/128-pixel **small render targets** at FBP 0x140/0x148/0x190, i.e. VRAM pages after the frame buffers). FBMSK 0xFF000000 is common (alpha writes masked) and 0x00FFFFFF occurs once (write alpha only).
- ZBUF: PSMZ24 (1) and PSMZ32 (0); ZMSK 1 (no depth write) in most 2D passes. ZBP 0xA8, 0xDC, 0x1C0.
- `XYOFFSET` / `SCISSOR` per context; DTHE (dither) off; PRMODECONT = 1.

### Primitives in EE-built GIF tags (functions)

| Primitive (PRIM field) | Funcs |
|---|---|
| SPRITE + TME + FST(UV), often ABE, context 1 or 2 | ≈40 |
| SPRITE without texture (ABE) | ≈8 |
| LINE / LINESTRIP, with IIP (Gouraud) and ABE, some **AA1 (antialiased)** | ≈20 |
| TRI / TRISTRIP (IIP, ABE, one with TME), one **TRI with AA1** | ≈5 |
| `PRE = 1, PRIM = 0` (POINT) with NREG 1 | 16, probably a "PRIM set later via A+D" placeholder (H) |

Tag modes: PACKED (NREG 1–6), REGLIST (NREG 1–15; used for fast register setup and vertex lists), IMAGE (texture or CLUT uploads, 2 functions plus the `BITBLTBUF`/`TRXPOS`/`TRXREG`/`TRXDIR` setup in 6–12 functions). STQ (perspective) appears in EE tags only once. Perspective-correct texturing comes from the VU1 path.

### Other

- Interlace field handling: field copy of the draw buffer into the display buffer every frame (`DrawDisplayTransfer` (H) 0x001049B0, 32-pixel strips, `ALPHA_1` and `TEX0` in PSMCT32).
- FOGCOL set in 3 functions. Fog itself (`FGE`) is not in EE tags, so it comes from VU1 output (H).

## Measured in GS dumps

Eight single-frame PCSX2 GS dumps (`../pcsx2/sessions-04.md`): title screen, main menu, first briefing, story intro movie, an in-engine cutscene labelled "first boot" in the capture (mission 0x23 no-save intro or 0x25 opening sequence, not recorded), in-engine demonstration, hangar (F-4E), and gameplay (bombers, explosions, HUD). Parsed with `tools/research/gsdump.py` (counts of drawn primitives by GS state; PCSX2 2.8 records the merged GIF-unit output, so the three GIF paths cannot be told apart in the dump).

| Feature | Seen |
|---|---|
| Primitives | **triangle fan** and **triangle strip** (3D: Gouraud, textured, STQ perspective, fogged), triangle list (demo, 396), sprite (2D, UV), line strip incl. **AA1** (HUD); no points, no plain lines |
| Fog | FGE on most 3D geometry; FOGCOL per scene (0x815F4D, 0x82604E, 0x816151; 0 in the hangar) |
| Texture formats | T8 and T4 (CLUT CT32 only), **T8H**, CT32, **CT24 (TCC 0)**, CT16S, **Z24 sampled as a texture**; sizes 16×16 to 1024×1024 |
| Texture filtering | bilinear (MMAG/MMIN 1) on almost everything; **mipmapping on 512×512 fogged triangle fans** (TEX1 0x36C: MXL 3, MMIN 5 = trilinear, MTBA 1 = automatic mip addresses, LCM 0 with K = 0xF28; also 0x32C with MMIN 4); nearest for movies and some 2D |
| Clamp | all four modes (repeat, clamp, region clamp, region repeat) |
| Blending | `(Cs−Cd)·As+Cd`, `(Cs−0)·As+Cd` (additive), `(Cs−Cd)·FIX+Cd` (fades, FIX 0x10–0x80), **`(Cs−0)·Ad+Cd` (destination alpha)**, `(0−Cs)·FIX+Cd` (subtractive), `(Cs−Cd)·Ad+Cd`; COLCLAMP always 1, DTHE always 0, PABE 0 |
| Alpha test | GREATER with AREF 0, 0x40 or 0x5A, AFAIL KEEP or FB_ONLY; LEQUAL; ALWAYS |
| Depth | Z24 (ZMSK on and off) and Z32 (ZMSK on); ZTST ALWAYS, GEQUAL, GREATER |
| Destination alpha test | DATE with DATM 0 and 1 |
| Frame masks | FBMSK 0xFF000000 (RGB only) and 0x00FFFFFF (alpha only) |
| Framebuffer sampled while drawing | heat-haze distortion (`func_001E88C0`, from the afterburner effect `func_001E81F0`): writes a mask into framebuffer alpha, then redraws displaced framebuffer strips sampled as CT32 1024×1024 at TBP 0 with `(Cs−Cd)·Ad+Cd`. The renderer must copy the affected framebuffer region before sampling it (feedback loop) (`../port/validation.md` V-7) |
| Render targets | main buffer FBP 0x000 (FBW 8 = 512, FBW 10 = 640 for menus/movies); off-screen targets FBP 0x120–0x140 (128/64 px wide, shadow and blur chain), 0x128/0x148/0x150/0x168 (256 px wide), 0x180–0x1C0 (512 px, CT32 and CT16S), 0x150 as Z24 |
| Transfers | host → local only (IMAGE uploads of T8/T4/T8H/CT32/CT24); **no local → local copies and no local → host readback** |
| Not seen | points, plain lines, SCANMSK, FBA, DIMX, PABE = 1, MIPTBP writes (mip addresses are automatic), HWREG |

## Requirements for the renderer (summary)

Must have:
- PACKED/REGLIST/IMAGE GIF parsing; A+D state; two contexts.
- Sprites, line strips (with AA1), triangles, triangle strips and **fans**; Gouraud; UV and STQ texturing; fog.
- General `(A−B)·C+D` blending with Ad/FIX and COLCLAMP; alpha test with AFAIL; depth test GEQUAL/GREATER/ALWAYS with ZMSK; DATE.
- 4-bit and 8-bit CLUT textures (including PSMT8H), CT32, CT24, CT16S, depth-as-texture (Z24), all clamp modes, bilinear filtering and **trilinear mipmapping with automatic mip addresses (MTBA)**.
- Local-memory model for render-to-texture: small targets at fixed pages and framebuffer copies.
- FBMSK, field-copy interlacing.

Not needed (not seen in code or dumps): PABE/FBA, SCANMSK, HWREG transfers, local → local and local → host transfers, PSMT4HL/HH, 16-bit Z, points.

The static scan said "no mipmapping" and "no CT24 textures": the dumps show both, so they are required.
