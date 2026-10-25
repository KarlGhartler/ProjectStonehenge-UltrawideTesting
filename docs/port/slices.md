# Translation slices (proposal)

Status: **order approved by the User 2026-10-13.** Work starts with S1.

Appendix A is the slice 4 inventory (formerly `s4-inventory.md`), appendix B the slice 5 plan (formerly `s5-plan.md`); their section numbers are kept with the prefix A. and B. (`s4-inventory.md` §3 is §A.3 here; the block P items P1–P5 of the slice 5 plan are in §B.4).

Method: ADR-007. Validation: `validation-strategy.md`. Every one of the 2,884 game functions belongs to exactly one slice; the assignment is computed by `tools/research/slices. B as ..py` (writes `out/research/slices.csv`; `--list S5d` lists one slice). Rules of the tool, in order: already translated → S0; dead → S8; pinned VS code → S6; then each slice's roots (sub-state handlers from the scene tables) and their call closure, optionally limited to some subsystems; code reached only through data tables falls back by subsystem. Counts below are from the tool's run of 2026-10-13. They will shift a little as translation finds callbacks the static graph misses; the tool is rerun at each slice start.

LAN play (ADR-008, option A accepted 2026-10-13) is added as **S9**, after S8: it needs the VS slice and determinism proven by the input-recording validation of S5c–S5j.

## Overview

| Slice | Content | Functions | Ends with (visible / playable) |
|---|---|---|---|
| S0 | skeleton (done, milestones A–E) | 244 | boot to the scene 6 stub |
| S1 | scene 6 startup | 87 | splash images, warning screens, memory-card check → request to scene 4 (save present) or scene 2 (no save) |
| S2a | front end: menu framework, title, main menu | 86 | title screen with BGM, main menu navigable |
| S2b | front end: options, controller, BGM player, records, sound options, save/load | 151 | every non-mission menu page works; saves written and loaded (folder card) |
| S3 | movies | 86 | story interludes, opening, interlude viewer play with sound; FSR 1 stage (passthrough) |
| S4 | front end: mission title, briefing, campaign menu, hangar, mission select | 253 | New Game → title card → briefing (movie, subtitles, 3D map) → hangar with 3D aircraft → mission request |
| S5a | mission load and system creation | 291 | loading screen; mission set up in RAM (heap exact) |
| S5b | static world rendering | 157 | the mission world drawn from the start cameras (no simulation input yet) |
| S5c | player flight | 65 | the player aircraft flies: flight model, collision, cameras, replay recording |
| S5d | weapons and effect simulation | 294 | guns, missiles, special weapons, damage; effect pools updated |
| S5e | AI, mission rules and phases, radio selection | 250 | enemies and allies act; objectives, phases, mission end |
| S5f | effect drawing | 208 | explosions, smoke, trails, heat haze, weather visible |
| S5g | HUD | 86 | full HUD: radar, gauges, lock-on, messages, map |
| S5h | mission sound | 49 | engine, weapons, SE, radio voices, BGM in missions |
| S5i | cutscenes and in-engine sequences | 102 | pre-mission cutscenes, mid-mission sequences, no-save intro, opening/ending |
| S5j | replay playback, carrier, game over, rearm | 98 | end-of-mission replay, title demos, carrier landing/takeoff (mission 3), game-over and rearm menus → results request |
| S6 | VS mode | 3 (+ two-player paths of shared code) | two players, split screen, VS rules and results |
| S7 | scene 5 results and post-mission menu | 46 | results/debriefing, post-mission menu; full campaign loop |
| S8 | remaining code | 328 | dead and unreachable code translated; debug menu to reach scenes 1/3; HDD stubs; scene 7 |
| S9 | VS LAN play (ADR-008 A, optional layer) | 0 (no game code) | two PCs play VS over the LAN in lockstep |
| | **Total** | **2,884** | |

Notes on the order:
- It follows the requested order (scene 6 → scene 4 → movies → scene 2 → VS → scene 5 → rest), with scene 4 split around the movie slice: the briefing (S4) plays a movie, so S3 comes before it, while the title and the non-mission menus (S2a/S2b) do not need movies.
- Until S5j, the title screen's idle demo and, until S5i, the no-save intro request scene 2 paths that are not translated yet; validation runs avoid them (a save is present, the title is not left idle).
- Scene 2 sub-slices share one frame (`FlightPlayFrame` 0x0010B150) and one simulation step (`FlightSimStep` 0x0010C320). Until S5d–S5f are done, untranslated parts are stubbed and their RAM regions are `pending-slice` differences; the random-table cursor matches only from S5f on (`validation-strategy.md` §2.1).

## Per slice

Decompile first (User rule 2026-10-15): before any port code of a slice is written, everything the slice uses from the game files is decompiled and documented: every game function it reaches, its VU microcode programs (`../subsystems/vu-programs.md`), and the data formats and tables it reads. Implementation starts only when that inventory is complete.

Common to all slices: `// EE:` comment and `translated-functions.csv` row per function; RAM checkpoint with 0 unexplained ranges; listed screenshots match; tests pass. "PCSX2 batch" = what the User is asked for in one session (exact steps written into `../pcsx2/procedures.md` at slice start).

### S1: scene 6 startup (87)
- **Functions:** `SceneStartup` 0x002613F0 and its 10 sub-states (table 0x003F8708), `StartupSplashUpdate` 0x00260980, card evaluation `func_00260488`/`func_00260288`, message windows; shared: text renderer (17), sound init/SE calls (21), 2D render helpers (12), libmc wrappers (4).
- **Renderer:** first real GS path: GIF PATH2/PATH3 packet intake from the DMA lists, IMAGE uploads (PSMCT32/PSMT8/PSMT4 + CLUT), sprites and triangles with texture, alpha blending, scissor, fades; frame/display registers; field copy executed but not presented (ADR-006 §4). Native screenshot in the overlay.
- **Audio:** SOUND.IRX HLE core (SIF RPC 0x81000101 command batches → SPU2-subset mixer, ADPCM voices, ADSR) for menu SE; voice status advanced per step.
- **Input:** existing (confirm button on the card messages).
- **sce:** libmc on the folder memory card (`sceMcGetInfo`, `Open`, `Read`, `GetDir`, `Close`; port 2 empty), `../formats/folder-memcard.md`.
- **Visible:** splash images, warnings, card check messages; with a save, the request to scene 4; without, the request to scene 2.
- **Validation:** RAM at sub 7 (first hit), sub 9 and both exits for card S (save), card E (unformatted: empty superblock) and no card; screenshots of both splash images and of the card messages. Card messages appear **only with no card** (two windows); card E (unformatted) shows none and goes to the intro (`../pcsx2/sessions.md` run 06).
- **PCSX2 batch:** done 2026-10-14 (`../pcsx2/sessions.md` run 06): 3 cold boots, 6 save states, 5 screenshots, card folders S and E.

### S2a: menu framework, title, main menu (86) — translated and validated 2026-10-14
- **Functions:** `SceneMenu` 0x002262D0, subs 0–6 (0x002261B8, 0x00226248, 0x0022DE00, 0x0022DEA0, 0x0022DF00), menu tree, crosshair cursor, panels, message box, fade object, data load queue `QueueMenuDataLoad` 0x0022C618, menu BGM (`PlayMenuBgm`), overlay passes.
- **Renderer:** frame-buffer feedback for the scanline/noise copy (0x00227F30), letterbox bars; text rendering at scale.
- **Audio:** VAG.IRX HLE (RPC 0x81000102, 6 stream slots) streaming BGM from `VAG/*.VPK` on the ISO; crossfades.
- **Visible/playable:** title screen with BGM, PRESS START, main menu navigation (entries leading to later slices end in a logged stub).
- **Validation:** RAM at title sub 6 entry (0x0022DF00), Start accepted (call site 0x0022D748), menu active (call site 0x0022D84C, first hit) and first confirm (call site 0x0022D880); screenshots of title, main menu and the opened item (4:3). BGM audibly correct (no RAM effect beyond slot state). The 16:9 screenshots move to S2b: the save's options are loaded only through the main menu's load path (S2b).
- **PCSX2 batch:** run 07 (`../pcsx2/sessions.md` run 07): 4 save states, 3 screenshots. Result: 4 RAM checkpoints with 0 unexplained ranges, screens without structural differences (`validation.md` S2a-3, S2a-4); open: blend rounding of the feedback filter (S2a-R), music by ear.

### S2b: options, controller, BGM player, records, sound options, save/load (151)
- **Functions:** scene 4 subs 41–51, 55–61, 74–89: options page and handlers (`func_0023F350`…`func_002402E0`), controller settings (both pads), BGM player, records, sound options, save/load frame and `SavePageUpdate` 0x0023D198, statistics clamp 0x00105448; HDD branches of the save page (unreachable at run time, translated).
- **Renderer:** nothing new beyond S2a (16:9 layout paths).
- **Audio:** option changes (category volumes, mono/stereo).
- **sce:** libmc write path (`sceMcOpen/Write/Flush/Close/Delete/GetDir`), byte-identical save image (`../formats/SAVE.md`).
- **Playable:** all non-mission menu pages; save and load.
- **Validation:** RAM at each page's first run step and after leaving it with changed options (`MenuGoTo` 0x00226450); RAM after a card load (0x0023D4FC); screenshots per page, 4:3 and 16:9. The save page enters **save** mode only from sub-pages 0x4A–0x4E (campaign menu, post-mission and mission-select pages, later slices); main-menu LOAD is sub-page 0x4F (load mode). So the byte-identical save check (folder card round trip both ways, `../STATUS.md` §2) moves to S4, the first slice that reaches a save sub-page. The write path is translated here.
- **PCSX2 batch:** written 2026-10-14 (`../pcsx2/procedures.md` item S2b): 12 save states, about 10 screenshots, button sequences per page.
- **Status (2026-10-14):** translated 151/151 and validated (`validation.md` S2b-1/S2b-2: 9 RAM checkpoints 0 unexplained, 10 screens). The BGM player reference was captured 2026-10-17 (`../pcsx2/sessions.md` run 11 S2b-B, card with a cleared campaign); its port comparison is pending.

### S3: movies (86)
- **Functions:** in-menu player subs 7–13 (`MenuMovieEnter` 0x002390C8 … `MenuMovieEnd` 0x002392A8), interlude viewer subs 52–54, `StartMenuMovie` 0x00238778, `UpdateMenuMovie` 0x00238810, `MovieFrameSync` 0x00238870, the SMV reader state machine (0x00212630–0x00212960), decode/draw `func_00212F88`/`func_00213048`, subtitle pages, audio feeder `func_00213CE8`.
- **sce:** `sceMpegInit/Create/AddBs/IsEnd/Reset/GetPicture` on **libmpeg2** (approved; fetched when this slice starts), RGBA32 output into the guest image buffer; timer 0 H-blank count (RCNT0 mode 0x83) modelled as two ticks per iteration.
- **Renderer:** frame mode 640×448 display; the **movie upscaling stage** (ADR-006 §5): component interface + FSR 1 EASU (+RCAS) implementation, passthrough at 1×, exercised at 2× through a development-only override for testing.
- **Audio:** SMV PCM blocks into the SPU2 core-0 sound-data input via the SOUND HLE (block transfer); confirm the 0x200-byte L/R interleave first (`../formats/SMV.md`); MIS movies use VPK stream tracks.
- **Visible:** opening/interludes (SS01–SS12, INTRO, STAFF from the interlude viewer) with sound and subtitles; each picture held exactly 2 ticks.
- **Validation:** RAM at a fixed movie picture (scene_frame N of SS01); screenshots of 3 pictures (640×448 and 640×336 letterboxed); PCSX2 SPU2 audio capture of the first seconds of SS01 for the interleave; FSR 1 output checked visually at 2×.
- **PCSX2 batch:** written 2026-10-14 (`../pcsx2/procedures.md` item S3): states at the first picture, a mid picture and the end of SS01, 3 screenshots, a sound capture.
- **Status (2026-10-14):** translated, 86/86 (`slices.py --list S3` is empty; 655 in total). libmpeg2 0.5.1 added (FetchContent, approved). SS01 plays in the port from New Game with sound and subtitles, 2 vblanks per picture, Start skips (`validation.md` S3-1). Fixed after the User's first look: seams at the 16-pixel movie columns (GS texture coordinates now rounded to 1/16 texel) and corrupt pictures at chunk changes (open GOPs: decoder references kept over `sceMpegReset`), `validation.md` S3-1. Movie upscaling stage built (User decision 2026-10-14, option b: decoder side path, ADR-006 §5): FSR 1 EASU at the internal-resolution factor, a passthrough at 1×; checked at ×2/×3 with `--movie-upscale`. Its use in the draws waits for the scaled GS backend. Validated 2026-10-15 against PCSX2 run 09 (`validation.md` S3-3 to S3-5): 3 RAM checkpoints 0 unexplained, 2 screens, sound capture. Not done: the interlude viewer test (needs a cleared campaign).

