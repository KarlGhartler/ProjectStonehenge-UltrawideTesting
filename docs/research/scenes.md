# Scene system (top-level game states)

Phase 2 result. `(H)` = hypothesis name (plate comment in Ghidra, not renamed). Main context = struct at 0x0029F380, pointer in gp global 0x004104A8 (`ctx`), layout in `main-context.md`.

## Dispatch

`SceneStep` (H) 0x00100B18, called once per frame from the main loop:

1. `scene_table[ctx->scene]()` with `scene` = u16 `ctx+0x8`, table 0x0029FDA0 (8 entries).
2. Frame counters `ctx+0x30` (++), `ctx+0x34` (++ unless `flags & 0x100`).
3. Request flags in `ctx+0x4` (`req`), handled after the handler returns, in this order:

| `req` bit | Effect |
|---|---|
| 0x01 | scene change: `ctx+0xA` = old scene, `ctx+0x8` = `ctx+0xC`, `ctx+0xE` (sub-state) = 0, clears `ctx+0x18..0x24` and frame counters `+0x30/+0x34`, sets `flags |= 0x800`. Then `req = (req & ~1) | 0x10` ("scene just changed", cleared again one frame later) |
| 0x02 | sub-state change: `ctx+0xE` = `ctx+0x10`. Then `req = (req & ~2) | 0x20` |
| 0x40 | return: `ctx+0x8` = `ctx+0x12`, `ctx+0xE` = `ctx+0x14` |
| 0x04 | reset graphics: `sceGsSyncPath(0,0)`, re-init the double buffer at `ctx+0x40` and the two DMA packet lists `ctx+0x50/+0x60`, `func_001010F0` |

Every scene handler dispatches again on the sub-state `ctx+0xE` (u16), either through a pointer table or a `switch`.

### Request helpers (game code, 0x00100A98–0x00100B17)

| Address | Ghidra | Behaviour | Callers |
|---|---|---|---|
| 0x00100AB0 | `RequestScene` | `ctx->req = 5; ctx->next_scene = a0` (scene change + graphics reset) | 17 call sites (see graph) |
| 0x00100AC8 | `RequestSubState` | `ctx->req = 2; ctx->next_sub = a0` | ~130 call sites |
| 0x00100AF8 | `SetRequestFlags` | `ctx->req = a0` (raw) | only the movie scene (`a0 = 0x40` = return) |
| 0x00100A98 | **undefined** | `ctx->req = a2; next_scene = a0; next_sub = a1` | no `jal`/`j`/data reference found |
| 0x00100AE0 | **undefined** | `ctx->req = 7; next_scene = a0; next_sub = a1` | none found |
| 0x00100B08 | **undefined** | `ctx->ret_scene (+0x12) = a0; ctx->ret_sub (+0x14) = a1` | none found |

The three undefined helpers are probably dead code (no raw `jal`/`j` encodings and no data pointers to them anywhere in the ELF).

## The 8 scenes

| Id | Handler | Defined in Ghidra? | Sub-state dispatch | Name | Confidence |
|---|---|---|---|---|---|
| 0 | 0x001008B0 | yes (created Phase 2b) | `switch` on gp global 0x00410494 (jump table 0x00403820, 8 cases), not on `ctx+0xE` | Boot loader | high |
| 1 | 0x00211C58 | yes | none | Menu entry trampoline | medium |
| 2 | 0x0010A030 | yes | table 0x002A1620 (31 entries) | In-engine flight scene: missions (pre-mission cutscene, gameplay, end replay) and the first-boot intro | high (PCSX2) |
| 3 | 0x00211CA8 | yes | `switch` 0..3 | Full-screen movie player. **Not** used for story movies (PCSX2); real use unknown | high (code) / unknown (use) |
| 4 | 0x002262D0 | yes | table 0x00384640 (90 entries) | Front-end: title, main menu, story movies (in-menu player), mission title/intro, briefing, aircraft selection, hangar, options, extras, save | high (PCSX2) |
| 5 | 0x00246E18 | yes | table 0x003B0190 (7 entries) | Mission results / debriefing | high (PCSX2) |
| 6 | 0x002613F0 | yes (created Phase 2b) | table 0x003F8708 (10 entries; all defined since 2026-10-10) | Startup: logos, warnings, memory-card/HDD check | high (PCSX2) |
| 7 | 0x00261680 | yes | table 0x00411720 (2 entries) | HDD read error screen | medium |

