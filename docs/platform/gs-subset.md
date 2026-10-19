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

## Port implementation status (slices S1, S2a; 2026-10-14)

`src/platform/render/gs/`: GIF intake (PATH3, and PATH2 through VIF1 DIRECT/DIRECTHL with TTE tag words; other VIF codes skipped and logged), PACKED/REGLIST/IMAGE parsing, both contexts, local memory with the GS page, block and column layout of each format (byte for byte the GS memory image, so data written in one format reads correctly in another; checked against PCSX2 VRAM images with `tools/research/gsvram.py`), host->local transfers (one byte stream per transfer: a CT24 pixel split across two IMAGE packets is completed from the next packet, S2a) and local->local transfers, CLUT loads (CSM1, CT32/CT16), texture decode per draw (T4/T8/T8H/CT32/CT24/CT16) into a per-frame atlas, sprites/triangles/strips/fans/lines/points, TFX modulate/decal/highlight, GS wrap modes and nearest/bilinear in the shader, alpha test (AFAIL KEEP), blending through dual-source blend (`(A-B)*C+D` forms with As and FIX), FBMSK per channel, scissor, XYOFFSET, frame buffers read as textures (copy first), presentation of the draw buffer as the field copy reads it (ADR-006 §4). Blending is fixed-function float, not the GS's integer `((A−B)·C >> 7) + D`; in feedback effects this leaves a small bias against PCSX2 (`../port/validation.md` S2a-4, open). Logged when used, not implemented yet: depth test, DATE, PABE, COLCLAMP 0, partial FBMSK, Ad blending other than `(Cs−0)·Ad+Cd`, VU1 programs (MPG/UNPACK/MSCAL).

