# ADR-004: Audio boundary (SOUND.IRX / VAG.IRX replacement)

Status: **Accepted** (User decision, 2026-10-11): **Option B, HLE at the RPC boundary with our own mixer.**

## Context

From `../game/iop-interface.md` (EE side) and `../subsystems/sound.md` (IOP side, from the modules' own symbol tables):

**SOUND.IRX** (`NAMCO_GV79_SOUND_TEST`, RPC 0x81000101) is a thin command interpreter over Sony's libsd. Once per frame the EE sends a batch of up to 0x2400 bytes:
- voice requests (ids 1–4, 72 bytes): key on/off, volume, ADSR1/2, note/fine and sample rate → pitch, SPU start address;
- key-off masks (5); reverb mode/depth/on/off (6–9);
- IOP→SPU-RAM transfers or **PCM block streaming** (0xA);
- master/core/data-input volumes (0xB–0xD); mono/stereo output mode (0xE).

The reply carries per-command status, the transfer ticket state and a 48-byte voice-status block. Voices are SPU2 hardware voices playing SPU-ADPCM from SPU RAM (sample banks = BD.CDP, uploaded with command 0xA).

**VAG.IRX** (`AC4 VAG module`, RPC 0x81000102) streams SPU-ADPCM from IOP RAM rings into 4 stream channels: 2 stereo at 44.1 kHz, 2 mono at 22.05 kHz, on core-1 voices. The EE fills the rings straight from disc (`sceCdReadIOPm`) and signals a data-ready bitmask each frame; commands are begin (2 modes), end and complete. No decoding on the IOP: SPU2 decodes.

**Movie audio** (SMV) is 48 kHz 16-bit stereo PCM, streamed by SOUND.IRX into the SPU2 core-0 sound-data input with `sceSdBlockTrans` (command 0xA with dst = 0), using an IOP buffer borrowed from VAG channel 0.

**USBJOY.IRX** (RPC 0x80000210) is polled synchronously each frame.

Other facts:
- one call per server in flight; replies are read on the next frame;
- the game reads the voice-status block (e.g. to know when a voice has finished), so the status semantics are game-visible;
- port rule: IOP modules are not ported (CLAUDE.md); `src/sce/` + `src/platform/` replace them.

## Options

### A. Emulate the IOP (LLE)
Run SOUND/VAG/LIBSD in an R3000 interpreter on an SPU2 emulator.
- \+ Bit-exact without understanding the modules.
- − Brings back console emulation (IOP kernel, libsd, SIF), against the project's direction. **Rejected.**

### B. HLE at the RPC boundary with our own SPU2-subset mixer (recommended)
`sceSifCallRpc` for the three server ids calls PC services that implement exactly the command sets documented in `../subsystems/sound.md`:
- **SOUND service:**
  - 48 voices with SPU-ADPCM decoding, ADSR envelopes, the pitch formula `Note2Pitch(centre, fine, note, fine) × rate / 48000`, volumes and the phase-invert sign;
  - two cores with master volumes, reverb (mode/depth/on/off), and the sound-data input stream for PCM;
  - an "SPU RAM" sample store filled by command 0xA;
  - replies in the original format, including the 48-byte status block (keyed on / releasing / off).
- **VAG service:** 4 channels reading SPU-ADPCM from an "IOP RAM" ring pool, with the same state machine (data-ready bits, begin/end/complete), feeding voices of the same mixer.
- **USBJOY service:** SDL3 joysticks → reply block.
- Output: SDL3 audio stream, 48 kHz stereo, mixed on a platform thread; the game thread only exchanges batches (ADR-002).

### C. HLE above the RPC layer
Replace the EE sound API layer (≈20 functions) with direct calls to a PC audio engine.
- − Loses the captured-batch test oracle and spreads audio semantics into game-side code; the status block must be reproduced anyway. **Rejected.**

### D. Reuse PCSX2's SPU2 code inside option B
Use PCSX2's SPU2 emulation (voices, ADPCM, ADSR, reverb) as the mixer behind the option-B services.

| Concern | Finding |
|---|---|
| License | PCSX2 is GPL-3.0-or-later overall, compatible with this repo. **Per-file check required** (older files may carry different headers; some SPU2 code descends from older projects with their own notices) |
| Fit | PCSX2's SPU2 emulates the hardware: registers, DMA, IRQs, core attributes. Option B needs a libsd-level API. We would drive SPU2 registers from our SOUND/VAG services (doable, but the code expects PCSX2's IOP/DMA/timing context) |
| Dependencies | PCSX2 `common/` (logging, config, SIMD), its audio-output backends, savestate/serialization hooks; would need stubbing |
| Accuracy | High, especially reverb (SPU2 reverb is hard to reproduce exactly) |
| Maintenance | A fork of part of PCSX2 (same issues as ADR-003 option B) |
| Policy | Would be copied and modified code, so `third_party/` rules apply (CLAUDE.md, `.claude/rules/third-party.md`) |

## Decision

**Option B with our own mixer** (accepted 2026-10-11) for voices, ADPCM, ADSR, pitch, volumes and the PCM input. This is a small, well-documented subset: about 48 voices, two cores, block input.

**Reverb**: start with our own implementation of the SPU2 reverb algorithm (it is documented: a fixed set of filters driven by mode presets), verified against PCSX2 output.
- If exactness turns out to be hard, adopt **option D only for the reverb unit**, after a per-file license check and under the third-party rules.
- PCSX2 stays the behaviour reference and test oracle either way: SPU2 captures, recorded output.

Test strategy:
- capture command batches (EE memory at the SOUND send buffer) and replies in PCSX2;
- replay them into the SOUND service and compare the status block and the mixed output.

## Consequences

- `src/platform/audio/`: SOUND service, VAG service, mixer (SPU2 subset), SDL3 output.
- `src/sce/`: `sceSifBindRpc`/`sceSifCallRpc`/`sceSifCheckStatRpc` routing for the three game-owned server ids; `sceCdReadIOPm` writes into the IOP ring pool (guest "IOP RAM" handles, ADR-001).
- Open items (do not block the decision): the exact VAG mode 4 vs. mode 8 semantics, the 0x200-byte L/R interleave of the PCM input (PCSX2 check), and the reverb presets used by the game.

## Research findings

Measured in PCSX2 (`../pcsx2/sessions.md` run 03); these are the exact replies the HLE must give at boot:
- **SOUND fno 0:** reply buffer = u32 count **2**, then zeros (0x600 bytes). `InitSoundRpc` returns 1 after this.
- **VAG fno 0:**
  - `+0x00` = IOP address of the stream area (0x0008DA40 on the PS2);
  - `+0x24..+0x30` = ring index 1 for channels 0–3;
  - `+0x50` = `+0x60` = 0x200 (meaning open);
  - the rest is zero.
  - The EE derives the ring segment addresses from `+0x00` + 0x180 (slots 0/1: 3 × 0x20000; slots 2–5: 3 × 0x10000, overlapping). The HLE should return the same base, so EE RAM stays comparable with PCSX2.
- **USBJOY fno 2 (no device):** word 0 = 0 (no joystick), axes 0x80, button words 0xFF, the rest zero. The HLE returns this every frame.
