# Translation slices (proposal)

Status: **order approved by the owner 2026-10-13.** Work starts with S1.

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

Decompile first (owner rule 2026-10-15): before any port code of a slice is written, everything the slice uses from the game files is decompiled and documented: every game function it reaches, its VU microcode programs (`../subsystems/vu-programs.md`), and the data formats and tables it reads. Implementation starts only when that inventory is complete.

Common to all slices: `// EE:` comment and `translated-functions.csv` row per function; RAM checkpoint with 0 unexplained ranges; listed screenshots match; tests pass. "PCSX2 batch" = what the owner is asked for in one session (exact steps written into `../pcsx2/procedures.md` at slice start).

### S1: scene 6 startup (87)
- **Functions:** `SceneStartup` 0x002613F0 and its 10 sub-states (table 0x003F8708), `StartupSplashUpdate` 0x00260980, card evaluation `func_00260488`/`func_00260288`, message windows; shared: text renderer (17), sound init/SE calls (21), 2D render helpers (12), libmc wrappers (4).
- **Renderer:** first real GS path: GIF PATH2/PATH3 packet intake from the DMA lists, IMAGE uploads (PSMCT32/PSMT8/PSMT4 + CLUT), sprites and triangles with texture, alpha blending, scissor, fades; frame/display registers; field copy executed but not presented (ADR-006 §4). Native screenshot in the overlay.
- **Audio:** SOUND.IRX HLE core (SIF RPC 0x81000101 command batches → SPU2-subset mixer, ADPCM voices, ADSR) for menu SE; voice status advanced per step.
- **Input:** existing (confirm button on the card messages).
- **sce:** libmc on the folder memory card (`sceMcGetInfo`, `Open`, `Read`, `GetDir`, `Close`; port 2 empty), `../formats/folder-memcard.md`.
- **Visible:** splash images, warnings, card check messages; with a save, the request to scene 4; without, the request to scene 2.
- **Validation:** RAM at sub 7 (first hit), sub 9 and both exits for card S (save), card E (unformatted: empty superblock) and no card; screenshots of both splash images and of the card messages. Card messages appear **only with no card** (two windows); card E (unformatted) shows none and goes to the intro (`../pcsx2/sessions-06.md`).
- **PCSX2 batch:** done 2026-10-14 (`../pcsx2/sessions-06.md`): 3 cold boots, 6 save states, 5 screenshots, card folders S and E.

### S2a: menu framework, title, main menu (86) — translated and validated 2026-10-14
- **Functions:** `SceneMenu` 0x002262D0, subs 0–6 (0x002261B8, 0x00226248, 0x0022DE00, 0x0022DEA0, 0x0022DF00), menu tree, crosshair cursor, panels, message box, fade object, data load queue `QueueMenuDataLoad` 0x0022C618, menu BGM (`PlayMenuBgm`), overlay passes.
- **Renderer:** frame-buffer feedback for the scanline/noise copy (0x00227F30), letterbox bars; text rendering at scale.
- **Audio:** VAG.IRX HLE (RPC 0x81000102, 6 stream slots) streaming BGM from `VAG/*.VPK` on the ISO; crossfades.
- **Visible/playable:** title screen with BGM, PRESS START, main menu navigation (entries leading to later slices end in a logged stub).
- **Validation:** RAM at title sub 6 entry (0x0022DF00), Start accepted (call site 0x0022D748), menu active (call site 0x0022D84C, first hit) and first confirm (call site 0x0022D880); screenshots of title, main menu and the opened item (4:3). BGM audibly correct (no RAM effect beyond slot state). The 16:9 screenshots move to S2b: the save's options are loaded only through the main menu's load path (S2b).
- **PCSX2 batch:** run 07 (`../pcsx2/sessions-07.md`): 4 save states, 3 screenshots. Result: 4 RAM checkpoints with 0 unexplained ranges, screens without structural differences (`validation.md` S2a-3, S2a-4); open: blend rounding of the feedback filter (S2a-R), music by ear.

