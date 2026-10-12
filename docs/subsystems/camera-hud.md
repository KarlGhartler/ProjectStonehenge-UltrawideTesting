# Camera and HUD

The cameras of the flight scene (player views, replay and death cameras) and everything drawn on top of the 3D view: HUD gauges, markers, reticles, radar, map, messages and warnings. HUD module 0x0015E7B0–0x0016CF88, camera library 0x001026B8–0x001031C0, view cameras 0x0010C850–0x0010D760, view control `func_00125B70`. Every function has a row in `../port/coverage-functions.csv`.

## Cameras

### Camera object (library, `core` module)

- `func_001026B8` init: unit matrices, default projection (640×480 screen, near/far), viewport = draw buffer.
- Projection `func_00102810` (+0x160..+0x170: screen width/height, distance, aspect, far), viewport `func_001028D8` (x, y, w, h, z range, GS screen centre 2048 − size/2).
- `func_001029E0` per frame: world→view from position + rotation, view→screen (`sceVu0ViewScreenMatrix`), clip matrix (`func_001031C0`), combined world→screen matrix at +0xE0.
- Look-at helpers: `func_00102B68` (reset, distance 512), `func_00102B90` (direction → pitch/yaw), `func_00102C08` (eye→target rotation, roll, orbit).

### View cameras (flight scene, 5 views of 0x440, `mission-flow.md`, `rendering.md`)

- `func_0010C9D0`: one view per player; split screen halves the viewport when `game[0x26]` is set; widths depend on the 16:9 flag (ctx 0x10000).
- `UpdateViewCameras` 0x0010CB80: picks the camera by player state:
  - player view byte +0x149 (`flight.md`): 0 first-person HUD-only view, 1 cockpit view, 2 external view (confirmed in PCSX2);
  - mission end and replay: presets 0x002A1700 / 0x002A1730.
- `func_0010D760` solves the camera from flags in +8: attach point (4), up vector (2), follow offsets (1), look-at (8). It keeps the eye above the terrain with a ray test (`func_0013D698`).
- `func_0010C868` death camera: tries up to 4 orbit angles until the terrain ray test is clear.
- `func_00125B70` (player view control): cycles views, looks at the target, camera shake from the random table (`NextRandomFloat`, table 0x00411998, not `rand`; `../game/random.md`).

## HUD

### State

Per player: 0x74 bytes, pointer table 0x00410AA0 (`func_0015E7B0`), current player in gp `D_004117D0`:

| Offset | Use |
|---|---|
| +0x00..+0x1F | kill-message ring: unit index per entry |
| +0x20..+0x3F | kill-message ages |
| +0x40 / +0x41 | ring read / write index |
| +0x42 | damage flash timer (bullet 8, missile 32, flak 16 frames) |
| +0x44..+0x63 | explored-map bits: 16 rows × u16 (16 × 16 grid of 32768-unit squares), set around the player each frame, initial bits from the mission (`func_0015E8C0`) |
| +0x64 | warning blink frame counter |
| +0x6C / +0x6E / +0x70 | shown warning, requested warning, blink phase |
| +0x72 | radar mode (SELECT cycles it; analog/digital pad modes handled separately) |

### Per frame

- `UpdateHud` 0x0015E928 (simulation step): flash timer, explored bits, kill-message ageing (`func_00162F98`, 120 frames), `SelectHudWarning` 0x0016CF88, `func_0016D688`.
- `DrawHud` 0x0015EAE0 (draw lists, per player):
  1. HUD colour in globals 0x00420680..0x00420683: green (0, 0x80, 0x40, alpha 0x60); **red** (0x80, 0, 0) while a missile is incoming or the player is locked (+0x50 & 0x3000000), or while the damage flash bit 4 is set.
  2. Screen frame from the camera (centre +0x178/+0x17C, size +0x18C/+0x18E, in GS 1/16-pixel units), nose position projected (`sceVu0RotTransPers`) and tested inside the frame.
  3. GS state packets for buckets 0, 1, 2, 4 (`func_0015F2B0` RGBAQ, `func_0015F350` alpha mode, `func_0015F420` alpha test, `func_0015F4F0` register 0x14), with variants for the 16:9 flag.
  4. Always: timer/score panel `func_001633C8`, weapon panel `func_00162958`, target info `func_00162D98`, kill messages `func_001630A0`, aircraft status `func_00163C08`, damage `func_00163818`, flight-path marker `func_00160598`.
  5. Mission kind 1: progress panel `func_00167758`; kind 2 (two players): score indicator `func_00167D68`.
  6. Third-person view (+0x149 = 2): speed and altitude readouts `func_00160E78`/`func_00161720`. Other views, when the nose is on screen: pitch ladder, heading tape, speed gauge, altitude gauge, throttle ladder, right ladder, nose marker (`func_00162030`, `func_00161BB0`, `func_001609D8`, `func_00161250`, `func_00160FF8`, `func_00161890`, `func_00160698`). View 0 (+0x149 = 0) adds look-around reference lines `func_00160200`.
  7. Sub-state 0x15: landing guidance `func_00166A10`.
  8. Target direction arrow `func_00166588`.
  9. Unless a scripted segment runs (flag 0x10000000): gun pipper `func_00164440` (with gun ammo), special-weapon / bomb reticle `func_00164F00` (when the special weapon is selected), multi-lock boxes `func_00164BD8`.
  10. Target markers for all units `func_00165AB8`.
  11. Radar (`DrawRadar` 0x00169010) or full map (`DrawRadarMap` 0x0016BC68) by mode +0x72. Mission kind 2 has 3 modes, the others 4.
  12. the warning box `DrawHudWarning` 0x0016D348 (not in sub 0x1D) and `func_0016D778` (not in segments).
