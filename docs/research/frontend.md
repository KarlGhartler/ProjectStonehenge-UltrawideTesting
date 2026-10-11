# Front end (scene 4): menus, hangar, briefing, options, extras

Priority 2, step 1. The front end is the code module 0x00226198–0x00246E17 plus helpers in other modules. Per-function status: `coverage-functions.csv` (subsystem `frontend`). Names are Ghidra names; `(H)` = hypothesis.

## 1. Scene handler and navigation

`SceneMenu` 0x002262D0: `table_0x00384640[ctx->sub]()`, then the menu fade overlay (`0x00226CD0`) when active, then `UpdateMenuBgm`.

| Sub | Handler | Role |
|---|---|---|
| 0 | `MenuEnterPage` 0x002261B8 | 0x40000-byte double buffer, `SetMenuDisplayMode(640, 448)`, fade reset, `RequestSubState(page_to_sub[g_MenuPage])` (table 0x003845F8, 35 pages); BGM reset when coming from another scene |
| 1 | `MenuEnterSubPage` 0x00226248 | draws the fade frame, `RequestSubState(g_MenuSubPage)` (< 0x5A, else 2) |

How pages are entered: `EnterMenu(page)` (restart scene 4 at sub 0), `SetMenuSubPage(sub)` (sub 1, then that sub-state), `MenuGoTo(kind, arg)` (0 page, 1 sub-page, 2/3 start the flight scene with `game[0x2A]` = 0/1).

Page → first sub-state (0x003845F8):

| Page | Sub | Page | Sub | Page | Sub | Page | Sub |
|---|---|---|---|---|---|---|---|
| 0 | 2 | 9 | 30 | 18 | 45 | 27 | 68 |
| 1 | 3 | 10 | 31 | 19 | 49 | 28 | 69 |
| 2 | 14 | 11 | 37 | 20 | 52 | 29 | 70 |
| 3 | 17 | 12 | 38 | 21 | 55 | 30 | 71 |
| 4 | 18 | 13 | 32 | 22 | 58 | 31 | 7 |
| 5 | 23 | 14 | 33 | 23 | 62 | 32 | 9 |
| 6 | 27 | 15 | 34 | 24 | 65 | 33 | 10 |
| 7 | 28 | 16 | 35 | 25 | 66 | 34 | 8 |
| 8 | 29 | 17 | 41 | 26 | 67 | | |

Sub-states 74–89 (0x0023E9F0…) are reached only through `SetMenuSubPage`.

## 2. Title screen and main menu (pages 0/1, subs 2–6)

| Sub | Handler | Role |
|---|---|---|
| 2–4 | 0x0022DE00 | `CreateMainMenu` (0x6A0-byte object), queue DATA 0xB0 (2 sub-blocks: title textures) → 5 |
| 5 | 0x0022DEA0 | wait for the load, register textures (0x00251D28), message box → 6 |
| 6 | 0x0022DF00 | `UpdateFade`, `MainMenuUpdate`, `DrawMainMenu` |

`MainMenuUpdate` state (`+0x67C`):
- 0: wait for the fade-in.
- 2: title idle. Start/X opens the menu. After **19 s** of idle the game runs `StartTitleDemo`.
- 3/4/5: transitions.
- 6: menu tree active. Circle selects (`MainMenuSelect`), Triangle goes back (0x0022D078).
- 7/8: fade out, then `MenuGoTo(+0x680, +0x684)`.

`MainMenuSelect` item ids (selected id = `0x0022AFB8`):

