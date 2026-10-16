# Mission data entries (DATA.CDP 1–100)

Container format: `count + offsets` (`CDP.md`). All 100 entries have **36 sub-blocks**.

Loading: `LoadFlightDataEntry` 0x0010AF10 (scene 2, sub-state 2). The index is `id + 1` or `id + 0x52` (`../game/scenes.md`, confirmed in PCSX2 session 7, `../pcsx2/sessions-02.md`). `StoreFlightDataPointers` 0x0010B038 turns the 36 offsets into a pointer table at **flight work + 0x1694 + 4·k** (flight work = `*gp 0x004104C4`). Every consumer reads its block from that table.

Method: per-block size, first word and container check over all 100 entries (`tools/research/cdp_blocks.py`); the code that reads each pointer slot; hash comparison across entries (distinct contents per block).

Roles come from the reading code. "H" marks interpretations not yet checked in PCSX2.

## Block table

| k | Slot | Size (min–max) | Distinct among 100 | First word / form | Read by | Role |
|---|---|---|---|---|---|---|
| 0 | +0x1694 | 33,088 | 1 (identical) | `0x10000812` DMA `cnt` tag | (pointer table only) | pre-built DMA/VIF packet shared by all missions (H: VU1 microcode/data for the flight renderer) |
| 1 | +0x1698 | 15,168 | 1 | container (3) | `func_0017D8B0` → `func_0017CCA8` | 3 sets of GIM textures, uploaded to TBP 0x2500 / CBP 0x3F3C (H: sky/cloud/sun textures) |
| 2 | +0x169C | 133,568 | 2 | `0x1000209A` DMA `cnt` | `func_001D3318` | pre-built packet `call`ed (bucket 1) only for mission ids 0x0F, 0x12, 0x24, with FRAME/ZBUF changes around it (H: special sky/background effect) |
| 3 | +0x16A0 | 104,352 | 1 | container | `func_00149FB8` | model sets (each sub-container → 2 models via `BindModelData` 0x00144618/`CreateModelInstance` 0x0013F018) (H: shared effect/weapon models) |
| 4 | +0x16A4 | 5,168 | 1 | GIM (PSMT8H 64×64) | `func_00149FB8` | texture for block 3's models (TBP 0x1F00, CBP 0x3F0C) |
| 5–15 | +0x16A8…+0x16D0 | 256 B … 5.6 MB | 13–61 | mixed (block 7: container of 4) | `SetupMapFromMissionData` (H) 0x00139B70 (map object setup, called by `CreateWorldRenderObjects` 0x0013EA28) | **terrain / map data** (confirmed from the draw code, section "Terrain blocks 5–18" below): heights (9/10), vertex colours (11/12/13), cell attributes (5/6), texture mip lists (7), CLUT (8), static objects per cell (14/15) |
| 16 | +0x16D4 | 16 B – 1.68 MB | 45 | container | `SetupMapFromMissionData` (H) | **map object models**: n × (`BindModelData` → 0x28-byte model, `CreateModelInstance` → 0x4C-byte instance) |
| 17 | +0x16D8 | 16 B – 352 KB | 44 | nested container | `SetupMapFromMissionData` (H) | per-group pointer lists (H: map object groups / placement) |
| 18 | +0x16DC | 16 B – 39 KB | 17 | container (some) | `SetupMapFromMissionData` (H) | pointer list (H: collision or object table) |
| 19 | +0x16E0 | 17,456 | 24 | GIM (PSMT8H 128×128) | `func_00139520` → `func_00137068(…, 0x1E00, 0x3F08)` | map-scale texture (H: terrain detail or ground texture) |
| 20 | +0x16E4 | 48–64 | 68 | first word `0x6F` (=111) | `SetupMapFromMissionData` (H) | **"MTXT" header**: version check `< 0x6F` prints `WARNING : MTXT version old! : %3.2f(%3.2f)` (expected 1.11). 93 entries are version 0x6F, 5 are 0x64 |
| 21 | +0x16E8 | 16 B – 4.9 MB | 63 | container | `func_001459E0` | **unit model sets**: one entry per unit model type (`func_001457E0`), used by the units placed by block 22 |
| 22 | +0x16EC | 368 – 87 KB | 69 | `0x00010527` or container of 3 | `SetupMissionUnits` 0x0010E758 | **unit / object placement**: when it is a container of 3, the variant is chosen by difficulty `game[0x28]`. Each record is 0x18 bytes (`type`, `index`, …) and points into 10 typed sub-tables (record sizes 0x24, 0x14, 0x20, 0x24, 0x20, 0x28, 0x18, 0x20, 0x18, 0x14). Sets the per-unit structs `0x002A1F70[i]` (AI/aircraft/ground types, flags by unit code) |
| 23 | +0x16F0 | 17.6 – 66.8 KB | 30 | `0x1000044E` / `0x1000104E` DMA `cnt` | `DrawRadarMap` 0x0016BC68 (radar/map HUD) via `call` tag `func_00104338` | **pre-built GS packet: mission map image** for the radar/map display |
| 24 | +0x16F4 | 512 | 73 | `0x02000004` | `func_001118C8` → `func_00113708` (stored in gp 0x004104D4 as a physical address `& 0x0FFFFFFF`) | (H) per-mission render settings (read again by `SetupMapFromMissionData` (H) for MTXT fix-ups: `+0x12E`) |
| 25 | +0x16F8 | 41,776 | 34 | `0x10000A31` DMA `cnt` | `func_001118C8`: `sceDmaSend(GIF ch.2, block & 0x0FFFFFFF)` | **pre-built texture-upload chain** sent once at load (GIF IMAGE transfers) |
| 26 | +0x16FC | 66,816 | 34 | `0x1000104E` DMA `cnt` | `func_00113708`, `func_00115F28` | pre-built packet (H: sky/horizon drawing) |
| 27 | +0x1700 | 5,168 – 16,416 | 17 | GIM | `SetupMapFromMissionData` (H) (`InitGimTexture` 0x00105A08, TBP 0x3A00, CBP 0x3F20) | map texture |
| 28 | +0x1704 | 240 – 2,336 | 40 | container (1 or 2) | `func_0010FA70` → `func_0011A3D8` (+ `func_0015E8C0` for the 2nd part) | (H) mission setup tables (called during sub-state init together with unit setup `SetupMissionUnits`) |
| 29 | +0x1708 | 16 B – 2.2 MB | 9 | container | `SetupInEngineSequence` 0x00176DD0 | **in-engine sequence data**, indexed by `game[0x2C]`; each sequence has 4 parts (part 2 → `func_00107BD0`, part 3 → `func_00106938`). Mostly empty (87 entries start with 0); used by the opening/intro/ending ids 0x23/0x24/0x25 (which also start VAG tracks 0x30/0x33/0x32) |
| 30 | +0x170C | 16,864 | 41 | container (16) | `InitEffects` 0x00154660 (mission init `StartFlightSystems` 0x0010AB58, `func_001704A0`) | (H) 16 sub-tables, e.g. effect or particle definitions |
| 31 | +0x1710 | 16 B – 1.2 MB | 39 | container (some) | `SetupMapFromMissionData` (H) → `func_00139718(map, block, 1)` | **second map entry** (confirmed from the code, `../subsystems/mission-load.md`): a container of 15 items with the roles of blocks 5–18 for map entry 0 (items 0–6, 8 → words 0–7; 7 → the 4 lists; 9, 10; 11 models; 12 groups; 13 GIM; 14 pointer list); an empty block means no second entry |
| 32 | +0x1714 | 64 | 38 | `0x6F` | `SetupMapFromMissionData` (H) (copied to map `+0xB8`) | second versioned header (0x6F) |
| 33 | +0x1718 | 512 | 38 | `0x02000004` | `SetupMapFromMissionData` (H) (copied to map `+0xC0`) | same form as block 24 |
| 34 | +0x171C | 17 – 33 KB | 41 | `0x00000030` | `StartFlightSystems` → `func_00250C30(block34, block35)` | (H) mission message/radio system data: tables of offsets resolved into a 0xA80-entry table at 0x004BA570 |
| 35 | +0x1720 | 31 – 70 KB | 41 | container (5 or 6) | `StartFlightSystems` → `func_00250C30` | second argument of the same system |

