# AI: enemy and friendly units

Every non-player unit. Modules 0x0012A488–0x0012F2D0 (aircraft) and 0x0012FC08–0x00136240 (ground and naval units, cannons, scripted objects, unit snapshots), plus the per-frame unit helpers at 0x0011D580–0x0011D988. Every function has a row in `../port/coverage-functions.csv`.

## Unit kinds

`SetupMissionUnits` 0x0010E758 (mission block 22) creates each unit and sets unit+0x54:

| unit+0x54 | Kind | State object (unit+0x44) | Per-frame update (from `UpdateUnitStates`) |
|---|---|---|---|
| 0 | player aircraft | 0x4C0 (`flight.md`) | `UpdatePlayerAircraft` |
| 1 | AI aircraft | 0x110, `CreateAIAircraftState` 0x0012A6B8 | `UpdateAIAircraft` 0x0012A880 |
| 2 | ground / naval / structure | 0xD0, `CreateGroundUnitState` 0x0012FC08 | `UpdateGroundUnit` 0x0012FD28 |

The update runs only for units with flags 0x10000000 (in play) and 0x100 (active). While a unit is being updated, its pointers live in gp globals: aircraft `D_004117AC` unit, `D_004117B0` state, `D_004117B4` AI parameter record (unit+0x40), `D_004117B8` aircraft performance record (state+0x00); ground `D_004117BC` unit, `D_004117C0` state, `D_004117C4` record.

### Unit fields used by AI (unit struct 0xA0, full layout in `mission-flow.md`)

| Offset | Use |
|---|---|
| +0x40 | AI parameter record (from the mission data) |
| +0x48 | side bits & 3 (0 = player side); bit 31 and 0x1000000/0x2000000 (air / ground target classes) |
| +0x4C | state flags: 0x1000000 alive, 0x10000000 in play, 0x200000 just destroyed, 0x20000 / 0x40000 distance tables valid, 0x80000000 finished (wreck done) |
| +0x50 | hit flags; AI sets 0x1000 (being shot at by a gun) and 0x2000 (missile lock warning) on its target |
| +0x55 | unit index |
| +0x5E | HP (9999 = invulnerable) |
| +0x62 | current target unit index (0xFF none) |
| +0x63 | unit that last hit this unit |
| +0x66 / +0x67 | index of a missile / projectile tracking this unit |
| +0x68 | number of enemies within detection range |
| +0x69 | unit type id |
| +0x6A | ground class (2..10) |
| +0x6C | frames since the target was chosen |

### AI parameter record (unit+0x40)

| Offset | Use |
|---|---|
| +0x0B | bit 0x40 may attack air units, bit 0x80 may attack ground units; low nibble = ground target mode |
| +0x0C | detection range |
| +0x14 / +0x18 | altitude band (y grows downwards: +0x14 is the lowest allowed, +0x18 the highest) |
| +0x22 | attack pattern index (mission table +0x58, 12-byte entries) |
| +0x23 | evade pattern index (mission table +0x5C, 0x18-byte entries) |

### Mission data tables used (pointer table `D_004104CC`)

| Offset | Contents |
|---|---|
| +0x04 | placement records, 0x18 bytes per unit; +0x04..+0x0C position, +0x16 route index |
| +0x50 | manoeuvre records, 12 bytes: `u8 manoeuvre, u8 param, s16 a, b, c, d, speed` |
| +0x54 | manoeuvre sets, 0x20 bytes: 16 manoeuvre ids + 16 cumulative percentages |
| +0x58 | attack patterns, 12 bytes: +0x04 duration, +0x08/+0x09/+0x0A manoeuvre set for far (> 16384), middle (> 8192), near |
| +0x5C | evade patterns, 0x18 bytes: +0x00 trigger time, +0x04 duration, +0x08 8 manoeuvre ids, +0x10 8 percentages |
| +0x60 | route commands, 0x18 bytes: `u8 type, u8 p[3], s32 a, b, c, d, e` |
| +0x64 | routes, 0x40 bytes = 32 u16 command indices |

Block mapping: `../formats/MISSION.md`.

### Route entries

`FetchAIRouteCommand` 0x0012B9B0 (aircraft) and the same code inside `UpdateGroundUnit` read entry `state+0x18` of the unit's route when the "next command" bit (aircraft +0x19 bit 7, ground +0x41 bit 7) is set:

