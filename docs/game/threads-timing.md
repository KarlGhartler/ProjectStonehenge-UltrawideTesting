# Threads, interrupts and timing

Sources: xrefs to every kernel stub (`CreateThread`, `StartThread`, `AddIntcHandler(2)`, `AddDmacHandler(2)`, `SetAlarm`, semaphores; `../platform/sdk-functions.md`), the main loop (`boot.md`), offline scans (`tools/research/hwregs.py`), and PCSX2 sessions 5 and 6 (`../pcsx2/sessions-02.md`).

## Summary

- **All game logic runs on the main thread inside the main loop.** No game logic runs in an interrupt handler or a second thread.
- The game installs **no interrupt handlers of its own**: `AddIntcHandler`/`AddIntcHandler2` have no callers, and `AddDmacHandler` is called only by SDK code.
- Frame pacing is a **busy-wait on vblank** (`sceGsSyncV` → `PollVblankStart`), once per main-loop iteration.
- Game logic is **frame-locked**: it counts frames and uses the constant 1/60 s (160 copies in `.lit4`). There is no variable timestep. Only movie playback reads a hardware timer.
- Video mode: **NTSC, interlaced** (`sceGsResetGraph(0, 1, 2, …)`: interlace on, mode 2 = NTSC). Menus and gameplay use field mode (display height 224); movies use frame mode (448).
- **Measured (PCSX2 session 5):** the game runs at a full 59.94 Hz in all gameplay (heavy and light scenes) and in in-engine cutscenes (60 frame-counter increments per 60 frame advances). SMV videos run at 29.97 fps: each MPEG picture is held for 2 vblanks while the main loop keeps running at 59.94 Hz.
- Extra threads: the SDK helper thread, libmc alarms, and two threads of the HDD module that are never created in this build (below).

## Threads

| Creator | Entry | Priority | Stack | What it does | Created |
|---|---|---|---|---|---|
| main (crt0) | `main` 0x001001F0 | 1 after `_InitSys`; set to 2 by the HDD module init | crt0 | everything: main loop, game logic, rendering, file I/O requests, RPC | boot |
| `_InitSys` (H) 0x0026B2D0 (libkernl) | 0x0026B2F8 | `ChangeThreadPriority(self, 1)` raises main to 1 | 0x400 bytes at 0x004C8BB0 | SDK helper thread (waits on its semaphore, woken via `_iWakeupThread` 0x0026B4A8) | crt0, before `main` |
| `func_0025BFC0` (HDD module) | `func_0025BF70` | 1 (main set to 2) | 0x2000 bytes at work+0x2000 | HDD job worker: `WaitSema`, run job `table_0x003F6A78[cmd]()` (39 jobs, `../subsystems/save.md`), `FlushCache`, repeat. work+0x4008 = 1 idle / 2 busy | **never**: created by `func_0025C090` ← `func_0025E218` ← `InitHddModule` (H) 0x00259938 ← `func_00259910`, and `func_00259910` has no caller |
| `func_0025C198` (HDD module) | `func_0025C108` | 1 | 0x2000 bytes | power-off handler: `WaitSema` (signalled by the CD power-off callback 0x0025C178, registered with `sceCdPOffCallback` (H) 0x0026EFB8), then `sceDevctl("pfs:", 0x5003)` (H: close all), `sceDevctl("hdd:", 0x4806)` (H), `sceCdPowerOff` (H) 0x00270730 | **never** (same chain) |

PCSX2 session 6 confirms: with a memory card and no HDD, the **only** `CreateThread` call is the SDK helper at boot (`ra` 0x0026B444 = call at 0x0026B43C + 8), and no thread is created during memory-card saves or loads. The game runs on the main thread plus the SDK helper only.

Memory-card operations are asynchronous step machines polled once per frame (`sceMcSync`, `../subsystems/save.md`). libmc itself uses `SetAlarm` + `SleepThread` internally (0x0026DCE0, 0x0026EB08).

## Interrupt and DMA handlers

| Registration | Handler | Owner | Purpose |
|---|---|---|---|
| `AddDmacHandler(5, 0x0026BDA8)` in 0x0026B8B0 (`sceSifInitRpc`) | 0x0026BDA8 | libkernl (SIF) | SIF0 receive: SIF command / RPC completion, wakes RPC waiters (standard SDK) |
| `AddDmacHandler(3, 0x00276E80)` in 0x00276FC8; `AddDmacHandler(4, 0x00277188)` in 0x00277260 | 0x00276E80, 0x00277188 | libmpeg/libipu | IPU DMA, installed only around a decode call and removed again (`RemoveDmacHandler`); the decode caller busy-waits for them |
| `RemoveIntcHandler` in `sceGsResetGraph` | — | libgraph | removes a possible old vsync handler; no game code adds one |
| `_EnableIntc` / `_EnableDmac` | — | libkernl helpers 0x002648B0 / 0x00264980 | used by the above |

