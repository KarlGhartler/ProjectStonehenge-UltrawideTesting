# EE-side sound, radio and rumble (priority 2, step 8)

Scope: everything the EE does to make sound: the voice/request API on top of SOUND.IRX, sound banks, engine and ambient sounds, positional and 3D sounds, the VAG streaming client (BGM and voices), the radio-chatter system with its subtitles, and pad rumble. IOP side: `iop-interface.md`, `iop-sound.md`. Every function has a row in `coverage-functions.csv` (subsystems `sound`, `radio`, `text`, `input`).

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
| `func_0021D2E0` PlaySound(bank, sound, l, r, voice) | type 1 | core `func_0021D328`: variant pick, category volume (`func_0021CD08`, table 0x0042CA50 from the BGM/SE/voice options), pan, linked sounds |
| `func_0021D500` | type 1 | variant with random volume/pitch variation |
| `func_0021D830` SetSoundVolume(l, r, voice) | type 2 | restarts the voice when the pan mode changes |
| `func_0021DE30` SetSoundPitch(note, fine, voice) | type 2 mode 4 | |
| `func_0021E058` StopSound(voice) | type 4 | |
| `func_0021E0E8` | command 5 | stop by masks (`func_002197E8` stops everything) |
| `func_0021CE38` / `func_0021CEA0` | commands 0xB / 0xC | volume pairs; 0xC has no callers |
| `func_0021CFE8` / `func_0021D018` | commands 8 / 9 | channel release |

Convenience wrappers: `func_002196A8` PlayMenuSe(bank, sound) on a fixed voice (50 callers), `func_002196E0`, `func_00219728`, `func_00219758`, `func_00219798`.

**Randomness:** variant, pitch and volume variation use the sound module's own 15-bit LFSR `func_0021CD20` (state `D_00410D30`), **not** `rand`. It never affects the game state, but the port must keep it separate so that it does not disturb the `rand` sequence.

## Sound banks

- Bank table (32 entries): ids 0x0033E298, headers 0x0033E194, SPU addresses 0x0033E214. Bank 0 starts at SPU 0x5A10; each next bank is packed after the previous one (`func_00218A00`).
- Loading (scene 6 and the menus): `LoadSoundBankData` (H) 0x00219B90 reads the DATA header entry, `StreamBdEntryToIop` (H) 0x00219C80 reads the BD.CDP body into IOP RAM in 0x30000-byte pieces and asks SOUND.IRX to move each piece to SPU RAM (command 0xA, tickets polled with `func_00217858`).

## Engine and ambient sounds

- Per player: 7 engine layers (voice records at 0x00423880 + player × 0x318 + layer × 0x48). Volume and pitch come from curves indexed by throttle × 10 with linear interpolation (`func_002159E0`, `func_00215A90`), plus speed terms (`func_00215B38`, `func_00215B90`). The per-frame mixer is `func_00215D20` (partly: skimmed).
- Start/stop: `func_002166D0` / `func_00216728`; per-frame entry `func_002166A8`; per-player parameters `func_00216B80` (called from the player update, `flight.md`).
- Ambient channel voice from the aircraft-frame velocity (`func_002167B0`, HYPOTHESIS: wind), an extra voice in missions 6, 9 and 10 (`func_00216A80`).

## Positional and 3D sounds

- `PlayEventSe` `func_00219188` (27 callers): one-shot sounds by event id (explosions, impacts, …) at a position, grouped into voice pools. Per-frame attenuation and pan: `func_00219330` (partly).
- 3-slot positional table `func_00216CE0`; queued positional lists (2 × 8) `func_0021AE30` / `func_0021AF18`.
- **Listener** per player (0x0042AA10 + player × 0x1020):
  - nearest units come from `func_0011DC20` (`func_0021B3A8`), nearest missiles from `func_0014D4D0` (`func_0021B3D8`);
  - `func_0021B408` (partly, 4.3 KB): engine voices of other aircraft (4 slots) with distance attenuation, pan and **Doppler** (`func_0021A8E8`: pitch from relative speed);
  - `func_0021C4F8`: missile whoosh voices (5 slots, bank 10 sound 0x19);
  - `func_0021C760` / `func_0021CA80`: fly-by sounds by distance and speed band.
