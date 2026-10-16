# Front end (scene 4): menus, hangar, briefing, options, extras

The front end is the code module 0x00226198–0x00246E17 plus helpers in other modules. Per-function status: `../port/coverage-functions.csv` (subsystem `frontend`). `(H)` marks a hypothesis.

## Scene handler and navigation

`SceneMenu` 0x002262D0: `table_0x00384640[ctx->sub]()` (90 entries, 52 unique), then the menu fade overlay (`func_00226CD0`) when `*(0x00410E88)+0x10` is set, then `UpdateMenuBgm` (`UpdateMenuBgm` 0x0022CAF0).

| Sub | Handler | Role |
|---|---|---|
| 0 | `MenuEnterPage` 0x002261B8 | 0x40000-byte double buffer, `SetMenuDisplayMode(640, 448)`, fade reset, `RequestSubState(page_to_sub[g_MenuPage])` (table 0x003845F8, 35 pages); BGM reset when coming from another scene |
| 1 | `MenuEnterSubPage` 0x00226248 | draws the fade frame, `RequestSubState(g_MenuSubPage)` (< 0x5A, else 2) |

Navigation helpers:
- `EnterMenu(page)` 0x00226198: `g_MenuPage` (gp 0x00410E7C, `gp−0x4B74`) = page, tail call `RequestScene(4)`. Restarts scene 4 at sub 0 with the new page. Other scenes enter the menus this way.
- `SetMenuSubPage(n)` 0x002261A8: `g_MenuSubPage` (0x00410E7E) = n, tail call `RequestSubState(1)`.
- `MenuGoTo(kind, arg)` 0x00226450: kind 0 → `EnterMenu(arg)`, 1 → `SetMenuSubPage(arg)`, 2/3 → `game[0x2A]` = 0/1 + `RequestScene(2)` (start the flight scene with or without the pre-mission cutscene). Most pages store a `(kind, arg)` pair for "next" and call it (16 callers).
- `EnterPostMissionMenu` 0x00226B78: after results, `EnterMenu(0x20)` with `ctx->ret_sub` = 1..11 for missions 1, 2, 5, 7, 8, 12, 14, 15, 17, 18 (story movie after those missions), otherwise `EnterMenu(0xB)`.

Title screen (PCSX2): scene 4, sub-state 6, `g_MenuPage` = 0, `g_MenuSubPage` = 2.

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

Strings referenced from 0x00231000–0x00246E00 include `AIRCRAFT >`, `WEAPON >`, `BUY >`/`SELL >` (hangar), `MISSION OBJECTIVES:`, `COORDINATES >`, `NOW LOADING` (briefing), `BRIGHTNESS >` (options), `TOTAL FLIGHT TIME >` (records), `BGM NUMBER >`, `INTERLUDE NUMBER >` (extras: sound test, movie viewer), `icon.sys` (save).

## Title screen and main menu (pages 0/1, subs 2–6)

| Sub | Handler | Role |
|---|---|---|
| 2–4 | `EnterTitle` 0x0022DE00 | heap back to the page mark (`PopWorkHeapMark`/`PushWorkHeapMark`), `CreateMainMenu` (0x6A0-byte object), queue DATA 0xB0 (2 items: font block, logo packet with the texture uploads) → 5 |
| 5 | `LoadTitle` 0x0022DEA0 | wait until the load queue is idle, `LoadFontBlock` 0x00251D28 (item 0), help box → 6 |
| 6 | `RunTitle` 0x0022DF00 | `UpdateFade`, `MainMenuUpdate` 0x0022D698, `DrawMainMenu` |

Main menu object (0x6A0 bytes, work heap, `g_TitleMenu` 0x00411810; translated in slice S2a, `src/game/menu.cpp`): +0 bucket list (2 buckets), +0x10 data load queue, +0x220 DATA 0xB0 items {font block, logo packet called by `DrawTitleLogo`}, +0x240 fade object, +0x250 menu tree, +0x658 help message box, +0x67C state, +0x680/+0x684 `MenuGoTo` target, +0x688 idle time, +0x68C title time, +0x690 logo alpha (all f32 seconds / 0..1).

