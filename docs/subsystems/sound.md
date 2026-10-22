# Sound: EE side, radio, rumble and the IOP sound drivers

Everything the EE does to make sound: the voice/request API on top of SOUND.IRX, sound banks, engine and ambient sounds, positional and 3D sounds, the VAG streaming client (BGM and voices), the radio-chatter system with its subtitles, and pad rumble. EE↔IOP transport: `../game/iop-interface.md`; IOP side and command table: section "SOUND.IRX and VAG.IRX" below. Every function has a row in `../port/coverage-functions.csv` (subsystems `sound`, `radio`, `text`, `input`).

Merged 2026-10-22 from `sound.md` and `iop-sound.md` (no content removed): the EE side first, then the IOP-side SOUND.IRX and VAG.IRX command handlers (last section, formerly `iop-sound.md`).

## Layers

```
game code ──► PlaySound / SetSoundVolume / SetSoundPitch / StopSound   (0x0021D2E0.., voice records 0x48 bytes)
          ──► PlayMenuSe, PlayEventSe, engine/ambient/listener systems
          ──► radio scheduler ──► VAG slots 4/5 (voice) + subtitles
          ──► BGM requests ──► VAG slots 0..3
                 │
   request writers 0x00217578.. (SOUND command batch, one RPC per frame: SendSoundCommands)
   VAG command batch (one RPC per frame: UpdateVagStream)
```

## Voice API (SOUND.IRX requests)

A **voice record** (0x48 bytes) is owned by the caller and passed to every call. Main fields: +0x08 linked voice handle, +0x0C bank, +0x0E channel, +0x10 request mode, +0x14 pan mode, +0x18 sound id, +0x1A channel byte, +0x1C flags, +0x1E linked sound, +0x20 volume curve, +0x28 category, +0x2A volume, +0x2C/+0x2E left/right, +0x30/+0x31 note/fine, +0x32/+0x33 pitch, +0x34/+0x35 note/fine offsets.

| Function | Request | What |
|---|---|---|
| `PlaySound(bank, sound, l, r, voice)` 0x0021D2E0 | type 1 | core `func_0021D328`: variant pick, category volume (`func_0021CD08`, table 0x0042CA50 from the BGM/SE/voice options), pan, linked sounds |
| `func_0021D500` | type 1 | variant with random volume/pitch variation |
| `SetSoundVolume(l, r, voice)` 0x0021D830 | type 2 | restarts the voice when the pan mode changes |
| `SetSoundPitch(note, fine, voice)` 0x0021DE30 | type 2 mode 4 | |
| `StopSound(voice)` 0x0021E058 | type 4 | |
| `func_0021E0E8` | command 5 | stop by masks (`func_002197E8` stops everything) |
| `func_0021CE38` / `func_0021CEA0` | commands 0xB / 0xC | master volume / core input volume pairs; 0xC has no callers (dead) |
| `func_0021CFE8` / `func_0021D018` | commands 8 / 9 | reverb on / off for one SPU core |

Convenience wrappers: `PlayMenuSe(bank, sound)` 0x002196A8 on a fixed voice (50 callers), `func_002196E0`, `func_00219728`, `func_00219758`, `func_00219798`.

**Randomness:** variant, pitch and volume variation use the sound module's own 15-bit LFSR `NextSoundRandom` 0x0021CD20 (state `D_00410D30`), **not** `rand` (`../game/random.md`). It never affects the game state; the port keeps it separate so that it does not disturb the `rand` sequence.

## Sound banks

- Bank table (32 entries): ids 0x0033E298, headers 0x0033E194, SPU addresses 0x0033E214. Bank 0 starts at SPU 0x5A10; each next bank is packed after the previous one (`func_00218A00`).
- Loading (scene 6 and the menus): `LoadSoundBankData` (H) 0x00219B90 reads the DATA header entry, `StreamBdEntryToIop` (H) 0x00219C80 reads the BD.CDP body into IOP RAM in 0x30000-byte pieces and asks SOUND.IRX to move each piece to SPU RAM (command 0xA, tickets polled with `func_00217858`).

## Engine and ambient sounds

