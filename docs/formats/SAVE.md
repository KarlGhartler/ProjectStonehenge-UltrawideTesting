# Save image (`BASLUS-20152AC04` main file)

The main save file on the memory card (`../subsystems/save.md`), built by the save page (`InitSaveFileImage` 0x0023C820, `../subsystems/frontend.md`). Size 0x4B10 bytes. Confidence: confirmed from the save page code.

## Identification

Magic `AC04` (u32 0x34304341) at offset 0.

## Layout

| Offset | Type | Field | Source |
|---|---|---|---|
| 0x0000 | u32 | magic `AC04` (0x34304341) | `InitSaveFileImage` |
| 0x0004 | u32 | size 0x4B10 | |
| 0x000C | u32 | checksum = byte sum of the 0x4B00 bytes from +0x10 | 0x0023C7B8 |
| 0x0010 | 4 × 0x12C0 | 4 slots, each a copy of the **campaign block 0x002A0318–0x002A15D7** | save page |

## Campaign block (0x002A0318–0x002A15D7, 0x12C0 bytes)

| Address | Content | Doc |
|---|---|---|
| 0x002A0318 | progress flags (bit 0 cleared once, bits 1/2 higher difficulties, bit 3 cleared twice) | `../subsystems/frontend.md` |
| 0x002A031C | trial unlock bits | `../subsystems/results-startup.md` |
| 0x002A0320 | ownership (six u64: 2 bits per aircraft per colour / special weapon) | `../subsystems/frontend.md` |
| 0x002A0350 / 0x002A0354 / 0x002A0358 / 0x002A035C | totals: flight time, kills, other kills, missions flown | `../subsystems/results-startup.md` |
| 0x002A0360 | usage counters [aircraft][colour] | same |
| 0x002A0448 + d·0x168 + m·0x14 | campaign mission records, 6 difficulties × 18 missions (first record 0x002A045C): `{u16 best rank, u16 points[3] descending, u32 time[3] ascending}` | same |
| 0x002A0CB8 + m·0x14 | trial mission records (same record layout) | same |
| 0x002A0D30 | credits (≤ 999,999,999) | `../subsystems/frontend.md` |
| 0x002A0D34 | trial ghost routes (128 samples per trial) | `../subsystems/mission-flow.md` |
| 0x002A15B8 / 0x002A15BA | `g_MissionNo`, difficulty | `../subsystems/frontend.md` |
| 0x002A15BE / C0 / C2 | selected aircraft, colour, special weapon | same |
| 0x002A15C4–0x002A15D3 | option bytes (sound, screen ratio, captions, HUD unit, interlude, brightness, screen position, controller settings) | same |

Statistics are clamped before saving (0x00105448), e.g. credits ≤ 999,999,999. Slot summaries shown by the save page: mission number and title, CLEAR mark, play time (frames / 0x34BC0 = hours), credits, difficulty.

## Other files of the save

| File | Size | Content |
|---|---|---|
| `icon.sys` | 0x3C4 | standard PS2 icon descriptor, built by the save page |
| `icon1.ico` | 0xCB98 | icon model, from the save page buffer (+0x228); loaded from DATA 0xB3 (`../formats/CDP.md`) |

Byte-identical saves require translating `InitSaveFileImage`, the checksum, the clamp, and the write order of `CreateMcSave`.
