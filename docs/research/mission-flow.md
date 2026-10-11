# Mission flow (scene 2)

Priority 2, step 2. Per-function status: `coverage-functions.csv`, subsystems `mission`, `sequence`, `replay`. Data formats: `docs/formats/MISSION.md`.

## 1. Scene handler and sub-states

`SceneFlight` 0x0010A030, called every frame:
1. Runs `EnterFlightSubState` (the per-sub-state init on the frame after a change, table 0x00403C60).
2. Sets `ctx->flags` 0x800, except for subs 0xB and 0x1C. Sub 0x1C also sets flag 0x200.
3. Calls `table_0x002A1620[sub]()`.
4. Ticks the two per-player screen fades (0x0010A158).
5. Counts the frame timers in the flight scene object.
6. Runs the exit hooks (0x0010A5D0).

**Flight scene object** (`CreateFlightScene`, 0x1810 bytes, gp 0x004104C4):

| Offset | Meaning |
|---|---|
| +0x000 + n·0x440 | view n (up to 5): flags (bit 31 active, bit 0 3D, bits 1–3 HUD layers), +5 player, +7 camera mode, +9 camera kind, +0x10 eye, +0x20 target, +0x30–0x7F camera inputs, +0xB0 camera object A, +0x270 camera object B (`camera` subsystem), +0x218/+0x3D8 FOV distance |
| +0x1540 | 16 packet-bucket descriptors for the frame's draw lists |
| +0x1650 | number of views (1, or 2 = split screen) |
| +0x1653 | mission kind: 0 campaign/free, 1 trial (ids 0x15–0x19), 2 VS (0x14, 0x1A–0x1F, 0x28), 3 tutorial (0x20–0x27), 4 in-engine special (0x23–0x25) |
| +0x1654 / +0x1658 | frames since sub-state change |
| +0x165C | simulation frame counter |
| +0x1660 | mission timer (stops while paused) |
| +0x1664 | work-heap mark after setup |
| +0x1668 | flags: bit 0 record replay this frame, 0x100 cutscene start, 0x10000 replay-skip |
| +0x1670 / +0x1678 / +0x1680 | per-player fade level / time / target |
| +0x1688 | scripted-sequence drawing active |
| +0x168C / +0x1690 | loading-screen flags / alpha |
| +0x1694 | the 36 mission-data sub-blocks (`MISSION.md`) |
| +0x1754 / +0x1794 / +0x17D4 | player aircraft, second aircraft and demo-data sub-blocks |

**Sub-states** (`table_0x002A1620`):

| Sub | Handler | Role |
|---|---|---|
| 0, 1 | `FlightSceneEnter` 0x0010A898 | create the scene object, pick the mission kind → 2 |
| 2–5 | `StoreFlightDataPointers` 0x0010B038 | load mission data (2), player aircraft (3), second aircraft (4, VS) and demo data (5) with `LoadFlightDataEntry` → 6 |
| 6 | 0x0010FDE8 | load the aircraft engine sound banks (BD entries 0x002A1878) → 0x1E |
| 0x1E | 0x0010A998 | loading screen with mission objectives ("NOW LOADING", "PRESS START BUTTON" in trial mode after 2 s) → 7 |
| 7 | `CreateFlightSystems` 0x0010AA50 | create terrain, units, effects, HUD, replay, script objects (heap) → 8 |
| 8 | `StartFlightSystems` 0x0010AB58 | start sound, objects, HUD, players → 0x13 (kind 4), 0x10 (demo), 0xD (mission 10 opening), else 0xA |
| 0xA–0xC | `FlightPlayFrame` 0x0010B150 | 0xA mission start (pre-mission cutscene when `flags & 0x100`), 0xB gameplay, 0xC mission end |
| 0xD–0xF | 0x0010B500 | scripted mid-mission sequences for mission ids 4, 5, 10, 14, 15 (`game[0x2C]` stage, BGM/length table 0x002A16A0) |
| 0x10–0x12 | 0x0010B930 / `UpdateReplayPlayback` | replay and demo playback, then results / menu |
| 0x13 | 0x0010BA38 | in-engine special sequence (opening/ending ids 0x23–0x25) |
| 0x14–0x18 | 0x0010BAB8 / 0x0017A1B8 | scripted flight segment after objective type 2 in mission id 3, with retry (H: meaning to confirm) |
| 0x19–0x1B | 0x0010BB98 / 0x0017AC68 | variant of the segment; **no requester found** (possibly unreachable) |
| 0x1C | 0x0010BC68 | game-over menu |
| 0x1D | 0x0010BCD8 | pause menu |
| 9 | 0x0010BA98 | cameras + draw only |

