# EE-side sound, radio and rumble

Everything the EE does to make sound: the voice/request API on top of SOUND.IRX, sound banks, engine and ambient sounds, positional and 3D sounds, the VAG streaming client (BGM and voices), the radio-chatter system with its subtitles, and pad rumble. EE↔IOP transport: `../game/iop-interface.md`; IOP side and command table: `iop-sound.md`. Every function has a row in `../port/coverage-functions.csv` (subsystems `sound`, `radio`, `text`, `input`).

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
- **Setup at mission load** (S5a, `mission-load.md`). Per player p: 11 layer voice records of 0x48 bytes at 0x00423880 + p · 0x318 (byte +0xE = voice channel, 0xFF = none); per-player words at 0x00410C88, 0x00410C90, 0x00410C98, 0x00410CA0, 0x00410CA8, 0x00410CB0, 0x00410CB8, 0x00410CC0 (+4p each); curve set 0x00423F50 + p · 0x10E; bank index 0x00410CC8 + 4p; u16 0x004117F8 + 2p; ambient record 0x0033D7C0 + p · 0xC0 (fields used here: +0x50 second voice record, +0x54 and +0xB4/+0xB8 cleared, +0xA0 aircraft id, +0xA4 parameter, +0xA8 bank index, +0xAC BD bank id, +0xB0 curve pointer, +0x9C cleared).
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

Slots, commands and the per-frame batch: `../game/iop-interface.md` and `iop-sound.md`. EE-side details:
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
