# Mission flow (scene 2)

The in-engine flight scene: loading, setup, rules and event scripts, in-engine cutscenes, replay and demo, pause and end menus. Per-function status: `../port/coverage-functions.csv`, subsystems `mission`, `sequence`, `replay`. Data format: `../formats/MISSION.md`.

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
| 0x14–0x18 | 0x0010BAB8 / 0x0017A1B8 | **carrier landing** in campaign mission 3 (mission id 3), started when the player crosses the return line (objective type 2): AWACS "Approaching return line. Follow the controller's orders for landing.", HUD "RETURN TO CARRIER", landing guidance (sub 0x15, `func_00166A10`); timer 0x2A30 frames, fail and retry when leaving the ±32768 area or crashing; approach parameters differ for the aircraft in mask 0x00204114. When mission flag 4 is set (mission complete), touchdown leads to 0x17 (success → replay) and START skips to 0x18 (results); otherwise touchdown leads to 0x16 and START opens the rearm menu (0x1D) (confirmed in PCSX2, `../pcsx2/sessions-04.md`) |
| 0x19–0x1B | 0x0010BB98 / 0x0017AC68 | **carrier takeoff** after a rearm (mission id 3): requested by the rearm-menu item TAKEOFF (`func_0017DBC0`); 0x19 intro, 0x1A takeoff run (timer 0xE10, radio messages 0x12/0x13), 0x1B result (success → back to gameplay sub 0xB). Confirmed in PCSX2: 0x19 → 0x1A → 0x1B → 0x0B, normal control afterwards (`../pcsx2/sessions-05.md`) |
| 0x1C | 0x0010BC68 | game-over menu |
| 0x1D | 0x0010BCD8 | carrier **rearm menu** (TAKEOFF, CHANGE WEAPON), mission id 3 only (sub-state 0x1D seen at `func_0017A7C8` in PCSX2, `../pcsx2/sessions-05.md`) |
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

PCSX2 session 7 (`../pcsx2/sessions-02.md`, breakpoint 0x0010B020) confirms sub 2 = mission data, sub 3 = player aircraft and the formula: with aircraft 2 (entry 0x67), campaign missions 1–4 (ids 1, 2, 3, 0x10) load DATA 83, 84, 85, 98; id 0x25 loads DATA 38 with aircraft entry 0x7C (aircraft 0x17). Mission 1 enters scene 2 twice (pre-mission cutscene, then gameplay), mission 2 once. With an aircraft outside the mask the same branch takes `id + 1` (e.g. DATA 2, 3, 4, 17 for campaign missions 1–4); the branch was checked in the disassembly (0x0010AF48–0x0010AF70: `srav` of the mask by the aircraft id), so no PCSX2 run is needed.

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
- At the end of the last cut: `EnterMenu(0)` (no-save intro 0x23) or `RequestScene(2)` (cutscene → gameplay).
- Interpolation (ADR-002): sequence cameras and objects are keyed per frame, so there is one transform per step and interpolation treats them like gameplay. Camera cuts happen at cut boundaries (+0x110 changes) and must snap.

## Replay and demo

- The recorder (`RecordReplayFrame` 0x0016E8B8, ring at gp 0x00410AF8) writes every frame into a ring buffer: unit states (players full, others compact) plus events (projectiles, effects, sounds).
  - Frame markers 0x01234567 / 0x76544567; wrap marker 0x76543210.
  - Unit snapshots: `func_00135D60` allocates, `func_00135E10`/`func_00135EF8` save and restore every unit (0x48 bytes), `func_00135FE0`/`func_00136240` the players (0x50 bytes). Player animation inputs are **quantised to u8 (×255)** in the snapshot (`ai.md`).
- **Ring size = remaining work heap rounded down to 256 KiB, minus 256 KiB** (0x0016DE48). That is why the heap is 98 % used in missions (`../pcsx2/sessions-03.md`).
- Playback (`UpdateReplayPlayback`, subs 0x10–0x12):
  - swaps the effect pools with replay copies (`func_00170460`, `func_001704A0`, `func_001704C0`) and re-inits effects (`InitEffects`);
  - re-applies recorded states and events (`func_00170520` dispatch: explosion/smoke `func_00170598`, trail/flare `func_001706D0`, debris/fire `func_001708D0`, sounds `func_00170F98`);
  - exits with `EnterMenu(0)` after a demo, `EnterMenu(0x1C)`, or `RequestScene(5)` (results).
- **Replay camera** (state 0x1E0 bytes, gp 0x00410B0C, `func_00175D08`; tables 0x002B7670):
  - auto camera: `func_001756C0` picks a style with `rand() % 10` over the allowed mask (state +0x1D0), dispatching through table 0x002B77B0 (17 entries, index +0x191);
  - styles: 0 `func_001744B8` fixed view, 1 `func_00174058` chase with terrain clearance, 2/11 `func_00173670` orbit with line-of-sight check, 3 `func_00172EF0` cockpit, 4 `func_00172850` offset view, 5 `func_00172098` fly-by (eye placed ahead of the aircraft, held for 300 frames), 6 `func_00172450` low fly-by with ground clamp, 7 `func_00171E88` midpoint to the target unit, 8 `func_00171AE8` / 9 `func_00171CD0` fixed offsets in the unit frame, 10 `func_001756C0` re-pick, 12 `func_001759E8` projectile follow (special weapon → `func_001748A8`, missile → `func_00174640`, else default `func_00172B78`), 13 `func_00173020` cockpit with pad input, 14 `func_00173468` player view, 15 `func_00173A98` projectile follow (per-unit projectile record 0x00410680; switches to the hit unit for 180 frames), 16 `func_00175890` follow-target mode;
  - helpers: line of sight `func_001712F8`, clamp `func_00171460`, tracking `func_00171640`, chase `func_00175420`, weapon/kill events `func_00175B08`, player destroyed `func_00175BA0`;
  - manual mode: 5 cameras switched with L/R.
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

  **The HUD item (on/off, `func_0017DB28`/`func_0017DAF0`, ctx flag 0x20000) is only in the list after the game has been cleared once** (0x002A0318 bit 0): `func_0017DCC0` sets the item count one lower otherwise (confirmed: no HUD item before a clear, `../pcsx2/sessions-04.md`). Callbacks: RETURN TO GAME `func_0017D938`, REPLAY `func_0017D958`, RETRY `func_0017D9A8`, QUIT MISSION `func_0017DA28`, CAPTIONS `func_0017DAD8`/`func_0017DAA8`.
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
