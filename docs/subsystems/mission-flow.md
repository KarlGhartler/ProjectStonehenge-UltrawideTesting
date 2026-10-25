# Mission flow (scene 2)

The in-engine flight scene: loading, setup, rules and event scripts, in-engine cutscenes, replay and demo, pause and end menus. Per-function status: `../port/coverage-functions.csv`, subsystems `mission`, `sequence`, `replay`. Data format: `../formats/MISSION.md`.

The exact creation order of scene 2 up to the first gameplay frame (formerly `mission-load.md`) is the last section, "Mission load".

## Scene handler and sub-states

`SceneFlight` 0x0010A030, every frame:
1. `EnterFlightSubState` 0x0010A4E0: per-sub-state init, only on the frame after a change (`req & 0x30` with only 0x20 set). It zeroes the two frame timers, clears `ctx->flags & 0x40000`, and jumps through table 0x00403C60 (index `sub − 2`, 28 entries, every case a tail jump). Subs 2–5 go to `LoadFlightDataEntry`; the others to per-sub inits (6 → 0x0010FDD8, 0xA → 0x0010A230, 0xB → 0x0010A2D8, …).
2. Sets `ctx->flags |= 0x800`, except for subs 0xB and 0x1C; sub 0x1C also sets flag 0x200.
3. `table_0x002A1620[sub]()`.
4. Ticks the two per-player screen fades (0x0010A158).
5. Counts the frame timers in the flight scene object (+0x1654, +0x1658, +0x1660); 0x0010A228.
6. Runs the exit hooks (0x0010A5D0).

**Flight scene object** (`CreateFlightScene` 0x0010A668, 0x1810 bytes, gp 0x004104C4):

| Offset | Meaning |
|---|---|
| +0x000 + n·0x440 | view n (5 slots): flags (bit 31 active, bit 0 3D, bits 1–3 HUD layers, 0x40000000 clear Z), +5 player, +7 camera mode, +9 camera kind, +0x10 eye, +0x20 target, +0x30–0x7F camera inputs, +0xB0 camera object A, +0x270 camera object B (`camera-hud.md`), +0x218/+0x3D8 FOV distance |
| +0x1540 | 16 packet-bucket table heads for the frame's draw lists (counter +0x1651, `rendering.md`) |
| +0x1650 | number of views (1, or 2 = split screen) |
| +0x1653 | mission kind: 0 campaign/free, 1 trial (ids 0x15–0x19), 2 VS (0x14, 0x1A–0x1F, 0x28), 3 tutorial (0x20–0x27), 4 in-engine special (0x23–0x25) |
| +0x1654 / +0x1658 | frames since the sub-state change |
| +0x165C | simulation frame counter (random-table index, `../game/random.md`) |
| +0x1660 | mission timer (stops while paused) |
| +0x1664 | work-heap mark after setup (retry rewind) |
| +0x1668 | flags: bit 0 record replay this frame, 0x100 cutscene start, 0x10000 replay skip |
| +0x1670 / +0x1678 / +0x1680 | per-player fade level / time / target |
| +0x1688 | scripted-sequence drawing active |
| +0x168C / +0x1690 | loading-screen flags / alpha |
| +0x1694 | the 36 mission-data sub-blocks (`../formats/MISSION.md`) |
| +0x1754 / +0x1794 / +0x17D4 | player aircraft, second aircraft and demo-data sub-blocks |

**Sub-states** (`table_0x002A1620`):