`MainMenuUpdate` state (+0x67C), pad bits from the pad-1 edge word (ctx+0x2B4), tree movement from the repeat word (ctx+0x2B8):
- 0: wait until the menu fade value exceeds `*0x00410150`, then 2.
- 1: back from the menu: when the object fade is out (flag 8), idle time 0, title BGM (track 3), state 2.
- 2: title idle. Pad 0x50 (Start or X) once the title time exceeds **2 s**: `MainMenuSetup(obj, 3)` (menu tree, menu BGM track 4), object fade in, SE (0, 2), state 4. After **19 s** idle: state 3, menu fade out.
- 3: when the fade is out: `StartTitleDemo`.
- 4: when the object fade-in is done (flag 4): 6. 5: when the menu fade value exceeds `*0x00410154`: 6 (mode 4 entry, reopening the save/load column).
- 6: menu tree active (`UpdateMenuTree`, tab requests 0x33). Pad 0x20 (Triangle) `MainMenuBack`, else pad 0x50 `MainMenuSelect`.
- 7: object fade out → `MenuGoTo(1, sub)`; 8: screen fade out → `MenuGoTo(0, page)`.

Quirk: `MainMenuBack` from a sub-column sets the active column to **0** (not the parent) and closes the sub-column; the menu tree has only one level of sub-columns, so it is the same thing in practice.

Help line (`UpdateMainMenuHelp`): message id per item from the s16 table 0x0039D6B8 (items with id 0 show 0x26 once a campaign is under way: mission > 1 or the cleared bit), typed out by the message box at `*0x00410130` characters per frame.

`MainMenuSelect` item ids (selected id = `func_0022AFB8`):

| Id | Action |
|---|---|
| 0, 1, 3, 4, 8 | open a sub-column (3 only when ctx flag 0x80000 is set, i.e. never in this build) |
| 2 | page 2 (mission title) after `ApplyCampaignSelection`: continue the campaign |
| 5, 6 | sub-pages 0x42 / 0x43, with `game[0x29] = 2` (free mission mode) |
| 7 | sub-page 0x44, default aircraft (2/0/0) |
| 9–14 | difficulty 0–5 into 0x002A15BA, `g_MissionNo = 1`, campaign data reset (`func_00105418`), page 0x20 (opening story movie): **new game** |
| 15–20 | the same without the data reset: **new game keeping unlocks** (H) |
| 0x15, 0x16, 0x1D–0x23 | sub-pages 0x56, 0x4F, 0x2D, 0x29, 0x3A, 0x37, 0x34, 0x31, 0x3E (options, records, extras, save/load) |
| 0x17–0x1C | sub-page 0x41 with `game[0x28]` = 0–5 |

Menu item visibility depends on the progress flags 0x002A0318 (bit 0 campaign cleared once; bits 1, 2 cleared on higher difficulties; bit 3 cleared twice). `MainMenuSetup` builds the hidden-item masks from them (column 0: 0x32 before the first clear, else 2, with 4 added while `g_MissionNo` is 0; columns 1/2/4: 0x30, 0x20 with bit 1, 0 with bit 2; column 5: 0x40 unless ctx flag 0x80000, plus 0x30 before the first clear) and passes them to `InitMenuTree` with the column definitions `kMainMenuColumns` 0x0039D5B0 (6 × 0x18), connectors 0x0039D640 and parent ids 0x0039D6A8. The cursor positions are written back into `kMainMenuColumns` when the menu is left (`StoreMenuTreeCursors`), so the menu reopens on the same item.

**Attract demo** (`StartTitleDemo` 0x0022CC68) cycles `D_00410E98` through 6 entries. Four of them start a flight:

| Entry | Mission id (`game[0x27]`) | Aircraft (`game[0]`) | `game[3]` | Mode (`game[0x29]`) |
|---|---|---|---|---|
| default (0, 2, 4) | 0x23 | campaign selection, or 0x11 if not cleared | | 1 |
| 1 | 3 | 0x10 | 1 | 4 (demo), `game[0x2B]` = 0 |
| 3 | 0x10 | 4 | 0 | 4, `game[0x2B]` = 1 |
| 5 | 5 | 3 | 1 | 4, `game[0x2B]` = 2 |

`func_0022CC50` sets `D_00410E98` = 1 on the no-save intro path and 0 on the title path (`results-startup.md`).

## Campaign state used by the menus

