# PCSX2 Folder Memory Card layout

Purpose: the port stores saves so that a save folder can be copied between the port's `saves\` folder (`%LOCALAPPDATA%\AC04Port\saves\`, ADR-005) and a PCSX2 Folder Memory Card, in both directions (ADR-005).

Source: PCSX2 source code, `pcsx2/SIO/Memcard/MemoryCardFolder.h` and `.cpp` (GPL-3.0+, PCSX2 Dev Team), `master` branch, read on 2026-10-10.
- Read for compatibility only. **No code is copied.** The port writes its own implementation from this description.
- Confidence: **source reading, not yet tested.** Every point below must be checked against the `v2.8.2` tag and a real round trip before the save layer is built. Open items are marked (H).

## 1. What a folder card is

A Folder Memory Card is a host folder (e.g. `memcards/Mcd001.ps2/`) that PCSX2 presents to the game as an 8 MB PS2 memory card.
- **PS2 directory → host folder, PS2 file → host file**, with the file's raw bytes.
- PCSX2 builds the PS2 file-system structures in memory when the card is loaded: superblock contents, FAT, indirect FAT and root directory. Only a few metadata files are stored on the host.

```
<card folder>/
  _pcsx2_superblock                 card root: raw superblock (binary)
  BASLUS-20152AC04/                 one host folder per PS2 save directory (AC04: CLAUDE.md, InitSaveSystem)
    _pcsx2_index                    YAML: file order + timestamps
    _pcsx2_meta_directory           optional: raw 512-byte directory entry
    _pcsx2_meta/                    optional folder: one raw 512-byte entry per file with non-default metadata
      <file name>
    <save files, raw bytes>         e.g. icon.sys, the icon file(s), the game's data file(s)
```

## 2. Card geometry (fixed)

| Item | Value |
|---|---|
| Page | 0x200 data + 0x10 ECC |
| Cluster | 0x400 (2 pages) |
| Block (erase unit) | 0x2000 (8 clusters) |
| Card | 0x4000 pages, 0x2000 clusters, 0x400 blocks = 8 MiB data (0x840000 raw with ECC) |

PCSX2 says sizes other than 8 MB are untested. The port only needs the PS2-visible behaviour of an 8 MB card (free space, sceMcGetInfo).

## 3. Host metadata files

All names start with `_pcsx2_`. PCSX2 skips every host entry with that prefix when it enumerates the card.

| File | Where | Content | When written |
|---|---|---|---|
| `_pcsx2_superblock` | card root | raw superblock bytes (layout §4). The card counts as formatted when byte 0x16 = 0x6F (H: inside the magic string) | always. **Without it PCSX2 does not load the folder as a card** |
| `_pcsx2_index` | each directory | YAML, one key per file (the cleaned host name), with `order`, `timeCreated`, `timeModified` (integers). The directory's own times are under the key `$ROOT` (legacy `%ROOT` is accepted and renamed) | when files are written |
| `_pcsx2_meta_directory` | inside the directory it describes | raw 0x200-byte directory entry (§5) | only if the name had to be cleaned, the mode is not the default, or `attr` ≠ 0 |
| `_pcsx2_meta/<file>` | folder inside the parent directory, one file per entry | raw 0x200-byte file entry (§5) | same condition, per file |
| `_pcsx2_deleted_<name>` | parent directory | a deleted entry. PCSX2 renames it instead of removing it | on delete |
| `_pcsx2_index.invalid.bak` | directory | backup of an index that failed to parse | on repair |

### Defaults when metadata is missing

This covers a save folder copied in by hand:
- the PS2 name is the host name;
- mode is the default (§5);
- times come from `_pcsx2_index`, or from the host file times if there is no index entry;
- files with no `order` get negative "legacy" order values, so they sort by host enumeration order;
- directories are listed before files;
- new files get the highest `order` + 1.

## 4. Superblock (packed, little-endian)

| Off | Size | Field |
|---|---|---|
| 0x00 | 28 | magic (Sony string "Sony PS2 Memory Card Format ", H) |
| 0x1C | 12 | version (H: "1.2.0.0") |
| 0x28 | u16 | page_len |
| 0x2A | u16 | pages_per_cluster |
| 0x2C | u16 | pages_per_block |
| 0x2E | u16 | unused |
| 0x30 | u32 | clusters_per_card |
| 0x34 | u32 | alloc_offset |
| 0x38 | u32 | alloc_end |
| 0x3C | u32 | rootdir_cluster |
| 0x40 | u32 | backup_block1 |
| 0x44 | u32 | backup_block2 |
| 0x48 | 8 | padding |
| 0x50 | u32[32] | ifc_list (indirect FAT clusters; PCSX2 uses 1) |
| 0xD0 | u32[32] | bad_block_list |
| 0x150 | u8 | card_type |
| 0x151 | u8 | card_flags |

The port can write a fixed superblock taken from a freshly formatted PCSX2 folder card. The exact bytes are to be captured from a real card created by PCSX2 2.8.2 (H).

## 5. Directory/file entry (0x200 bytes, as on a real card)

| Off | Size | Field |
|---|---|---|
| 0x00 | u32 | mode: 0x0001 read, 0x0002 write, 0x0004 execute, 0x0008 copy-protected, 0x0010 file, 0x0020 directory, 0x0800 PocketStation, 0x1000 PS1, 0x8000 used |
| 0x04 | u32 | length (bytes for a file, entry count for a directory) |
| 0x08 | 8 | created time |
| 0x10 | u32 | first cluster (0xFFFFFFFF for an empty file) |
| 0x14 | u32 | dir entry (parent index, in "." entries) |
| 0x18 | 8 | modified time |
| 0x20 | u32 | attr |
| 0x24 | 0x1C | padding |
| 0x40 | 32 | name (NUL-terminated) |
| 0x60 | 0x1A0 | unused |

