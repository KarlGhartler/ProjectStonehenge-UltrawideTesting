# Results (scene 5), startup (scene 6) and HDD error (scene 7)

Mission results and debriefing, how results change the campaign block, the startup sequence with the memory-card check, and the HDD error screen. Save layers: `save.md`; save image: `../formats/SAVE.md`; scene dispatch: `../game/boot.md`.

## Scene 5: results (`SceneResults` 0x00246E18)

Input: the mission result record (gp `D_00411578` → 0x0042D2C0), created by `func_0024CBE0` in the mission setup (`func_0010FA70`) and updated by the flight scene (`func_0024CDD0` per simulation step, `func_0024D1C8` in the versus/objective rules):

| Offset | Use |
|---|---|
| +0x00 | mission time (frames) or score base (mode 3 rank) |
| +0x04 | buffer 0x0043A300 |
| +0x08 | unit-group records, 0xD0 each (0x0042D300) |
| +0x0C | 0xFFFF at creation |
| +0x0E | written as u16 = `*0x00410640` (unit count) at creation; read as a byte "number of players" by the results code (H: two fields, byte +0x0E players) |
| +0x0F | mode (set by `func_0024CBE0`): 2 two-player versus (`game[0x25]` = 2), else 3 trial (mission ids 0x15–0x19), else 1 free mission (`game[0x29]` = 2), else 0 (campaign, tutorial, demo) |
| +0x19 | mission number |
| +0x1C.. | rank thresholds (points) |