### Scene 0: boot loader (0x001008B0)

A state machine in gp global 0x00410494 (`gp-0x555C`), one step per frame:

| State | Action |
|---|---|
| 0 | gp 0x00410498 = 0; `ctx->flags &= ~0x80000`; HDD context (`func_002597B8()` = 0x004BE0C0)`+0x4398 = 3`; → 1 |
| 1 | `QueueCdRead(1, GetCdFileLsn(3), 0, GetTblBuffer(1), 0, 1 sector, -1)`: `BD.TBL` into TBL slot 1 (0x00422880). Ticket in gp 0x0041049C → 2 |
| 2 | When `GetCdDoneTicket(1) >= ticket`: `LoadDataEntryAsync(0x163, func_00219840())` (DATA.CDP entry 355 into the sound buffer) → 3 |
| 3 | When `IsDataLoadDone()`: `func_0021A750(buf + buf[1])`, `func_00219B48`, `func_0021CE38(1,0xFF,0xFF)`, `func_0021CF08(0,0,0)` (sound init with the loaded data) → 4 |
| 4 | `LoadSoundBankData` (H) `func_00219B90(0x164, func_00219830())`, repeated until it returns 1: load DATA.CDP entry 356 and register its sub-blocks with the sound driver → 5 |
| 5 | `StreamBdEntryToIop` (H) `func_00219C80(0, 0)`, repeated until it returns 1. The arguments come from a zeroed 2-word stack array indexed by counter gp 0x004104A0, and the loop ends after one pass, so only one call sequence `(0, 0)` runs. It streams BD.CDP to the sound driver in ≤0x30000-byte pieces (double-buffered) → 6 |
| 6 | → 7 |
| 7 | `RequestScene(6)` |

The earlier `main` init step `func_00100768` already loads `DATA.TBL` (file id 2, slot 0), DATA entry 0 (a pointer table copied into `ctx+0xA10`), the 21 stream-audio slot file LSNs (0x0021F968/0x0021F908), and DATA entry 0x166 (358). All of these are blocking loops on `CdQueueUpdate` + `sceGsSyncV`.

Evidence: only file/sound work, and it is the first scene (`ctx` is zeroed, so scene 0 runs first). Re-decompiled as a defined function in Phase 2b: role unchanged.

### Scene 6: startup sequence (0x002613F0)

Phase 2 called this "logo / splash". Re-decompiling it in Phase 2b (after creating 0x002613F0 and decoding the 5 sub-states that are still undefined) shows it also runs the startup memory-card / HDD check. Name: `SceneStartup` (H).

The handler is only `table_0x003F8708[ctx->sub]()`. The scene work struct is `w = *0x00411714` (0x210 bytes, from the bump allocator). `w+0x1F0` is a flag word and `w+0x1F4` a frame counter. A message window object `*0x00411900` shows numbered messages with `func_0024F1B8(win, msg)`.

