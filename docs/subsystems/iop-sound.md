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
| 2 | 0x48 | `set_tone_attr` | same; `SetSoundVolume`, `SetSoundPitch` | voice request + +0x10 flags | modify a playing voice: bit 0 volume, bit 1 raw pitch (+0x38), bit 2 pitch from note (+0x30..+0x33, rate +0x3A) |
| 3 | 0x48 | `tone_key_on` | same | same as 1 | same as 1, reply type 3 |
| 4 | 0x48 | `tone_key_off` | same; `StopSound` | voice at +0x0E | key off one voice |
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

Decoded from `sifrpc_server`, `tone_key_on`, `set_tone_attr_all` and `set_tone_attr`. Correction 2026-10-14 (S2a): the voice byte is +0x0E and the modify flags +0x10 (`sifrpc_server` reads them relative to record + 4; the earlier table had +0x12 / +0x14). Bytes +0x20…+0x47 are passed by value on the IOP stack. The EE-side meaning of the fields the IOP does not read is in `../subsystems/sound.md` (voice record).

| Off | Type | Field | Used by |
|---|---|---|---|
| 0x00 | u32 | command id (1–4) | dispatch |
| 0x04 | u32 | request id, echoed in the reply | reply |
| 0x08–0x0D | | EE-side data, not read by the IOP | |
| 0x0E | s8 | **voice 0–47** (also echoed as the reply tag, byte +8). Read by sifrpc_server as `*(char *)(record + 4 + 10)` | all |
| 0x10 | u32 | modify flags (id 2 only): bit 0 volume, bit 1 raw pitch, bit 2 note pitch (the EE sets 0x4001 for a direct volume change) | `set_tone_attr` |
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

