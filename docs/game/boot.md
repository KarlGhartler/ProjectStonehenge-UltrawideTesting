# Boot, main loop, scenes and the main context

From the ELF entry point to the per-frame main loop. All addresses are EE virtual addresses in `SLUS_201.52`. `(H)` marks a hypothesis name (Ghidra plate comment, function not renamed). All other names are Ghidra names listed in `docs/symbols.csv`.

Merged 2026-10-22 from `boot.md`, `scenes.md` and `main-context.md` (no content removed): the boot path and main loop first, then the scene system ("Scene system", formerly `scenes.md`), then the main context and the game-state block ("Main context", formerly `main-context.md`).

## crt0: `entry` 0x00100008 (Sony SDK 2.3 crt0)

| Address | Action |
|---|---|
| 0x00100008–0x00100030 | zero `.sbss` + `.bss` 0x00411780–0x004CC74B with `sq zero` (16 bytes per iteration) |
| 0x00100034–0x00100064 | `gp = 0x004159F0`. Syscall 0x3C **SetupThread**(gp, stack = −1 (top of RAM), stack size 0x100000, args 0x004C55C0, root `_root` 0x001000C0). Returns the new `sp` |
| 0x0010006C–0x00100080 | syscall 0x3D **SetupHeap**(0x004CC74C = end of `.bss`, −1 = up to the stack) |
| 0x00100084 | `_InitSys` (H) 0x0026B2D0: libkernl runtime init (kernel patches, helper thread and semaphore) |
| 0x0010008C | `FlushCache(0)` |
| 0x00100094 | `ei` |
| 0x001000A4 | **`main`**(argc = *(0x004C55C0), argv = 0x004C55C4) |
| 0x001000AC | `Exit`(main's return value) (syscall 4) |

Also in crt0: `_exit` (H) 0x001000B8 = `j Exit` with a0 = 0, and `_root` 0x001000C0, an ExitThread stub (syscall 0x23) used as the main thread's root function.

`func_001001C0` (48 bytes, inside the game-code range but compiler runtime): `__deregister_frame_info(0x0029F230)` (libgcc 0x0028DFA8). Its only reference is the `.dtors` entry at 0x0029F340, run by the libc global-destructor walker 0x00288848 at exit. The game never exits, so it never runs; as C runtime it is not translated (the port has no EH frame registry).

## `main` 0x001001F0: initialisation

In call order:

| Site | Callee | Role |
|---|---|---|
| 0x001001F8 | `__main` 0x00288950 | GCC run-once global constructor call (`__do_global_ctors` (H) 0x002888A0). Runs the `.ctors` list 0x0029F308 (count 10), see below |
| 0x00100200 | `InitMainContext` 0x00100650 | main context struct at 0x0029F380; pointer in the gp global 0x004104A8 (`gp−0x5548`) (section "Main context" below) |
| 0x00100208 | `IopBoot` 0x00100400 | `sceSifInitRpc`, `sceCdInit(0)`, `sceCdMmode` by disc type (0x12 CD / 0x14 DVD), `sceSifRebootIop("cdrom0:\IOPRP23.IMG;1")`, `sceSifSyncIop`, re-init of SIF/fileio/iopheap/cdvd, then `sceSifLoadModule` in this order: SIO2MAN, MCMAN, MCSERV, PADMAN, LIBSD, SOUND, VAG, USBD, USBJOY |
| 0x00100210 | `InitHardware` 0x00100620 | `sceGsResetPath`, `sceDmaReset(1)`, `sceVu0ResetVif0` (H), `sceCdInit(0)` |
| 0x00100218 | `InitWorkHeap` 0x00100F48 | one big `malloc` block, size probed by `ProbeLargestMallocSize` 0x00100EC8; all game allocations use `WorkHeapAlloc` 0x00101000 (`memory.md`) |
| 0x00100220 | `InitGameStateGlobals` (H) 0x001006C0 | fills the `g_GameState` block 0x0029F350 (sets `game[0x27] = 1`) |
| 0x00100228 | `InitOptionData` (H) 0x001053F8 | option and save-data defaults at 0x002A0318 |
| 0x00100230 | `InitRandomTable` 0x00100D38 | `srand(1)`, `srand(123456789)`, 512 random floats at 0x00411998 (`random.md`) |
| 0x00100238 | `InitSubsystems` 0x001000D8 | `InitCdFileSystem`, `BuildCdLsnTable`, `InitSaveSystem`, `sceSifInitRpc`, `InitSoundRpc` loop, `InitVagStream` loop, audio/movie inits |
| 0x00100240 | `ApplyOptionSettings` (H) 0x00105340 | sound output mode and volumes, display offset, screen scale, vibration |
| 0x00100248 | — | `ctx->flags |= 0x800` |
| 0x00100254 | `InitGraphics` 0x001044A8 | `sceGsResetGraph(0,1,2,1)`, 512×448 draw / 512×224 display, `BuildDrawEnvPackets` |
| 0x0010025C | `LoadBootData` 0x00100768 | loads `DATA.TBL`, DATA entry 0 (→ `ctx->data0_table`), the 21 stream-audio slot LSNs, DATA 0x166. Blocking waits call `UpdateCdQueue` + `sceGsSyncV(0)` |
| 0x00100264 | `PadSystemInit` 0x00102050 | `BindUsbJoyRpc`, `scePadInit`, `scePadPortOpen` ×2 |
| 0x0010026C | `InitSprRingBuffer` 0x00103C00 | DMAC `D_RBOR` = 0x418000, `D_RBSR` = 0x7FF0; scratchpad double buffer |
| 0x00100274 | `SetWorkHeapMark` 0x001010A8 | heap mark = current offset (scene changes reset to it) |
| 0x00100280–0x00100288 | `sceGsSyncV(0)` | loop until it returns non-zero (field parity) |

### Static constructors

`__main` runs the `.ctors` list at 0x0029F30C (10 entries, count at 0x0029F308):

| Entry | Function | Effect |
|---|---|---|
| 0x0029F30C | `func_001E0A20` | seeds effect generator state 0x00410BB0 (`random.md`) |
| 0x0029F310 | `func_001E94C0` | seeds effect generator state 0x00410BE0 |
| 0x0029F314 | `func_001FF608` | seeds effect generator states 0x00410BF8–0x00410C20 |
| 0x0029F318 | `func_00210D88` | seeds effect generator states 0x00410C30 / 0x00410C38 |
| 0x0029F31C | `func_002124F8` | movie memory allocator 0x002BBA00 (alignment 0x40) |
| 0x0029F320 | `func_00214030` | resets the two movie audio feeder objects 0x002BBCB8 / 0x002BBCE0 |
| 0x0029F324 | `func_002596F8` | three `rand()` calls with newlib's default seed (before `InitRandomTable`) → radio chatter timers at 0x004BE0B0 |
| 0x0029F328, 0x0029F32C | 0x0027CF88, 0x0027C680 | C++ runtime |
| 0x0029F330 | `func_00100188` | `__register_frame_info` (eh frame 0x0029F230, object 0x00411980) |

## Main loop: 0x00100290–0x001003BC inside `main`

There is no separate main-loop function. The body is part of `main` and closes with `beq s0,zero,0x00100290`.

```
loop:                                              ; 0x00100290
    SendDmaPacketList(1, &ctx->vif1_list_head, 1)  ; 0x00104438: last frame's VIF1 chain
    SendDmaPacketList(2, &ctx->gif_list_head, 0)   ;             last frame's GIF chain
    ctx->cur_buf = SwapDoubleBuffer(&ctx->dbuf)    ; 0x00103E90
    if (!(ctx->flags & 0x40000)) `UpdatePads` 0x00102220()   ; per-frame pad/semaphore task
    FlushCache(0)
    UpdateSoundRpc()        0x00218F18             ; IOP RPC 0x81000101 (SOUND.IRX)
    UpdateVagStream()       0x0021FE60             ; IOP RPC 0x81000102 (VAG.IRX)
    s0 = SceneStep()        0x00100B18             ; scene_table[ctx->scene]()
    f  = WaitVSync()        0x00104920             ; sceGsSyncPath + sceGsSyncV
    RCNT0_COUNT (0x10000000) = 0                   ; EE timer 0 reset every frame
    UpdateDisplayFrame(f)   0x00104950             ; field copy, sceGsPutDispEnv, DrawEnv packet
    ctx->frame_count++ ; if !(flags & 0x100) ctx->frame_count_unpaused++
    flag bookkeeping on bits 0x100/0x200/0x400/0x800
    UpdateCdQueue()         0x00214350
    if (s0 == 0) goto loop
exit (never reached: SceneStep always returns 0):
    `ShutdownPads` 0x001021E8()  ; scePadEnd + RPC shutdown
    `FreeWorkHeap` 0x00100FB0()
    sceCdInit(5)     ; SCECdEXIT
    printf(0x00403700)
    return 0
```

The GPU side therefore runs one frame behind the CPU: each frame first sends the chains built in the previous frame (`../subsystems/vu-programs.md`, `../subsystems/rendering.md`).

## Scene table 0x0029FDA0

`SceneStep` calls `scene_table[ctx->scene]()`. The 8 handlers and the request mechanism are in the section "Scene system" below. The word after the table (0xFF01FF01) is not a code pointer.

## Facts for the port

- One main thread runs all game logic. The libkernl helper thread comes from `_InitSys`. The only game-owned threads (save/HDD job worker, power-off handler) are on the unreachable HDD path (`threads-timing.md`, `../port/dead-code.md`).
- The frame is paced by `sceGsSyncV` (busy-poll of INTC VBLANK). EE timer 0 is reset every frame and read only by movie code. Game timing counts frames with a fixed 1/60 s step (`threads-timing.md`).
- All game memory comes from one `malloc` block and a bump allocator; there is no `new`/`delete` (`memory.md`).
- The main context at 0x0029F380 holds the frame counters and global flags (section "Main context" below).

## Scene system (top-level game states)

`(H)` marks a hypothesis name (Ghidra plate comment, function not renamed). The main context (`ctx`) is the struct at 0x0029F380 (section "Main context" below).

### Dispatch

`SceneStep` 0x00100B18, called once per frame by the main loop (section "Main loop" above):

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

#### Request helpers (0x00100A98–0x00100B17)

| Address | Function | Behaviour | Callers |
|---|---|---|---|
| 0x00100AB0 | `RequestScene` | `req = 5; next_scene = a0` (scene change + graphics reset) | 19 call sites, constants 2, 4, 5, 6, 7 or the saved scene in scene 7 |
| 0x00100AC8 | `RequestSubState` | `req = 2; next_sub = a0` | about 130 call sites |
| 0x00100AF8 | `SetRequestFlags` | `req = a0` (raw) | only the movie scene (`a0 = 0x40` = return) |
| 0x00100A98 | `func_00100A98` | `req = a2; next_scene = a0; next_sub = a1` | none (dead) |
| 0x00100AE0 | `func_00100AE0` | `req = 7; next_scene = a0; next_sub = a1` | none (dead) |
| 0x00100B08 | `SetReturnScene` (H) | `ret_scene = a0; ret_sub = a1` | none (dead); the only writer of `ret_scene` |

### The 8 scenes

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

Scenes 2, 4, 5 and 6 are confirmed in PCSX2 (`../pcsx2/sessions.md` run 01).

#### Scene 0: boot loader (`SceneBootLoader` (H) 0x001008B0)

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

#### Scene 1: menu entry trampoline (`SceneMenuEntry` (H) 0x00211C58)

`game[0x27] = GetMissionId(0, 1)`, then `EnterMenu(4)`: the menu at page 4 with mission 1 selected.

#### Scene 3: full-screen movie player (`SceneMovie` (H) 0x00211CA8)

| Sub | Address | Action |
|---|---|---|
| 0 | 0x00211D48 | `WorkHeapAlloc(0x600000, 64)` stream buffer, 640×448 display |
| 1 | 0x00211E68 | RCNT0 mode 0x83, display setup, `MovieStart` (H) 0x00212150`(game->movie)` |
| 2 | 0x00211FB0 | per frame `MovieDecodeFrame` (H) 0x002122D0; waits until RCNT0 ≥ 300 |
| 3 | 0x002120D8 | stop, `SetRequestFlags(0x40)` = return to `ret_scene` / `ret_sub` |

The movie id indexes the table 0x002BBA10 (32 × 0x14 bytes, `../formats/SMV.md`): 0 INTRO, 1–12 SS01–SS12, 13–30 MIS01–MIS18, 31 STAFF. Story movies and mission intros use the in-menu player of scene 4 instead (`../subsystems/frontend.md`).

#### Why scenes 1 and 3 are unreachable

- Every `RequestScene` call site passes a constant 2, 4, 5, 6 or 7, or the saved scene id in scene 7 (0x00261650 / 0x00261660). There is no constant 1 or 3.
- A constant-propagated scan of every store that covers `ctx+0x0C…+0x15` (including delay slots) finds `next_scene` written only by `RequestScene`, by `InitMainContext` (zero) and by the dead helpers 0x00100A98 / 0x00100AE0. `ret_scene`, which scene 3 needs for its exit, is written only by the dead helper 0x00100B08.
- No `.data` word and no `lui/addiu` pair refers to the scene 1/3 handlers or the three helpers, except the scene table itself (0x0029FDA4, 0x0029FDAC). The 13 `jalr` sites in game code are table dispatchers.
- PCSX2 session 2 never saw scene 1 or 3 on the normal route.

Interpretation (H): leftovers of a development or debug path. Catalogued in `../port/dead-code.md`.

### Transition graph

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

## Main context (`MainContext` 0x0029F380) and game-state block (0x0029F350)

### `MainContext`

Ghidra data type `MainContext` (0xA20 bytes), applied as the global `g_MainContext` 0x0029F380. The gp pointer 0x004104A8 (`gp−0x5548`) is typed `MainContext *` and named `g_pMainContext`. Field names follow `../decisions/naming-convention.md`.

Derivation:
- every load/store through `lw rX,-0x5548(gp)` in `.text`, tracked until `rX` is overwritten (57 distinct offsets);
- fields reached through derived pointers (`ctx+0x40`, `+0x50`, `+0x60`, `+0x90`, `+0x210 + port*0x50`, `+0xA10`) from their callees: `SwapDoubleBuffer`, `SendDmaPacketList`, `BuildDrawEnvPackets` 0x00104558, `PutDispAndDrawEnv`, `PadSystemInit`, `UpdatePads` 0x00102220, `LoadBootData`.

Size: the only absolute references in 0x0029F000–0x0029FDA0 are the struct base and four globals before it, the scene table follows at 0x0029FDA0, and all 0xA20 bytes are zero in the ELF. The end at 0xA20 is a **hypothesis** (the struct may be smaller, with padding).

Confidence: **C** confirmed from code, **H** hypothesis, **?** only the access width is known.

| Off | Field | Type | Meaning | Conf. |
|---|---|---|---|---|
| 0x000 | flags | u32 | global flags (bit list below) | C (bits partly H) |
| 0x004 | req | u32 | scene request bits: 1 change scene, 2 change sub-state, 4 reset graphics lists, 0x10/0x20 "changed last frame", 0x40 return to `ret_scene` (section "Scene system" above) | C |
| 0x008 | scene | u16 | current scene id, index into 0x0029FDA0 | C |
| 0x00A | prev_scene | u16 | scene before the last change | C |
| 0x00C | next_scene | u16 | requested scene (`RequestScene`) | C |
| 0x00E | sub | u16 | current sub-state of the scene | C |
| 0x010 | next_sub | u16 | requested sub-state (`RequestSubState`) | C |
| 0x012 | ret_scene | u16 | scene to return to on `req & 0x40` | C |
| 0x014 | ret_sub | u16 | sub-state to return to; also written 1..11 by `EnterPostMissionMenu` 0x00226B78 before `EnterMenu(0x20)` | C / H |
| 0x016 | unk_16 | u16 | — | ? |
| 0x018 | scene_work[4] | u32[4] | zeroed on every scene change; scene-local scratch | C (use ?) |
| 0x028 | frame_count | u32 | ++ every main-loop frame | C |
| 0x02C | frame_count_unpaused | u32 | ++ every frame unless `flags & 0x100` | C |
| 0x030 | scene_frame | u32 | ++ every `SceneStep`, reset on scene change | C |
| 0x034 | scene_frame_unpaused | u32 | same, but not while `flags & 0x100` | C |
| 0x038 | cur_buf | void* | current half of the double buffer, set every frame by `SwapDoubleBuffer(&ctx->dbuf)` | C |
| 0x040 | dbuf[2] | void*[2] | double buffer (each half allocated with `func_00101060(size)`, e.g. 0x10000 bytes in scene 7) | C |
| 0x048 | dbuf_index | u32 | 0/1, toggled every frame | C |
| 0x050 / 0x054 | gif_list_head / gif_list_tail | void* | DMA packet list sent on **channel 2 (GIF)** with TTE off every frame | C |
| 0x060 / 0x064 | vif1_list_head / vif1_list_tail | void* | DMA packet list sent on **channel 1 (VIF1)** with TTE on every frame; appended by `func_001043C0` | C |
| 0x070 | draw_w | u16 | draw buffer width (0x200 at boot, 0x280 for movies) | C |
| 0x072 | draw_h | u16 | draw buffer height (0x1C0 = 448) | C |
| 0x074 | disp_w | u16 | display width, passed to `sceGsSetDefDispEnv` | C |
| 0x076 | disp_h | u16 | display height (0xE0 = 224 at boot = field mode; 0x1C0 for movies) | C |
| 0x078 | clear_rgba[4] | u8[4] | clear colour R, G, B, A (RGBAQ of the clear sprite at +0x1D0) | C |
| 0x07C | frame_psm | u8 | FRAME PSM | C |
| 0x07D | zbuf_psm | u8 | ZBUF PSM (0x31 = PSMZ24, 0x3A = PSMZ16S) | C |
| 0x07E | ztest | u8 | ZTST value in TEST_1/TEST_2 (bits 17–18) | C |
| 0x07F | draw_mode | u8 | selects ALPHA/prim settings in the clear packet (0..3) | H |
| 0x080 | brightness_r, brightness_g, brightness_b | float ×3 | brightness colour scale: set by `ApplyBrightness` 0x00105988 (option level 0–6), applied by `DrawDisplayTransfer` (H) 0x001049B0 when copying the frame | C |
| 0x08C | disp_flags | u16 | display flags: 0x100/0x200 = `sceGsResetGraph` with field/frame mode on the next flip, 0x2000/0x8000 select the field copy, 0x4000 skips it; mask 0x5FFF cleared after each flip | C (bits H) |
| 0x08E | zbuf_page | u16 | first free VRAM page after the frame buffers (ZBP) | C |
| 0x090 | dispenv | u64[5] | `sceGsDispEnv` (pmode, smode2, dispfb, display, bgcolor); `sceGsPutDispEnv(ctx+0x90)` every frame | C |
| 0x0C0 | drawenv_pkt | u64[28] | GIF packet: GIFtag (A+D, 13 regs) + FRAME_1, ZBUF_1, XYOFFSET_1, SCISSOR_1, TEST_1, PRMODECONT, COLCLAMP, DTHE, FRAME_2, ZBUF_2, XYOFFSET_2, SCISSOR_2, TEST_2; sent every frame by `PutDispAndDrawEnv` | C |
| 0x1A0 | clear_pkt | u64[14] | GIF packet: GIFtag (6 regs) + TEST, PRIM (sprite), RGBAQ, XYZ2, XYZ2, TEST: full-screen clear | C |
| 0x210 | pad_state | 2 × 0x50 bytes | per-port pad state, filled from the raw `scePadRead` buffers (0x004121D8, 100 bytes per port) by `ConvertPadInput` 0x00101858 | C (internals ?) |
| 0x2B0 | pad_buttons | 2 × 4 × u32 | per-port button words (`UpdatePadButtonEdges` 0x00101CF0); +0x2B4 (port 0, word 1) is the most used: bits 0x10 / 0x50 are confirm-type buttons in menus and the startup scene | C (bits H) |
| 0x2D0 | pad_ext | 2 × 0x20 bytes | per-port extra pad data (`UpdateMappedPadButtons` 0x00101E28, with a constant table at 0x0029FFC0 + port·0x50); u16 at +0x2DE / +0x2FE | ? |
| 0x310 | unk_310 | 0x700 bytes | not accessed through `g_pMainContext` in any scanned code | ? |
| 0xA10 | data0_table | void*[4] | pointers into DATA.CDP entry 0 (`buf + buf[i+1]` for `i < buf[0]`), filled once by `LoadBootData` | C (count H) |

#### `flags` bits

| Bit | Meaning |
|---|---|
| 0x100 | pause/freeze (pause menu open): stops `frame_count_unpaused` and `scene_frame_unpaused`; also tested by the flight draw lists (pause-menu overlay) and by `UpdateChatterTimers` 0x00259588 (`../subsystems/mission-flow.md`, `../subsystems/sound.md`) |
| 0x200, 0x400 | main-loop bookkeeping; 0x200 also set by scene 2 sub-state 0x1C; 0x400 set by the pause-menu callback 0x0017D938 |
| 0x800 | set on every scene change and by `main`; cleared in scene 2 sub-states 0x0B/0x1C |
| 0x10000 | 16:9 screen ratio (options page; HUD and cameras, `../subsystems/camera-hud.md`) |
| 0x20000 | hides the whole HUD (`DrawHud` returns early, `../subsystems/camera-hud.md`); toggled by the pause-menu item HUD ON/OFF (callbacks 0x0017DAF0 / 0x0017DB28), which is only offered after the game has been cleared once (`../subsystems/mission-flow.md`) |
| 0x40000 | skip the main loop's pad task `UpdatePads`; set by `LoadFlightDataEntry`, cleared by `EnterFlightSubState` |
| 0x80000 | "HDD present": cleared by the boot scene and never set by game code (`../subsystems/save.md`) |
| 0x100000 | set by the HDD install page (unreachable) |
| 0x200000 | set by scene 6 sub 0 |
| 0x400000 | sound banks loaded (scene 6, `../subsystems/results-startup.md`) |

### Per-frame helpers called from the main loop

| Address | Function | Behaviour |
|---|---|---|
| 0x00104438 | `SendDmaPacketList(chan, list, tte)` | `ch = sceDmaGetChan(chan)`; sets CHCR.TTE (bit 6) if `tte == 1`, clears it otherwise; if `list->head` is non-null, `sceDmaSend(ch, list->head)`; then `head = tail = 0`. Called as `(1, &ctx->vif1_list_head, 1)` and `(2, &ctx->gif_list_head, 0)`: kicks last frame's VIF1 and GIF chains |
| 0x00103E90 | `SwapDoubleBuffer(db)` | `db->index ^= 1; return db->buf[db->index]`; the result goes to `ctx->cur_buf` |
| 0x00104950 | `UpdateDisplayFrame(odd_field)` | unless `disp_flags & 0x4000`, calls `DrawDisplayTransfer` (H) 0x001049B0`(mode, odd_field, &ctx->brightness_r)`: builds a GIF packet at 0x00420000 that copies or blends the draw buffer into the display buffer in 32-pixel strips (interlace field handling) and sends it on DMA channel 2. Then clears flag bits 0x2000/0x8000 and calls `PutDispAndDrawEnv` 0x00104E28 (`sceGsPutDispEnv(ctx+0x90)`, optional `sceGsResetGraph(0,1,2,field/frame)`, DMA channel 2 send of the DrawEnv packet `ctx+0xC0`) |

Per frame: kick last frame's DMA lists → swap the CPU-side buffer → game update (`SceneStep` builds new lists) → vsync → copy/flip and re-send the draw environment.

### Game-state block `g_GameState` (0x0029F350)

The gp global `g_GameState` 0x004104A4 (`gp−0x554C`) points at the byte block 0x0029F350. It is set at boot by `InitGameStateGlobals` (H) 0x001006C0. All later writes go through the pointer, so Ghidra shows only one direct write to each byte. Known fields (`game[n]`):

| Offset | Meaning | Source |
|---|---|---|
| 0x00 / 0x01 | player 1 aircraft id / colour variant (0/1/2) (0x0029F350 / 0x0029F351) | `LoadFlightDataEntry` (section "Scene system" above) |
| 0x03 | per-demo flag | title demo table (`../subsystems/frontend.md`) |
| 0x10 / 0x11 | player 2 aircraft id / variant (0x0029F360 / 0x0029F361) | `LoadFlightDataEntry` sub 4 |
| 0x20 | pointer to the movie request object | movie code (`../subsystems/frontend.md`, scene 3) |
| 0x25 | number of players (1; 2 in VS) | startup, mission select |
| 0x26 | split screen (two views) | `func_0010C9D0` (`../subsystems/camera-hud.md`) |
| 0x27 | **mission id** (not the campaign number; 0x23 intro, 0x24 after mission 18, 0x25 opening before mission 1) | section "Scene system" above |
| 0x28 | difficulty 0–5 | free mission, missile damage, unit variants |
| 0x29 | mode: 1 campaign, 2 free mission, 3 trial, 4 title demo | front end |
| 0x2A | 0 pre-mission cutscene, 1 gameplay (set by `MenuGoTo` kinds 2/3) | section "Scene system" above |
| 0x2B | title demo index 0/1/2 → DATA 173–175 | `StartTitleDemo` 0x0022CC68 |
| 0x2C | scripted sequence / stage index | `../subsystems/mission-flow.md` |
| 0x2D | used by the versus rules and results | `../subsystems/mission-flow.md` |
| 0x2E | VS stage variant | front end |

The campaign mission number the menus use is the separate u16 `g_MissionNo` 0x002A15B8 (1..18, section "Scene system" above).