### S2b: options, controller, BGM player, records, sound options, save/load (151)
- **Functions:** scene 4 subs 41–51, 55–61, 74–89: options page and handlers (`func_0023F350`…`func_002402E0`), controller settings (both pads), BGM player, records, sound options, save/load frame and `SavePageUpdate` 0x0023D198, statistics clamp 0x00105448; HDD branches of the save page (unreachable at run time, translated).
- **Renderer:** nothing new beyond S2a (16:9 layout paths).
- **Audio:** option changes (category volumes, mono/stereo).
- **sce:** libmc write path (`sceMcOpen/Write/Flush/Close/Delete/GetDir`), byte-identical save image (`../formats/SAVE.md`).
- **Playable:** all non-mission menu pages; save and load.
- **Validation:** RAM at each page's first run step and after leaving it with changed options (`MenuGoTo` 0x00226450); RAM after a card load (0x0023D4FC); screenshots per page, 4:3 and 16:9. The save page enters **save** mode only from sub-pages 0x4A–0x4E (campaign menu, post-mission and mission-select pages, later slices); main-menu LOAD is sub-page 0x4F (load mode). So the byte-identical save check (folder card round trip both ways, `../STATUS.md` §2) moves to S4, the first slice that reaches a save sub-page. The write path is translated here.
- **PCSX2 batch:** written 2026-10-14 (`../pcsx2/procedures.md` item S2b): 12 save states, about 10 screenshots, button sequences per page.
- **Status (2026-10-14):** translated 151/151 and validated (`validation.md` S2b-1/S2b-2: 9 RAM checkpoints 0 unexplained, 10 screens). The BGM player reference was captured 2026-10-17 (`../pcsx2/sessions-11.md` S2b-B, card with a cleared campaign); its port comparison is pending.

### S3: movies (86)
- **Functions:** in-menu player subs 7–13 (`MenuMovieEnter` 0x002390C8 … `MenuMovieEnd` 0x002392A8), interlude viewer subs 52–54, `StartMenuMovie` 0x00238778, `UpdateMenuMovie` 0x00238810, `MovieFrameSync` 0x00238870, the SMV reader state machine (0x00212630–0x00212960), decode/draw `func_00212F88`/`func_00213048`, subtitle pages, audio feeder `func_00213CE8`.
- **sce:** `sceMpegInit/Create/AddBs/IsEnd/Reset/GetPicture` on **libmpeg2** (approved; fetched when this slice starts), RGBA32 output into the guest image buffer; timer 0 H-blank count (RCNT0 mode 0x83) modelled as two ticks per iteration.
- **Renderer:** frame mode 640×448 display; the **movie upscaling stage** (ADR-006 §5): component interface + FSR 1 EASU (+RCAS) implementation, passthrough at 1×, exercised at 2× through a development-only override for testing.
- **Audio:** SMV PCM blocks into the SPU2 core-0 sound-data input via the SOUND HLE (block transfer); confirm the 0x200-byte L/R interleave first (`../formats/SMV.md`); MIS movies use VPK stream tracks.
- **Visible:** opening/interludes (SS01–SS12, INTRO, STAFF from the interlude viewer) with sound and subtitles; each picture held exactly 2 ticks.
- **Validation:** RAM at a fixed movie picture (scene_frame N of SS01); screenshots of 3 pictures (640×448 and 640×336 letterboxed); PCSX2 SPU2 audio capture of the first seconds of SS01 for the interleave; FSR 1 output checked visually at 2×.
- **PCSX2 batch:** written 2026-10-14 (`../pcsx2/procedures.md` item S3): states at the first picture, a mid picture and the end of SS01, 3 screenshots, a sound capture.
- **Status (2026-10-14):** translated, 86/86 (`slices.py --list S3` is empty; 655 in total). libmpeg2 0.5.1 added (FetchContent, approved). SS01 plays in the port from New Game with sound and subtitles, 2 vblanks per picture, Start skips (`validation.md` S3-1). Fixed after the owner's first look: seams at the 16-pixel movie columns (GS texture coordinates now rounded to 1/16 texel) and corrupt pictures at chunk changes (open GOPs: decoder references kept over `sceMpegReset`), `validation.md` S3-1. Movie upscaling stage built (owner decision 2026-10-14, option b: decoder side path, ADR-006 §5): FSR 1 EASU at the internal-resolution factor, a passthrough at 1×; checked at ×2/×3 with `--movie-upscale`. Its use in the draws waits for the scaled GS backend. Validated 2026-10-15 against PCSX2 run 09 (`validation.md` S3-3 to S3-5): 3 RAM checkpoints 0 unexplained, 2 screens, sound capture. Not done: the interlude viewer test (needs a cleared campaign).

