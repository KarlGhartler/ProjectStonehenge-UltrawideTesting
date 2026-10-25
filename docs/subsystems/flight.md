# Flight model and player control

The player aircraft from pad input to new position and rotation each frame, plus landing gear, player-side collision response, and the effects and sounds driven by the flight state. Region 0x0011B900–0x0012A488 (`../port/coverage-functions.csv` has every function with its note). Weapons and targeting in the same module: `weapons.md`. Views and cameras: `camera-hud.md`.

## Call chain per frame

`FlightSimStep` 0x0010C320 → `UpdateUnitStates` 0x0011BE30 (all units, hits and HP, `weapons.md`) and, for each player:

1. `UpdatePlayerAircraft` 0x0011F3F8:
   1. `ReadPlayerControls` 0x0011FF30: pad → control values in the pad-extension block `ctx->pad_ext[player]` (0x20 bytes per player).
   2. `func_001205F8`: control-surface animation inputs (smoothed stick, throttle, flap values).
   3. `UpdateFlightDynamics` 0x00129500, or `func_00129B58` / `func_0012A0E0` when the aircraft is flown automatically (scripted segment, in-engine sequence).
   4. `UpdateLandingGear` 0x00121370 and the gear state machine `func_00120D38`.
   5. `func_00122638`: collision with terrain, units and meshes; damage by impact angle; crash or bounce (`func_0012A2D0` levels the aircraft after a bounce).
   6. `UpdatePlayerTargeting` 0x001232D8, `FirePlayerWeapons` 0x001249A0, reload timers `func_00124740`.
   7. View control `func_00125B70`.
   8. Effects `func_00126758` / `func_00126970`, sounds and warnings `func_001270B8`.
   9. `UpdateAircraftMovingParts` 0x001206E0: model part angles from the flight values (aircraft block 3, 0x60-byte records). Details below ("Moving parts").

## Data

### Unit struct (0xA0; pointer table 0x002A1F70[255])

`0x002A1F70[i]` is a **pointer** to unit i (0xA0 bytes, 16-byte aligned, allocated from the work heap by `func_0011B948`); players are units 0 and 1.


Fields used by the flight model (full layout in `mission-flow.md`):

| Offset | Use |
|---|---|
| +0x00 | position (vec4) |
| +0x10 | rotation, Euler angles x (pitch), y (yaw), z (roll), radians |
| +0x20 | velocity, world space (vec4) |
| +0x44 | pointer to the aircraft state (player: 0x4C0 bytes) |
| +0x50 | hit flags (`weapons.md`) |
| +0x69 | aircraft id |

### Player aircraft state (0x4C0, work heap, `func_0011E8D8`)

Float index `f[n]` = byte offset n·4.

| Field | Use |
|---|---|
| f[0..2] | rotation deltas for this frame (added to the Euler angles) |
| f[0x44] | pointer to the aircraft flight-parameter record (`func_0011B8B8(aircraft id)`) |
| f[0x46], f[0x47] | auto-level targets (pitch, roll), −1..1, used when flag byte +0x185 bit 4 is set |
| f[0x4C] | forward speed in body frame (z of the body velocity) |
| f[0x4D] | gravity / sink term subtracted from the vertical velocity |
| byte +0x149 | current view, cycled 0 → 1 → 2 by the view button: **0 first-person HUD-only view, 1 cockpit view (draws the cockpit model), 2 external (third-person) view** (confirmed in PCSX2, `../pcsx2/sessions.md` run 04). VS mode forces 0, mission kind 3 forces 2 (`func_00125B70`) |
| byte +0x14A | look-at target unit (0xFF none) |
| +0x158 | gun ammo |
| f[0x62] | view/flag bits (0x40 view changed, 2/4/8 look-at states) |
| f[0xBA], f[0xBB] | side-slip ratios (body x/z, y/z), used to rotate the velocity back towards the nose |
| +0x240..+0x25C | animation inputs for the moving parts (stick, throttle, flaps, gear, speed, heading) |
| +0x280 | gear state (0 down, 1 retracting after 180 frames, 2/3 up) |
| +0x2DC | ceiling fade factor |

### Flight parameters

- Normal play: the per-player parameter block at `D_00410650[player]`, copied from the aircraft record (aircraft block 6, `../formats/AIRCRAFT.md`).
- When the mission status flags (`D_004104F4` +0x20) have bit 0x10000000 set (demo / scripted segment), `func_00129368` copies an override parameter set to the stack and that set is used instead.
- Parameter offsets used directly in `UpdateFlightDynamics`: +0x88 (side-slip gain x), +0x9C (side-slip gain y). The others are read in the helper functions (coverage notes).

