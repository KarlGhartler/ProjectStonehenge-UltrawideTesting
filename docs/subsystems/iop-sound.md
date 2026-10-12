# SOUND.IRX and VAG.IRX: IOP-side command handlers

The two game-owned IOP modules, imported into Ghidra as MIPS R3000 (`MIPS:LE:32:default`). They keep their original **symbol tables**, so function and data names below are Namco's own. The EE side (RPC clients, batches, data path) is in `../game/iop-interface.md`.

## Modules

| Module | `.iopmod` name | Version | Entry | `gp` | Text / data / bss |
|---|---|---|---|---|---|
| SOUND.IRX | `NAMCO_GV79_SOUND_TEST` | 0x101 | 0xA6C `start` | 0x9700 | 0x1670 / 0xC0 / 0x4840 |
| VAG.IRX | `AC4 VAG module` | 0x101 | 0x100 `start` | 0x9B90 | 0x1780 / 0x440 / 0xC0310 |
| LIBSD.IRX | `Sound_Device_Library` | 0x104 | 0x90 | 0xC9E0 | no symbols (stock Sony module) |

Both modules carry a `.mdebug` section (debug info). Ghidra's STABS analyzer could import types and line numbers from it, but it needs `stdump` from the Emotion Engine plugin.

### How they are loaded in Ghidra

- The ghidra-mcp `import_file` with a language override loads files as raw binary, and without an override Ghidra picks the EE plugin's 64-bit language. Working method:
  1. `tools/irx_imports.py --image` writes each module's loaded image (program segments at their link address 0) to `out/irx/image/<NAME>.IRX`;
  2. the image is imported raw at base 0 as `MIPS:LE:32:default`;
  3. functions, import stubs and data labels are created from the module's own `.symtab` (`out/irx/<NAME>.IRX.symbols.csv`, `.ghidra_seed.json`).
- **Relocations:** IRX modules are linked at 0 and relocated by the IOP loader. At base 0 the stored code is already correct: every `jal` lands on the right function or import stub.
- **Imports:** each import stub (`jr ra; addiu zero, zero, N`) is named by the module's own symbol table, which agrees with the import table's (library, index) pairs (appendix).
- **`gp`:** the MCP has no tool to set a register value, so `gp`-relative accesses show as `unaff_gp ± offset`; the values are in the table above.

## SOUND.IRX (RPC 0x81000101)

`start` creates `sifrpc_thread` (priority 0x1C, stack 0x800). It enables IOP interrupts 0x24, 0x28 and 9 (SPU2 DMA/IRQ), registers RPC `0x81000101` with receive buffer `buffer` (0x1740, 0x2400 bytes; reply area 0x3B40, 0x600 bytes), and runs `sceSifRpcLoop`.

`sifrpc_server(fno, data)` (0xB90):

