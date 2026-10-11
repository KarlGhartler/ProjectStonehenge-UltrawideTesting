# Threads, interrupts and timing

Phase 2.5. Sources: xrefs to every kernel stub in `sdk-functions.md` (`CreateThread`, `StartThread`, `AddIntcHandler(2)`, `AddDmacHandler(2)`, `SetAlarm`, semaphores), the main loop (`boot.md`), and offline scans (`out/scratch/hwscan2.py`, `frameparity.py`).

## Summary

- **All game logic runs on the main thread inside the main loop.** No game logic runs in an interrupt handler or a second thread.
- The game installs **no interrupt handlers of its own**: `AddIntcHandler`/`AddIntcHandler2` have no callers anywhere, and `AddDmacHandler` is called only by SDK code.
- Frame pacing is a **busy-wait on vblank** (`sceGsSyncV` → `PollVblankStart`), once per main-loop iteration.
- Game logic is **frame-locked**: it counts frames and uses the constant 1/60 s (160 copies in `.lit4`). There is no variable timestep, and only movie playback reads a hardware timer.
- Video mode: **NTSC, interlaced** (`sceGsResetGraph(0, 1, 2, …)`: interlace on, mode 2 = NTSC). Menus and gameplay use field mode (display height 224); movies use frame mode (448). **Measured (PCSX2 session 5, `pcsx2-sessions-02.md`): the game runs at a full 59.94 Hz in all gameplay (heavy and light scenes) and in in-engine cutscenes** (60 frame-counter increments per 60 frame advances). Only SMV videos run at 29.97 fps: each MPEG frame is held for 2 vblanks while the main loop keeps running at 59.94 Hz.
- Extra threads exist only in the SDK (libkernl helper, libmc alarms) and in the **save/HDD module** (an async job worker and a power-off handler). The save/HDD threads are only created on the HDD path: in normal play (memory card, no HDD) the only `CreateThread` call is the SDK helper (PCSX2 session 6).

## Threads

| Creator | Entry | Priority | Stack | What it does | Started when |
|---|---|---|---|---|---|
| main (crt0) | `main` 0x001001F0 | 1 after `_InitSys`; set to 2 by the save module | crt0 | everything: the main loop, game logic, rendering, file I/O requests, RPC | boot |
| `_InitSys` (H) 0x0026B2D0 (libkernl) | 0x0026B2F8 | (`ChangeThreadPriority(self, 1)` raises main to 1) | 0x400 bytes at 0x004C8BB0 | SDK helper thread (waits on its semaphore, woken via `_iWakeupThread` 0x0026B4A8) | crt0, before `main` |
| `FUN_0025BFC0` (save/HDD module, **not created in normal play**) | `FUN_0025BF70` | 1 (main is set to 2) | 0x2000 bytes at `work+0x2000` | **save/HDD job worker**: `WaitSema`, run job `table_0x003F6A78[cmd]()` (39 job types), `FlushCache`, repeat. `work+0x4008` = 1 idle / 2 busy | when the HDD module is started: `FUN_0025C090` ← `FUN_0025E218` ← `FUN_00259938` ← `FUN_00259910`, reached only through pointer tables at 0x003F6450–0x003F7680 (H: on demand, when the HDD is used) |
| `FUN_0025C198` (save/HDD module, **not created in normal play**) | `FUN_0025C108` | 1 | 0x2000 bytes | **power-off handler**: `WaitSema` (signalled by the CD power-off callback 0x0025C178, registered with `sceCdPOffCallback` (H) 0x0026EFB8), then `sceDevctl("pfs:", 0x5003)` (close all, H), `sceDevctl("hdd:", 0x4806)` (H), `sceCdPowerOff` (H) 0x00270730 | same as above |

The save module's jobs run in parallel with the main loop. The main loop polls `work+0x4008` (idle/busy) instead of blocking (H, from the state flag). libmc uses `SetAlarm` + `SleepThread` internally (0x0026DCE0, 0x0026EB08). Whether the game polls libmc asynchronously or blocks is not checked yet.

## Interrupt and DMA handlers

| Registration | Handler | Owner | Purpose |
|---|---|---|---|
| `AddDmacHandler(5, 0x0026BDA8)` in 0x0026B8B0 (`sceSifInitRpc`) | 0x0026BDA8 | libkernl (SIF) | SIF0 receive: SIF command / RPC completion. Wakes RPC waiters. Standard SDK |
| `AddDmacHandler(3, 0x00276E80)` in 0x00276FC8; `AddDmacHandler(4, 0x00277188)` in 0x00277260 | 0x00276E80, 0x00277188 | libmpeg/libipu | IPU DMA, installed only around a decode call and removed again (`RemoveDmacHandler`). The decode caller busy-waits for them |
| `RemoveIntcHandler` in `sceGsResetGraph` | — | libgraph | removes a possible old vsync handler. **No game code adds one** |
| `_EnableIntc`/`_EnableDmac` | — | libkernl helpers 0x002648B0 / 0x00264980 | used by the above |