| Address | Meaning | Evidence |
|---|---|---|
| `g_MissionNo` 0x002A15B8 | campaign mission number 1–18 | `AdvanceCampaignMission` |
| 0x002A15BA | difficulty 0–5 | `MainMenuSelect` 9–14 |
| 0x002A15BE / C0 / C2 | selected aircraft (1–21), colour (0–2), special weapon (0–3) | `ApplyCampaignSelection`, 0x00226A00 |
| 0x002A0318 | progress flags (cleared bits) | `MarkCampaignCleared` |
| 0x002A031C | trial unlock bits | `ApplyMissionResults` (`results-startup.md`) |
| 0x002A0320 | ownership: 2 bits per aircraft, per colour (+ weapon words) | 0x00226A00 |
| `g_GameState` bytes | `[0]` aircraft, `[1]` colour, `[3]` weapon, `[0x10..0x13]` copies, `[0x25]` = 1, `[0x27]` mission id, `[0x28]` difficulty, `[0x29]` mode (1 campaign, 2 free mission, 3 trial, 4 demo), `[0x2A]` flight-entry kind, `[0x2B]` demo index | `../game/main-context.md` |

`AdvanceCampaignMission`: after mission 18 it wraps to 0 and calls `MarkCampaignCleared`, which sets the cleared bits and unlocks aircraft (0x00226968, 0x00226748).

The whole campaign block 0x002A0318–0x002A15D7 is the save payload (`../formats/SAVE.md`).

## Shared menu widgets (0x00226D00–0x0022C600)

- **List widget** (stride 0x18; `SetListWidgetState` 0x002271B8, `UpdateListWidget` 0x00227298, `DrawListWidgets`): open/close commands, type-in effect while opening (SE 9 every third frame), cursor with wrap (pad bits 1/2, SE 1), per-item highlight scale. Layouts: `src/game/menu_widgets.cpp`.
- **Menu tree** (`InitMenuTree` 0x0022ABC0, `UpdateMenuTree` 0x0022AF60, `DrawMenuTree` 0x0022AFF0, `GetMenuTreeSelection` 0x0022AFB8): up to 8 columns of list widgets, hidden-item masks, connector lines with glow caps (`BuildConnector` 0x00229158).
- **Crosshair cursor** with a 10-position trail (0x002298F8–0x00229BC0).
- **Panel animation** (0x0022B1A8 family).
- **Message box** (`SetMessageBoxText` 0x0022B8B8, `UpdateMessageBox` 0x0022B9A0, `DrawMessageBox` 0x0022BA68): text ids from the text system (0x00251xxx–0x00252xxx), up to 3 lines, typewriter effect, coloured characters with codes 8–11, frame lines brighter for started lines.
- **Fade object**: `StartFade` / `UpdateFade`, cosine ease; flag 4 = fade-in done, flag 8 = fade-out done.
- **Data load queue**: `QueueMenuDataLoad` 0x0022C618 / `UpdateMenuDataLoad` 0x0022C6B0, a 32-entry ring of `{DATA entry, sub-block count, pointer table}` (reset `func_0022C5E8`); loads into the work heap and splits the entry into its sub-blocks.
- **Menu BGM**: `PlayMenuBgm` crossfades between stream slots 0 and 1; `UpdateMenuBgm` starts the track when the slot is idle; `FadeOutMenuBgm` fades the slots out.
- Full-screen overlay passes: `DrawMenuFrameFilter` 0x00227C10 (tinted copy of the frame onto itself + `BuildScanlineBands` 0x00227800 + `BuildZoomBlur` 0x00227A10, blended with destination alpha), `DrawMenuAlphaFilter` 0x00227F30 (writes only the alpha channel that the bands and blur then use); letterbox bars (0x0022A558). The menu fade object `g_MenuFade` 0x0039D4E0 (pointer `g_pMenuFade` 0x00410E88) also holds 7 overlay levels drawn by `DrawMenuTabs` 0x00228750 (H: the six tabs along the bottom).

## Pages

Each page follows the same pattern: an **enter** sub-state allocates the page object from the work heap and queues its DATA entries; a **load** sub-state waits and registers the textures (0x00251D28); a **run** sub-state calls `UpdateFade`, the page update and the page draw. Pages leave with `MenuGoTo(kind, arg)`; most pages store their return target in the object.

