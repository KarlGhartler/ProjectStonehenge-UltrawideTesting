# Decompilation coverage (game code)

Game code = the functions Ghidra defines in 0x001000D8–0x002616E7. SDK/libc code is outside this range and is not counted (CLAUDE.md, dead-code principle).

- Per-function master list: `coverage-functions.csv` (`address, name, subsystem, status, note`).
- Status words:
  - **understood**: behaviour known and documented. Named in Ghidra, or documented in a research doc where a name is still pending;
  - **partly**: some behaviour known; the note or the subsystem doc states the open point;
  - **unknown**: not yet analysed;
  - **dead**: unreferenced or unreachable game code (`dead-code.md`), still translated later.
- Recomputed with `out/scratch/coverage.py`, which reads a read-only function dump from Ghidra through the ghidra-mcp HTTP API.

## How the subsystems were seeded

The seed comes from call-graph reachability from each scene handler and its sub-state table (`scenes.md`):
- a function reachable from exactly one scene starts in that scene's subsystem;
- a function reachable from several scenes is **shared-unsorted**;
- a function reachable from the main loop is **core**;
- the rest is **unreached**: called only through data pointers not yet decoded, or dead.

The flight scene (scene 2) closure holds most of the game. It is split into flight model, AI, weapons, camera/HUD, mission scripting and rendering as the analysis proceeds.

## Milestones

### Baseline (2026-10-10, start of priority 2)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| boot (scene 0) | 5 | 0 | 5 | 0 | 0 |
| core (main loop closure) | 189 | 146 | 24 | 19 | 0 |
| flight-unsorted (scene 2) | 1541 | 57 | 74 | 1410 | 0 |
| frontend (scenes 1, 4) | 458 | 5 | 52 | 401 | 0 |
| hdd (scene 7) | 9 | 0 | 5 | 4 | 0 |
| movie (scene 3) | 7 | 1 | 6 | 0 | 0 |
| results (scene 5) | 34 | 1 | 10 | 23 | 0 |
| shared-unsorted | 233 | 5 | 49 | 179 | 0 |
| startup (scene 6) | 23 | 0 | 20 | 3 | 0 |
| unreached | 255 | 3 | 12 | 174 | 66 |
| **Total** | **2754** | **232** | **243** | **2213** | **66** |

Note: the skeleton closure (`skeleton-readiness.md`, 199 game functions) used a narrower definition of "game function" (only direct calls from `main` through the skeleton subsystems). The 2,754 here are every Ghidra function in the game range.

### Milestone 1: front end done (2026-10-10)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| frontend (scene 4 module 0x00226198–0x00246E17 + helpers) | 441 | 440 | 0 | 0 | 1 (scene 1) |
| **Total game code** | **2754** | **668** | **194** | **1825** | **67** |

- Front-end helpers that live in other modules were moved to their own subsystems and are counted there: model render API → `render`, camera library → `camera`, libmc/HDD wrappers → `save`, aircraft data accessors → `aircraft-data`, text renderer → `text`.
- 35 functions classified as "shared" by reachability turned out to be front-end library code (menu widgets, text, unlock logic) and were moved to `frontend`.
- Naming: only entry points and important logic are renamed in Ghidra (about 45 front-end renames this milestone). Small helpers are documented in `coverage-functions.csv` notes and `frontend.md` instead of a Ghidra rename. A helper counts as understood once its behaviour is written down.

### Milestone 2: mission flow done (2026-10-10)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| mission (scene 2 control, rules, event script, menus, helpers) | 127 | 123 | 4 | 0 | 0 |
| sequence (in-engine cutscene player, overlays) | 15 | 15 | 0 | 0 | 0 |
| replay (recorder, playback, replay camera) | 48 | 48 | 0 | 0 | 0 |
| **Total game code** | **2754** | **1062** | **143** | **1482** | **67** |

The 4 partly items are the scripted segments of scene 2 subs 0x14–0x1B (open questions).

### Milestone 3: flight model and player control done (2026-10-11)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| flight (player aircraft, flight model, gear, effects) | 31 | 31 | 0 | 0 | 0 |
| collision (player/unit collision response) | 18 | 18 | 0 | 0 | 0 |

Doc: `flight.md`. One open question (CONTROL TYPE label).

### Milestone 4: AI done (2026-10-11)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| ai (aircraft and ground/naval units, cannons, scripted objects) | 81 | 80 | 1 | 0 | 0 |
| replay (adds the unit/player snapshots) | 54 | 54 | 0 | 0 | 0 |
| **Total game code** | **2754** | **1211** | **141** | **1335** | **67** |

Doc: `ai.md`. The partly item is the periodic strike event `func_00135008`.

