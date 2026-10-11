# Boot path: ELF entry → main → main loop

All addresses are EE virtual addresses in SLUS_201.52. `(H)` = hypothesis name (Ghidra plate comment, not renamed). Everything else is renamed in Ghidra and listed in `docs/symbols.csv`.

## 1. crt0 — `entry` @ 0x00100008 (Sony SDK 2.3 crt0)

| Address | Action |
|---|---|
| 0x00100008–0x00100030 | Zero `.sbss`+`.bss` 0x00411780–0x004CC74B with `sq zero` (16 bytes per iteration) |
| 0x00100034–0x00100064 | `gp = 0x004159F0`. Syscall 0x3C **SetupThread**(gp, stack = -1 (top of RAM), stack_size = 0x100000, args = 0x004C55C0, root = `_root` 0x001000C0). Returns the new `sp` |
| 0x0010006C–0x00100080 | Syscall 0x3D **SetupHeap**(0x004CC74C = end of `.bss`, -1 = up to the stack) |
| 0x00100084 | `_InitSys` (H) @ 0x0026B2D0: libkernl runtime init (kernel patches, helper thread + semaphore) |
| 0x0010008C | `FlushCache(0)` |
| 0x00100094 | `ei` |
| 0x001000A4 | **`main`(argc = *(0x004C55C0), argv = 0x004C55C4)** |
| 0x001000AC | `Exit(main's return value)` (syscall 4) |

Also in crt0: `_exit` (H) @ 0x001000B8 = `j Exit` with a0 = 0; `_root` @ 0x001000C0 = ExitThread stub (syscall 0x23), the main thread's root function.

## 2. `main` @ 0x001001F0: initialisation

