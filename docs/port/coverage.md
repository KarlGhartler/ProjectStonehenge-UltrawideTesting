# Decompilation coverage (game code)

Game code = the functions Ghidra defines in 0x001000D8–0x002616E7. SDK/libc code is outside this range and is not counted. The 33 g++ STL helper copies and the 446 C++ runtime functions above the game range are library code and are not counted either.

- Per-function master list: `coverage-functions.csv` (`address, name, subsystem, status, note`).
- Status words:
  - **understood**: behaviour known and documented (named in Ghidra, or documented in a subsystem doc or the CSV note). Small helpers count as understood once their behaviour is written down, without a Ghidra rename;
  - **partly**: some behaviour known; the note or the subsystem doc states the open point;
  - **unknown**: not yet analysed;
  - **dead**: not reachable from `main` (unreferenced, referenced only from dead code, or reachable only through scene-table entries that no code ever requests: scenes 1 and 3; `dead-code.md`). Still translated.
- Recomputed with `tools/research/coverage.py`, which reads a read-only function dump from Ghidra through the ghidra-mcp HTTP API.

## Totals

| | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| **Game code** | **2884** | **2612** (91 %) | **0** | **0** | **272** (9 %) |

## Per subsystem

| Subsystem | Functions | understood | partly | unknown | dead | Doc |
|---|---|---|---|---|---|---|
| effects | 585 | 576 | 0 | 0 | 9 | `../subsystems/effects.md` |
| frontend | 467 | 455 | 0 | 0 | 12 | `../subsystems/frontend.md` |
| render | 214 | 193 | 0 | 0 | 21 | `../subsystems/rendering.md` |
| core (main loop closure, libraries) | 211 | 196 | 0 | 0 | 15 | `../game/` |
| sound | 202 | 149 | 0 | 0 | 53 | `../subsystems/sound.md` |
| mission | 164 | 164 | 0 | 0 | 0 | `../subsystems/mission-flow.md` |
| weapons | 116 | 116 | 0 | 0 | 0 | `../subsystems/weapons.md` |
| sequence (cutscene player and character engine) | 108 | 82 | 0 | 0 | 26 | `../subsystems/mission-flow.md`, `../subsystems/rendering.md` |
| radio | 98 | 94 | 0 | 0 | 4 | `../subsystems/sound.md` |
| hdd | 96 | 73 | 0 | 0 | 23 | `../subsystems/save.md` |
| hud | 88 | 88 | 0 | 0 | 0 | `../subsystems/camera-hud.md` |
| ai | 81 | 81 | 0 | 0 | 0 | `../subsystems/ai.md` |
| unreached | 78 | 0 | 0 | 0 | 78 | `dead-code.md` |
| replay | 72 | 72 | 0 | 0 | 0 | `../subsystems/mission-flow.md` |
| movie | 57 | 48 | 0 | 0 | 9 | `../formats/SMV.md`, `../subsystems/frontend.md` |
| results | 40 | 40 | 0 | 0 | 0 | `../subsystems/results-startup.md` |
| camera | 35 | 32 | 0 | 0 | 3 | `../subsystems/camera-hud.md` |
| text | 33 | 30 | 0 | 0 | 3 | `../subsystems/sound.md` |
| flight | 32 | 32 | 0 | 0 | 0 | `../subsystems/flight.md` |
| collision | 30 | 30 | 0 | 0 | 0 | `../subsystems/weapons.md`, `../subsystems/flight.md` |
| startup | 23 | 22 | 0 | 0 | 1 | `../subsystems/results-startup.md` |
| save (memory card) | 21 | 18 | 0 | 0 | 3 | `../subsystems/save.md` |
| file | 13 | 1 | 0 | 0 | 12 | `../game/file-loading.md` |
| input (rumble) | 12 | 12 | 0 | 0 | 0 | `../subsystems/sound.md` |
| aircraft-data | 7 | 7 | 0 | 0 | 0 | `../formats/AIRCRAFT.md` |
| boot | 1 | 1 | 0 | 0 | 0 | `../game/scenes.md` |

**The HDD subsystem is unreachable at run time** although only 23 of its 96 functions are "dead": the save page and the HDD install page call the HDD layer when the HDD flags allow, but the HDD module init has no caller and no code sets the flags (`../subsystems/save.md`). The same holds for scenes 1 and 3 and for scene 7 (`../game/scenes.md`). "Dead" in this table is call-graph reachability only.

## Depth of the notes

Every reachable function is now "understood", but not every note has the same depth:
- **Game logic** (flight, AI, weapons, collision, mission scripting, HUD, results, radio selection, replay recording): the rules, constants and random reads are written down in the CSV note or the subsystem doc.
- **Large draw routines** (effect draws, terrain and model packets, the cutscene character engine, movie upload, menu 3D objects): documented at interface level. The note lists the inputs, what the function writes (packet buffers, scratchpad, private draw state), every random read, and confirms that it writes no game state. The exact packet contents are read from the code when the function is translated (`port-function` skill).
- **Random use** in draw-phase code is listed in `../game/random.md`, because it must keep its order even where it does not affect gameplay.
- **Double precision**: a few functions call libgcc double routines (`__extendsfdf2`, `__muldf3`, `__subdf3`, `__truncdfsf2`, the compare at 0x0028BD28), e.g. `func_00137930` (absolute value, exact), `func_00215D20` (engine throttle compared with the double constants 0.2 and 0.9), `func_00216D88` (positional SE volume × 0.7), `func_00224848` (morph weight 1 − t), `func_001FD398`. The port must keep these in double: a float compared with double 0.2 is not the same test as a compare with 0.2f.

## How the subsystems were assigned

The seed is call-graph reachability from each scene handler and its sub-state table (`../game/scenes.md`):
- a function reachable from exactly one scene starts in that scene's subsystem;
- a function reachable from several scenes was "shared" and then assigned by role;
- a function reachable from the main loop is **core**;
- the rest is **unreached**.

The flight scene (scene 2) closure holds most of the game; it is split into flight model, AI, weapons, camera/HUD, mission scripting, replay, effects and rendering. Helpers that live in another module are counted in their own subsystem (model render API → `render`, camera library → `camera`, libmc/HDD wrappers → `save`/`hdd`, aircraft data accessors → `aircraft-data`, text renderer → `text`).

Function boundaries:
- 129 functions were created in Ghidra from `.eh_frame` FDEs (39), code gaps (88) and two single finds (0x001759E8 replay camera style, 0x002124F8 static constructor); 0x0025CF08 (HDD op 0x10 stub) was created separately. All are logged in `../rename-log.csv`.
- The 13 special-weapon init functions are reached through a jump table Ghidra did not recover (plate comment at 0x0014FB88, `../subsystems/weapons.md`).

The skeleton closure (`skeleton-readiness.md`, 199 game functions) uses a narrower definition: only direct calls from `main` through the skeleton subsystems.