| Id | Action |
|---|---|
| 0, 1, 3, 4, 8 | open a sub-column (3 only when ctx flag 0x80000 is set) |
| 2 | page 2 (mission title) after `ApplyCampaignSelection`: continue the campaign |
| 5, 6 | sub-pages 0x42 / 0x43, with `game[0x29] = 2` (free mission mode) |
| 7 | sub-page 0x44, default aircraft (2/0/0) |
| 9–14 | difficulty 0–5 into 0x002A15BA, `g_MissionNo = 1`, campaign data reset (`0x00105418`), page 0x20 (opening story movie): **new game** |
| 15–20 | the same, but without the data reset: **new game keeping unlocks** (H) |
| 0x15, 0x16, 0x1D–0x23 | sub-pages 0x56, 0x4F, 0x2D, 0x29, 0x3A, 0x37, 0x34, 0x31, 0x3E (options, records, extras, save/load: section 4) |
| 0x17–0x1C | sub-page 0x41 with `game[0x28]` = 0–5 |

The visibility of menu items depends on the progress flags 0x002A0318:
- bit 0: campaign cleared once;
- bits 1, 2: cleared on higher difficulties;
- bit 3: cleared twice.

`MainMenuSetup` builds the item masks from these flags.

**Attract demo** (`StartTitleDemo` 0x0022CC68) cycles 0x00410E98 through 6 entries. Four of them start a flight:

| Entry | Mission id (game[0x27]) | Aircraft (game[0]) | game[3] | Mode (game[0x29]) |
|---|---|---|---|---|
| default (0, 2, 4) | 0x23 | campaign selection, or 0x11 if not cleared | | 1 |
| 1 | 3 | 0x10 | 1 | 4 (demo), game[0x2B] = 0 |
| 3 | 0x10 | 4 | 0 | 4, game[0x2B] = 1 |
| 5 | 5 | 3 | 1 | 4, game[0x2B] = 2 |

## 3. Campaign state used by the menus

| Address | Meaning | Evidence |
|---|---|---|
| `g_MissionNo` 0x002A15B8 | campaign mission number 1–18 | `AdvanceCampaignMission` |
| 0x002A15BA | difficulty 0–5 | `MainMenuSelect` 9–14 |
| 0x002A15BE / C0 / C2 | selected aircraft (1–21), colour (0–2), special weapon (0–3) | `ApplyCampaignSelection`, 0x00226A00 |
| 0x002A0318 | progress flags (cleared bits) | `MarkCampaignCleared` |
| 0x002A0320 | aircraft availability: 2 bits per aircraft, per colour (+ weapon words at +3) | 0x00226A00 |
| `g_GameState` bytes | `[0]` aircraft, `[1]` colour, `[3]` weapon, `[0x10..0x13]` copies, `[0x25]` = 1, `[0x27]` mission id, `[0x28]` difficulty, `[0x29]` mode (1 campaign, 2 free mission, 4 demo), `[0x2A]` flight-entry kind, `[0x2B]` demo index | the functions above |

- `AdvanceCampaignMission`: after mission 18 it wraps to 0 and calls `MarkCampaignCleared`, which sets the cleared bits and unlocks aircraft (0x00226968, 0x00226748).

## 4. Shared menu widgets (0x00226D00–0x0022C600)

- **List widget** (stride 0x18):
  - open/close commands (0x002271B8);
  - type-in effect while opening (SE 9);
  - cursor with wrap (pad bits 1/2, SE 1);
  - per-item highlight scale.
- **Menu tree** (0x0022ABC0 init, 0x0022AF60 update, 0x0022AFF0 draw): up to 8 columns of list widgets, hidden-item masks, connector frames.
- **Crosshair cursor** with a 10-position trail (0x002298F8–0x00229BC0).
- **Panel animation** (0x0022B1A8 family).
- **Message box**:
  - text ids from the text system (0x00251xxx–0x00252xxx);
  - up to 3 lines (0x0022B8B8);
  - typewriter effect (0x0022B9A0);
  - coloured characters, codes 8–11 (0x0022BA68).
- **Fade object**: `StartFade` / `UpdateFade`, cosine ease; flag 4 = fade-in done, flag 8 = fade-out done.
- **Data load queue**:
  - `QueueMenuDataLoad` / `UpdateMenuDataLoad`: a 32-entry ring of `{DATA entry, sub-block count, pointer table}`;
  - loads into the work heap and splits the entry into its sub-blocks.