| Page(s) / sub-pages | Subs | Enter / run | DATA | Content |
|---|---|---|---|---|
| 0, 1 | 2–6 | 0x0022DE00 / 0x0022DF00 | 0xB0 | title screen + main menu |
| 2 | 14–16 | 0x00230708 / `MissionTitleRun` 0x00230790 | 0xBB + mission − 1 | mission title card |
| 3, 4 | 17–22 | 0x00238278 … 0x00238440 | 0x111 + mission (3) | **briefing**: briefing movie with timed subtitles, then the 3D terrain map with unit icons |
| 5 | 23–26 | 0x00230EB0 / `CampaignMenuRun` 0x00230FC8 | 0xB1 | campaign menu between missions (`CampaignMenuUpdate` 0x00230CA0: menu tree states 0..3, select `func_00230A78`, exit `MenuGoTo(+0x670, +0x674)`) |
| 6–10, 13–16 | 27–36 | `HangarEnter` / `AircraftSelectRun` 0x00235AB8 | 0xCD…, 0xE4… | hangar: aircraft select, buy/sell aircraft, colours, special weapons; 3D aircraft viewer |
| 11, 12 | 37–40 | 0x002362A0 / 0x00236390 | 0xB2 | post-mission menu: next mission, replay, quit, save |
| 17 | 41–44 | 0x00240E78 / 0x00240F68 | 0xB4 | options: screen position, brightness, SCREEN RATIO, CAPTIONS, HUD UNIT, INTERLUDE |
| 18 | 45–48 | 0x002436D8 / 0x002437C8 | 0xB5 | controller settings (both pads) |
| 19 | 49–51 | 0x00244DA8 / 0x00244EA0 | 0xB8 | BGM player (36 tracks, table 0x003AF6A0) |
| 20 | 52–54 | 0x00245A58 / 0x00245B50 | 0xB9 | interlude (movie) viewer, 12 entries; plays through page 0x21 |
| 21 | 55–57 | 0x002422B8 / 0x002423A0 | 0xB7 | records: total flight time, targets destroyed, per-aircraft usage |
| 22 | 58–61 | 0x00244320 / 0x00244410 | 0xB6 | sound options |
| 23 | 62–64 | 0x00246960 / 0x00246A58 | 0xBA | **HDD install** (unreachable: the main-menu item needs ctx flag 0x80000; `save.md`) |
| 24–30, sub-pages 0x41–0x47 | 65–73 | 0x0023C600 / 0x0023C708 | 0x110 | Free Mission / Trial Mission / Tutorial Mission / VS Stage select, records, action menus |
| 31–34 | 7–13 | `MenuMovieEnter` 0x002390C8 … `MenuMovieEnd` 0x002392A8 | 0x103 + n (subtitles) | in-menu movie player (story interludes, opening) |
| sub-pages 0x4A–0x57 | 74–89 | 0x0023E9F0 / 0x0023EB68 (save page frame, sub 0x59) → `SavePageUpdate` 0x0023D198 (save/load state machine) | 0xB3 | save / load (memory card; HDD variants unreachable; `save.md`) |

### Page 2: mission title (subs 14–16)
- 0x00230708: creates a 0x260-byte object (`func_00230258`) and queues DATA 0xBB + (mission − 1), the title image.
- 0x00230728: waits for the load, fades in, fades out the BGM.
- `MissionTitleRun` / `MissionTitleUpdate` 0x00230308: zoom-in; Start/X or 5 s ends it. Then `game[0x27] = GetMissionId(0, g_MissionNo)`, and `EnterMenu(0x1F)` for mission 1 (opening movie page), otherwise `EnterMenu(3)` (briefing).

### Briefing (pages 3/4)
- `CreateBriefing` picks the mission index from `game[0x27]` (via `func_00226320`). Per-mission tables: timings 0x003AF8B8 / 0x003AF900 / 0x003AF948; subtitle list 0x0039E528; header strings 0x003A5330 (5 per mission: DATE/AREA, PARALLEL, OPERATION, COORDINATES, CONDITION).
- The briefing movie (`StartMenuMovie(m + 0xD)` 0x00238778) is the time base. `m` = campaign mission number − 1 (0..17), from `func_00226320(game[0x27])` (mission id → campaign number, u8 table 0x003847A8) in the briefing init `CreateBriefing` 0x002365C0 (briefing object +0x560). Movie ids 13–30 are MIS01–MIS18, so **MISnn.SMV is the briefing movie of campaign mission nn** (confirmed from code). Subtitles advance when the movie frame passes their time (0x002363D0).
- Circle advances a line, Start skips.
- Map view (sub 22):
  - a height-field terrain drawn as 128×128 coloured point sprites (0x0023EE20: MMI bilinear sampling, VU0 projection);
  - unit icons from per-mission lists (0x003A5150, 20-byte entries: x, y, z1, z2, type, faction);
  - a point list with camera presets (0x0039EB88);
  - enemy counts AIR/GROUND/SEA (0x003A54B8).