- The whole HUD is skipped when ctx flag 0x20000 is set (pause-menu item HUD ON/OFF, offered only after the game has been cleared once; `../subsystems/mission-flow.md`).

### Units and options

- Speed and altitude use option byte 0x002A15CC bit 2 (UNITS, `frontend.md`) to choose between two conversion constants. Speed = |velocity| × constant × 3600 × 60 / 1000; altitude = −y × constant.
- Digit counts adapt to the value (1–5 digits).

### 16:9 mode (ctx flag 0x10000, option SCREEN RATIO)

Every HUD element has two layouts. In 16:9 mode:
- sprite widths are multiplied by **3/4** (`DrawHudSprite` 0x0015FEE0), and offsets from the centre are smaller (e.g. −0x90 instead of −0xC0);
- font and digit tables switch to narrower variants (`func_0015F6A0`, `func_0015FA18`);
- the view cameras use a 16:9-aware width (`func_0010C9D0`).

So the HUD is drawn **anamorphically**: the 512-pixel-wide frame is meant to be stretched to 16:9 by the TV. In this mode the port's output pass must present the frame at 16:9, otherwise the HUD looks squeezed (`../decisions/ADR-006-presentation-resolution.md`). Presentation only (category 1, original behaviour).

### Warnings (`SelectHudWarning`)

The player position predicted 600 frames ahead (position + 600 × velocity, x and z) decides the area warnings. Priority order, then an 8-frame fade-in/hold/fade-out cycle per change:

| Id | Condition | Text (line 1 / line 2) |
|---|---|---|
| 0 | predicted position outside the mission area (±`D_004104F0[8]` in x and z; in scripted segments ±32768 around the segment point) | CAUTION / OFF COURSE |
| 1 | never selected | FRIENDLY / HOLD FIRE (unused) |
| 2 | missile incoming (hit flags +0x50 & 0x3000000) | MISSILE ALERT |
| 3 | locked by an enemy (+0x50 & 0x60000000) | WARNING |
| 4 / 5 | predicted z below the mission limit `D_004104F0[7]` (the return line); 5 for aircraft ids 2, 4, 8, 14, 21 (mask 0x00204114), 4 for the others | 4: CAUTION / RETURN TO BASE; 5: CAUTION / RETURN TO CARRIER |
| 6 | player state flag 0x1000000; in scripted segments player state +0x2E0 < 0.5 or the ceiling fade +0x2DC < 1.0 (not while state flag 0x40000000 is set) | CAUTION / STALL |
| 7 | player state flag 0x2000000 | CAUTION / DAMAGE |
| 8 | mission flag 0x10000000 (mission data +0x20) and less than 256 units above the terrain | CAUTION / PULL UP |
| 9–12 | never selected | CAUTION / PULL DOWN, TURN RIGHT, TURN LEFT, RADAR CLEAR (unused) |
| 0xFF | none | — |

The texts come from the executable, read at runtime: `DrawHudWarning` 0x0016D348 takes 4 bytes per id from table 0x002B7450 (text index of line 1, text index of line 2 or 0xFF, colour flag, padding). The texts are a table of 8-byte entries {string pointer, two half-widths for the two HUD layouts} at 0x002B73D8. The colour flag is set only for id 1. The box fades in and out over 8 steps (blink phase +0x70, counter +0x64). Id 5 was confirmed on screen in PCSX2 (`../pcsx2/sessions-04.md`), which also confirms the table decoding.

## Things the port must keep

- HUD drawing happens in the draw-list phase. It reads simulation state but never changes it, except the radar mode byte (+0x72, pad input in `DrawHud`) and the kill-message ring read side. These are game state and must still run once per simulated frame, not once per presented frame (ADR-002 interpolation: HUD logic stays on the simulation tick; only its drawing may be re-run).
- Camera shake uses the random table (`NextRandomFloat`), not `rand`.
- 16:9 layout rules above.

## Open points

None.