- Per player: 7 engine layers (voice records at 0x00423880 + player × 0x318 + layer × 0x48). Volume and pitch come from curves indexed by throttle × 10 with linear interpolation (`func_002159E0`, `func_00215A90`), plus speed terms (`func_00215B38`, `func_00215B90`). The per-frame mixer is `func_00215D20`; it switches layers 7 and 8 when the throttle crosses 0.2 or 0.9, compared in double precision against double constants (keep the double compare in the port).
- Start/stop: `func_002166D0` / `func_00216728`; per-frame entry `func_002166A8`; per-player parameters `func_00216B80` (called from the player update, `flight.md`).
- **Setup at mission load** (S5a, `mission-flow.md`). Per player p: 11 layer voice records of 0x48 bytes at 0x00423880 + p · 0x318 (byte +0xE = voice channel, 0xFF = none); per-player words at 0x00410C88, 0x00410C90, 0x00410C98, 0x00410CA0, 0x00410CA8, 0x00410CB0, 0x00410CB8, 0x00410CC0 (+4p each); curve set 0x00423F50 + p · 0x10E; bank index 0x00410CC8 + 4p; u16 0x004117F8 + 2p; ambient record 0x0033D7C0 + p · 0xC0 (fields used here: +0x50 second voice record, +0x54 and +0xB4/+0xB8 cleared, +0xA0 aircraft id, +0xA4 parameter, +0xA8 bank index, +0xAC BD bank id, +0xB0 curve pointer, +0x9C cleared).
  - `func_00216550(p, a, b)`: +0xA0 = a, +0xA4 = b (sub 8: `(0, game[0], 0xFFFF)`, and `(1, game[0x10], 0xFFFF)` with 2 players).
  - `func_002165D0(players)`: `func_00216528` (limits `func_00215D08(100, 100, 0x100, 100)`: u16 0x00410CF0/F2/F4/F6); `func_00215BD0(mode)` with mode 0 for 1 player, 1 for 2 (any other count → 2 players, mode 1); then per player: +0x9C = 0, `func_00215CD0(p, curves +0xB0, bank +0xA8, u16 +0xA4)`, +0x54 = 0, +0xB4 = 0, +0x04 = 0, +0xB8 = 0; finally `func_00216728` (everything off).
  - `func_00215BD0(mode)`: 0x00410CD8 = 0 (disabled), 0x00410CDC = mode; for both players the eight per-player words = 0 and the 11 layer channel bytes = 0xFF (written from layer 10 down).
  - `func_00215CD0(p, curves, bank, v)`: 0x00410CC8[p] = bank, u16 0x004117F8[p] = v, `func_002155D0(p, curves)`: copies 0x10E bytes (aircraft block 8) to 0x00423F50 + p · 0x10E (8-byte words, then the 14-byte tail).
  - `func_002166D0` (on): 0x0033D858 = 1; `func_002162F0`: 0x00410CD0 = bank id of index 0x00410CC8 (`func_00218F00`: u16 0x0033E298[n]), when mode − 1 < 3 (unsigned) also 0x00410CD4 = bank id of 0x00410CCC; 0x00410CD8 = 1; `func_0021CF70(0, 3)`; then +0xB4 = +0xB8 = 0 for both ambient records.
  - `func_00216728` (off): 0x0033D858 = 0; `func_00216348`: 0x00410CD8 = 0, per player the eight words = 0 and every layer with a channel (+0xE ≠ 0xFF) is stopped (`func_00215838(p, layer)` = `StopSound(0x00423880 + p · 0x318 + layer · 0x48)`) and set to 0xFF; then `func_00219798` (`StopSound`, result 0 → 1) on 0x0033D7C0 + p · 0xC0 and 0x0033D810 + p · 0xC0 for both players.
  - `func_00216790(mode)` → `func_00216520`: 0x00410CDC = mode (called from `func_001279B0` with 0–3 by which players' engines are on).
  - `func_0021A8D8(a, b, c)`: Doppler parameters 0x00410D24 = a, 0x00410D28 = b, 0x00410D20 = c (sub 8: `(0x10, 5, 600)`).
- `func_0021E798(slot, mode)` (VAG slot fade, sub 8 `(0, 0)`): slot +0x58 = mode; mode 0 (restore): `func_0021F190(1.5f, slot, +0x74, +0x78, u16 +0x66)`; mode 1 (duck): saved volumes +0x74/+0x78 = (+0x6C, +0x70) when u16 +0x64 ≠ 0, else (+0x7C, +0x80); u16 +0x66 = +0x64 (1 when 0); `func_0021F190(0.5f, slot, 0, 0, 1)`; other modes only store +0x58.
- Ambient channel voice from the aircraft-frame velocity (`func_002167B0`, HYPOTHESIS: wind), an extra voice in missions 6, 9 and 10 (`func_00216A80`).

## Positional and 3D sounds

- `PlayEventSe` 0x00219188 (27 callers): one-shot sounds by event id (explosions, impacts, …) at a position, grouped into voice pools. Per-frame update `func_00219330`: each event is delayed by its distance / pool speed (sound travel time; dropped above 30000), then played with a volume from three distance bands and pan from the angle.
- 3-slot positional table `func_00216CE0`; queued positional lists (2 × 8) `func_0021AE30` / `func_0021AF18`.
- **Listener** per player (0x0042AA10 + player × 0x1020):
  - nearest units come from `func_0011DC20` (`func_0021B3A8`), nearest missiles from `func_0014D4D0` (`func_0021B3D8`);
  - `func_0021B408` (4.3 KB): engine voices of other aircraft (4 slots) with distance attenuation, pan and **Doppler** (`func_0021A8E8`: pitch from relative speed);
  - `func_0021C4F8`: missile whoosh voices (5 slots, bank 10 sound 0x19);
  - `func_0021C760` / `func_0021CA80`: fly-by sounds by distance and speed band.
- Volume/pan helpers: `func_00219A40` (distance gain and angle), `func_00219998` (clamped gain curve), `func_0021A800` (volume × pan).

## Streams (VAG.IRX client)

Slots, commands and the per-frame batch: `../game/iop-interface.md` and the section "SOUND.IRX and VAG.IRX" below. EE-side details:
- VPK track records are 0x18 bytes (`func_0021F9D8`..`func_0021FA98`: start, size, rate, loop points).
- `func_0021EF10` starts the next of 4 BGM variations on slot 0 in a cycle.
- `func_0021FB70` fades all streams; `func_0021E798` sets a slot's fade mode.
- Category volumes (options) are pushed to all slots by `func_0021A1C0` / `func_0021A328`.

## Radio chatter and subtitles (subsystem `radio`)

The radio is a rule-based selector over a message database loaded with the mission (0xA80 message records at 0x004BA570, `func_002507F0`):

1. **Requests**: six 4-entry ring queues:
   - event chatter `QueueRadioChatter` 0x002528C8 (accepted with the probability from table 0x00409DA8 by event, tested against **`rand`**);
   - speaker events `func_00252AF0`;
   - `func_00252B88`;
   - conditional chatter `func_00252C30` (uses `rand`);
   - scripted ids `QueueRadioMessage` 0x00252F80 (`func_00252FB8` picks base + `rand() % n`);
   - extra ids `func_00253000`.

   Callers: AI (`ai.md`), weapons, mission scripts and the mission-specific hooks.
2. **Matchers** (one per queue, run by `func_00251050`) turn requests into candidates in the 20-entry ring 0x004BA330 (16 bytes: message id, candidate kind, event kind, record pointer):
   - `func_00253158` (event chatter): collects up to 48 matching message indices by event kind, side (`func_002530E8`) and target class ranges, then one `rand() % count` picks one;
   - `func_00254930` (target chatter): up to 320 indices, skipping used records; ids above 0x452 are entered three times (triple weight); one `rand() % count`;
   - `func_00254340` (situation chatter): up to 80 indices; generic entries only within 1800 frames of the last pick and not yet played (played mask from `func_00255670`); one `rand() % count`;
   - `func_00253F88` (mission timer call-outs): builds a three-part message (0xA5F + value, 0xA7C + minutes, suffix 0xAA0–0xAA5 by remaining time); no `rand`;
   - `func_00254B98` (scripted ids, play-once list of 48) and `func_00254D40` (extra ids): no `rand`.

   Each `rand` call happens only when the matcher has at least one candidate for an event.
3. **Scheduler** `func_00256BC8` (per frame, from `func_00251050`):
   - sorts candidates by priority (record +9, `func_00254F00`);
   - drops messages played in the last 600 frames (`func_00255B38`/`func_00255B98`) and blocked pairs/speakers;
   - keeps a 16-entry pending list.
4. **Playback**: two voice channels on VAG slots 4/5 (`func_002557E0`, `func_00255F30`, `func_002565F0`, `func_00256910`), with a click SE, BGM/SE ducking (`func_00255D28`) and the portrait window (`func_00257D08`). `func_002565F0` starts a message: on success it sets the gap after the message to base + `rand() % range` from the radio parameters (a quiet-battle range and a busy-battle range) and writes the subtitle record (duration max(120, voice length / 210 + 180) frames). `func_00255F30` plays the click: a random variant (`rand() % 6`) on a normal start.
5. **Subtitles**: text layout `func_002515B8` (3 lines, drop shadow, inline icons) and the centred style `func_00258E48` (sequence timelines, `func_002593A0`). Shown unless the CAPTIONS option (0x002A15CC bit 1) hides them for messages with priority ≥ 2 (`func_00251C40`).
6. **Tutorial control hints** `func_00258690`: only when the loaded radio bank is the tutorial kind (first word of its first section 0x13; block pointer 0x00411868). Mission code requests icons by mask (`func_0017BFB0` → `func_00258400`); requested icons pulse.

**Determinism:** the radio calls `rand` (steps 1 and 2) during gameplay. AI and weapons use the random table instead, but `rand` is shared with the sky, replay camera and effect code, so the order of all `rand` calls inside the frame must stay exactly as in the original (`../game/random.md`).

## Text engine (subsystem `text`)

- Fonts are identified by the header magic `go18` (type 0) or `go28` (type 1) (`func_00251E48`).
- Text tables hold u16 strings ending with a code ≥ 0xFFFE (`func_00250930`, `func_00251F08`). The string table is at 0x004BCF70.
- Glyph codes of the message font (decoded from the messages on the disc, 2026-10-22; `tools/research/msgscan.py`): 0x0D space, 0x1D-0x26 `0`-`9`, 0x27-0x40 `A`-`Z`, 0x41-0x5A `a`-`z`, 0xB1 `(`, 0x0E `)`, 0x12 `-`, 0xA4 `,`, 0xA5 `.`, 0xA6 `:`, 0xA8 `?`; 0xFFFD new line; end 0xFFFE, or 0xFFFF when the next message continues the text (`SetMessageBoxText` shows up to 3 such lines). The glyph set is global (font tables in `.data`, `SelectFont`), so every page's messages can use every character.
- Glyph metrics: `func_00251D70`/`func_00251D90`/`func_00251DB8`. Layout with line wrap: `func_00252160` (glyphs uploaded from the font sheet into the text texture, new line code 0xFFFD, wrap every 16 glyphs, inline icons for codes 8–11). Upload of a message: `func_00252540`. Drawing: the GIF builders 0x002581C8–0x00258550.

## Rumble (subsystem `input`)

- Two pattern slots per player (0x004BDFD0), patterns from table 0x003F61A8. `RequestRumble` 0x002576C8 RequestRumble(id, strength, player) is used, for example, by the cannon-shell warning (`weapons.md`).
- `func_00257890` mixes the slots into the pad actuator request `func_001025C8` each frame and honours the VIBRATION option: the small motor follows the pattern, the large motor level is a distance ramp (kinds 0 and 3) or the pattern level, scaled by the record intensity for kinds 3, 7, 8 and 10, and is sent only when ≥ 0x30. `func_00257C28` stops everything.

## Things the port must keep

- One SOUND command batch and one VAG batch per simulated frame. Their contents follow from the calls above; the IOP-side behaviour is reimplemented in the audio backend (ADR-004).
- The `rand` calls of the radio system (determinism), and the separate sound LFSR.
- Frame-counted timers: 600-frame repeat block, fades, rumble patterns.

## Open points

- Mapping of the radio event ids to the probability table 0x00409DA8 (read from the executable at runtime; no copy).

## SOUND.IRX and VAG.IRX: IOP-side command handlers

The two game-owned IOP modules, imported into Ghidra as MIPS R3000 (`MIPS:LE:32:default`). They keep their original **symbol tables**, so function and data names below are Namco's own. The EE side (RPC clients, batches, data path) is in `../game/iop-interface.md`.

### Modules

| Module | `.iopmod` name | Version | Entry | `gp` | Text / data / bss |
|---|---|---|---|---|---|
| SOUND.IRX | `NAMCO_GV79_SOUND_TEST` | 0x101 | 0xA6C `start` | 0x9700 | 0x1670 / 0xC0 / 0x4840 |
| VAG.IRX | `AC4 VAG module` | 0x101 | 0x100 `start` | 0x9B90 | 0x1780 / 0x440 / 0xC0310 |
| LIBSD.IRX | `Sound_Device_Library` | 0x104 | 0x90 | 0xC9E0 | no symbols (stock Sony module) |

Both modules carry a `.mdebug` section (debug info). Ghidra's STABS analyzer could import types and line numbers from it, but it needs `stdump` from the Emotion Engine plugin.

#### How they are loaded in Ghidra

- The ghidra-mcp `import_file` with a language override loads files as raw binary, and without an override Ghidra picks the EE plugin's 64-bit language. Working method:
  1. `tools/irx_imports.py --image` writes each module's loaded image (program segments at their link address 0) to `out/irx/image/<NAME>.IRX`;
  2. the image is imported raw at base 0 as `MIPS:LE:32:default`;
  3. functions, import stubs and data labels are created from the module's own `.symtab` (`out/irx/<NAME>.IRX.symbols.csv`, `.ghidra_seed.json`).
- **Relocations:** IRX modules are linked at 0 and relocated by the IOP loader. At base 0 the stored code is already correct: every `jal` lands on the right function or import stub.
- **Imports:** each import stub (`jr ra; addiu zero, zero, N`) is named by the module's own symbol table, which agrees with the import table's (library, index) pairs (appendix).
- **`gp`:** the MCP has no tool to set a register value, so `gp`-relative accesses show as `unaff_gp ± offset`; the values are in the table above.

### SOUND.IRX (RPC 0x81000101)

`start` creates `sifrpc_thread` (priority 0x1C, stack 0x800). It enables IOP interrupts 0x24, 0x28 and 9 (SPU2 DMA/IRQ), registers RPC `0x81000101` with receive buffer `buffer` (0x1740, 0x2400 bytes; reply area 0x3B40, 0x600 bytes), and runs `sceSifRpcLoop`.

`sifrpc_server(fno, data)` (0xB90):

| fno | Action |
|---|---|
| 0 | `iopbuf = data[1]` (the EE's init parameter), then `iop_sound_init`: `sceSdInit(0)`, core attributes, master volume 0x3FFF on both cores, all voices routed (switch 0x1800/0x1A00 on, 0x1900/0x1B00 off), reverb off on both cores, clears the 48 voice-state bytes, starts the BD transfer helper (`init_TransBd`) |
| 1 | process a command batch `{count, records…}` and build the reply |

#### Command records (fno 1)

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

#### The 72-byte voice request (ids 1–4)

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

#### Reply (fno 1)

Word 0 = number of records (voice records + 2), then (`sifrpc_server` 0x0B90, checked against PCSX2 run 06, `../pcsx2/sessions.md` run 06):
- ids 1-4: `{id, request id, u8 tag, u8 status}` (status 1 = OK, 0xFF = error). Ids 6-9: `{id, -, u8 status at +8}`: word 1 (except id 6) and bytes 9-11 are **not written**. For id 6, word 1 = `sceSdGetAddr` of the lower of the two cores' effect start addresses, read right after `sceSdSetEffectAttr` (which writes the ESA register synchronously): ESA = EEA - (table[mode] * 8 - 2), table at LIBSD.IRX 0x4148 (modes 0-9: 0x2, 0x4D8, 0x3E8, 0x908, 0xDFC, 0x15BC, 0x1ED8, 0x3008, 0x3008, 0x780), EEA 0x001DFFFF (core 0) / 0x001FFFFF (core 1). The game's (core 1, mode 2) gives core 1 0x001FE0C0, so the reply is core 0's mode-0 0x001DFFF0 (PCSX2 SPU2 state, runs 06/09/10; `../port/validation.md` S4-7).
- a transfer-status record `{10, last finished ticket, status}` from `trans_bd` (base 0x16D0): the ticket is stored by `handler_DmaInt` when the DMA ends; `main_TransBd` runs after the reply of the batch that requested it, so that reply still has the previous ticket. `handler_DmaInt` (0x1300, the SD transfer interrupt) is the only writer of the finished ticket: a block-transfer stop `{0, 0, 0}` (`sceSdBlockTrans` mode 2) runs no DMA, so its ticket is never reported (PCSX2: one behind after every movie; `../port/validation.md` S4-7). Status = `sceSdBlockTransStatus(0,0) >> 24`, 0 in every measured reply;
- a voice-status record `{0xF, output mode, 48 status bytes}` (`get_ch_stat`): a voice SOUND keyed on (`iop_ch_state` 1) reports 1 until its ENDX bit (`sceSdGetSwitch(0x1700)`) is set; every other voice (state 0 or 4, or 1 after ENDX) reports 4 when its envelope ENVX (param 0x500) is non-zero, else 0. Voices 42-47 report 4 at every measured point (title, scene 6): they are the VAG.IRX stream voices, keyed on by VAG.IRX (not SOUND) and looping forever (confirmed by the code, S2a).
- The reply is built in **one IOP area** (0x3B40, 0x600 bytes) that is never cleared and copied back whole: bytes after the last record are leftovers of earlier replies and reach EE memory (the port's stand-in keeps the same area).
- fno 0 (`iop_sound_init`) writes only the count 2 into the reply area; its records go to IOP address 0 (s5 = 0).

VAG fno 1 reply (run 06, nothing streaming): word 0 stream area 0x0008DA40, words 5-8 = 1 (state idle), words 9-12 (ring index per channel) = 2, +0x50/+0x60 = 0x200. The fno 0 reply has ring indices 1 and words 5-8 = 0. Both follow from VagAllInit/VagMain (VAG.IRX section).

#### Movie audio

The EE movie player uses SOUND.IRX, not VAG.IRX:
- it borrows the IOP address `*0x00353BF8` (VAG channel 0's first ring segment) as a 2 × 0x4000-byte PCM double buffer (`func_00213CE8`, `func_00213D10` with `sceSifSetDma`);
- it starts the stream with command 0xA `{src = that buffer, dst = 0, size = 0x8000}` (`func_00213DC8`) → `sceSdBlockTrans(core 0, loop)`, and stops it with 0xA `{0, 0, 0}` (`func_00213E30`);
- it sets the input volume with command 0xD: `func_00213C50` resets the feeder with volume 0, `func_00213EA0` starts a fade and `func_00213F60` steps it each frame.

The PCM format is therefore the SPU2 core-0 **sound-data input**: 48 kHz 16-bit stereo, left and right in alternating 0x200-byte blocks (checked on the SMV data, `../formats/SMV.md`, and confirmed against the PCSX2 capture, `../port/validation.md` S3-5).
- The feeder refills by the transfer status of each SOUND reply (`{10, ticket, status}`, status = `sceSdBlockTransStatus >> 24`, stored at client +0x30 by `func_00217838` and read by `GetSoundTransferStatus` 0x00217848): the half of the 0x8000-byte ring the SPU2 is playing. When it differs from the half the reader filled last, the other half gets the next block (`StreamSmvReader`). Confirmed by the S3 PCSX2 batch (`../port/validation.md` S3-3, S3-5).
- Port (S3): the SOUND stand-in starts/stops a loop-mode block input in the SPU mixer (`SpuMixer::StartBlockInput`), applies command 0xD as the input volume, and reports the half in the status word.

### VAG.IRX (RPC 0x81000102)

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

#### Channel record (`vag_param`, 4 × 0x6C at 0x17D0; `next_vag_param` 4 × 0x6C at 0x1980)

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

### Appendix: import mapping

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

### Open

- SOUND `delay_timer` / `spu_register_timer` (hardware-timer based key-on/off spacing, H).
- Import of the `.mdebug` STABS info via the plugin's STABS analyzer.