Translation (S4, `src/game/briefing_page.cpp`; names in `../symbols.csv`):
- Page object 0x5D0 bytes at `g_Briefing` 0x00411820; field list at the top of the source file.
- Movie part (subs 19–21): `RunBriefingMovie` runs one movie iteration per frame; time = pictures shown × k; subtitle entries `{u32 message, f32 time}` type out 2 characters per frame. Start ends the movie; Circle completes or advances a line.
- Map view (sub 22, `BriefingMapUpdate`, states 0–5 by the jump table 0x004089F0): camera presets per mission (`g_BriefingCameraPresets` → 0x0039EB88, 0x18-byte entries), left/right to browse, Start or Circle past the last preset leaves to `MenuGoTo(1, 0x17)` (the campaign menu) or `(1, 0x45)`.
- Terrain (`WriteTerrainPoints` 0x0023EE20): DATA 0x111+mission item 1 holds 256×256 height bytes at +0x70 and 256 colours at +0x100D0. Per row, two height rows are interpolated with MMI into the scratchpad (0x570); per column, again; the point is projected with the map camera on VU0 and drawn as an 80×80 (1/16 px) sprite coloured by height. REGLIST tags of at most 0x7FFE sprites.
- Units (`DrawBriefingUnits`): list `g_BriefingUnitLists` → 0x003A5150, `{count, units}` per mission, 20-byte entries `{f32 x, f32 y, f32 y2, f32 z, u8 type, u8 faction}`; icons by type (0x003A5550), colours by faction (0x003A55C0); type 4 units also get a height link to y2.
- Checked in the port: MIS01 with subtitles and the header panels, then the map with terrain, icons and texts.

### Campaign menu (page 5, subs 23–26)
- `src/game/campaign_menu_page.cpp`: page object 0x680 bytes at `g_CampaignMenu` 0x00411818; a 6-column menu tree (0x0039D920) with a help line (messages 0x0039DA28).
- `CampaignMenuSelect` (jump table 0x00407120, 19 items): page 6 (hangar), 3 (briefing; sets `game[0x27]`), 0xD–0x10, 0; sub-pages 0x2E, 0x2A, 0x3B, 0x51, 0x57, 0x4A/0x50 (save/load; their HDD columns 4/5 need ctx flag 0x80000); items 2–4 open columns 1–3.
- Checked in the port: START MISSION, BRIEFING, BUY, SELL, OPTIONS, SAVE, LOAD with the help line.

### Hangar (pages 6–10, 13–16)
- Mode by sub-state (0x00233808): 0x1B aircraft select for the next mission (sets the mission id and mode 1); 0x1C–0x23 buy/sell aircraft, colours, special weapons.
- **Credits** at 0x002A0D30. The hangar keeps a check value `credits ^ 0x5963` and commits credits and ownership only if the check matches (`CommitHangarSelection`): a simple tamper check.
- **Ownership** (0x002A0320, six u64): 2 bits per aircraft (21) per colour (3 words) and per special weapon (3 words); values 0 locked, 1 shown, 2 buyable, 3 owned. Reordered between aircraft order and menu order with tables 0x0039DA80 / 0x0039DA50.
- Aircraft table 0x0039DA80 (stride 0x3C): name, description, colour prices +9, weapon prices +0xC, stats +0xC (gauges). Selling the last owned colour also sells that aircraft's weapons (0x00234C50).
- **Alternate mission ids chosen at aircraft select** (`AircraftSelectRun`): mission 1 flown with the default aircraft → mission id 0x25 and aircraft 0x17; mission 18 → mission id 0x24.

