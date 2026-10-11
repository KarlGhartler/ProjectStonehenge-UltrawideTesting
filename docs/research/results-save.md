# Results, startup and save system (priority 2, step 7)

Scope: scene 5 (mission results and debriefing), scene 6 (startup), scene 7 (HDD read error), how mission results change the campaign block, and the memory-card and HDD save layers. The save page UI is in `frontend.md`; the save image layout (magic `AC04`, 0x4B10 bytes, checksum, 4 slots) is there too.

## Scene 5: results (`SceneResults` 0x00246E18)

Input: the mission result record (gp `D_00411578`) written by the flight scene:

| Offset | Use |
|---|---|
| +0x00 | time or score base (mode 3 rank) |
| +0x08 | unit-group records, 0xD0 each |
| +0x0E | number of players |
| +0x0F | mode: 0 campaign, 1, 2 (two-player versus), 3 (trial) |
| +0x19 | mission number |
| +0x1C.. | rank thresholds (points) |

Sub-states (table 0x003B0190):

| Sub | Function | What |
|---|---|---|
| 0 | 0x0024C328 | 640×224 display; result state (0x50, gp `D_0041157C`) and per-player buffers; points (`func_00247540`); **campaign mission 18 skips the screen** (summary, then `EnterPostMissionMenu`) |
| 1 | 0x0024C700 | load DATA entry 0x14E + mission (background), or 0x161/0x162 for modes 2/3 |
| 2 | 0x0024C758 | wait, resolve container offsets |
| 3 | 0x0024C7C8 | debriefing message window, BGM 0x26 |
| 5 | 0x0024C918 | fade in |
| 4 | 0x0024C850 | `ResultsUpdate` 0x00247BD8 + draw `func_0024C278` until the exit flag |
| 6 | 0x0024CA28 | `ResultsExit`: fade out, summary, `ApplyMissionResults`, then by mode: 0 `EnterPostMissionMenu`, 1 menu 0x18, 2 menu 0x1B, 3 menu 0x19 |

`ResultsUpdate`:
- pages through kills (air, ground, ship; `func_002476F0`), points, time, objectives, reward and credits (texts in `func_0024B248`);
- rank = number of thresholds the points reach (mode 3: by time against the record table);
- reward = (points − first threshold) × 100;
- the debriefing voice plays in VAG slot 4, clip 0xAAE + mission × 2 + part (`func_002572A8`; not for mission 0x12 part 1).

`EnterPostMissionMenu` 0x00226B78: missions 1, 2, 5, 7, 8, 12, 14, 15, 17, 18 go to the story movie page 0x20 with `ctx->wRet_sub` 1–11; the others go to menu 0x0B.

### How results change the campaign block (`ApplyMissionResults` 0x00247210)

The summary (`func_00246FF0`, record at `PTR_004114D8`) holds a valid bit, a new-record bit, time, points, kills, rank, mission and mode. When valid:

- **credits** 0x002A0D30 += reward + rank bonus. The bonus is the table 0x003AFFE0 (s16, [difficulty][rank − 1]) × 100. The total is capped at 999,999,999. Campaign mode only;
- totals 0x002A0350 (u32 points, u32 kills), 0x002A0358, mission count 0x002A035C + 1;
- play counter 0x002A0360[mission (1..20, else 1)][`game[1]`] + 1;
- **mission record** (`UpdateMissionRecord` 0x002470A0) at 0x002A0448 + difficulty × 0x168 + mission × 0x14 (campaign), or 0x002A0CB8 + mission × 0x14 (trial):

  | Offset | Field |
  |---|---|
  | +0x00 | best rank (u16, max) |
  | +0x02 | best 3 times (u16, ascending) |
  | +0x08 | best 3 scores (u32, descending) |

- trial mode: rank ≥ 2 on missions 0–5 sets bit (mission) in 0x002A031C (trial unlocks, `frontend.md`), then `func_0017BE38`;
- new-record bit → `func_00226710(mission)`; then `func_002268B0(mission)` (campaign progress, `frontend.md`).

All of these addresses are inside the campaign block 0x002A0318–0x002A15D7 that the save image copies (`frontend.md`). So these rules decide the bytes of a save file.

## Scene 6: startup (`SceneStartup` 0x002613F0)

The sub-states are documented in `scenes.md`. Additions from this step:
- `func_0025FEF8` / `func_0025FF48`: the **sound banks are loaded once here** (DATA 0x165 header, then the BD banks). Ctx flag 0x400000 marks them as loaded.
- `func_00260288`: card checks. A new save needs **75** free units (`func_0023C7B0` returns 75).
- `func_002603D8`: when an HDD is usable, the HDD branch shows message 7.
- `func_0022CC50`: flag `D_00410E98` = 1 on the first-boot intro path, 0 on the title path.
- `StartupSplashUpdate` 0x00260980: exits to the intro (no save found) or the title.

## Scene 7: HDD read error (`SceneHddError` 0x00261680)

- Copies its message-window resources from the executable (0x003F8730, 0x2360 bytes), because the disc may be unreadable at that moment.
- Uses its own copy of the message-window widget (0x0024D470–0x0024EC30, the same code as the debriefing window 0x0024ED80–).
- Pad 0x10 returns to the saved scene; scene 4 re-enters menu 1.
- Reachable only with an HDD: dead in the port's default build (`dead-code.md`).