- Volume/pan helpers: `func_00219A40` (distance gain and angle), `func_00219998` (clamped gain curve), `func_0021A800` (volume × pan).

## Streams (VAG.IRX client)

Covered in `iop-interface.md`. Additions:
- VPK track records are 0x18 bytes (`func_0021F9D8`..`func_0021FA98`: start, size, rate, loop points).
- `func_0021EF10` starts the next of 4 BGM variations on slot 0 in a cycle.
- `func_0021FB70` fades all streams; `func_0021E798` sets a slot's fade mode.
- Category volumes (options) are pushed to all slots by `func_0021A1C0` / `func_0021A328`.

## Radio chatter and subtitles (subsystem `radio`)

The radio is a rule-based selector over a message database loaded with the mission (0xA80 message records at 0x004BA570, `func_002507F0`):

1. **Requests**: six 4-entry ring queues:
   - event chatter `func_002528C8` (accepted with the probability from table 0x00409DA8 by event, tested against **`rand`**);
   - speaker events `func_00252AF0`;
   - `func_00252B88`;
   - conditional chatter `func_00252C30` (uses `rand`);
   - scripted ids `func_00252F80` (`func_00252FB8` picks base + `rand() % n`);
   - extra ids `func_00253000`.

   Callers: AI (`ai.md`), weapons, mission scripts and the mission-specific hooks.
2. **Matchers** (one per queue; `func_00253158`, `func_00253F88`, `func_00254340`, `func_00254930`, `func_00254B98`, `func_00254D40`; partly, skimmed) turn requests into candidate messages by event, speaker side (`func_002530E8`, `func_00253110`) and conditions. Several of them call `rand`.
3. **Scheduler** `func_00256BC8` (per frame, from `func_00251050`):
   - sorts candidates by priority (record +9, `func_00254F00`);
   - drops messages played in the last 600 frames (`func_00255B38`/`func_00255B98`) and blocked pairs/speakers;
   - keeps a 16-entry pending list.
4. **Playback**: two voice channels on VAG slots 4/5 (`func_002557E0`, `func_00255F30`, `func_002565F0`, `func_00256910`), with a beep SE, BGM/SE ducking (`func_00255D28`) and portrait animation (`func_00257D08`).
5. **Subtitles**: window `func_00258690` and text `func_002515B8`. Shown unless the SUBTITLE option (0x002A15CC bit 1) hides them for messages with priority ≥ 2 (`func_00251C40`).

**Determinism:** the radio calls `rand` (steps 1 and 2) during gameplay, so it is part of the global `rand` sequence that the AI and weapons also use (`ai.md`). The order of these calls inside the frame must stay exactly as in the original.

## Text engine (subsystem `text`)

- Fonts are identified by the header magic `go18` (type 0) or `go28` (type 1) (`func_00251E48`).
- Text tables hold u16 strings ending with a code ≥ 0xFFFE (`func_00250930`, `func_00251F08`). The string table is at 0x004BCF70.
- Glyph metrics: `func_00251D70`/`func_00251D90`/`func_00251DB8`. Layout with line wrap: `func_00252160` (partly). Drawing: `func_00252540` (partly) and the GIF builders 0x002581C8–0x00258550.

## Rumble (subsystem `input`)

- Two pattern slots per player (0x004BDFD0), patterns from table 0x003F61A8. `func_002576C8` RequestRumble(id, strength, player) is used, for example, by the cannon-shell warning (`weapons.md`).
- `func_00257890` (partly) mixes the slots into the pad actuator request `func_001025C8` each frame and honours the VIBRATION option. `func_00257C28` stops everything.

## Things the port must keep

- One SOUND command batch and one VAG batch per simulated frame. Their contents follow from the calls above; the IOP-side behaviour is reimplemented in the audio backend (ADR-004).
- The `rand` calls of the radio system (determinism), and the separate sound LFSR.
- Frame-counted timers: 600-frame repeat block, fades, rumble patterns.

## Open points

- Engine mixer `func_00215D20`, listener `func_0021B408`, radio matchers and scheduler: skimmed only (partly). They are translated as code; their exact rules are needed only for documentation.
- Mapping of the radio event ids to the probability table 0x00409DA8 (read from the executable at runtime; no copy).
