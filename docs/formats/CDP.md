# CDP / TBL archives (`BIN/DATA.CDP` + `DATA.TBL`, `BIN/BD.CDP` + `BD.TBL`)

Layout derived from the loader code (`../game/file-loading.md`). Research extractor: `tools/cdp_extract.py`.

## Identification

No magic. The archives are found by fixed path in the file table `g_CdFileTable` 0x002BCBA8 (ids 0–3). A `.TBL` is a bare array; a `.CDP` is the concatenation of sector-aligned entries.

## TBL (index) — confirmed

| Offset | Type | Field | Source |
|---|---|---|---|
| 8·i + 0 | u32 LE | start sector of entry i inside the CDP (×2048 = byte offset) | `GetTblEntrySector` 0x00215468 |
| 8·i + 4 | u32 LE | size of entry i in bytes | `GetTblEntrySize` 0x00215488 |

- No header and no count field. The entry count is `file_size / 8`: DATA.TBL 2880 B → **360** entries, BD.TBL 104 B → **13** entries. The game never checks bounds; it indexes with constants.
- The TBL is loaded once into a fixed 0x1000-byte slot (`GetTblBuffer(slot)` 0x00215450 = 0x00421880 + slot·0x1000; slot 0 = DATA, slot 1 = BD). So the format allows at most 512 entries.
- Loads: DATA.TBL by `LoadBootData` 0x00100768 (main init, 2 sectors), BD.TBL by the boot scene 0x001008B0 state 1 (1 sector).

## CDP (data) — confirmed

- Entry i = bytes `[sector·2048, sector·2048 + size)`.
- The game reads `size/2048 + 1` whole sectors (`LoadDataEntryAsync` 0x002154E8, `GetDataEntryAllocSize` 0x002154A8), so buffers include the sector padding (and one extra sector when the size is an exact multiple of 2048).
- Entries are stored in index order, back to back, each starting on a sector boundary. Padding is zero.

Verification (`python tools/cdp_extract.py <ExtractedGameFiles>`):

| Archive | Entries | Sorted | Overlaps | Out of bounds | Coverage (bytes) | Coverage (sector-rounded) |
|---|---|---|---|---|---|---|
| DATA.CDP (721,625,088 B) | 360 | yes | 0 | 0 | 99.951 % | 99.999 % (last entry ends 0x7F0 bytes short of EOF) |
| BD.CDP (4,014,080 B) | 13 | yes | 0 | 0 | 99.700 % | 100.000 % |

## DATA.CDP entry contents

**Every one of the 360 entries is a container** (confirmed for the code paths that use it, e.g. `LoadBootData` with entry 0, `LoadSoundBankData` (H) with 0x164, `UpdateMenuDataLoad` 0x0022C6B0):

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | `count` |
| 4 | u32[count] | byte offset of each sub-block from the start of the entry (ascending) |
| … | | sub-blocks; sub-block k ends where k+1 starts (the last one ends at the entry size) |

Code converts the offsets to pointers in place (`ptr[k] = base + off[k]`, e.g. into `ctx->data0_table`).

Index map. Groups come from identical sub-block counts and leading words (`tools/research/cdp_sub.py`); meanings come from the loader code unless marked (H):