### S4: mission title, briefing, campaign menu, hangar, mission select (253)
- **Functions:** scene 4 subs 14–36 and 65–73: `MissionTitleRun` 0x00230790, briefing (`CreateBriefing` 0x002365C0, subtitle timing, 3D map 0x0023EE20), `CampaignMenuRun` 0x00230FC8, `HangarEnter`, `AircraftSelectRun` 0x00235AB8, `CommitHangarSelection` 0x00233F10, `CreateAircraftViewer` and the menu 3D object API (0x0014AE60–0x0014B5A0), `WriteLightSetPacket` 0x001370D8, unlock rules, mission select (`MissionSelectAction` 0x0023AC70, `func_0023A668`).
- **Renderer:** first **VU1 program HLE** (program A, `../subsystems/vu-programs.md`) for models, lighting, the reflection and shadow passes; VU0 macro-mode math (`vu0_math.h`); point sprites for the map.
- **Visible/playable:** New Game → mission title card → briefing → campaign menu → hangar with the rotating aircraft → `RequestScene(2)` (ends in the scene 2 stub until S5a).
- **Validation:** RAM at briefing map view, at the hangar with aircraft N, and at the scene-2 request (game state block exact); screenshots of title card, briefing map, hangar viewer (two aircraft, two colours).
- **PCSX2 batch:** 4 save states, ~8 screenshots; test vectors for the hangar credit/ownership commit. Written 2026-10-15 (`../pcsx2/procedures.md` item S4).
- **Status (2026-10-16):** translated and validated against PCSX2 runs A–C: all S4 RAM checkpoints 0 unexplained (`validation.md` S4-1 to S4-7), `CommitHangarSelection` byte-exact test vector, hangar at 60 fps (S4-6). 29 functions (menu-object effect drawers, glow pass) are not reached in S4 and move to block B of the slice 5 plan (appendix B).

Plan for all of slice 5 (blocks P, A = S5a+S5b, B = S5c–S5h, C = S5i+S5j, and why): appendix B (proposal 2026-10-16, awaiting the User).

### S5a: mission load and system creation (291)
- **Functions:** scene 2 subs 0–8 and 0x1E: `FlightSceneEnter` 0x0010A898, `StoreFlightDataPointers` 0x0010B038, engine sound banks `func_0010FDE8`, loading screen 0x0010A998, `CreateFlightSystems` 0x0010AA50, `StartFlightSystems` 0x0010AB58, mission setup `func_0010FA70` / `SetupMissionUnits`, and every create/init function they reach (pools of effects, weapons, HUD, replay, radio).
- **Why first:** the work heap is 98.4 % full in mission 1 and allocations are unchecked; every allocation size and order must be exact before anything runs (`../game/memory.md`).
- **Visible:** the loading screen with objectives; the mission ready in RAM.
- **Validation:** RAM at the first step of sub 0xA for 3 missions (1, 3, 10) — heap mark +0x1664 and every pool header exact; screenshot of the loading screen.
- **PCSX2 batch:** 3 save states, 1 screenshot.

### S5b: static world rendering (157)
- **Functions:** world/terrain update and draw (0x00139FE8, 0x00144CC0, 0x0013EC20/0x0013EC38), sky, sea, clouds, model rendering, shadows, the frame's packet build and ordering tables, `UpdateViewCameras` 0x0010CB80 and the camera library.
- **Renderer:** the full mission draw path: VU1 programs (all 5 as CPU HLE), ordering-table buckets, trilinear mipmapping, CT24, Z24 sampled as texture, destination-alpha blending (`../platform/gs-subset.md`), fog.
- **Visible:** the world from the start cameras, with the simulation frozen at its first step (development option), or with S5c stubbed input.
- **Validation:** RAM after the first frame's draw-list build (draw-phase random reads such as the sky, `../game/random.md`); GS-level comparison with a PCSX2 GS dump of the same frame (`gsdump.py`); screenshots of 3 missions' first frames.
- **PCSX2 batch:** 3 save states + 3 GS dumps of the first gameplay frame, 3 screenshots.

### S5c: player flight (65)
- **Functions:** flight model (`flight.md`), landing gear, controls, collision (terrain, objects; `func_0017C020`/`func_0017C688` with the `cfc2` delay rule, `validation.md` B-2), double-precision damping, `RecordReplayFrame` 0x0016E8B8 (recording runs inside the simulation).
- **Input:** analog sticks and pressure buttons exactly as `scePadRead`; first **input-recording replay** (`.p2m2`, `../formats/p2m2.md`), anchor calibration.
- **Playable:** fly the player aircraft (other subsystems stubbed).
- **Validation:** test vectors for the flight model step and collision (entry/exit states); a 60 s recording of free flight in mission 1 with checkpoints every 10 s comparing the player unit, camera and replay ring (rest `pending-slice`); determinism checker port-vs-port.
- **PCSX2 batch:** 1 recording + 6 checkpoint save states; 6 entry/exit pairs for test vectors; the pad-latency calibration recording.

### S5d: weapons and effect simulation (294)
- **Functions:** `UpdateProjectiles` 0x0014BB40 and the 7 pools, special weapons (13 init functions via jump table 0x0014FB88), damage `ApplyAreaDamage` 0x00153D88, effect pool updates and spawns with their `NextRandomFloat` reads (`../game/random.md` appendix), slot rules.
- **Validation:** test vectors for slot allocation and each random-reading spawn; recording of a firing run (guns, missiles, special weapon) with checkpoints; projectile and effect pools exact.
- **PCSX2 batch:** 1 recording + 4 checkpoints, ~8 entry/exit pairs.

### S5e: AI, mission rules and phases, radio selection (250)
- **Functions:** unit AI (`UpdateAITarget` 0x0012B540 …), mission rules `CheckMissionObjectives` 0x001191B8, phase change `func_0010FB30`, `MissionEndRules` 0x00119910, event script, proximity triggers (libc `rand`), radio message selection (probability table 0x00409DA8).
- **Playable:** complete missions (silent, effects not drawn).
- **Validation:** recordings of missions 1 and 3 to the end with checkpoints at phase changes; test vectors for target selection and the radio choice.
- **PCSX2 batch:** 2 recordings + ~8 checkpoints, ~6 entry/exit pairs.

### S5f: effect drawing (208)
- **Functions:** all effect draw routines (sprites, trails, smoke ribbons, explosions, heat haze `func_001E88C0`, weather), frustum clip setup `func_001FF818`.
- **Renderer:** framebuffer-as-texture feedback (heat haze), remaining blend modes.
- **Validation:** from here the full RAM must match at every checkpoint (random cursor included); GS dumps of 2 effect-heavy frames; screenshots.
- **PCSX2 batch:** 2 save states + 2 GS dumps, 4 screenshots; reuse S5d/S5e recordings for full-RAM checkpoints.

### S5g: HUD (86)
- **Functions:** `UpdateHud` 0x0015E928, HUD draw (radar, gauges, ladder, lock-on boxes, messages `ShowCenterMessage` 0x0016DDB8, map), 16:9 HUD variants, text for HUD.
- **Validation:** screenshots of HUD states (lock-on, warning, map, messages, 16:9); RAM at checkpoints.
- **PCSX2 batch:** ~6 screenshots from the existing recordings.

### S5h: mission sound (49)
- **Functions:** engine sound (`func_001279B0`, double-precision throttle compare `func_00215D20`), positional SE (`func_00216D88`), radio voice playback through VAG slots, mission BGM.
- **Audio:** 3D SE panning/volume, stream slots under load, voice-status timing per step (affects radio queues).
- **Validation:** RAM (SOUND/VAG status bytes now real); audio compared by ear and with a PCSX2 SPU2 capture of a short segment.
- **PCSX2 batch:** 1 audio capture + 2 save states.

