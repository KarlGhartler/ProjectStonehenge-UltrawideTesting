# Mission data entries (DATA.CDP 1–100)

Phase 2.5 backlog item c. Container format: `count + offsets` (`CDP.md`). All 100 entries have **36 sub-blocks**.

Loading: `LoadFlightDataEntry` 0x0010AF10 (scene 2, sub-state 2). The index is `id + 1` or `id + 0x52` (`scenes.md`, confirmed in PCSX2 session 7). `func_0010B038` turns the 36 offsets into a pointer table at **flight work + 0x1694 + 4·k** (flight work = `*gp 0x004104C4`). Every consumer reads its block from that table.

Sources:
- `out/scratch/mission_blocks.py`: per-block size, first word and container check over all 100 entries;
- `block_users.py`: which code reads each slot;
- hash comparison across entries (distinct contents per block).

Roles come from the reading code. "H" marks interpretations not yet checked in PCSX2.

## Block table

| k | Slot | Size (min–max) | Distinct among 100 | First word / form | Read by | Role |
|---|---|---|---|---|---|---|
| 0 | +0x1694 | 33,088 | 1 (identical) | `0x10000812` DMA `cnt` tag | (pointer table only) | pre-built DMA/VIF packet shared by all missions (H: VU1 microcode/data for the flight renderer) |
| 1 | +0x1698 | 15,168 | 1 | container (3) | `func_0017D8B0` → `func_0017CCA8` | 3 sets of GIM textures, uploaded to TBP 0x2500 / CBP 0x3F3C (H: sky/cloud/sun textures) |
| 2 | +0x169C | 133,568 | 2 | `0x1000209A` DMA `cnt` | `func_001D3318` | pre-built packet `call`ed (bucket 1) only for mission ids 0x0F, 0x12, 0x24, with FRAME/ZBUF changes around it (H: special sky/background effect) |
| 3 | +0x16A0 | 104,352 | 1 | container | `func_00149FB8` | model sets (each sub-container → 2 models via `func_00144618`/`func_0013F018`) (H: shared effect/weapon models) |
| 4 | +0x16A4 | 5,168 | 1 | GIM (PSMT8H 64×64) | `func_00149FB8` | texture for block 3's models (TBP 0x1F00, CBP 0x3F0C) |
| 5–15 | +0x16A8…+0x16D0 | 256 B … 5.6 MB | 13–61 | mixed (block 7: container of 4) | `func_00139B70` (map object setup, called by `func_0013EA28`) | **terrain / map data**: copied into the map object (`*PTR 0x00410694`) fields 0–13; block 7 = 4 sub-tables (H: terrain quadrants or LODs); blocks 6/10/12/15 are 256-byte tables; 13 is a 1 KB table of `0x80808080`; 14 starts with `0xFFFFFFFF` |
| 16 | +0x16D4 | 16 B – 1.68 MB | 45 | container | `func_00139B70` | **map object models**: n × (`func_00144618` → 0x28-byte model, `func_0013F018` → 0x4C-byte instance) |
| 17 | +0x16D8 | 16 B – 352 KB | 44 | nested container | `func_00139B70` | per-group pointer lists (H: map object groups / placement) |
| 18 | +0x16DC | 16 B – 39 KB | 17 | container (some) | `func_00139B70` | pointer list (H: collision or object table) |
| 19 | +0x16E0 | 17,456 | 24 | GIM (PSMT8H 128×128) | `func_00139520` → `func_00137068(…, 0x1E00, 0x3F08)` | map-scale texture (H: terrain detail or ground texture) |
| 20 | +0x16E4 | 48–64 | 68 | first word `0x6F` (=111) | `func_00139B70` | **"MTXT" header**: version check `< 0x6F` prints `WARNING : MTXT version old! : %3.2f(%3.2f)` (expected 1.11). 93 entries are version 0x6F, 5 are 0x64 |
| 21 | +0x16E8 | 16 B – 4.9 MB | 63 | container | `func_001459E0` | **unit model sets**: one entry per unit model type (`func_001457E0`), used by the units placed by block 22 |
| 22 | +0x16EC | 368 – 87 KB | 69 | `0x00010527` or container of 3 | `func_0010E758` | **unit / object placement**: when it is a container of 3, the variant is chosen by difficulty `game[0x28]`. Each record is 0x18 bytes (`type`, `index`, …) and points into 10 typed sub-tables (record sizes 0x24, 0x14, 0x20, 0x24, 0x20, 0x28, 0x18, 0x20, 0x18, 0x14). Sets the per-unit structs `0x002A1F70[i]` (AI/aircraft/ground types, flags by unit code) |
| 23 | +0x16F0 | 17.6 – 66.8 KB | 30 | `0x1000044E` / `0x1000104E` DMA `cnt` | `func_0016BC68` (radar/map HUD) via `call` tag `func_00104338` | **pre-built GS packet: mission map image** for the radar/map display |
| 24 | +0x16F4 | 512 | 73 | `0x02000004` | `func_001118C8` → `func_00113708` (stored in gp 0x004104D4 as a physical address `& 0x0FFFFFFF`) | (H) per-mission render settings (read again by `func_00139B70` for MTXT fix-ups: `+0x12E`) |
| 25 | +0x16F8 | 41,776 | 34 | `0x10000A31` DMA `cnt` | `func_001118C8`: `sceDmaSend(GIF ch.2, block & 0x0FFFFFFF)` | **pre-built texture-upload chain** sent once at load (GIF IMAGE transfers) |
| 26 | +0x16FC | 66,816 | 34 | `0x1000104E` DMA `cnt` | `func_00113708`, `func_00115F28` | pre-built packet (H: sky/horizon drawing) |
| 27 | +0x1700 | 5,168 – 16,416 | 17 | GIM | `func_00139B70` (`func_00105A08`, TBP 0x3A00, CBP 0x3F20) | map texture |
| 28 | +0x1704 | 240 – 2,336 | 40 | container (1 or 2) | `func_0010FA70` → `func_0011A3D8` (+ `func_0015E8C0` for the 2nd part) | (H) mission setup tables (called during sub-state init together with unit setup `func_0010E758`) |
| 29 | +0x1708 | 16 B – 2.2 MB | 9 | container | `func_00176DD0` | **in-engine sequence data**, indexed by `game[0x2C]`; each sequence has 4 parts (part 2 → `func_00107BD0`, part 3 → `func_00106938`). Mostly empty (87 entries start with 0); used by the opening/intro/ending ids 0x23/0x24/0x25 (which also start VAG tracks 0x30/0x33/0x32) |
| 30 | +0x170C | 16,864 | 41 | container (16) | `func_00154660` (mission init `func_0010AB58`, `func_001704A0`) | (H) 16 sub-tables, e.g. effect or particle definitions |
| 31 | +0x1710 | 16 B – 1.2 MB | 39 | container (some) | `func_00139B70` → `func_00139718(map, block, 1)` | map data part (H: terrain mesh patches) |
| 32 | +0x1714 | 64 | 38 | `0x6F` | `func_00139B70` (copied to map `+0xB8`) | second versioned header (0x6F) |
| 33 | +0x1718 | 512 | 38 | `0x02000004` | `func_00139B70` (copied to map `+0xC0`) | same form as block 24 |
| 34 | +0x171C | 17 – 33 KB | 41 | `0x00000030` | `func_0010AB58` → `func_00250C30(block34, block35)` | (H) mission message/radio system data: tables of offsets resolved into a 0xA80-entry table at 0x004BA570 |
| 35 | +0x1720 | 31 – 70 KB | 41 | container (5 or 6) | `func_0010AB58` → `func_00250C30` | second argument of the same system |

