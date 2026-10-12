# Scene system (top-level game states)

`(H)` marks a hypothesis name (Ghidra plate comment, function not renamed). The main context (`ctx`) is the struct at 0x0029F380 (`main-context.md`).

## Dispatch

`SceneStep` 0x00100B18, called once per frame by the main loop (`boot.md`):

1. `scene_table[ctx->scene]()`, table 0x0029FDA0 (8 entries).
2. Frame counters `scene_frame` (++) and `scene_frame_unpaused` (++ unless `flags & 0x100`).
3. Request flags in `ctx->req`, handled after the handler returns, in this order:

| `req` bit | Effect |
|---|---|
| 0x01 | scene change: `prev_scene` = old scene, `scene` = `next_scene`, `sub` = 0; clears `scene_work` and `scene_frame`/`scene_frame_unpaused`; `flags |= 0x800`. Then `req = (req & ~1) | 0x10` ("scene just changed", cleared one frame later) |
| 0x02 | sub-state change: `sub` = `next_sub`. Then `req = (req & ~2) | 0x20` |
| 0x40 | return: `scene` = `ret_scene`, `sub` = `ret_sub` |
| 0x04 | reset graphics: `sceGsSyncPath(0,0)`, re-init the double buffer and the two DMA packet lists, `ResetWorkHeapToMark` 0x001010F0 |

Every scene handler dispatches again on `ctx->sub`, through a pointer table or a `switch`.

### Request helpers (0x00100A98–0x00100B17)

| Address | Function | Behaviour | Callers |
|---|---|---|---|
| 0x00100AB0 | `RequestScene` | `req = 5; next_scene = a0` (scene change + graphics reset) | 19 call sites, constants 2, 4, 5, 6, 7 or the saved scene in scene 7 |
| 0x00100AC8 | `RequestSubState` | `req = 2; next_sub = a0` | about 130 call sites |
| 0x00100AF8 | `SetRequestFlags` | `req = a0` (raw) | only the movie scene (`a0 = 0x40` = return) |
| 0x00100A98 | `func_00100A98` | `req = a2; next_scene = a0; next_sub = a1` | none (dead) |
| 0x00100AE0 | `func_00100AE0` | `req = 7; next_scene = a0; next_sub = a1` | none (dead) |
| 0x00100B08 | `SetReturnScene` (H) | `ret_scene = a0; ret_sub = a1` | none (dead); the only writer of `ret_scene` |

## The 8 scenes

| Id | Handler | Sub-state dispatch | Role | Details |
|---|---|---|---|---|
| 0 | `SceneBootLoader` (H) 0x001008B0 | `switch` on gp global 0x00410494 (jump table 0x00403820, 8 cases) | boot loader | below |
| 1 | `SceneMenuEntry` (H) 0x00211C58 | none | menu entry trampoline; **never requested** (dead) | below |
| 2 | `SceneFlight` 0x0010A030 | table 0x002A1620 (31 entries) | in-engine flight scene: missions (pre-mission cutscene, gameplay, end replay), the no-save intro (every boot without an AC04 save on the card), title demos | `../subsystems/mission-flow.md` |
| 3 | `SceneMovie` (H) 0x00211CA8 | `switch` 0..3 | full-screen movie player; **never requested** (dead) | below |
| 4 | `SceneMenu` 0x002262D0 | table 0x00384640 (90 entries, 52 unique) | front end: title, menus, story movies (in-menu player), briefing, hangar, options, extras, save | `../subsystems/frontend.md` |
| 5 | `SceneResults` 0x00246E18 | table 0x003B0190 (7 entries) | mission results and debriefing | `../subsystems/results-startup.md` |
| 6 | `SceneStartup` 0x002613F0 | table 0x003F8708 (10 entries) | startup: splash images, warnings, memory-card check | `../subsystems/results-startup.md` |
| 7 | `SceneHddError` 0x00261680 | table 0x00411720 (2 entries) | HDD read error screen; unreachable in this build | `../subsystems/results-startup.md` |

Scenes 2, 4, 5 and 6 are confirmed in PCSX2 (`../pcsx2/sessions-01.md`).

### Scene 0: boot loader (`SceneBootLoader` (H) 0x001008B0)

A state machine in gp global 0x00410494 (`gp−0x555C`), one step per frame. It is the first scene because `ctx` starts zeroed.