No `AddIntcHandler`, no `SetVSyncFlag` handler in game code. `SetVSyncFlag` is only used inside libgraph's alternative `sceGsSyncV` path 0x00264560.

## Code that runs outside the main loop's control flow

1. SDK interrupt/DMA handlers (SIF RPC completion, IPU DMA during movie decode).
2. The libkernl helper thread.
3. The save/HDD worker and power-off threads (game code, save module only).
4. Kernel callbacks: `sceCdPOffCallback` handler 0x0025C178 (`iSignalSema` only).

Everything else, including pad reading (`func_00102220` → `scePadRead`, once per frame), sound and stream RPC updates, CD queue servicing (`UpdateCdQueue`) and all scene logic, is called from the main loop in a fixed order (`boot.md` §3).

## Frame pacing

Main loop order (per iteration): kick last frame's DMA lists → swap packet buffer → pad task → sound RPC update → stream RPC update → `SceneStep` → `WaitVSync` (H) 0x00104920 = `sceGsSyncPath(0,0)` + `sceGsSyncV(0)` → `T0_COUNT = 0` → `UpdateDisplayFrame(field)` → frame counters ++ → `UpdateCdQueue`.

- `sceGsSyncV` 0x00261C20 → `PollVblankStart` 0x00264520: writes `INTC_STAT = 4`, then busy-polls bit 2 (VBLANK start). There is no thread sleep, so the EE spins until the next vblank.
- One `SceneStep` per vblank if the frame's work fits in one field (16.68 ms at 59.94 Hz). If it does not fit, the loop simply waits for the following vblank, and that frame takes 33 ms of real time with **no compensation**.
- Loading code blocks with its own loops on `UpdateCdQueue` + `sceGsSyncV(0)` (e.g. `func_00100768`) and does not run `SceneStep`.

## Frame count vs. time

| Evidence | Meaning |
|---|---|
| `ctx+0x28` (frame count) read by 28 functions, `ctx+0x2C` (unpaused) by 9, `ctx+0x30` (scene frame) by 1 | per-frame counters drive timing |
| Float 1/60 (0x3C888889) appears **160 times** in `.lit4`, 1/30 15 times; e.g. the title fade adds `DAT_004101B8` = 1/60 each frame (`MissionTitleUpdate` 0x00230308) | fixed timestep of one NTSC field |
| Fades and holds are in frames: startup splash 60/120/60 (0x00260980), CD timeout 3600 frames (`ProcessCdRequest`) | frame counts |
| Mission timers `flight+0x1654/0x1658/0x1660` += 1 per `SceneStep` (scene 2 handler) | mission time counts frames |
| `T0_COUNT` read only by movie code (`0x00211CA8`, `0x00238810`) with `T0_MODE` = 0x83 (counts H-blanks), reset every main-loop frame | movies pace decode by H-blank count; gameplay never reads a timer |
| Parity tests on the frame counter (`& 1`) in only 3 menu functions (0x0022B1E0, 0x00234190, 0x0023BE88) | no general "every other frame" scheme found in game logic (the flight code may use its own counters, not checked) |

Conclusion: the game is designed for **60 updates per second** (one per NTSC field), and gameplay speed depends on the frame rate. A port must run the simulation at a fixed 59.94 (or 60) Hz step, independent of render rate.

Thread creation measured (PCSX2 session 6): with a memory card and no HDD, the **only** `CreateThread` call is the SDK helper at boot (`ra` 0x0026B444 = call at 0x0026B43C + 8). No thread was created during memory-card saves or loads, so in the supported configuration **the game runs on the main thread plus the SDK helper only**. The two save/HDD threads belong to the HDD path (started through the HDD module's tables) and are irrelevant for a port without HDD support.

## Resolved

- 30 Hz question: closed, no 30 Hz mode in gameplay (session 5).
- Game threads: not created without an HDD (session 6).

## Timing requirement for the SMV player

Movies are 29.97 fps MPEG-2 shown on a 59.94 Hz loop: one decoded picture per 2 vblanks. The movie scene paces decode with `T0` (H-blank counter, reset every main-loop frame); scene 3 sub 2 waits until `T0 ≥ 300`. A port must present each movie frame for exactly 2 game ticks and keep the PCM audio (48 kHz) in sync with that, instead of using wall-clock video timing.