Added in S2a (front-end filters):
- `(Cs−0)·Ad+Cd` (ALPHA 0x58, the menu scanline bands and zoom blur): targets store the GS alpha byte as a/255, so the pipeline blends with DST_ALPHA and the shader outputs Cs/128 instead of Cs/255. Exact while Cs ≤ 0x80 (UNORM outputs clamp to 1); the menu filter colours stay below that. Other equations with Ad are logged and not mapped.
- Alpha test fail modes: KEEP and ZB_ONLY discard (ZB_ONLY's depth write is not done), FB_ONLY keeps the pixel, RGB_ONLY is a second draw of the failing pixels with alpha masked.
- Texel footprint for bilinear filtering: `floor(uv − 0.5)` to `floor(uv − 0.5) + 1` (the shader's taps). The earlier one-texel margin made REPEAT-mode sprites starting at u = 0.5 decode the whole 1024×1024 texture and overflow the 2048² atlas (title logo).

Added in S4 (hangar 3D models, 2026-10-16):
- **VIF1 + VU1**: `src/platform/render/vu1/` consumes the VIF1 code stream (UNPACK with CL/WL, masks, modes, FLG/USN; MPG source tracking; BASE/OFFSET/TOPS double buffer; DIRECT; MSCAL/MSCNT) and runs programs A, B, C, E and the terrain program T (`.vudata`) as CPU reimplementations (A, E, T since 2026-10-16, not yet checked against PCSX2) whose XGKICK output joins the GIF stream (`../subsystems/vu-programs.md`). Their output was compared with the PCSX2 hangar GS dump (`tools/research/gifstrips.py`): packet and vertex counts per texture, screen and Z ranges and mean vertex colours agree.
- **Depth buffer**: every colour target has a D32 depth image; vertex Z is the GS Z divided by the size of the ZBUF format (exact for Z24/Z16), interpolated linearly in screen space; TEST.ZTE/ZTST (NEVER, ALWAYS, GEQUAL, GREATER) and ZBUF.ZMSK become the pipeline depth state. AFAIL ZB_ONLY still writes no depth.
- **Z buffer cleared as a colour buffer**: the viewer clears Z by drawing a sprite with FRAME_2 = the ZBP (0xDC). A Z buffer that draws really test (ZTE with GEQUAL/GREATER) is remembered with its target; later untextured sprites whose FBP is that ZBP fill the target's depth image with their colour as Z (RGB = bits 0-23, A = 24-31) instead of drawing colour. Menus keep ZTE with ALWAYS and are not counted (their ZBP aliases other buffers).
- **Triangles decode their whole texture** (one decode and one atlas entry per texture instead of per triangle, so a model merges into a few draws); the atlas is 4096².
- Not yet: fog (FGE; the hangar's 3D prims set it), the glow pass's half-resolution copy (program D, S5f).

### Frame buffer read as texture: target selection (2026-10-16)
Two targets can share a block pointer: the menus draw a 640-wide buffer (FBW 10) at FBP 0 and the flight scene a 512-wide one (FBW 8) at the same FBP. The field copy of the flight scene (`DrawDisplayTransfer` mode 1, TEX0.TBW 8) picked the stale menu target (the last match in key order), so missions presented a black 640-wide image. The source is now the target whose FBW equals TEX0.TBW (any target at that FBP otherwise), and only a frame-as-texture draw that writes into the display buffer (FBP of the last `sceGsPutDispEnv` DISPFB) is taken as the image to present (ADR-006 §4); the flight sky's own frame reads (work pages 0x120/0x140/0x190, glow chain) are not.

Frames without a field copy (the briefing movie, for example) present the frame buffer directly. `PresentSize` took the first target at FBP 0 and `CopyPresent` the last, so with both the menu (FBW 10) and flight (FBW 8) targets alive the briefing was copied at 512 columns (found 2026-10-17 by `--fast-until` screenshot comparisons: the result depended on which targets earlier screens had created). Both now use `GsBackend::DisplayTarget`: the target at DISPFB's FBP with DISPFB's FBW, else the first target at FBP 0.

Local memory column order (2026-10-17): pixels inside a 256-byte block are stored in the GS column order (32/16/8/4-bit tables in `gs_memory.cpp`), not row-major. The tables were checked against the PCSX2 state `s5_m1_start` (`tools/research/gsvram.py`: 585 CT32, 48 T8 and 87 T4 blocks of the port's memory matched PCSX2's only after this conversion; after the change they are equal byte for byte). The 16-bit table had no data to check in that scene. The HUD depends on it: its font is uploaded every frame as a T8 128×128 image at TBP 0x2500 and read as a T4 256×256 texture at the same address. Earlier screens render identically (`validation.md` S5-T2).

REGLIST tags with PRE set now load PRIM as PACKED tags do (the HUD glyphs are REGLIST sprites with PRE).

Frame buffers overwritten by transfers: `GsState` tracks, in stream order, the frame buffers drawn (FBP and the pages FBW × scissor height covers). A host->local or local->local transfer overlapping one marks it overwritten until it is drawn into again, and a texture at that address is then read from local memory instead of the frame buffer. The flight sky renders into work pages 0x120–0x148 early in the frame and the game later uploads the HUD font and cloud textures over the same pages; before this, those draws sampled the sky's render target (no HUD text, no cloud texture).

GS-dump player (`--play-gsdump`, development builds): loads a PCSX2 dump's memory image (offset 0x1A9 of the version-9 GS state) into local memory, feeds its GIF transfers frame by frame and writes the native picture next to PCSX2's own screenshot, without the game. The mission 1 dump showed the three problems above; after the fixes the frame matches PCSX2's screenshot except a dark speckled patch near the bottom centre (the 192 DATE draws, not implemented). Limits: the GS registers of the dump's state are not loaded (each frame of the game sets the ones it uses), nor the CLUT buffer.

### Mission frames: fog, mipmaps, DATE, AA1, frame buffers in other formats (2026-10-17)
Found by drawing the PCSX2 dumps of missions 1, 3 and 4 with `--play-gsdump` (`../port/validation.md` S5-A3) and listing their draws with `tools/research/gsdraws.py`:
- **Fog (FGE).** F (XYZF2/FOG) was parsed but never reached the GPU. It is now a vertex attribute (interpolated) and `gs.frag` applies `floor(mix(FOGCOL, C, F/255))` to the colour after the texture function (alpha unchanged). Mission 4's haze over the far terrain and most of the far-terrain colour of missions 1 and 3 come from it.
- **Mipmapping and the filter choice (TEX1).** The terrain uses MMIN 5 (trilinear), MXL 3, MTBA 1. `DecodeTexture` now also decodes levels 1..MXL (MTBA: each level follows the previous one in local memory, `w·h·bpp/8` bytes, buffer width halved, HYPOTHESIS beyond the power-of-two page-filling case; MTBA 0: MIPTBP1/2) into the same atlas rectangle, right of level 0. `gs.frag` computes LOD = K (LCM 1) or log2(1/|Q|)·2^L + K, takes MMAG for LOD ≤ 0 and MMIN above (nearest/linear, nearest mip level by rounding or two levels blended). Before, only MMAG was used and level 0 everywhere, which made far terrain sharp and contrasty.
- **DATE.** Before a DATE draw the target region its vertices cover (clipped to the scissor) is copied into an image the shader reads; pixels whose stored alpha bit 7 differs from DATM are discarded. The copy is taken per draw, so later primitives of the same draw see the copy, not earlier primitives of that draw.
- **AA1.** The HUD lines are AA1 line strips without ABE. The GS blends AA1 primitives with the ALPHA register using the pixel coverage as As; inside a primitive the coverage is 0x80, so the lines are added to the background (`(Cs−0)·As+Cd`, ALPHA 0x48). Edge coverage is not modelled.
- **Frame buffers in other formats or widths (gs_conv.comp).** The sky's work pages are drawn as CT32, copied onto themselves as CT16S (a half-pixel-shifted 16-bit copy) and read back as CT32; targets are read as T8 and T8H too. Targets are now keyed by FBP, FBW and layout class (32-bit, CT16, CT16S) and carry the draw clock of their last write. A texture read picks the newest target at its TBP; in the same 32-bit layout and width it is copied as before, otherwise a compute pass rebuilds the texture through the GS memory layout (forward page table of the texture format, inverse table of the target format, CLUT/TEXA applied). Before a draw into a target, a newer target at the same FBP (another format or width of the same memory) is converted into it (`GsBackend::Sync`). 16-bit targets keep the GS's 5-bit colour and 1-bit alpha (shader flag 24). A frame buffer drawn earlier in the same frame also counts as a texture source (`GsState`; the backend creates its targets only when the frame is recorded).
- **Presented height.** At most 448 lines (the flight scissor includes line 448).
Result: the three mission dumps match PCSX2's screenshots to 1.5–2.0 (mission 1) and 1.2–1.8 (mission 4) mean per channel; mission 3's remaining difference is camera motion between the dump and the screenshot. The title-to-hangar screenshots are unchanged pixel for pixel.

Still logged when used: PABE, COLCLAMP 0, partial FBMSK, CLUT CSM2, texture PSM 0x5, PRIM 7, local→host transfer, the blend equations with Ad other than `(Cs−0)·Ad+Cd`.