| Sub | Address | Defined? | Action |
|---|---|---|---|
| 0 | 0x00260E80 | yes | display setup (640×448 draw, 640×224 display, PSMZ16S), allocates `w`, two 0x28-byte texture slots (`w+0x1E0/+0x1E4`) and the message window, 0x40000-byte double buffer, audio calls; `flags |= 0x200000` → 1 |
| 1 | 0x00260FC0 | yes | `w[0] = AllocAndLoadDataEntry(0x14D)` (DATA 333) → 2 |
| 2 | 0x00260FE8 | yes | when loaded: splits entry 333 into its 4 sub-blocks: 2 splash textures (`w[0x78]`, `w[0x79]`) + message-window resources → 3 |
| 3 | 0x00261100 | yes | `func_001026B8(w+0x10)` → 4 |
| 4 | 0x00261128 | yes (created 2026-10-10) | `func_00260078()`: if any of `w+0x1F0` bits 0x100000/0x200000/0x400000/0x800000 is set, shows startup warning message 5/6/10/7 and waits for a button (`ctx->pad_buttons[1] & 0x70`); returns 1 when done → 5. Draws the window every frame |
| 5 | 0x002611A0 | yes (created 2026-10-10) | `w+0x1FE = 0`, `w+0x1E8 = 0`, `w+0x1F4 = 120`, clears bit 0x100 → 6 |
| 6 | 0x002611D8 | yes (created 2026-10-10) | memory-card probe: `func_0025F298()` = save-module state 0x003F7940; `func_0025F528(save, 0)` (slot 0: `sprintf("%s"/"%s%s", …)` file name, then read) or `func_0025F438(save, 0)` (slot 0 status, 0x540 bytes per slot). Sets `w+0x1F0` bits 0x80/0x100/0x200/0x10000 from the result; after 120 frames with bit 0x200 → 7 |
| 7 | 0x00261378 | yes | `func_00260488()` + draw `func_00260900()`. 0x00260488 evaluates the card/HDD result (`func_00260288`: card state `save+0x4C8`, error `save+0x4D4` = -0xB / -2, free space `save+0x4CC` vs `func_0023C7B0()`; HDD context `+0x10`, `+0x216`, `+0x4399`), shows messages 1–10 and 0x10, handles confirm (pad 0x50) / cancel (pad 0x20), fades the window. → 8 when done (→ 6 to retry the probe) |
| 8 | 0x00261398 | yes (created 2026-10-10) | `w+0x1FD = 0` (image 0), `w+0x1F4 = 0`, `w+0x1F0 |= 1` (start fade), `func_0025FEF8()` (flag 0x2000 if `ctx->flags & 0x400000`, else 0xC00 + buffer 0x004C35C0) → 9 |
| 9 | 0x002613D0 | yes (created 2026-10-10) | per frame: `func_00260980()` (fade/hold logic) + `func_00260B90()` (draw one GS sprite) |

Splash logic 0x00260980 (one image at a time, image index `w+0x1FD`, fade factor float `w+0x1EC`):
- bit 1: fade 60 frames (`1 - i/60`) → bit 2;
- bit 2: hold up to 120 frames (image 1 is skippable with pad `0x50`); continues only when bit 0x2000 is set → bit 4;
- bit 4: fade 60 frames (`i/60`), then exit.

Exits after the fade (`StartupSplashUpdate` (H) 0x00260980):
- After image 0, when `w+0x1F0 & 0x4000` is clear: sets game state (`g_GameState` = 0x0029F350): `+0x25 = 1`, `+0x27 = 0x23`, `[0] = 0x11`, `[1] = [3] = +0x2A = 0`, then `func_0022CC50(0)` and tail call `RequestScene(2)` at 0x00260B54: **first-boot intro** in the flight scene (PCSX2 session 01 row 4a: no AC04 save on the card). Mission id 0x23 is the intro.
- After image 0, when bit 0x4000 is set: shows image 1.
- After image 1: `func_0022CC50(1)`, then tail call `EnterMenu(0)` at 0x00260B78 → `RequestScene(4)`: **title screen**, menu page 0 (PCSX2 row 4b: save present).

Correction to the PCSX2 interpretation ("one computed call site"): there are two separate call sites in the code. Both are **tail calls** (`j`), and `EnterMenu` also tail-calls `RequestScene`. So `ra` still holds the return address of the `jal 0x00260980` in sub 9 (0x002613D8 → `ra` 0x002613E0) at the store in `RequestScene`, and both exits show the same `ra`. The choice between them is the bit 0x4000 test.

Bit 0x4000 is set by `func_00260288` when `save+0x4D8 != 0` or HDD context `+0x216 == 1`. PCSX2 (session 01, session 3) confirms that the choice follows the card contents: no AC04 save → 2 (intro), save present → 4 (title), also when the card is inserted after the "Cannot save without memory card" message. So `save+0x4D8` is probably "AC04 save found" (H).

Scene 6 is in the code range of the save/HDD module (0x00259000–0x002616E7), so that range is not only save code.

### Scene 1: menu entry trampoline (0x00211C58)

`game->unk_27 = func_00226378(0, 1)` (a lookup in the u16 table 0x00384800), then `func_00226198(4)` = enter menu scene 4 at page 4. No code path that requests scene 1 was found (see open questions).

### Who requests scenes 1 and 3 (Phase 2.5, item d)