Notes:
- Blocks 0, 1 and 3–4 are identical in all 100 entries; block 2 has 2 variants. These are common flight-scene resources duplicated into every mission entry.
- Several blocks are **pre-built DMA/GS packets** (0, 2, 23, 25, 26). They start with DMA `cnt` tags (ID 1; the QWC covers almost the whole block, e.g. block 0: QWC 0x812 = 33,056 bytes of 33,088) and are either sent directly (block 25, `sceDmaSend` of a physical address) or linked into the frame with `call` tags (blocks 2, 23). Loaded data therefore contains ready-to-send packets (`../subsystems/rendering.md`). Whether they contain absolute addresses (e.g. `ref` tags) is open (`../game/memory.md`).

## Terrain blocks 5–18 (from the draw code)

Chain: `SetupMapFromMissionData` copies the blocks into map entry 0 (`../subsystems/mission-load.md`), `func_00139FE8` copies the entry into the terrain object T (`*0x00410694`) and `func_00144CC0` into the static-object object O (`*0x0041069C`) each frame. The map is 256 × 256 cells of 2048 units, grouped in 16 × 16 blocks of 16 × 16 cells; each cell has a 3 × 3 grid of half-cell samples (32 × 32 per block). Readers: `../subsystems/rendering.md` (Terrain).

