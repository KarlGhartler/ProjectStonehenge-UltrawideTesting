# Save system: memory-card and HDD layers

The memory-card layer (used) and the HDD layer (complete but unreachable in this build). The save page UI is in `frontend.md`; the file contents in `../formats/SAVE.md`; the PCSX2 folder layout the port uses in `../formats/folder-memcard.md` (ADR-005).

## Memory-card layer

State object: `func_0025F298` returns 0x003F7940; init `InitSaveSystem` 0x0025F2A8 (`sceMcInit`, two port blocks).

| Offset | Use |
|---|---|
| +0x00 / +0x04 | operation step / check step |
| +0x40 + port × 0x540 | port block (ports 0 and 1, slot 0) |
| +0xAC0 | mask of registered files |
| +0xAC4 | current file index |
| +0xAC8 + i × 0xC | file table: path, buffer, size (i = 0..5) |

Port block (`func_0025E330`): +0x000 directory buffer (18 entries), +0x480 port, +0x484 slot, +0x488 card type, +0x48C free space, +0x490 format flag, +0x494 error, +0x498 result, +0x49C byte count, +0x4A0 step, +0x4A4 status, +0x4A8 directory name **`/BASLUS-20152AC04`**. Seen from the save object these are `save+0x4C8` (type), `+0x4CC` (free space), `+0x4D4` (error), `+0x4D8` (H: AC04 save found) for port 0.

### Files (`RegisterMcSaveFile` 0x0025F340, called by the save page 0x0023EAA0)

| Index | Path on the card | Size | Buffer |
|---|---|---|---|
| 0 | `/BASLUS-20152AC04/icon.sys` | 0x3C4 | built by the save page |
| 1 | `/BASLUS-20152AC04/icon1.ico` | 0xCB98 | icon model buffer of the save page (+0x228) |
| 5 | `/BASLUS-20152AC04/BASLUS-20152AC04` | 0x4B10 | save image (`../formats/SAVE.md`) |

Mask 0x23 = files 0, 1, 5. The main file has the same name as the directory (format `"%s%s"` with the directory name twice).

### Operations

Every libmc call runs as a per-frame step machine: issue the call, then poll `sceMcSync` 0x0026DD28 each frame. A step that is not done after 180 frames (300 for format, 1800 for unformat) fails with error −9999.

| Function | Operation |
|---|---|
| `CheckMcCard` 0x0025F438 | `sceMcGetInfo`. **The same result must be read 3 times in a row** before it counts (type, free space, format). Status: 7 = not a PS2 card, −11 = no card, −2 = unformatted, 1 = OK |
| `func_0025F528` | load check: `sceMcGetDir` for the directory, then for the main file |
| `func_0025F6B8` | format (only when unformatted) |
| `CreateMcSave` 0x0025F900 | requires a formatted PS2 card; the directory must not exist (else status 9); `sceMcMkdir` 0x0026D7C0; then writes every registered file **in index order** (icon.sys, icon1.ico, main file): `sceMcOpen(0x203)`, `sceMcWrite`, byte-count check, `sceMcFlush`, `sceMcClose` |
| `LoadMcSaveFile` 0x0025FAF0 | check, then `sceMcOpen(1)` / `sceMcRead` / `sceMcClose` of the current file |
| `OverwriteMcSaveFile` 0x0025FC20 | check, then write the current file |
| `DeleteMcSave` 0x0025FD50 | delete each registered file, then the directory |
| `func_0025EB58` (`sceMcUnformat` step machine), `func_0025F170` (`sceMcChdir` step machine), `func_0025F820` (unformat operation) | no callers (dead) |

A new save needs **75** free units (`func_0023C7B0`, checked by scene 6, `results-startup.md`).

Consequences for the port (ADR-005):
- The save folder is `BASLUS-20152AC04` with three files. Byte-identical saves come from translating the save page (image build, checksum) and the write order above; `src/sce/` implements `sceMc*` on top of the PCSX2 folder layout.
- Translated in slice S2b: the memory-card layer in `src/game/save_mc.cpp` (`ReadMcFile` … `DeleteMcSave`, globals 0x004118D0–0x004118F4), the save page in `src/game/save_page.cpp`; the libmc write path on the folder card is described in `../formats/folder-memcard.md` §10.1. Main-menu LOAD (sub-page 0x4F) works in the port; save mode is reached only from sub-pages 0x4A–0x4E (later slices), so the byte-identical save check is part of S4.
- The 3-read debounce and the frame-based timeouts are game logic: with an always-ready backend they cost exactly 3 frames per check, as on real hardware with a stable card.

## HDD layer (unreachable in this build)

The HDD save, load and install code is complete, and the save page and the HDD install page call it when the HDD flags allow. The module is **never initialised**:
- `func_00259910` → `InitHddModule` (H) 0x00259938 (thread and semaphore setup, worker thread `func_0025E218` → `func_0025C090` → job worker `func_0025BF70`, power-off handler `func_0025C108`) has no caller;
- the boot scene clears ctx flag 0x80000 ("HDD present") and writes 3 to HDD context +0x4398; no game code sets 0x80000 or the "HDD usable" byte +0x4399.

So the HDD paths are unreachable on any hardware. The game code is still translated (`../port/dead-code.md`), and the SDK calls it needs (iomanX, pfs, threads) become logging stubs.

Structure:
- Context 0x004BE0C0 (`func_002597B8`): +0xE HDD-install flag (makes `GetCdFileLsn(0)` return −1), +0x10, +0x214/+0x215 (HDD present / partition mounted), +0x216, +0x4364 request id, +0x437C–+0x4390 size and error words, +0x4394/+0x4396 install progress, +0x4398 HDD mode for saves, +0x4399 HDD usable.
- Worker requests: `func_0025E248` posts `(op, args)` and `SignalSema`s the worker; a second call polls completion (`func_0025E1F0`). Dispatch `func_0025BF20` through op table 0x003F6A78 (39 ops; op 0x10 at 0x0025CF08 is a `return −1` stub).
- Ops wrap iomanX calls: open/close/read/write/lseek, dopen/dread/dclose, mkdir/rmdir/remove/rename, getstat/chstat, devctl/ioctl2, mount/umount of `pfs%d:`. Op names: table 0x003F6B18 (`HDD0_CREATE_PARTITION`, …).
- Save path: `pfs%d:/Your Saves/BISLUS-20152AC04/BISLUS-20152AC04` (directory `/Your Saves/BISLUS-20152AC04`), same file set as the card. Operations: check 0x0025AEE0, new save 0x0025B0D8, delete 0x0025B6C8, overwrite 0x0025B948, load 0x0025BBD0, "Your Saves" query 0x0025BED8.
- Install: `HddInstallDataCdp` (H) 0x0025A090 copies DATA.CDP (0x2B031000 bytes, plus DATA 0x167 = `PS2I` icon data) to the HDD in 10 MiB steps (HDD install page, `frontend.md`).
- HDD read path: `func_00215070` (sends IOP-destined data to IOP 0x07800000) and scene 7 on errors (`results-startup.md`).
