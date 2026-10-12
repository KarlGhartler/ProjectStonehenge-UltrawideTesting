# Effects

Explosions, smoke, trails, impacts, weather particles and the "special object" renderers. Drawing order inside the frame: `rendering.md` (effects table, 0x400 buckets in view bucket 0xC). Random sources: `../game/random.md`. Every effect function has a row in `../port/coverage-functions.csv` (subsystem `effects`; pool notes "effect pool +0x…").

## Pools

One work block (`D_004106B4`, about 0x625A0 bytes) holds 45 pools. One pool (+0x2010) is empty. Each pool has fixed-size records, an occupancy bitmask (one u32 per 32 records) and a count. Per pool there are three generic wrappers, each driving the type's own functions:

| Phase | Dispatcher | Per pool |
|---|---|---|
| init/reset | `InitEffects` 0x00154660 (flight start, replay start) / `ResetEffects` 0x00154A48 (sequences) | record init |
| update | `UpdateEffects` 0x00154DD0, **called from the simulation step** | record update; the record is freed when it returns "done" |
| draw | `DrawEffects` 0x00155330, per view, after the effect sub-table setup `func_001558C8` | draw setup once, then record draw |

`InitEffects` also seeds the VU0 R register (`vrinit`, seed from 0x0040E7F0).

## Spawning

- Spawn functions (`spawn or refresh`, one per type) take a handle. A handle of −1 allocates the **lowest free record**. A full pool returns −1 and the effect is simply not created. Owners keep the handle and refresh continuous effects every frame.
- Main explosion API: `SpawnExplosion` 0x001F08E8 (id, position) / `SpawnMovingExplosion` 0x001F0910 (with velocity).
- Impact effects by surface: `SpawnImpactEffect` 0x00154558 (`weapons.md`).
- Replay playback re-spawns effects from recorded events (`func_00170598`, `func_001706D0`, `func_001708D0`; `mission-flow.md`).
- A per-view effect pass from `BuildFlightDrawLists` (`func_001D3318`, lists 0x001D8588/0x001D86C8) draws additional effects into the view main table.

## Relation to game state

Effect code reads game state (mission number, terrain height, unit types) but writes no unit, projectile or mission state. Effects are presentation. Their update still runs inside the simulation step and reads the random table before the AI and weapons of the same frame, so **effects cannot be skipped, reduced or reordered without changing gameplay** (`../game/random.md`). Some effect functions also call `rand`, the effect draw functions use their own LCG, and five draw functions read the random table after the simulation (`func_001CFCA8`, `func_001E46E0`, `func_001E6B50`, `func_001E9890`, `func_001EA840`; same doc). Draw functions write only effect-private state: the scratchpad cursor 0x00410BAC, the LCG words 0x00410BB0–0x00410C38, the parameter tables 0x002B89B4–0x002B9CA4, the group bucket cache 0x002B8AE4 and the lightning countdown 0x004117E8.

## Special objects

`DrawSpecialObject` 0x001E0A98 dispatches "special object" renderers by a record's kind byte (+0x3D, about 20 kinds, 7–17 KB each, parameters in `.data` 0x002B8xxx). It is used by units, aircraft, sequences and menus.

## Things the port must keep

- Pool sizes and lowest-free allocation: a full pool silently drops effects, and replays depend on the same slots.
- The update step inside the simulation, in the original order, with its random-table reads.
- The VU0 R register (`vrinit`/`vrnext`), used by missile trail segments `func_001DAC38`.

## Open points

- The 111 effect helper functions marked "body not read" in the coverage list, and the names of the effect types marked (H).
