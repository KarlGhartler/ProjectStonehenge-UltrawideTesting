# EE ↔ IOP interface (game-owned IRX modules)

Phase 2.5. **Interface only**: what the EE sends to and receives from the game's own IOP modules. No IOP code was analysed. The only IRX inspection was a byte search for the RPC server ids.

## Modules and RPC server ids

| Server id | Module | Evidence | EE client (bind) |
|---|---|---|---|
| `0x81000101` | **SOUND.IRX** | `lui 0x8100 / ori 0x0101` in SOUND.IRX code at file offset 0xBFC | `func_002172C8` (bind + fno 0) |
| `0x81000102` | **VAG.IRX** | id built in VAG.IRX code at 0x14C | `func_0021E370` (bind + fno 0) |
| `0x80000210` | **USBJOY.IRX** | id built in USBJOY.IRX code at 0xE94 | `func_001096D8` (bind only, called from `PadSystemInit` (H) 0x00102050) |

The aligned-word matches of `0x80000210` inside MCMAN/SOUND/VAG data are coincidences: odd offsets, data not code. Sony modules (SIO2MAN, MCMAN, MCSERV, PADMAN, LIBSD, USBD) are reached only through the SDK libraries (`sdk-functions.md`).

All binds use the standard pattern `while (sceSifBindRpc(cd, id, 0), cd->serve == 0) delay(0x10000);`. All game RPC calls use **mode 0 (blocking)**, but the game checks completion with `sceSifCheckStatRpc` before the next call, so at most one call per server is in flight.

The game has only **7 RPC call sites and 2 `sceSifSetDma` sites** in total (`out/scratch/callers.py`).

## SOUND.IRX (`0x81000101`): command batches, once per frame

Client state: struct at `*0x004117FC` (set in `func_002172C8`):

| Off | Field |
|---|---|
| +0x00 | command count in the current batch |
| +0x04/+0x08 | send buffers A/B (`buf`, `buf + 0x2400`), double-buffered |
| +0x0C/+0x10 | receive buffers A/B (`rbuf`, `rbuf + 0x600`) |
| +0x14 | write offset in the current send buffer (starts at 4) |
| +0x18, +0x24 | init parameters (+0x24 copied into send word 1 for fno 0) |
| +0x1C | current buffer index (0/1), +0x20 previous |
| +0x34 | `sceSifClientData` |

| fno | Caller | Send | Receive | When |
|---|---|---|---|---|
| 0 | `func_002172C8` (from the sound init loop `func_00219030` in `InitSubsystems`) | 0x2400 bytes: word 0 = 0 commands, word 1 = init parameter | 0x600 bytes | once at boot |
| 1 | `func_00217428` (from `SoundRpcUpdate` (H) 0x00218F18, main loop, every frame) | 0x2400 bytes: word 0 = command count, then the command records | 0x600 bytes: reply list | every frame, only when the previous call has completed (`sceSifCheckStatRpc`) |

**Command records** (appended by writers 0x00217578–0x002179D8; the first word is the command id; size is fixed per writer):

| Id | Size | Built by (API) | Fields (after the id) | Meaning (H unless noted) |
|---|---|---|---|---|
| 1–4 | 0x48 | `func_00217578` via `func_002174D0(type, rec)`; API 0x0021D018–0x0021E240 (12 functions, e.g. `func_0021D680`, `func_0021D138`) | full 72-byte voice/sound request struct | play/modify a sound (4 sub-types) |
| 5 | 12 | `func_0021E0E8(a, b)` via `func_002179D8` | `a`, `b` | (unknown) |
| 6 | 16 | `func_0021CF70(ch, val)` | byte `ch`, word `val` | per-channel parameter |
| 7 | 16 | `func_0021CFA0(ch, a, b)` | byte `ch`, u16 `a`, u16 `b` | per-channel pair (volume L/R?) |
| 8 / 9 | 16 | `func_0021CFE8(ch)` / `func_0021D018(ch)` | byte `ch` | stop / release voice (H) |
| 0xA | 20 | `func_00217680(src, dst, size)` | IOP `src`, SPU `dst`, `size`, **ticket** (returned) | **transfer IOP RAM → SPU RAM** (used by `StreamBdEntryToIop` (H) for BD.CDP banks). Completion is reported in the reply (type 10) and polled by `func_00217858` |
| 0xB | 12 | `func_0021CE38(b, l, r)` | byte, u16 `l`, u16 `r` (scaled to 0x3FFF) | master / effect volume |
| 0xD | 12 | `func_0021CF08(b, l, r)` | byte, u16 `l`, u16 `r` (scaled to 0x7FFF) | another volume (reverb depth?) |
| 0xE | 8 | `func_0021CE08(v)` | word | global setting (boot: called in scene 0 state 3) |