## Control input (`ReadPlayerControls`)

- Option word `D_002A15D0[player]` (u16, front-end options page):
  - bit 0 → `pad_ext[player]+0x0E` bit 0, the **CONTROL TYPE** option that selects the flight-model path below: set = **EASY**, clear = **NORMAL** (PCSX2: EASY 0x0009, NORMAL 0x0008 with vibration on);
  - bit 1 swaps the two stick-axis bytes +0x10/+0x11 (REVERSE pitch);
  - bit 2 swaps +0x18/+0x19;
  - bit 3 vibration (front end, rumble in `sound.md`).
- In-engine sequence type 3 (flight object +0x1653 == 3) forces control type bit 0 off and ignores the option.
- `pad_ext +0x0E` bit 0x100 enables a button remap (pad bits 0x8000 of three pad words → bit 2 of three control words).
- Tutorial missions restrict inputs (coverage notes).

## Flight model (`UpdateFlightDynamics` 0x00129500)

An arcade model on Euler angles with a body-frame velocity. Per frame:

1. Build the rotation matrix from the Euler angles (`func_00102D28`) and its inverse. With control type bit 0 set, the pitch angle used for the matrix is replaced by a constant when the aircraft is beyond ±(constant) pitch, else 0, which keeps the frame level for the next step.
2. Subtract the sink term f[0x4D] from the world vertical velocity, then transform the velocity into the body frame; f[0x4C] = body forward speed.
3. Speed: `func_00127BA8` (speed limits by altitude; a high-altitude max above the ceiling), `func_00127EC0` (ceiling fade, low-speed lift loss / stall), `func_00128090` (gravity and lift from bank angle and speed, climb penalty). The throttle → target-speed step (`func_00127CB0`, acceleration and brake rates, afterburner) is called from `UpdatePlayerAircraft`.
4. Side-slip ratios f[0xBA] = vx/vz, f[0xBB] = −vy/vz (in the control-type-1 path both are 0 below speed 1.0).
5. Rotation, two paths:
   - **Control type bit 0 clear** (matrix path): auto-level targets from the current angles, then `func_001282B8` (pitch rate), `func_00128AF0` (yaw rate from bank, `sin`), `func_00128D40` (rudder rate). The deltas f[0..2] are applied through `func_00102EF0` (rotate the Euler angles by a delta in the body frame), so pitching at any bank angle behaves like a real aircraft.
   - **Control type bit 0 set** (Euler path): auto-level, then `func_00128510` (pitch rate variant) and `func_00128768` (roll rate with inertia and auto-level). The deltas are added directly: x += f[0], y += f[1] (sign flipped when the aircraft is upside down, |roll| beyond a constant), z += f[2]. Angles are then wrapped (pitch past ±π/2 flips yaw and roll by π; yaw and roll wrap at ±π).
   - Bit 0 set is CONTROL TYPE **EASY** (Euler path), clear is **NORMAL** (matrix path); confirmed in PCSX2 (`../pcsx2/sessions.md` run 04).
6. Rotate the body velocity by the side-slip correction (gains +0x88/+0x9C) and transform it back to world space into unit +0x20.
7. `func_00128E80`: blend the velocity towards the nose direction (drift), ground effect and stall sink.

Position integration (position += velocity) happens in the unit update, not here.

### Precision notes for the port

- `func_001282B8`, `func_00128510` and `func_00128AF0` do their damping in **double precision through libgcc soft-float** (`__muldf3`, `__adddf3`, `__extendsfdf2`, `__truncdfsf2`). The port must do these steps in `double` and round to `float` at the same points, otherwise trajectories drift from PCSX2.
- Everything else is single-precision float on the FPU (EE float rules: no denormals, no NaN/Inf; `ee_math.h`).
- `sinf`/`cosf` are the statically linked libm routines (outside the game range). The port needs bit-exact versions (translate the libm code; do not call the host libm).
- Matrix building uses VU0 macro mode (`sceVu0*`, `func_00102D28`); the port uses `vu0_math.h`.

## Automatic flight

- `func_00129B58`: heading and pitch towards a target point, speed hold. Used by scripted segments and in-engine sequences that fly the player aircraft.
- `func_0012A0E0`: automatic turn manoeuvre with speed control.

## Landing gear and ground contact