Result state (0x50 bytes, gp `D_0041157C`): +0x30 points (u16, sum of the destroyed units' score values), +0x34 reward, +0x42 rank, +0x41 mode, +0x45 new-record flag.

Sub-states (table 0x003B0190):

| Sub | Function | What |
|---|---|---|
| 0 | 0x0024C328 | 640×224 display; result state and per-player buffers; points (`func_00247540`); **campaign mission 18 skips the screen** (summary, then `EnterPostMissionMenu`) |
| 1 | 0x0024C700 | load DATA 0x14E + mission (background), or 0x161/0x162 for modes 2/3 |
| 2 | 0x0024C758 | wait, resolve container offsets |
| 3 | 0x0024C7C8 | debriefing message window (`func_0024ED80` / `func_0024EEB0`), BGM 0x26 |
| 5 | 0x0024C918 | fade in |
| 4 | 0x0024C850 | `ResultsUpdate` 0x00247BD8 + draw `func_0024C278` until the exit flag |
| 6 | 0x0024CA28 | `ResultsExit`: fade out, summary, `ApplyMissionResults`, then by mode: 0 `EnterPostMissionMenu`, 1 menu 0x18, 2 menu 0x1B, 3 menu 0x19 |

`ResultsUpdate`:
- pages through kills (air, ground, ship; `func_002476F0`), points, time, objectives, reward and credits (texts in `func_0024B248`: `MISSION %02d`, `TRIAL MISSION %02d`, `PLAYER 01`, `GRND TGT`);
- rank = number of thresholds the points reach (mode 3: by time against the record table);
- **reward = (points − first threshold) × 100** (0 when below);
- the debriefing voice plays in VAG slot 4, clip 0xAAE + mission × 2 + part (`func_002572A8`; not for mission 0x12 part 1).

### Results screen details (translated 2026-10-18, `src/game/results_scene.cpp`)

- **Result state** (0x50 bytes, `*0x0041157C`): +0 map centre, +0x10 tallies [3] (`{u32 entries, u16 score}`; entries of 6 bytes {+1 count, +4 u16 score}: 0 aircraft, 1 ground, 2 ships, 3 (+0x12) aircraft of the other player in VS; tally 0 player 1, 1 player 2), +0x14 frame layouts [4] by mode (`{rects, items, s8 rect count, s8 item count}`: 0x003AF9E0 / 0x003AFA20, 4 / 29 for modes 0, 1, 3; 0x003AFCD8 / 0x003AFD28, 5 / 28 for mode 2), +0x18 unit displays (0x160 per unit record), +0x1C aircraft list (8 × {model, count, name index (unit +3), -, u16 score}), +0x20 track stream position, +0x24 track step, +0x28 step × 6, +0x2C u16 track block, +0x2E units in the block, +0x2F aircraft list size, +0x30 u16 points, +0x34 reward, +0x38 credits (copied from 0x002A0D30), +0x3C f32 timer, +0x40 flags (1 opening, 2 closing, 4 tracks running, 0x10 tracks shown, 0x20 map shown, 0x40 view held with L2, 0x80 leaving), +0x41 mode, +0x42 rank, +0x43 shown player, +0x44 trial record slot, +0x45 new-record flag.
- **Unit display** (0x160): +0 trail (256 frames × 4 points, +0x4000 head, +0x4002 tail), +4 kill markers (0x20 each: time, size, position), +0x10 matrix, +0x50 / +0x60 track points, +0x70 position, +0x80 / +0x90 Bezier control points, +0xA0 / +0xB0 angles, +0xD0 / +0xE0 directions, +0xF0 / +0x100 up, +0x120 / +0x130 forward, +0x150 model, +0x151 group, +0x152 unit (0xFF none), +0x153 flags (1 curve, 2 hidden, 4 marker running, 0x10 in the block, 0x20 ground unit), +0x154 marker index.
- **Track record** (result record +4, written during the flight by `func_0024CDD0`, `mission-flow.md`): blocks of a 4-byte header and records up to a 0 (block end, 2 bytes) or 0xFF; record 0x10 (16 bytes: +2 unit, +3..+5 angles as fractions of a turn / 255, +6/+8/+0xA s16 position / 8, +0xC..+0xE s8 direction / 127, +0xF flag; bit 0 clears the display's trail and selects its next kill marker) and 0x11 (4 bytes, unit kinds 2 / 3: +2 unit; the results draw it as a box at its record position). One block per 10 track steps of 6 units of the mission timer (status +0x28).
- **`ResultsUpdate`** per frame (R2 held: 5 steps): opening timer (+K 0x00410404 until 5.0, then the tracks and the debriefing page start; mode 2 page = shown player, mode 3 page = record slot), closing timer (−K 0x00410408, SE 0xE at 3.00); kill markers grow (+K 0x0041040C), each kill starts its marker in the step its time falls in (SE 7) and is counted while the tracks run (bit 4); every 10 steps the next block is read and each unit placed between its two track points (Bezier `func_00246DE8`, weights from `func_00246D98`, control points at distance × ±0.3 along the directions), aircraft leave a 256-frame trail. After the last block the tracks restart from the beginning (without counting again). Square advances the debriefing text (`func_0024F158`); X skips the counting (`func_002476F0` counts everything at once) or closes the page (window hidden, voice slot 4 stopped, BGM faded over 1.0).
- **Rank, reward** (`ResultsUpdate` and `func_002476F0`): reward = (points − first threshold) × 100 or 0; rank = number of the four thresholds (record +0x1C) the points reach. Trial missions whose byte in 0x004114E7 + mission is 1 rank by time instead: rank 1, then 2 + k for every time threshold k (record +0x2C, three) not below the time; the record slot (`func_00247540`) compares the points (byte 0) or the mission time (byte 1) with the third best of the trial record.
- **Draw** (`func_0024C278`, bucket 0): background with a depth-setting sprite (`func_00249118`), frame filter, map grid 64 × 64 every 2048 units around the shown player with distance fade (`func_00249368`), ground units as boxes (`func_00249E60`), units as 6-point shapes with their trails (`func_0024A510`), kill markers as an X (`func_00249908`), the text page (`func_0024B248`, strings in .rodata/.sdata 0x00409CA0-0x00409D68 and 0x004114F0-0x00411568, aircraft names 0x002B7008, rank names 0x003AFFC8), frame outlines and item markers (`func_0024C178`), tabs, debriefing window. Map camera: `func_00246EB8` (2D camera at the map centre, rotation 0x003B01B0, distance `*0x004114E4`), matrices `func_00246C08` (far × K 0x00410400).
- **Debriefing voice**: `func_0024F0D8` starts part 1 of the window's message when VAG slot 4 is idle (`func_00257290`: slot 4 state 0x00353E7C ≠ 1 = busy); `func_002572A8(id, part)`: stream 1, clip 0xAAE + id × 2 + part on slot 4 at volume 0xFF (not part 1 of message 0x12).
- **Text page columns**: three timed columns, (timer − 2), (timer − K 0x00410430), (timer − K 0x00410434), each × 10 characters. Times: `func_0024B1E8` formats frames as `MM'SS"hh` (hundredths = (frames % 60) × 100 / 60); objective 2's hundredths get +1 when the two objective times' hundredths do not add up to the mission time's last digit.

## Post-mission menu (scene 4 subs 0x25-0x28, `src/game/post_mission_page.cpp`)

Page object 0x680 bytes at `*0x0041181C` (same layout as the campaign menu page, `frontend.md`), DATA 0xB2, menu tree of 2 columns (0x0039E220, connections 0x0039E250, parents 0x00411070; column 0 hides mask 8, or mask 3 when entered from sub 0x26, which also sets +0x66C). Help messages per item: s16 table 0x0039E268 = 0, 1, 5, 7, 2, 4; item 2 shows message 6 when +0x66C is set, else 5. Items (jump table 0x00407390): 0 apply the results (`ApplyMissionResults`, `ClampStatistics`, `AdvanceCampaignMission`) and go to menu page 0x20 (story movie; return value 6 after mission 12) or 2; 1 retry (`MenuGoTo(3, 0)`: game[0x2A] = 1, scene 2); 2 save (sub-page 0x4C / 0x4B by +0x66C; with ctx flag 0x80000 opens column 1 instead); 3 apply and go to menu page 0; 4 sub-page 0x53 / 0x52; 5 sub-page 0x4C / 0x4B.

Message window widget: init `func_0024ED80` (position 0x2500/0x3000, colours 0xB1/0x99), load `func_0024EEB0` (font + text block, GIM textures), reset `func_0024F210`, per frame `func_0024F230` (type-out timer, end flag, page advance), start message `func_0024F1B8`, draw `func_0024F698`. Shared with scene 6.

### How results change the campaign block (`ApplyMissionResults` 0x00247210)

The summary (`func_00246FF0`, record at `PTR_004114D8`):

| Offset | Field |
|---|---|
| +0x00 | bit 0 valid, bit 1 new record |
| +0x02 | mode (from the result state +0x41) |
| +0x04 | rank |
| +0x05 | mission |
| +0x06 | points (u16) |
| +0x08 | reward |
| +0x0C | mission time (result record +0) |
| +0x10 / +0x14 | kills / other kills |

When valid:
- **credits** 0x002A0D30 += reward + rank bonus. The bonus is the table 0x003AFFE0 (s16, [difficulty][rank − 1]) × 100. The total is capped at 999,999,999. Campaign mode only;
- totals: 0x002A0350 += time, 0x002A0354 += kills, 0x002A0358 += other kills, 0x002A035C (missions flown) + 1;
- usage counter 0x002A0360[aircraft − 1][colour] + 1 (aircraft `game[0]`, 1..20, else index 1; colour `game[1]`; records page "per-aircraft usage");
- **mission record** (`UpdateMissionRecord` 0x002470A0) at 0x002A0448 + difficulty × 0x168 + mission × 0x14 (campaign, missions 1..18: first record 0x002A045C), or 0x002A0CB8 + mission × 0x14 (trial):

  | Offset | Field |
  |---|---|
  | +0x00 | best rank (u16, max) |
  | +0x02 | best 3 points (u16, descending) |
  | +0x08 | best 3 times (u32, ascending) |

- trial mode: rank ≥ 2 on missions 0–5 sets bit (mission) in 0x002A031C (trial unlocks), then `func_0017BE38` (best-run track, below);
- new-record bit → `func_00226710(mission)`: for missions 1–18 sets the aircraft linked to the mission (u16 at 0x0038484A + (mission − 1) × 4) to state 2 = buyable (`func_00226580`); then `func_002268B0(mission)` (campaign progress unlocks, `frontend.md`).

**Trial best-run track** (mission kind 1, trial ids 0x15–0x19). `func_0017BC70` at mission start selects the stored best track for the trial (`*0x00410B38` + (id − 0x15) × 0x200; 128 samples, 32 for 0x19) and clears the current track (`*0x00410B3C`). Each kill by the player appends a sample {mission timer / 2, player score (unit +0x5C)} (`func_0017BD68`, called from `UpdateUnitStates`). The trial HUD panel `func_00167758` reads the best track at the current time (`func_0017BDD0`) to draw the comparison bar. At the results `func_0017BE38` keeps the better run: for ids 0x15–0x17 the higher final score, for 0x18–0x19 the lower final time; if the current run is better it is copied over the best track.

**Results title by mode** (`func_0024B248`, state +0x41 = mode): mode 3 shows `TRIAL MISSION nn` and `POINTS` as points / target (mission 4: 2400, mission 5: 750, otherwise the result record's threshold); modes 1 and 2 show `MISSION nn` and `POINTS` points / threshold. In modes 1–3 `REWARD` and `TOTAL CREDITS` show `----`. Mode 0 (campaign) shows `MISSION nn`, `POINTS`, the reward amount, the rank and the rank bonus (table 0x003AFFE0 by difficulty × 100). Modes 2 and 3 also select the results backgrounds DATA 0x161 / 0x162 (`func_0024C700`). There is no on-screen name for the mode itself.

All of these addresses are inside the campaign block 0x002A0318–0x002A15D7 that the save image copies, so these rules decide the bytes of a save file.

## Scene 6: startup (`SceneStartup` 0x002613F0)

The handler is `table_0x003F8708[ctx->sub]()`. The scene work struct is `w = *0x00411714` (0x210 bytes, work heap): `w+0x1F0` flag word, `w+0x1F4` frame counter. A message window object `*0x00411900` shows numbered messages (`func_0024F1B8(win, msg)`).

| Sub | Address | Action |
|---|---|---|
| 0 | 0x00260E80 | display setup (640×448 draw, 640×224 display, PSMZ16S), allocates `w`, two 0x28-byte texture slots (`w+0x1E0`/`+0x1E4`) and the message window, 0x40000-byte double buffer, audio calls; `ctx->flags |= 0x200000` → 1 |
| 1 | 0x00260FC0 | `w[0] = AllocAndLoadDataEntry(0x14D)` (DATA 333) → 2 |
| 2 | 0x00260FE8 | when loaded: splits entry 333 into its 4 sub-blocks: 2 splash textures (`w[0x78]`, `w[0x79]`) + message-window resources → 3 |
| 3 | 0x00261100 | `func_001026B8(w+0x10)` (camera init) → 4 |
| 4 | 0x00261128 | `func_00260078()`: if any of `w+0x1F0` bits 0x100000/0x200000/0x400000/0x800000 is set, shows startup warning 5/6/10/7 and waits for a button (`ctx->pad_buttons[1] & 0x70`); returns 1 when done → 5. Draws the window every frame |
| 5 | 0x002611A0 | `w+0x1FE = 0`, `w+0x1E8 = 0`, `w+0x1F4 = 120`, clears bit 0x100 → 6 |
| 6 | 0x002611D8 | memory-card probe: `func_0025F298()` = save-module state 0x003F7940; `func_0025F528(save, 0)` (load check of slot 0) or `CheckMcCard(save, 0)` 0x0025F438 (status, 0x540 bytes per port block). Sets `w+0x1F0` bits 0x80/0x100/0x200/0x10000 from the result; when the probe is done (bit 0x200) and the counter is at least 120 → 7. Sub 5 presets the counter to 120, so there is no extra wait (the 120 only matters when sub 7 returns to sub 6 after a card was inserted). Card S: two card-info checks and two `sceMcGetDir`; card E / no card: one card-info check (PCSX2 run 06, `../pcsx2/sessions.md` run 06) |
| 7 | 0x00261378 | `func_00260488()` + draw `func_00260900()`. `func_00260488` evaluates the card result (`func_00260288`: card type `save+0x4C8`, error `save+0x4D4` = −0xB / −2, free space `save+0x4CC` vs `func_0023C7B0()` = **75**; HDD context +0x10, +0x216, +0x4399 via `func_002603D8`), shows messages 1–10 and 0x10, handles confirm (pad 0x50) / cancel (pad 0x20), fades the window. → 8 when done (→ 6 to retry the probe) |
| 8 | 0x00261398 | `w+0x1FD = 0` (image 0), `w+0x1F4 = 0`, `w+0x1F0 |= 1` (start fade), `func_0025FEF8()` (sound bank load request) → 9 |
| 9 | 0x002613D0 | per frame: `StartupSplashUpdate` 0x00260980 (fade/hold logic) + `func_00260B90()` (draw one GS sprite) |

**Sound banks** are loaded once per boot here: `func_0025FEF8` requests the load (flag 0x2000 when `ctx->flags & 0x400000`, else 0xC00 + buffer 0x004C35C0); `func_0025FF48` loads DATA 0x165 (`LoadSoundBankData` (H)) and then streams BD entry 1 into bank slot 10 (`StreamBdEntryToIop` (H)), and sets ctx flag 0x400000 (`sound.md`).

Splash logic (`StartupSplashUpdate`, one image at a time, image index `w+0x1FD`, fade factor float `w+0x1EC`):
- bit 1: fade 60 frames (`1 − i/60`) → bit 2;
- bit 2: hold up to 120 frames (image 1 skippable with pad 0x50); continues only when bit 0x2000 is set → bit 4;
- bit 4: fade 60 frames (`i/60`), then exit.

Exits after the fade:
- after image 0, when `w+0x1F0 & 0x4000` is clear: sets `g_GameState` `+0x25 = 1`, `+0x27 = 0x23`, `[0] = 0x11`, `[1] = [3] = +0x2A = 0`, then `func_0022CC50(0)` and tail call `RequestScene(2)` at 0x00260B54: **no-save intro** in the flight scene;
- after image 0, when bit 0x4000 is set: shows image 1;
- after image 1: `func_0022CC50(1)`, then tail call `EnterMenu(0)` at 0x00260B78: **title screen**, menu page 0.

Both exits are tail calls, and `EnterMenu` also tail-calls `RequestScene`, so `ra` at the store in `RequestScene` is the return address of the `jal 0x00260980` in sub 9 (0x002613D8 → `ra` 0x002613E0) for both. Bit 0x4000 is set by `func_00260288` when `save+0x4D8 != 0` or HDD context +0x216 == 1 (HDD path dead). `save+0x4D8` is port block +0x498, written by `func_0025E5E8` with the `sceMcGetDir` entry count: sub 6 (`func_0025F528`) first lists `/BASLUS-20152AC04` (format `"%s"`, 0x00411710) and, if found, `/BASLUS-20152AC04/BASLUS-20152AC04` (`"%s%s"`, 0x00411700); if the directory exists without the save file, +0x498 is set to −1 (non-zero: title path, message 0x10). So the intro is **not tied to the first boot**: it plays at **every boot** where card slot 1 has no `BASLUS-20152AC04` directory (no card, unformatted card, or no AC04 save), and never when the directory exists. The probe runs once per boot (scene 6); saving later in the session does not replay it. PCSX2 (sessions 01 and 3) confirms the choice follows the card contents: no AC04 save → intro, save present → title, also when the card is inserted after the "Cannot save without memory card" message.

`func_002603D8` (HDD branch of the check: HDD usable → message 7, window page 8) and the warning flags 0x100000..0x800000 (`func_00260078`) belong to the same evaluation. `func_00260058` (clear the warning flags) is dead.

Messages seen in PCSX2 run 06 (no card in slot 1): page 1 "Cannot save without memory card (PS2) in MEMORY CARD slot 1 (75KB necessary)" (`GetSaveSizeKb` 0x0023C7B0 = 75), then after a confirm page 3 "Start game anyway? X Button: YES / Δ Button: NO"; confirming fades the window out (2 frames) and goes to sub 8. A formatted card without the AC04 directory and an unformatted card show no message. Translated in slice S1 (`src/game/startup.cpp`, `save_mc.cpp`, `message_window.cpp`); function names in `../symbols.csv`.

## Scene 7: HDD read error (`SceneHddError` 0x00261680)

- Entered only from `EnterHddErrorScene` (H) 0x002616B8, which `ProcessCdRequest` 0x00214A40 calls when a request flagged as HDD (`lsn == −1`, DATA.CDP installed to the HDD) fails. It saves the current scene in `*0x00411718` and calls `RequestScene(7)`. Unreachable in this build (`save.md`).
- Sub 0 (0x00261428): stops audio, copies its message-window resources from the executable (0x003F8730, 0x2360 bytes, because the disc may be unreadable), allocates the window with its own copy of the widget (0x0024D470–0x0024EC30, same code as the debriefing window `func_0024ED80`…).
- Sub 1 (0x00261530): first frame 640×224 display setup; draws the window and waits for pad bit 0x10 (`ctx+0x2B4`), then `RequestScene(saved)` (0x00261650 / 0x00261660), and `EnterMenu(1)` when the saved scene was 4.

## Open points

None.