In call order (main's own addresses). Corrected 2026-10-10 (`skeleton-readiness.md` §2): the earlier roles of 0x00100620 and 0x00100768 were wrong.

| Site | Callee | Role |
|---|---|---|
| 0x001001F8 | `__main` 0x00288950 | GCC run-once global constructor call (`__do_global_ctors` (H) 0x002888A0) |
| 0x00100200 | `InitMainContext` (H) 0x00100650 | Main context struct at 0x0029F380; pointer stored in the gp global 0x004104A8 (`gp-0x5548`) |
| 0x00100208 | `IopBoot` 0x00100400 | `sceSifInitRpc`, `sceCdInit(0)`, `sceCdMmode` by disc type (0x12 CD / 0x14 DVD), `sceSifRebootIop("cdrom0:\IOPRP23.IMG;1")`, `sceSifSyncIop`, re-init SIF/fileio/iopheap/cdvd, then `sceSifLoadModule` in this order: SIO2MAN, MCMAN, MCSERV, PADMAN, LIBSD, SOUND, VAG, USBD, USBJOY |
| 0x00100210 | `InitHardware` 0x00100620 | `sceGsResetPath`, `sceDmaReset(1)`, `sceVu0ResetVif0` (H), `sceCdInit(0)` |
| 0x00100218 | `InitWorkHeap` 0x00100F48 | One big `malloc` block, size probed by `ProbeLargestMallocSize` 0x00100EC8. All game allocations go through `WorkHeapAlloc` 0x00101000 |
| 0x00100220 | `InitGameStateGlobals` (H) 0x001006C0 | fills the `g_GameState` block 0x0029F350 (field meaning open) |
| 0x00100228 | `InitOptionData` (H) 0x001053F8 | option/save-data defaults at 0x002A0318 |
| 0x00100230 | `InitRandomTable` 0x00100D38 | `srand(1)`, `srand(123456789)`, 512 random floats at 0x00411998 (read by 0x00100DC8) |
| 0x00100238 | `InitSubsystems` 0x001000D8 | `InitCdFileSystem`, `BuildCdLsnTable`, `InitSaveSystem`, `sceSifInitRpc`, `InitSoundRpc` loop, `InitVagStream` loop, audio/movie inits |
| 0x00100240 | `ApplyOptionSettings` (H) 0x00105340 | sound output mode/volumes, display offset, screen scale, vibration |
| 0x00100248 | — | `ctx->flags |= 0x800` |
| 0x00100254 | `InitGraphics` 0x001044A8 | `sceGsResetGraph(0,1,2,1)`, 512×448 draw / 512×224 display, `BuildDrawEnvPackets` |
| 0x0010025C | `LoadBootData` 0x00100768 | loads `DATA.TBL`, DATA entry 0 (→ `ctx->data0_table`), sound-bank LSN table, DATA 0x166; blocking waits call `UpdateCdQueue` + `sceGsSyncV(0)` |
| 0x00100264 | `PadSystemInit` 0x00102050 | `BindUsbJoyRpc`, `scePadInit`, `scePadPortOpen` ×2 |
| 0x0010026C | `InitSprRingBuffer` 0x00103C00 | DMAC `D_RBOR`=0x418000, `D_RBSR`=0x7FF0; scratchpad double buffer |
| 0x00100274 | `SetWorkHeapMark` 0x001010A8 | heap mark = current offset (scene changes reset to it) |
| 0x00100280–0x00100288 | `sceGsSyncV(0)` | loop until it returns non-zero (field parity) |

## 3. Main loop: 0x00100290–0x001003BC inside `main`

There is no separate main-loop function. The loop body is in `main` and closes with `beq s0,zero,0x00100290`.

```
loop:                                         ; 0x00100290
    func_00104438(1, ctx+0x60, 1)             ; DMA/packet buffer kick (sceDmaSend) - role unconfirmed
    func_00104438(2, ctx+0x50, 0)
    ctx[0x38] = func_00103E90(ctx+0x40)
    if (!(ctx->flags & 0x40000)) func_00102220()   ; optional per-frame task (uses pad/semaphores)
    FlushCache(0)
    SoundRpcUpdate()        (H) 0x00218F18    ; IOP RPC 0x81000101
    StreamAudioUpdate()     (H) 0x0021FE60    ; IOP RPC 0x81000102
    s0 = SceneStep()        (H) 0x00100B18    ; game logic: scene_table[ctx->scene]()
    f  = WaitVSync()        (H) 0x00104920    ; sceGsSyncPath + sceGsSyncV
    RCNT0_COUNT (0x10000000) = 0             ; reset EE timer 0 each frame
    func_00104950(f)                          ; display flip / sceGsPutDispEnv + DMA kick
    ctx[0x28]++ ; if !(flags & 0x100) ctx[0x2C]++   ; frame counters
    flag bookkeeping on bits 0x100/0x200/0x400/0x800
    CdQueueUpdate()         (H) 0x00214350
    if (s0 == 0) goto loop
exit (never reached in practice: SceneStep always returns 0):
    func_001021E8()  ; scePadEnd + RPC shutdown
    func_00100FB0()
    sceCdInit(5)     ; SCECdEXIT
    printf(0x00403700)
    return 0
```

## 4. Scene table @ 0x0029FDA0

`SceneStep` calls `scene_table[*(u16*)(ctx+8)]()`. A scene change is requested by setting `ctx+4` bit 0 with the next scene id in `ctx+0xC`. The table has 8 entries; the word after them (0xFF01FF01) is not a code pointer.

| Id | Handler | Notes |
|---|---|---|
| 0 | 0x001008B0 | |
| 1 | 0x00211C58 | |
| 2 | 0x0010A030 | |
| 3 | 0x00211CA8 | |
| 4 | 0x002262D0 | |
| 5 | 0x00246E18 | |
| 6 | 0x002613F0 | in the 0x00259000–0x002616E7 game tail (save/HDD area) |
| 7 | 0x00261680 | same |

All handlers reach almost nothing through direct `jal`, so they dispatch further through tables. Roles (Phase 2): see `scenes.md`. 0 boot loader, 1 menu entry, 2 in-engine flight scene (missions + first-boot intro), 3 movie player, 4 front-end incl. story movies, 5 results, 6 startup (logos, card/HDD check), 7 HDD error (H). Roles 2, 4, 5, 6 confirmed in PCSX2 (`pcsx2-sessions-01.md`).

## 5. Facts for the port

- Single main thread for all game logic. The libkernl helper thread is from `_InitSys`; the only game-owned threads are the save/HDD module's job worker and power-off handler (`threads-timing.md`).
- The frame is paced by `sceGsSyncV` (busy-poll of INTC VBLANK). EE timer 0 is reset every frame but read only by movie code; game timing is frame-count based with a fixed 1/60 s step (`threads-timing.md`).
- All game memory comes from one `malloc` block and a bump allocator. There is no `new`/`delete`.
- The main context struct at 0x0029F380 (gp global 0x004104A8) holds the frame counters (+0x28, +0x2C) and global flags (+0x0).