| Block | Entry word → T / O field | Format (as read) |
|---|---|---|
| 10 | e[0] → T+8 | 256 bytes: per block (x / 16 + 16 · (z / 16)) the page index of block 9 |
| 9 | e[1] → T+0xC | height pages of 0x400 bytes: byte at 2 · (x % 16) + 0x40 · (z % 16) + half-cell offset (0, 1, 2 along x, +0x20 per half step along z); height = byte · −0x50 (`func_0013A5C8`) |
| 12 | e[2] → T+0x10 | 256 bytes: per block the page index of block 11 |
| 11 | e[3] → T+0x14 | colour-index pages of 0x400 bytes, same layout as block 9; each byte indexes block 13 |
| 13 | e[4] → T+0x18 | 256 words: vertex colour palette (RGBA; the shared default is 0x80808080) |
| 6 | e[5] → T+0x1C | 256 bytes: per block the page index of block 5 |
| 5 | e[6] → T+0x20 | attribute pages of 256 u16 (0x200 bytes), index (x % 16) + 16 · (z % 16): bits 0–9 texture id, 0x400 mirror U, 0x800 mirror V, bits 12–15 other flags (kept in the cell list, `func_0013B2F8`) |
| 7 | e[8..0xB] → T+0x24…+0x30 | 4 lists (mip levels 0–3) indexed by texture id: pointer to the level's 8-bit texels (64 >> k square, 0x100 >> 2k qwords, uploaded with a DMA `ref`) |
| 8 | e[7] → T+0x34 | 16 × 16 CT32 CLUT (1 KiB), uploaded to block 0x3F00 |
| 15 | e[0xC] → O+0x7018 | 256 bytes: per block the page index of block 14 |
| 14 | e[0xD] → O+0x7014 | object pages of 256 bytes, index (x % 16) + 16 · (z % 16): static-object instance index for the cell, 0xFF = none |
| 16 | e[0xE] (instances) → O+0x7004 | static-object models (container; 0x28-byte model + 0x4C-byte instance each); a model's word +0x1C → texture record whose word +0x1C is its texture index (the draw bucket) |
| 17 | e[0xF] → O+0x7010, count e[0x13] → O+0x700C | per texture: a container of 4 GIMs (mip levels, uploaded to 0x2500, 0x2900, 0x2A00, 0x2A40; CLUT 0x3F10) |
| 18 | e[0x11], count e[0x14] | pointer list; not read by the draw code (gameplay side, S5 block B) |

Checked against all 164 DATA entries with `tools/research/terrain_check.py` (2026-10-16): every page table stays below its page count, blocks 6/10/12/15 are 256 bytes and 13 is 1 KiB, the block 7 lists have equal counts and every texture id in block 5 is below that count (all ids are used), block 7 items are at least 0x1000 >> 2k bytes, every block 14 object index is below the block 16 model count, and block 17 is one group of 4 GIMs (or empty, with no models). Attribute bits 12–13 take the values 0–3 and bits 14–15 are 0; the draw code keeps bits 10–15 in the cell list but reads only the mirror bits 10 and 11 (bits 12–13: H, gameplay terrain kind).

Block 31 holds the same set for map entry 1. The per-entry render settings (block 24 / entry word 0x15 = block 20 header, `func_00139038`) set T+0x88…+0xF4 (sun angles, fog, LOD bias, flags).

## Blocks 22, 28, 30, 34, 35 (from the reading code)

- **22** (unit placement): header + 18 typed sub-table pointers; unit records of 0x18 bytes (type, index, position, angles); type records per sub-table (sizes in the table above); full read order in `../subsystems/mission-load.md` (`SetupMissionUnits`).
- **28** (mission rules): part 1 = event script tables (0x13 section offsets, three level-sorted lists, three unit link lists; `func_0011A3D8`); part 2 = 16 halfwords of map-explored bits (`func_0015E8C0`).
- **30** (effects): 16 sub-tables at offsets +4…+0x40 relative to the block (`func_00184C20`, stored at 0x002B8530…0x002B856C), used by the effect pools.
- **34** (radio database): header section offsets and u16 counts → 0x004BA300…0x004BA32F; record sections of 8, 12 and 8 bytes resolved into the 0xA80-entry table at 0x004BA570 (`func_00250C30`).
- **35** (messages): header words w0…w5 (font data at w1/w4, string pointers per message, u16 strings ending ≥ 0xFFFE, w5 → `func_00259280`) (`func_002509F8`).

## `id + 1` vs `id + 0x52`

Comparing DATA 2 (id 1, `+1`) with DATA 83 (id 1, `+0x52`): blocks 0–20 and 23–30 are **identical**. Only these differ:
- 21: unit model sets (0.8 MB vs 1.6 MB);
- 22: unit placement;
- 31: map part;
- 32/33: small headers;
- 34/35: message/radio data.

So the alternate set is the same map with a different set of units, models and messages. The choice is made by the player's aircraft (mask 0x00204114 = aircraft 2, 4, 8, 14, 21). The reason is open; one possibility is mission variants with different wingman or enemy setups for certain starting aircraft (H, weak).

## Open

- Block 18 (gameplay pointer list) and the exact field meanings of the unit type records (22) beyond what `SetupMissionUnits` reads.
- What the shared packets 0 and 26 draw.
- Which campaign missions use which variant in normal play, and what differs on screen.