**Nobody, in the shipped code (static, strong).** Evidence:
- Every `RequestScene` call site passes a constant 2, 4, 5, 6 or 7 (19 sites), or the saved scene id in the HDD-error scene (0x00261650/0x00261660, which returns to whatever scene was active when the error happened). There is no constant 1 or 3.
- Constant-propagated scan of every store that covers `ctx+0x0C…+0x15` (with delay slots): `next_scene` (+0x0C) is written only by `RequestScene`, by `InitMainContext` (zero) and by the unreferenced helpers 0x00100A98 / 0x00100AE0. `ret_scene` (+0x12), which scene 3 needs for its "return" exit (`SetRequestFlags(0x40)`), is written **only** by the unreferenced helper 0x00100B08 (`ret_scene = a0; ret_sub = a1`).
- No `.data` word and no `lui/addiu` pair refers to the scene 1/3 handlers or to the three helpers, except the scene table itself (0x0029FDA4, 0x0029FDAC). The 13 `jalr` sites in game code are table dispatchers.
- PCSX2 session 2 never saw scene 1 or 3 on the normal route, and story movies run in scene 4.

Interpretation (H): scenes 1 and 3 and the helpers 0x00100A98/0x00100AE0/0x00100B08 are leftovers of a development/debug path. Scene 1 jumps into the menu at page 4 with mission 1 selected; scene 3 is a stand-alone full-screen movie player that returns to a caller set by 0x00100B08. The port can keep them as dead code, or drop them after an optional PCSX2 check:
- Optional check: memory-write breakpoint on 0x0029F38C (2 bytes) while using the Extras menu movie viewer ("INTERLUDE NUMBER") and watching the staff roll. Expected: only value 4 (in-menu player), never 3.

### Scene 2: in-engine flight scene (0x0010A030)

PCSX2 (session 01): scene 2 runs the pre-mission cutscene, gameplay and end replay of every mission, and the first-boot intro (when the card has no AC04 save). Scene 2 re-requests itself for the cutscene → gameplay step (row 11).

- Per frame: `EnterFlightSubState` 0x0010A4E0, sets/clears `flags & 0x800` depending on sub-state (0x0B and 0x1C are special: 0x1C also sets `flags |= 0x200`), calls `table_0x002A1620[sub]()`, then `func_0010A158`, increments mission timers at `*(0x004104C4)+0x1654/+0x1658/+0x1660`, then `func_0010A228`, `func_0010A5D0`.
- `EnterFlightSubState` (confirmed) does work only on the frame after a sub-state change (`req & 0x30` with only 0x20 set). It zeroes the two frame timers and clears `flags & 0x40000`, then jumps through table 0x00403C60 (index `sub - 2`, 28 entries). Every case is a tail jump (`j`). Subs 2, 3, 4 and 5 all go to `LoadFlightDataEntry`; the others go to per-sub init functions (6 → 0x0010FDD8, 0xA → 0x0010A230, 0xB → 0x0010A2D8, …).

#### Mission data loading: `LoadFlightDataEntry` 0x0010AF10 (re-analysed in Phase 2.5)

The function takes **no arguments**. In PCSX2 session 4, `a0` (0x0010A54C / 0x0010A554) and `a1` (1) were leftovers: `a0` held the jump-table target that `EnterFlightSubState` had just loaded (`lw a0, table[sub-2]; jr a0`). 0x0010A54C is the case for sub 2 and 0x0010A554 the case for sub 3, and both are `j 0x0010AF10`. Because they are tail jumps, `ra` stayed at the scene handler (0x0010A040). So the 6 hits are: sub 2 (mission data) followed by sub 3 (player aircraft), once per entry into scene 2. Mission 1 enters scene 2 twice (pre-mission cutscene, then gameplay) and mission 2 once. Subs 4 and 5 were not hit on that route.

It sets `flags |= 0x40000`, picks the DATA index from `ctx->sub` and the game-state block, and calls `AllocAndLoadDataEntry(idx)` at 0x0010B020 (index in `a2`, copied to `a0` in the delay slot). The result pointer goes to gp 0x004117A8.