No `AddIntcHandler` and no `SetVSyncFlag` handler in game code. `SetVSyncFlag` is used only inside libgraph's alternative `sceGsSyncV` path 0x00264560.

## Code that runs outside the main loop's control flow

1. SDK interrupt/DMA handlers (SIF RPC completion, IPU DMA during movie decode).
2. The libkernl helper thread.
3. Kernel callbacks: the `sceCdPOffCallback` handler 0x0025C178 (`iSignalSema` only), registered only by the unreachable HDD module.

Everything else is called from the main loop in a fixed order (`boot.md`): pad reading (`UpdatePads` 0x00102220 → `scePadRead`, once per frame), sound and stream RPC updates, CD queue servicing (`UpdateCdQueue`) and all scene logic.

## Frame pacing

Per main-loop iteration: kick last frame's DMA lists → swap packet buffer → pad task → sound RPC update → stream RPC update → `SceneStep` → `WaitVSync` 0x00104920 (= `sceGsSyncPath(0,0)` + `sceGsSyncV(0)`) → `T0_COUNT = 0` → `UpdateDisplayFrame(field)` → frame counters ++ → `UpdateCdQueue`.

- `sceGsSyncV` 0x00261C20 → `PollVblankStart` 0x00264520: writes `INTC_STAT = 4`, then busy-polls bit 2 (VBLANK start). No thread sleep: the EE spins until the next vblank. The return value is GS `CSR.FIELD` (bit 13) when interlaced, else 1.
- One `SceneStep` per vblank if the frame's work fits in one field (16.68 ms at 59.94 Hz). If it does not fit, the loop waits for the following vblank and that frame takes 33 ms with **no compensation**.
- Loading code blocks in its own loops on `UpdateCdQueue` + `sceGsSyncV(0)` (e.g. `LoadBootData`) and does not run `SceneStep`.

## Frame count vs. time

| Evidence | Meaning |
|---|---|
| `frame_count` read by 28 functions, `frame_count_unpaused` by 9, `scene_frame` by 1 | per-frame counters drive timing |
| Float 1/60 (0x3C888889) appears 160 times in `.lit4`, 1/30 15 times; e.g. the title fade adds `DAT_004101B8` = 1/60 each frame (`MissionTitleUpdate` 0x00230308) | fixed timestep of one NTSC field |
| Fades and holds in frames: startup splash 60/120/60 (0x00260980), CD timeout 3600 frames (`ProcessCdRequest`) | frame counts |
| Mission timers flight object +0x1654/+0x1658/+0x1660 += 1 per `SceneStep` (scene 2 handler) | mission time counts frames |
| `T0_COUNT` read only by movie code (`SceneMovie` (H) 0x00211CA8, `UpdateMenuMovie` 0x00238810) with `T0_MODE` = 0x83 (counts H-blanks), reset every main-loop frame | movies pace decoding by H-blank count; gameplay never reads a timer |
| Parity tests on the frame counter (`& 1`) only in 3 menu functions (0x0022B1E0, 0x00234190, 0x0023BE88) | no general "every other frame" scheme in game logic |

The game is designed for **60 updates per second** (one per NTSC field), and gameplay speed depends on the frame rate. A port must run the simulation at a fixed 59.94 (or 60) Hz step, independent of the render rate (ADR-002). There is no 30 Hz mode in gameplay (session 5).

## Timing requirement for movies

Movies are 29.97 fps MPEG-2 shown on a 59.94 Hz loop: one decoded picture per 2 vblanks. Scene 3 paces decoding with `T0` (H-blank counter, reset every frame): sub 2 waits until `T0 ≥ 300`. The in-menu player waits in `MovieFrameSync` 0x00238870 until `T0` reaches 0x118, which makes each iteration two fields; `frame_count` advances by 2 per iteration (measured, `../pcsx2/sessions-04.md`; `../subsystems/frontend.md`). A port must present each movie picture for exactly 2 game ticks and keep the PCM audio (48 kHz) in sync with that, not use wall-clock video timing.