Frame order in gameplay (`FlightPlayFrame`):
1. set the random-table index from the simulation frame;
2. terrain/world update (0x00139FE8, 0x00144CC0, 0x0013EC20/38);
3. `FlightSimStep` (objects, collisions, AI, players, HUD, radio, rules, replay recording);
4. `UpdateViewCameras`;
5. players (cutscene or controlled);
6. sound listeners;
7. cutscene skip;
8. `BuildFlightDrawLists`.

## 2. Mission setup

- `SetupMissionUnits` 0x0010E758 reads block 22. When it is a 3-entry container, it picks the difficulty variant: `game[0x28]` ≤ 1, = 2, ≥ 3.
- 0x18-byte placement records → unit structs at 0x002A1F70[i] (255 × 0xA0 bytes, allocated by 0x0011B948).

| Unit offset | Meaning |
|---|---|
| +0x00 / +0x10 / +0x20 | position, rotation (Euler), velocity |
| +0x40 | definition record (one of 10 typed tables: record sizes 0x24, 0x14, 0x20, 0x24, 0x20, 0x28, 0x18, 0x20, 0x18, 0x14) |
| +0x44 | class state (aircraft or ground object) |
| +0x48 | static flags: side (`&3`, 1 = enemy), 0x10 ground target, 0x100, 0x800000, 0x1000000/0x2000000 (from the record), 0x20000000 target, 0x8000000 difficulty-dependent |
| +0x4C | dynamic flags: 0x1000000 alive/active, 0x10000000 on radar, 0x20000000 lockable |
| +0x54 | class: 0 player aircraft, 1 AI aircraft, 2 ground/naval |
| +0x58–0x5B, +0x65 | group, AI parameters |
| +0x5E | hit points |
| +0x60 | score value |
| +0x69 | model type code |
| +0x70 | radius |

- Players: slot from `g_GameState`; aircraft, colour and special weapon from the hangar.
- `0x0010FA70` also builds the rules and events from block 28 (`0x0011A3D8`).

## 3. Mission rules and event script

Objects:
- rules: gp 0x004104F0, 0x70 bytes;
- status: gp 0x004104F4, 0x90 bytes. +0x20 flags: 1 success, 2/8 failure kinds, 0x100 gameplay running, 0x1000; +0x24 mission time; +0x38 segment countdown; +0x7C kill count; +0x7D result bits;
- event tables: gp 0x004104F8, 0xA8 bytes.

`UpdateMissionRules` 0x0011A390 dispatches on the sub-state:
- 0xA (0x0011A128): runs the start list; after the intro time shows "mission start" → 0xB.
- 0xB (0x0011A208): `CheckMissionObjectives` (or the VS rules 0x00118CC0), the mission timer and limit, timed events.
- 0xC (0x0011A340): `MissionEndRules` (or VS 0x00119000).

Objective types (`rules+0x10 & 3`):
- 0: points/kills quota;
- 1: number of targets destroyed (`+0x28`);
- 2: reach a position (z < `+0x1C`; mission id 3 goes to sub 0x14 instead of 0xC).

**Event script** (`RunMissionEvents` 0x0011AAE0): 4-byte-aligned commands, terminated by 0xFF.

| Op | Effect |
|---|---|
| 0x00 / 0x07 | objective type / mode bits |
| 0x01 / 0x02 / 0x03 | time limit (seconds) / quota / target count |
| 0x04, 0x0D, 0x09 | rule and status flags |
| 0x05 / 0x06 / 0x08 / 0x0C / 0x0E / 0x0F | positions, limits, area, timers (×6 frames) |
| 0x0A | trigger event group n |
| 0x0B | start scripted sequence n (`game[0x2C]`, sub 0xD) |
| 0x10 / 0x11 / 0x16 / 0x19 | radio/subtitle messages (0x00252F80 / 0x00252FB8), conditional on kills, with timer |
| 0x12 / 0x15 / 0x17 / 0x18 | waypoint, proximity messages (three lists) |
| 0x13 / 0x14 | status bytes |
| 0x20–0x24 | unit activate / deactivate / activate at position (relative to another unit) / set HP / kill |
| 0x25 / 0x26 / 0x27 / 0x28 / 0x29 | AI flags, non-target, unit flags |
| 0x2A | explosion effect at a unit |
| 0x30–0x33 | timers |
| 0x34–0x37 | difficulty-conditional quota/area (difficulty ≥ 2 or < 2) |
| 0x40–0x44 | status bytes, tutorial hints (0x43) |
| 0x50–0x53 | conditional messages, parameters, flags |
| 0x60–0x63 | BGM play, stop, change with fade |

