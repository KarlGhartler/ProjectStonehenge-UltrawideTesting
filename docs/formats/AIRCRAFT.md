# Aircraft entries (DATA.CDP 101–172)

72 entries = 24 aircraft × 3 variants. Index = `0x65 + a`, `0x7D + a` or `0x95 + a` for variant 0/1/2 (`LoadFlightDataEntry` 0x0010AF10 sub-states 3/4: `a` = aircraft id `game[0x00]`/`game[0x10]`, variant `game[0x01]`/`game[0x11]`). Confirmed in PCSX2 (session 7, `../pcsx2/sessions-02.md`): aircraft 2 variant 0 → DATA 103 (0x67); the opening sequence id 0x25 used aircraft 0x17 → DATA 124 (0x7C).

Container: `count + offsets`, **9 sub-blocks** in all 72 entries. `StoreFlightDataPointers` 0x0010B038 stores them at flight work + **0x1754 + 4·k** (player, sub-state 3) and **0x1794 + 4·k** (second aircraft, sub-state 4). Loaders index the two by `unit · 0x40`.

## Block table

| k | Slot | Size (min–max) | First word / form | Read by | Role |
|---|---|---|---|---|---|
| 0 | +0x1754 | 606 – 876 KB | container (10 or 11) | `SetupAircraftModels` (H) 0x00147BF0, `func_00149FB8` | **aircraft models**: item 0's first word selects 4 LODs (< 0xB4) or 5; items 2… are models (`BindModelData` 0x00144618 → 0x28-byte model, `CreateModelInstance` 0x0013F018 → 0x4C instance); item 1 = pointer list (H: model parts/nodes); the last items are extra models (H: damaged parts, `func_00149FB8`) |
| 1 | +0x1758 | 264 – 281 KB | container (2) | `SetupAircraftModels` (H) | **skin textures**: GIMs. The first two are re-tagged at load to **PSMT8H** and placed at TBP 0x0E00 / 0x2300 with CLUTs 0x3F04 / 0x3F14 (`GIM.md`) |
| 2 | +0x175C | 26 – 83 KB | container (7, or 5) | `SetupAircraftModels` (H), `func_001459E0` | when count ≠ 5: 4 models at items 3–6 plus item 2 (H: cockpit/canopy or weapon models); also the unit model set for this aircraft (`func_001457E0`) |
| 3 | +0x1760 | 688 – 1,552 | `0x00010618` / `0x00010122` / `0x00001031` | `UpdateAircraftMovingParts` 0x001206E0 | **moving-part animation drivers**: records of 0x60 bytes `{n parts, part ids…, rate, up to 4 drivers × 0x14}`; each driver maps a flight value (`unit+0x240…0x25C`: control inputs, speed, …) through a range to a part angle, with combine modes 0–6 (set, max, min, larger deviation, add, scale). Types 7/8/9 = continuously accumulating (e.g. propeller), driven by the value at `unit+0x04` (scaled ×1/65536) and by the angle at `unit+0x14` (H) |
| 4 | +0x1764 | 3,392 | `0x100000D2` DMA `cnt` | `func_00163C08` (via `call` tag) | **pre-built GS/VIF packet** (H: aircraft-specific HUD or cockpit graphics) |
| 5 | +0x1768 | 4,208 | `0x10000105` DMA `cnt` | no reader confirmed (the offset match in `func_0017EBE8` is probably another struct) | pre-built packet |
| 6 | +0x176C | 208 – 240 | (zero first word) | `func_0011EFA0` (cached per unit in `0x00410650[i]`; `+0xD0` passed to `func_00149E28`) | **aircraft parameter block** (H: flight model / performance values) |
| 7 | +0x1770 | 448 | (zero first word) | `func_0010FDE8` → `func_00216570(unit, block8, block7)` | **aircraft sound setup**, part 1 (H: engine sound parameters) |
| 8 | +0x1774 | 272 | bytes like `35 30 2B 26` | `func_0010FDE8` → `func_00216570` | aircraft sound setup, part 2 (H: volume/pitch curve) |

Blocks 4, 5, 7 and 8 have the same size in every entry (fixed-layout structs or packets); block 6 varies (208–240 bytes).

## Open

- Model format (`BindModelData` input): header and node tree in `MODEL.md`; the VIF vertex layout inside the node packets is open.
- The exact fields of block 6 (flight model).
- Which item of block 2 is the cockpit.