| State | Action |
|---|---|
| 0 | gp 0x00410498 = 0; `ctx->flags &= ~0x80000`; HDD context (`func_002597B8()` = 0x004BE0C0) +0x4398 = 3 → 1 |
| 1 | `QueueCdRead(1, GetCdFileLsn(3), 0, GetTblBuffer(1), 0, 1 sector, −1)`: `BD.TBL` into TBL slot 1 (0x00422880); ticket in gp 0x0041049C → 2 |
| 2 | when `GetCdDoneTicket(1) >= ticket`: `LoadDataEntryAsync(0x163, func_00219840())` (DATA 355 into the sound buffer) → 3 |
| 3 | when `IsDataLoadDone()`: `func_0021A750(buf + buf[1])`, `func_00219B48`, `func_0021CE38(1,0xFF,0xFF)`, `SetSoundDataInputVolume(0,0,0)` (sound init with the loaded data) → 4 |
| 4 | `LoadSoundBankData` (H) 0x00219B90`(0x164, func_00219830())`, repeated until it returns 1: loads DATA 356 and registers its sub-blocks with the sound driver → 5 |
| 5 | `StreamBdEntryToIop` (H) 0x00219C80`(0, 0)`, repeated until it returns 1: streams BD entry 0 into bank slot 0 in ≤ 0x30000-byte double-buffered pieces. The arguments come from a zeroed 2-word stack array indexed by gp counter 0x004104A0; the loop ends after one pass → 6 |
| 6 | → 7 |
| 7 | `RequestScene(6)` |

`LoadBootData` (in `main`) has already loaded `DATA.TBL`, DATA entry 0, the 21 stream-audio slot LSNs and DATA 0x166 (`../game/file-loading.md`).

### Scene 1: menu entry trampoline (`SceneMenuEntry` (H) 0x00211C58)

`game[0x27] = GetMissionId(0, 1)`, then `EnterMenu(4)`: the menu at page 4 with mission 1 selected.

### Scene 3: full-screen movie player (`SceneMovie` (H) 0x00211CA8)

| Sub | Address | Action |
|---|---|---|
| 0 | 0x00211D48 | `WorkHeapAlloc(0x600000, 64)` stream buffer, 640×448 display |
| 1 | 0x00211E68 | RCNT0 mode 0x83, display setup, `MovieStart` (H) 0x00212150`(game->movie)` |
| 2 | 0x00211FB0 | per frame `MovieDecodeFrame` (H) 0x002122D0; waits until RCNT0 ≥ 300 |
| 3 | 0x002120D8 | stop, `SetRequestFlags(0x40)` = return to `ret_scene` / `ret_sub` |

The movie id indexes the table 0x002BBA10 (32 × 0x14 bytes, `../formats/SMV.md`): 0 INTRO, 1–12 SS01–SS12, 13–30 MIS01–MIS18, 31 STAFF. Story movies and mission intros use the in-menu player of scene 4 instead (`../subsystems/frontend.md`).

### Why scenes 1 and 3 are unreachable

- Every `RequestScene` call site passes a constant 2, 4, 5, 6 or 7, or the saved scene id in scene 7 (0x00261650 / 0x00261660). There is no constant 1 or 3.
- A constant-propagated scan of every store that covers `ctx+0x0C…+0x15` (including delay slots) finds `next_scene` written only by `RequestScene`, by `InitMainContext` (zero) and by the dead helpers 0x00100A98 / 0x00100AE0. `ret_scene`, which scene 3 needs for its exit, is written only by the dead helper 0x00100B08.
- No `.data` word and no `lui/addiu` pair refers to the scene 1/3 handlers or the three helpers, except the scene table itself (0x0029FDA4, 0x0029FDAC). The 13 `jalr` sites in game code are table dispatchers.
- PCSX2 session 2 never saw scene 1 or 3 on the normal route.

Interpretation (H): leftovers of a development or debug path. Catalogued in `../port/dead-code.md`.

## Transition graph

```
main ─► 0 boot ─► 6 startup ─┬─(no AC04 save)─► 2 flight: no-save intro ─► 4 title
                              └─(save present)─► 4 title
4 menus ─► 4 (page changes, story movies, mission title/intro, briefing) ─► 2 flight
4 title idle ─► 2 flight: title demo ─► 4 title
2 flight: cutscene ─► 2 gameplay/replay ─► 5 results ─► 4 (story movie / next menu)
1, 3: never requested; 7: entered only after an HDD read failure (unreachable)
```