### Aircraft viewer (3D hangar)
`CreateAircraftViewer` (0x9D0 bytes):
- Loads DATA 0xCD (hangar scene, 5 sub-blocks), DATA 0xCD + aircraft (1–21, the aircraft model, `../formats/MODEL.md`), and DATA 0xE4 + weapon id for each weapon of the aircraft (weapon list from `func_0011B900(aircraft)`; ids above 0x22 map to 0).
- Draws the hangar models, light spots (VU0 projection, 0x0022F810), the aircraft in the selected colour with a floor reflection (Y-mirrored matrix) and a shadow, and the weapon models selected by bitmask.
- A per-aircraft part setup in `func_0022E978` goes through jump table 0x00407070 (8 cases, case code at 0x0022E9D4 inside the function).
- `SetAircraftViewerAircraft` reloads it for another aircraft or colour.
- Uses the menu 3D object API of the flight module (0x0014AE60–0x0014B5A0) and the light-set writer `WriteLightSetPacket` 0x001370D8 (`rendering.md`).

### Options (page 17); option bytes 0x002A15C4–0x002A15D3

| Address | Meaning | Applied by |
|---|---|---|
| 0x002A15C4 (8 bytes) | sound: byte 0 bit 0 output mode (mono/stereo), bytes 4–7 levels 0–15 for BGM, SE, VOICE, JET SE | `ApplySoundOptions` |
| 0x002A15CC | flags: bit 0 **SCREEN RATIO** (16:9; also ctx flag 0x10000), bit 1 **CAPTIONS** (subtitles), bit 2 HUD UNIT, bit 3 INTERLUDE (skip interlude movies) | menus, `MenuMovieEnter`, radio subtitles (`sound.md`) |
| 0x002A15CD | brightness level 0–6 (default 3) | `ApplyBrightness` 0x00105988 |
| 0x002A15CE / CF | screen position dx / dy, −12…+12 | `ApplyDisplayOffset` |
| 0x002A15D0 | controller settings, u16 per player: bit 0 CONTROL TYPE (H: EASY; `flight.md`), bit 1 REVERSE (pitch), bit 2 PILOT VIEW, bit 3 VIBRATION | `ApplyVibrationOptions`, flight code |

**SCREEN RATIO is an original 16:9 mode.** It sets ctx flag 0x10000, and many layout functions branch on that flag: menu layouts, the viewer camera aspect, text boxes, the briefing map, and the flight HUD and cameras (anamorphic HUD, `camera-hud.md`). See ADR-006.

Options page "SCREEN SETTINGS" (subs 0x29–0x2C, `src/game/options_page.cpp`): `UpdateOptionsHelp` 0x0023F350 (help text, table 0x003AF440), `SetOptionsView` 0x0023F538 (0 list, 1 screen position, 2 brightness; CAPTIONS hidden before the first clear), `OptionsBack` 0x0023F7D8, `OptionsSelect` 0x0023F870, `OptionsListUpdate` 0x0023FB48 (writes 0x002A15CC live; SCREEN RATIO also sets the ctx flag), `OptionsAdjustCancel`/`OptionsAdjustAccept` 0x0023FD10/0x0023FDB8, `ScreenPositionUpdate` 0x0023FE60 (−12…12, pad 0x4000 resets), `BrightnessUpdate` 0x00240088 (0–6), dispatched by `OptionsUpdate` 0x002402E0 by view. Brightness test patterns: `BuildGreyRamp`, `BuildLevelSquares`, `BuildColorRamps` (0x002408E0–0x00240A88), drawn by `DrawBrightnessPatterns` 0x00240BB8 at strength level × 128.

