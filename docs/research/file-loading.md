# File loading pipeline

Phase 2 result. Every name below is a Ghidra name; `(H)` = hypothesis (plate comment only). Archive format: `docs/formats/CDP.md`.

## Summary

- The game never opens files by name at run time. At boot, `BuildCdLsnTable` resolves **57 fixed ISO9660 paths** to start sectors (LSNs) with `sceCdSearchFile`. After that, every read is `sceCdRead(lsn + sector_offset, n_sectors, dest, mode)`.
- All reads go through two ring-buffer request queues, serviced **one request at a time** by `UpdateCdQueue`. It is called once per main-loop frame, and also from the blocking wait loops during init.
- Game data lives in two packed archives: `BIN/DATA.CDP` (index `BIN/DATA.TBL`) and `BIN/BD.CDP` (index `BIN/BD.TBL`). Code addresses archive entries **by index** only.
- There is no `sceCdSt*` streaming API use. Movies and stream audio are fed by the same queue (queue 0) in sector chunks.
- An optional **HDD install** of DATA.CDP exists (PS2 HDD, `pfs`): reads with `lsn == -1` go to the HDD module instead of the disc.

## 1. File table (`g_CdFileTable` @ 0x002BCBA8)

57 × `CdFileEntry {char *path; u32 lsn;}`. `lsn` is 0 in the ELF and filled by `BuildCdLsnTable` 0x00214508, which is called once from `InitSubsystems` (H) right after `InitCdFileSystem`. It loops `sceCdSearchFile` until each file is found (`"lsn table: %s Not Found\n"`).

| Id | Path | Id | Path |
|---|---|---|---|
| 0 | `\BIN\DATA.CDP;1` | 6 | `\SMV\INTRO.SMV;1` |
| 1 | `\BIN\BD.CDP;1` | 7–24 | `\MIS\MIS01.SMV;1` … `MIS18` |
| 2 | `\BIN\DATA.TBL;1` | 25–36 | `\SMV\SS01.SMV;1` … `SS12` |
| 3 | `\BIN\BD.TBL;1` | 37 | `\SMV\STAFF.SMV;1` |
| 4 | `\VAG\VAGBGM.VPK;1` | 38–55 | `\VAG\BRIEF01.VPK;1` … `BRIEF18` |
| 5 | `\VAG\VOICE1.VPK;1` | 56 | `\VAG\BRBGM.VPK;1` |

- `GetCdFileLsn(id)` 0x002147F0 returns `table[id].lsn`, except id 0 returns −1 when the HDD-install flag (byte `+0xE` of the HDD context 0x004BE0C0) is set.
- `FindCdFileLsnByName(path)` 0x00214738 is a `strcmp` lookup with a `sceCdSearchFile` fallback. It has **no callers**.
- A parallel table of build-host paths (`../makepack/cdpack/cd/BIN/DATA.cdp`, `…/dvd/MIS/mis01.smv`, …) at 0x002BD010 is used only by the uncalled helper 0x00214FB0 (H, dev leftover). The `cd/` vs `dvd/` prefixes show which files were planned for a CD build (`DATA`, `BD`, `voice1.vpk`) and which are DVD-only (movies, BGM, briefings).

## 2. File-system state (`g_CdFsState` @ 0x00410C68 → 0x0029F180)

Initialised by `InitCdFileSystem(state=0x0029F180, toc_buf=0x0029C980, …)` 0x002145C8: `sceCdInit(0)`, media mode from `sceCdGetDiskType` (0x12 CD → 1, 0x14 DVD → 2), `sceCdMmode`, both queues emptied, then `sceCdDiskReady` + `sceCdGetToc` (0x002146C0).

| Off | Meaning |
|---|---|
| +0x00 | TOC buffer pointer |
| +0x04 | BCD-decoded value from TOC byte 0x11 |
| +0x08 | media mode for `sceCdMmode` (1 CD, 2 DVD) |
| +0x14 / +0x18 | last **issued** ticket, queue 0 / queue 1 |
| +0x1C | ticket of the request in progress (bit 31 set → queue 1) |
| +0x20 / +0x24 | last **completed** ticket, queue 0 / queue 1 (`GetCdDoneTicket(q)`) |
| +0x28, +0x2C | −1 (unused?) |
| +0x30 | 1 = idle (pick next request), 2 = busy |
| +0x38 | abort requested |
| +0x3C | `ProcessCdRequest` state (0..4) |
| +0x40 | `sceCdRMode` {trycount, spindlctrl, datapattern} = {0, 0, 0} |
| +0x44 / +0x48 | `sceCdSync(1)` result (busy flag), previous value |
| +0x4C | last `sceCdDiskReady(1)` result |
| +0x50 | frames waited on the current read |