Time (8 bytes): `{u8 unused, u8 second, u8 minute, u8 hour, u8 day, u8 month, u16 year}`.
- PCSX2 converts to and from host time as **UTC** (`gmtime`/`timegm`).
- On real hardware the libmc times are Japan time (JST). PCSX2 does not shift them (H).

Defaults:
- directory = read | write | execute | directory | 0x0400 | used (0x8427);
- file = read | write | execute | file | 0x0080 | 0x0400 | used (0x8497).

## 6. Name mapping

PS2 names are up to 32 bytes. On the host, PCSX2 replaces the following with `_`, and then writes `_pcsx2_meta*` so the original name is restored:
- `\ % : | " < >`;
- a trailing space or dot.

AC04's directory name `BASLUS-20152AC04` needs no cleaning. Its files (confirmed from code, `../subsystems/save.md`) are `icon.sys` (0x3C4 bytes), `icon1.ico` (0xCB98) and `BASLUS-20152AC04` (0x4B10, the save image), written in that order by `CreateMcSave`. None of the names needs cleaning.

## 7. Filter

When "filter by game" is enabled, PCSX2 loads only top-level directories whose name contains one of the filter tokens.
- The tokens are the game serial plus the fixed `DATA-SYSTEM` and `BWNETCNF` (network config).
- AC04's directory contains `SLUS-20152`, so it is loaded.
- Files directly in the card root are never loaded.

## 8. What the port does (design notes for the save layer, not decided code)

- `saves\` (under `%LOCALAPPDATA%\AC04Port\`, ADR-005) = the port's folder card root. It holds one host folder per PS2 directory and a `_pcsx2_superblock`, so the whole `saves` folder can also be used directly as a PCSX2 folder card. Copying single save folders works in both directions.
- `sceMc*` (written in `src/sce/`) map onto host file operations. The raw save files are written byte for byte as the game passes them to `sceMcWrite`. The port writes `_pcsx2_index` with `order` and times so PCSX2 shows the same order. It writes `_pcsx2_meta*` only in the cases PCSX2 would.
- `sceMcGetDir` results (sizes, times, attributes) are synthesised from the host files plus the metadata, exactly as PCSX2 would present them.
- Memory card port 1 vs. 2: H. The port could use `saves` as port 1 only and report port 2 as empty. Owner decision when the save layer is designed.

## 9. Observed in a real folder card (owner's example, 2026-10-11)

Card `AceCombat4Saves_converted.ps2` (path in `CLAUDE.local.md`), converted by PCSX2 from a file card and holding one AC04 save. Only structure and field values are recorded here; no bytes are copied into the repo.

Files:

| Path | Size | Notes |
|---|---|---|
| `_pcsx2_superblock` | **0x2000** (one erase block) | not 0x200: PCSX2 keeps the first whole block |
| `BASLUS-20152AC04/icon.sys` | 0x3C4 | matches §6 |
| `BASLUS-20152AC04/icon1.ico` | 0xCB98 | matches §6 |
| `BASLUS-20152AC04/BASLUS-20152AC04` | 0x4B10 | save image (`SAVE.md`) |
| `BASLUS-20152AC04/_pcsx2_index` | 278 bytes | below |

No `_pcsx2_meta*` files: every entry uses the default modes and the names need no cleaning, as predicted in §6.

`_pcsx2_index` is **YAML flow style on one line**, no spaces after commas, keys unquoted:
`{$ROOT: {timeCreated: T,timeModified: T},icon.sys: {order: 1,timeCreated: T,timeModified: T},icon1.ico: {order: 2,...},BASLUS-20152AC04: {order: 3,...}}`
- Times are **Unix seconds (UTC)** as integers.
- `order` 1/2/3 follows the write order of `CreateMcSave` (icon.sys, icon1.ico, save image).
- `$ROOT.timeModified` equals the save image's times (last file written). (H: PCSX2 updates `$ROOT` on every write in the directory.)

Superblock field values (§4 layout):

| Field | Value |
|---|---|
| magic + version | "Sony PS2 Memory Card Format " + "1.2.0.0" |
| page_len / pages_per_cluster / pages_per_block | 0x200 / 2 / 16 |
| unused (0x2E) | 0xFF00 |
| clusters_per_card | 0x2000 |
| alloc_offset / alloc_end | 0x29 / 0x1FC7 |
| rootdir_cluster | 0 |
| backup_block1 / backup_block2 | 0x3FF / 0x3FE |
| ifc_list[0] | 8 (rest 0) |
| bad_block_list | all 0xFFFFFFFF |
| card_type / card_flags | 2 / 0x2B |

Bytes 0x154–0x1FFF are not zero (rest of the original card's first block). Whether PCSX2 accepts a superblock file that holds only the 0x154-byte header plus zero padding is untested (H); until then the port writes a full 0x2000-byte file with the field values above and zeros elsewhere, and the round-trip test (open point 2) confirms it.

## Open points

1. Verify against tag `v2.8.2`: file names and YAML format are confirmed by the example card (§9); whether a zero-padded superblock is accepted is still open.
2. Round trip test: create a save in PCSX2 on a folder card, copy the folder into the port, load it; and the reverse.
3. Which times the game itself sets (`sceMcSetFileInfo`?) and whether the port must reproduce JST/UTC handling.