- **Menu BGM**:
  - `PlayMenuBgm` crossfades between stream slots 0 and 1;
  - `UpdateMenuBgm` starts the track when the slot is idle;
  - `FadeOutMenuBgm` fades the slots out.
- Full-screen overlay passes:
  - 0x00227F30 / 0x002281E0: scanline/noise copy of the frame, using `frame_count`;
  - letterbox bars (0x0022A558).

## 5. Pages

Each page follows the same pattern:
- an **enter** sub-state allocates the page object from the work heap and queues its DATA entries (`QueueMenuDataLoad`);
- a **load** sub-state waits (`UpdateMenuDataLoad`) and registers the textures (0x00251D28);
- a **run** sub-state calls `UpdateFade`, the page update and the page draw.
- Pages leave with `MenuGoTo(kind, arg)`. Most pages store their return target in the object.

| Page(s) / sub-pages | Subs | Enter / run | DATA | Content |
|---|---|---|---|---|
| 0, 1 | 2–6 | 0x0022DE00 / 0x0022DF00 | 0xB0 | title screen + main menu (section 2) |
| 2 | 14–16 | 0x00230708 / `MissionTitleRun` | 0xBB + mission − 1 | mission title card |
| 3, 4 | 17–22 | 0x00238278 … 0x00238440 | 0x111 + mission (3) | **briefing**: briefing movie with timed subtitles, then the 3D terrain map with unit icons |
| 5 | 23–26 | 0x00230EB0 / `CampaignMenuRun` | 0xB1 | campaign menu between missions |
| 6–10, 13–16 | 27–36 | `HangarEnter` / `AircraftSelectRun` | 0xCD…, 0xE4… | hangar: aircraft select, buy/sell aircraft, colours, special weapons; 3D aircraft viewer |
| 11, 12 | 37–40 | 0x002362A0 / 0x00236390 | 0xB2 | post-mission menu: next mission, replay, quit, save |
| 17 | 41–44 | 0x00240E78 / 0x00240F68 | 0xB4 | options: screen position, brightness, SCREEN RATIO, CAPTIONS, HUD UNIT, INTERLUDE |
| 18 | 45–48 | 0x002436D8 / 0x002437C8 | 0xB5 | controller settings (both pads) |
| 19 | 49–51 | 0x00244DA8 / 0x00244EA0 | 0xB8 | BGM player (36 tracks, table 0x003AF6A0) |
| 20 | 52–54 | 0x00245A58 / 0x00245B50 | 0xB9 | interlude (movie) viewer, 12 entries; plays through page 0x21 |
| 21 | 55–57 | 0x002422B8 / 0x002423A0 | 0xB7 | records: total flight time, targets destroyed, per-aircraft usage |
| 22 | 58–61 | 0x00244320 / 0x00244410 | 0xB6 | sound options |
| 23 | 62–64 | 0x00246960 / 0x00246A58 | 0xBA | **HDD install** (unsupported path, `dead-code.md` §5) |
| 24–30, sub-pages 0x41–0x47 | 65–73 | 0x0023C600 / 0x0023C708 | 0x110 | Free Mission / Trial Mission / Tutorial Mission / VS Stage select, records, action menus |
| 31–34 | 7–13 | `MenuMovieEnter` … `MenuMovieEnd` | 0x103 + n (subtitles) | in-menu movie player (story interludes, opening) |
| sub-pages 0x4A–0x57 | 74–89 | 0x0023E9F0 / 0x0023EB68 | 0xB3 | save / load (memory card; HDD variants unsupported) |

### Briefing (pages 3/4)
- `CreateBriefing` picks the mission index from `game[0x27]`. Per-mission tables:
  - timings 0x003AF8B8 / 0x003AF900 / 0x003AF948;
  - subtitle list 0x0039E528;
  - header strings 0x003A5330 (5 per mission: DATE/AREA, PARALLEL, OPERATION, COORDINATES, CONDITION).
