# GIM texture (`GIM\0`), Namco in-house format

Phase 2.5. Found as sub-blocks of DATA.CDP entries (301 sub-blocks start with `GIM\0`, `CDP.md`). **Not** Sony's PSP GIM format (which starts with `MIG.00.1PSP`).

Parser: `func_00105A08(tex, gim)` (EE 0x00105A08, 13 caller functions, e.g. the map loader 0x00139B70, aircraft loader 0x00147BF0, sky textures 0x0017CCA8, startup scene 0x00260FE8). It fills a 0x28-byte runtime texture descriptor `tex`. The helper `func_00105CF8(tex, tbp, cbp)` (3 callers) sets the VRAM addresses.

## Layout

| Off | Type | Field | Confidence |
|---|---|---|---|
| 0x00 | char[4] | magic `GIM\0` | confirmed (data) |
| 0x04 | u32 | flags: bit 0 = a CLUT follows the pixels; bit 1 = (copied to `tex+0x11`, meaning unknown). Some files hold `0xCDCDCDCD` here (H: MSVC debug-heap fill left by the original tool; bit 0 is still set and a CLUT is present) | confirmed (code) / H (bit 1) |
| 0x08 | u32[2] | unused (0) | data |
| 0x10 | 16 bytes | **image header**, see below | confirmed |
| 0x20 | … | pixel data, `size(psm, w, h)` bytes | confirmed |
| align16(0x20 + size) + … | 16 + n | optional CLUT: same 16-byte header layout (PSM at +3, w/h), then CLUT data | confirmed (code: `tex[2] = gim + align16(size + 0x2F)`, `tex[7] = gim + align16(size + 0x3F)`) |

Image / CLUT header (16 bytes):

| Off | Type | Field |
|---|---|---|
| +0x0 | u16 | GS buffer pointer (TBP0 / CBP). Overwritten at load time with the VRAM address chosen by the game (e.g. `0x3A00`, `0x1F00`, `0x0E00`/`0x2300` for aircraft skins) |
| +0x2 | u8 | buffer width TBW, recomputed at load as `(w + 63) >> 6` |
| +0x3 | u8 | GS PSM: 0x00/0x01/0x30/0x31 (32-bit), 0x02/0x0A/0x32/0x3A (16-bit), 0x13/0x1B (8-bit, PSMT8/PSMT8H), 0x14/0x24/0x2C (4-bit, PSMT4/4HL/4HH) |
| +0x4 | 8 bytes | unused in the observed files |
| +0xC | u16 | width |
| +0xE | u16 | height |

Pixel size by PSM (`func_00105A08`): 32-bit = w·h·4, 16-bit = w·h·2, 8-bit = w·h, 4-bit = (w·h+1)/2. The loader also stores log2(w) and log2(h) (TW/TH for TEX0) in the descriptor. For PSMT8/PSMT4 images, TBW is rounded up to an even value.

The game sometimes **changes the PSM at load time**: aircraft skins are re-tagged to PSMT8H (0x1B) and placed into the alpha byte of 32-bit VRAM pages (0x00147BF0). A port must emulate that VRAM placement, or convert PSMT8H to plain 8-bit with the same CLUT.

## Examples (DATA entry 2, mission id 1)

| Sub-block | PSM | Size | CLUT |
|---|---|---|---|
| 4 | 0x1B (PSMT8H) | 64×64 | yes |
| 19 | 0x1B (PSMT8H) | 128×128 | yes (flags 0xCDCDCDCD) |
| 27 | 0x13 (PSMT8) | 64×64 | yes |
