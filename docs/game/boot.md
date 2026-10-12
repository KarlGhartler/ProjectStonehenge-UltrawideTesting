# Boot path and main loop

From the ELF entry point to the per-frame main loop. All addresses are EE virtual addresses in `SLUS_201.52`. `(H)` marks a hypothesis name (Ghidra plate comment, function not renamed). All other names are Ghidra names listed in `docs/symbols.csv`.

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

## `main` 0x001001F0: initialisation

In call order:

| Site | Callee | Role |
|---|---|---|
| 0x001001F8 | `__main` 0x00288950 | GCC run-once global constructor call (`__do_global_ctors` (H) 0x002888A0). Runs the `.ctors` list 0x0029F308 (count 10), see below |
| 0x00100200 | `InitMainContext` 0x00100650 | main context struct at 0x0029F380; pointer in the gp global 0x004104A8 (`gp−0x5548`) (`main-context.md`) |
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

`SceneStep` calls `scene_table[ctx->scene]()`. The 8 handlers and the request mechanism are in `scenes.md`. The word after the table (0xFF01FF01) is not a code pointer.

## Facts for the port

- One main thread runs all game logic. The libkernl helper thread comes from `_InitSys`. The only game-owned threads (save/HDD job worker, power-off handler) are on the unreachable HDD path (`threads-timing.md`, `../port/dead-code.md`).
- The frame is paced by `sceGsSyncV` (busy-poll of INTC VBLANK). EE timer 0 is reset every frame and read only by movie code. Game timing counts frames with a fixed 1/60 s step (`threads-timing.md`).
- All game memory comes from one `malloc` block and a bump allocator; there is no `new`/`delete` (`memory.md`).
- The main context at 0x0029F380 holds the frame counters and global flags (`main-context.md`).