- The briefing movie (`StartMenuMovie(mission + 0xD)`) is the time base. Subtitles advance when the movie frame passes their time (0x002363D0).
- Circle advances a line, Start skips.
- Map view (sub 22):
  - a height-field terrain drawn as 128×128 coloured point sprites (0x0023EE20: MMI bilinear sampling, VU0 projection);
  - unit icons from per-mission lists (0x003A5150, 20-byte entries: x, y, z1, z2, type, faction);
  - a point list with camera presets (0x0039EB88);
  - enemy counts AIR/GROUND/SEA (0x003A54B8).

### Hangar (pages 6–10, 13–16)
- Mode by sub-state (0x00233808):
  - 0x1B: aircraft select for the next mission (sets the mission id and mode 1);
  - 0x1C–0x23: buy/sell aircraft, colours, special weapons.
- **Credits** are in 0x002A0D30. The hangar keeps a check value `credits ^ 0x5963` and commits credits and ownership only if the check matches (`CommitHangarSelection`): a simple tamper check.
- **Ownership** (0x002A0320, six u64):
  - 2 bits per aircraft (21) per colour (3 words) and per special weapon (3 words);
  - values: 0 locked, 1 shown, 2 buyable, 3 owned.
  - Reordered between aircraft order and menu order with tables 0x0039DA80 / 0x0039DA50.
- Aircraft table 0x0039DA80 (stride 0x3C): name, description, colour prices `+9`, weapon prices `+0xC`, stats `+0xC` (gauges).
  - Selling the last owned colour also sells that aircraft's weapons (0x00234C50).
- **Alternate mission ids chosen at aircraft select** (`AircraftSelectRun`):
  - mission 1 flown with the default aircraft → mission id 0x25 and aircraft 0x17;
  - mission 18 → mission id 0x24.

### Options (page 17); option bytes 0x002A15C4–0x002A15D3

| Address | Meaning | Applied by |
|---|---|---|
| 0x002A15C4 (8 bytes) | sound: byte 0 bit 0 output mode (mono/stereo), bytes 4–7 levels 0–15 for BGM, SE, VOICE, JET SE | `ApplySoundOptions` |
| 0x002A15CC | flags: bit 0 **SCREEN RATIO** (16:9; also ctx flag 0x10000), bit 1 CAPTIONS, bit 2 HUD UNIT, bit 3 INTERLUDE (skip interlude movies) | menus, `MenuMovieEnter` |
| 0x002A15CD | brightness level 0–6 (default 3) | `ApplyBrightness` 0x00105988 (was `ApplyScreenScale` (H)) |
| 0x002A15CE / CF | screen position dx / dy, −12…+12 | `ApplyDisplayOffset` |
| 0x002A15D0 | controller settings, u16 per player: bit 0 CONTROL TYPE (EASY), bit 1 REVERSE (pitch), bit 2 PILOT VIEW, bit 3 VIBRATION | `ApplyVibrationOptions`, flight code |

**SCREEN RATIO is an original 16:9 mode.**
- It sets ctx flag 0x10000.
- Many layout functions branch on that flag: menu layouts, the viewer camera aspect, text boxes and the briefing map. Flight code is still to be checked.
- See the ADR-006 note.

### Save / load (sub-pages 0x4A–0x57)
- Files registered with the libmc layer (0x0025F340): `icon.sys` (0x3C4 bytes), `icon1.ico` (0xCB98 bytes), and the save image (0x4B10 bytes). Its file name and the libmc call order belong to step 7.
- **Save image** (`InitSaveFileImage` 0x0023C820):
  - +0 magic `AC04` (0x34304341);
  - +4 size 0x4B10;
  - +0xC checksum = byte sum of the 0x4B00 bytes from +0x10 (0x0023C7B8);
  - +0x10: 4 slots of 0x12C0 bytes, each a copy of the **campaign block 0x002A0318–0x002A15D7**.
- That campaign block holds:
  - progress flags;
  - ownership;
  - records (free mission 0x002A045C: 6 difficulties × 18 missions × 0x14 bytes `{u16 rank, u16 points[3], u32 time[3]}`; trial 0x002A0CCC);
  - credits 0x002A0D30;
  - statistics;
  - `g_MissionNo` and the difficulty;
  - the option bytes above.
