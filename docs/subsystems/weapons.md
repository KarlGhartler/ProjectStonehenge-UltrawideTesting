# Weapons, damage and collision

Every projectile in flight, how hits become damage, and the terrain/unit collision queries. Code: projectile module 0x0014B9D8–0x00154638, terrain queries 0x0013D590–0x0013EA08, segment tests 0x0017C020–0x0017CB70, and the damage step in `UpdateUnitStates` 0x0011BE30 (`mission-flow.md`). Firing logic for the player is in `flight.md` (`FirePlayerWeapons`, `UpdatePlayerTargeting`), for AI units in `ai.md`.

## Projectile pools

`CreateProjectilePools` 0x0014B9D8 allocates one 0x8A50-byte block from the work heap (pointer gp `D_004106AC`), plus 0x160 bytes at `D_004106B0`. `ResetProjectiles` 0x0014BA10 clears it at mission start.

| Kind | Slots × size | Offset | Occupancy | Slot choice | Spawn |
|---|---|---|---|---|---|
| bullets | 256 × 0x40 | 0x0000 | 4 × u64 at +0x8A00 | player 0: 0–63, player 1: 64–127, others: 128–255; round-robin cursors +0x8A4E/+0x8A4F/+0x8A4D, first free slot | `FireBullet` 0x0014C8F0 |
| missiles | 32 × 0x60 | 0x4000 | u32 +0x8A28 | ring counter +0x8A44, **overwrites the oldest** | `FireMissile` 0x0014D8B0 |
| special weapons | 32 × 0x80 | 0x4C00 | u32 +0x8A2C | ring counter +0x8A46, overwrites | `FireSpecialWeapon` 0x0014FA88 |
| flak bursts | 64 × 0x30 | 0x5C00 | u64 +0x8A20 | ring counter +0x8A48, overwrites | `SpawnFlakBurst` 0x0014DE08 |
| shells | 64 × 0x40 | 0x6800 | u64 +0x8A30 | first free from cursor +0x8A4A; none free = not fired | `FireShell` 0x0014E1C8 |
| bombs | 64 × 0x40 | 0x7800 | u64 +0x8A38 | first free from cursor +0x8A4C | `DropBomb` 0x0014EAE8 |
| cannon shells | 8 × 0x40 | 0x8800 | u32 +0x8A40 | ring counter +0x8A4B | `FireCannonShell` 0x0014F2A0 |

The occupancy masks are 64-bit values (EE `ld`/`sd`, `dsllv`). The port uses `uint64_t`.

Every spawn function records a replay event (`func_00170240`) when scene 2 is in sub-state 0xB (recording). Replays re-spawn projectiles from these events, so slot numbers must match the original exactly. That is why the slot rules above matter.

Per frame the flight scene calls, in this order:
1. `AgeProjectiles` 0x0014BAF8: lifetime, gravity, per-kind pre-step;
2. `UpdateProjectiles` 0x0014BB40: move, hit tests;
3. `DrawProjectiles` 0x0014BB88: only bullets (tracer GIF packets), missiles and special weapons draw anything; the other kinds' draw functions are empty.

## Kinds

### Bullets

- Init (`func_0014CAE0`): position, angles and velocity of the shooter, plus muzzle speed added along the nose.
- Age: gravity on vy, freed at the end of their lifetime.
- Hit (`func_0014BFC0`):
  - First, `func_0014BE10` writes a "hittable" byte per unit into the **scratchpad at 0x70000000**. The port needs a scratchpad buffer here, or an equivalent array.
  - Swept segment against unit collision meshes (`func_0017C758`/`func_0017C778`).
  - A player hitting its own side gets radio message 0x15 and no damage.
  - An enemy hit sets hit flag 1 on the target and the attacker index at target+0x63.
  - Terrain hits (`func_0017C8D0`) spawn an impact effect by surface (`SpawnImpactEffect` 0x00154558).
- Gun pipper on the HUD: `func_0014C7A0` (flight time) and `func_0014C800` (predicted point with gravity 24.5·t²).

### Missiles (standard)