## 3. Request queues

Two rings of 64 × 0x1C-byte entries. Descriptors are at 0x002BCB08, 12 bytes each `{entries, read_idx, write_idx}`:

| Queue | Entries | read/write idx | Tickets | Used by |
|---|---|---|---|---|
| 0 | 0x002BBD08 | 0x002BCB0C / 0x002BCB10 | 1, 2, … (wrap at 0x7FFFFF00) | streaming: movies (0x00212630…0x00212960), stream audio (0x0021EAB8, 0x0021F448). **Serviced first** |
| 1 | 0x002BC408 | 0x002BCB18 / 0x002BCB1C | 0x80000001, … | data loads: `LoadDataEntryAsync`, TBL loads, BD.CDP → IOP, HDD install |

Request entry (0x1C bytes, written by `PushCdReadRequest` 0x00214258):

| Off | Field |
|---|---|
| 0x00 | file LSN (−1 = HDD) |
| 0x04 | destination address (EE RAM, or IOP RAM when flags bit 0) |
| 0x08 | flags: bit 0 → `sceCdReadIOPm` (H) 0x0026FFD8 instead of `sceCdRead` |
| 0x0C | `lsn == -1` (HDD request) |
| 0x10 | sector offset inside the file |
| 0x14 | sector count |
| 0x18 | ticket |

API:
- `QueueCdRead(q, lsn, flags, dest, sector_off, n_sect, chunk)` 0x00214870 splits the read into `chunk`-sector requests when `chunk > 0` and returns the last ticket. **Quirk:** the sector offset is advanced by `chunk` **twice** per iteration (0x00214938 and the branch delay slot 0x00214954). Every caller passes `chunk = -1`, so the quirk is never triggered. A port should still keep the single-request path exact.
- `GetCdDoneTicket(q)` 0x00214490. A request is complete when `done[q] >= ticket` (unsigned compare).
- `UpdateCdQueue` 0x00214350 (per frame). When idle, it picks the head of queue 0, otherwise queue 1, and calls `ProcessCdRequest(entry, q)` 0x00214A40. When busy, it continues the current request (the queue is chosen by the sign of `+0x1C`).

`ProcessCdRequest` state machine (one step per call):

| State | Action |
|---|---|
| 0 | `sceCdSync(1)`; `sceCdDiskReady(1)`; if not 6 (busy) → 1 |
| 1 | `IssueCdRead` 0x002149D0: `sceCdRead(lsn + off, n, dest, &rmode)` (or `sceCdReadIOPm`) → 2; failure → 3. HDD requests skip to 2 |
| 2 | Wait for `sceCdSync(1) == 0`. Then `done[q] = ticket`, `read_idx++`, `FlushCache(0)`, state 0, return 1. After **3600 frames** (0xE10, 60 s) without completion: `sceCdBreak` → state 3. HDD requests call the HDD read module 0x0025ACD0 instead |
| 3 | retry: wait for `sceCdDiskReady`, then → 1. HDD error → `EnterHddErrorScene` (H) 0x002616B8 = scene 7 |
| 4 | after an abort (`+0x38`): mark the request done (return −2) |

## 4. Archive access (`DATA.CDP` / `BD.CDP`)

- **TBL loading.** `DATA.TBL` (file id 2) is read in `func_00100768` (main init, before the main loop): `QueueCdRead(1, lsn(2), 0, GetTblBuffer(0), 0, 2, -1)` loads 2 sectors into slot 0 = 0x00421880. `BD.TBL` (file id 3) is read by the boot scene: 1 sector into slot 1 = 0x00422880. TBL slots are 0x1000 bytes (`GetTblBuffer(i) = 0x00421880 + i*0x1000`), so a TBL can hold at most 512 entries.
- `GetTblEntrySector(slot, i)` 0x00215468 = `u32 tbl[slot][2i]`; `GetTblEntrySize(slot, i)` 0x00215488 = `u32 tbl[slot][2i+1]`.
- `GetDataEntryAllocSize(i)` 0x002154A8 = `(size/2048 + 1) * 2048` (always at least one spare sector).
- `LoadDataEntryAsync(i, dest)` 0x002154E8 = `QueueCdRead(1, GetCdFileLsn(0), 0, dest, sector(i), size(i)/2048 + 1, -1)`; ticket stored in gp 0x004117F4. **Only one DATA load can be tracked at a time.**
- `IsDataLoadDone()` 0x00215588 = `done[1] >= ticket`, then `FlushCache(0)`.
- `AllocAndLoadDataEntry(i)` 0x00102668 = `WorkHeapAlloc(GetDataEntryAllocSize(i), 64)` + `LoadDataEntryAsync`; returns the buffer. Callers must poll `IsDataLoadDone`.
- BD.CDP (slot 1) is read only by `StreamBdEntryToIop` (H) 0x00219C80: pieces of ≤ 0x30000 bytes / 0x60 sectors, queue 1, flags 1 (destination in IOP memory), double-buffered, handed to the sound driver. **BD.CDP = sound bank data for the IOP sound driver** (H).