Event sources:
- start/play lists;
- timed lists `{u16 time/6 s, u16 offset}` (0x0011A7D8);
- per-group destruction thresholds (`TriggerEventGroup` 0x0011A920).

## 4. In-engine sequences (cutscenes)

- `SetupInEngineSequence` 0x00176DD0 loads block 29 entry `game[0x2C]`: cuts, a model table, animated sprite sets (GMA) and still images (GIM).
- `UpdateInEngineSequence` 0x00177058 is a baked keyframe player: one record **per frame** per track.
- Track ids:
  - 0x10 camera;
  - 0x11 look-at;
  - 0x20–0x24 object transforms and animation channels (aircraft, skinned characters 0x00220xxx);
  - 0x40–0x49 object flags and effects;
  - 0x70 still-image overlay;
  - 0x71 animated-sprite overlay;
  - 0x80–0x84 fades and colour.
- At the end of the last cut: `EnterMenu(0)` (first-boot intro 0x23) or `RequestScene(2)` (cutscene → gameplay).
- **Interpolation note (ADR-002):** sequence cameras and objects are keyed per frame, so there is one transform per step and interpolation works on them like on gameplay. Camera cuts happen at cut boundaries (`+0x110` changes) and must snap.

## 5. Replay and demo

- The recorder (gp 0x00410AF8) writes every frame into a ring buffer: unit states (players full, others compact) plus events (projectiles, effects, sounds).
  - Frame markers: 0x01234567 / 0x76544567.
  - Wrap marker: 0x76543210.
- **Ring size = remaining work heap rounded down to 256 KiB, minus 256 KiB** (0x0016DE48). This is why the heap is 98 % used in missions (`pcsx2-sessions-03.md`): the replay takes the rest.
- Playback (`UpdateReplayPlayback`, subs 0x10–0x12):
  - swaps the effect and particle pools with replay copies;
  - re-applies recorded states and events;
  - drives the replay camera (auto camera picked with `rand()` from a style table, or 5 manual cameras switched with L/R).
- **Demos** (attract mode) are replay recordings stored in DATA 0xAD + n (sub 5).
- Port consequence: the replay buffer size depends on the exact free heap. A different heap size changes how much of a mission can be replayed (ADR-001).

## 6. Pause and end menus

- Pause (sub 0x1D): freezes the player, lowers BGM to 1/4, lets the player change the special weapon (only some modes).
- End-of-mission and game-over menus: retry (sub 8, with a work-heap rewind), replay, back to the right menu page by mode (campaign 0x1A/0x1B/0x1C, free 0x18, trial 0x19, tutorial 5), or title.

## 7. Other mission helpers

- Trial mode keeps a ghost route per trial (0x002A0D34, 128 samples).
- Tutorial hints are a bitmask set by script op 0x43.
- Proximity message lists exist for mission ids 1 and 8 and a generic list.

## Risks for the port

1. **`rand()` reseeding**:
   - `srand(seed)` is called when the sky stars/clouds are generated (0x00113548, 0x001135C8) with seeds from the mission render settings.
   - The replay auto-camera, sky twinkle and sun jitter also call `rand()`.
   - Gameplay randomness that uses `rand()` therefore depends on exactly when these calls happen. Correction (step 9): AI, weapons and the player use the random **table** (`func_00100DC8`), not `rand()`; the table cursor is reset from the frame counter at the start of each gameplay frame (step 1 above). The `rand()` users are the sky, sun/moon jitter, replay auto-camera, radio and some effects (`render.md`). The port must keep every `rand()` call in the same order (the C runtime `rand` is game state: `skeleton-readiness.md` risk 6).
2. **Replay buffer from the free heap**: allocation sizes must match the PS2 (see above).
3. **Sub-states 0x19–0x1B** have no requester; they are classified as possibly unreachable and catalogued for `dead-code.md` once confirmed.
4. **Scripted segment for mission id 3 (subs 0x14–0x18)**: the code is understood, but which part of the game it is still needs a PCSX2 check (open question).