- `func_0014CC70` guidance:
  - lifetime 6 s;
  - after a short delay, turns towards the target if it is inside the seeker cone, limited by a turn rate. Player missiles use per-difficulty tables 0x002B67C0 (cone) and 0x002B67D8 (turn) indexed by `game[0x28]`; enemy missiles use constants;
  - outside the cone the lock is lost and a "MISSED" message is shown once (flag 0x10);
  - sets the target's +0x66 (missile index) and hit flag 0x100 (incoming), which drive the missile-alert warning and the AI jinks (`ai.md`);
  - accelerates to its max speed.
- `func_0014D1C0` hit: hit flag 2 on the unit, explosion 0xB5 (ground) or 0x81 (air); terrain hit; smoke trail colour by owner. After a hit the slot lingers 180 frames (+0x5C bit 0) before it is freed.

### Special weapons (13 kinds, 17 weapon ids)

`func_0014FB88` sets up the slot. Its per-id init is a switch through jump table 0x004047E0. Ghidra does not recover the switch: the case code 0x0014FC24–0x0014FD30 lies outside the function body. A plate comment in Ghidra lists the cases.

| Weapon id | Init | Kind | Behaviour |
|---|---|---|---|
| 0, 1, 2 | 0x0014FFA8(n) | 0 | bomb; area damage radius 500 / 700 / constant |
| 3 | 0x00150880 | 1 | guided air-to-ground missile, radius 700 |
| 4 | 0x00151F50 | 6 | cluster bomb: ring of random sub-blasts up to 1400 |
| 5 | 0x00153D60 | 11 | fuel-air style: ignites < 512 above the ground, blast grows over 600 frames, radius 1400 |
| 6 | 0x00152298 | 7 | random scattered blasts, radius 128 |
| 7 | 0x00151BA8 | 5 | spinning dispenser, 120-frame burst phase |
| 8 | 0x001502F0 | 2 | short-range rocket, life 300, radius 210 |
| 9 | 0x00152C18 | 8 | homing missile that releases sub-munitions |
| 10 | 0x00150F30 | 3 | homing air-to-air missile, life 600 |
| 11 | 0x00153958 | 10 (ground/naval target) or 3 | anti-surface missile, life 960 |
| 12, 13 | 0x00151550(1/0) | 4 | homing missile, life 540 / 720 |
| 14 | 0x001531A0 | 9 | homing missile, life 1440 |
| 15 | none | — | **kind byte not initialised: the slot keeps the kind of its previous use**. HYPOTHESIS: id 15 is never fired by the shipped data |
| 16 | 0x001524F0 | 12 | ground fire spread: 16 frames of random blasts within 3× radius 96 |

Mapping weapon ids to the in-game names (QAAM, XLAA, …) needs the aircraft special-weapon tables (`../formats/AIRCRAFT.md`); open point.

Shared helpers:
- `func_00154198`: launch point by kind (second jump table 0x00404840, also outside the Ghidra body);
- `func_001542C0`: segment test of a special weapon against units;
- `ApplyAreaDamage` 0x00153D88: every unit whose distance to the point is less than radius + unit size (+0x70) gets the hit flag of the weapon kind and the attacker index. Players' weapons skip their own side. Cannon blasts (kind 0x20) skip classes 8/9 (the cannons themselves).

Kinds 5, 6, 7 and 12 take blast positions from the random table (`NextRandomFloat` 0x00100DC8, not `rand`). The table cursor is reset from the frame counter every gameplay frame, so only the call order inside the frame matters (`../game/random.md`).

### Flak, shells, bombs, cannon shells

- **Flak** (`func_0014DC38`): a burst within 200 of a player sets hit flag 8, the attacker, and a push direction at unit+0x30.
- **Shells**: ballistic with g/2 = 24.5 (units per s², time in seconds = frames/60). Four aim modes, chosen by the ground unit's weapon record: flight time (0x0014E3C8), horizontal speed (0x0014E4A8), launch angle (0x0014E588), apex height (0x0014E700). They explode on terrain contact (`func_0013D698`) or at the end of their lifetime.
- **Bombs** (AI): lifetime 1800, explode below the terrain height.
- **Cannon shells** (class-8 cannons and the event script `func_0014F470`):
  - flight, then on terrain impact a blast whose radius grows by 500 per frame up to a limit, with `ApplyAreaDamage` kind 0x20;
  - the nearest shell's distance to player 0 goes to player state +0x290 and to `RequestRumble` 0x002576C8.