`ra` below is the value seen at the store in `RequestScene` (pc 0x00100ABC) in PCSX2 session 01. It points at the requester's `jal`. When the request goes through tail calls (`j EnterMenu`, `j MenuGoTo`, `j RequestScene`), `ra` points at the last real `jal` in the chain instead. The "Requester" column gives the function that decides.

| # | Transition | Value | ra | Requester (call site) | Path |
|---|---|---|---|---|---|
| 3 | boot → startup | 6 | 0x00100A84 | `SceneBootLoader` (H) state 7 (`jal RequestScene` 0x00100A7C) | direct |
| 4a | startup → no-save intro | 2 | 0x002613E0 | `StartupSplashUpdate` 0x00260980, `j RequestScene` 0x00260B54, called from scene 6 sub 9 (0x002613D8) | tail call |
| 4b | startup → title | 4 | 0x002613E0 | `StartupSplashUpdate`, `j EnterMenu(0)` 0x00260B78 | tail calls |
| 4a' | intro → title | 4 | 0x00178CE8 | `UpdateInEngineSequence` 0x00177058, `jal EnterMenu(0)` 0x00178CE0 when `game[0x27] == 0x23` | `EnterMenu` tail |
| 5 | main menu (New Game) → story movie | 4 | 0x0022D8B4 | `MainMenuUpdate` 0x0022D698, `jal MenuGoTo(+0x680, +0x684)` 0x0022D8AC | `MenuGoTo` → `EnterMenu` tails |
| 6 | story movie → mission title | 4 | 0x002262FC | `MenuMovieEnd` 0x002392A8 (scene 4 sub 0xD), `j MenuGoTo` 0x002392F8; `ra` is the `jalr` in `SceneMenu` 0x002262F4 | tail calls |
| 7 | mission title → mission intro | 4 | 0x002307A8 | `MissionTitleUpdate` 0x00230308 (`EnterMenu(0x1F)` for mission 1, else `EnterMenu(3)`), called by `MissionTitleRun` 0x00230790 at 0x002307A0 | tail calls |
| 8 | mission intro → campaign menu | 4 | 0x002262FC | `MenuMovieEnd` 0x002392A8 (mission intro = in-menu movie) | tail calls |
| 9 | campaign menu → aircraft selection | 4 | 0x00230FE8 | `CampaignMenuUpdate` 0x00230CA0 → `MenuGoTo(+0x670, +0x674)`, called by `CampaignMenuRun` 0x00230FC8 at 0x00230FE0 | tail calls |
| 10 | aircraft selection → mission | 2 | 0x00235B38 | `AircraftSelectRun` 0x00235AB8 (scene 4 sub 36), `jal MenuGoTo(+0xA8, +0xAA)` 0x00235B30, kind 2/3 | `MenuGoTo` |
| 11 | pre-mission cutscene → gameplay | 2 | 0x00178D64 | `UpdateInEngineSequence` 0x00177058, `jal RequestScene(2)` 0x00178D5C (`game[0x2A] = 0`) | direct |
| 12 | gameplay/replay → results | 5 | 0x0016E450 | `UpdateReplayPlayback` 0x0016E2D0, `jal RequestScene(5)` 0x0016E448 (switch case, table 0x00404FC0) | direct |
| 13 | results → story movie | 4 | 0x0024CB60 | `ResultsExit` 0x0024CA28 (scene 5 sub 6), `jal EnterPostMissionMenu` 0x0024CB58 → `EnterMenu(0x20)` | tail calls |

Other requesters: `RequestScene(5)` also from 0x0010B500 and 0x0017A1B8; `RequestScene(2)` from `MenuGoTo` 0x00226450 (kinds 2/3) and `StartTitleDemo` 0x0022CC68 (2 sites).

PCSX2 rows 1–2 (value 0) are not requests: they are the kernel clearing memory at boot and `InitMainContext` 0x00100650.

Pausing the emulator while the game idles lands at pc 0x00264530 / ra 0x00261C4C. That is `PollVblankStart` 0x00264520 inside `sceGsSyncV` 0x00261C20, the main loop's vsync wait (busy-poll of `INTC_STAT` bit 2), not a shared prompt routine.