Notes:
- Blocks 0, 1 and 3–4 are identical in all 100 entries; block 2 has 2 variants. These are common flight-scene resources duplicated into every mission entry.
- Several blocks are **pre-built DMA/GS packets** (0, 2, 23, 25, 26). They start with DMA `cnt` tags (ID 1; the QWC covers almost the whole block, e.g. block 0: QWC 0x812 = 33,056 bytes of 33,088) and are either sent directly (block 25, `sceDmaSend` of a physical address) or linked into the frame with `call` tags (blocks 2, 23). This confirms the render-boundary hypothesis that loaded data contains ready-to-send packets. Whether they contain absolute addresses (e.g. `ref` tags) is still open (`memory-model.md`).

## `id + 1` vs `id + 0x52`

Comparing DATA 2 (id 1, `+1`) with DATA 83 (id 1, `+0x52`): blocks 0–20 and 23–30 are **identical**. Only these differ:
- 21: unit model sets (0.8 MB vs 1.6 MB);
- 22: unit placement;
- 31: map part;
- 32/33: small headers;
- 34/35: message/radio data.

So the alternate set is the same map with a different set of units, models and messages. The choice is made by the player's aircraft (mask 0x00204114 = aircraft 2, 4, 8, 14, 21). The reason is open; one possibility is mission variants with different wingman or enemy setups for certain starting aircraft (H, weak).

## Open

- Formats of the terrain blocks (5–18, 31), the unit placement records (22) and the message system (34/35).
- What the shared packets 0 and 26 draw.
- Which campaign missions use which variant in normal play, and what differs on screen.