- Slot summary: mission number and title, CLEAR mark, play time (frames / 0x34BC0 = hours), credits, difficulty.
- Statistics are clamped before saving (0x00105448), e.g. credits ≤ 999,999,999.

### Mission select (sub-pages 0x41–0x47)
- Modes:
  - 0 Free Mission (difficulty `game[0x28]`);
  - 1 Trial Mission (unlocked count from bits 0x002A031C);
  - 2 Tutorial Mission;
  - 3 VS Stage (two players: pad 2 is read too; stage variant `game[0x2E]`).
- `GetMissionId(kind, n)` maps them to mission ids (table 0x00384800: campaign 18, trial 5, tutorial 5, VS 8).
- Ranks are shown as `---`, `<C>`, `<B>`, `<A>`, `<S>`.

### Unlocks (0x00226580–0x002268B0)
After a campaign clear:
- aircraft become buyable from their first mission onwards (0x0039DA8A);
- colour 1 is unlocked for missions ranked A or better on difficulty ≥ 2;
- aircraft 18 when all others are buyable;
- aircraft 21 colours by S ranks on difficulties 2/3/4 (needs the cleared-twice bit 8).

### In-menu movie player (pages 31–34, subs 7–13); important for ADR-002
- `StartMenuMovie` allocates a **6 MiB** work buffer from the work heap and sets **`RCNT0_MODE = 0x83`**: timer 0 counts H-blanks.
- Each movie frame:
  - resets `RCNT0_COUNT` (0x00238808);
  - runs the player;
  - at the end, **`MovieFrameSync`** flips the display buffer, **busy-waits until RCNT0 ≥ 0x118 (280 H-blanks)** and increments `ctx->frame_count` itself.
- The movie loop is therefore paced by the H-blank counter, not by `sceGsSyncV`. The port must emulate T0 counting H-blanks at the NTSC line rate (15,734 Hz) on this path, or prove that "one field" is equivalent (H, see ADR-002).

### Quirks to preserve
- The records page allocates `((frame_count & 0xF) + 1) * 0x10` bytes before its object (0x002414E0). Work-heap offsets after it depend on the frame count at entry.
- The HDD install page allocates 10 MiB and is unreachable without an HDD.
- Debug `printf("CH0:%08x  CH2:%08x")` when the stream slots are not ready (0x002386D8).

### Page 2: mission title (subs 14–16)
- 0x00230708: creates a 0x260-byte object and queues DATA 0xBB + (mission − 1), the title image.
- 0x00230728: waits for the load, fades in, fades out the BGM.
- `MissionTitleRun` 0x00230790 / `MissionTitleUpdate`: zoom-in; Start/X or 5 s ends it. Then `game[0x27] = GetMissionId(0, g_MissionNo)`, and `EnterMenu(0x1F)` for mission 1 (opening movie page), otherwise `EnterMenu(3)` (briefing).

### Aircraft viewer (3D hangar, used by the hangar and aircraft pages)
`CreateAircraftViewer` (0x9D0 bytes):
- Loads:
  - DATA 0xCD (hangar scene, 5 sub-blocks);
  - DATA 0xCD + aircraft (1–21, the aircraft model);
  - DATA 0xE4 + weapon id for each weapon of the aircraft (weapon list from `0x0011B900(aircraft)`).
- Draws:
  - the hangar models;
  - light spots (VU0 projection, 0x0022F810);
  - the aircraft in the selected colour, with a floor reflection (Y-mirrored matrix) and a shadow;
  - the weapon models, selected by bitmask.
- A per-aircraft part setup goes through jump table 0x00407070 (8 cases): **open point**, because the targets are not defined as functions yet.
- `SetAircraftViewerAircraft` reloads it for another aircraft or colour.
- Uses the model render API of the flight module (0x0014B168–0x0014B5A0, 0x001370D8 lighting): rendering path for priority 2, step 9.