| Entry | Meaning |
|---|---|
| 0x0000–0xFFDD | command index into +0x60 |
| 0xFFDE | run the unit's destroy scripts, then remove it (HP 0, alive flag cleared, off radar) |
| 0xFFDF | remove the unit |
| 0xFFE0–0xFFFE | jump to route entry (value & 0x1F) |
| 0xFFFF, or route index 0xFF | end: aircraft patrol their placement position (radius 0x8000); ground units wait 108000 frames |

## AI aircraft (`UpdateAIAircraft` 0x0012A880)

Per frame, in this order:

1. **Kill handling and radio**: when hit by a player (+0x50 0x20000) or destroyed (+0x4C 0x200000): kill message, score/radio (`ShowCenterMessage` 0x0016DDB8, `QueueRadioChatter` 0x002528C8, `func_0011E560`), destruction explosion (`func_0011E5E0` → `SpawnMovingExplosion`). Mission-specific hooks: mission 1 type 0x34, mission 7 type 0x12 (sets mission flags 0x280000), mission 0xB type 0x32, mission 0x12 types 0x3B and 0x11.
2. `DetectAIEvents` 0x0012B1E0 sets reaction flags in state+0x10, predicting the position 300 frames ahead:
   - 1: below the terrain or the altitude band ahead;
   - 2: above the band;
   - 4: outside the map (±262144);
   - 8: outside the area radius (state+0xC0) around the anchor (+0xB0, or unit +0xCC);
   - 0x10: attack timer expired / attack pattern running;
   - 0x20: being hit (+0x50 & 0x13010000) or evade pattern running;
   - 0x40: mission-wide retreat flags (`D_004104F0+0x10` bits 4/8, or flag 0x80000 for type 0x12).
3. `UpdateAITarget` 0x0012B540 (only if the record allows air or ground attacks): every 64 frames (phase = unit index) drops a target held for more than 20 s and picks the nearest enemy within detection range; players are taken first. Every 4 frames computes the target vector in the body frame (+0x90), distance (+0xA4) and cos of the angle off the nose (+0xA8). Radio call once when an enemy closes within 32768 of a friendly.
4. **Command layer** (state+0x1B, from the route). If reaction flags are set, `SelectAIReaction` 0x0012B8A0 turns the highest-priority flag (order 1, 0x40, 4, 2, 8, 0x20, 0x10) into a behaviour instead. Command 6 clears the flags.

   | Command | Function | Meaning |
   |---|---|---|
   | 0 | 0x0012BC90 | patrol an area (x, y, z, radius); ends after N frames or when N enemies are in range |
   | 1 | 0x0012BE78 | fly to an absolute point, arrive radius, speed |
   | 2 | 0x0012C0B8 | escort a unit for N frames |
   | 3 | — | no handler: keeps the current behaviour |
   | 4 | 0x0012BD88 | patrol around the current position |
   | 5 | 0x0012C190 | straight flight for N frames |
   | 6 | 0x0012C230 | kinematic move to a point (manoeuvre 0x12) |
   | 7 | 0x0012BF88 | fly to a point relative to the current position |
   | 0xFF | 0x0012BBE0 | end of route: patrol the placement point |

5. **Behaviour layer** (requested +0x3E, current +0x3D; a change resets the step counter +0x3F and the timer +0x38):

   | Id | Function | Behaviour |
   |---|---|---|
   | 0 | 0x0012C330 | patrol (straight flight, 660 frames) |
   | 1 | 0x0012C358 | fly to point +0xD0 |
   | 2 | 0x0012C3A0 | chase the target (64 above it), match its speed |
   | 3 | 0x0012C428 | attack pattern: manoeuvre set by distance to the player, choice by percentage |
   | 4 | 0x0012C678 | evade pattern |
   | 5 | 0x0012C8A8 | return to the altitude band |
   | 6 | 0x0012C970 | destroyed: choose a crash manoeuvre |
   | 7 | 0x0012CA40 | retreat to the nearest map edge, leave play beyond ±294912 |
   | 8 | 0x0012CDB0 | scripted (route commands 5/6 drive the manoeuvre) |
   | 9 | 0x0012CDB8 | formation on player 0: slot by unit index % 5, match the player's speed |