### Controller settings (page 18, subs 0x2D–0x30)
`src/game/controller_page.cpp`. Rows: player (1/2; both pads are read), CONTROL TYPE, REVERSE, VIBRATION, PILOT VIEW (bits 1, 2, 8, 4 of the player's u16 at 0x002A15D0). Accept writes both words and calls `ApplyVibrationOptions`; switching vibration on vibrates that pad. The button-mapping table and the controller picture (DATA 0xB5) are drawn by `DrawControllerTexts`/`DrawControllerImage`/`DrawControllerGrid` 0x00242BE8–0x002434A0. From the main menu (sub 0x2D) back to page 4, otherwise to sub-page 0x18.

### Records "PERSONAL DATA" (page 21, subs 0x37–0x39)
`src/game/records_page.cpp`. Total flight time, sortie and kill totals, and the aircraft/colour usage list sorted by sorties (`BuildUsageList` 0x00241068 with `CombSortDescending` 0x00240FA0), five rows per page with usage bars (`AnimateRecordBars`) and aircraft icons (DATA 0xB7). Quirk: frame-count dependent pad before the object (below).

### Sound settings (page 22, subs 0x3A–0x3D)
`src/game/sound_page.cpp`. Rows: SOUND (MONO/STEREO, bit 0 of 0x002A15C4 set = mono) and the BGM, SE, VOICE, JET SE levels 0–15 (bytes 0x002A15C8–CB). Every change goes into a working copy and is applied at once (`ApplySoundPageValues` 0x00243D18 → `ApplySoundOptions`); accept stores the copy, cancel re-applies the stored options. Pad 0x4000 sets the level row to 15. From the main menu (sub 0x3A) back to page 4, otherwise to sub-page 0x18.

### BGM player (page 19, subs 0x31–0x33)
`src/game/sound_page.cpp`. Entering fades the menu BGM out (0.5 s). An 18 × 2 grid of the 36 tracks of table 0x003AF6A0 (`{name, length in frames, menu BGM track}`); pad 0x10 plays the cursor track with `PlayMenuBgm` (not when that track is already playing), pad 0x2000 stops it (2 s fade), and the track fades out (6 s) when its length has played (`UpdateBgmPlayerTimer`). Cancel returns to page 4 (`BgmPlayerUpdate`). `BgmPlayerInput` has its own cancel branch with a 1 s BGM fade, but it is unreachable: `BgmPlayerUpdate` checks cancel first and its `SetPanelState(…, 1)` clears panel flag 0x800, so `BgmPlayerInput` is not called in that frame (`../port/dead-code.md` §5). Reached from the EXTRAS column of the main menu (after the first campaign clear).

### Save / load (sub-pages 0x4A–0x57)
- Files registered with the memory-card layer: `icon.sys`, `icon1.ico` and the save image (`save.md`). The image layout is in `../formats/SAVE.md`.
- Slot summary: mission number and title, CLEAR mark, play time (frames / 0x34BC0 = hours), credits, difficulty.
- Statistics are clamped before saving (0x00105448), e.g. credits ≤ 999,999,999.

### Mission select (sub-pages 0x41–0x47)
- Modes: 0 Free Mission (difficulty `game[0x28]`); 1 Trial Mission (unlocked count from bits 0x002A031C); 2 Tutorial Mission; 3 VS Stage (two players: pad 2 is read too; stage variant `game[0x2E]`).
- `GetMissionId(kind, n)` maps them to mission ids (table 0x00384800: campaign 18, trial 5, tutorial 5, VS 8).
- Ranks are shown as `---`, `<C>`, `<B>`, `<A>`, `<S>`.
- Input: `func_0023A668` (two-player mode merges pads 5/6), confirm `func_0023A550`.

### Unlocks (0x00226580–0x002268B0)
After a campaign clear:
- aircraft become buyable from their first mission onwards (0x0039DA8A);
- colour 1 is unlocked for missions ranked A or better on difficulty ≥ 2;
- aircraft 18 when all others are buyable;
- aircraft 21 colours by S ranks on difficulties 2/3/4 (needs the cleared-twice bit 8).

### In-menu movie player (pages 31–34, subs 7–13)
- Subs: 7–10 `MenuMovieEnter` 0x002390C8 (sub 9 with `0x002A15CC & 8` skips straight to `MenuGoTo`; otherwise starts the movie, descriptor `*0x00411824`, and queues subtitle DATA 0x103 + n) → 0xB `MenuMovieWait` 0x00239188 (subtitle font loaded and VAG slots 0/2 idle) → 0xC `MenuMoviePlay` 0x00239248 (play, `UpdateMenuMovie` 0x00238810) → 0xD `MenuMovieEnd` 0x002392A8 (tail call `MenuGoTo(desc+0x278, desc+0x27C)`). A movie's "next" target is stored in its descriptor.
- `StartMenuMovie` 0x00238778 allocates a **6 MiB** work buffer from the work heap and sets **`RCNT0_MODE = 0x83`**: timer 0 counts H-blanks. Display modes for movies: `SetMoviePageDisplay` 0x00238480, `SetMoviePlayDisplay` 0x00238560 (draw into frame buffer 0x8C, display 0; `MovieFrameSync` swaps them), `SetMenuDisplayAfterMovie` 0x00238660 (thunks 0x00238770 / 0x00238918).
- Each movie frame resets `RCNT0_COUNT` (0x00238808), runs the player, and at the end **`MovieFrameSync`** 0x00238870 flips the display buffer, **busy-waits until RCNT0 ≥ 0x118 (280 H-blanks)** and increments `ctx->frame_count` itself.
- 280 H-blanks is longer than one field (262.5 lines), so each movie iteration spans **two fields**: `frame_count` +2 per iteration (main loop + `MovieFrameSync`), `scene_frame` +1 (measured in PCSX2, `../pcsx2/sessions-04.md`). The port can treat the wait as "until the next field" (two 59.94 Hz ticks per iteration) as long as decoding stays within one field.
- Movie per page (`CreateMenuMoviePage` 0x00238B80, `StartPageMovie` 0x00238D10): sub 8 plays STAFF (n 0xD, movie 0x1F, no subtitles, then sub-page 0xC); subs 9/10 play story movie n = `ret_sub` (movie n + 1 = SS01–SS12; New Game is n 0 = **SS01**), then page 2 (n 0 and 6: mission title), 0x22 (n 0xB) or 0xB; sub 10 (from the interlude viewer) returns to page 0x14; any other sub plays the INTRO (n 0xC, movie 0, then page 3). With SCREEN RATIO 16:9 the picture is drawn 0x38 lines lower and `DrawMovieWidescreen` 0x00238E88 stretches rows 112–447 over the frame (anamorphic). Movie length per n: s32 frames at 0x003AF880.
- Subtitles: table 0x003A6038 `{count, entries}` per n, entries `{message, f32 start, f32 length}` against the movie time (pictures × `K(0x004102D0)`); `UpdateSubtitles` 0x00238AB0 shows the message box while inside an entry, `UploadSubtitleGlyphs` 0x0022B650 / `DrawSubtitleText` 0x0022C1B0 draw up to 3 lines (glyph sprites in three layers). The INTRO's box is raised (+0 bit 4).
- Skip (`HandleMenuMovieInput` 0x00238D80): after 30 iterations Start stops the movie (`RequestMovieStop(1.0)`: 1 s audio fade), the page fades out over 0.5 s, the menu BGM and stream slots 2–5 fade out.
- Page draw (`DrawMenuMoviePage` 0x00238FE0): 16:9 stretch, subtitles, black fade, then `DrawMovieLineBlend` 0x00238920: the draw buffer copied onto itself one line up with ALPHA 0x64 and FIX 0x68 (flicker filter).
- Port (S3): `src/game/movie_page.cpp`, `src/game/movie.cpp`; the decoder in `src/sce/libmpeg.cpp` (`../formats/SMV.md`). SS01 plays with sound and subtitles; Start skips.

### Interlude viewer (page 20, subs 0x34–0x36)
`src/game/interlude_page.cpp`. `EnterInterludePage` 0x00245A58 (DATA 0xB9, 2 entries, menu BGM 4), `LoadInterludePage` 0x00245B00, `RunInterludePage` 0x00245B50. A row of 12 boxes; left/right move the cursor (`SetInterludeCursor` 0x00245038, kept in 0x00411498); the preview of the resting cursor's interlude grows in over the box (`UpdateInterludePreview` 0x00244F48, `DrawInterludePreview` 0x00245728: still image at VRAM 0x2500 + n · 0x40, sin-shaped growth); texts: number, length mm'ss from 0x003AF880, the boxes (`DrawInterludeTexts` 0x00245390). X fades out and enters page 0x21 with `ret_sub` = the interlude (movie page sub 10); Triangle returns to page 4. Reachable only after a cleared campaign (not tested in the port yet).

### Loading screen (flight scene sub 0x1E)
Drawn by the flight scene but built from front-end code: `func_00239B60` (text and background, menu SE 0xD once, fade by frame count), `func_002396B0` (mission objective text, tables 0x003AEE28 / 0x003AEDE0 by difficulty, typed out), `func_002399E0` (background rectangle), `func_00239310` (progress graphic), `func_00239640` (mission number to show). Full behaviour: `mission-load.md`, "Loading-screen drawing".

### Quirks to preserve
- The records page allocates `((frame_count & 0xF) + 1) · 0x10` bytes before its object (0x002414E0). Work-heap offsets after it depend on the frame count at entry.
- The HDD install page allocates 10 MiB (unreachable).
- Debug `printf("CH0:%08x  CH2:%08x")` when the stream slots are not ready (0x002386D8).
