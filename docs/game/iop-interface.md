# EE ↔ IOP interface (game-owned IRX modules)

What the EE sends to and receives from the game's own IOP modules. The IOP-side handlers (command semantics, voice record layout, stream channels) are in `../subsystems/sound.md`; the EE sound API built on top is in `../subsystems/sound.md`.

## Modules and RPC server ids

| Server id | Module | Evidence | EE client (bind) |
|---|---|---|---|
| `0x81000101` | **SOUND.IRX** | `lui 0x8100 / ori 0x0101` in SOUND.IRX code at file offset 0xBFC | `BindSoundRpc` 0x002172C8 (bind + fno 0) |
| `0x81000102` | **VAG.IRX** | id built in VAG.IRX code at 0x14C | `InitVagStream` 0x0021E370 (bind + fno 0) |
| `0x80000210` | **USBJOY.IRX** | id built in USBJOY.IRX code at 0xE94 | `BindUsbJoyRpc` 0x001096D8 (bind only, called from `PadSystemInit` 0x00102050) |

Aligned-word matches of `0x80000210` inside MCMAN/SOUND/VAG data are coincidences (odd offsets, data not code). The Sony modules (SIO2MAN, MCMAN, MCSERV, PADMAN, LIBSD, USBD) are reached only through the SDK libraries (`../platform/sdk-functions.md`).

All binds use `while (sceSifBindRpc(cd, id, 0), cd->serve == 0) delay(0x10000);`. All game RPC calls use **mode 0 (blocking)**, but the game checks completion with `sceSifCheckStatRpc` before the next call, so at most one call per server is in flight. The game has **7 RPC call sites and 2 `sceSifSetDma` sites** in total.

## SOUND.IRX (`0x81000101`): command batches, once per frame

Client state: struct at `*0x004117FC` (set in `BindSoundRpc`):

| Off | Field |
|---|---|
| +0x00 | command count in the current batch |
| +0x04 / +0x08 | send buffers A/B (`buf`, `buf + 0x2400`), double-buffered |
| +0x0C / +0x10 | receive buffers A/B (`rbuf`, `rbuf + 0x600`) |
| +0x14 | write offset in the current send buffer (starts at 4) |
| +0x18, +0x24 | init parameters (+0x24 copied into send word 1 for fno 0) |
| +0x1C / +0x20 | current / previous buffer index (0/1) |
| +0x34 | `sceSifClientData` |

| fno | Caller | Send | Receive | When |
|---|---|---|---|---|
| 0 | `BindSoundRpc` (from the sound init loop `InitSoundRpc` 0x00219030 in `InitSubsystems`) | 0x2400 bytes: word 0 = 0 commands, word 1 = init parameter | 0x600 bytes | once at boot |
| 1 | `SendSoundCommands` 0x00217428 (from `UpdateSoundRpc` 0x00218F18, main loop, every frame) | 0x2400 bytes: word 0 = command count, then the command records | 0x600 bytes: reply list | every frame, only when the previous call has completed |

The command records (ids 1–0xE, sizes, EE writers, IOP handlers) are listed in `../subsystems/sound.md`. Command 0xA returns a **ticket**; completion is reported in the reply (type 10) and polled by `func_00217858`.

**Reply list** (read by `UpdateSoundRpc`): word 0 = count, then 3-word records `{type, a, b}`. Type 6 → `func_00218EE8(a)` (reverb work-area address), type 10 → transfer done (ticket `b`, `func_002177F8` / `func_00217838`), type 0xF → voice status block: 48 status bytes copied into 0x00424FD0 (`UpdateSoundVoiceStatus` 0x00217A88). Types 1–4 and 7–9 are skipped.

## VAG.IRX (`0x81000102`): 6 EE stream slots on 4 IOP channels

Client state around 0x00353BF8–0x00353FC0: per-slot structs 6 × 0x94 bytes at 0x00353BF8; send buffers A/B at 0x00353F78 / 0x00353F7C (0x100 bytes each); receive buffers at 0x00353F80 / 0x00353F84.