| Sub-state | DATA.CDP index | Meaning |
|---|---|---|
| 2 | `id + 0x52` when `id < 0x13` **and** bit `game[0x00]` (player aircraft id) of `g_AltMissionAircraftMask` (gp 0x004104C8, constant 0x00204114 = aircraft 2, 4, 8, 14, 21) is set; otherwise `id + 1` | per-mission data (confirmed, PCSX2 session 7); `id` = `game[0x27]` = byte 0x0029F377 |
| 3 | `0x65 + a`, `0x7D + a` or `0x95 + a`; `a` = `game[0x00]`, variant `game[0x01]` (0/1/2) | player aircraft (3 variants × 24) |
| 4 | same formula with `game[0x10]` / `game[0x11]` | second aircraft (wingman?) |
| 5 | `0xAD + game[0x2B]` | |

Where the mission number comes from:
- `g_GameState` (gp 0x004104A4) points at the game-state block 0x0029F350, set at boot by `func_001006C0` (which also sets `game[0x27] = 1`). All later writes to `game[0x27]` go through this pointer, so Ghidra shows only one direct write to 0x0029F377.
- The menus keep the campaign mission number in `g_MissionNo` (u16 0x002A15B8, 1..18) and convert it with `GetMissionId(list, n)` 0x00226378 = `table_0x00384800[list_offset + n - 1]`:
  - list 0 (18 campaign missions): mission 1..18 → id 1, 2, 3, 16, 4, 5, 6, 7, 8, 9, 17, 10, 11, 18, 12, 13, 14, 15;
  - list 1 (5): ids 21, 22, 23, 25, 24; list 2 (5): 32, 33, 34, 38, 39; list 3 (8): 27, 26, 28, 29, 31, 30, 20, 40 (H: extra / free / special missions).
- Writers of `game[0x27]`: `MissionTitleUpdate` (H) 0x00230308 (`GetMissionId(0, g_MissionNo)`), `func_00226500` (next mission), scene 1 0x00211C58, `func_00226A98`, `func_00230A78`, `func_00233808`, `func_0023A550`, `func_0023AC70`. Fixed ids: 0x23 = first-boot intro (scene 6), 0x24 after mission 18, 0x25 for mission 1 in a special case (`AircraftSelectUpdate` (H) 0x00235AB8).
- So the DATA index for campaign mission n is `table[n-1]`, then `+ 0x52` or `+ 1` depending on the **player's aircraft**, not on the kind of mission. The mask is a never-written `.sdata` constant 0x00204114 (bits 2, 4, 8, 14, 21), read only at 0x0010AF54. In PCSX2 session 7 the player flew aircraft 2 (aircraft entry 0x67 = 0x65 + 2), so missions 1–4 (ids 1, 2, 3, 0x10) loaded DATA 83, 84, 85, 98. The Phase 2.5 prediction "mission 4 → DATA 17" assumed the `+ 1` branch and was wrong for that aircraft. With an aircraft outside the mask, the same missions should load DATA 2, 3, 4, 17 (H, to check in PCSX2). DATA 1–100 therefore holds two sets of mission data: `id + 1` (ids 0–99) and the aircraft-specific alternates `id + 0x52` for ids 1–18 (DATA 83–100). What differs between the two sets is open.
- Ids ≥ 0x13 always use `+ 1`. Id 0x25 (in-engine opening sequence before mission 1, set by `AircraftSelectUpdate` (H) for campaign mission 1) loaded DATA 38 with aircraft entry 0x7C (aircraft 0x17), confirmed in PCSX2. Ids 0x29–0x50 (DATA 42–81) are not produced by any table found so far.

**PCSX2 check done (session 7, `pcsx2-sessions-02.md`):** breakpoint 0x0010B020 confirmed sub-state 2 = mission data and 3 = player aircraft, and the index formula above. Open: a run with an aircraft outside the mask (expected `id + 1`).

- Exits: `RequestScene(5)` from `MissionEndUpdate` (H) 0x0016E2D0 (results), also 0x0010B500 and 0x0017A1B8. `RequestScene(2)` from `MissionEventUpdate` (H) 0x00177058 (cutscene → gameplay). `EnterMenu(0)` from 0x00177058 when `game[0x27] == 0x23` (intro → title).

### Scene 3: movie player (0x00211CA8)