6. **Manoeuvre layer** (requested +0x46, current +0x45; a change resets the step +0x47, timer +0x40 and the rate scales +0x80/+0x84/+0x88). Each manoeuvre is a short step sequence that sets target angle deltas (pitch +0x70, yaw +0x74, roll +0x78) and finishes by setting state+0x44 bit 7. `StartAIManeuver` 0x0012A488 loads a 12-byte manoeuvre record (angles in degrees converted to radians, speed scaled by the aircraft's max speed).

   | Id | Function | Manoeuvre |
   |---|---|---|
   | 0 | 0x0012CFA8 | none |
   | 1 | 0x0012CFB0 | straight flight for N frames |
   | 2 | 0x0012D058 | level wings, level nose, hold |
   | 3 | 0x0012D4F8 | banked turn for N frames |
   | 4 | 0x0012D6F8 | bank, then pull up to a fixed pitch |
   | 5 | 0x0012D7D0 | loop with roll-out |
   | 6 | 0x0012DA90 | recover attitude, then bank/pull/level |
   | 7 | 0x0012D920 | bank, pull, roll level, pitch level |
   | 8 | 0x0012DC90 | coupled pitch and roll |
   | 9 | 0x0012DD50 | 10-step sequence with deceleration |
   | 0x10 | 0x0012E1D0 | steer to point (`SteerAIToPoint` 0x0012E618, or 0x0012E7F0 for aircraft flag bit 0) |
   | 0x11 | 0x0012D150 | break turn away from an incoming missile |
   | 0x12 | 0x0012D320 | kinematic move: velocity set directly, limited yaw rate |
   | 0x20 | 0x0012E1F8 | crash: fall with smoke, ground explosion 0xB5 |
   | 0x21 | 0x0012E3C8 | crash: tumble and sink |
   | 0x22 | 0x0012E448 | crash: fall, ground explosion or mid-air explosion 0xC2 after a delay |
   | 0x23 | 0x0012E5C0 | crash: immediate mid-air explosion |

7. `UpdateAIRotation` 0x0012EC08: per axis, the angular rate (+0x60/+0x64/+0x68) accelerates towards the remaining angle, limited by the performance record (+0x18 pitch up, +0x1C pitch down, +0x2C yaw, +0x24 roll), and is halved when close. The remaining angle is reduced by the rate and the Euler angles are rotated in the body frame (`func_00102EF0`). Pitch authority drops by a fatigue factor after state+0xF0 frames (600/900/1200 by difficulty `game[0x28]`). Unit+0x56 bit 0 marks pulling at the limit (vapour effect).
8. `UpdateAISpeed` 0x0012F120: speed mode +0xE0 (0 keep, 1 cruise +0x0C, 2 max +0x04, 3 min +0x08, 4 commanded +0xE4, 5 match the target) gives a target speed, clamped to the record's min/max; the body-frame forward speed accelerates towards it by +0x10.
9. `UpdateAIWeapons` 0x0012F2D0 (only with mission flag 0x100 "enemies may fire"): two slots (+0x04/+0x05 kind, +0x08/+0x0C weapon records, counters +0xF8/+0x100):
   - gun (1): needs unit flag 0x1000 or mission 0x11; fires every N frames inside range and cone, random spread (`FireBullet` 0x0014C8F0), bursts; sets 0x1000 on the target;
   - missile (2): lock counter while the target stays in range and cone, sets 0x2000 (warning) on the target, radio calls, fires `FireMissile` 0x0014D8B0 when locked and reloaded;
   - bomb (5): ballistic fall time to the terrain, drops when the predicted impact is within radius of one of two target points (`DropBomb` 0x0014EAE8).

## Ground and naval units (`UpdateGroundUnit` 0x0012FD28)

1. Kill handling like the aircraft, plus destruction effects by unit type (spawns 0x001D32A0, 0x001BFEF0, 0x00188A50, 0x001979F0; `effects.md`), with replay events.
2. Route command (+0x43) when alive:

   | Command | Function | Meaning |
   |---|---|---|
   | 0 | 0x001313D0 | patrol area, random point ±2048 |
   | 1 | 0x00131628 | move to point |
   | 2 | 0x00131708 | follow a unit at a speed for N frames |
   | 3 | 0x00131778 | wait N frames |
   | 4 | 0x00131500 | patrol around the current position |

3. `UpdateGroundUnitTarget` 0x00130758: every 64 frames picks a target by mode (record +0x0B & 0xF: 1 = farthest in range, 2–4 = 2nd–4th nearest), then each frame computes the target vector in the unit frame (+0x30) and distance (+0x84), with optional lead for guns.
4. Death animation (progress +0xC0 to 1.0) by type: 0x72, 0x7D, 0x84 have explosion sequences, others a plain fade.
5. Class handler (+0x6A):

   | Class | Function | Unit |
   |---|---|---|
   | 2 | 0x001317A8 | fixed launcher, instant aim (type 0x4D bobs on water; type 0x43 rises at mission time 0x4E0C/0x4FB0 frames) |
   | 3 | 0x00131A30 | turret with yaw/pitch limits and turn rate |
   | 4 | 0x00131DC8 | ground vehicle: wander, terrain follow, turret |
   | 5 | 0x001329B0 | ship |
   | 6 | 0x00132D18 | part mounted on a parent unit |
   | 7 | 0x00133228 | vehicle with gravity when destroyed |
   | 8 | 0x00133BB0 | large cannon with an HP pool, at most 2 firing at once (fire slots 0x00410674). HYPOTHESIS: the Stonehenge railguns |
   | 9 | 0x00134288 | cannon sub-parts sharing the parent's HP pool; collapse animation 0x00135848 |
   | 10 | 0x00134810 | scripted objects moved by mission flags |

6. `UpdateGroundUnitWeapons` 0x00130BF8 (mission flag 0x100): kind 1 AA gun, 2 SAM (lock time, warning flag 0x2000), 3 flak bursts near the target's predicted position, 4 artillery/ground shells (four projectile kinds 0x0014E3C8/0x0014E4A8/0x0014E588/0x0014E700).

## Other unit-level AI helpers

- `func_0011D988`: called from the scene-2 frame functions. Escort/target unit handling for player info +0x88, and a round-robin "AI tick" over 1/16 of the units each frame (phase = mission timer).
- `func_0011D580`: group target selection (+0x58/+0x5A); `func_0011D738`: special behaviour by unit class (fire effects, …).
- `func_00135008`: a periodic three-zone strike event (state in gp 0x00410670, skipped during scripted flight). State 0 waits 300 frames, then picks 3 zone ids from table 0x002A2618 at (simulation frame & 0xF) and 3 random target points (3 `NextRandomFloat` reads each). State 1 drifts the points towards the centre and activates up to 3 strikes, one every 18 frames, each an explosion column (`func_001FF6C0`, variant 10) and replay event 0xB. After 121 frames, state 2 sets the zone-hit bits at +0x45 at frames 60, 120 and 180. Class 10 scripted objects of type 0x4C (`func_00134810`) read these bits and set their state flag 0x1000 when their zone (+0x44) is hit.
- Mission 0x12: `func_00135C68`/`func_00135C90` track the closest approach of type-0x3B units and play radio messages at distance thresholds.

## Randomness and determinism

The AI uses two sources:
- **frame counter arithmetic**: `(flight object +0x165C frame counter + unit index) % 100` against pattern percentages; `% 3` for crash types; 64-frame and 4-frame phases (fully deterministic);
- **the random table** (`NextRandomFloat` 0x00100DC8): gun spread, crash pitch, ground wander points, ship speed, missile jinks, flak positions, the strike event.

The random table cursor is reset from the frame counter each gameplay frame, so the call order inside the frame decides the values (`../game/random.md`).

## Unit snapshots (replay)

Units and players are snapshotted for replays (`mission-flow.md`, replay). Player animation inputs are **quantised to u8 (×255)** in the snapshot, so a restored replay starts from slightly different values than the live game; the port must keep this quantisation.

## Things the port must keep

- The update order: units in table order, AI phases by unit index.
- y axis points down; all altitude comparisons are reversed.
- Mission-specific hooks keyed on mission id and unit type (listed above): they are game logic, not data.
- Integer division traps (`trap 7`) on a zero divisor in weapon records: unreachable with the shipped data; the port can assert.

## Open points

- Identity of class 8/9 cannons (HYPOTHESIS: Stonehenge) and of the scripted object types in class 10: confirm by mission when the mission data parser covers unit types.