Word 0 = number of records (voice records + 2), then (`sifrpc_server` 0x0B90, checked against PCSX2 run 06, `../pcsx2/sessions-06.md`):
- ids 1-4: `{id, request id, u8 tag, u8 status}` (status 1 = OK, 0xFF = error). Ids 6-9: `{id, -, u8 status at +8}`: word 1 (except id 6) and bytes 9-11 are **not written**. For id 6, word 1 = `sceSdGetAddr` of the lower of the two cores' effect start addresses, read right after `sceSdSetEffectAttr` (which writes the ESA register synchronously): ESA = EEA - (table[mode] * 8 - 2), table at LIBSD.IRX 0x4148 (modes 0-9: 0x2, 0x4D8, 0x3E8, 0x908, 0xDFC, 0x15BC, 0x1ED8, 0x3008, 0x3008, 0x780), EEA 0x001DFFFF (core 0) / 0x001FFFFF (core 1). The game's (core 1, mode 2) gives core 1 0x001FE0C0, so the reply is core 0's mode-0 0x001DFFF0 (PCSX2 SPU2 state, runs 06/09/10; `../port/validation.md` S4-7).
- a transfer-status record `{10, last finished ticket, status}` from `trans_bd` (base 0x16D0): the ticket is stored by `handler_DmaInt` when the DMA ends; `main_TransBd` runs after the reply of the batch that requested it, so that reply still has the previous ticket. `handler_DmaInt` (0x1300, the SD transfer interrupt) is the only writer of the finished ticket: a block-transfer stop `{0, 0, 0}` (`sceSdBlockTrans` mode 2) runs no DMA, so its ticket is never reported (PCSX2: one behind after every movie; `../port/validation.md` S4-7). Status = `sceSdBlockTransStatus(0,0) >> 24`, 0 in every measured reply;
- a voice-status record `{0xF, output mode, 48 status bytes}` (`get_ch_stat`): a voice SOUND keyed on (`iop_ch_state` 1) reports 1 until its ENDX bit (`sceSdGetSwitch(0x1700)`) is set; every other voice (state 0 or 4, or 1 after ENDX) reports 4 when its envelope ENVX (param 0x500) is non-zero, else 0. Voices 42-47 report 4 at every measured point (title, scene 6): they are the VAG.IRX stream voices, keyed on by VAG.IRX (not SOUND) and looping forever (confirmed by the code, S2a).
- The reply is built in **one IOP area** (0x3B40, 0x600 bytes) that is never cleared and copied back whole: bytes after the last record are leftovers of earlier replies and reach EE memory (the port's stand-in keeps the same area).
- fno 0 (`iop_sound_init`) writes only the count 2 into the reply area; its records go to IOP address 0 (s5 = 0).

VAG fno 1 reply (run 06, nothing streaming): word 0 stream area 0x0008DA40, words 5-8 = 1 (state idle), words 9-12 (ring index per channel) = 2, +0x50/+0x60 = 0x200. The fno 0 reply has ring indices 1 and words 5-8 = 0. Both follow from VagAllInit/VagMain (VAG.IRX section).

### Movie audio

The EE movie player uses SOUND.IRX, not VAG.IRX:
- it borrows the IOP address `*0x00353BF8` (VAG channel 0's first ring segment) as a 2 × 0x4000-byte PCM double buffer (`func_00213CE8`, `func_00213D10` with `sceSifSetDma`);
- it starts the stream with command 0xA `{src = that buffer, dst = 0, size = 0x8000}` (`func_00213DC8`) → `sceSdBlockTrans(core 0, loop)`, and stops it with 0xA `{0, 0, 0}` (`func_00213E30`);
- it sets the input volume with command 0xD: `func_00213C50` resets the feeder with volume 0, `func_00213EA0` starts a fade and `func_00213F60` steps it each frame.

The PCM format is therefore the SPU2 core-0 **sound-data input**: 48 kHz 16-bit stereo, left and right in alternating 0x200-byte blocks (checked on the SMV data, `../formats/SMV.md`, and confirmed against the PCSX2 capture, `../port/validation.md` S3-5).
- The feeder refills by the transfer status of each SOUND reply (`{10, ticket, status}`, status = `sceSdBlockTransStatus >> 24`, stored at client +0x30 by `func_00217838` and read by `GetSoundTransferStatus` 0x00217848): the half of the 0x8000-byte ring the SPU2 is playing. When it differs from the half the reader filled last, the other half gets the next block (`StreamSmvReader`). Confirmed by the S3 PCSX2 batch (`../port/validation.md` S3-3, S3-5).
- Port (S3): the SOUND stand-in starts/stops a loop-mode block input in the SPU mixer (`SpuMixer::StartBlockInput`), applies command 0xD as the input volume, and reports the half in the status word.

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

### Channel record (`vag_param`, 4 × 0x6C at 0x17D0; `next_vag_param` 4 × 0x6C at 0x1980)

Read from `VagAllInit`, `VagCtrl`, `VagMain`, `chg_NextIopBuf`, `begin_PlayVag_main`, `end_PlayVag`, `complete_PlayVag`, `set_VagFreq` (slice S2a).

| Off | Field |
|---|---|
| +0x00 | ring segment being read (0–2) |
| +0x04 | ring segment the EE fills next: reply `+0x24 + ch*4` |
| +0x08 / +0x0C | SPU half being refilled / the next one (0 top, 1 bottom) |
| +0x10..+0x1C | SPU half-buffer addresses [voice][half]: from 0x5010, `half` bytes each, in channel/voice order |
| +0x20..+0x28 | IOP ring segments: channels 0-1 at stream base + 0x180 + (ch × 3 + r) × 0x20000; slots 2-5 at channel 0's first segment + (slot − 2) × 0x30000 + r × 0x10000 |
| +0x2C | silence buffer (stream base, or base + 0x100 when the frequency word is not 0xEB4) |
| +0x30 / +0x34 | read position inside the segment, per voice / next position |
| +0x38 | half-buffer size: 0x100 (channels 0-1), 0x80 (channels 2-3) |
| +0x3C / +0x40 | underrun counter / its previous value |
| +0x44 / +0x48 | SPU voices (core 1) |
| +0x4C | voice count (2 stereo, 1 mono) |
| +0x54 | frequency word from the EE (only selects the silence buffer; the voice pitch stays 0xEB4 / 0x75A) |
| +0x58 | state: 1 idle, 4 / 8 start (command 1 / 2), 0x20 playing, 0x40 last data sent, 0x80 chained stream follows |
| +0x5C | "switched to the chained stream" flag |
| +0x60 / +0x64 | bytes per voice to play / played |
| +0x68 | data-ready state from the EE bitmask (0 none, 1 ready, 2 bit dropped while playing) |

- **Init** (`VagAllInit`, fno 0): each voice gets ADSR 0x00FF/0x1FC0, SSA = its top half, pitch 0xEB4 (channels 0-1) or 0x75A (2-3), both halves filled with silence, and all 6 voices are keyed on once (they loop forever). Reply ring index 1, then `chg_NextIopBuf` (read 1, fill 2).
- **IRQ:** core 1 IRQA is set to channel 1's right voice at the half it plays next; the SPU2 IRQ (one half-buffer period: 448 samples at 44.1 kHz) wakes `VagMain`.
- **`VagMain`** per channel: a start request (state 4/8) switches to the next ring segment (unless chained) and sets 0x20, or 0x40/0x80 for command 2. Without data (ready 0) or when not playing, the silence buffer is sent (no transfer after 3 such periods). Otherwise the next half (stereo: left block at `pos/0x800 × 2 × 0x800 + pos%0x800`, right 0x800 later) goes to the half just played, `pos` advances; at the end of a segment (`pos` reaches 0x10000) the ring index advances and the EE refills; at the end of the data the channel goes idle (1) or switches to `next_vag_param` (state 4). Reply words: `+4 + ch*4` played bytes (0 when nothing advanced), `+0x14 + ch*4` state, `+0x24 + ch*4` ring index to fill.
- **`VagCtrl`** (fno 1): ready bits per EE slot (slots 4/5 → channels 2/3), then the commands. Reply = `rbuf` as `VagMain` last wrote it (words +0x50/+0x60 = 0x200 measured, writer not found).
- Port: `src/platform/iop/iop_services.cpp` (`VagService`) follows these functions; the SPU2 IRQ comes from the mixer (`SpuMixer::SetIrqAddress`).

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

- SOUND `delay_timer` / `spu_register_timer` (hardware-timer based key-on/off spacing, H).
- Import of the `.mdebug` STABS info via the plugin's STABS analyzer.