| fno | Action |
|---|---|
| 0 | `iopbuf = data[1]` (the EE's init parameter), then `iop_sound_init`: `sceSdInit(0)`, core attributes, master volume 0x3FFF on both cores, all voices routed (switch 0x1800/0x1A00 on, 0x1900/0x1B00 off), reverb off on both cores, clears the 48 voice-state bytes, starts the BD transfer helper (`init_TransBd`) |
| 1 | process a command batch `{count, records…}` and build the reply |

### Command records (fno 1)

Byte offsets inside each record. "voice" = 0–47 (0–23 core 0, 24–47 core 1; `which_is_core`). Size and handler are fixed per id; the EE writers match exactly. Unknown ids are ignored.

| Id | Size | IOP handler | EE writer | Fields | Effect |
|---|---|---|---|---|---|
| 1 | 0x48 | `tone_key_on` | `QueueSoundVoiceCmd` 0x00217578 via `func_002174D0(type, rec)`; API `PlaySound` and variants (`../subsystems/sound.md`) | voice request (below) | set all voice attributes, key on (`sceSdSetSwitch(KEY_ON)`), state = 1 |
| 2 | 0x48 | `set_tone_attr` | same; `SetSoundVolume`, `SetSoundPitch` | voice request + +0x14 flags | modify a playing voice: bit 0 volume, bit 1 raw pitch (+0x38), bit 2 pitch from note (+0x30..+0x33, rate +0x3A) |
| 3 | 0x48 | `tone_key_on` | same | same as 1 | same as 1, reply type 3 |
| 4 | 0x48 | `tone_key_off` | same; `StopSound` | voice at +0x12 | key off one voice |
| 5 | 12 | `tone_key_off_bit` | `func_0021E0E8(a, b)` via `func_002179D8` | +4 core-0 mask (24 bits), +8 core-1 mask | key off by bitmask; voice states cleared |
| 6 | 16 | `set_reverb_mode` | `func_0021CF70(core, mode)` | +4 byte core, +8 mode | reverb type; the reply carries the effect work-area start address (`sceSdGetAddr` of the ESA) |
| 7 | 16 | `set_reverb_depth` | `func_0021CFA0(core, l, r)` | +4 core, +0xC s16 depth L, +0xE depth R | reverb depth |
| 8 / 9 | 16 | `set_reverb_on` / `set_reverb_off` | `func_0021CFE8(core)` / `func_0021D018(core)` | +5 core | reverb on / off |
| 0xA | 20 | posts to the `TransBd` thread | `func_00217680(src, dst, size)`, returns a ticket | +4 IOP source, +8 SPU destination, +0xC size, +0x10 ticket | **dst ≠ 0**: `sceSdVoiceTrans` (IOP → SPU RAM; BD.CDP sample banks, `StreamBdEntryToIop` (H) 0x00219C80). **dst = 0**: `sceSdBlockTrans` on core 0 (PCM streaming into the core-0 sound-data input: mode 0x10 = loop with a source, mode 2 = stop when the source is 0); used for SMV movie audio |
| 0xB | 12 | `set_master_volume` | `func_0021CE38(core, l, r)` (scaled to 0x3FFF) | +4 core, +6 s16 L, +8 s16 R | master volume |
| 0xC | 12 | `set_coreinput_volume` | `func_0021CEA0` (no callers) | same layout | core input volume; never sent (dead command) |
| 0xD | 12 | `set_datainput_volume` | `SetSoundDataInputVolume` 0x0021CF08 (scaled to 0x7FFF) | same layout | volume of the core-0 sound-data input (movie PCM, fades) |
| 0xE | 8 | inline | `SetSoundOutputMode` 0x0021CE08 | +4 mode | `iop_output_mode`: 2 = mono (left/right averaged in every attribute write) |

### The 72-byte voice request (ids 1–4)

Decoded from `sifrpc_server`, `tone_key_on`, `set_tone_attr_all` and `set_tone_attr`. Bytes +0x20…+0x47 are passed by value on the IOP stack. The EE-side meaning of the fields the IOP does not read is in `../subsystems/sound.md` (voice record).

| Off | Type | Field | Used by |
|---|---|---|---|
| 0x00 | u32 | command id (1–4) | dispatch |
| 0x04 | u32 | request id, echoed in the reply | reply |
| 0x08–0x0D | | EE-side data, not read by the IOP | |
| 0x0E | u8 | tag, echoed in reply byte +8 | reply |
| 0x12 | u8 | voice 0–47 | all |
| 0x14 | u32 | modify flags (id 2 only): bit 0 volume, bit 1 raw pitch, bit 2 note pitch | `set_tone_attr` |
| 0x18, 0x1C, 0x20 | u32 ×3 | passed as arguments but not read by the attribute code | |
| 0x24 | u16 | ADSR1 | `sceSdSetParam(voice | 0x300)` |
| 0x26 | u16 | ADSR2 | `sceSdSetParam(voice | 0x400)` |
| 0x28–0x2B | | unused | |
| 0x2C | s16 | volume left (clamped to ±0x3FFF; negative = phase-inverted) | `0x000` |
| 0x2E | s16 | volume right | `0x100` |
| 0x30 / 0x31 | u8 | centre note / centre fine | `sceSdNote2Pitch` |
| 0x32 / 0x33 | u8 / s8 | note / fine | |
| 0x38 | u16 | raw pitch (id 2, flag bit 1) | `0x200` |
| 0x3A | u16 | **sample rate in Hz**: pitch = `Note2Pitch × rate / 48000` | `0x200` |
| 0x40 | u32 | **SPU start address** of the sample (SSA) | `sceSdSetAddr(voice | 0x2040)` |

### Reply (fno 1)

Word 0 = number of records (voice records + 2), then:
- one 12-byte record per voice command (ids 1–4, 6–9): `{id, request id, u8 tag, u8 status}`. For id 6, word 1 is the reverb work-area address. Status 1 = OK, 0xFF = error;
- a transfer-status record `{10, last finished ticket, done flag}` (the `trans_bd` state, updated by `handler_DmaInt`);
- a voice-status record `{0xF, output mode, 48 status bytes}` (`get_ch_stat`): 1 = keyed on and playing, 0 = off, 4 = released but the envelope still sounds (ENVX ≠ 0). In PCSX2 at the title screen (`scenestep_first_2.p2s`, 0x00424FD0) voices 42–47 report 4 and all others 0. HYPOTHESIS: those are the 6 VAG.IRX streaming voices (audible, but not keyed on by SOUND). The port's stand-in reports 0 until the audio backend exists (`../port/validation.md` E-1).

### Movie audio

The EE movie player uses SOUND.IRX, not VAG.IRX:
- it borrows the IOP address `*0x00353BF8` (VAG channel 0's first ring segment) as a 2 × 0x4000-byte PCM double buffer (`func_00213CE8`, `func_00213D10` with `sceSifSetDma`);
- it starts the stream with command 0xA `{src = that buffer, dst = 0, size = 0x8000}` (`func_00213DC8`) → `sceSdBlockTrans(core 0, loop)`, and stops it with 0xA `{0, 0, 0}` (`func_00213E30`);
- it sets the input volume with command 0xD: `func_00213C50` resets the feeder with volume 0, `func_00213EA0` starts a fade and `func_00213F60` steps it each frame.

The PCM format is therefore the SPU2 core-0 **sound-data input**: 48 kHz 16-bit stereo. In that input format, left and right samples alternate in 0x200-byte blocks (H, standard SPU2 behaviour; to check against a PCSX2 SPU2 capture).

## VAG.IRX (RPC 0x81000102)

`start` creates `MainThread` (priority 0x1C). It registers RPC `0x81000102` with `dispatch(fno, data)` and receive buffer `arg_4`. Replies go to `rbuf` (0x1CC0).

| fno | Handler | Action |
|---|---|---|
| 0 | `VagAllInit(data, rbuf)` | **once**: allocates the stream area `vag_buf` (0x1D10, 0xC01C0 bytes, 64-byte aligned) and builds **4 stream channels** (`vag_param`, 0x6C bytes each). Channels 0 and 1 are **stereo** (2 SPU voices each, 3 × 0x20000-byte IOP ring segments, initial pitch 0xEB4 = **44.1 kHz**). Channels 2 and 3 are **mono** (1 voice, 3 × 0x10000, pitch 0x75A = **22.05 kHz**). `data[0..3]` = first SPU voice number per channel (core 1). Starts `VagThread` (`VagMain`), installs the SPU2 DMA/IRQ handlers, preloads silence. Reply: `rbuf[0]` = IOP base of the stream area, `rbuf[9+i]` = per-channel ring index |
| 1 | `VagCtrl(data, rbuf)` | `data[0]` = command count, `data[1]` = **data-ready bitmask** for EE slots 0–5. Per-channel play state: 0 idle → 1 playing when its bit is set; bit cleared while playing → 2 (underrun/wait). Then `count` × 20-byte commands `{id, slot, a, b, c}` via table `func` (0x1B80) |

EE slots 0–5 map onto the 4 channels: slots 0–3 → channels 0–3, slot 4 → channel 2, slot 5 → channel 3 (`VagCtrl`, `begin_PlayVag_main`).

| Id | Handler | Fields | Effect |
|---|---|---|---|
| 0 | `bad_command` | | error print |
| 1 | `begin_PlayVag` → `begin_PlayVag_main(cmd, 4)` | +8 flags → channel +0x60; +0xC → +0x4C (data size / block count, H); +0x10 pitch → `set_VagFreq` | start a stream on a channel (mode 4); a busy channel queues the request in `next_vag_param` |
| 2 | `begin_PlayVag1` → `begin_PlayVag_main(cmd, 8)` | same | start in mode 8 (H: one-shot vs. looping) |
| 3 | `end_PlayVag` | +4 slot | stop: state 0, end flag 1 |
| 4 | `complete_PlayVag` | +4 slot | mark "all data sent" (end flag 0x40): the stream plays out and stops |

Playback: `VagMain` (`VagThread`) moves each filled IOP ring segment into the channel's SPU RAM double buffer with `sceSdVoiceTrans` (`MarkTop`/`MarkBottom` set the ADPCM loop flags on the half-buffers) and keys the voices. The data is **SPU-ADPCM decoded by SPU2 voices**; VAG.IRX does no decoding and no PCM output (it does not import `sceSdBlockTrans`).

## Appendix: import mapping

| Library | Index → function (SOUND) |
|---|---|
| libsd | 4 `sceSdInit`, 5 `sceSdSetParam`, 6 `sceSdGetParam`, 7 `sceSdSetSwitch`, 8 `sceSdGetSwitch`, 9 `sceSdSetAddr`, 10 `sceSdGetAddr`, 11 `sceSdSetCoreAttr`, 13 `sceSdNote2Pitch`, 17 `sceSdVoiceTrans`, 18 `sceSdBlockTrans`, 20 `sceSdBlockTransStatus`, 23 `sceSdSetEffectAttr`, 26 `sceSdSetTransIntrHandler` (VAG also 27 `sceSdSetSpu2IntrHandler`) |
| intrman | 6 `EnableIntr`, 9 `CpuEnableIntr` |
| sifcmd | 14 `sceSifInitRpc`, 17 `sceSifRegisterRpc`, 19 `sceSifSetRpcQueue`, 22 `sceSifRpcLoop` |
| sifman | 5 `sceSifInit`, 29 `sceSifCheckInit` |
| thbase | 4 `CreateThread`, 6 `StartThread`, 20 `GetThreadId`, 39 `USec2SysClock` |
| thsemap | 4 `CreateSema`, 6 `SignalSema`, 7 `iSignalSema` (VAG), 8 `WaitSema` |
| timrman | 4 `AllocHardTimer`, 6 `FreeHardTimer`, 10 `GetTimerCounter`, 22 `SetupHardTimer`, 23 `StartHardTimer`, 24 `StopHardTimer` |
| loadcore / stdio / sysclib (VAG) | 5 `FlushDcache` / 4 `printf` / 14 `memset` |

Source of every name: the module's own `.symtab` (the linker resolved the stub names). The (library, index) pairs agree with the import tables.

## Open

- `VagMain` details: ring-segment hand-off, underrun handling, mode 4 vs. mode 8.
- SOUND `delay_timer` / `spu_register_timer` (hardware-timer based key-on/off spacing, H).
- Import of the `.mdebug` STABS info via the plugin's STABS analyzer.