### S4: mission title, briefing, campaign menu, hangar, mission select (253)
- **Functions:** scene 4 subs 14–36 and 65–73: `MissionTitleRun` 0x00230790, briefing (`CreateBriefing` 0x002365C0, subtitle timing, 3D map 0x0023EE20), `CampaignMenuRun` 0x00230FC8, `HangarEnter`, `AircraftSelectRun` 0x00235AB8, `CommitHangarSelection` 0x00233F10, `CreateAircraftViewer` and the menu 3D object API (0x0014AE60–0x0014B5A0), `WriteLightSetPacket` 0x001370D8, unlock rules, mission select (`MissionSelectAction` 0x0023AC70, `func_0023A668`).
- **Renderer:** first **VU1 program HLE** (program A, `../subsystems/vu-programs.md`) for models, lighting, the reflection and shadow passes; VU0 macro-mode math (`vu0_math.h`); point sprites for the map.
- **Visible/playable:** New Game → mission title card → briefing → campaign menu → hangar with the rotating aircraft → `RequestScene(2)` (ends in the scene 2 stub until S5a).
- **Validation:** RAM at briefing map view, at the hangar with aircraft N, and at the scene-2 request (game state block exact); screenshots of title card, briefing map, hangar viewer (two aircraft, two colours).
- **PCSX2 batch:** 4 save states, ~8 screenshots; test vectors for the hangar credit/ownership commit. Written 2026-10-15 (`../pcsx2/procedures.md` item S4).
- **Status (2026-10-16):** translated and validated against PCSX2 runs A–C: all S4 RAM checkpoints 0 unexplained (`validation.md` S4-1 to S4-7), `CommitHangarSelection` byte-exact test vector, hangar at 60 fps (S4-6). 29 functions (menu-object effect drawers, glow pass) are not reached in S4 and move to block B of `s5-plan.md`.

Plan for all of slice 5 (blocks P, A = S5a+S5b, B = S5c–S5h, C = S5i+S5j, and why): `s5-plan.md` (proposal 2026-10-16, awaiting the owner).

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
- **Debug layer:** a debug menu entry to enter scenes 1 and 3 (`../port/debug-overlay.md` "Jump to scene"), marked as modifying the session.
- **Validation:** default build never reaches them (scene request log); build and unit tests; `dead-code.md` updated with each function's translation.
- **PCSX2 batch:** none (dead code cannot be reached in PCSX2 without patches).
- **Status (2026-10-19):** all functions translated (`slices.py --list S8` empty; 2,888 translated functions in total, 269 of them dead). Default build unchanged (mission 1 smoke run). The debug menu entry for scenes 1 and 3 is not built yet (needs the owner's go-ahead). Details: `dead-code.md` "S8 translation".

### S9: VS LAN play (no game code; ADR-008 option A)
- **Port:** netplay layer in `src/platform/` between the input backend and `scePadRead` (input queue per port, step gate in the game thread), UDP sockets (no dependency unless the owner approves one), LAN discovery, handshake (ISO SHA-1, build hash), transfer of the host's memory card image for the session, per-step RAM hash exchange and desync stop, optional crop of the output pass to the local player's half. Opt-in, off by default.
- **Validation:** two port instances on one PC and on two PCs run a VS match in lockstep with identical hash streams; a recorded 2-port PCSX2 VS match replayed on both sides gives the same checkpoints as S6.
- **PCSX2 batch:** none beyond S6.