**Reply list** (read by `SoundRpcUpdate`): word 0 = count, then 3-word records `{type, a, b}`. Type 6 → `func_00218EE8(a)`, type 10 → transfer done (ticket `b`, `func_002177F8`/`func_00217838`), type 0xF → voice status block: 48 status bytes copied into 0x00424FD0 (`func_00217A88`), types 1–4/7–9 are skipped.

## VAG.IRX (`0x81000102`): 6 streaming voices

Client state around 0x00353BF8–0x00353FC0. Per-slot structs: 6 × 0x94 bytes at 0x00353BF8. Send buffers A/B at `0x00353F78/7C` (0x100 bytes each), receive buffers at `0x00353F80/84`.

| fno | Caller | Send (0x100 bytes) | Receive (0x100 bytes) | When |
|---|---|---|---|---|
| 0 | `func_0021E370` (stream init `func_0021E2D8`) | 4 per-slot words (slot params) | IOP buffer base at `+0`; the EE derives the IOP ring addresses: slots 0–1 3 × 0x20000 bytes each, slots 2–5 3 × 0x10000 each, starting at `base + 0x180`, stored at 0x00353BF8 + slot × 0x94 | once |
| 1 | `func_0021E8F0` (from `StreamAudioUpdate` (H) 0x0021FE60, main loop, every frame, after `func_0021F448` builds the slot commands) | word 0 = command word `0x00353F70`, word 1 = slot-ready bitmask `0x00353F90` (bit n = slot n's disc data has arrived), then per-slot data | per-slot status (play position at `+0x24 + slot*4`, flags at `+0x14 + slot*4`) | every frame, when the previous call has completed |

**Data path**: VAG.IRX never reads the disc. The EE queues `QueueCdRead(queue 0, lsn, flags=1, dest = IOP ring address, …)` (`func_0021EAB8`, `func_0021F448`), which runs `sceCdReadIOPm` and reads VPK data **straight into IOP RAM**. The EE then marks the slot ready in the bitmask. Slot requests come from a 32-entry EE-side queue (0x0042CEBC): `func_0021FD40(slot, track)` (10 callers, start track), `func_0021FC80(slot, a, b)` (3 callers), stop/fade commands (types 3/4). `func_0021F190(vol, slot, l, r, fade)` (27 callers) sets per-slot volume and pan for the next update.

**SMV movie audio** (corrected 2026-10-10, `iop-sound.md`): the movie player borrows the IOP address `*0x00353BF8` (VAG channel 0's first ring segment) as a 2 × 0x4000-byte PCM double buffer, DMAs 0x4000-byte blocks into it with `sceSifSetDma` (`func_00213D10`), and plays it through **SOUND.IRX**. SOUND command 0xA `{src, dst = 0, size 0x8000}` makes the IOP run `sceSdBlockTrans` (core-0 sound-data input, loop); command 0xA `{0,0,0}` stops it, and command 0xD sets its volume. VAG.IRX is not involved (it has no PCM path).

## USBJOY.IRX (`0x80000210`): USB joystick polling

| fno | Caller | Send | Receive | When |
|---|---|---|---|---|
| 2 | `func_00109780` (from the pad task `func_00102220`, every frame) | 0x80 bytes at 0x00420440 | 0x80 bytes at 0x004204C0: two devices × buttons, axes, hat (bytes at +0x04…+0x14 and +0x48…+0x54) | every frame, **synchronous**: `sceSifCheckStatRpc` busy-wait loop right after the call |

The reply is converted into the game's pad format (two ports), so USB joysticks look like extra DualShock data.

## Direct EE → IOP DMA (`sceSifSetDma`)

| Caller | Source | Destination | Use |
|---|---|---|---|
| `func_00213D10` (SMV reader 0x00212788/0x00212960/0x00212C78/0x00212DF0) | 0x4000-byte PCM block in the chunk buffer | IOP ring `*0x00353BF8` (+0x4000 for the second half) | movie audio |
| `func_00215070` (HDD read path, state 4) | EE buffer | IOP 0x07800000 | HDD-installed data that has to go to the IOP (flag 1 requests, e.g. BD banks) |

## What the port needs

- No IOP emulation. The audio backend implements the **three server interfaces above** on the PC side:
  - SOUND command batch → a sound-effect mixer with SPU-ADPCM voices;
  - VAG slots → streamed ADPCM from VPK, plus PCM for movies;
  - USBJOY → joystick input.
- "IOP RAM" becomes a buffer pool owned by the audio backend. `sceCdReadIOPm` writes into it, and command 0xA copies from it into the "SPU RAM" sample store.
- The per-frame call order (sound update, then stream update, then `SceneStep`) and the one-call-in-flight rule must be kept (ADR-004).

## Open

- Meaning of the 72-byte voice request struct (types 1–4) and of command ids 5, 6, 7.
- VAG fno 1 per-slot command layout beyond words 0–1.
- Whether VAG.IRX decodes ADPCM itself or plays through SPU voices (affects mixing). This is an IOP-side question, so check it in PCSX2 (SPU2 voice usage) instead of in IOP code.