## Memory-card save layer

State object: `func_0025F298` returns 0x003F7940; init `InitSaveSystem` 0x0025F2A8 (`sceMcInit`, two port blocks).

| Offset | Use |
|---|---|
| +0x00 / +0x04 | operation step / check step |
| +0x40 + port × 0x540 | port block (ports 0 and 1, slot 0) |
| +0xAC0 | mask of registered files |
| +0xAC4 | current file index |
| +0xAC8 + i × 0xC | file table: path, buffer, size (i = 0..5) |

Port block (`func_0025E330`): +0x000 directory buffer (18 entries), +0x480 port, +0x484 slot, +0x488 card type, +0x48C free space, +0x490 format flag, +0x494 error, +0x498 result, +0x49C byte count, +0x4A0 step, +0x4A4 status, +0x4A8 directory name **`/BASLUS-20152AC04`**.

### Files (`RegisterMcSaveFile` 0x0025F340, called by the save page 0x0023EAA0)

| Index | Path on the card | Size | Buffer |
|---|---|---|---|
| 0 | `/BASLUS-20152AC04/icon.sys` | 0x3C4 | built by the save page |
| 1 | `/BASLUS-20152AC04/icon1.ico` | 0xCB98 | icon model buffer of the save page (+0x228) |
| 5 | `/BASLUS-20152AC04/BASLUS-20152AC04` | 0x4B10 | save image (`frontend.md`) |

Mask 0x23 = files 0, 1, 5. The main file has the same name as the directory (format `"%s%s"` with the directory name twice).

### Operations

Every libmc call runs as a per-frame step machine: issue the call, then poll `sceMcSync` (0x0026DD28) each frame. A step that is not done after 180 frames (300 for format, 1800 for unformat) fails with error −9999.

| Function | Operation |
|---|---|
| `CheckMcCard` 0x0025F438 | `sceMcGetInfo`. **The same result must be read 3 times in a row** before it counts (type, free space, format). Status: 7 = not a PS2 card, −11 = no card, −2 = unformatted, 1 = OK |
| `func_0025F528` | load check: `sceMcGetDir` for the directory, then for the main file |
| `func_0025F6B8` | format (only when unformatted) |
| `CreateMcSave` 0x0025F900 | requires a formatted PS2 card; the directory must not exist (else status 9); `sceMcMkdir`; then writes every registered file **in index order** (icon.sys, icon1.ico, main file): `sceMcOpen(0x203)`, `sceMcWrite`, check the byte count, `sceMcFlush`, `sceMcClose` |
| `LoadMcSaveFile` 0x0025FAF0 | check, then `sceMcOpen(1)` / `sceMcRead` / `sceMcClose` of the current file |
| `OverwriteMcSaveFile` 0x0025FC20 | check, then write the current file |
| `DeleteMcSave` 0x0025FD50 | delete each registered file, then the directory |
| `func_0025EB58`, `func_0025F170` | `sceMcUnformat`, `sceMcChdir`: no callers (HYPOTHESIS: dead) |

Consequences for the port (ADR-005, `folder-memcard.md`):
- The save folder is `BASLUS-20152AC04` with three files. Byte-identical saves come from translating the save page (image build, checksum) and the write order above; `src/sce/` implements `sceMc*` on top of the PCSX2 folder layout.
- The 3-read debounce and the frame-based timeouts are game logic: with an always-ready backend they cost exactly 3 frames per check, as on real hardware with a stable card.

## HDD save layer (unreachable without an HDD)

- Context from `func_002597B8`; worker thread fed by `func_0025E248` (`SignalSema`), dispatch `func_0025BF20` through op table 0x003F6A78 (39 ops; op 0x10 is a `return −1` stub, created as a function on 2026-10-11).
- Ops wrap iomanX calls: open/close/read/write/lseek, dopen/dread/dclose, mkdir/rmdir/remove/rename, getstat/chstat, devctl/ioctl2, mount/umount of `pfs%d:`.
- Save path: `pfs%d:/Your Saves/BISLUS-20152AC04/BISLUS-20152AC04` (directory `/Your Saves/BISLUS-20152AC04`), same file set as the card.
- Install: `func_0025A090` copies DATA.CDP to the HDD (HDD install page, `dead-code.md`).
- Open question "Does the NTSC-U build really support HDD saves?":
  - The HDD save, load and install code is complete, and the save page and install page call it when the HDD flags allow.
  - **Correction (step 11, 2026-10-11): the HDD module is never initialised in this build.**
    - `func_00259910` → `func_00259938` (thread and semaphore setup, worker `func_0025E218`) has no caller.
    - The boot scene clears ctx flag 0x80000 ("HDD present") and writes 3 to context +0x4398.
    - No game code sets 0x80000 or the "HDD usable" byte +0x4399.
  - So the HDD paths are unreachable on any hardware, not only without an HDD.
  - Per the dead-code rule the game code is still translated, and the SDK calls it needs (iomanX, pfs, threads) become logging stubs.

## Open points

- Names of result modes 1 (menu 0x18) and 3 (trial) as seen on screen.
- `func_00226710` (new-record handling) and `func_0017BE38` (trial follow-up): documented in their subsystems; confirm with a trial-mission run in PCSX2.