Sub-states (switch):
- 0 0x00211D48: `WorkHeapAlloc(0x600000, 64)` stream buffer, 640×448 display (`ctx+0x70..`).
- 1 0x00211E68: RCNT0 mode 0x83, display setup, `MovieStart` (H) 0x00212150(`game->unk_20`).
- 2 0x00211FB0: per frame `MovieDecodeFrame` (H) 0x002122D0, waits until RCNT0 ≥ 300.
- 3 0x002120D8: stop, `SetRequestFlags(0x40)` = return to `ctx+0x12`/`+0x14`.

The movie id indexes the table at 0x002BBA10 (32 × 0x14 bytes, `docs/formats/SMV.md`): 0 INTRO, 1–12 SS01–SS12, 13–30 MIS01–MIS18, 31 STAFF. The menu scene also plays movies in place, through 0x00238778/0x00238728/0x00238810 (briefing videos).

Evidence: SMV file ids, MPEG stream buffer, return-to-caller exit. PCSX2 (session 01): story movies and mission intros play in **scene 4**, not here, and scene 3 was never requested on the normal route (new game → missions 1–2). Who enters scene 3 is still unknown. Candidates: the title-screen idle movie, the ending/staff roll, or the extras movie viewer ("INTERLUDE NUMBER").

### Scene 4: front-end menus (0x002262D0)

`table_0x00384640[sub]()` (90 entries, 52 unique), then `func_00226CD0` if `*(0x00410E88)+0x10`, then `func_0022CAF0`. `func_00226198(page)` (stores `page` in gp 0x00410E7C (`gp-0x4B74`) and calls `RequestScene(4)`) is how other scenes enter the menu at a given page. `func_002261A8(sub)` stores 0x00410E7E and requests a sub-state.

Strings referenced from 0x00231000–0x00246E00: `AIRCRAFT >`, `WEAPON >`, `BUY >`/`SELL >` (hangar), `MISSION OBJECTIVES:`, `COORDINATES >`, `NOW LOADING` (briefing), `BRIGHTNESS >` (options), `TOTAL FLIGHT TIME >` (records), `BGM NUMBER >`, `INTERLUDE NUMBER >` (extras: sound test, movie viewer), `icon.sys` (save).

Exits: `RequestScene(2)` from `MenuGoTo` 0x00226450 (kinds 2, 3, with `game[0x2A]` = 0/1) and 0x0022CC68 (2 sites) = start mission.

Navigation helpers (confirmed):
- `EnterMenu(page)` 0x00226198: `g_MenuPage = page`, tail call `RequestScene(4)`. This restarts scene 4 at sub-state 0 with the new page.
- `SetMenuSubPage(n)` 0x002261A8: `g_MenuSubPage = n`, tail call `RequestSubState(1)`.
- `MenuGoTo(kind, arg)` 0x00226450: kind 0 → `EnterMenu(arg)`, 1 → `SetMenuSubPage(arg)`, 2/3 → `game[0x2A] = 0/1` + `RequestScene(2)`. Most menu pages store a `(kind, arg)` pair for "next" and call this (16 callers).
- `EnterPostMissionMenu` (H) 0x00226B78: after results, `EnterMenu(0x20)` with `ctx->ret_sub` = 1..11 for missions 1, 2, 5, 7, 8, 12, 14, 15, 17, 18 (story movie after those missions, H), otherwise `EnterMenu(0xB)`.

Title screen (PCSX2): scene 4, sub-state 6, `g_MenuPage` = 0, `g_MenuSubPage` = 2.

In-menu movie player (story movies, mission intros; PCSX2 rows 5–8). Scene 4 sub-states 7–13:
- 7–10 `MenuMovieSetup` (H) 0x002390C8: sub 9 with `0x002A15CC & 8` skips straight to `MenuGoTo`; otherwise starts the movie (descriptor `*0x00411824`) → sub 0xB;
- 0xB 0x00239188: waits for the stream → 0xC;
- 0xC 0x00239248: plays (`func_00238810`) → 0xD;
- 0xD `MenuMovieEnd` (H) 0x002392A8: when finished, tail call `MenuGoTo(desc+0x278, desc+0x27C)`.

So a movie's "next" target is stored in its descriptor.

### Scene 5: mission results (0x00246E18)