## 5. Who loads what (DATA.CDP indices)

| Caller | DATA index | Notes |
|---|---|---|
| `func_00100768` (main init) | 0 | count + offset table, pointers stored in `ctx->data0_table` |
| `func_00100768` | 0x166 (358) | into the buffer from `func_0021F8A8` (sound) |
| boot scene state 2 | 0x163 (355) | sound data (`func_00219840` buffer) |
| boot scene state 4 (`LoadSoundBankData` (H) 0x00219B90) | 0x164 (356) | sound bank sub-blocks |
| scene 6 (startup) sub 1, 0x00260FC0 | 0x14D (333) | splash images + message-window data |
| scene 2 `LoadFlightDataEntry` 0x0010AF10 | `id + 0x52` if `id < 0x13` and the player's aircraft id is in `g_AltMissionAircraftMask` (0x00204114), else `id + 1` | **mission data (confirmed, PCSX2 session 7)**; id = byte 0x0029F377 |
| scene 2 `func_0010AF10` | `0x65 + a`, `0x7D + a`, `0x95 + a` (a = aircraft id 0x0029F350/0x0029F360) | aircraft (H) |
| scene 2 `func_0010AF10` | `0xAD + game->unk_2B` | (H) |
| menu `func_0022C6B0` | from a 32-entry request ring | menu assets streamed one by one |
| `HddInstallDataCdp` (H) 0x0025A090 | 0x167 (359) | HDD install data, then copies 0x2B031000 bytes of DATA.CDP |

**Mission data is in DATA.CDP entries 1–100 (0x01–0x64)** (confirmed in PCSX2 session 7). The loader uses `id + 0x52` (DATA 83–100 for ids 1–18) when the mission id is below 0x13 **and** the player's aircraft id is one of 2, 4, 8, 14, 21 (constant mask 0x00204114 at gp 0x004104C8). Otherwise it uses `id + 1`. Observed with aircraft 2: campaign missions 1–4 (ids 1, 2, 3, 0x10) → DATA 83, 84, 85, 98; the opening sequence id 0x25 → DATA 38. Which code chooses: the branch at 0x0010AF48–0x0010AF70 in `LoadFlightDataEntry` (`scenes.md`). All 100 entries share one 36-sub-block layout (`docs/formats/CDP.md`). Ids above 17 are probably demo/extra mission slots: the startup scene (6) starts scene 2 with `game->unk_27 = 0x23`. The MIS*.SMV files are only the briefing videos (movie table ids 13–30).

## 6. Movie / audio streaming (overview, details in `docs/formats/SMV.md`)

- Movies: `MovieStart` (H) 0x00212150 looks up the movie table 0x002BBA10, gets the file LSN with `GetCdFileLsn`, and creates a reader (0x00214130 → 0x00212508). The reader allocates through `func_00214058`, uses a 0x600000-byte ring buffer and reads 0x2300-sector windows. Sector reads go through **queue 0**.
- Stream audio (VPK files, ids 4, 5, 38–56): 21 slot LSNs fetched in `func_00100768` (`func_0021F968(i) = GetCdFileLsn(table_0x00354680[i])`); reads into IOP memory through queue 0 (0x0021EAB8, 0x0021F448).

## 7. Port notes

- Replace the queue with synchronous or threaded file reads, but keep the ticket semantics: callers poll `IsDataLoadDone` / `GetCdDoneTicket` and may run game logic while waiting.
- Loads complete in request order, queue 0 before queue 1. Game code relies on `ctx->data0_table` being valid after init.
- Reads always cover whole sectors (`size/2048 + 1`), so destination buffers are sized by `GetDataEntryAllocSize`.