### Milestone 5: weapons, damage and collision done (2026-10-11)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| weapons (projectile pools, 7 projectile kinds, 13 special-weapon kinds, area damage) | 116 | 116 | 0 | 0 | 0 |
| collision (terrain height map and segment tests, unit meshes) | 26 | 26 | 0 | 0 | 0 |
| **Total game code** | **2754** | **1332** | **140** | **1215** | **67** |

Doc: `weapons.md`. 13 functions previously counted as "unreached" are the special-weapon init functions, reached through a jump table that Ghidra did not recover (plate comment at 0x0014FB88).

### Milestone 6: camera and HUD done (2026-10-11)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| hud (HUD, radar, map, warnings, reticles) | 88 | 88 | 0 | 0 | 0 |
| camera (camera library, view cameras) | 26 | 26 | 0 | 0 | 0 |
| **Total game code** | **2754** | **1389** | **137** | **1161** | **67** |

Doc: `hud.md`.

### Milestone 7: results, startup and save system done (2026-10-11)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| results (scene 5, campaign updates) | 37 | 37 | 0 | 0 | 0 |
| startup (scene 6) | 22 | 22 | 0 | 0 | 0 |
| save (memory-card layer) | 20 | 20 | 0 | 0 | 0 |
| hdd (HDD save/install layer, scene 7) | 85 | 85 | 0 | 0 | 0 |
| **Total game code** | **2755** | **1550** | **96** | **1042** | **67** |

Doc: `results-save.md`. One function was created (0x0025CF08, HDD op 0x10 stub), so the total is now 2755.

### Milestone 8: EE-side sound, radio, text and rumble (2026-10-11)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| sound (voice API, banks, engine/ambient, 3D listener, streams) | 170 | 166 | 4 | 0 | 0 |
| radio (chatter selector, voice channels, subtitles) | 89 | 71 | 18 | 0 | 0 |
| text (fonts, strings, glyph packets) | 25 | 23 | 2 | 0 | 0 |
| input (rumble) | 12 | 11 | 1 | 0 | 0 |
| **Total game code** | **2755** | **1819** | **92** | **777** | **67** |

Doc: `sound.md`. The partly items are large selector/mixer functions that were only skimmed; they are listed in `sound.md`, "Open points". Ghidra renames for this step (PlaySound, StopSound, SetSoundVolume, SetSoundPitch, PlayMenuSe, PlayEventSe, QueueRadioChatter, QueueRadioMessage, RequestRumble, NextSoundRandom) were applied after a Ghidra restart; the restart lost the last unsaved step-7 edits, which were re-applied from `rename-log.csv`.

### Milestone 9: rendering, models, terrain, effects, VU programs (2026-10-11)

| Subsystem | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| render (frame tables, models, lighting, terrain, map objects, shadows) | 214 | 169 | 24 | 0 | 21 |
| effects (45 pools: dispatchers, wrappers, spawns, allocators, helpers) | 585 | 295 | 281 | 0 | 9 |
| sequence (cutscene character engine, VU0 micro skinning) | 108 | 69 | 13 | 0 | 26 |
| movie (SMV reader, audio feeder, frame upload) | 57 | 38 | 16 | 0 | 3 |
| replay (incl. 16 replay camera styles) | 72 | 60 | 12 | 0 | 0 |

Doc: `render.md`. Most of the effects "partly" rows are per-type update/draw functions and helpers: their role is known from the dispatcher, but the body was not read (notes say so). The random sources were corrected in `ai.md`, `weapons.md` and `mission-flow.md`. ADR-002 has a new finding.

### Milestone 10: remaining DATA/BD formats (2026-10-11)

- `CDP.md`: every DATA index range is mapped to its loader except 291–332. The BD loading map is added (BD 2 has no loader).
- `MODEL.md` (new): the `SM` model format (header, node tree, `ret`-terminated node packets).
- PCSX2 checks for the unloaded entries: `open-questions.md`.

### Milestone 11: remaining dead code (2026-10-11)

- Full reachability over a raw call graph (`jal`/`j`, address builds, data pointers minus `.eh_frame`), plus an audit of function boundaries against the `.eh_frame` FDEs.
- **129 functions created in Ghidra** this milestone: 39 from FDEs, 88 from code gaps (three passes: 74 + 10 + 4), plus 0x001759E8 (replay camera style) and 0x002124F8 (static constructor). All are logged in `rename-log.csv`; 2755 + 129 = 2884.
- Earlier "unreached" tags were rechecked. The replay camera styles, the sequence Euler-order matrices, the options-page handlers and 8 pause-menu callbacks are live.
- The HDD module init has no caller, so the HDD paths are unreachable on any hardware.
- `dead-code.md` sections 3b, 4, 5 and 6 are updated.

| | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| **Total game code (final)** | **2884** | **2229** | **389** | **0** | **266** |

Every game function now has a status. "Dead" means not reachable from `main` (unreferenced, or referenced only from dead code); it is still translated per the project rules. The 33 g++ STL helper copies and the 446 C++ runtime functions above the game range are library code and are not counted here.