`table_0x003B0190[sub]()`: 0x0024C328, 0x0024C700 (both undefined), 0x0024C758, 0x0024C7C8, 0x0024C850, 0x0024C918, 0x0024CA28. The last calls `func_00226198` (3 sites) = back to the menus. The region references `MISSION %02d\n`, `TRIAL MISSION %02d\n`, `PLAYER 01`, `GRND TGT` (0x0024B248). Name is a HYPOTHESIS until the screen is seen in PCSX2.

### Scene 7: HDD read error (0x00261680)

Entered only from `func_002616B8`, which `CdReadStep` (0x00214A40) calls when a request flagged as HDD (`lsn == -1`, i.e. DATA.CDP installed to the HDD) fails. The function saves the current scene in `*0x00411718` and calls `RequestScene(7)`.
- 0x00261428 (sub 0): stops audio, allocates a message window (0x0024D470/0x0024D580).
- 0x00261530 (sub 1): draws it and waits for pad bit 0x10 (`ctx+0x2B4`), then `RequestScene(saved)` (and `func_00226198(1)` when the saved scene was 4).

## Transition graph (static + PCSX2 session 01)

`ra` is the value seen at the store in `RequestScene` (pc 0x00100ABC). It points at the requester's `jal`. When the request is reached through tail calls (`j EnterMenu`, `j MenuGoTo`, `j RequestScene`), `ra` instead points at the last real `jal` in the chain. The "Requester" column gives the function that actually decided.

```
main ─► 0 boot ─► 6 startup ─┬─(no AC04 save)─► 2 flight: first-boot intro ─► 4 title
                              └─(save present)─► 4 title
4 menus ─► 4 (page changes, story movies, mission title/intro, briefing) ─► 2 flight
2 flight: cutscene ─► 2 gameplay/replay ─► 5 results ─► 4 (story movie / next menu)
any (HDD read error) ─► 7 ─► previous scene
1, 3: never requested by any code (static, see "Who requests scenes 1 and 3"); 3 would return to ctx+0x12, which only the unreferenced helper 0x00100B08 sets
```

| # | Transition | Value | ra | Requester (call site) | Path |
|---|---|---|---|---|---|
| 3 | boot → startup | 6 | 0x00100A84 | `SceneBootLoader` (H) 0x001008B0 state 7 (`jal RequestScene` 0x00100A7C) | direct |
| 4a | startup → first-boot intro | 2 | 0x002613E0 | `StartupSplashUpdate` (H) 0x00260980, `j RequestScene` 0x00260B54, called from scene 6 sub 9 (0x002613D8) | tail call |
| 4b | startup → title | 4 | 0x002613E0 | `StartupSplashUpdate` (H), `j EnterMenu(0)` 0x00260B78 | tail calls |
| 4a' | intro → title | 4 | 0x00178CE8 | `MissionEventUpdate` (H) 0x00177058, `jal EnterMenu(0)` 0x00178CE0 when `game[0x27] == 0x23` | `EnterMenu` tail |
| 5 | main menu (New Game) → story movie | 4 | 0x0022D8B4 | `MainMenuUpdate` (H) 0x0022D698, `jal MenuGoTo(+0x680, +0x684)` 0x0022D8AC | `MenuGoTo` → `EnterMenu` tails |
| 6 | story movie → mission title | 4 | 0x002262FC | `MenuMovieEnd` (H) 0x002392A8 (scene 4 sub 0xD), `j MenuGoTo` 0x002392F8. `ra` is the `jalr` in `SceneMenu` 0x002262F4 | tail calls |
| 7 | mission title → mission intro | 4 | 0x002307A8 | `MissionTitleUpdate` (H) 0x00230308 (`EnterMenu(0x1F)` for mission 1, else `EnterMenu(3)`), called by page handler 0x00230790 at 0x002307A0 | tail calls |
| 8 | mission intro → briefing | 4 | 0x002262FC | `MenuMovieEnd` (H) 0x002392A8 (mission intro = in-menu movie) | tail calls |
| 9 | briefing → aircraft selection | 4 | 0x00230FE8 | `BriefingUpdate` (H) 0x00230CA0 → `MenuGoTo(+0x670, +0x674)`, called by page handler 0x00230FC8 at 0x00230FE0 | tail calls |
| 10 | aircraft selection → mission | 2 | 0x00235B38 | `AircraftSelectUpdate` (H) 0x00235AB8 (scene 4 sub 36), `jal MenuGoTo(+0xA8, +0xAA)` 0x00235B30, kind 2/3 | `MenuGoTo` |
| 11 | pre-mission cutscene → gameplay | 2 | 0x00178D64 | `MissionEventUpdate` (H) 0x00177058, `jal RequestScene(2)` 0x00178D5C (`game[0x2A] = 0`) | direct |
| 12 | gameplay/replay → results | 5 | 0x0016E450 | `MissionEndUpdate` (H) 0x0016E2D0, `jal RequestScene(5)` 0x0016E448 (switch case, table 0x00404FC0) | direct |
| 13 | results → story movie | 4 | 0x0024CB60 | `ResultsExit` (H) 0x0024CA28 (scene 5 sub 6), `jal EnterPostMissionMenu` 0x0024CB58 → `EnterMenu(0x20)` | tail calls |