## Damage

Hit flags are set on the target (unit+0x50, low 16 bits) during a frame. At the start of the next frame, `UpdateUnitStates` 0x0011BE30 shifts the word left by 16 (`+0x50 <<= 16`) and turns the upper bits into HP loss. All sources add up in the same frame:

| Set as | Applied as | Source | Damage |
|---|---|---|---|
| 0x01 | 0x10000 | bullet | 5; if the attacker (+0x63) is not a player: random 1..N (`NextRandomFloat` × constant + 1, random table, not `rand`) |
| 0x02 | 0x20000 | standard missile | 40; 100 when difficulty `game[0x28]` = 0, outside sequences, and the attacker is player 0 |
| 0x04 | 0x40000 | special weapon / area damage | 100 |
| 0x08 | 0x80000 | flak burst | 20 |
| 0x20 | 0x200000 | heavy hit (cannon blast) | 200 |
| 0x40 | 0x400000 | collision (`func_001222E8`): player HP set to 0, mission flag 0x4000 | crash |
| 0x80 | 0x800000 | player special case (player state +0x2D4) | see `UpdateUnitStates` |

Rules:
- HP is reduced only when it is 1..9998: HP 9999 means invulnerable, 0 means already dead.
- Units with +0x48 & 0x8C000000 take no damage from non-player attackers.
- With mission flag 0x80000, units of type 0x12 (index > 1) take no damage.
- Max HP comes from the type string table 0x002A2388, scaled by difficulty (`func_0011E878`).
- The bullet damage from AI attackers reads the random table (`NextRandomFloat`), so it depends on the order of table reads inside the frame.

## Collision

### Terrain

- Height map: 256 × 256 cells of 2048 units, centred on the origin (cell = (coord >> 11) + 0x80), so the map spans ±262144. Cells are grouped into 16 × 16 blocks. A block index table (terrain +0x08) maps each block to 0x400-byte sample data (terrain +0x0C). Each cell has 2 × 2 u8 height samples.
- `func_0013E338`: height at (x, z). It interpolates on one of the two triangles of the cell. y = sample × **−80.0** (y grows downwards: higher ground has more negative y).
- `func_0013E2B0`: surface height by a vertical segment test; `func_0013E820`: coarse height from the samples.
- `func_0013E978`: material from a u16 attribute grid (terrain +0x1C / +0x20), value >> 12 (2 = water).
- `func_0013D698`: segment against terrain. It walks the cells between the two points and returns the hit flag, point, normal and material. It is used by bullets, missiles, shells, landing gear, player crashes and falling units.
- Two different cell-rounding rules exist: `func_0013E338` (adds 0x7FF for negatives) and `func_00139000` (uses 0x7F as the base for negatives). Both are original; keep each where it is used.

### Units

- `func_0017C758` / `func_0017C778`: a moving unit's swept volume against collision meshes. Mesh kinds come from flags 0x10000 / 0x20000 / 0x40000.
- `func_0017C940` / `func_0017C9B8`: a swept segment (projectile) against unit meshes, skipping the owner.
- `func_0017CB70`: segment against terrain; `func_0017C8D0`: ray along the velocity against terrain.
- Triangle tests 0x0017C020–0x0017C708 run on VU0 macro mode: check the disassembly when translating (CLAUDE.md, VU0 rule).
- Player response (bounce, crash, damage by impact angle) is in `flight.md`.

## Things the port must keep

- Slot allocation rules, including overwrites of the oldest missile, special-weapon and flak slots (replay events refer to slot numbers).
- 64-bit occupancy masks.
- The scratchpad table at 0x70000000 used by bullet hit tests.
- Special-weapon id 15's uninitialised kind byte (only if id 15 can be fired; HYPOTHESIS: it can't).
- Order of random-table reads (`NextRandomFloat`) inside the scatter weapons.

## Open points

- Weapon id → in-game weapon name (needs the special-weapon tables in `../formats/AIRCRAFT.md`).
- Whether any aircraft carries special-weapon id 15.