| Sub | Handler | Role |
|---|---|---|
| 0, 1 | `FlightSceneEnter` 0x0010A898 | create the scene object, pick the mission kind → 2 |
| 2–5 | `StoreFlightDataPointers` 0x0010B038 | wait for the entry loaded by `LoadFlightDataEntry` and store its sub-block pointers: mission data (2), player aircraft (3), second aircraft (4, VS), demo data (5) → 6 |
| 6 | `func_0010FDE8` | load the aircraft engine sound banks (BD index per aircraft from table 0x002A1878, `../formats/CDP.md`) → 0x1E |
| 0x1E | 0x0010A998 | loading screen with mission objectives ("NOW LOADING", "PRESS START BUTTON" in trial mode after 2 s; `frontend.md`, loading screen) → 7 |
| 7 | `CreateFlightSystems` 0x0010AA50 | create terrain, units, effects, HUD, replay, script objects (work heap) → 8 |
| 8 | `StartFlightSystems` 0x0010AB58 | start sound, objects, HUD, players → 0x13 (kind 4), 0x10 (demo), 0xD (mission 10 opening), else 0xA |
| 0xA–0xC | `FlightPlayFrame` 0x0010B150 | 0xA mission start (pre-mission cutscene when `flags & 0x100`), 0xB gameplay, 0xC mission end |
| 0xD–0xF | 0x0010B500 | scripted mid-mission sequences for mission ids 4, 5, 10, 14, 15 (`game[0x2C]` stage, BGM/length table 0x002A16A0) |
| 0x10–0x12 | 0x0010B930 / `UpdateReplayPlayback` 0x0016E2D0 | replay and demo playback, then results / menu |
| 0x13 | 0x0010BA38 | in-engine special sequence (opening/ending ids 0x23–0x25) |
| 0x14–0x18 | 0x0010BAB8 / 0x0017A1B8 | **carrier landing** in campaign mission 3 (mission id 3), started when the player crosses the return line (objective type 2): AWACS "Approaching return line. Follow the controller's orders for landing.", HUD "RETURN TO CARRIER", landing guidance (sub 0x15, `func_00166A10`); timer 0x2A30 frames, fail and retry when leaving the ±32768 area or crashing; approach parameters differ for the aircraft in mask 0x00204114. When mission flag 4 is set (mission complete), touchdown leads to 0x17 (success → replay) and START skips to 0x18 (results); otherwise touchdown leads to 0x16 and START opens the rearm menu (0x1D) (confirmed in PCSX2, `../pcsx2/sessions.md` run 04) |
| 0x19–0x1B | 0x0010BB98 / 0x0017AC68 | **carrier takeoff** after a rearm (mission id 3): requested by the rearm-menu item TAKEOFF (`func_0017DBC0`); 0x19 intro, 0x1A takeoff run (timer 0xE10, radio messages 0x12/0x13), 0x1B result (success → back to gameplay sub 0xB). Confirmed in PCSX2: 0x19 → 0x1A → 0x1B → 0x0B, normal control afterwards (`../pcsx2/sessions.md` run 05). Also at the airbase of campaign mission 6 (mission id 5; User's PCSX2 screenshots 2026-10-23: LANDING SUCCESSFUL, TAKEOFF / CHANGE WEAPON, "cleared for takeoff", then "Altitude restrictions canceled. Return to your mission"). 0x1A ends after 5 s in the air (counter +0x27C, `../port/validation.md` §2af TO-1) |
| 0x1C | 0x0010BC68 | game-over menu |
| 0x1D | 0x0010BCD8 | carrier **rearm menu** (TAKEOFF, CHANGE WEAPON), mission id 3 only (sub-state 0x1D seen at `func_0017A7C8` in PCSX2, `../pcsx2/sessions.md` run 05) |
| 9 | 0x0010BA98 | cameras and drawing only |

Frame order in gameplay (`FlightPlayFrame`):
1. `SetRandomIndex(simulation frame)` (`../game/random.md`);
2. terrain/world update (0x00139FE8, 0x00144CC0, 0x0013EC20/0x0013EC38);
3. `FlightSimStep` 0x0010C320 (objects, collisions, AI, players, effects, HUD, radio, rules, replay recording);
4. `UpdateViewCameras` 0x0010CB80;
5. players (cutscene or controlled);
6. sound listeners;
7. cutscene skip;
8. `BuildFlightDrawLists` 0x0010BD68 (`rendering.md`).

## Mission data loading (`LoadFlightDataEntry` 0x0010AF10)

Called by tail jump from `EnterFlightSubState` on the frame after a change to subs 2–5. It takes **no arguments**: it sets `flags |= 0x40000`, picks the DATA index from `ctx->sub` and `g_GameState`, and calls `AllocAndLoadDataEntry(idx)` at 0x0010B020 (index in `a2`, copied to `a0` in the delay slot). The result goes to gp 0x004117A8.

| Sub | DATA.CDP index | Meaning |
|---|---|---|
| 2 | `id + 0x52` when `id < 0x13` **and** bit `game[0x00]` (player aircraft id) of `g_AltMissionAircraftMask` (gp 0x004104C8, constant 0x00204114 = aircraft 2, 4, 8, 14, 21) is set; otherwise `id + 1` | per-mission data; `id` = `game[0x27]` (0x0029F377) |
| 3 | `0x65 + a`, `0x7D + a` or `0x95 + a`; `a` = `game[0x00]`, variant `game[0x01]` (0/1/2) | player aircraft (3 variants × 24, `../formats/AIRCRAFT.md`) |
| 4 | same formula with `game[0x10]` / `game[0x11]` | second aircraft (VS) |
| 5 | `0xAD + game[0x2B]` | title demo data (DATA 173–175) |

The branch is at 0x0010AF48–0x0010AF70. The mask is a never-written `.sdata` constant read only at 0x0010AF54.

Where the mission id comes from:
- `game[0x27]` is set to 1 by `InitGameStateGlobals` (H) at boot. The menus keep the campaign number in `g_MissionNo` (u16 0x002A15B8, 1..18) and convert it with `GetMissionId(list, n)` 0x00226378 = `table_0x00384800[list_offset + n − 1]`:
  - list 0 (18 campaign missions): mission 1..18 → id 1, 2, 3, 16, 4, 5, 6, 7, 8, 9, 17, 10, 11, 18, 12, 13, 14, 15;
  - list 1 (5): ids 21, 22, 23, 25, 24; list 2 (5): 32, 33, 34, 38, 39; list 3 (8): 27, 26, 28, 29, 31, 30, 20, 40 (trial, tutorial and extra missions, H per list).
- Writers of `game[0x27]`: `MissionTitleUpdate` 0x00230308 (`GetMissionId(0, g_MissionNo)`), `AdvanceCampaignMission` 0x00226500 (next mission), scene 1 0x00211C58, `ApplyCampaignSelection` 0x00226A98, `func_00230A78`, `func_00233808`, `func_0023A550`, `MissionSelectAction` 0x0023AC70. Fixed ids: 0x23 no-save intro (scene 6, played at every boot when card slot 1 has no AC04 save), 0x24 after mission 18, 0x25 for the opening sequence before mission 1 (`AircraftSelectRun` 0x00235AB8).
- So the DATA index for campaign mission n is `table[n−1]` + 0x52 or + 1 depending on the **player's aircraft**. DATA 1–100 hold two sets of mission data: `id + 1` (ids 0–99) and the aircraft-specific alternates `id + 0x52` for ids 1–18 (DATA 83–100). What differs between the two sets: alternate unit and model sets, `../formats/MISSION.md`.
- Ids ≥ 0x13 always use `+ 1`. Ids 0x29–0x50 (DATA 42–81) are produced by no table found.

PCSX2 session 7 (`../pcsx2/sessions.md` run 02, breakpoint 0x0010B020) confirms sub 2 = mission data, sub 3 = player aircraft and the formula: with aircraft 2 (entry 0x67), campaign missions 1–4 (ids 1, 2, 3, 0x10) load DATA 83, 84, 85, 98; id 0x25 loads DATA 38 with aircraft entry 0x7C (aircraft 0x17). Mission 1 enters scene 2 twice (pre-mission cutscene, then gameplay), mission 2 once. With an aircraft outside the mask the same branch takes `id + 1` (e.g. DATA 2, 3, 4, 17 for campaign missions 1–4); the branch was checked in the disassembly (0x0010AF48–0x0010AF70: `srav` of the mask by the aircraft id), so no PCSX2 run is needed.

## Mission setup

- `SetupMissionUnits` 0x0010E758 reads block 22. When it is a 3-entry container, it picks the difficulty variant: `game[0x28]` ≤ 1, = 2, ≥ 3.
- 0x18-byte placement records → unit structs pointed to by 0x002A1F70[i] (255 pointers to 0xA0-byte structs in the work heap, allocated by 0x0011B948).

| Unit offset | Meaning |
|---|---|
| +0x00 / +0x10 / +0x20 | position, rotation (Euler), velocity |
| +0x40 | definition record (one of 10 typed tables: record sizes 0x24, 0x14, 0x20, 0x24, 0x20, 0x28, 0x18, 0x20, 0x18, 0x14) |
| +0x44 | class state (aircraft or ground object, `ai.md`) |
| +0x48 | static flags: side (`&3`, 1 = enemy), 0x10 ground target, 0x100, 0x800000, 0x1000000/0x2000000 (from the record), 0x20000000 target, 0x8000000 difficulty-dependent |
| +0x4C | dynamic flags: 0x1000000 alive/active, 0x10000000 on radar, 0x20000000 lockable |
| +0x50 | hit flags (`weapons.md`) |
| +0x54 | class: 0 player aircraft, 1 AI aircraft, 2 ground/naval |
| +0x58–0x5B, +0x65 | group, AI parameters |
| +0x5A | model state (`rendering.md`) |
| +0x5E | hit points |
| +0x60 | score value |
| +0x69 | model type code |
| +0x70 | radius |

- Players: slot from `g_GameState`; aircraft, colour and special weapon from the hangar.
- `func_0010FA70` also builds the rules and events from block 28 (`func_0011A3D8`) and creates the mission result record (`results-startup.md`).

## Mission rules and event script

Objects:
- rules: gp 0x004104F0, 0x70 bytes;
- status: gp 0x004104F4, 0x90 bytes. +0x20 flags: 1 success, 2/8 failure kinds, 0x100 gameplay running, 0x1000; +0x24 mission time; +0x38 segment countdown; +0x7C kill count; +0x7D result bits;
- event tables: gp 0x004104F8, 0xA8 bytes.

`UpdateMissionRules` 0x0011A390 dispatches on the sub-state:
- 0xA (0x0011A128): runs the start list; after the intro time shows "mission start" → 0xB.
- 0xB (0x0011A208): `CheckMissionObjectives` 0x001191B8 (or the VS rules 0x00118CC0), the mission timer and limit, timed events.
  - Phase change: when the rules state (`*0x004104F4` +0x20) has flag 8, it becomes flag 4, the timer +0x24 restarts, `func_0010FB30` activates the next phase's units (unit flags48 0x100; with 0x8000000 only when `g_GameState[0x28]` ≥ 2 and 0x002A0318 bit 0) and retires the remaining ones, the waypoint list is reset (`func_0017B2F0`) and the next phase's mission events run (`RunMissionEvents`). (`../port/validation.md` V-5.)
- 0xC (0x0011A340): `MissionEndRules` 0x00119910 (or VS 0x00119000).

Objective types (`rules+0x10 & 3`):
- 0: points/kills quota;
- 1: number of targets destroyed (+0x28);
- 2: reach a position (z < +0x1C; mission id 3 goes to sub 0x14 instead of 0xC).

**Event script** (`RunMissionEvents` 0x0011AAE0): 4-byte-aligned commands, terminated by 0xFF.

| Op | Effect |
|---|---|
| 0x00 / 0x07 | objective type / mode bits |
| 0x01 / 0x02 / 0x03 | time limit (seconds) / quota / target count |
| 0x04, 0x0D, 0x09 | rule and status flags |
| 0x05 / 0x06 / 0x08 / 0x0C / 0x0E / 0x0F | positions, limits, area, timers (×6 frames) |
| 0x0A | trigger event group n |
| 0x0B | start scripted sequence n (`game[0x2C]`, sub 0xD) |
| 0x10 / 0x11 / 0x16 / 0x19 | radio/subtitle messages (`QueueRadioMessage` 0x00252F80 / `func_00252FB8`), conditional on kills, with timer |
| 0x12 / 0x15 / 0x17 / 0x18 | waypoint, proximity messages (three lists) |
| 0x13 / 0x14 | status bytes |
| 0x20–0x24 | unit activate / deactivate / activate at position (relative to another unit) / set HP / kill |
| 0x25 / 0x26 / 0x27 / 0x28 / 0x29 | AI flags, non-target, unit flags |
| 0x2A | explosion effect at a unit (`func_0020C2A0`, `effects.md`) |
| 0x30–0x33 | timers |
| 0x34–0x37 | difficulty-conditional quota/area (difficulty ≥ 2 or < 2) |
| 0x40–0x44 | status bytes, tutorial hints (0x43) |
| 0x50–0x53 | conditional messages, parameters, flags |
| 0x60–0x63 | BGM play, stop, change with fade |

Event sources: start/play lists; timed lists `{u16 time/6 s, u16 offset}` (0x0011A7D8); per-group destruction thresholds (`func_0011A920`, TriggerEventGroup(n) in the notes).

## In-engine sequences (cutscenes)

- `SetupInEngineSequence` 0x00176DD0 loads block 29 entry `game[0x2C]`: parts 0 cuts, 1 model table, 2 animated sprite sets (GMA, 0x00107BD0), 3 still images (GIM, 0x00106938). State 0x150 bytes at gp 0x00410B18. BGM 0x30/0x33/0x32 for ids 0x23/0x24/0x25.
- `UpdateInEngineSequence` 0x00177058 is a baked keyframe player: one record **per frame** per track. Track ids:
  - 0x10 camera; 0x11 look-at;
  - 0x20–0x24 object transforms and animation channels (aircraft, skinned characters of the sequence character engine, `rendering.md`);
  - 0x40–0x49 object flags and effects;
  - 0x70 still-image overlay (0x00106A50); 0x71 animated-sprite overlay (0x00107CC0);
  - 0x80–0x84 fades and colour.
- Record layout (translated 2026-10-18, `src/game/sequence.cpp`): the cut header holds +4 track count, +6 length, +8/+0xC/+0x10 origin, +0x14 track offsets; each track is a type byte, a slot byte and one entry per frame (stride by type: camera 0x18, look-at 0x10, 0x20 / 0x22 / 0x23 / 0x49 0x1C, 0x21 0x24, 0x24 0x28, 0x40 / 0x80-0x82 4, 0x41 / 0x83 / 0x84 8). Positions are stored with y and z negated and the cut origin added (x + ox, -y - oy, -z - oz); angles are degrees (Euler matrix X, Y, Z through `func_00103290`). Camera FOV = 240 / tan(fov / 2) (the game's `tanf`, now in `src/sce/libm.cpp`), roll wrapped by K 0x0040EB90-0x0040EB98.
- Per frame: VU0 program S uploaded (unless +0x119 bit 2: no models), units 0/1 reset for drawing, overlay slots cleared, tracks applied, world origin and unit hierarchies, lights (`func_001FF6C0`, 8 slots 0x00420730), the frame advanced once the first-frame wait (+0x11A, from 0x00353C2C & 0x24) is over, camera through `func_00178DF8` (single view, projection 640 x 480 / 360 in 16:9, character engine view matrices and fog).
- End: the last cut, or 32 frames after Start (+0x119 bit 8, counter +0x11B, with a fade-out drawn by `func_00179298`): sub 0xE -> 0xF (fade 0.5); id 0x23 `EnterMenu(0)`; id 0x24 -> mission 0xF; id 0x25 -> game[0] = 2, mission 1; then `RequestScene(2)` and a music fade.
- Draw: `func_00179008` (world pass: sequence-time effects, effect markers between a ZMSK set / restore of ZBUF_1, character models into the sequence list) and `func_00179298` (screen pass: sky-colour overlay, two fades, sprite and image overlays, depth walls, skip fade), both from `BuildFlightDrawLists` bucket 9 in sub 0x13 or with +0x1688.
- Seen in the port (2026-10-18): the opening of id 0x25 plays (carrier deck, F-4E, deck crew) and its skip leads into mission 1; mission 12's scripted sequence (sub 0xD-0xF) plays into gameplay.
- Interpolation (ADR-002): sequence cameras and objects are keyed per frame, so there is one transform per step and interpolation treats them like gameplay. Camera cuts happen at cut boundaries (+0x110 changes) and must snap.

## Replay and demo

- The recorder (`RecordReplayFrame` 0x0016E8B8, ring at gp 0x00410AF8) writes every frame into a ring buffer: unit states (players full, others compact) plus events (projectiles, effects, sounds).
  - Frame markers 0x01234567 / 0x76544567; wrap marker 0x76543210.
  - Unit snapshots: `func_00135D60` allocates, `func_00135E10`/`func_00135EF8` save and restore every unit (0x48 bytes), `func_00135FE0`/`func_00136240` the players (0x50 bytes). Player animation inputs are **quantised to u8 (×255)** in the snapshot (`ai.md`).
- **Ring size = remaining work heap rounded down to 256 KiB, minus 256 KiB** (0x0016DE48). That is why the heap is 98 % used in missions (`../pcsx2/sessions.md` run 03).
- Playback (`UpdateReplayPlayback`, subs 0x10–0x12):
  - swaps the effect pools with replay copies (`func_00170460`, `func_001704A0`, `func_001704C0`) and re-inits effects (`InitEffects`);
  - re-applies recorded states and events (`func_00170520` dispatch: explosion/smoke `func_00170598`, trail/flare `func_001706D0`, debris/fire `func_001708D0`, sounds `func_00170F98`);
  - exits with `EnterMenu(0)` after a demo, `EnterMenu(0x1C)`, or `RequestScene(5)` (results).
- Playback details (translated 2026-10-18, `src/game/replay.cpp`): recorder R (+0 flags: 1 restart, 2 end reached, 0x10 start marker seen; +2 kind; +4 ring start; +8 write; +0xC read; +0x10 playback start; +0x18 loops). Kind (set on entry by `func_0016E010`): 0 with scene flag +0x1668 bit 0x10000 (playback asked for during the mission), else 5 when game[0x29] == 4 (demo), 4 when game[0x29] == 3 with status flag 0x80, 3 status 0x10000000, 1 status 1, 2 status 0x80, 6 scene +0x1653 == 2, 0 otherwise. Sub 0x10 starts 600 (kind 2) / 900 (kind 0) frames before the end or at the oldest frame (`func_0016E950`) and counts the frames for the timer; sub 0x12 continues by kind (0 back to sub 0xB, 1 / 6 results, 2 game-over menu 0x1C, 3 results or rearm menu 0x1D, 4 `EnterMenu(0x1C)`, 5 `EnterMenu(0)`). Start fades out. Record types: 0 unit (players 0x30, vehicles 0x30, others 0x1C), 1 weapon (0x28: launch replayed through the projectile spawners, follow camera and lock records), 2 effect (1, 2 or 3 vectors; 16 replay effect slots *0x00410B04 keep looping effects alive), 3 sound.
- **Replay camera** (state 0x1E0 bytes, gp 0x00410B0C, `func_00175D08`; translated 2026-10-18, `src/game/replay_camera.cpp`), frame `func_00176060`:
  - manual cameras, table 0x002B77D8 index +0x1AE (R1 / L1 step 0-5, 0-4 in the special situations of +0x1A4 bits 0x40/0x80/0x100): 0 auto `func_001756C0`, 1 orbit `func_00173670`, 2 projectile follow `func_001759E8`, 3 cockpit look-around `func_00173020`, 4 player view `func_00173468`, 5 missile-then-target `func_00173A98`, 6 path-table camera `func_00175890` (forced on terrain set 1 and mission ids 0x29-0x3A / 0x3D-0x4E, path tables 0x002B7670 / 0x002B7708, chase `func_00175420`);
  - auto styles, table 0x002B77B0 index +0x191, picked on each cut with `rand() % 10` over the u16 mask +0x1D0 + 2n (n from the situation: 3/4/5 special, 1 near the ground, 2 with a target, 0) and re-drawn while it repeats: 0 `func_001744B8` fixed view, 1 `func_00174058` chase with drift, 2 `func_00173670` orbit, 3 `func_00172EF0` cockpit, 4 `func_00172850` aircraft mounts, 5 `func_00172098` fly-by, 6 `func_00172450` low fly-by, 7 `func_00171E88` two-unit view, 8 `func_00171AE8` / 9 `func_00171CD0` fixed offsets;
  - helpers: obstacles on a segment `func_001712F8`, obstacle hit point `func_00171460`, per-frame situation and nearest path point `func_00171640`, follow views `func_00174640` (missile) / `func_001748A8` (special weapon, also from the weapon-event camera `func_00175B08`), default external view `func_00172B78`, dead player `func_00175BA0`;
  - every frame: shake (random target every 10-19 frames, curve 0x00246D98 / 0x00246DE8) and drift (+0x100), both from `rand()`.
- **Demos** (title attract mode) are replay recordings stored in DATA 0xAD + n (sub 5), started by `StartTitleDemo` 0x0022CC68.
- Port consequence: the replay buffer size depends on the exact free heap; a different heap size changes how much of a mission can be replayed (ADR-001).

## Pause and end menus

- **Pause menu** (not a sub-state: ctx flag 0x100 during `FlightPlayFrame`; reset `func_0017DCC0`, drawn by `func_0017E118`). Items are 0x38-byte records from 0x002B8108 (label pointer, option labels, get/set callbacks); the list depends on the mission kind:

  | List | Kind | Items |
  |---|---|---|
  | 0x002B8370 | campaign / free | RETURN TO GAME, REPLAY, RETRY, QUIT MISSION, CAPTIONS, HUD |
  | 0x002B8390 | campaign / free, replay shorter than 0xB5 frames | the same without REPLAY |
  | 0x002B83D0 / 0x002B83F0 | trial | RETURN TO GAME, (REPLAY,) RETRY, QUIT MISSION, HUD |
  | 0x002B8410 | VS | RETURN TO GAME, RETRY, QUIT MISSION |
  | 0x002B83B0 | tutorial | RETURN TO GAME, RETRY, QUIT MISSION |

  **The HUD item (on/off, `func_0017DB28`/`func_0017DAF0`, ctx flag 0x20000) is only in the list after the game has been cleared once** (0x002A0318 bit 0): `func_0017DCC0` sets the item count one lower otherwise (confirmed: no HUD item before a clear, `../pcsx2/sessions.md` run 04). Callbacks: RETURN TO GAME `func_0017D938`, REPLAY `func_0017D958`, RETRY `func_0017D9A8`, QUIT MISSION `func_0017DA28`, CAPTIONS `func_0017DAD8`/`func_0017DAA8`.
- **Carrier rearm menu** (sub 0x1D, mission id 3; enter `func_0017A7C8`, reset `func_0017E228`, draw `func_0017E460`): list 0x002B8450 with TAKEOFF (`func_0017DBC0` → sub 0x19) and CHANGE WEAPON (`func_0017DC48`/`func_0017DBD8`; the option labels "TEST / 00..02" at 0x002B8090 are templates overwritten with the owned special weapons as "NAME / ammo"). It freezes the player, switches to the cockpit view and lowers BGM to 1/4.
- Game-over menu (sub 0x1C, `func_0017E210`) uses list 0x002B8430 (RETRY, QUIT MISSION, RETURN TO TITLE `func_0017DB40`).
- End-of-mission handling (`func_0017E210` and the menu actions): retry (sub 8, with a work-heap rewind), replay, back to the right menu page by mode (campaign 0x1A/0x1B/0x1C, free 0x18, trial 0x19, tutorial 5), or title.

## Other mission helpers

- Trial mode keeps a ghost route per trial (0x002A0D34, 128 samples).
- Tutorial hints are a bitmask set by script op 0x43.
- Proximity message lists exist for mission ids 1 and 8 and a generic list (mission 0x12: `func_00135C68`/`func_00135C90`, `ai.md`).
- `UpdateChatterTimers` 0x00259588 (simulation step): periodic chatter and sun-flare event timers (`sound.md`).
- Centre messages ("MISSION ACCOMPLISHED", …): `ShowCenterMessage` 0x0016DDB8 (`camera-hud.md`).

## Port notes

- Randomness: `../game/random.md` (the random table, `rand` users, draw-phase calls).
- Replay buffer from the free heap: allocation sizes must match the PS2.

## Mission load: scene 2 creation order (block A of slice 5)

Exact creation and allocation order of scene 2 up to the first gameplay frame, function by function (`../port/slices.md` §B P5; inventory and status per function: `../port/s5-blockA-inventory.csv`). The work heap is a bump allocator (`WorkHeapAlloc(size, align)` 0x00101000; `WorkHeapAlloc(0, 0)` returns the current mark, `func_00101090(mark)` rewinds to it) and mission 1 fills it to 98.4 % without checks (`../game/memory.md`), so the order and every size below must be reproduced exactly. Sub-state flow: section "Scene handler and sub-states" above. Data blocks (+0x1694…): `../formats/MISSION.md`.

Notation: `A(size, align) → where` is one `WorkHeapAlloc`; `game[n]` is the game-state byte n (`g_GameState`, 0x0029F350); `S` is the flight scene object (gp 0x004104C4).

### Sub 0/1: `FlightSceneEnter` 0x0010A898

1. `CreateFlightScene` 0x0010A668:
   - when `game[0x24] == 0`: if bytes `game[0x20..0x27]` masked with 0x00FFFF0000000000 equal 0x0000020000000000 (i.e. `game[0x25] == 0` and `game[0x26] == 2`), `game[0x26] = 1`; then if `game[0x25] == 1` and `game[0x26] != 0`, `game[0x26] = 0`;
   - `A(0x1810, 0x10) → S` (gp 0x004104C4);
   - clears the data-block pointers: S+0x1694..0x1753 (48 words, written backwards from +0x1750: the 36 mission sub-blocks and more), S+0x1754..0x1793 and S+0x1794..0x17D3 (2 × 16 words: player and second aircraft), S+0x17D4..0x17E3 (4 words: demo data); the rest of the 0x1810 bytes is not cleared here;
   - S+0x1650 = 0; the 5 view slots reset by `func_0010D6A0(S + n·0x440)` for n = 0..4 (each slot 0x440 bytes, two camera records at +0xB0/+0x270);
   - S+0x1652 = 0, +0x1660 = 0, +0x1654 = 0, +0x1658 = 0, +0x166C = 0, +0x1688 = 0, u64 S+0x17E8 = 5, +0x1668 = 0;
   - main context: clear RGBA 0, draw height 0x1C0, display 0x200 × 0xE0, draw width 0x200, frame PSM 0 (CT32), Z PSM 0x31 (Z24), Z test 3 (GREATER), draw mode 0, display flags |= 0x8000; `BuildDrawEnvPackets`;
   - `game[0x2D] = 0`; S+0x1690 = 1.0f; S+0x168C = 0; when `game[0x2A] == 1`: S+0x168C = 1 and `func_00239640` (loading-screen variant).
2. Main context flag 0x20000 cleared. Mission kind S+0x1653: sub 1 → 4; else by `game[0x27]` (mission id): 0x15–0x19 → 1 (trial), 0x23–0x25 → 4, 0x20–0x27 → 3 (tutorial), 0x14–0x28 → 2 (VS), otherwise 0 (ids ≤ 0x13 always 0).
3. `AllocDoubleBuffer(main context dbuf, 0x10000)`, `SwapDoubleBuffer` → current packet buffer. → sub 2.

### Sub 7: `CreateFlightSystems` 0x0010AA50

In this order (each line: the function, then its allocations and initial state):

1. `func_001118C8` (sky/render settings): gp 0x004104D4 = block 24 (S+0x16F4) & 0x0FFFFFFF (physical address); `func_00113708(that)`: `A(0x1080, 0x10) → gp 0x004104E8`, gp 0x004104EC = block 26 (S+0x16FC) + 0x70, `func_00113750(block 24)`; `A(0x40, 0x10) → gp 0x004104D0`; `FlushCache(0)`, `sceGsSyncPath(0, 0)`, `sceDmaSend(GIF, block 25 & 0x0FFFFFFF)` (the pre-built texture-upload chain), `sceGsSyncPath(0, 0)`.
2. `CreateWorldRenderObjects` 0x0013EA28:
   - `func_00139520(map object holder *0x00410694)` — map object: `A(200, 0x10) → M` (stored at holder+0); two `A(0x40, 0x10)` at M+0x58 and M+0x58+100 (the two map entries are 100 bytes apart); then on the holder `h`: h[0x40] `A(0x1000)`, h[0x41] `A(0x40)`, h[0x44] `A(0x2000)`, h[0x45] `A(0x4000)`, h[0x42] `A(0x1000)`, h[0x43] `A(0x1000)`, h[0x1D] `A(0x2000)`, h[0x1E] `A(0x2000)`, h[0x20] `A(0x80)`, h[0x21] `A(0x80)`, h[0x49] `A(0x2000)`, h[0x4A] `A(0x2000)`, h[0x4B] `A(0x800)`, h[0x46] `A(0x400)` (all align 0x10, in this order; h[n] = word n). Initial words: h[0x22..0x25] = 0, h[0x2B] = h[0x2C] = h[0x3D] = h[0x2E] = 0, h[0x128] = 1, h[0x2A] = 4096.0f, h[0x29] = *0x0040E4F0, h[0x129] = *0x0040E4F4, u16 h[0x3C] = 0xF30, h[0x28] = 0x80F0C080, h[300] = 0x80202020, h[0x27] = 0x80000000, bytes +0xB4..+0xB6 = 0x40, 0x7F, 0x20, u16 h[0x3B] = 0xF30; then `func_001390D0(h)`, `InitLightSet(*0x00410688)`, `SetLightTexture(*0x00410688, block 19, 0x1E00, 0x3F08)`.
   - (in `func_00139520`) `func_001390D0(holder)` writes the map renderer's fixed GIF/DMA templates into the holder: four mip-level upload blocks at +0x2B0 (0x1C words each: DMA `cnt` tag 0x10000006, GIF A+D tags for BITBLTBUF (DBP 0x2900, 0x2D00, 0x2E00, 0x2E40 for k = 0 … 3, i.e. each level after a 0x400 >> 2k-block area; DBW 8 >> k, PSM 0x13 = T8), then TRXREG (0x40 >> k square), TRXPOS (0), TRXDIR (0, high word unwritten), in that order, and an IMAGE tag (EOP) of 0x100 >> 2k qwords) and the byte tables at +0x150..+0x24F (per-entry triples used by the terrain draw), tags at +0x130, +0x140, +0x1A0, +0x258..+0x290.
   - `SetupMapFromMissionData` (H) 0x00139B70(holder) — copies block pointers into the map object M: M[0x18] = 1; M[0] = block 10 (S+0x16BC), M[1] = block 9 (+0x16B8), M[2] = block 12 (+0x16C4), M[3] = block 11 (+0x16C0), M[4] = block 13 (+0x16C8), M[5] = block 6 (+0x16AC), M[6] = block 5 (+0x16A8), M[7] = block 8 (+0x16B4); block 7 (+0x16B0): when it is a container of 4, each sub-container n gets `A(count·4, 4) → M[8 + n]` filled with its item pointers; otherwise M[8..11] = block 7. M[0xC] = block 15 (+0x16D0), M[0xD] = block 14 (+0x16CC). Block 16 (+0x16D4) container of n models: `A(n·0x28, 0x10)` (model records) then `A(n·0x4C, 0x10) → M[0xE]` (instances); per item `BindModelData`, `CreateModelInstance`; M[0x12] = n. Block 17 (+0x16D8) container of n: `A(n·4, 4) → M[0xF]`, per item (a container of k) `A(k·4, 4)` filled with item pointers; M[0x13] = n. `A(0x28, 0x10) → M[0x10]`, `InitGimTexture(M[0x10], block 27)`, TBP 0x3A00, CBP 0x3F20. Block 18 (+0x16DC) container of n: `A(n·4, 0x10) → M[0x11]` filled; M[0x14] = n. M[0x17] = block 24, M[0x15] = block 20; `func_00139718(holder, block 31, 1)` — **block 31 is the second map entry** (entry 1 at M + 100): when the container is not empty, entry +0x60 |= 1 and its words get the same layout as entry 0 from the item indexes 0–6 → w0–w6, 8 → w7, 7 (container of 4 → four `A(k·4, 4)` lists, else the same pointer 4×) → w8–w11, 9 → w12, 10 → w13, 11 (models: `A(n·0x28)`, `A(n·0x4C) → w14`, w18 = n), 12 (groups: `A(n·4, 4) → w15` + per group `A(k·4, 4)`, w19 = n), 13 (GIM: `A(0x28, 0x10) → w16`, TBP 0x3A00, CBP 0x3F20), 14 (pointer list: `A(n·4, 0x10) → w17`, w20 = n); an empty block 31 leaves entry 1 absent (+0x60 = 0); holder[1] = 0; M+0xB8 = block 32, M+0xC0 = block 33; `func_00139FE8(holder)`; holder words 0x14..0x17 = 8192.0f, *0x0040E4F8, *0x0040E4FC, 32768.0f; when the MTXT version (first word of M[0x15], block 20) < 0x6F: the warning printf (double-precision `__extendsfdf2`), MTXT[9] = MTXT[8], MTXT[0xB] top byte = u16 at block 24 + 0x12E.
   - `func_00144728(*0x0041069C)` (static objects; size depends on the map data): for each of the 2 map entries E (M + n·100) with flag E+0x60 bit 0 set: count c = E+0x48, object table T = E+0x38 (0x4C bytes per object); `A(c·4, 4)` → +0x7020[n], `A(c, 1)` → +0x7054[n], `A(c, 1)` → +0x7034[n], `A(c·4, 4)` → +0x703C[n], `A(c·4, 4)` → +0x7044[n]; per-object counts are histogrammed over the 16×16×16×16 cell grid (E+0x30 cell table, E+0x34 per-cell object lists, 0xFF = empty); then per object kind u: `A(count·4, 4)`, `A(count, 1)`, `A(count, 1)`, and per placed instance `A(model size·64, 0x10)` holding the instance's baked packet, built by `func_00143C10(object, buffer, translation matrix, 0x80)` with the translation ((cell x − 0x80)·2048, 0, (cell z − 0x80)·2048) (VU0 `sceVu0UnitMatrix`/`sceVu0TransMatrix`); finally `func_00144CC0`.
   - `func_00147BF0(*0x004106A4)` (aircraft models, `../formats/AIRCRAFT.md`): ten small arrays (`A(8, 0x10)` × 5, `A(8, 4)`, `A(2, 1)`, `A(8, 0x10)` × 3, in the order [0], [1], [0xC], [0xD], [5], [0x11], [7], [2], [3], [4]); for each of the 2 aircraft slots with data (S+0x1754 + n·0x40): LOD count 4, or 5 when the first item's first word > 0xB3; `A(lods·0x4C, 0x10)`, `A(lods·0x28, 0x10)`, models from items 2…; item 1 (container of k): `A(k·4, 0x10)`; skin block S+0x1758 (container of k ≠ 0): `A(k·4, 0x10)`, `A(k·0x28, 0x10)`, GIMs with skin 0 at TBP 0x0E00 / CBP 0x3F04 and skin 1 at TBP 0x2300 / CBP 0x3F14 (PSM forced to 0x1B = PSMT8H, TBW from the width); extra block S+0x175C unless it has 5 items: `A(0x130, 0x10)`, `A(0xA0, 0x10)`, 4 models from items 3–6, item 2 kept, flag = 1. Then `func_00149FB8(*0x004106A8)` (shared models, blocks 3/4), [0xF] = 3, `A(8, 4) → [6]` cleared, per slot camera defaults (0x80000000 markers, 0, 0, 1.0, 1.0), [0x10] = 0.
   - `func_001381F8(*0x0041068C)` (sky/cloud renderer): `A(0x40C0)`, `A(0x40C0)`, `A(0x420)`, then 4 × `A(0x40000)` (all align 0x10) — 1 MiB of buffers; byte parameters (10, 3, 2, 10, 10, 10, 10, 3), `func_00137550`, DMA/GIF tag templates (words 0xE–0x25 as listed in the code).
   - `A(0x120, 0x10) → *0x00410698` (per-frame sequence-model list).
   - (in `func_001118C8`) `func_00113750(block 24)`: `func_00113548(gp 0x004104E8, 400, u16 block24+0x80)` and `func_001135C8(gp 0x004104E8 + 0xC80, 0x80, u16 block24+0x132)` fill the sky buffer (star and cloud generators, section "Sky star and cloud tables" below; the same call is made again in sub 8 by `func_00111950`).
3. `func_0011B948` (units): `A(0x90, 4) → *0x00410640`; for each of 2 players: 4 × `A(0x14, 4)`, 5 × `A(0x14, 4)`, `A(0x1C, 4)`; then **255** × `A(0xA0, 0x10)` → the unit pointer table 0x002A1F70; mission id 10 → `func_00133A58` (`A(0xC, 4) → *0x00410674`, `func_00133A80`), id 15 → `func_00134FA8` (`A(0x50, 0x10) → *0x00410670`, words +0x30..+0x38 = −1, byte +0x3C = 0, +0x40 = 0); `func_0011BA90` (reset: list bytes, then all 255 units cleared field by field with the VU0 zero vector `sqc2 vf0`, unit index at +0x55; id 10 → `func_00133A80`, id 0x12 → `func_00135C50`). Byte +0x8A of the list = 0x34 for mission 1, 0x39 for 0x10, 0x12 for 0xE/0xF, else 0xFF.
4. `CreateProjectilePools` 0x0014B9D8: `A(0x8A50, 0x10) → *0x004106AC` (7 projectile pools with occupancy masks at +0x8A00…), `A(0x160, 0x10) → *0x004106B0`, `ResetProjectiles` (counters and masks cleared, then the 7 pool resets, each over its records in the projectile block *0x004106AC: `func_0014BC80` bullets 256 × 0x40 at +0 (+0x30, +0x34 = 0); `func_0014CBD0` missiles 32 × 0x60 at +0x4000 (+0x50 = 0, written from the last record down); `func_0014F5B0` special weapons 32 × 0x80 at +0x4C00 (+0x50, +0x70 = 0); `func_0014DA98` 64 × 0x30 at +0x5C00 (+0, +4 = 0); `func_0014DF40` 64 × 0x40 at +0x6800 (+4 = 0, last record first); `func_0014E890` 64 × 0x40 at +0x7800 (+4 = 0, last record first); `func_0014ECA0` 8 × 0x40 at +0x8800 (+0 = 0, last record first)).
5. `func_00154638`: `A(0x625A0, 0x10) → *0x004106B4` (the effect pools, 394 KiB).
6. `func_00136518`: 2 × (`A(0x18, 4) → *0x00410680[n]`, `func_00136570(n)`: words 5 = 4 = −1, others 0) (projectile-follow camera states).
7. `func_00176908`: sequence state (*0x00410B08) +0x18 = +0x14 = 0.
8. `func_0015E7B0`: per player (`game[0x25]` players) `A(0x74, 0x10) → 0x00410AA0[n]`; `func_0015E818` resets them (HUD/lock state: bytes 0xFF at +0x6C..0x6F, 16 halfwords 0xFFFF at +0x44..0x62, the rest 0).
9. `func_0016DA60`, `func_00155AE0`: empty.
10. `func_0016DA78`: per player `A(6, 4) → 0x00410AF0[n]`.
11. `func_0016DE20` (replay): `A(0x20, 0x10) → *0x00410AF8`; `func_0016FEE8`: `A(0x8A50, 0x10) → *0x00410AFC` (replay copy of the projectile pools); `func_00170460`: a second effect-pool block: the effect pointer is swapped so that `func_00154638` allocates `A(0x625A0, 0x10)` into *0x00410B00, then `A(0xA0, 4) → *0x00410B04`, and the pointers are swapped back (*0x004106B4 = gameplay pools, *0x00410B00 = replay pools).
12. Mission kind 1 (trial): `func_0017BB38`.
12. (trial) `func_0017BB38` only sets two pointers: *0x00410B38 = 0x002A0D34, *0x00410B3C = 0x002B7C38 (no allocation).
13. `func_0017D8B0`: `A(0x60, 0x10) → *0x00410B40`, `func_0017CBE0` (init), `func_0017CCA8(that, block 1)` (the 3 GIM sets of block 1, TBP 0x2500 / CBP 0x3F3C, `../formats/MISSION.md`); `func_0017DC98`: `A(0x10, 4) → *0x00410B94`, `func_0017DCC0` (pause-menu reset); `func_0017B2C8`: `A(0x90, 0x10) → *0x00410B28`; `func_0017B708`: `A(0x90, 0x10) → *0x00410B2C`; mission id 1 → `func_0017B868`: `A(0xD0, 0x10) → *0x00410B30`; id 8 → `func_0017B978`: `A(0xB0, 0x10) → *0x00410B34`.
14. S+0x1664 = `WorkHeapAlloc(0, 0)` (the heap mark for retries).
15. `func_0010AD80` (frame draw lists: S+0x1651 = 0, 16 buckets at S+0x1540, VIF1 list buckets, loading-screen draw `func_00239310` or the variant `func_00239B60` with the alpha S+0x1690 and counter S+0x166C, which is then incremented). → sub 8.

### Sub 8: `StartFlightSystems` 0x0010AB58 (also on every retry)

1. `func_00101090(S+0x1664)`: rewinds the work heap to the mark of sub 7 (used bytes *0x0041178C = mark − base *0x00411788). Everything allocated from here on is allocated again, identically, at each retry.
2. S+0x1660 (mission timer) = 0; sound: `func_00219260` (stop event SE voices), `func_0021AEE0` (two positional-list heads 0x00351F60 and 0x00351F60 − 0x110 cleared), `StopAllVoices`, `func_0021FDF8(1)`, `func_0021FDF8(2)`.
3. `func_0015E818` (HUD/lock records reset, as in sub 7), `func_0011BA90` (units reset, as in sub 7).
4. `InitEffects` 0x00154660(block 30): `func_00184C20`; 45 pool resets on the effect block *0x004106B4 at the offsets (in call order) 0, 0x2010, 0x30700, 0x31710, 0x38320, 0x22220, 0x22C30, 0x22E40, 0x24650, 0x247E0, 0x268E0, 0x267F0, 0x274F0, 0x28400, 0x2B430, 0x2B6C0, 0x2DCD0, 0x2E2E0, 0x2E4F0, 0x38B30, 0x61940, 0x3FF90, 0x5FEE0, 0x61F00, 0x5E490, 0x62410, 0x5D880, 0x61BE0, 0x5E8A0, 0x5F2C0, 0x61420, 0x4A830, 0x604F0, 0x447D0, 0x61AD0, 0x39F40, 0x5EAB0, 0x5D070, 0x60D00, 0x5B050, 0x5FAD0, 0x61010, 0x47800, 0x61630, 0x61DF0 (one thunk per pool to the wrappers 0x00155BC8…0x00157310; table below); `func_001BDAD8`; block bytes/words +0x62451 = 1, +0x62440/44/48 = 0, +0x62494 = 65536.0f, +0x62498 = 1.0f; **`vrinit` with the word at 0x0040E7F0** (seeds the VU0 R register, one of the five random sources, `../game/random.md`).
5. `ResetProjectiles`; `func_0017B2F0` (record *0x00410B28: byte +0x80 = 0, 4 × {+0x18 = 0, +0x14 = 30} at stride 0x20); `func_0017B730` (*0x00410B2C +0x80/+0x84 = 0); mission id 1 → `func_0017B890` (*0x00410B30 +0xC8/+0xCC = 0); id 8 → `func_0017B9A0` (*0x00410B34 +0xA0/+0xA4 = 0).
6. `func_00118A18` (mission rules records): `A(0x70, 0x10) → gp 0x004104F0`, `A(0x90, 0x10) → gp 0x004104F4`, `A(0xA8, 0x10) → gp 0x004104F8`; 0x4104F0 record: +0x28 = 0xFA, +0x10 = 0, +0x14 = 0x1A5E0, +0x18 = 9999, +0x2A = 2, +0x20 = 262144.0f (196608.0f in difficulty mode `game[0x29] == 3` for mission ids 3, 6, 9, 14, 17), +0x1C = −262144.0f, u16 +0x24 = 100, +0x26 = 200, +0x2B = 1, +0x34 = 0xFF, +0x30 = 131072.0f, +0x2C = 0, +0x35 = 0xFF, +0x60 = 60, +0x44 = 9999, +0x48 = 0xE10, +0x4C = 0x1C20, +0x50 = 0x2A30, +0x58 = 600, +0x5C = 0x78, +0x38/+0x3C/+0x40 = 9999, +0x54 = 600; 0x4104F4 record: two qwords stored from vf0 (each = (0, 0, 0, 1.0), so the fourth word of each is 0x3F800000, not zero), bytes and words cleared field by field (+0x20..+0x7F), +0x80 = −1, u16 +0x84 = +0x86 = 0.
7. `func_0010FA70` (mission setup; next section), `func_00135D60`: per unit (count = first word of *0x00410640) `A(0x48, 4) → 0x002A2660[n]`, per player `A(0x50, 4) → 0x00410678[n]`; `func_0010C840` (empty); `func_00136570(0)`, `func_00136570(1)`; `func_0016DAE0` (per player record 0x00410AF0[n]: u16 0, u16 0, bytes 0xFF, 0xFF); `func_00111950` (sky record gp 0x004104D0: +8 = 8, +0xC = 0, +4 = block24 word +0x48 & 0xFFFFFF, +0x10 = 0, +0x20..+0x2F = 0 (VU0 zero), `func_001117F8(game[0x27])`, `func_00113750(block 24)`).

8. In `InitEffects`: `func_00184C20(block 30)` resolves the 16 sub-tables of block 30 (offsets at +4…+0x40, relative to the block) into 0x002B8530…0x002B856C (order of the stores: +4, +8, +0xC, +0x10, +0x40, +0x14 … +0x3C); `func_001BDAD8`: **384 random-table reads**: 128 × (x, y, z) = 1 − 2·`NextRandomFloat()` (EE `add.s` then `sub.s`), w = 1.0, into 0x002B91C0 (16 bytes each; the z read is the third); mission id 0x12 → `func_001D6518` (pool 26 reset again with `func_001568F0`, then `func_001D8998` takes its single record: scans the one mask word at +0x20 for the lowest clear bit among the first 1, sets it, counter +0x24 += 1, returns the index or −1; on success `func_001D4200(pool)` (+0x10 = 0)); `func_0019FF90` (0x004117E8 = 2, 0x004117EC = 1, 1,024 words at 0x00420850 = `rand()` >> 15, arithmetic shift); mission id 8 → 128 × (sin, cos) of i · *0x0040F970 · 30 into 0x002B8C3C / 0x002B8E3C (newlib `sinf`/`cosf`).
9. Sub 8 continued (after `func_00111950`):
   - `func_00257630(game[0x25])` (pad/vibration state): words 0x00411880/84 and 0x00411888/8C cleared; 0x00411878 = main-context pad state, 0x0041187C = pad state + 0x50 for 2 players, else 0; `func_00257468(0, n)` and `func_00257468(1, n)` for n = 0, 1.
   - mission id 8 → `func_002594F0`: **3 `rand()` calls**: 0x004BE0B0 = rand() % 180 + 900, 0x004BE0B4 = rand() % 16 + 16 (C remainder, sign kept), 0x004BE0B8 = rand() % 120 + 180, 0x004BE0BC = 0.
   - mission kind 1 → `func_0017BC70` (trial table: 0x002B7C2C = *0x00410B3C, 0x002B7C20/24 = 0; ids 0x15–0x18 → limit 0x80 and table *0x00410B38 + 0x200·(id − 0x15), 0x19 → 0x20 and +0x800, else 0; 128 × {0xFFFF, 0xFFFF} at *0x00410B3C); kind 3 → `func_0017BF90` (0x004117E4 = 0).
   - `func_00250C30(block 34, block 35)` (mission message/radio system): 0xA80 words at 0x004BA570 = −1 (written backwards from 0x004BCF6C); header words of block 34 → 0x004BA300…0x004BA32F (section offsets made absolute, counts as u16 halves); the pointer table 0x004BA570 is filled per section (records of 8, 12 and 8 bytes; −1 stays −1, else base 0x004BA31C + offset), then a range of 16-byte entries and, when all three counts are non-zero, a block of 40 entries (window by the u16 at 0x004BA32A) and `func_00254E48`; `func_002509F8(base, block 35)`, `func_002556A0`, `func_00253038`, `func_00254FB0` on the 20 records at 0x004BA330 and the 16 at 0x004BA470 (16 bytes each), `func_002508E0`, 0x00411870 = 0x0041186C = 0, then a debug `printf(" %x %x %x %x
", block35[0..3])` (no visible output in the port; the call is kept).
   - `func_00219110`, `func_0021B030` (positional sound tables: 2 × 0x1020-byte records at 0x0042AA10: u16 0xFFFF, 0; 4 voice slots {−1, 1, 0, 0, 0}; 5 slots {−1, 1, 0} at +0x5F0 stride 0x130; 4 slots at +0xBE0 stride 0x110; then `func_0021A8D8(0x10, 5, 600)`).
   - `func_0016DE48` (replay buffer): record *0x00410AF8: +0x1C = **((free heap `func_00101080()` / 0x40000, rounded toward zero) − 1) × 0x40000**; demo playback (`game[0x29] == 4`): +4 = demo block S+0x17D4, +0x1C = S+0x17D8 − S+0x17D4 (no allocation); mission kind 4: `A(0x400, 0x10) → +4`, +0x1C = 0x400; otherwise `A(+0x1C, 0x10) → +4` — **the replay takes all the remaining work heap in 256 KiB units**, which is why the heap is 98.4 % full; its size depends on every allocation before it.
   - `func_0016DEF8` (replay reset): +0 = 0, +0x10 = +0xC = +8 = +4 (write pointers), +0x14 = +0x18 = 0; unless demo playback the whole buffer is zeroed with 128-bit stores (size / 16 qwords).
   - `func_0010AD80` (loading-screen draw, as at the end of sub 7).
   - next sub: kind 4 → 0x13; demo (`game[0x29] == 4`) → S+0x1668 &= ~0x10000, 0x10; mission id 10 and `game[0x29] != 3` → `game[0x2C] = 0`, 0xD; else 0xA.

#### `func_0010FA70` (mission setup, in sub 8)
`func_0021F190(0, n, 0xFF, 0xFF, 1)` for VAG slots n = 0…5, `func_0021E798(0, 0)`, `func_0021F190(0, 0, 0, 0, 1)` (stream volumes); `SetupMissionUnits(0)` 0x0010E758 (block 22, `../formats/MISSION.md`); `func_0024CBE0`; block 28 (S+0x1704): when its first word is 2 (container of 2), `func_0011A3D8(gp 0x004104F8, block28 + word1)` and `func_0015E8C0(block28 + word2)`; otherwise `func_0011A3D8(gp 0x004104F8, block28)`.
- `func_001117F8(mission id)` (in `func_00111950`): sky flags at block24 +4: ids 2, 0x11, 0x19 → |= 0x400; 0x12, 0x28 → |= 0x100; 0x14 → |= 0x500; 4 → |= 0x800, sky record +0x1C = *0x0040DC24, u16 +0x18 = 2; 8 → |= 0xA00, *0x0040DC28, 2; 0xB → |= 0xC00, *0x0040DC2C, 3; 0xC → |= 0x900, *0x0040DC30, 2.

##### `SetupMissionUnits` 0x0010E758 (block 22)
- Block 22 as a container of 3 is chosen by difficulty: `game[0x28]` < 2 → item 1, < 3 → item 2, else item 3. `func_0010E3F8(block)` → gp 0x004104CC: the header (word 0 → unit table header, unit count = s16 at +4) and 18 typed sub-table pointers (indexes 1–0x13).
- For each unit i (0x18-byte record r, unit U = 0x002A1F70[i]): U+0x4C = U+0x50 = 0, u16 U+0x56 = U+0x5C = 0, U+0x48 = 0x10000000, bytes U+0x63 = U+0x62 = 0xFF; `func_0010E5C0(U, r)` (position, angles from the record); U+0x40 = the record of type r[0] in sub-table r[0] + 1 at index r[1] (sizes per type: 1 0x24, 2 0x14, 3 0x20, 4 0x24, 5 0x20, 6 0x28, 7 0x18, 8 0x20, 9 0x18, 10 0x14; other types 0).
  - Without a type record and U+0x55 < 2 (players): model id `game[i·0x10]`, `func_0011E878(id)` → u16 U+0x5E, U+0x59 = 0xFF, U+0x70 = 50.0f, u16 U+0x60 = 100, U+0x64 = 1, U+0x58 = U+0x5A = 0, U+0x5B = 1, U+0x65 = 0.
  - With a type record t: U+0x58 = t[1], U+0x5A = 0, U+0x59 = t[2] (team); t[1] ≠ −1 → U+0x5B = 1, flag 0x20000000; team not in {−2, −1} → U+0x5B = 2; U+0x9A = t[3]; flags |= t[10] << 16; U+0x70 = (float)s16 t[8]; U+0x69 = t[0] (model id); U+0x64 = t[0x10] (bit 0 → flag 0x1000000, else bit 1 → 0x2000000); U+0x65 = t[0x11]; u16 U+0x60 = t[4], U+0x5E = t[6].
  - Flags by model id: 0x48, 0x45, 0x34, 0x3C–0x40, 0x47, 0x41 → 0x10; 0x39, 0x8A → 0x80; 0xB1 → 0x40; mission 0x10 with id 0x39: appended to the unit list +0x80 (count +0x83); mission 0xB with id 0x41: count +0x83 + 1; ids 0xB8–0xBA and 0x4A → 0x800000. Unless flags & 0x8000700: U+0x4C |= 0x10000000 (0x30000000 with 0x20000000); with 0x8000000 and not 0x100 at difficulty ≥ 2 the same when bit 0 of 0x002A0318 is set.
  - Record type 0 (player slots): i < players → U+0x4C |= 0x30000000, flags |= 0x1330000, U+0x54 = 0; else U+0x4C &= 0xCFFFFFFF; velocity: rotation matrix from the angles (U+0x10, `func_00102D28`) applied to (0, 0, *0x0040DBC4, 0) → U+0x20 (`sceVu0ApplyMatrix`); player defaults as above with u16 U+0x56 |= 0x100; `func_0011E8D8(i)`, `func_0011EFA0(i, game[i·0x10 + 3])` (special weapon).
  - Type 1 (aircraft): U+0x54 = 1, U+0x56 |= 0x100, `CreateAIAircraftState(i)`; the AI state (U+0x44) gets its behaviour record (sub-table 0xE, 0x34 bytes, index t[0x1D]), path/target references by t[0x1E]/t[0x20] (0 none, 1 sub-table 0xF × 0x14, 2 sub-table 0x10 × 16, 5 sub-table 0x13 × 32); velocity = rotation × (0, 0, behaviour speed (+0xC) × *0x0040DBC8, 0).
  - Other types (ground/naval): U+0x54 = 2, `CreateGroundUnitState(i)`; the state's path kind t[0x12] → +1 and reference +0x80 (1: sub-table 0xF × 0x14, 2: 0x10 × 16, 3: 0x11 × 16, 4: 0x12 × 12 with **one `NextRandomFloat`**: state +0xC = s16 ref+8 + (int)((float)s16 ref+0xA × (2r − 1)), 5: 0x13 × 32).
- Unit count → word 0 of *0x00410640; `func_001459E0(*0x004106A0)` (unit model sets, block 21); per player `func_001279B0(p, 0)`; `func_00216550(0, game[0], 0xFFFF)` and for 2 players `func_00216550(1, game[0x10], 0xFFFF)`; `func_002165D0(players)` (engine sound); per player `func_001279B0(p, 1)`, aircraft model flags |= 2, U+0x74 = `func_00146710(*0x004106A0, p)`; `func_00149E90(*0x004106A4, 0)`.
- Units i ≥ 2: U+0x74 = U+0x70 (no group) or `func_00146710` (and U+0x70 = it when ≤ 0); U+0x78 = 0 or `func_00146788`; when U+4 (y) is 0 and i ≠ list byte +0x88: y = terrain height `func_0013E338(map, U)` − U+0x78.
- Per player `A(units·0xC, 4) → 0x00410648[p]`, each entry {0, 262144.0f, −1.0f}.
- U+0x9B (radar class) and U+0x99 (type index) by model id (tables in the code; unchanged for flag 0x400).
- Last pass, **random-table reads**: for each aircraft unit (U+0x54 = 1), in unit order: flags & 3 == 0 → state +0xF5 = (int)(r · *0x0040DBCC) (and +0xF6 = 0 when that is 1); else state +0xF5 = (int)(r · *0x0040DBD0).

##### Other mission-setup helpers
- `func_0024CBE0` (unit summary for the radar/map, 0x0042D2C0): pointers 0x0042D2C8 = 0x0042D300, 0x00411578 = 0x0042D2C0, 0x0042D2C4 = 0x0043A300; u16 count +0xE = unit count, +0xC = 0xFFFF, +0x18 = 0; `A(8, 4) → 0x00411584` (copy of header word 1, twice); per unit a 0xD0-byte record: model id, +0x6A, flags & 3, +0x65, u16 +0x60, flags 0x8000000 → +4 = 1, position / 8 as s16 (x, y, z, truncated); +0 = 36000; mode +0xF: 2 players → 2, trial (ids 0x15–0x19) → 3, `game[0x29] == 2` → 1, else 0; +0x19 from 0x00411570[id − 0x15] (trial) or 0x003B01C0[id ≤ 0x12 ? id : 0].
- `func_0011A3D8(gp 0x004104F8, block 28 part)` (event script tables): 0x13 section offsets made absolute into the record (words 0–0x11); three sorted lists (4-byte entries, first byte = level, 0xFF end) indexed by level 0–7 into words 0x12–0x19, 0x1A–0x21, 0x22–0x29; three unit link lists (u16 unit, u16 offset, 0xFFFF end) set U+0x8C/0x90/0x94 for units with flag 0x10000000.
- `func_0015E8C0(block 28 part 2)`: the 16 halfwords copied into each player's HUD record (+0x44).
- Message system: `func_002509F8(base, block 35)`: 0x200 words at 0x004BD76C = −1 (backwards), `SelectFont(block35 + w1, block35 + w4)`, string pointers per message (u16 strings ending ≥ 0xFFFE) into the base records +0xC, 0x4D string-table entries at 0x004BCF70, state words 0x004BD770…0x004BD790 (−1, −1, 0, 0, 3, header pointer), `func_00259280(block35 + w5)`, 0x00411868 = block35 + block35[w0] when the first word of the message header is 0x13 else 0, `func_002583C0`. `func_002556A0`: radio state bytes 0x004BD9B0–B2 = 0, two channel records {1, 15, 23, −1, 0, 0, −1, 8}, 16 records at 0x004BDA00 reset by `func_00254FB0` (u16 0xFFFF, bytes 8, 0x1B, three words 0), 0x400 bytes at 0x004BDB00..0x004BDEFF = 0, 4 words 0x004BDF00.. = 0, 48 words 0x004BDF10..0x004BDFCC = −1. `func_00253038`: bytes 0x00411871–76 = 0. `func_002508E0`: 0x00411860 = 0, u16 0x00411864 = 0, bytes 0x00411865–67 = 0. `func_00254E48(p)`: 0x004BD9A0[0..2] = p, p + 0x10, p + 0x20.
- `func_00257468(a, b)`: `func_00257430(0x004BDFD0 + (a % 2)·0x38 + (b % 2)·0x1C)` (vibration slot reset).
- Heap helpers: `func_00101080()` = heap size *0x00411784 − used *0x0041178C (free bytes).
- `func_0010D6A0(v)` (view record reset): word +0 = 0, bytes +4…+7, +8, +0xA = 0, word +0xC = 0, qwords +0x10…+0x8F and +0xA0…+0xAF = 0 (`sq` of the zero register; +0x90…+0x9F is left as it is), `Init2dCamera(v + 0xB0)`, `Init2dCamera(v + 0x270)`, words +0x430…+0x43C = 0.
- `func_0010A130(p, target, time)` (target in f12, time in f13): S+0x1680+4p = target, S+0x1678+4p = time; returns f0 = S+0x1670+4p (current fade level of player p).
- `func_00133A80` (AI fire slots): for n = 0, 1: word *0x00410674 + 4n = 0, byte *0x00410674 + 8 + n = 0xFF. `func_001279A0`: engine-sound flags 0x00410658/0x0041065C = 0.
- `func_00257430(slot)` (vibration slot): +0 = 12, +4 = −1, u16 +0x10 = 1, +0x18 = 0, +8 = 0, u16 +0xC, +0xE, +0x12 = 0, +0x14 = 0. `func_002583C0` (tutorial control hints): 0x004BE040, 0x004BE044 = 0, 13 words 0x004BE048…0x004BE078 = 0.
- `func_00109658(out, a, b, c)` (VU0 triangle normal): e1 = a − b, e2 = c − b (xyzw); n.xyz = e2 × e1 (`vopmula`/`vopmsub`), n.w = e1.w; Q = sqrt(n.x² + n.y² + n.z²) (`vaddy.x`, `vaddz.x`), Q = 1 / Q (`vdiv` of vf0.w); out = n · Q (all four fields, so out.w = (a.w − b.w) / |n|).
- `func_00149C40(models, n, track, v)` (v in f12): when models[0][n] (instance array of model n) ≠ 0 and the byte count (models+0x1C)[n] > 0: `func_00140F68(instances + k · 0x4C, track, v)` (animation track time) for k < count. `func_00149EB8(track, n, v)` = `func_00149C40(*0x004106A4 (aircraft models), n, track, v)`. `func_00149E28(models, i, v)`: models+0x78+4i = v.
- `func_0017CBE0(r)` (replay/pause HUD record): +0 = *0x0040EC48, +0x10 = *0x0040EC4C, +0x20 = *0x0040EC50, +0x30 = 1.0f; words +4, +8, +0xC, +0x14, +0x18, +0x1C, +0x24, +0x28, +0x2C, +0x34, +0x38, +0x3C, +0x40…+0x4C = 0; bytes +0x50 = +0x51 = +0x52 = 0xFF, +0x53 = +0x54 = +0x55 = 0, +0x58 = +0x59 = 0; u16 +0x5A = 0, +0x5C = 0xFF19, +0x5E = 0xB7.
- `func_0017CCA8(hud, data)` (replay/pause HUD textures, mission block 1): only when data ≠ 0 and it is a container of 3. For (item, count byte, pointer) = (1, +0x53, +0x40), (2, +0x54, +0x44), (0, +0x55, +0x48) in that order: sub-container c of the item; count byte = low byte of c's count; `A(count · 0x28, 0x10)` → pointer (allocated even for count 0); per texture k: `InitGimTexture(pointer + k · 0x28, item k of c)`, `SetTextureVramAddress(same, 0x2500, 0x3F3C)`.
- `func_0017DCC0` (pause-menu reset, from `FlightPlayFrame` when paused): when S+0x1670 ≠ 0: `func_0010A130(0, 0.0, 0.5)`; when S+0x1674 ≠ 0: `func_0010A130(1, 0.0, 0.5)`; menu state *0x00410B94: bytes +4, +5, +6 = 0, words +0xC, +8 = 0. Item list by kind k = s8 S+0x1653, replay frames R = word *0x00410AF8 + 0x14, bit b = word 0x002A0318 & 1 (the list's item count word +4 is overwritten where shown):
  - k = 0: R ≥ 0xB5 → list 0x002B8370, count 6 when b else 5; R < 0xB5 → list 0x002B8390, count 5 when b else 4.
  - k = 1: R ≥ 0xB5 → list 0x002B83D0, count 5 when b else 4; R < 0xB5 → list 0x002B83F0, count 4 when b else 3.
  - k = 2 → 0x002B8410, k = 3 → 0x002B83B0, any other k → 0x002B8370 (counts unchanged).
  - then `func_0017E7D0(list)`.
- `func_0017E7D0(list)` (pause-menu layout): x = 0x8E0 (0xAA0 with main-context flag 0x10000, 16:9); y = 0x140 (0x300 when list[0] bit 0); for each item i < list[1] (item = list[2 + i]): +0x1C = +0x24 = x, +0x20 = y + 0xC00, +0x28 = y + 0xC00 + 0x180·i, +0x18 = 1 when i == s8 (*0x00410B94)+5 (selected); then `func_0017E8B0(item)`.
- `func_0017E8B0(item)` (menu animation targets; menu state m = *0x00410B94, e = item[0]): byte m+4 == 0 → e+4 = item+0x1C, e+8 = item+0x20, e+0x10 = item+0x24, e+0x14 = item+0x28, e+0x24 = (e+0x14 − e+8) / 6 (signed), e+0x18 = e+0xC = (item+0x18 == 0 ? 8 : 0). byte m+4 == 1 → x = e+0x10 + (16:9 ? e+0x20 : e+0x1C) + 0x40; for each sub-item j < item[5] (sub = item[1 + j]): +4 = +0x10 = x, +8 = e+0x14, +0x14 = e+0x14 + 0x180·j, +0x24 = (sub+0x14 − sub+8) / 6, +0x18 = +0xC = (j == s8 m+6 ? 0 : 8).
- `func_00259280(t)` (subtitle timeline bind): the 32 bytes of t → 0x004BE080…0x004BE09F; for i < u16 t+2: word 0x004BE08C + 4i = t + word t+0xC+4i; then 0x004BE0A0 = 0, 0x004BE0AC = word 0x004BE08C, u16 0x004BE0A4 = u16 0x004BE0A6 = 0.
- `func_00135C50`: nearest-distance trackers gp 0x004117C8 and 0x004117CC = 524288.0f (0x49000000).
- `func_00218B28(n)` (sound banks, keep the first n): when count ≥ n + 1, the SPU allocation pointer 0x00411800 = end of bank n − 1 (`func_00218CB0`) or the base *0x00410D04 for n = 0, entries n…count−1 get SPU address 0 and id 0xFFFF, count = n, returns 0; else returns −1 and changes nothing. `func_00218ED0` = *0x00410D04.

##### Callees of `SetupMissionUnits` (allocation-relevant)
- `func_0010E3F8(block)`: `A(0x68, 4)` (the first allocation of `SetupMissionUnits`): word 0 = block, words 1–0x19 = block + 4 · s16 at block + 6, +0xA, … +0x66 (25 sub-table offsets in words).
- `func_0010E5C0(U, r)`: U+0 = r+4, U+4 = r+8, U+8 = r+0xC (position), U+0xC = 1.0f; angles U+0x10/0x14/0x18 = (float)s16 r+0x10/0x12/0x14 × *0x0040DBC0 / 180.0f (multiply, then divide), U+0x1C = 0; flags |= r[2] & 3, r[2] bit 7 → 0x80000000, bit 6 → 0x8000000, bit 5 → 0x4000000, |= (r[3] & 3) << 12; r[3] == 0x80: flag 0x400 and, if list byte +0x88 is −1, it becomes the unit index (U+0x55); else r[3] bit 2 → 0x100, bit 3 → 0x200, U+0x88 = (r[3] & 0x70) >> 4; U+0x6A = r[0].
- `CreateAIAircraftState` 0x0012A6B8: `A(0x110, 0x10)` → U+0x44 (also 0x004117B0); the fields set in the code (bytes 0, +0x19 = 0x80, floats 1.0 at +0x80/84/88/9C/BC, +0xA4 = 524288.0f, +0xA8 = −1.0f, +0xC0 = 262144.0f, +0xC4 = *0x0040E020, +0xC8 = −1800.0f, +0xCC = 0xFF, the rest 0).
- `CreateGroundUnitState` 0x0012FC08: `A(0xD0, 0x10)` → U+0x44 (also 0x004117C0); zero qwords at +0x10, +0x20, +0x60, +0x70, +0x90–0xBF, +0x41 = 0x80, +0x47 = 0xFF, the rest of the listed fields 0.
- `func_0011E8D8(i)` (player state): `A(0x4C0, 0x10)` → U+0x44; +0x110 = `func_0011B8B8(model id)`; zero qwords at +0, +0x10, +0x20, +0x30, +0x40, +0x50, +0x70, +0x80, +0x90, +0xA0; +0x148 = i, +0x151 = 0xFF, +0x152 = 1; +0x154..0x157 = 0xFF; +0x1B4 = 8192.0f, +0x1B8 = +0x1BC = *0x0040DD60, +0x188 = 0x30000; 32 floats 0.5 at +0x1C0..+0x23C and +0x240/+0x250/+0x254/+0x258 = 0.5; +0x290 = 524288.0f; 8 words −1 at +0x2B0..; two {−1, −1, 0, −1, byte 0} sets at +0x2B4/+0x2BC/+0x2C4/+0x2CC; `func_001279A0`; +0x2D4 = 0 (full list in the code).
- `func_0011EFA0(i, special)`: six position history vectors +0xB0…+0x100 = the unit position with w = 2.0f (`sceVu0CopyVector`); with U+0x4C bit 0x10000000 `UpdateAircraftMovingParts(i, 1)`; `func_0011EC48(state, special)`; 0x00410650[i] = S+0x176C + i·0x40 (aircraft data); players → `func_00149E28(aircraft models, i, data+0xD0)`.
- `func_0011EC48(state, slot)` (special weapon slot select): w = state+0x110 (aircraft weapon record); +0x172 = slot; +0x15A = byte w+2; u16 +0x158 = u16 w+0; slot < 3 (signed): entry = w + 4 + 4·slot (slot sign-extended from a byte): +0x173 = entry[0] (weapon type), +0x175 = entry[1], +0x15B = entry[2]; type 0–7 → +0x174 = 0, flags +0x14C |= 0x80; type 8 → +0x174 = 1, +0x14C &= ~0x80; types 9–14 → +0x174 = 0, +0x14C &= ~0x80; other types change nothing more; slot ≥ 3 → +0x15B = 0, +0x173 = 0xF, +0x175 = 0.
- `func_001457E0(_, desc, data)` (model-set descriptor, 0x14 bytes: [0] model objects, [1] extra item list, [2] header item, [3] header word 1, [4] model count): data == 0 → all five words 0. Else a container reader over data (count n): [4] = n; header h = item 0; v = h[0], but v = 0x96 when n == 5; [3] = h[1].
  - v < 0xA0 (unsigned): [2] = item 0; `A(n · 0x28 − 1, 0x10)` → [0] (the size really is one byte short); `BindModelData([0] + (k − 1) · 0x28, item k)` for k = 1 … n − 1; [4] = n − 1.
  - v ≥ 0xA0: [2] = item 2; `A(0xA0, 0x10)` → [0]; `BindModelData` of items 3, 4, 5, 6 into the 4 slots; item 1 is a container of m entries: m < 1 → [1] = 0, else `A(m · 4, 0x10)` → [1] and the m item pointers are stored there; [4] = 4.
- `func_00149FB8(M)` (shared models, *0x004106A8; blocks S+0x16A0 / S+0x16A4): M[0x12] = 0; container C = S+0x16A0 with n items: M[0xE] = n, `A(4n, 0x10)` → M[1] (instance pairs), `A(4n, 0x10)` → M[0] (model pairs). Per item i: sub-container c (k items), v = word 0 of item 0 (0x96 when k == 2); `A(0x98, 0x10)` → M[1][i], `A(0x50, 0x10)` → M[0][i]; for j = 0, 1: `BindModelData(M[0][i] + 0x28·j, item (v < 0xA0 ? j : j + 2) of c)`, `CreateModelInstance(M[1][i] + 0x4C·j, M[0][i] + 0x28·j)`. Then `InitGimTexture(M + 0x10, S+0x16A4)`; u16 at word M[5] = 0x1F00 and, when word M[6] ≠ 0, u16 at M[6] = 0x3F0C (the texture's TBP/CBP fields, HYPOTHESIS); `A(8, 0x10)` → M[3], M[2], M[0xF], M[0x10], M[0x11] in that order. Per aircraft slot s = 0, 1: `A(8, 0x10)` → M[0x10][s] (2 words cleared), `A(0x18, 0x10)` → M[0x11][s] (6 words cleared); when word S+0x1754+0x40·s is 0: M[3][s] = M[2][s] = M[0xF][s] = 0; else a reader over that aircraft container (count m): small = (word 0 of item 0 < 0xB4); M[0xF][s] = m − 6 (small) or m − 7; when > 0: `A(4·cnt, 0x10)` → M[3][s], `A(4·cnt, 0x10)` → M[2][s], and per t: `A(0x98, 0x10)` → M[3][s][t], `A(0x50, 0x10)` → M[2][s][t], sub-container d = item t + 6 (small) or t + 7, `GetContainerItem(d, 0)` (result unused), for j = 0, 1: `BindModelData(M[2][s][t] + 0x28·j, item j + 2 of d)`, `CreateModelInstance(M[3][s][t] + 0x4C·j, M[2][s][t] + 0x28·j)`. Finally M[0x12] |= 2.
- `func_0011B920(id, slot)` → special weapon id: byte 0x002A1CDC + 16 · id + 4 · slot (`.data`), with id > 0x15 treated as 0 (signed 64-bit compare).
- `func_0011E878(model)`: s8 table 0x002A2388[model] (×70/100 at difficulty 4, ×30/100 at 5).
- `func_001459E0(*0x004106A0)` (unit model sets, block 21): `A(0x28, 4)`: two 0x14-byte set descriptors for the player aircraft (`func_001457E0` from S+0x175C + p·0x40); block 21 container of n: holder[3] = n, `A(n·4, 0x10) → holder[2]`, then **two more `A(n·4, 0x10)` whose results are not stored** (the heap still advances), `A(n·0x14, 4)` (one descriptor per model set, `func_001457E0`); `A(units·0x18, 0x10) → holder[0]`; per unit: LOD count = U+0x5B, `A(lods·0x20, 0x10)`, per LOD (group byte U+0x58 + k; 0xFF = none): descriptor (players: their own; others: set n), `A(instances·0x4C, 0x10)` and `CreateModelInstance` per model; holder word 0x2009 = −1.
- `func_001279B0(p, on)` (engine sound activation): per-player flag 0x00410658[p]; on a change: 2 players → `func_00216790(1/2/3)` by which are on, else `func_00216790(0)`; first activation → `func_002166D0`, last deactivation → `func_00216728`.
- `func_00146710(units, i)`: players: aircraft model +0x40 (10.0f when absent); others: LOD of the group (U+0x5A) +0x40. `func_00146788`: the LOD's +0x44. `func_00149E90(models, v)`: models+0x40 bit 0 = (v == 0).


##### Effect-pool resets (`InitEffects`, 45 wrappers)

Each wrapper gets the pool base P (effect block *0x004106B4 + offset). It runs the per-record init on P, P + stride, … (count records, ascending), then clears the occupancy mask: N words directly after the records (at P + count·stride, written from the highest word down), then sets the live counter word after the mask to 0. The mask has one bit per record (N = ⌈count / 32⌉). The table is in call order. The 0x2010 pool has no per-record init: its constructor is empty, so only an empty counted loop is left. Its record size (0x20) and count are inferred from the 0x20000-byte record area and the 128 mask words. `func_001DB0A8` serves two pools.

| # | Pool offset | Wrapper | Record init | Stride | Count | Mask words | Counter |
|---|---|---|---|---|---|---|---|
| 1 | +0 | 0x00155BC8 | `func_001DB0A8` | 0x100 | 32 | 1 | +0x2004 |
| 2 | +0x2010 | 0x00155C50 | none (empty 4,096-iteration loop) | (0x20) | (4,096) | 128 | +0x20200 |
| 3 | +0x30700 | 0x00155CB8 | `func_001F1300` | 0x40 | 64 | 2 | +0x1008 |
| 4 | +0x31710 | 0x00155D40 | `func_001F38F0` | 0x1B0 | 64 | 2 | +0x6C08 |
| 5 | +0x38320 | 0x00155DC8 | `func_001F6978` | 0x40 | 32 | 1 | +0x804 |
| 6 | +0x22220 | 0x00155E50 | `func_001FC818` | 0x50 | 32 | 1 | +0xA04 |
| 7 | +0x22C30 | 0x00155ED8 | `func_001E4588` | 0x40 | 8 | 1 | +0x204 |
| 8 | +0x22E40 | 0x00155F60 | `func_001E7FF0` | 0x60 | 64 | 2 | +0x1808 |
| 9 | +0x24650 | 0x00155FE8 | `func_001FF7E8` | 0x30 | 8 | 1 | +0x184 |
| 10 | +0x247E0 | 0x00156070 | `func_001DB0A8` | 0x100 | 32 | 1 | +0x2004 |
| 11 | +0x268E0 | 0x001560F8 | `func_00203480` | 0x30 | 64 | 2 | +0xC08 |
| 12 | +0x267F0 | 0x00156180 | `func_00206588` | 0xE0 | 1 | 1 | +0xE4 |
| 13 | +0x274F0 | 0x00156208 | `func_0020A4A0` | 0xF0 | 16 | 1 | +0xF04 |
| 14 | +0x28400 | 0x00156290 | `func_0020C378` | 0x30 | 256 | 8 | +0x3020 |
| 15 | +0x2B430 | 0x00156318 | `func_001E97F0` | 0x50 | 8 | 1 | +0x284 |
| 16 | +0x2B6C0 | 0x001563A0 | `func_001EB708` | 0x130 | 32 | 1 | +0x2604 |
| 17 | +0x2DCD0 | 0x00156428 | `func_001EC458` | 0x30 | 32 | 1 | +0x604 |
| 18 | +0x2E2E0 | 0x001564B0 | `func_001ED3C8` | 0x40 | 8 | 1 | +0x204 |
| 19 | +0x2E4F0 | 0x00156538 | `func_001DDBD0` | 0x110 | 32 | 1 | +0x2204 |
| 20 | +0x38B30 | 0x001565C0 | `func_001DF9A0` | 0x50 | 64 | 2 | +0x1408 |
| 21 | +0x61940 | 0x00156648 | `func_001CD400` | 0x30 | 8 | 1 | +0x184 |
| 22 | +0x3FF90 | 0x001566D0 | `func_0019D9C0` | 0x30 | 384 | 12 | +0x4830 |
| 23 | +0x5FEE0 | 0x00156758 | `func_0019C888` | 0x30 | 32 | 1 | +0x604 |
| 24 | +0x61F00 | 0x001567E0 | `func_001CF398` | 0x500 | 1 | 1 | +0x504 |
| 25 | +0x5E490 | 0x00156868 | `func_001D0B40` | 0x20 | 32 | 1 | +0x404 |
| 26 | +0x62410 | 0x001568F0 | `func_001D4200` | 0x20 | 1 | 1 | +0x24 |
| 27 | +0x5D880 | 0x00156978 | `func_001C9838` | 0x30 | 64 | 2 | +0xC08 |
| 28 | +0x61BE0 | 0x00156A00 | `func_001C0010` | 0x40 | 8 | 1 | +0x204 |
| 29 | +0x5E8A0 | 0x00156A88 | `func_001D34F0` | 0x10 | 32 | 1 | +0x204 |
| 30 | +0x5F2C0 | 0x00156B10 | `func_001BE778` | 0x40 | 32 | 1 | +0x804 |
| 31 | +0x61420 | 0x00156B98 | `func_001A1C08` | 0x20 | 16 | 1 | +0x204 |
| 32 | +0x4A830 | 0x00156C20 | `func_00184DF0` | 0x210 | 128 | 4 | +0x10810 |
| 33 | +0x604F0 | 0x00156CB0 | `func_001859D8` | 0x40 | 32 | 1 | +0x804 |
| 34 | +0x447D0 | 0x00156D38 | `func_0018EC70` | 0x30 | 256 | 8 | +0x3020 |
| 35 | +0x61AD0 | 0x00156DC0 | `func_0018AF40` | 0x20 | 8 | 1 | +0x104 |
| 36 | +0x39F40 | 0x00156E48 | `func_00197A98` | 0x30 | 512 | 16 | +0x6040 |
| 37 | +0x5EAB0 | 0x00156ED0 | `func_0017F6D8` | 0x40 | 32 | 1 | +0x804 |
| 38 | +0x5D070 | 0x00156F58 | `func_00181CB8` | 0x20 | 64 | 2 | +0x808 |
| 39 | +0x60D00 | 0x00156FE0 | `func_00182948` | 0x30 | 16 | 1 | +0x304 |
| 40 | +0x5B050 | 0x00157068 | `func_00199788` | 0x40 | 128 | 4 | +0x2010 |
| 41 | +0x5FAD0 | 0x001570F0 | `func_00186DA0` | 0x20 | 32 | 1 | +0x404 |
| 42 | +0x61010 | 0x00157178 | `func_00188B18` | 0x40 | 16 | 1 | +0x404 |
| 43 | +0x47800 | 0x00157200 | `func_00189B88` | 0x30 | 256 | 8 | +0x3020 |
| 44 | +0x61630 | 0x00157288 | `func_00183760` | 0x30 | 16 | 1 | +0x304 |
| 45 | +0x61DF0 | 0x00157310 | `func_00196908` | 0x20 | 8 | 1 | +0x104 |

Pool end = counter + 4 (bytes from the pool base); the pools do not overlap (each starts 16-byte aligned; the 4–12-byte gaps are padding), and the highest one (+0x62410, 0x28 bytes) ends at +0x62438, right below the block's tail fields at +0x62440…+0x62498.
##### Per-record init functions of the effect pools
Notation: r = `NextRandomFloat()` (random table, `../game/random.md`; the cursor is **not** reset during loading, so every read advances it), R = newlib `rand()`, `cvt` = EE `cvt.w.s` (truncation toward zero, saturating), `U` = `__fixunssfsi` 0x0028D220 (software float → unsigned: negative → 0, ≥ 2³² → 0xFFFFFFFF), T = block word +0x6249C (HYPOTHESIS: an effect frame clock; it is **not** written by `InitEffects` before these reads, so whatever value the block already holds is used). Records are initialised in ascending order and the pools in the call order of the table above, so the random reads happen in exactly that order. "s16 x % 0x43" is a signed `div` remainder after sign-extending to 16 bits (the "unreachable" blocks in Ghidra are the divide-by-zero `break 7` traps).

| # | Init | Writes |
|---|---|---|
| 1, 10 | `func_001DB0A8` | +0xF4, +0xF0, +0xE8, +0xEC = 0 |
| 3 | `func_001F1300` | +0x20 = 0 |
| 4 | `func_001F38F0` | +0x198, +0x134, +0x138, +0x184, +0x188 = 0 |
| 5 | `func_001F6978` | +0x24 = 0 |
| 6 | `func_001FC818` | u16 +0x10, +0x12 = 0 |
| 7 | `func_001E4588` | bytes +0x39, +0x3A = 0, +0x24 = 0, words +0x28…+0x34 = 0 (written from +0x34 down) |
| 8 | `func_001E7FF0` | u16 +0x50 = 0, +0x48, +0x4C = 0 |
| 9 | `func_001FF7E8` | u16 +0x20 = 0 |
| 11 | `func_00203480` | +0x10 = 0 |
| 12 | `func_00206588` | +0xC4, +0xC8, +0xC0, +0xB8, +0xBC = 0; **r**: +0x30 = U(r · *0x0040FF10) + T; +0xD4 = −1 |
| 13 | `func_0020A4A0` | +0xA4, +0xD0, +0xD4, +0xA8 = 0 |
| 14 | `func_0020C378` | +0x20 = 0 |
| 15 | `func_001E97F0` | u16 +0x20 = 0, +0x40 = 0, u16 +0x22 = 0, +0x44 = 0 |
| 16 | `func_001EB708` | +0x110 = 0 |
| 17 | `func_001EC458` | +0x20, +0x24 = 0; **r**: +0x28 = U(r · *0x0040FE1C + (float)(u32)T) (the u32 → float conversion of T is unsigned) |
| 18 | `func_001ED3C8` | +0x30 = 0 |
| 19 | `func_001DDBD0` | +0xF0, +0xF4, +0xC8, +0xC4, +0x100 = 0, +0x104 = 12, +0xF8 = 60; **r**: +0xFC = U(r · *0x0040FDB8) + T |
| 20 | `func_001DF9A0` | +0x44, +0x38, +0x34, +0x40 = 0 |
| 21 | `func_001CD400` | +0x20 = 0; **R, R**: u16 +0x2C = (R₁ + R₂) & 0x7F; **r**: +0x28 = r; +0x24 = 360.0f |
| 22 | `func_0019D9C0` | +0x20 = 0; by the **stale** s16 +0x2E (kind, left from earlier use): **r**, then +0x1C = (r · k + 1) · n · 60 with (kind: k, n) = 0: *0x0040F36C, 10; 0xD2: *0x0040F388, 22; 0xD3: *0x0040F384, 22; 0xD4: *0x0040F378, 13; 0xD5: *0x0040F374, 12; 0xDF: *0x0040F364, *0x0040F368; 0xE0: *0x0040F37C, 14; 0xE1: *0x0040F380, 16; 0xE7: *0x0040F370, 10; 0x60: k = 0.0 (the read is still made), n = 2 → 120.0; every other kind (0xD0, 0xE6 included): *0x0040F38C, 18; then **r**: u16 +0x2C = cvt((r + 1) · 128) − 0x80 |
| 23 | `func_0019C888` | +0x20 = 0; **R, R**: +0x28 = (R₁ + R₂) & 0x7F |
| 24 | `func_001CF398` | +0x4F4 = 0, +0x4F0 = −1; **R, R, R**: u16 +0x4F8 = (R₁ + R₂ + R₃) & 0x1FF; then a loop over +0x4F0 entries clearing +0x180…+0x18C and +0x300…+0x30C per 16 bytes, which never runs because the count was just set to −1 |
| 25 | `func_001D0B40` | +0x10 = 0; **R**: u16 +0x18 = R >> 15 (arithmetic); **r**: +0x14 = r |
| 26 | `func_001D4200` | +0x10 = 0 |
| 27 | `func_001C9838` | +0x20 = 0; **R, R**: s16 +0x2C = (R₁ + R₂) / 2 (signed division); **r**: +0x28 = r; mission id `game[0x27]` < 0x13 → +0x24 = 180.0f; else **r**: +0x24 = (f + 2f · r) · 60 with f = float at block-30 sub-table *0x002B8538 + 0x20 |
| 28 | `func_001C0010` | +0x20 = 0; **R, R**: u16 +0x30 = (R₁ + R₂) & 0x1FFF; +0x24 = 240.0f |
| 29 | `func_001D34F0` | +4 = 0, +0 = −100.0f; **R, R**: s16 +0xC = (R₁ + R₂) / 2; **r**: +8 = (4 − 2r) · 60 |
| 30 | `func_001BE778` | +0x30 = 0 |
| 31 | `func_001A1C08` | +0x10 = 0, u16 +0x14 = 0xFFFF |
| 32 | `func_00184DF0` | u16 +0x208 = 0, +0x204, +0x200 = 0, u16 +0x20A = 0 |
| 33 | `func_001859D8` | +0x30 = 0 |
| 34 | `func_0018EC70` | +0x20 = 0; **r**: u16 +0x28 = s16 cvt((r + 1) · 128) % 0x43; **r**: +0x24 = r |
| 35 | `func_0018AF40` | +0x10 = 0; **r**: u16 +0x18 = s16 cvt((r + 1) · 128) % 0x43; +0x14 = 300.0f when mission id is 4, then +0x14 = 132.0f unconditionally (the first store is always overwritten) |
| 36 | `func_00197A98` | +0xC = 0; **r**: +0x1C = (0.5 − r) · 192 · 0.25 + 192; **r**: +0x20 = r; **r**: +0x24 = cvt((r + 1) · 128) − 0x80 |
| 37 | `func_0017F6D8` | +0x20 = 0; **r**: +0x28 = cvt((r + 1) · 128) % 0x2B |
| 38 | `func_00181CB8` | +0x10 = 0; **r**: +0x14 = cvt((r + 1) · 128) − 0x80; **r**: +0x18 = r |
| 39 | `func_00182948` | +0x20 = 0; **r**: +0x24 = cvt((r + 1) · 128) − 0x80; **r**: +0x28 = r |
| 40 | `func_00199788` | +0xC = 0; **r**: +0x20 = r; **r**: +0x30 = cvt((r + 1) · 128) − 0x80 |
| 41 | `func_00186DA0` | +0x10 = 0 |
| 42 | `func_00188B18` | +0x30 = 0; **r**: u16 +0x3C = s16 cvt((r + 1) · 128) % 0x43; **r**: +0x38 = r |
| 43 | `func_00189B88` | +0x20 = 0; **r**: u16 +0x2C = s16 cvt((r + 1) · 128) % 0x43; **r**: +0x24 = r |
| 44 | `func_00183760` | +0x20 = 0; **R**: +0x28 = (R >> 15) & 0x7F |
| 45 | `func_00196908` | +0x10 = 0; **r**: +0x18 = (int)(s16 cvt((r + 1) · 128)) % 0x43; **r**: +0x14 = r |

**Random consumption of the pool resets:** 4,033 `NextRandomFloat` reads (4,097 when mission id ≥ 0x13: one more per record of pool 27) and 339 `rand()` calls; then `func_001BDAD8` adds 384 table reads and, through `func_0019FF90`, **1,024 `rand()` calls**, so one `InitEffects` makes 1,363 `rand()` calls. During mission loading this does not reach gameplay: the table cursor is reset by `FlightPlayFrame` at the first gameplay frame, and `func_00111950` (step 7, after `InitEffects`) calls `func_00113750`, which **reseeds `rand()` with `srand`** (next section). But `InitEffects` is also called **during a mission** without a reseed: by `func_0010B820` (mid-mission reset: mission 4 phase 0, missions 10 and 15 phase 1; it also calls `ResetProjectiles`, `func_0011D888`, `func_0016E910`, sound stops) and by the replay start/restart `func_001704A0` (swaps *0x004106B4 with the replay pools *0x00410B00) and `func_001704D8` (then 16 bytes at *0x00410B04 = 0). There the 1,363 `rand()` calls advance the sequence that the radio system and the visual effects read afterwards. In every case the calls must be made in this order. All float arithmetic here uses the EE single-precision rules of `src/common/ee_math.h`, in the operation order given.

##### Sky star and cloud tables (`func_00113750`, called in sub 7 and again in sub 8)
`func_00113750(block 24)`: `func_00113548(*0x004104E8, 400, lhu block24+0x80)` then `func_001135C8(*0x004104E8 + 0xC80, 128, lhu block24+0x132)`. Both start with **`srand(seed)`** (newlib), so after sub 8 the global `rand()` sequence restarts from the cloud seed of the mission's block 24 and has advanced by 400 × 4 + 128 × 13 = 3,264 calls; the `rand()` calls made earlier during loading (effect pools, sub 7) do not reach gameplay. The next `rand()` users are mission 8's `func_002594F0` (3 calls, step 9) and gameplay.
- `func_00113548(out, n, seed)` (stars): `srand(seed)`; n × 8 bytes: u16 x, y, z = `rand() >> 15` (0…0xFFFF), u16 w = (`rand()` & 0x7FF) + 0x800.
- `func_001135C8(out, n, seed)` (cloud puffs): `srand(seed)`; n × 8 bytes: x, y, z each = (R₁ + R₂ + R₃ + R₄) / 4 − 0x4000 stored as s16, where the sum is a **32-bit wrapping** add of four `rand()` values (each ≤ 0x7FFFFFFF, so it often overflows to negative) and `/ 4` is signed division rounding toward zero (`+3` before `sra 2` when negative); u16 w = (`rand()` & 0xFF) + 0x100.

##### Sub 6 `func_0010FDE8` (player aircraft sounds)
State D_002A1938, player counter D_002A193C, players n = `game[0x25]`; every call ends with `func_0010AD80` (loading-screen frame).
- 0: v = slot word +0 (first IOP ring buffer) of the VAG slot `(~g_MenuBgm & 1)` (0x00353BF8 + slot · 0x94, `src/game/vag_stream.cpp`), i.e. the stream slot the menu BGM is **not** using; `func_00218B28(2)` (sound banks: keep banks 0–1, set the SPU allocation pointer to the end of bank 1, clear the entries 2… (SPU address 0, id 0xFFFF), count = 2; returns −1 without change when fewer than 3 banks exist); `func_00217778(v)` (SOUND client word +0x24 = v: the IOP staging buffer); `func_00219B48` (BD streamer: 0x00351600/04 = 0, buffers 0x00351608 = v, 0x0035160C = v + 0x30000, 0x00351618 = 0); → 1, counter 0.
- 1: wait until that slot's state +0x34 is 1 (idle) → 2.
- 2: `func_00216570(p, word S+0x1774+p·0x40, word S+0x1770+p·0x40)` (aircraft blocks 8 and 7, `../formats/AIRCRAFT.md`): bank n = current bank count; 0x0033D870 + p·0xC0 = block 8 pointer, 0x0033D868 + p·0xC0 = n; `func_00219810(n, block 7)` adds the bank header → 3.
- 3: aircraft id a = `game[p·0x10]`; `StreamBdEntryToIop(bank = word 0x002A187C + a·8, entry = word 0x002A1878 + a·8)` (`.data` table, read from the ISO); until it returns 1 the state stays 3. Then 0x0033D86C + p·0xC0 = that bank id, p + 1; all players done → counter 0, → 6; else → 2.
- 6 → 7 → 8 (two frames of delay), 8: `RequestSubState(0x1E)`.

##### Sub 0x1E `func_0010A998` (loading screen wait)
S = flight scene (gp 0x004104C4), flags S+0x168C, counter S+0x166C (incremented by `func_0010AD80`), alpha S+0x1690.
- Flag 2 set (leaving): S+0x1690 −= *0x0040DA88 (EE `sub.s`); below 0.0 → S+0x1690 = 0, `RequestSubState(7)`.
- Else flag 1 set (objectives page shown): buttons = pad word 1 of the main context (`aPad_buttons[1]`), | word 5 when S+0x1653 == 2 (two players); counter > 120 and buttons & 0x50 → flag |= 2.
- Else: counter > 60 → flag |= 2.
- Then `func_0010AD80`.

##### Subs 2–5 `StoreFlightDataPointers` 0x0010B038
Every call first draws the loading frame (`func_0010AD80`). When `IsDataLoadDone()`: the loaded container at *0x004117A8 (`{count, offset[count]}`) is turned into pointers `container + offset[i]` stored at a base chosen by the current sub, then `RequestSubState(next)`:
| Sub | Base | Next |
|---|---|---|
| 2 (mission data) | S+0x1694 | 3 |
| 3 (player aircraft) | S+0x1754 | 4 with 2 players (`game[0x25] == 2`), else 5 in demo playback (`game[0x29] == 4`), else 6 |
| 4 (second aircraft) | S+0x1794 | 6 |
| 5 (demo data) | S+0x17D4 | 6 |
(Any other sub would write through base 0 and request sub 0; not reachable.)

##### Loading-screen drawing (`func_0010AD80` → `func_00239310` or `func_00239B60`)
- `func_00239640` (sub 0/1 when `game[0x2A] == 1`): mission number shown: `game[0x27]` 0x24 → 18, 0x25 → 1, 1…0x12 → `GetCampaignMissionNumber()`, other ids → return without change; when the number is 1…18, 0x00411100 = number − 1; 0x00411828 = 0 (the SE-once counter).
- `func_00239B60(alpha, list, bucket, frames, start_pressed)` (objectives variant): t = (float)frames · *0x004102E4; when t < 1 and t < alpha, alpha = t; when `start_pressed == 1`: if 0x00411828 == 0 → `PlayMenuSe(0, 0xD)`, then 0x00411828 += 1; then `func_002396B0(t, list, bucket, frames, 0x00411828)`, `func_002399E0(list, bucket)`, `DrawBlackFade(alpha, …)`, `DrawMenuFrameFilter(alpha, …, 1)`.
- `func_002396B0(t, list, bucket, frames, pressed)` (objective text, typed out): mission index m = 0x00411100 (0 when > 17); objective table 0x003AEE28 (`game[0x28]` < 2) or 0x003AEDE0 (harder difficulties). Between `BeginMenuTextLayer` and `EndMenuTextLayer`, when t − 1 > 0: c = (t − 1) · 60 (characters shown); `DrawMenuText(c, 0x380, 0x380, 1, sprintf(fmt 0x00411108, m + 1))`, `DrawMenuText(c − 8, 0x380, 0x5C0, 3, sprintf(fmt 0x00411110, mission name table 0x003844A0[m]))`, `DrawMenuText(c − 16, 0x380, 0x760, 3, string 0x00409000)`, `DrawMenuText(c − 24, 0x380, 0x8E0, 3, objective[m])`; typing = c − (strlen(objective) + 24) < 0. Then t −= 3: when `pressed == 0` and t > 0: c = t · 60, `DrawMenuText(c, 0x380, 0xC80, 3, string 0x00409018)`, typing |= c < 12, k = (int)(2 · (t − *0x004102DC)) capped at 0x50, k × `DrawMenuText(8.0, x, 0xC80, 3, string 0x00411118)` with x = 0xAE0 + 0x40·i (a row of dots); when `pressed ≠ 0`: blink with u = (float)pressed · *0x004102E0: frac(u) > 0.25 → `DrawMenuText(100.0, 0x380, 0xC80, 3, string 0x00409028)`, else `DrawMenuTextColored(…, 0x60584C, 0x606060)`. After the layer: typing and frames % 3 == 1 → `PlayMenuSe(0, 9)`.
- `func_002399E0(list, bucket)`: a GIF packet in the current buffer (`BuildFrameDrawEnv`, then a sprite over the draw buffer using the draw-env FBP field and `(draw width + 63) / 64` as TBW; constants in the code), linked with `LinkPacket`.
- `func_00239310(alpha, list, bucket, frames, flag)` (NOW LOADING variant): nothing when alpha ≤ 0; alpha capped at 1; DMA call to the fixed packet 0x003A60A0, `BuildFrameDrawEnv`, ALPHA_2 = (Cs − 0) · FIX + Cd with FIX = (int)(alpha · 128), XYOFFSET_2 = 0, a sprite whose colour is `LerpColor(clamp(f, 0, 1), 0x80000000, 0x80808080)` with f = (float)frames · *0x004102D4; when `flag == 0` and f > 8: g = (f − 8) · 0.5, b = (1 − cosf((g − floorf(g)) · *0x004102D8)) · 0.5, and when b > 0 a second sprite with colour `LerpColor(b, 0, 0x80808080)` (HYPOTHESIS: the pulsing "PRESS START" graphic); `CloseDmaPacket`, `LinkPacket`.

(To be continued: sub 6 and the loading screen; then block A's world-render functions.)
