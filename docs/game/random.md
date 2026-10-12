# Random number sources

The game uses five independent random sources. All of them must be reproduced bit-exactly, including the order of calls inside a frame, for replays, demos, visuals and radio chatter to match the original.

| Source | State | Seeded | Users | Affects gameplay? |
|---|---|---|---|---|
| **Random table** `NextRandomFloat` 0x00100DC8 | cursor `D_00411780` (index & 0x1FF), 512 floats at 0x00411998 | table filled once at boot by `InitRandomTable` 0x00100D38; **cursor reset to `frame & 0x1FF` at the start of every gameplay frame** | AI, weapons, player, effect spawns and updates, map flicker, some effect draws | **yes** |
| **libc `rand`** (newlib LCG, multiplier 0x41C64E6D, increment 0x3039) | C runtime | boot constructor uses the default seed; `InitRandomTable` calls `srand(1)`, `srand(123456789)`; the sky generator calls `srand(seed)` per mission | sky, sun/moon jitter, replay auto-camera, radio chatter and timers, terrain noise table, a few effects | no direct gameplay use; affects radio choices and visuals |
| **VU0 R register** | VU0 `R` | `vrinit` in `InitEffects` 0x00154660 (flight start and replay start), seed from 0x0040E7F0 | `FirePlayerWeapons` 0x001249A0 (4 × `vrnext`, result unused) and missile trail segments `func_001DAC38` | no |
| **Effect LCG** `x = x · 0x10DCD + 1` | 10 words 0x00410BB0–0x00410C38 | static constructors (`n · 0xEE29 + 1`, `func_001E0A80`), then reseeded per effect record at draw time | effect draw functions | no |
| **Sound LFSR** (15-bit) | `D_00410D30` (`NextSoundRandom` 0x0021CD20); unused second copy `D_00410D1C` (`func_0021A080`, dead) | — | sound variant, pitch and volume variation | no |

## Random table

```
InitRandomTable 0x00100D38:  srand(1); srand(123456789);
                             for i in 0..511: table[i] = rand() * 4.656613e-10   ; 0x00411998
                             cursor = 0
NextRandomFloat 0x00100DC8:  v = table[cursor & 0x1FF]; cursor++; return v
SetRandomIndex  0x00100DB8:  cursor = n & 0x1FF
```

`FlightPlayFrame` 0x0010B150 calls `SetRandomIndex(flight object +0x165C)` (the simulation frame counter) as its first step. The values a unit receives therefore depend only on the frame number and on the order of `NextRandomFloat` calls inside that frame. Reads from draw code happen after the simulation in the same frame and do not leak into the next frame, because the cursor is reset. Outside `FlightPlayFrame` (menus, cutscene-only sub-states) the cursor is not reset.

`NextRandomFloat` has 67 callers. Gameplay users:
- AI: gun spread, crash pitch, ground wander points, ship speed, missile jinks, the periodic strike event (`../subsystems/ai.md`);
- weapons: flak positions, scatter-weapon blast positions (special-weapon kinds 5, 6, 7, 12), bullet damage from AI attackers (`../subsystems/weapons.md`);
- player: weapon fire spread (`FirePlayerWeapons`), landing gear, camera shake (`../subsystems/camera-hud.md`);
- mission setup (`SetupMissionUnits` 0x0010E758) and mission event code.

Effect spawn and update functions read the table inside the simulation step (`../subsystems/effects.md`). Effects therefore shift the values that later AI and weapon code of the same frame receives: **effects cannot be skipped, reduced or reordered without changing gameplay.**

Effect draw functions that also read the table, after the simulation of the frame: `func_001CFCA8` (5 reads), `func_001E46E0` (2), `func_001E6B50`, `func_001E9890`, `func_001EA840` (1 each). Because the cursor is reset at the start of the next `FlightPlayFrame`, these reads do not affect gameplay; they must still run in the original order so that the effects look the same. `func_001F09C8` (explosion setup, 2 reads) runs in the simulation phase.