PCSX2 rows 1–2 (value 0) are not requests: they are the kernel clearing memory at boot and `InitMainContext` 0x00100650.

The PCSX2 "pause snapshots" (pc 0x00264530, ra 0x00261C4C) are **not** a shared message/prompt routine. 0x00261C4C is inside `sceGsSyncV` 0x00261C20, and 0x00264520 (`PollVblankStart`, renamed) is the libgraph helper that busy-polls `INTC_STAT` bit 2 (VBLANK start). It is not a syscall stub. Any pause while the game idles lands in the main loop's per-frame vsync wait. The prompts themselves are ordinary per-frame menu sub-states.

## Functions created from the scene tables (Phase 2b)

The 29 targets listed in Phase 2 were created as functions after user approval. Checks before each one:
- no existing function at or containing the address;
- the previous words are the end of a function (`jr ra` or a tail-call `j`, then the delay slot, then a `nop` pad);
- the entry is a stack-frame prologue (`addiu sp,sp,-N` within the first two instructions);
- after creation, the body ends on `jr ra` or a tail call to an existing function (`RequestSubState`, 0x00226280, 0x0010AD80, 0x0010BD68).

All are logged in `rename-log.csv`:
- Scene table: 0x001008B0 (scene 0), 0x002613F0 (scene 6).
- Scene 2 table 0x002A1620: 0x0010A998, 0x0010AB58, 0x0010BA38, 0x0010BAB8, 0x0010BCD8.
- Scene 4 table 0x00384640: 0x002261B8, 0x0022DE00, 0x0022DEA0, 0x00230708, 0x00230F78, 0x00236340, 0x00238278, 0x00238310, 0x002390C8, 0x002392A8, 0x0023C6A8, 0x0023EAA0, 0x00240E78, 0x00240F18, 0x00242360, 0x00243778, 0x00244320, 0x002443C0, 0x00245B00, 0x00246A08.
- Scene 5 table 0x003B0190: 0x0024C328, 0x0024C700.

Before creating 0x001008B0, the listing in 0x00100880–0x0010091F (where the Phase 2 `disassemble_bytes` call ran) was compared with the ELF: the bytes are identical, and all 40 instructions decode the same as an independent decode of the raw words, including delay slots and branch/jump targets. No wrong instructions were committed.

## Undefined code still referenced by the scene tables

A full re-check of all 86 distinct targets in the sub-state tables of scenes 2, 4, 5, 6 and 7 found 5 more undefined entries, all in scene 6 (Phase 2 missed scene 6's own table). Created on 2026-10-10 (user-approved):
- 0x00261128 (sub 4), 0x002611A0 (sub 5), 0x002611D8 (sub 6), 0x00261398 (sub 8), 0x002613D0 (sub 9).

All five have a prologue (or a leaf body) directly after a function end, and end on `jr ra` or a tail call (decoded offline).

Scene-0 helpers (no references): 0x00100A98, 0x00100AE0, 0x00100B08.

`find_code_gaps` reports 160 gaps ≥ 64 bytes in the program, about 50 of them in `.text` with orphaned instructions, so the auto-analysis missed a lot of table-dispatched code (`open-questions.md`).