| Index | Count | Sub-blocks | Meaning | Evidence |
|---|---|---|---|---|
| 0 | 1 | 1 | global pointer table → `ctx->data0_table[0]` | `LoadBootData` |
| 1–100 (0x01–0x64) | 100 | 36 each, 1.7–13 MB | **mission data** (confirmed, PCSX2 session 7, `../pcsx2/sessions-02.md`) | `LoadFlightDataEntry` 0x0010AF10 scene 2 sub 2: `id + 0x52` (DATA 83–100) for ids < 0x13 when the player's aircraft id is in mask 0x00204114 (aircraft 2/4/8/14/21), else `id + 1`. Aircraft 2: campaign missions 1–4 → 83/84/85/98. All 100 share one 36-block layout |
| 101–172 (0x65–0xAC) | 72 = 3 × 24 | 9 each, ~1 MB | aircraft: 24 aircraft × 3 variants (confirmed, PCSX2 session 7, `../pcsx2/sessions-02.md`; layout in `AIRCRAFT.md`) | `LoadFlightDataEntry` sub 3/4: `0x65/0x7D/0x95 + aircraft_id` |
| 173–175 (0xAD–0xAF) | 3 | 2 | **title-screen demo flights** (2–4 MB each): `game[0x2B]` = 0/1/2 is set by the title idle cycle `StartTitleDemo` 0x0022CC68 (6 demo flights) | `LoadFlightDataEntry` 0x0010AF10 sub 5 (`0xAD + game[0x2B]`) |
| 176–186 (0xB0–0xBA) | 11 | 1–3 | **menu page assets**: 176 main menu (2), 177 campaign menu, 178 post-mission menu, 179 save page (3: icon.sys, icon1.ico, textures), 180 options, 181 controller settings (2), 182 sound options, 183 records, 184 BGM player, 185 interlude viewer (2), 186 HDD install | page enter functions via `QueueMenuDataLoad` 0x0022C618 (`../subsystems/frontend.md`) |
| 187–204 (0xBB + mission − 1) | 18 | 1, 105,488 B each | **mission title images** | `func_00230258` |
| 205–272 | 68 | 1–5 | menu 3D and text assets: 205 hangar scene (5); 206–227 aircraft-viewer models (0xCD + aircraft id: {`SM` model, 3 GIM}, `MODEL.md`); 228–258 weapon models (0xE4 + weapon id, ids ≤ 0x22 accepted, 0x00–0x1E present); 259–271 movie subtitle texts (0x103 + movie 0..12); 272 mission-select page (2) | `func_0022E1A8`, `func_0022E1F0`, `func_0022E218`, `MenuMovieEnter` 0x002390C8, `func_0023C600` |
| 273–290 (0x111 + mission) | 18 | 3 | **briefing data** per campaign mission | `func_00238278` |
| 291–332 | 42 | 1–2 | 291: 2 sub-blocks; 292–332: 41 single images of 66,832 B. **never loaded**: every DATA load call site was checked and its index is a constant or bounded at 290 (`../port/dead-code.md`) | — |
| 333 (0x14D) | 1 | 4 | startup-scene resources: 2 splash images + message-window data | scene 6 subs 1–2, 0x00260FC0 / 0x00260FE8 |
| 334–352 (0x14E + mission) | 19 | 3 | **results-screen backgrounds** per mission (0–18) | `func_0024C700` |
| 353–354 (0x161–0x162) | 2 | 3 | results backgrounds for result modes 2 and 3 | `func_0024C700` |
| 355 (0x163) | 1 | 1 (528 B) | descending u16 table 0x7FFF… (volume/pitch curve, H) | boot scene state 2 |
| 356–357 (0x164–0x165) | 2 | 1 | Namco sound-bank headers (H): SPU ADSR words, and 0x165 contains BD entry 1's size 0x14CC30 | boot scene state 4 |
| 358 (0x166) | 1 | 21 | stream-audio slot table (21 slots) | `LoadBootData` loads it next to the 21 stream-slot LSNs |
| 359 (0x167) | 1 | 1 | `PS2I` = HDD install icon/data | `HddInstallDataCdp` (H) |

Per-block layouts: mission entries `MISSION.md`, aircraft entries `AIRCRAFT.md`, textures `GIM.md`, models `MODEL.md`. The full list of DATA load call sites is: `AllocAndLoadDataEntry` 0x00102668 (entry 0, flight entries, results, startup), the menu queue `UpdateMenuDataLoad`, the sound-bank loader `LoadSoundBankData` (H), and the HDD install (0x167). Every index except 291–332 is matched to one of them; no call site can produce 291–332.

Common sub-block leading words: `0x1000xxxx` (e.g. `12080010` = `0x10000812`). These look like DMA tags (ID = 1 `cnt`, QWC in the low 16 bits), i.e. pre-built VIF/GIF packets. Confirmed for several mission blocks: sent directly with `sceDmaSend` or linked with DMA `call` tags (`MISSION.md`). `47494D00` (`GIM\0`, 301 sub-blocks) is Namco's texture format (`GIM.md`).

## BD.CDP entry contents

13 entries, all **raw SPU2 ADPCM** with no header: every 16-byte block has a valid shift/filter byte and flag byte, and loop/end flags appear 7–86 times per entry. They are streamed into IOP memory by `StreamBdEntryToIop` (H) 0x00219C80, and the matching bank headers are in DATA.CDP (0x164/0x165, H). The naming follows Sony's `.HD` (header) / `.BD` (body) sound-bank convention.

Which code streams which BD entry (`StreamBdEntryToIop(slot, bd_index)` 0x00219C80):

| BD | Size | Loaded by | Bank slot | Content |
|---|---|---|---|---|
| 0 | 66 KB | boot scene 0x001008B0 state 5 | 0 | common SE (header DATA 0x164) |
| 1 | 1.36 MB | startup `func_0025FF48` (once per boot, ctx flag 0x400000) | 10 | menu/3D SE (header DATA 0x165; bank 10 holds the missile whoosh, `../subsystems/sound.md`) |
| 2 | 1.00 MB | **never loaded**: the three `StreamBdEntryToIop` callers use BD 0, 1 and 3–12 only (`../port/dead-code.md`) | — | — |
| 3–12 | 112–181 KB | flight scene sound setup `func_0010FDE8` state 3, one per player aircraft | per aircraft (table 0x002A1878: {BD index, slot} by aircraft id, read from the executable at runtime) | engine sound banks shared by aircraft families (BD 5: aircraft 0/9/11/17/22, BD 9: 1/2/6/7/23, BD 10: 10/16/18/19, BD 7: 3/13/15, BD 8: 8/14, BD 3/4/6/11/12: one aircraft each) |

## Extractor output

`out/cdp/<DATA|BD>/<index:04d>.bin` plus `index.csv` (index, sector, offset, size, detected type, first 16 bytes). `--no-write` only runs the checks.