- `func_00120D38` gear state machine (+0x280): down at start on runway/carrier missions, retracts 180 frames after take-off, animates gear nodes 0x2B–0x2D.
- `func_001211B0` forces the gear up for an in-air start and returns the ground clearance; `func_00121318` resets it to down from the per-type table 0x002A24C0.
- `UpdateLandingGear` 0x00121370: three wheel rays against the terrain or the carrier deck (unit type 0x88), suspension compression, touchdown sound, tyre smoke (`effects.md`), replay events.

## Collision response (player side)

- `func_00122268`: impact angle factor 1..100 from surface normal and velocity.
- `func_001222E8`: sphere test of a unit against all units (radius unit +0x74), sets hit flag 0x40.
- `func_00122488`: unit against the collision meshes of large units (`func_00146808`, `rendering.md`), push-out, hit flag.
- `func_00122638`: player against terrain, units and meshes; damage scaled by impact angle; crash when HP runs out, otherwise a bounce.
- Hit flags are consumed by `UpdateUnitStates` (damage table in `weapons.md`).

## Effects, sounds, warnings

- `func_00126758` / `func_00126970`: afterburner flame, contrails, wingtip vapour, damage smoke and fire (effect pools in `effects.md`).
- `func_001270B8`: engine sound (`func_00216B80`), stall/altitude/missile warnings, HP alarms, sound listener.
- `func_001279B0`: start/stop the engine sound per player; both players share a voice in split screen.
- `func_0011D300`: altitude/terrain warnings and a stall wobble (sine) on very easy difficulty.

## Things the port must keep

- Frame-locked integration: every rate is per 60 Hz frame (ADR-002 fixed step).
- The Euler wrap rules and the upside-down yaw sign flip (control type bit 0 set).
- The double-precision damping steps.
- The override parameter set for demos (flag 0x10000000): demos are replays, so a different parameter set changes the recorded result.

## Open points

- Meaning of the constants at 0x0040DF68–0x0040DFBC (angle limits and wrap values; read from `.sdata` at run time).

## Moving parts (`UpdateAircraftMovingParts` 0x001206E0)
`UpdateAircraftMovingParts(i, immediate)`: unit U = 0x002A1F70[i], player state P = U+0x44, block 3 B = word S+0x1760+i·0x40 (`../formats/AIRCRAFT.md`). Called per frame from the player update and once with immediate = 1 at mission setup (`func_0011EFA0`).
- Altitude factor a = clamp(U+4 · −1/65536 (`-1.5258789e-05f`), 0, 1). Spin factor s = (U+0x14 + *0x0040DD9C) / *0x0040DDA0, then −1 when > 1, +1 when < 0 (one step each).
- Records: count B+4, records of 0x60 bytes from B+8; the walk also stops at a record whose part count byte is 0. A record: u8 +0 part count n, s8 +1… part ids, f32 +8 base, f32 +0xC rate, then 4 drivers of 0x14 bytes at +0x10: s16 +0 source, s16 +2 mode, f32 +4 in_min, +8 out_min, +0xC in_max, +0x10 out_max.
- Value v starts at base. Drivers are evaluated in order while their source is not 0, 7, 8 or 9 (at most 4): x = source value (1 → P+0x250, 2 → P+0x258, 3 → P+0x254, 4 → P+0x248, 5 → P+0x24C, 6 → P+0x25C, 10 → P+0x240, any other → base); when in_min ≠ in_max, x = (x − in_min) / (in_max − in_min); clamp 0…1; y = x · (out_max − out_min) + out_min; clamp 0…1; mode 0/1: v = y; 2: v = max(v, y) (y when v < y); 3: min; 4: v = y when |y − 0.5| > |v − 0.5| (compared as doubles through libgcc `__extendsfdf2`/`__subdf3`/`0x0028BD28`; the differences are computed in float first, so a float compare gives the same result); 5: v = v + y − base, capped at 1; 6: v = (v − base) · y + base; other modes: unchanged; then clamp v to 0…1. The "kind" of the record is the source of the driver where the walk stopped (the 4th driver when all 4 were evaluated).
- For each part id p (j < (s8)n), once per call (a 32-bit done mask, bit p & 31): angle slot A = P+0x1C0+4p; kind 7 (free-running): A += rate, then while A > 1: A −= 1; kind 9: A = s; kind 8: A = a; otherwise immediate → A = v, else rate-limited: d = v − A; d > rate → A += rate; d < −rate → A −= rate; else A += d. Then, unless S+0x1653 == 2 (two players) and p == 15: `func_00149EB8(track p, model i, A)`; p == 9 also sets track 0x42 to P+0x1E4, p == 10 track 0x43 to P+0x1E8 (the same slots).