## libc `rand`

39 callers in game code:
- boot: `InitRandomTable` 0x00100D38 (table fill), the static constructor `func_002596F8` (three calls with newlib's default seed before `InitRandomTable` reseeds: radio chatter timers at 0x004BE0B0);
- sky (draw phase): `func_00110630` (ring colours, twinkle), `func_00113548` / `func_001135C8` (star and cloud generation; these call `srand(seed)` with seeds from the mission render settings);
- `func_001145E8` (sun/moon camera jitter, draw phase);
- terrain: the gradient noise table, 255 calls once per boot when the sea object is allocated (`func_00137550` from `func_001381F8`, guarded by `D_00410690`); the table is read only by dead code, but the calls advance the sequence;
- replay auto-camera: style choice (`func_001756C0`, `func_00175890`) and the style functions on each cut: one call in 0x00172098, 0x00172450, 0x00172850, 0x00174058; two in 0x00173670 and 0x00173A98 (the first result discarded) (`../subsystems/mission-flow.md`);
- radio: event chatter probability (`func_00252C30`, one call when a report gets that far), the matchers for queues 1, 2 and 4 (`func_00253158`, `func_00254930`, `func_00254340`: one call per event with candidates, index `rand() % count`), the click SE variant (`func_00255F30`, `rand() % 6`), the post-message gap (`func_002565F0`), conditional chatter and scripted variants (`../subsystems/sound.md`); `UpdateChatterTimers` 0x00259588 and `func_002594F0` (periodic chatter timers);
- effects: `func_0019C888`, `func_0019FF90`, `func_001A1FA8`, `func_001BFEF0`, `func_001C0010`, `func_001C9838`, `func_001CD400`, `func_001CF398`, `func_001CFCA8`, `func_001D0B40`, `func_001D34F0`, `func_001D3950` and others in `../subsystems/effects.md`;

No AI, weapon or rule code calls `rand` directly. `rand` calls from drawing (sky, sun jitter, effect draws) shift the sequence that the radio uses later, so drawing must run exactly once per simulated frame, in the original order (ADR-002).

## Consequences for the port

- Implement newlib `rand`/`srand` exactly (the C runtime `rand` is game state, `../port/skeleton-readiness.md`).
- Keep the random table, its cursor reset and every `NextRandomFloat` call in the original order.
- Emulate the VU0 R register (`vrinit`, `vrnext`) in `vu0_math.h`.
- Keep the effect LCG and the sound LFSR separate from the other sources.
- Run the game's draw-list build exactly once per simulation step (ADR-002).

## Appendix: every caller of `NextRandomFloat`

Generated from Ghidra cross-references on 2026-10-11 (`../port/validation.md` V-8): **67 functions, 152 call sites**. Call sites, not calls per frame: loops and branches decide how many reads happen. Every function below must keep its reads in the original order.

| Function | Name | Subsystem | Call sites |
|---|---|---|---|
| 0x0010E758 | `SetupMissionUnits` | mission | 3 |
| 0x0011BE30 | `UpdateUnitStates` | weapons | 1 |
| 0x00121370 | `UpdateLandingGear` | flight | 1 |
| 0x001249A0 | `FirePlayerWeapons` | weapons | 4 |
| 0x00125B70 | `func_00125B70` | camera | 9 |
| 0x0012CBD8 | `func_0012CBD8` | ai | 1 |
| 0x0012E1F8 | `func_0012E1F8` | ai | 1 |
| 0x0012F2D0 | `UpdateAIWeapons` | ai | 2 |
| 0x00130BF8 | `UpdateGroundUnitWeapons` | ai | 9 |
| 0x001313D0 | `func_001313D0` | ai | 2 |
| 0x00131500 | `func_00131500` | ai | 2 |
| 0x00131DC8 | `func_00131DC8` | ai | 3 |
| 0x001329B0 | `func_001329B0` | ai | 1 |
| 0x00133228 | `func_00133228` | ai | 2 |
| 0x00135008 | `func_00135008` | ai | 3 |
| 0x0014F470 | `func_0014F470` | mission | 1 |
| 0x00151C10 | `func_00151C10` | weapons | 1 |
| 0x00152020 | `func_00152020` | weapons | 2 |
| 0x00152300 | `func_00152300` | weapons | 2 |
| 0x0016B7C8 | `func_0016B7C8` | hud | 2 |
| 0x00179D70 | `func_00179D70` | mission | 7 |
| 0x0017F6D8 | `func_0017F6D8` | effects | 1 |
| 0x00181CB8 | `func_00181CB8` | effects | 2 |
| 0x00182948 | `func_00182948` | effects | 2 |
| 0x001859E0 | `func_001859E0` | effects | 1 |
| 0x00185A88 | `func_00185A88` | effects | 2 |
| 0x00186928 | `func_00186928` | effects | 1 |
| 0x00188A50 | `func_00188A50` | effects | 2 |
| 0x00188B18 | `func_00188B18` | effects | 2 |
| 0x00189B88 | `func_00189B88` | effects | 2 |
| 0x0018AF40 | `func_0018AF40` | effects | 1 |
| 0x0018EC70 | `func_0018EC70` | effects | 2 |
| 0x00196908 | `func_00196908` | effects | 2 |
| 0x00197A98 | `func_00197A98` | effects | 3 |
| 0x00199788 | `func_00199788` | effects | 2 |
| 0x0019C278 | `func_0019C278` | effects | 9 |
| 0x0019D9C0 | `func_0019D9C0` | effects | 12 |
| 0x001BDAD8 | `func_001BDAD8` | effects | 3 |
| 0x001C9838 | `func_001C9838` | effects | 2 |
| 0x001CD400 | `func_001CD400` | effects | 1 |
| 0x001CFCA8 | `func_001CFCA8` | effects | 5 |
| 0x001D0A78 | `func_001D0A78` | effects | 1 |
| 0x001D0B40 | `func_001D0B40` | effects | 1 |
| 0x001D34F0 | `func_001D34F0` | effects | 1 |
| 0x001D3950 | `func_001D3950` | effects | 1 |
| 0x001D4110 | `func_001D4110` | effects | 4 |
| 0x001DAC38 | `func_001DAC38` | effects | 1 |
| 0x001DDBD0 | `func_001DDBD0` | effects | 1 |
| 0x001E45C8 | `func_001E45C8` | effects | 2 |
| 0x001E46E0 | `func_001E46E0` | effects | 2 |
| 0x001E6B50 | `func_001E6B50` | effects | 1 |
| 0x001E7DC0 | `func_001E7DC0` | effects | 4 |
| 0x001E9708 | `func_001E9708` | effects | 1 |
| 0x001E9890 | `func_001E9890` | effects | 1 |
| 0x001EA840 | `func_001EA840` | effects | 1 |
| 0x001EB440 | `func_001EB440` | effects | 1 |
| 0x001EC458 | `func_001EC458` | effects | 1 |
| 0x001ED2E8 | `func_001ED2E8` | effects | 1 |
| 0x001F09C8 | `func_001F09C8` | effects | 2 |
| 0x001F37E0 | `func_001F37E0` | effects | 1 |
| 0x001F3910 | `func_001F3910` | effects | 1 |
| 0x001F6880 | `func_001F6880` | effects | 1 |
| 0x001FC6C0 | `func_001FC6C0` | effects | 1 |
| 0x002033F0 | `func_002033F0` | effects | 1 |
| 0x00206588 | `func_00206588` | effects | 1 |
| 0x0020A4B8 | `func_0020A4B8` | effects | 2 |
| 0x0020C2A0 | `func_0020C2A0` | effects | 1 |