### S5i: cutscenes and in-engine sequences (102)
- **Functions:** sequence engine and character engine (`sequence` subsystem), mid-mission sequences 0x0010B500, sub 0x13 specials, pre-mission cutscenes (`UpdateInEngineSequence` 0x00177058), VU0 micro-mode skinning (C++ rewrite, decision 14), the no-save intro.
- **Validation:** skinning test vectors (part types 0/1/3, `../pcsx2/procedures.md` #1); RAM at fixed sequence frames; screenshots of the no-save intro and a pre-mission cutscene.
- **PCSX2 batch:** the pending skinning vectors + 3 save states + 4 screenshots.

### S5j: replay playback, carrier, game over, rearm, draw-only frame (98)
- **Functions:** `UpdateReplayPlayback` 0x0016E2D0 and replay cameras, title demos (`StartTitleDemo` 0x0022CC68 path), carrier landing/takeoff (mission 3, subs 0x14–0x1B), game-over menu, rearm menu, sub 9.
- **Playable:** full missions end to end including the replay → `RequestScene(5)`; title demos.
- **Validation:** replay of mission 1 end (replay restores u8-quantised snapshots); title demo run from the title idle (deterministic from the title); carrier sequence recording.
- **PCSX2 batch:** 2 recordings + 4 checkpoints.

### S6: VS mode (3 VS-only functions + the two-player paths of 66 shared functions)
- **Functions:** `func_00118CC0`, `func_00119000`, `func_00167D68` (pinned), plus validation of every `game[0x25]`/`[0x26]` branch in shared code (`slices.py --vs`), the VS stage list in mission select.
- **Input:** pad port 2 from the second gamepad (exists); recordings with both ports.
- **Renderer:** two views side by side (scissor/viewport per view).
- **Playable:** two-player VS on all 8 stages (`../subsystems/vs-mode.md`).
- **Validation:** 2-port recording of one VS match to the end (score win and time-out), checkpoints; split-screen screenshots at 4:3 and 16:9.
- **PCSX2 batch:** needs a second controller (or a keyboard mapping for pad 2) in PCSX2; 2 recordings + 4 checkpoints, 3 screenshots.

### S7: scene 5 results and post-mission menu (46)
- **Functions:** `SceneResults` 0x00246E18 and its 7 sub-states (table 0x003B0190), result record use, rank computation, `ResultsExit` 0x0024CA28 → `EnterPostMissionMenu`; scene 4 subs 37–40 (post-mission menu: next mission, replay, save).
- **Playable:** the full campaign loop mission → results → story movie → next mission, with saving.
- **Validation:** RAM at the results screen and after `ResultsExit` for a campaign mission, a trial and a VS match (mode 0/2/3); screenshots.
- **PCSX2 batch:** 3 save states, 4 screenshots.
- **Status (2026-10-18):** all 46 functions translated (`src/game/results_scene.cpp`, `src/game/post_mission_page.cpp`); not yet validated (batch S7 in `../pcsx2/procedures.md`: test vectors and screenshots, since the port cannot reach the results on its own yet).

### S8: remaining code (328)
- **Functions:** all dead code (272: scenes 1 and 3, `SetReturnScene`, unreached helpers, dead sound/sequence/frontend functions, `func_0022D030` debug VS setup), the HDD layer and HDD install page (scene 4 subs 62–64), scene 7 `SceneHddError` 0x00261680, file-layer helpers (`InternHostPath`), and anything the tool still assigns by fallback.
- **sce:** HDD SDK functions reached only from dead code: stubs that log once and return an error (CLAUDE.md).
- **Debug layer:** a debug menu entry to enter scenes 1 and 3 (`../port/port-features.md` §8 "Jump to scene"), marked as modifying the session.
- **Validation:** default build never reaches them (scene request log); build and unit tests; `dead-code.md` updated with each function's translation.
- **PCSX2 batch:** none (dead code cannot be reached in PCSX2 without patches).
- **Status (2026-10-19):** all functions translated (`slices.py --list S8` empty; 2,888 translated functions in total, 269 of them dead). Default build unchanged (mission 1 smoke run). The debug menu entry for scenes 1 and 3 is not built yet (needs the User's go-ahead). Details: `dead-code.md` "S8 translation".

### S9: VS LAN play (no game code; ADR-008 option A)
- **Port:** netplay layer in `src/platform/` between the input backend and `scePadRead` (input queue per port, step gate in the game thread), UDP sockets (no dependency unless the User approves one), LAN discovery, handshake (ISO SHA-1, build hash), transfer of the host's memory card image for the session, per-step RAM hash exchange and desync stop, optional crop of the output pass to the local player's half. Opt-in, off by default.
- **Validation:** two port instances on one PC and on two PCs run a VS match in lockstep with identical hash streams; a recorded 2-port PCSX2 VS match replayed on both sides gives the same checkpoints as S6.
- **PCSX2 batch:** none beyond S6.

## Results per slice

Slice reports as they were written into `../STATUS.md` (moved here 2026-10-20; newest last). Validation records: `validation.md`.

S1 result (plan approved 2026-10-14, reference data `../pcsx2/sessions.md` run 06):
- 87/87 functions translated (`tools/research/slices.py --list S1` is empty; 331 game functions translated in total).
- RAM: 6 checkpoints (card S sub 7, sub 9, exit; card E sub 7, exit; no card sub 7) with **0 unexplained ranges**; remaining differences are frame timing (CD latency model, `ramdiff-expected-s1.csv`) and the User's controller stick. First SceneStep check still clean.
- Screens: splash 1 (card S), splash 0 (card E) and the first no-card message match PCSX2 at offset 0 (mean difference 0.2-0.6 per channel; edge differences from PCSX2's field output). The second no-card message matches too (reached with `--pad-script`). Not compared: splash 0 of card S (PCSX2 screenshot taken during the fade-in).
- New: first GS renderer path (2D: sprites, lines, CLUT textures, blending, fades), libmc on folder cards, SOUND stand-in matched to SOUND.IRX (reply area, transfer record, reverb address), SPU2-subset mixer clocked per game step, native screenshot (F8, `--screenshot-at`), `--memcard`/`--no-memcard`/`--dump-ram-at`/`--dump-audio`, `tools/research/imgdiff.py`. 56 unit tests pass.
- User feedback 2026-10-14: audio OK; controller input missing with `--no-memcard` -> gamepads are now opened at start-up (recheck by the User); the brightness option's effect is now presented (ADR-006 §4 note).
- Open: PCSX2 I/O timing differs by a frame between cold boots, so exact frame-count matching needs an I/O latency model (`open-questions.md`).

S2a result (2026-10-14, reference data `../pcsx2/sessions.md` run 07):
- 86/86 functions translated (`slices.py --list S2a` is empty; 418 translated in total): scene 4 handler and navigation, menu framework (list widget, menu tree, connectors, message box, fade object, data load queue, filters, tabs), menu BGM, attract demo, title screen and main menu. Pages of later slices end in a logged stub (46 sub-state handlers).
- Visible: title screen, PRESS START, main menu tree with help line, sub-columns. Music: title and menu tracks stream from `VAG/*.VPK` through the new VAG.IRX stand-in (channels driven by the mixer's SPU2 IRQ, as on the PS2).
- Renderer: destination-alpha blend `(Cs−0)·Ad+Cd`, alpha-test fail modes, exact bilinear texel footprint (fixes an atlas overflow).
- Fixes found on the way: SOUND voice record offsets (+0x0E voice, +0x10 flags; the S1 stand-in played every SE on voice 0) and SOUND voice status from `get_ch_stat` (ENDX/ENVX). S1 checkpoints still 0 unexplained (`validation.md` S2a-1).
- 70 Ghidra renames (65 functions, 4 globals, 1 table), 2 plate comments.
- Validation (`validation.md` S2a-3 to S2a-5): 4 RAM checkpoints with 0 unexplained ranges (`ramdiff-expected-s2a.csv`: CD latency, frame counter, music time, input timing); 3 screens without structural differences (PCSX2 field blend modelled by `imgdiff.py --blend-fields`). Open: a −1 to −2 level G/B bias of the menu frame filter (blend rounding, `../pcsx2/procedures.md` S2a-R) and the music by ear.
- Found by validation: PCSX2's `add.s`/`sub.s` mask the smaller operand (`FPU_ADD_SUB`); `fpu::Add`/`Sub` model it and, by the User's decision, all 83 float add/sub-type instructions of the translated code (30 functions) use them. S1 bug from the User: PSMCT24 image transfers lost the bytes of a pixel split across GIF packets (green band on the AC04 PROJECT splash); fixed. S1 checkpoints still 0 unexplained; 59 unit tests pass.

S2b result (2026-10-14, reference data `../pcsx2/sessions.md` run 08):
- 151/151 functions translated (`slices.py --list S2b` is empty; 569 translated in total, `translated-functions.csv`). New files: `campaign.cpp`, `results.cpp`, `save_mc.cpp`, `save_page.cpp`, `options_page.cpp`, `records_page.cpp`, `controller_page.cpp`, `sound_page.cpp`, additions to `menu_widgets.cpp`, `menu.cpp`, `options.cpp`; HDD branches in `hdd.cpp` (unreachable, `dead-code.md`).
- sce/platform: libmc write path (`sceMcOpen/Close/Read/Write/Flush/Mkdir/Delete/Format`) on the folder card, `_pcsx2_index` written like PCSX2 (`../formats/folder-memcard.md` §10.1); request delay 1 vblank (hypothesis until calibrated).
- Visible in the port: SCREEN SETTINGS (list, screen position, brightness patterns), CONTROLLER SETUP, SOUND SETTINGS (changes applied at once), PERSONAL DATA, LOAD (load, then CONTINUE in the main menu). Not reachable with the available cards: the BGM player (needs a cleared campaign). Save mode starts only from later-slice pages, so the byte-identical save check moves to S4.
- EE FPU: PCSX2's add/sub operand masking reproduced in `fpu::Add/Sub` and applied to all translated code (User decision 2026-10-14; `validation.md` S2a-3); `tools/research/fpucheck.py` checks the count per function. 60 unit tests pass.
- About 140 Ghidra renames logged (`rename-log.csv`, evidence "(S2b)").
- Validation (`validation.md` S2b-1, S2b-2): 9 RAM checkpoints with 0 unexplained ranges (`ramdiff-expected-s2b.csv`: frame parity, CD latency, input timing, and extra LOAD visits in the User's run, confirmed); 10 screens with the same content, including 16:9 and CONTINUE after LOAD. The reddish rectangle and the empty usage bars are in PCSX2 too (original). BGM player: PCSX2 reference captured 2026-10-17 (`../pcsx2/sessions.md` run 11), port comparison pending. Not checked: the libmc request delay (the `s2b_load_done` breakpoint was placed after the read it was meant to catch; procedure error).
- S2a-R: PCSX2's software and hardware renderers agree; the port's menu frame filter differs in a radial pattern (centre G/B −1.5/−2.8, edges +1 to +3), not a uniform rounding bias. Open (`open-questions.md`).

S3 result (translation 2026-10-14, validation 2026-10-15 against PCSX2 run 09, `../pcsx2/sessions.md` run 09):
- 86/86 functions translated (`slices.py --list S3` is empty; 655 in total). New files: `movie.cpp` (SMV reader, picture upload, PCM feeder, arena), `movie_page.cpp` (in-menu movie pages, subtitles, display modes, frame sync), `interlude_page.cpp` (extras interlude viewer), `src/sce/libmpeg.cpp`.
- New dependency in use: libmpeg2 0.5.1 (approved 2026-10-11, fetch confirmed by the User 2026-10-14; FetchContent with SHA-256, built as portable C).
- sce/platform: sceMpeg* on libmpeg2 with the IPU colour conversion; sceSifSetDma/sceSifDmaStat into IOP RAM; SOUND command 0xA with destination 0 plays a loop-mode PCM block input in the mixer, command 0xD sets its volume, the reply reports the half being played; timer 0 busy-wait model (`EeHwWaitTimer`); `--exit-at` (development builds).
- Visible: New Game → SS01 with sound and subtitles at 30 pictures/s, Start skips; the next page (mission title) is slice S4.
- Ghidra: 85 renames and 9 plate rewrites logged; `sceMpegAddBs`/`sceMpegGetPicture` were swapped in the earlier hypotheses (0x00270B58 adds the bitstream, 0x00270B90 decodes a picture).
- Fixed after the User's test: vertical seams in movies (GS texture coordinates rounded to the GS 1/16-texel grid; also slightly closer to PCSX2 on the S2a screens) and flickering corrupt pictures at SMV chunk changes (open GOPs; libmpeg2 partial reset keeps the references).
- Movie upscaling stage (ADR-006 §5, option b decided 2026-10-14): the decoder hands each picture to the renderer through a side path, only when the factor is above 1. FSR 1 EASU runs at the internal-resolution factor (×2 per pass) and is a passthrough at 1×. Draws from the movie buffer are identified through DMA source marks. Dev option `--movie-upscale <2-4>`. Sampling the output in the draws comes with the scaled GS backend (internal resolution work).
- Validation (`validation.md` S3-3 to S3-5): 3 RAM checkpoints (movie start, mid-movie at picture 662, movie end) 0 unexplained with the new `ramdiff-expected-s3.csv`; mid-movie to end 7028 frames on both sides; image buffer the same picture (IDCT rounding, max 10 levels); 2 screens matched (0.8 and 2.4 mean); sound correlates 0.97-0.995 per channel with the PCSX2 capture, no drift. Confirmed: IPU alpha 0x80, PCM 0x200-byte L/R blocks, the SOUND reply's audio half (`GetMovieAudioHalf` renamed in Ghidra). New research tool `tools/research/flac_mp4.py` (decodes PCSX2's FLAC/MP4 audio capture).
- Open: the interlude viewer test (needs a cleared campaign); libmpeg frameCount/flags rule (no game effect).

S5 block B translation (2026-10-18; validation against the S5-B captures still to do):
- Every game function of S4 and S5a–S5h now has a translation (`tools/research/slices.py --list`, compared with the `// EE:` markers: 0 missing; the only S5a entry left is the CRT helper 0x001001C0). 2,323 translated functions in total (`translated-functions.csv`, regenerated; `translated.py` now finds names behind long comments).
- Last pieces: the special-object drawers of `DrawSpecialObject` 0x001E0A98 (kinds 12/13 searchlight 0x001AD510, 20 fire 0x001AFDF8, 21 burning wreck 0x001B1BD8, 23–25 oil fire 0x001B3C58, 36 rocket launch 0x001B9C90, 37 smoke column 0x001B5AC8) and the S4-deferred half-resolution glow pass (`DrawGlowPass` 0x00142460 with `BuildScaledCameraMatrices` 0x001420F0 and its composite packet 0x00141EE0), wired into the aircraft drawer and the menu object (whose call now passes the 9th argument 0x20 as the original does).
- VU1 program D (glow fins) implemented as CPU HLE in `vu1_programs.cpp` from its listing (`../subsystems/vu-programs.md`). Not yet seen running: the glow pass needs the map flag +0xF4 bit 1 (external or demo views); its GS output is unchecked.
- `fpucheck.py` clean for all translated code (the drawers carry `verified N` notes where the original repeats or unrolls adds).
- Smoke runs: missions 1, 3, 4, 7, 9 with the standard pad script exit cleanly at vblank 2610.

S5 block C translation (2026-10-18; validation against the S5-C captures still to do):
- All 194 functions of S5i/S5j translated (`s5-blockC-inventory.csv`, every row `translated`): in-engine sequences and their draw passes (`sequence.cpp`), the sequence character engine (`sequence_model.cpp`), replay playback and cameras (`replay.cpp`, `replay_camera.cpp`), scene 2 sequence frames, carrier landing / takeoff, snapshots and the flight record (`flight_sequences.cpp`), projectile drawing and the sequence effects (`projectile_draw.cpp`), positional sound (`mission_sound.cpp`), the subtitle track selector (`radio.cpp`). 2,517 translated functions in total.
- New platform / SDK pieces: VU0 micro mode for program S (`src/platform/vu0/vu0_hle.cpp`: register file, data memory, VIF0 chains with MPG / UNPACK, the nine entries), the VIF0 DMA registers with a start hook (`ee_hw.h`, `hw_registers.cpp`), `sceDmaSync`, `tanf` / `__kernel_tanf`, `sceVu0RotMatrix` / `CameraMatrix` / `NormalLightMatrix` / `LightColorMatrix`. Renderer: an empty or inverted texel range (seen once in the opening) now falls back to the whole texture instead of a huge allocation.
- Wired: `DrawProjectiles` and the sequence draw passes in `BuildFlightDrawLists`, the follow cameras of `func_00175B08`.
- Stubbed: only S6 (VS rules 0x00118CC0, 0x00119000). `fpucheck.py` clean (new `verified N` notes where the original inlines or duplicates adds).
- Smoke runs: the opening of mission id 0x25 plays (carrier deck, F-4E, skinned deck crew) and Start skips it into mission 1; mission 12 plays its scripted sequence into gameplay. Not seen yet: replay, carrier landing, game over, title demo (S5-C recordings).

S7 translation (2026-10-18; validation against the S7 batch in `../pcsx2/procedures.md` still to do):
- All 46 functions translated: scene 5 (`results_scene.cpp`: the 7 sub-states, `ResultsUpdate` with the kill tally and the replay of the recorded unit tracks on the results map, the map / unit / trail / marker / text draw passes, the debriefing voice, `EnterPostMissionMenu`) and the post-mission menu (`post_mission_page.cpp`, scene 4 subs 0x25-0x28). `SceneStub<5>` and the three menu stubs removed; 2,563 translated functions in total. Details: `../subsystems/results-startup.md`.
- `fpucheck.py` clean (`verified 13` on `ResultsUpdate`: the angle wraps share a helper).
- Not seen running: the port cannot reach the results screen yet. Replaying `b3_m4_full.p2m2` over a `--mission 4` start (start press added before it) diverges and does not complete the mission (the `--mission` route's RNG and heap differ from PCSX2's). Validation needs the S7 test vectors and screenshots.

S8 translation (2026-10-19):
- All S8 functions translated (`slices.py --list S8` empty; 2,888 translated functions, 269 dead): scenes 1 and 3 (`movie.cpp`, the scene stubs are gone), the HDD layer, scene 7 and the HDD install page, the `C_PCCON` profiler (`perf_counter.cpp`), the host-file debug I/O, the unused save, core, render, effect, menu, text, radio and replay functions. Catalogue: `dead-code.md` "S8 translation".
- New SDK stubs (reached only from dead code, log once, return an error): libpc (`scePcStart`, `scePcStop`, counter reads), `sceMcUnformat`, `sceMcChdir`, `sceIoctl`.
- Live-code fix found on the way: the glow alpha of `func_001F3D38` divides by span x (span x 4) (the translation had span x 2 - n); the related open question is closed.
- `fpucheck.py` clean for the new functions; ctest passes; mission 1 smoke run (`--mission 1`, card S) reaches gameplay as before.
- Not done: the debug-layer entry to scenes 1 and 3 (port code, waits for the User). S6 and S9 skipped as instructed.

Internal resolution, 16:9 and clean-up (2026-10-19, User request):
- Internal rendering resolution 1-8 (`presentation.internal_scale`, `--internal-scale`; baseline feature, `../decisions/port-baseline.md`): scaled GS backend (per-target scale, scaled scissor/DATE/depth fills/frame-as-texture reads, native-grid UVs for 2D sprites, native-resolution format conversions, movies from the FSR 1 stage). Design and as-built table: `renderer.md` §1.7.
- 16:9: the output pass now follows the game's SCREEN RATIO option (ctx flag 0x10000); before, the port always presented 4:3 (ADR-006 implementation note).
- Validation (`validation.md` IR-1 to IR-4): new suite `tools/research/full.json` (30 jobs, baseline from the build before the session): at 1x every RAM checkpoint, hash log and screenshot identical; at 2x/4x/8x every RAM checkpoint and hash log identical. All PCSX2 screenshots of the Slices folder and the three GS dumps compared again with `tools/research/pcsx2_compare.py` at 1x (unchanged from the slice results), 2x, 4x and 8x. 8x keeps 60 fps in the hangar (0 dropped ticks).
- Clean-up and speed (no logic change, proven by IR-1): texture decoding about 2x faster (`GsMemory::ReadRect`, 18 % -> 8 % of samples in gameplay); 91 lines of duplicated guest helpers in 29 game files replaced by `src/game/guest_helpers.h`; explicit `/fp:precise` (approved 2026-10-13, was missing).

Opening fixes, port menu features, caches and leaks (2026-10-20, User request; `validation.md` §2l):
- Mission 1 opening: the corrupted frames (black with a garbage strip, speckles over the sea) were a translation error in `func_002206C0` (the sequence characters' GIF tag at part +0, not +0x70; checked in the disassembly); the black F-4E was the renderer reading a T8H texture from a frame buffer whose top byte no draw writes (FBMSK 0xFF000000). Both fixed.
- New port features (baseline, approved by the User's request, `../decisions/port-baseline.md`; as built: `port-features.md`): GRAPHICS page under SCREEN SETTINGS (fullscreen, v-sync, frame cap, internal resolution, filter); Modern control types on CONTROLLER SETUP (L1 / L2 and R1 / R2 swapped by the input layer, Classic type kept in the save data, default NORMAL (MODERN) for both players); `settings.json` gets `version`, `presentation.window_mode` `fullscreen`, `input.modern_controls`, and an unreadable, invalid or incompatible file is replaced with the defaults. The game's own options stay in its save data.
- Performance and memory: textures re-decoded only in the GS blocks written since their last decode; decoded-texture cache bounded at 256 MiB (it reached about 6 GB in flight); the Vulkan backend reuses its per-frame lists. Mission 4 replay: 0 dropped ticks (was 35-37), main thread 19.8 % → 15.9 % of samples.
- Leaks: none found (code scan, Vulkan validation at shutdown); new Debug-only CRT leak tests and a cache-budget test.
- Regression suite against the 2026-10-19 baseline: identical except the opening (both fixes), the end of `s4_frontend` (the opening) and the known loading-fade column; new baseline `out/runs/baseline_s9`.

Texture residency, settings, sprite edges, frame-rate overlay, docs (2026-10-20, User request; `validation.md` §2m):
- Renderer: decoded textures stay in the GPU atlas across frames; an incremental decode updates its predecessor's slot with the changed rectangles only. Uploads 3 MB → 18 KiB per frame in the mission 1 opening, recording 0.26 → 0.10 ms in mission 4. Sprite edges on native pixel borders at N > 1 (fix 9): removes the 32-pixel seams of the glow pass at 2× and above. Regression at 1× and 4×: RAM and hash logs identical.
- Settings: defaults from the start display (fullscreen; v-sync on at 60 / 120 Hz, else off with the matching cap); GRAPHICS changes apply at once (ACCEPT saves, CANCEL restores, DEFAULT = display defaults); the frame cap row is disabled with v-sync on and the cap is not used then; the game's SCREEN RATIO starts at 16:9 on a wide monitor when no save is loaded (validation runs keep 4:3). New keys `presentation.default_screen_ratio`, `debug.fps_overlay`.
- Frame-rate overlay (debug layer): `--fps-overlay`, `debug.fps_overlay`, or the debug overlay's checkbox.
- Tested: every GRAPHICS row and combination live at normal speed, CONTROLLER SETUP with the save written through the game's SAVE page and read back (only the controller word and the checksum change on the card).
- Deck crew of the opening: drawn in this build at 1×, 2×, 4× and through New Game (the earlier OP-1 fix).
- Docs reorganised (no content removed): `../README.md` lists what moved where; FXAA / MSAA / anisotropic filtering study in `renderer.md` §1.8; command line regrouped with a development-build section and recipes.

Anti-aliasing (2026-10-20, User request; `validation.md` §2n):
- `presentation.antialiasing` `off` (default) / `fxaa` / `smaa`, GRAPHICS row ANTI-ALIASING, `--antialiasing`. FXAA 3.11 (NVIDIA, BSD-3) or SMAA 1x (MIT), fetched and pinned like FSR 1. Runs on each flight view's 3D at the start of its 2D overlay, so the HUD is drawn afterwards and stays sharp; RGB only. Regression: off identical to the baseline; with SMAA every RAM checkpoint and hash log identical.

New card, first save, startup messages, no anti-aliasing in movies (2026-10-20, User request; `validation.md` §2n AA-4, §2o):
- The port formats a new PCSX2 folder card in its saves folder at first start; the game's own first save on it matches a PCSX2-made save and loads back.
- Startup messages in one layout (headline, explanation, numbered steps, labelled details); disc image problems offer Select ISO... (system file picker, saved to `settings.json`).
- SMV movies are never anti-aliased (only FSR 1): checked identical with SMAA on and off at 1× and 2×.

## Appendix A. S4 inventory (decompile-first, User rule 2026-10-15)

Everything slice S4 uses from the game files, decompiled and described before the rest of S4 is implemented (CLAUDE.md Workflow step 1). Function list: `python tools/research/slices.py --list S4` (197 left on 2026-10-15; the title card, briefing, camera library, campaign menu and aircraft-data accessors are already translated). Status per line: **D** decompiled and described here, **T** translated. **All lines are T since 2026-10-16** (the table column still shows D), except the effect drawers and the glow pass listed under "In the static closure but not run in S4", deferred to S5f. Names in *italics* are working names, not yet renamed in Ghidra.

### A.1 Hangar (scene 4 subs 27–36; pages 6–10, 13–16)

#### Hangar state object (0x170 bytes, `*0x00411048`)
- +0x40..+0x6F: copy of the purchase state at 0x002A0320 (6 × u64; per aircraft 2 bits, in **save order**, see `0x00233720`/`0x00233790`).
- +0x70..+0x9F: the same 6 × u64 in **list order** (2 bits per aircraft: 0 not available, 2 owned, 3 for sale; rows 0–2 colours, rows 3–5 special weapons).
- +0xA0 player (0/1), +0xA2 page (0 aircraft, 1–4 shop columns, 5–7 special weapon, 8/9 confirm, 10/11 leave), +0xA4 next page, +0xA6 return page, +0xA8/+0xAA MenuGoTo target on accept, +0xAC/+0xAE on cancel.
- +0xB0..+0xDF: 6 × u64 availability masks (bit per aircraft) from the 2-bit rows (`0x00233950` by shop mode +0xE0).
- +0xE0 shop mode (0 select, 1 buy, 2 sell, 3/4 special weapons buy/sell), +0xE8 aircraft id, +0xEC list index (aircraft − 1), +0xEE colour (0–2), +0xF0 special weapon slot, +0xF2 panel count, +0xF4 = 0x15, +0xF6 special weapon count, +0xF8 panel flags, +0xFA animation flags (bits 0–2 phase: 1 shown, 2 opening, 4 closing; 0x10 sliding, 0x20 texts, 0x80 settled, 0x100 input, 0x200 crosshair, 0x400, 0x1000 SE played, 0x2000 tick SE).
- +0xFC..+0x108 animation times, +0x10C 6 approach values, +0x124 6 panel levels, +0x144 panel list (0x80 bytes, 16-byte entries from 0x0039E148), +0x148 credits, +0x168 credits ^ 0x5963 (checksum; the commit is skipped when it does not match), +0x14C purchase confirmation pending, +0x150, +0x154 nothing available, +0x158 page fade level, +0x15C viewer delay, +0x160/+0x164 viewer camera preset and time.

#### Functions
| Address | Working name | Role | St |
|---|---|---|---|
| 0x002356D8 | HangarEnter | subs 27–35: work heap reset; state object (0x170) and its panel list (0x80); `InitHangarState` from game slot 0 or 1 (sub 0x1F = player 2); `CreateAircraftViewer(aircraft, colour)`; viewer mode +0x9B8 by page (1 for pages 1/3, 2 for 2/4); camera preset 0 at once; BGM (`0x00235540`); sub 36 | D |
| 0x00235540 | *PlayHangarBgm* | sub 0x1B: `GetProgressBgmTrack(2, mission)`, else track 9 | D |
| 0x00235588 | InitHangarState | clears the object (keeps +0x144); aircraft 1–23 (else 2), colour 0–2; list index via `0x00226400`; +0xF4 = 0x15; special count (`0x00233EF0`); credits from 0x002A0D30 and checksum; page/targets by sub (`0x00233808`); purchase state copy and list-order conversion; masks; first available aircraft (`0x00233BA0`); panel set-up (`0x00235580`); fade level 1 | D |
| 0x00233808 | *SetHangarPageForSub* | sub 0x1B: page 0, accept → page 3 (briefing), game[0x27] = mission id, game[0x25] = game[0x29] = 1; 0x1C–0x1F: page 0, cancel target 0x1C/0x1D/0x1E; 0x20–0x23: shop pages (buy/sell aircraft/specials) with mode +0xE0 | D |
| 0x00233720 / 0x00233790 | *PurchaseStateToList* / *PurchaseStateToSave* | reorder the 2-bit fields of 6 u64 rows between save order and list order (table 0x0039DA80 +8 per 0x3C-byte aircraft entry / s16 table 0x0039DA50) | D |
| 0x00233908 | *GetStateMask* | bit mask of the 21 aircraft whose 2-bit state equals `n` | D |
| 0x00233950 | *BuildAvailabilityMasks* | masks +0xB0.. from the rows by shop mode (0/2: state 3; 1: state 2; 3/4: mixed) | D |
| 0x00233B48 | *ListSetBits* | writes the indices of the set bits (below n) to a byte list; returns the count | D |
| 0x00233BA0 | *SelectFirstAvailable* | moves aircraft/colour/special to an available one; returns change flags (1 aircraft, 2 colour, 4 special) | D |
| 0x00233D48 | *SelectAvailableSpecial* | special slot to the first one available for the aircraft (returns 4 on change) | D |
| 0x00233DF0 | *MoveHangarCrosshair* | crosshair target by page (aircraft list row/colour column, special slot) | D |
| 0x00233E58 / 0x00233E88 | *ClearValues* / *ApproachValues* | n floats zeroed / `ApproachValue` towards a table (step k) | D |
| 0x00233EF0 | *GetSpecialWeaponCount* | `*GetAircraftSpecialWeapons(a)` | D |
| 0x00233F10 | CommitHangarSelection | writes aircraft/colour/special to game slot; slot 0 also to 0x002A15BE.. and `ValidateCampaignSelection`; when the checksum matches: credits to 0x002A0D30 and the purchase state back to save order at 0x002A0320 | D |
| 0x00234000 | *BuildPanelList* | copies the 16-byte panel entries (table 0x0039E148, 8) selected by flag bits (always the first two) | D |
| 0x00234078 | *OpenHangarPage* | page set-up: crosshair, values cleared, panel flags from 0x0039E128 by page, panel list, crosshair target | D |
| 0x00234150 | *SetHangarPhase* | phase 2 → `OpenHangarPage`; 4 → closing (times reset, crosshair reset) | D |
| 0x00234190 | *AnimateHangarPanels* | opening: panel levels (time − start) × 6, SE 0xD at 0.75, flags at 1.0; closing: 1 − t × 6, at 0 next page (10/11: fades and viewer state 4/5 = leave); tick SE 9 while texts type | D |
| 0x00234588 / 0x00234628 | *GetAircraftArrows* / *GetSpecialArrows* | tab/arrow flags for the menu fade (0x30/0x3C, +3 or +0xC) from the counts | D |
| 0x002346B0 | ApplyHangarChange | after a selection change: texts restart, special count, slot 0; a colour change sets camera preset 4 (+0x160; corrected 2026-10-16: not the viewer delay) | D |
| 0x00234740 | *HangarAircraftInput* | left/right: next available aircraft (`0x00226430` list index); up/down: colour among available; SE 4; viewer delay 5 on left/right | D |
| 0x00234A08 | *HangarSpecialInput* | left/right through the available special weapons | D |
| 0x00234C50 | *GetPurchasePrice* | price/ownership change for page `kind` (prices from table 0x0039DA80, 0x3C bytes per aircraft: name, code, ..., prices at +0x24.. and +0x30..); with `apply` sets the 2-bit state (3 → 2 buy, 2 → 3 sell); returns −price (buy) or +price (sell), 0 when not possible | D |
| 0x00234EB8 | *HangarAccept* | X/Circle: pages with a price: first press asks (SE 2, +0x14C), second applies (SE 0xF/0x10), credits and checksum, masks, reselect; other pages: next page from 0x0039E1C8 (or SE 5 when not available) | D |
| 0x002351B0 | *HangarCancel* | Triangle: SE 3; cancels the pending purchase or goes to the page from 0x0039E1E8 | D |
| 0x00235238 | HangarInput | pad of the player (player 2 also takes player 1's Triangle); accept/cancel/Start (Start on pages 0/5: confirm page 8/9); aircraft and special input by panel flags | D |
| 0x002353E8 | *HangarConfirmInput* | pages 8/9: Start → back to +0xA6 | D |
| 0x00235438 | HangarUpdate | panel animation; pages 8/9 fade the page out; else fade in, crosshair, approach values (0x0039DA8C + 0x3C × index), input | D |
| 0x00235580 | InitHangarPanels | tail call `SetHangarPhase(h, 2)` (opens the first page; corrected 2026-10-16: not an empty stub) | D |
| 0x00235828 | *SyncHangarViewer* | viewer colour, aircraft (`SetAircraftViewerAircraft`) after the delay; highlight mask +0x9AC of parts that are for sale (state 3, or 2 on pages 3/6) | D |
| 0x00235988 | *UpdateHangar* | from viewer state 2 on: help line, `HangarUpdate`, viewer sync, camera (`0x00231CE0`); `UpdateAircraftViewer` | D |
| 0x002359F0 | *DrawHangar* | buckets (7), viewer, page fade bar (`0x002281E0`), panels (`0x00233138`), help line, tabs, black fade | D |
| 0x00235AB8 | AircraftSelectRun | sub 36: update, draw; viewer state 3/4 after the menu fade: commit and MenuGoTo(+0xA8/+0xAA) (clear colour 0; mission 1 with the default aircraft → game[0x27] 0x25, game[0] 0x17; mission 18 → game[0x27] 0x24); state 5: commit and MenuGoTo(+0xAC/+0xAE) | D |
| 0x002334F8 | *UpdateHangarHelp* | help line by page (tables 0x0039E038/68/98, special weapon help 0x0039E0C8/E8/108) | D |
| 0x00233450 | *HasOtherColourForSale* | another colour or special of the aircraft is for sale | D |
| 0x00233138 | *DrawHangarPanels* | frame outlines, price box (`0x00232E70`), crosshair, gauges (`0x00231ED0`), special icons (`0x00231DE0`), texts (`0x00232F80`), pulse boxes | D |
| 0x00232F80 | *DrawHangarTexts* | text layer: name (`0x00232260`), statistics (`0x00232408`), weapon (`0x002325A8`), colours (`0x002327A0`), special (`0x00231FB8`), price (`0x00232BE8`), ownership marks (`0x00232930`, `0x00232AD0`); returns "finished" flags | D |
| 0x00232260, 0x00232408, 0x002325A8, 0x002327A0, 0x00231FB8, 0x00232BE8, 0x00232930, 0x00232AD0 | *DrawHangar…Text* | the panel texts sliding in by level × 80 (sprintf formats at 0x00410FB8..0x00411040) | D |
| 0x00232E70 | *DrawPriceBox* | frame outline of the price box (0x0039E018), red when unaffordable; inner box `0x0022AB20` | D |
| 0x00231ED0 / 0x00231DE0 | *DrawHangarGauges* / *DrawSpecialIcons* | `BuildGauge` × n; icon boxes `0x0022A868` | D |
| 0x00231CE0 | *UpdateHangarCamera* | viewer camera preset by page (3 aircraft, 4–6 special slots, 7 cockpit, 8 special view) and time | D |
| 0x002318F8 | UpdateViewerCamera | look-at eye/target per preset: 0 slow orbit (fmodf), 3 orbit by colour (floorf, cosf, sinf), 4–6 special slot orbits, 7/8 pad-controlled (`0x00231420` + limits `0x002316D0`/`0x00231728`); either jump or `sceVu0InterVector` blend | D |
| 0x00231420 | *UpdateFreeCamera* | analog sticks → cubic speed (VU0: x³ × k1 × k2), `SetCameraLookAt`, distance limits (`0x00104F50`), rotation about Y, blend | D |
| 0x002316D0 / 0x00231728 | *ClampCameraBox* | box limits (`0x001051F0` twice), floor heights 740/400 | D |
| 0x002313B8 | *BuildModelMatrices* | `0x001096A8`, `0x00108C68`, `sceVu0TransMatrix` | D |
| 0x002312E0 | *BuildShadowMatrix* | `sceVu0ScaleVector`, direction angles, RotZ/RotX | D |
| 0x00231238 / 0x002311B8 | *SetLightSet* | light set (`0x00136F48` ambient, `0x00136DA0` × 3 directions; one variant transforms the directions) | D |
| 0x00231100 | *SetLightDirection* | light n colour and direction (−Z rotated by X then Y) | D |
| 0x002310C8 | *InitLights* | ambient colour, 3 directions cleared | D |
| 0x00231000 | *CreateMenuObject* | container reader on a model file: items 0 and 1 (or n), `0x0014AE60` / `0x0014AEF8` / `0x0014B078` (menu 3D object API) | D |

### A.2 Aircraft viewer (0x0022DF70–0x002301F0)

Viewer object (0x9D0 bytes, `*0x00410EDC`): +0x10 data load queue, +0x220 DATA 0xCD (font), +0x224/+0x228/+0x22C hangar models, +0x234 aircraft model (DATA 0xCD + aircraft), +0x238.., +0x23C.. special weapon models (DATA 0xE4 + id, 8 bytes each), +0x260 fade (+0x26C level), +0x270 camera, +0x430 look-at, +0x460 light set, +0x4B0/+0x580 menu objects (hangar), +0x650/+0x750/+0x850 light sets, +0x950 aircraft record, +0x954 special weapon records, +0x958 help line message box, +0x97C leave state, +0x980 load state (0 queue, 1 loading, 2 shown, 3 change pending, 4 reload, 5 loading, 6 building), +0x984 aircraft, +0x988 colour, +0x98C/+0x990 special list/count, +0x994, +0x998 build step, +0x99C.., +0x9A4 fog/alpha value, +0x9AC/+0x9AE highlight masks, +0x9B0 pending colour, +0x9B4, +0x9B8 mode, +0x9BC/+0x9C0.

| Address | Working name | Role | St |
|---|---|---|---|
| 0x002300F8 | CreateAircraftViewer | clear colour (16,16,16,0x80), menu draw mode, object (0x9D0), init (`0x0022DFA8`), state 0 | D |
| 0x0022DFA8 | *InitAircraftViewer* | fades, 2D camera with a 640 × 480 (or 16:9) projection (distance 920, +0x16C 50, +0x170 16384), look-at defaults (`0x002300A8`), aircraft/colour, special list, light set (ambient 0x101210, two lights 0x707070/0x282828) | D |
| 0x002300A8 | *ResetViewerLookAt* | default eye/target | D |
| 0x0022DF70 | *SetViewerSpecialList* | special weapon list of the aircraft | D |
| 0x0022FF78 | UpdateAircraftViewer | fade, load state machine (`0x0022E2B0`), fade-in when the menu fade is done, camera (`0x0022F750`) | D |
| 0x0022E2B0 | *UpdateViewerLoad* | states 0–6: font, models (`0x0022E1F0`, `0x0022E218`), hangar objects (`0x0022E450`), aircraft record (`0x0022E580`), colour variants (`0x0022E660` × 3), specials (`0x0022E800`) | D |
| 0x0022E1A8 / 0x0022E1F0 / 0x0022E218 | *QueueViewer…* | DATA 0xCD (5 items), 0xCD + aircraft (2), 0xE4 + special id per special | D |
| 0x0022E450 | *CreateHangarObjects* | two hangar objects (items of +0x224/+0x228), three light sets, `0x00137068` | D |
| 0x0022E580 | *CreateAircraftRecord* | record (0x30) + object block (0x5B0); shadow object | D |
| 0x0022E660 | *CreateAircraftColour* | per colour: model matrices, two objects (normal and mirrored by −Y: the floor reflection), part flags (`0x0022E978`), `0x0022EBD0` | D |
| 0x0022E978 | *SetAircraftParts* | switch on the mode (jump table 0x00407070, 8 cases): part values (`0x0014B0A0` part types 0x1F/0x13) | D |
| 0x0022EBD0 | *FinishAircraftObjects* | `0x0014B168` on the three objects | D |
| 0x0022E800 | *CreateSpecialRecords* | per special weapon: record, three objects (positions 0x0039D870) | D |
| 0x0022F750 | *UpdateViewerCamera3D* | `SetCameraLookAt`, `UpdateCameraMatrices`, scale ×10 (`0x00105178`) into the screen/clip matrices | D |
| 0x00230048 | DrawAircraftViewer | state 2: everything (0xF); 3–6: hangar only (3) | D |
| 0x00230080 | SetAircraftViewerAircraft | in state 2/3: new aircraft and colour, state 4 (reload) | D |
| 0x0022FDA0 | *DrawViewerScene* | `0x0022FCD0`, five render-state packets (`0x0022EC28`: FRAME/ZBUF/TEST/ALPHA by pass), hangar (`0x0022ED70`, `0x0022EE30`, lights `0x0022F810`), aircraft passes (`0x0022F048` shadow, `0x0022F130` reflection, `0x0022EEF0`), specials (`0x0022F3D8`, `0x0022F5B0`, `0x0022F200`), black fade | D |
| 0x0022EC28 | *WriteViewerPassState* | GIF A+D: PRMODECONT, TEXA?, FBA, TEST (by pass), ZBUF, CLAMP/ALPHA | D |
| 0x0022ED70 / 0x0022EE30 | *DrawHangarObject* | hangar object pass: VU program upload (`0x0014B280`), light set, view matrices (`0x0014B3F8`), texture/context (`0x0014B2A8`), model (`0x0014B5A0`) | D |
| 0x0022EEF0 / 0x0022F048 / 0x0022F130 | *DrawAircraftPass* | aircraft: with its light set transformed into the model (`0x002312E0`, `0x00231238`), mirrored floor reflection (light set +0x750), shadow/floor object | D |
| 0x0022F200 / 0x0022F3D8 / 0x0022F5B0 | *DrawSpecialPass* | special weapon objects (mask bit per weapon), same three passes | D |
| 0x0022F810 | *DrawCeilingLights* | 5 rows × 8/9 ceiling light glows: VU0 projection (`vmula/vmadda` with the camera, Q divide, ftoi4), MMI packing/visibility, size by distance, colour `LerpColor`, sprites via `0x00112A10` (needs disassembly check: MMI + VU0) | D |
| 0x0022FCD0 | *ClearViewerBackground* | z clear sprite (`BuildSpriteStrips`) | D |

### A.3 Menu 3D object API and model library

Menu object (0xD0 bytes; written by `0x00231000`): +0 radius, +4/+8, +0x10 world→view part matrix, +0x50 world matrix, +0x90 model instance, +0x94 second instance (glow model, mode 2), +0x98 VU1 program block (`.vutext` packet: 0x00298AC0 mode 0, 0x00297C00 mode 1, 0x00299820 mode 2), +0x9C mode, +0xA0 texture (GIM), +0xA4/+0xA8 effect list (always empty in S4: the only creator passes 0), +0xAC alpha, +0xB0 flags, +0xB4..+0xC0 frame/z formats.

| Address | Working name | Role | St |
|---|---|---|---|
| 0x0014AE60 | *InitMenuObject* | defaults, VU program by mode | D |
| 0x0014AEF8 | *BindMenuObject* | model data → `BindModelData` (0x28) + `CreateModelInstance` (0x4C); texture (`InitGimTexture`); glow model; effect list | D |
| 0x0014B078 | *SetMenuObjectTexturePos* | texture TBP/CBP words | D |
| 0x0014B0A0 | *SetMenuObjectPart* | part value on both instances (`0x00140F68`) | D |
| 0x0014B168 | *UpdateMenuObject* | `UpdateModelHierarchy` from identity on both instances | D |
| 0x0014B1D0 / 0x0014B238 | *SetMenuObjectPose* / *SetMenuObjectMatrices* | rotation (`0x00108C68`) + translation; or copy two matrices | D |
| 0x0014B280 | *UploadMenuObjectProgram* | DMA `ref` to the VU1 program packet (`0x001042D0`) | D |
| 0x0014B2A8 | *WriteMenuObjectTexture* | texture upload (`BuildTextureUpload`) and TEX0/TEX1 via VIF UNPACK to VU address 0x1C | D |
| 0x0014B3F8 | *WriteMenuObjectCamera* | VIF UNPACK to VU 0x1E (1 qword) and 0x00 (8 qwords: world→screen and world→clip of the camera) | D |
| 0x0014B5A0 | *DrawMenuObject* | GIF A+D (TEST/ALPHA), `EmitModelInstance` (mode 2: three passes with alpha +0xAC), effect list (`DrawSpecialObject`; empty in S4), glow pass `0x00142460` for mode 2 | D |
| 0x00144618 | BindModelData | model header: +0x10 node table, +0x14 root node; node count, max depth, part index table | D |
| 0x00144238 / 0x00144340 | *CountModelNodes* / *BindModelNodes* | recursive node walk (format version at data+4: ≥ 0xB4 uses relative child lists; ≥ 0xBF part indices) | D |
| 0x0013F018 | CreateModelInstance | per-node matrices (+0x10 local, +0x14 world), vectors, flags, part values (0.5), morph buffers (node flag 4), mesh VIF list counts (tag 0x60000000 terminated) | D |
| 0x0013FE58 | UpdateModelHierarchy | recursive: toggles visited flag, local transform from node T/R (+0x10/+0) with part keyframes (`0x0013F4D0`), rotation matrix (`0x001089B0`), multiply with parent (`0x00108F00`), morph (`0x0013F6C0`), node origins | D |
| 0x0013F4D0 | *InterpolatePartPose* | part value × 100 → keyframe pair, VU0 lerp of translation/rotation | D |
| 0x0013F6C0 | *BuildMorphMesh* | morph targets: lerp of positions (s16), colours (u8), normals (f32), UVs (s16) between two keyframes into the instance's VIF buffer | D |
| 0x00140F68 | *SetModelPartValue* | part value change marks the nodes dirty (flag 4) | D |
| 0x001404A8 | EmitModelInstance | per visible node: VIF UNPACK of 13 qwords at VU 0 (world→screen × node, world→clip × node, light matrix), GIF tags at VU 0x15/0x16/0x19/0x1F (PRIM by flags: Gouraud, texture, fog, alpha), DMA `call` to the node's mesh packet | D |
| 0x00142460 | *DrawGlowPass* | VU1 program D (0x0029A940): half-resolution copy of the frame (`0x00141EE0`), model glow geometry twice, blur/add passes with `BuildScaledCameraMatrices` | D |
| 0x00141EE0 | *WriteHalfResCopy* | GS sprites copying the frame buffer to a half-size buffer | D |
| 0x001420F0 | BuildScaledCameraMatrices | camera matrices with the viewport scaled by k (0.5 for the glow pass) | D |
| 0x00136AA0 / 0x00136DA0 / 0x00136F48 / 0x00137068 | *InitLightSet* / *SetLight* / *SetAmbient* / *SetLightTexture* | light set (directions as matrix rows at +0x50, colours at +0x90, ambient +0xD0, VIF header +0xE0/+0xF0); colours /128 | D |
| 0x001370D8 | WriteLightSetPacket | VIF UNPACK 9 qwords at VU 0x0C (light matrix, colours, ambient); optional environment texture upload and A+D state | D |
| 0x001041E8 / 0x001042D0 | *AllocBucketPacket* / *AddBucketRef* | DMA packet in a bucket (`cnt` + data) / `ref` tag to external data (VU programs) | D |
| 0x001066D8 | *WriteTex0Tex1* | GIF A+D TEX0/TEX1 from a GIM texture record | D |
| 0x00112A10 | *WriteSpriteStrip* | sprite strips with UV interpolation, clipped to the scissor | D |
| 0x00104F50 | *GetDistanceXZ* | √(dx² + dz²) | D |
| 0x00105178 | *BuildScaleMatrix* | diagonal matrix (MMI pextlw/pcpyld) | D |
| 0x001051A8 | *EvalHermite* | (VU0) cubic of 1 − t | D |
| 0x001051F0 | *ClampVector* | VU0 vmax/vmini | D |
| 0x001089B0 / 0x00108C68 | *BuildRotationMatrix* / *RotateMatrix* | VU0 sin/cos polynomials (constants at 0x002A15E0..0x002A1610) for rotations X, Y, Z; Ghidra drops the broadcast fields: translate from the disassembly | D |
| 0x00108F00 | *MulMatrixVu0* | 4×4 multiply (vmula/vmadda order) | D |
| 0x001096A8 | *SetUnitMatrixVu0* | identity | D |

#### In the static closure but not run in S4
The slice tool assigns these to S4 through `DrawMenuObject` → `DrawSpecialObject`, but the menu object's effect list is always empty in S4 (`0x00231000` passes 0 to `0x0014AEF8`). They draw mission effects and are deferred to S5f (effect drawing): `DrawSpecialObject` 0x001E0A98 and the 20 drawers it dispatches (0x001A41F8, 0x001A5DA8, 0x001A7AE8, 0x001A9940, 0x001AB5D8, 0x001AD510, 0x001AFDF8, 0x001B1BD8, 0x001B3C58, 0x001B5AC8, 0x001B9C90, 0x001E0B60, 0x001E1270, 0x001E1838, 0x001E2238, 0x001E24C8, 0x00211278, and the constant getters 0x001E0A40/58/78), with their helpers `0x00108FC0` (VU0 triangle clipper using VU0 data memory through `vlqi`/`vsqi`), `0x00109500` (VU0 vertex projection), `0x0013E338`/`0x0013D590` (terrain height lookup), `0x001456F8` (direction angles).

#### libc and SDK used by S4 (src/sce)
- `fmodf`, `floorf` (exact on the host), `sinf`, `cosf`, `atan2f` (newlib: **not** bit-exact on the host; translate the newlib routines before the hangar camera depends on them; `risks.md`).
- libvu0: `sceVu0ScaleVector`, `CopyVector`, `CopyMatrix`, `InterVector` (have), `AddVector`, `SubVector`, `OuterProduct`, `InnerProduct`, `Normalize` (effects only).

### A.4 Mission select (scene 4 subs 0x42–0x49; free mission, VS, records)

Page object (0x770 bytes, `*0x0041182C`): +0x10 data load queue, +0x220 DATA 0x110 (font block, +0x224 image), +0x240 fade (+0x24C level), +0x250 panel tab, +0x290 animated frame {x, y, w, h, sx, sy}, +0x2A0 panel (5 rows, 0x003AF070), +0x2F8/+0x2FA crosshair target, +0x300 crosshair, +0x304 menu tree (4 columns 0x003AEFD0, connections 0x003AF030, parents 0x00411190), +0x70C help line, +0x730 mode (0 campaign free mission, 1 extra, 2 VS, 3 VS team), +0x734 state (0–5 mission list: 1 browsing, 2 confirm, 3/4 leaving; 6–10 settings menu), +0x738/+0x73A MenuGoTo target, +0x73C mission index, +0x740 shown index, +0x744 available count, +0x748 preview level, +0x74C row count, +0x74E row x, +0x750 thumbnail table, +0x754 frame animation step, +0x758 frame time, +0x75C VS flag, +0x760/+0x764 text times.

| Address | Working name | Role | St |
|---|---|---|---|
| 0x0023C600 | *EnterMissionSelect* | sub 0x42..0x47 entry: menu draw mode, page (`CreateMissionSelectPage`), DATA 0x110, BGM 4, sub 0x48 | D |
| 0x0023C6A8 | *LoadMissionSelect* | sub 0x48: font block, help line, sub 0x49 | D |
| 0x0023C708 | *MissionSelectRun* | sub 0x49: fade, help line; states < 6: list (`0x0023A8B8`) and its draw (`0x0023BD78`); else settings menu (`MissionSelectUpdate`, `0x0023C4C0`) | D |
| 0x0023A118 | CreateMissionSelectPage | by sub: mode 0–3, list (`0x00239E58`) or settings (`0x00239F58`) | D |
| 0x00239E58 | *InitMissionList* | fade, mode, counts (`0x00239D50`), row layout, frame, panel (`InitPanel`), crosshair | D |
| 0x00239F58 | *InitMissionSettings* | settings menu tree with hidden items by mode; VS team: weather/time strings into 0x0042D1E0, game[0x2E] | D |
| 0x00239D50 | *GetMissionCounts* | rows per mode (0x00410E80); extra missions from the unlock bits 0x002A031C; VS needs bit 0 of 0x002A0318; last index 0x00411198 | D |
| 0x00239E10 | *SelectMissionRow* | index, crosshair to the row | D |
| 0x00239C58 | *UpdateMissionSelectHelp* | help message by state (0x003AEEB8 per mode, 0x003AEEC8 per settings item, 0x2C VS) | D |
| 0x0023A208 / 0x0023A380 | *LerpFrame* / *AnimateMissionFrame* | frame between key rectangles (0x003AF190.., times 0x003AF1D0), eased by `0x001051A8` | D |
| 0x0023A360 | *ResetMissionFrame* | first key rectangle | D |
| 0x0023A550 | *AcceptMission* | VS: confirm then game[0x25] = 1, game[0x29] = 2, mission id (`GetMissionId(2, n)`), slot 0 aircraft 2; else go to the settings menu | D |
| 0x0023A668 | *MissionListInput* | X/Circle accept; left/right through the available rows (SE 4) | D |
| 0x0023A770 | *UpdateMissionPreview* | preview fade after the cursor rests (SE 5 at start) | D |
| 0x0023A8B8 | *UpdateMissionList* | frame animation (SE 0xE), panel, preview; switch on state (jump table 0x00409150): 0 wait fade, 1 browse (Triangle → state 5), 2 confirm, 3 → settings, 4 → MenuGoTo, 5 → back | D |
| 0x0023AB48 / 0x0023ABA0 | *LeaveMissionSelect* / *LeaveMissionSelectMenu* | state 8/9 with target | D |
| 0x0023AC00 | *MissionSettingsCancel* | close column or back to the list | D |
| 0x0023AC70 | MissionSelectAction | settings item 0–18: start (game[0x25]/[0x29], mission id by kind), hangar pages (0x1C/0x1D...), records, save/load sub-pages (0x4D/0x4E/0x54/0x55; HDD columns with ctx flag 0x80000), VS weather 0x10–0x12 | D |
| 0x0023AF78 | MissionSelectUpdate | settings states 6–10 | D |
| 0x0023B108 | DrawMissionRecords | mission name, difficulty/rank (records 0x002A045C + difficulty × 0x168 + mission × 0x14, extras 0x002A0CCC), best times (h'm"s) and points, row tabs | D |
| 0x0023B930 | *DrawMissionThumbnail* | thumbnail sprite from the page image (sinf wobble) | D |
| 0x0023BBA0 | *DrawMissionFrame* | frame sprite from the animated rectangle | D |
| 0x0023BD78 | *DrawMissionList* | buckets, frame, fades, panel, thumbnail, records, help line | D |
| 0x0023BE88 | *DrawMissionSettingsTexts* | per player: aircraft code, special weapon; VS team texts; tick SE 9 | D |
| 0x0023C230 | *DrawTitleBar* | bar sliding in with a sinf pulse (`LerpColor`) | D |
| 0x0023C4C0 | *DrawMissionSettings* | menu tree, texts, title bar, help line, fades | D |

### A.5 VU programs, formats and libc (analysed 2026-10-15)
- **VU1 programs B (0x00298AC0), C (0x00299820), D (0x0029A930)**: disassembled (`tools/research/vu_disasm.py`) and documented in `../subsystems/vu-programs.md` (inputs, clipping, lighting, reflection pass, backface/ADC, glow fins). Program A (0x00297C00) is used by menu mode 1, which S4 never selects; it stays with S5b.
- **Model data format**: `../formats/MODEL.md` (header, nodes, keyframes, morph table, mesh DMA list and its UNPACK layout); research dump `tools/research/model_dump.py`.
- **newlib float maths** (bit-exact translation into `src/sce/libc.cpp` with the EE float helpers; currently host libm): `sinf` 0x00284830, `cosf` 0x00284660 → `__ieee754_rem_pio2f` 0x00286DE0 (medium range: 3-part π/2 constants; large: `__kernel_rem_pio2f` 0x00287450 with `scalbnf` 0x002886B8, `floorf`, tables 0x0040C960/0x0040C970 and the 2/π digits from 0x0040C578), `__kernel_sinf` 0x00287DA0, `__kernel_cosf` 0x002872F8; `atan2f` 0x00284CB8 (wrapper: `_LIB_VERSION` 0x0040CA0C, `isnanf` 0x00288690, matherr path) → `__ieee754_atan2f` 0x00286018 → `atanf` 0x002883A0 (tables 0x0040CA20 atanhi, 0x0040CA30 atanlo, 0x0040CA40 aT[11]); `fabsf` 0x00288648; `fmodf`, `floorf` (exact on the host).

### A.6 Disassembly checks to do while translating
Where Ghidra drops VU0 broadcast fields or MMI: `0x001089B0`, `0x00108C68`, `0x0022F810`, `0x00231420`, `0x0013F4D0`, `0x001051A8`, `0x00105178`; jump tables `0x0022E978` (0x00407070, 8 cases) and `0x0023A8B8` (0x00409150, 6 cases).

### A.7 Port pieces S4 needs (beyond game code)
- **VIF1 + VU1 model**: VU1 data memory (16 KiB), VIF1 UNPACK (formats used: V4-32, V4-16, V4-8, with STCYCL/FLG/USN), BASE/OFFSET/TOPS double buffering, ITOP, MPG (program identity by source address), MSCAL/MSCNT → CPU HLE of programs B, C, D producing GIF packets (PATH1), in `src/sce/libgraph.cpp` (DMA intake) and `src/platform/render/` (ADR-003).
- **GS features**: local→local transfers and texture-from-frame-buffer at half resolution (glow pass), alpha/blend modes of the viewer passes, the ADC bit, fog.

## Appendix B. Slice 5 plan: scene 2 (missions)

Status: **approved by the User 2026-10-16** (blocks P, A, B as one block, C; `--freeze-sim` for block A). P1 done 2026-10-16 (`validation.md` S5-P1); P2 done 2026-10-17 (`--replay-p2m2`, `--p2m2-offset`; offset 153 for power-on recordings, `validation.md` S5-P2). `--fast-until` added 2026-10-17 (`validation.md` S5-T1). P3 done. P4 done 2026-10-16 for block A (terrain blocks 5–18 checked on all 164 entries, `../formats/MISSION.md`). P5 done 2026-10-16 for block A: all 448 functions documented to packet level (`s5-blockA-inventory.csv`), plus the 9 libvu0 functions block A needs (`../platform/sdk-functions.md`). Translation of block A in progress (2026-10-16): load path complete (mission load sub-states 0-8, 0x1E and their systems: units, effect pools, projectiles, HUD/replay/radio/sound state, terrain/static objects/unit and aircraft models, sky, sea) plus the camera helpers and core math; the build links with no stubs. Per-frame world drawing translated (2026-10-16): view cameras and the collision core they need (0x0013D698, 0x00144DC8, 0x0017C4E8, not in the block A inventory), terrain (culling, texture cache, cells), sea and its CPU clipper, static objects, units, aircraft with shadow passes and weapons, tracers. Calls into slice S5f (DrawSpecialObject 0x001E0A98, effect camera 0x00155900, effect drawer 0x001BCBD8) and the glow pass 0x00142460 are logged once and skipped. Sky group translated 2026-10-16 (0x0010FFB8–0x00118400: dome, glow/blur, stars, clouds and cloud shadow, moon, sun with flare and glare, sun halo, weather particles; two open points in `../pcsx2/procedures.md`); `BuildFlightDrawLists` 0x0010BD68 and the sub 9 handler 0x0010BA98 translated, `--freeze-sim` implemented (subs 0xA–0xC and 0x13), plus the development capture `--dump-dma-at` (`command-line.md`). First visible result: loading screen and the mission sky (`validation.md` S5-A). VU1 terrain program T (`.vudata`, documented in `vu-programs.md`) and programs A/E implemented as HLE. Mission 1 load checked against PCSX2 2026-10-17 (`validation.md` S5-A2): allocation exact, heap contents equal except timing and the random cursor left by the opening sequence. GS dump of mission 1 drawn through the port's renderer 2026-10-17 (`validation.md` S5-T2): GS memory now in the GS column layout, two renderer bugs fixed, the frame matches PCSX2 except the DATE draws. Missions 3 and 4 checked 2026-10-17 (`validation.md` S5-A3): 0 unexplained RAM ranges outside the scene heap, heap equal except timing and the uninitialised effect clock; renderer: fog, TEX1 filtering with trilinear mipmaps, DATE, AA1, frame buffers in other formats (the three mission dumps now match PCSX2; the black band is gone). Remaining for block A: PABE, COLCLAMP 0, CSM2, partial FBMSK when a frame needs them. Plan for the rest of S5 (User request 2026-10-17): test tooling (`--mission`, parallel runner), block B decompile-first, then translation. Block B translated 2026-10-18: every S4 and S5a–S5h function has a translation, including the special-object drawers and the glow pass with VU1 program D ("Results per slice" above); Block C decompiled and translated 2026-10-18: all 194 S5i/S5j functions (`s5-blockC-inventory.csv`), VU0 program S as a C++ rewrite in `src/platform/vu0/` (decision 14); only the S6 VS rules stay stubbed. Validation of blocks B and C against the S5-B / S5-C captures is next. Slice definitions: this file; validation method: `validation-strategy.md`.

### B.1 Size

Function lists from `tools/research/slices.py --list S5x` (run 2026-10-16, after S4). "Bytes" is code size; "VU0" = functions with COP2 macro instructions; "double" = functions that call the libgcc double-precision routines; "random" = call sites of `NextRandomFloat` (table) and libc `rand`.

| Sub-slice | Content | Functions | Bytes | >= 1 KB | VU0 | MMI | Double | Random calls (table / rand) |
|---|---|---|---|---|---|---|---|---|
| S5a | mission load, system creation | 291 | 52 KB | 9 | 6 | 5 | 3 | 45 / 37 |
| S5b | static world rendering | 157 | 88 KB | 26 | 25 | 18 | 1 | 9 / 2 |
| S5c | player flight, collision | 65 | 43 KB | 12 | 5 | 2 | 3 | 1 / 0 |
| S5d | weapons, effect simulation | 294 | 101 KB | 11 | 23 | 0 | 2 | 48 / 2 |
| S5e | AI, mission rules, radio choice | 250 | 98 KB | 27 | 6 | 5 | 1 | 34 / 12 |
| S5f | effect drawing | 208 | 426 KB | 87 | 87 | 38 | 1 | 13 / 6 |
| S5g | HUD | 86 | 73 KB | 21 | 0 | 14 | 1 | 2 / 0 |
| S5h | mission sound | 49 | 17 KB | 2 | 0 | 7 | 3 | 0 / 0 |
| S5i | sequences, cutscenes | 102 | 37 KB | 9 | 14 | 4 | 1 | 0 / 0 |
| S5j | replay, carrier, game over, rearm | 98 | 43 KB | 12 | 5 | 0 | 1 | 0 / 15 |
| (S4 rest) | menu-object effect drawers, glow pass (deferred to S5f) | 29 | 127 KB | 19 | 17 | 4 | 0 | 0 / 0 |
| **Total** | | **1,629** | **~1.1 MB** | | | | | |

For comparison, S1–S4 together were 663 functions.

### B.2 Why the sub-slices cannot be validated one by one

- **One frame, one step.** `FlightPlayFrame` 0x0010B150 runs the draw-list build, HUD sector scan, player sound, terrain/object selection, sound pools, `FlightSimStep` 0x0010C320 and `UpdateViewCameras` 0x0010CB80 every frame. Inside the step, the AI pass (`func_0011D988`) and the player-effects pass (`func_00126970`) spawn effects directly (pools +0x4A830, +0x47800, +0x61630 …), weapons request rumble, and the HUD centre-message tick (`func_0016DB40`) runs in the same step.
- **Random table.** The draw code reads the random table too (S5f: 13 `NextRandomFloat` calls, 6 `rand`; S5b: 9 / 2), and the table cursor is shared by everything in the frame. Until every reader of a frame is translated, RAM can only be compared region by region (`pending-slice`, `risks.md`).
- **Sound and radio.** The radio queue (S5e) waits on SOUND voice status (S5h), so the radio state depends on the sound code.

So a **full-RAM checkpoint during gameplay is only possible when S5c–S5h are all translated**. Translating them as one block is less work than translating them separately with stubs: there are no stubs to write, keep consistent and remove, and only one validation round.

### B.3 Proposed blocks

| Block | Sub-slices | Functions | Ends with | Full-RAM checkpoints |
|---|---|---|---|---|
| **P** (prerequisites) | — | 0 game functions | determinism, VU1 programs A/D/E documented, formats documented, input replay | port-vs-port determinism hash |
| **A** | S5a + S5b | 448 | mission loads with its loading screen; the world drawn from the start camera, simulation frozen at the first step (development option) | sub 0xA first step (heap exact), first frame's draw-list build |
| **B** | S5c + S5d + S5e + S5f + S5g + S5h + S4 rest | 1,081 | missions playable start to end with effects, HUD and sound | every 10 s of the input recordings |
| **C** | S5i + S5j | 200 | cutscenes, in-engine sequences, replay, carrier, game over, rearm, title demos; `RequestScene(5)` | sequence frames, replay frames |

Alternative if B is too large to review: **B1** = S5c + S5e + S5g + S5h (flight, AI, mission rules, HUD, sound; 450 functions; weapons and effects stubbed, regional RAM checks only) then **B2** = S5d + S5f + S4 rest (631 functions; full RAM from here). Same total work plus the stubs. Recommendation: **one block B**, translated internally in the order of §B.6, reported to the User at each internal milestone, validated at its end.

### B.4 Block P: prerequisites (before any S5 game code)

These need the User's go-ahead because they touch `src/`.

| Item | Why | Work |
|---|---|---|
| **P1 Determinism** | Input-recording validation and port-vs-port hashes need bit-identical runs. Checked 2026-10-16: the game side is already step-driven (the SPU mixer, SPU IRQs and VAG.IRX run in `AdvanceStep` on the game thread at every vblank; timer 0 advances per read; CD completion counts `sceCdSync` polls; libmc counts vblanks). One solo run and four parallel runs to the S4 title card were byte-identical except **the pad's analog bytes** (0x004121F4/0x00412214): `host.read_pad` overlays the script on the live device state, which the main thread samples once per presented frame. An earlier observation (movie buffers differing between parallel runs, with an older build) did not reproduce. | (1) Input source chosen per run: live devices, or only the script/recording (validation runs ignore live devices). (2) Live input latched per game tick: the main thread publishes timestamped snapshots, the game thread takes one at the start of each tick and logs it (the same log is what a replay or LAN lockstep feeds back). (3) A determinism checker: `--hash-log` writes a hash of guest RAM per vblank (plus per-region hashes), and a host-stress option (random sleeps on the main thread) runs against a calm run; the first differing vblank and region are reported. (4) Audit of the remaining host inputs: memory-card file times (folder card directory entries) and the real-time clock, fixed in validation runs. |
| **P2 Input replay** | S5c+ validation uses PCSX2 input recordings (`.p2m2`, `../formats/p2m2.md`). | Reader for `.p2m2`, pad feed with analog sticks and pressure buttons as `scePadRead` returns them (`libpad.cpp` today serves digital only), latency calibration (procedure in §B.8). |
| **P3 VU1 programs A, D, E** | A is the generic model renderer (view buckets 6 and 9, terrain and most of the world, menu lighting mode 1); D draws the glow fins; E the aircraft view mode 2. Only B and C are analysed (`../subsystems/vu-programs.md`). Decompile-first rule. | Microcode disassembly and documentation with `tools/research/vu_disasm.py` (never in Ghidra, `.claude/rules/vu-microcode.md`); compare against the Oct-11 GS dumps (`gameplay_attacking_bombers_explosions_hud`, `first_briefing`). HLE implementation belongs to block A (A, E) and block B (D). |
| **P4 Formats** | Block A reads them. | Mission data blocks 5–18 and 31 (terrain), 22 (unit placement), 28 (setup tables), 30 (effect tables), 34/35 (message/radio system), packets 0 and 26 (`../formats/MISSION.md` "Open"); `tools/research/` parsers checked against the extracted DATA entries. |
| **P5 Function documentation to packet level** | The coverage notes describe the large draw routines at interface level only (`coverage.md`). The decompile-first rule needs every function documented before implementation. | Per block, before its implementation: read every function's disassembly (MMI, `lq/sq`, VU0 macro and `cfc2` sites checked as the rules require), record packet layouts, random reads and double-precision paths in the subsystem docs. Block A first (448 functions), block B while A is validated. |

### B.5 Block A: S5a + S5b (448 functions)

- **Entry points:** `FlightSceneEnter` 0x0010A898, `StoreFlightDataPointers` 0x0010B038, engine sound banks `func_0010FDE8`, loading screen 0x0010A998, `CreateFlightSystems` 0x0010AA50, `StartFlightSystems` 0x0010AB58, `func_0010FA70`, `SetupMissionUnits` 0x0010E758, `SetupMapFromMissionData` (H) 0x00139B70, `CreateWorldRenderObjects` 0x0013EA28, `InitEffects` 0x00154660; world update/draw 0x00139FE8, 0x00144CC0, 0x0013EC20/38, sky 0x001118C8/0x00113708/0x00115F28, `BuildFlightDrawLists` 0x0010BD68, `UpdateViewCameras` 0x0010CB80.
- **Heap:** mission 1 fills the work heap to 98.4 % and allocations are unchecked (`../game/memory.md`): every allocation size and order must be exact. Checked at sub 0xA.
- **Renderer:** VU1 programs A and E as CPU HLE (B/C exist), ordering-table buckets for the flight views, fog (FGE, already requested by the hangar), CT24 and Z24 read as textures, destination-alpha blending and DATE, PABE, partial FBMSK, COLCLAMP 0 where the scan found them (`../platform/gs-subset.md`). Trilinear mipmapping with automatic mip addresses (TEX1.MTBA: MIPTBP is never written because the GS computes the addresses; corrected 2026-10-17, implemented with fog, DATE, AA1 and frame-buffer format conversion, `validation.md` S5-A3).
- **sce/platform:** CD streaming of large mission entries (5.6 MB blocks) at the measured read latency; the engine-sound bank load (SOUND transfers).
- **Visible:** the loading screen with the objectives, then the world from the start camera (development option `--freeze-sim` or similar: the step not run; decided with the User).
- **Validation:** RAM at the first frame of sub 0xA (0x0010A230) for missions 1, 3 and 4 (heap mark +0x1664 and every pool header exact); GS-level comparison of the first frame with PCSX2 GS dumps (`tools/research/gsdump.py`, `gifstrips.py`); loading-screen screenshot.

### B.6 Block B: S5c–S5h + S4 rest (1,081 functions)

Internal order (each step ends with a build and the regional RAM checks that are possible at that point):
1. Frame skeleton: `FlightPlayFrame`, `FlightSimStep`, sub-state handlers, pause and end-of-mission menus.
2. Player flight and collision (S5c): flight model with the double-precision damping, landing gear, controls from analog input, terrain/object collision with the `cfc2` flag-delay rule (`validation.md` B-2, the three PCSX2-patched sites), replay recording (`RecordReplayFrame` 0x0016E8B8).
3. AI, mission rules, phases, event script, proximity triggers, radio choice (S5e).
4. Weapons and effect simulation (S5d): the 7 projectile pools, 13 special-weapon kinds, damage with its one-frame delay, effect pools with their random reads; pool slot rules literal (`risks.md`).
5. HUD (S5g) and mission sound (S5h): sector scan, radar, gauges, lock-on, messages, 16:9 variants; engine sound with the double-precision throttle compare, positional SE, radio voices through VAG slots, mission BGM.
6. Effect drawing (S5f + the 29 S4 menu-object effect drawers and the glow pass): sprites, trails, smoke ribbons, explosions, heat haze (frame buffer as texture), weather; VU1 program D.
- **Renderer:** VU1 program D, framebuffer feedback (heat haze), the remaining blend forms; performance target: 60 fps in effect-heavy frames without the validation layer (the hangar work, `validation.md` S4-6, applies).
- **Audio:** 3D SE panning/volume, reverb mixing (the effect areas are now known, `validation.md` S4-7), stream slots under load.
- **Replay of the S5-B recordings:** they start from the mission-start states (`.p2m2` `from_savestate` = 1, `../formats/p2m2.md`), so the port runs its route to the same checkpoint (0x0010A230, 0 unexplained ranges in S5-A3) and then feeds recording frame 0 onwards; this needs a development option to start a `.p2m2` at a RAM checkpoint (to add at the start of block B validation; asked for before it is written).
- **Validation:** test vectors (flight step, collision, slot allocation, every random-reading spawn, target selection, radio choice); input recordings of missions 1 and 3 (free flight, firing, full mission) with full-RAM checkpoints every 10 s and at phase changes; GS dumps of two effect-heavy frames; HUD screenshots; an SPU2 capture of a short segment.

### B.7 Block C: S5i + S5j (200 functions)

- Sequence and character engine, pre-mission cutscenes (`UpdateInEngineSequence` 0x00177058), mid-mission sequences 0x0010B500, the no-save intro; the **VU0 micro-mode skinning** program 0x0029C200 as a verified C++ rewrite of its 9 entry points (decision 14; vectors for part types 0 and 3 still pending, `../pcsx2/procedures.md` #1).
- Replay playback and cameras (`UpdateReplayPlayback` 0x0016E2D0; u8-quantised snapshots), title demos (`StartTitleDemo` 0x0022CC68), carrier landing/takeoff (mission 3, subs 0x14–0x1B), game-over and rearm menus, draw-only frame (sub 9) → `RequestScene(5)`.
- Validation: skinning vectors; RAM at fixed sequence frames; the mission 1 replay; the title demo from the title idle; the carrier recording.

### B.8 PCSX2 batches (written into `../pcsx2/procedures.md` when the plan is approved)

| Batch | For | Content |
|---|---|---|
| A | block A | **written 2026-10-16** (`../pcsx2/procedures.md` S5-A, together with P2): save states at the first frame of sub 0xA (0x0010A230) for missions 1, 3, 4 (no card has a mission-10 save); GS dumps of the first gameplay frame; loading-screen screenshots |
| P2 | input replay | **written 2026-10-16** (S5-A run 1): power-on recording with two checkpoint states and their frame numbers |
| C | block C | **written 2026-10-18** (`../pcsx2/procedures.md` S5-C: mission 1 pre-mission sequence, mission 4 replay, mission 3 carrier landing/rearm/takeoff, game over and retry, title demo; optional no-save intro and mid-mission sequence; plus `../pcsx2/procedures.md` section 1, the VU0 skinning vectors) |
| B | block B | **written 2026-10-17** (`../pcsx2/procedures.md` S5-B: recordings anchored at the S5-A mission 3/4 start states instead of power-on, so the load-length drift does not apply): input recordings: mission 1 free flight 60 s, a firing run (guns, missiles, special weapon), missions 1 and 3 to the end; checkpoint save states every 10 s and at phase changes; ~20 entry/exit pairs for the test vectors; 2 GS dumps of effect-heavy frames; ~6 HUD screenshots (lock-on, warning, map, messages, 16:9); one SPU2 audio capture |
| C | block C | skinning vectors (part types 0 and 3), 3 sequence save states, 4 screenshots, a replay checkpoint, the carrier recording |

Rule for unknowns (User, 2026-10-16): when a function cannot be translated with confidence from the code and needs PCSX2 to settle it, its translation is **skipped and logged** in `../pcsx2/procedures.md` (new, one row per function: address, question, what to capture), and the User collects the answers in the next batch.

### B.9 Known risks for S5

- Heap exactness (block A): one byte off breaks every later pool address.
- Double precision: 17 functions call the libgcc double routines (3 each in S5a, S5c and S5h); bit-exact soft-float paths (`ee_math.h`).
- VU0 flag pipeline delay at `cfc2` reads (S5c collision and the projection site; PCSX2 patches them, the port reproduces hardware).
- Random order: 152 table-read and 74 `rand` call sites across the blocks; the translation keeps their order (appendix of `../game/random.md`).
- Performance: S5f is 426 KB of packet code; effect-heavy frames will need the same kind of profiling as the hangar.
- Determinism (P1) must be done first, or the recordings cannot be validated; the known leak is live input (analog bytes), found by a port-vs-port test.

### B.10 Questions for the User

1. Blocks P, A, B, C as proposed (B as one block), or B split into B1/B2?
2. Go-ahead for block P (P1 and P2 touch `src/`), in parallel with the documentation of block A (P3–P5 are research work).
3. The development option to freeze the simulation for block A's world view (name and behaviour).