| fno | Caller | Send (0x100 bytes) | Receive (0x100 bytes) | When |
|---|---|---|---|---|
| 0 | `InitVagStream` (stream init `func_0021E2D8`) | 4 words: first SPU voice per IOP channel | IOP base of the stream area at +0; the EE derives the ring addresses (slots 0–1: 3 × 0x20000 bytes each, slots 2–5: 3 × 0x10000 each, from base + 0x180), stored at 0x00353BF8 + slot × 0x94 | once |
| 1 | `SendVagCommands` 0x0021E8F0 (from `UpdateVagStream` 0x0021FE60, main loop, every frame, after `func_0021F448` builds the slot commands) | word 0 = command count (0x00353F70), word 1 = slot-ready bitmask (0x00353F90; bit n = slot n's disc data has arrived), then 20-byte commands | per-slot status (play position at +0x24 + slot·4, flags at +0x14 + slot·4) | every frame, when the previous call has completed |

The IOP maps EE slots 0–3 onto channels 0–3, slot 4 onto channel 2 and slot 5 onto channel 3 (`../subsystems/sound.md`). Channels 0/1 are stereo 44.1 kHz, channels 2/3 mono 22.05 kHz.

**Data path:** VAG.IRX never reads the disc. The EE queues `QueueCdRead(queue 0, lsn, flags = 1, dest = IOP ring address, …)` (`func_0021EAB8`, `func_0021F448`), which runs `sceCdReadIOPm` (H) and reads VPK data **straight into IOP RAM**, then marks the slot ready in the bitmask. Slot requests come from a 32-entry EE-side queue (0x0042CEBC): `RequestStreamPlay` (H) 0x0021FD40`(slot, track)` (10 callers), `func_0021FC80(slot, a, b)` (3 callers), stop/fade commands (types 3/4). `func_0021F190(vol, slot, l, r, fade)` (27 callers) sets per-slot volume and pan for the next update.

**SMV movie audio** does not use VAG.IRX: the movie player borrows the IOP address `*0x00353BF8` (VAG channel 0's first ring segment) as a 2 × 0x4000-byte PCM double buffer, DMAs 0x4000-byte blocks into it with `sceSifSetDma` (`func_00213D10`), and plays it through **SOUND.IRX** command 0xA with `dst = 0` (core-0 sound-data input), stopped with 0xA `{0,0,0}` and faded with command 0xD (`../subsystems/sound.md`).

## USBJOY.IRX (`0x80000210`): USB joystick polling

| fno | Caller | Send | Receive | When |
|---|---|---|---|---|
| 2 | `ReadUsbJoy` 0x00109780 (from the pad task `UpdatePads` 0x00102220, every frame) | 0x80 bytes at 0x00420440 | 0x80 bytes at 0x004204C0: two devices × buttons, axes, hat (bytes +0x04…+0x14 and +0x48…+0x54) | every frame, **synchronous** (`sceSifCheckStatRpc` busy-wait loop right after the call) |

The reply is converted into the game's pad format (two ports), so USB joysticks look like extra DualShock data.

## Direct EE → IOP DMA (`sceSifSetDma`)

| Caller | Source | Destination | Use |
|---|---|---|---|
| `func_00213D10` (SMV reader `SmvReaderStart` (H) 0x00212788, `SmvReaderStream` (H) 0x00212960, 0x00212C78, 0x00212DF0) | 0x4000-byte PCM block in the chunk buffer | IOP ring `*0x00353BF8` (+0x4000 for the second half) | movie audio |
| `func_00215070` (HDD read path, state 4; unreachable) | EE buffer | IOP 0x07800000 | HDD-installed data destined for the IOP (flag 1 requests, e.g. BD banks) |

## What the port needs

- No IOP emulation. The audio backend implements the three server interfaces on the PC side (ADR-004):
  - SOUND command batch → sound-effect mixer with SPU-ADPCM voices, plus the PCM input for movies;
  - VAG slots → streamed ADPCM from VPK;
  - USBJOY → joystick input.
- "IOP RAM" becomes a buffer pool owned by the audio backend. `sceCdReadIOPm` writes into it, and command 0xA copies from it into the "SPU RAM" sample store.
- The per-frame call order (sound update, then stream update, then `SceneStep`) and the one-call-in-flight rule must be kept.

## Open

- VAG fno 1 per-slot command fields beyond the documented ones (`../subsystems/sound.md`).
