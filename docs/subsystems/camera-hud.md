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

#### View record (0x440 bytes at S + p·0x440) and its setters
Fields used here: word +0 flags (0x8000000F after init), bytes +4, +5 (player), +6 (player), +7 (view mode), +8 solve flags (1 follow offsets, 2 up vector, 4 attach point, 8 look-at), +0xA, +0xB (death-camera try index, 0xFF = none); qwords +0x10 eye, +0x20 target, +0x30 attach point, +0x40 up vector, +0x50/+0x60 follow offsets, +0x70 look-at point, +0xA0 extra look offset; camera objects at +0xB0 and +0x270 (`func_001026B8` layout); orbit +0x430 (word mode), +0x434 yaw, +0x438 pitch, +0x43C distance.
- `func_0010D5E8(v, f)`: byte +8 = f. `func_0010D5F0(v, eye, target)`: +0x10 = eye, +0x20 = target (4 words each). `func_0010D610(v, q)`: +8 |= 4, +0x30 = q. `func_0010D630(v, q)`: +8 |= 2, +0x40 = q. `func_0010D650(v, a, b)`: +8 |= 1, +0x50 = a, +0x60 = b. `func_0010D680(v, q)`: +8 |= 8, +0x70 = q. `func_0010D748(v, q)`: +0xA0 = q, byte +0xA = 0.
- `func_0010C850(v, mode, yaw, pitch, dist)` (floats in f12–f14): +0x430 = mode, +0x434 = yaw, +0x438 = pitch, +0x43C = dist.
- `func_00102838(dst, src)` (camera objects): copies +0x160, +0x164, +0x168, +0x16C, +0x170, +0x198. `func_00102990(dst, src)`: copies u16 +0x188, +0x18A, +0x18C, +0x18E and words +0x178, +0x17C, +0x190, +0x194, +0x198.
- `func_0010C9D0` (called at the start of every `UpdateViewCameras`, so the split-screen aspect below is the previous frame's zoom): w = 640.0 (*0x0040DAA0 in 16:9); S+0x1650 = players `game[0x25]`; per player p (V = S + 0x440·p, C = V+0xB0): bytes +7 = +4 = 0, +5 = +6 = p, word +0 = 0x8000000F; split screen (`game[0x26] == 1`): `SetCameraProjection(w · 0.5, 480.0, *0x0040DAA4, (float at V+0x3D8, i.e. the second camera's +0x168) · 0.0625, terrain +0xA4, C)`, `SetCameraViewport(1.0, *0x0040DAA8, C, x = (s16)(p · (draw_w >> 1)), 0, draw_w >> 1, draw_h)`; else `SetCameraProjection(w, 480.0, *0x0040DAAC, 1.0, terrain +0xA4, C)`, `SetCameraViewport(1.0, *0x0040DAB0, C, 0, 0, draw_w, draw_h)`; then `func_00102838(V+0x270, C)`, `func_00102990(V+0x270, C)`.
- `func_0010C868(θ, p)` (death camera; θ in f12): U = 0x002A1F70[p], V+0xB = 0xFF; up to 4 tries k: q = (sinf(θ) · −16384, −16384, cosf(θ) · −16384, 0) + U (`sceVu0AddVector`); `func_0013D698(terrain, …, U, q, …)` ≠ 1 (no hit) → V+0xB = k, stop; else θ += *0x0040DA94, and when θ > *0x0040DA98, θ −= *0x0040DA9C. Then `func_0010C850(V, 0, θ, 0.0, 0.0)`.

#### `UpdateViewCameras` 0x0010CB80 (per frame, from the disassembly)
`func_0010C9D0()`; S+0x1650 = `game[0x25]`. Per player p: U = 0x002A1F70[p], P = U+0x44, V = S + 0x440·p, C0 = V+0xB0, C1 = V+0x270, R = mission rules record *0x004104F4; base zoom z0 = *0x0040DAB4. "Zoom" Z is stored to C0+0x168 and C1+0x168.
1. **Unit not flying** (U+0x4C bit 0x1000000 clear; death camera): V+0 &= ~0x102, V+0xC = 0, V+4 = 5, V+7 = 1, V+9 = 4, `func_0010D5E8(V, 0)`; d = −2048 − ((float)(s32)V+0x430 · 8192) / 600. When V+0xB < 4: eye = (sinf(V+0x434) · d, d, cosf(V+0x434) · d, 0) + U, target = 0, `func_0010D5F0`; look = U+0x20 · 60 + U (`sceVu0ScaleVector`, `sceVu0AddVector`), `func_0010D680`. Else eye = (0, d, −1.0, 0) + U, target = (*0x0040DAB8, 0, 0, 0), `func_0010D5F0`. Then V+0xC = 0, z0 = *0x0040DABC, Z = z0; counter V+0x430 += 1 when main-context flag 0x100 is clear, or when it is set and the main sub is 0xC.
2. **R+0x20 bit 0x100000** (mission-end camera): `func_0010D5E8(V, 0)`, V+9 = 4, V+0 &= ~2, V+7 = 0, V+0xC = 0, V+0x90 = 0; eye = qword 0x002A16E0 with eye.y = eye.y + (float)(s32)V+0x430 · −20.0, target = 0, `func_0010D5F0`; `func_0010D680(V, 0x002A16F0)`; Z = z0; counter += 1 only in main sub 0xC.
3. **Main sub 0x14 or 0x1B**: `func_0010D5E8(V, 0)`, `func_0010D5F0(V, U, U+0x10)`, V+7 = 0x83, V+9 = 2, V+0 &= ~2, V+0xC = 0, V+0x90 = 0; k = (u32)R+0x24 % 3, t = 0x002A1700 + 12k (3 floats); a = P+0x40 with a.y = a.y / t[2] → `func_0010D610`; b = P+0x30 with b.y = b.y / t[2] → `func_0010D650(V, b, P+0x70)` and `func_0010D748(V, b)`; up = (t[0], t[1], 0, 0) → `func_0010D630`; z0 = *0x0040DAC0 (*0x0040DAC4 in 16:9), Z = z0.
4. **Main sub 0x19**: k = (u32)R+0x24 % 3; when R+0x20 bit 0x2000000: k += 3 if bit 0x4000000 else k += 6; `func_0010D5E8(V, 0)`, V+9 = 2, V+0 &= ~2, V+7 = 3, V+0xC = 0, V+0x90 = 0; `func_0010D5F0(V, 0x002A1730 + 16k, zero)`, `func_0010D680(V, 0x002A17C0 + 16k)`; Z = float 0x002A1850[k].
5. **P+0x188 bit 2** (external/replay camera): V+7 = 3; `func_00136770(p, P+0x188 bit 4 ? 1 : 0)`; `func_00175B08(V)`; V+9 = 3, V+0 &= ~2, V+0x90 = 0, qword V+0x40 = 0.
6. **Otherwise** (player view): `func_0010D5E8(V, 0)`, `func_0010D5F0(V, U, U+0x10)`; s8 P+0x149: 2 → V+9 = 2, V+7 = 0x83, z0 = *0x0040DAC8 (*0x0040DACC in 16:9), `func_0010D610(V, P+0x40)`, `func_0010D650(V, P+0x30, P+0x70)`; 1 → V+9 = 1, V+7 = 0x82, `func_0010D610(V, P+0x50)`; other → V+7 = V+9 = 0, `func_0010D610(V, P+0x50)`. `func_0010D630(V, P+0x10)`. P+0x188 bit 0x40 → cleared; else V+0 |= 0x100. P+0x188 bit 1 (target view): `func_0010D680(V, P+0x60)`, x = `func_00104F10`(0x002A1F70[P+0x14A], U) · *0x0040DAD0, capped at 4096, raised to z0, Z = (x − z0) · P+0x18C + z0. Else Z = (4096 − z0) · P+0x18C + z0 when P+0x18C > 0, else Z = z0. Then Z > 4096 → Z = 4096.
7. `func_0010D760(V)` (solve). When P+0x18C > 0 (zoom ray): v = (0, 0, 2 · C0+0x168, 0); M = `func_00102D28`(unit, C0+0x10); v = M · v; q = C0+0 + v; when `func_0013D698(terrain, hit, …, C0+0, q, …)` == 1: h = M⁻¹ · (hit − C0+0) (`sceVu0InversMatrix`, `sceVu0SubVector`, `sceVu0ApplyMatrix`); Z = max(h.z · 0.5, z0) (z0 of the branch taken above).
8. f = *0x0040DAD4, or *0x0040DAD8 when (V+7 & 3) == 2; C1+0x16C = C1+0x168 · f, C0+0x16C = C0+0x168 · f; `UpdateCameraMatrices(C1)`, `UpdateCameraMatrices(C0)` (0x001029E0).

#### `func_0010D760(V)` camera solve (from the disassembly)
C0 = V+0xB0 (pos C0+0, angles C0+0x10), C1 = V+0x270. A = (0, 0, 0, 0) accumulated angles, Z = (0, 0, 0, 1). `T(p, a, v)` = `func_00103070` (p + R(a) · v, `../game/math.md`). Hit test = `func_0013D698(terrain *0x00410694, hit, …, from, to, …)` (1 = blocked; hit point y at hit+4).
1. When S+0x1668 bit 0x100: clear it and V+0 &= ~0x100.
2. Flag 4: C0+0 = T(V+0x10, V+0x20, V+0x30), C1+0 = T(Z, V+0x20, V+0x30); else C0+0 = V+0x10, C1+0 = Z. Then C0+0x10 = V+0x20.
3. Flag 8 (look-at V+0x70), unless its x, y, z all equal C0+0's: d = V+0x70 − C0+0 (x, y, z; `sub.s`); d = R(C0+0x10)⁻¹ · d (`func_00102D28` on a unit matrix, `sceVu0InversMatrix`, `sceVu0ApplyMatrix`); A.x += asinf(−d.y / `GetVectorLength`(d)); A.y += atan2f(d.x, d.z); A.z = V+0xC.
4. Flag 1: `func_00102EF0(C0+0x10, C0+0x10, V+0x60)`.
5. Flag 2: when flag 1 is also set: q = V+0x10 with q.y += `GetVectorLength`(V+0x50) · *0x0040DAEC; a hit from V+0x10 to q clears qword V+0x40. Then A.xyz += V+0x40.xyz.
6. old = C0+0x10 (copy). V+0 bit 0x100 clear (snap): wrap A.x (while > *0x0040DB20: −= *0x0040DB24; while < *0x0040DB28: += *0x0040DB2C), A.y (DB30/DB34/DB38/DB3C), A.z (DB40/DB44/DB48/DB4C); V+0x80..0x88 = A, V+0x8C = 0. Bit 0x100 set (smooth): A −= V+0x80 (x, y, z), wrap with DAF0/DAF4/DAF8/DAFC, DB00…DB0C, DB10…DB1C; V+0x8C = 0; V+0x80/84/88 += A · 0.125.
7. `func_00102EF0(C0+0x10, C0+0x10, V+0x80)` (a2 is still V+0x80 from step 6); C1+0x10 = C0+0x10. Without flag 1 the solve ends here.
8. Flag 1 (follow offset o = copy of V+0x50): M = R(C0+0x10); q = C0+0 + (M · o) · *0x0040DB50; a hit from V+0x10 to q → o.z = o.z + o.y, o.y = 0, V+0x90 = 1 − (1 − V+0x90) · *0x0040DB54; else V+0x90 = V+0x90 · *0x0040DB58.
9. D = old − C0+0x10 (4 fields); per component (x, y, z in turn) D > *0x0040DB5C → −= *0x0040DB60/64/68; then D < *0x0040DB6C → += *0x0040DB70/74/78; D = D · V+0x90; C0+0x10 += D (4 fields); then the same two wraps on C0+0x10 with DB5C/DB7C/DB80/DB84 and DB6C/DB88/DB8C/DB90; C1+0x10 = C0+0x10.
10. Smoothed offset: V+0xA8 = V+0xA8 · *0x0040DB94 + o.z · *0x0040DB98 → o.z; V+0xA4 = V+0xA4 · *0x0040DB94 + o.y · *0x0040DB98 → o.y. t = M · o, q = t + C0+0.
11. No hit from V+0x10 to q: C0+0 = T(C0+0, C0+0x10, o), C1+0 = T(C1+0, C1+0x10, o); done.
12. Hit: vertical probe from (q.x, q.y − 128, q.z) to (q.x, q.y + 128, q.z); g = hit y when it hits, else 0. When q.y ≤ g: C0+0 = T(C0+0, C0+0x10, o), C1+0 = T(C1+0, C1+0x10, o); done. Else (camera above the ground there): h = sqrtf(t.y² + t.z²); δ = asinf(−t.y / h) − asinf(−(g − C0+4) / h); w = M · (o.x, 0, o.z, o.w); C0+0x10.x −= δ when (w.z > 0) ≠ (t.z > 0), else += δ. Pitch fold: x < *0x0040DB9C → x = *0x0040DBA0 − x, y += *0x0040DBA4 (y < 0) or −= *0x0040DBA8, z += *0x0040DBAC (z < 0) or −= *0x0040DBBC; else x > *0x0040DBB0 → x = *0x0040DBB4 − x, y ± *0x0040DBB4, z += *0x0040DBB8 (z < 0) or −= *0x0040DBBC. C1+0x10 = C0+0x10; C0+0 = T(C0+0, C0+0x10, o), C1+0 = T(C1+0, C1+0x10, o).

#### `func_00125B70(p)` player view control (per frame, from the disassembly)
U = 0x002A1F70[p], P = U+0x44, A = aircraft data 0x00410650[p], pad record K = main context + 0x2D0 + 0x20·p (word +0 held, word +4 pressed, bytes +0x18…+0x1C analog/pressure values), V = view p, R = *0x004104F4. Shake k = `func_00257360(p)`: D_00411888[p] ≠ 0 → (float)(s32)(D_00411888[p] − 0x48) / 183.0, else *0x0041045C when D_00411880[p] ≠ 0, else 0; clamped to 0…1 (the **rumble state**: the camera-shake random reads below happen only while the pad vibrates, so the port must keep these rumble words exactly like the PS2, `../game/random.md`). A shake with scale c is: s = k · c; r1 = `NextRandomFloat()`: X += (r1 − 0.5) · s; r2: when r2 < 0.5, s = −s; r3: Y += (r3 − 0.5) · s (three reads, only when k ≠ 0).
1. Pressed bit 0x10 (change view): P+0x149 = (P+0x149 + 1), 0 when it reaches 3; P+0x188 |= 0x40; new view 2 → `func_0010D748(V, A)`; view 1 → qword P+0x20 = 0.
2. S+0x1653 == 2 → P+0x149 = 0. S+0x1653 == 3: when P+0x185 bit 4 is set or s8 R+0x7F ≠ 0: `func_0010D748(V, A)` unless the view is already 2, then P+0x149 = 2; otherwise P+0x149 = 0.
3. f = P+0x188 & ~0xC (stored). When held bit 2 and the projectile-follow record p is active (+0xC bit 0): P+0x150 < 30 → P+0x150 += 1, P+0x188 = f & ~2; else P+0x188 = f | 2, plus | 4 when f had no bit 2. Otherwise: when f & 2: f |= 8 (stored); P+0x150 = 0; P+0x188 = f & ~2.
4. Pressed bit 8 (target): U+0x62 ≠ 0xFF → P+0x14A = U+0x62; else P+0x14A ≠ 0xFF → P+0x14A = 0xFF; else search units i = 2 … (word 0 of *0x00410640) − 1 with W+0x4C bits 0x10000000 and 0x4000 both set and (W+0x48 & 3) == (U+0x48 & 3): d = float (0x00410648[p]) + 12i + 8; skip d < *0x0040DE50; keep the largest d (strictly greater); P+0x14A = best index or 0xFF.
5. Held bit 8 and P+0x14A ≠ 0xFF: W = 0x002A1F70[P+0x14A]; when W+0x4C bit 0x10000000 is clear, or U+0x4C bit 0x40000000 is set: int P+0x190 += 1, and P+0x14A = 0xFF once it reaches 121. Otherwise (not held or none): P+0x190 = 0.
6. View 1 (cockpit, the view byte read after step 4): o = qword P+0x20; P+0x188 &= ~1; P+0x20 = 0. With held bit 8 and a target: d = R(U+0x10)⁻¹ · (W − U); P+0x20 = asinf(−d.y / `GetVectorLength`(d)), P+0x24 = atan2f(d.x, d.z); else P+0x190 = 0. Shake (X = P+0x20, Y = P+0x24, c = *0x0040DE54). Limits L = A+0x30…0x40 (a, b, c, d, e at +0x30, +0x34, +0x38, +0x3C, +0x40), or A+0xD4…0xE4 in 16:9. Yaw: P+0x24 += ((float)K+0x1B · c) / 255; += ((float)K+0x1A · −c) / 255; clamp to [−c, c]; P+0x24 = P+0x24 · 0.125 + o.y · 0.875. Pitch: P+0x20 += ((float)K+0x18 · *0x0040DE58 · 0.5) / 255; −= ((float)K+0x19 · *0x0040DE58 · 0.5) / 255; clamp to [*0x0040DE5C, *0x0040DE60]; P+0x20 = P+0x20 · 0.125 + o.x · 0.875. qword P+0x10 = P+0x20. With y = P+0x14 and h = *0x0040DE60: y > h → q = ((y − h) / (c − h))² · (d − e) + e; y < *0x0040DE5C → q = ((y + h) / (−c + h))² · (d − e) + e; else q = |y / h| · e. P+0x10 = max(P+0x10, q), then capped at *0x0040DE64. Words P+0x18 = P+0x1C = 0, P+0x188 |= 0x40, qword P+0x50 = A+0x20; P+0x24 > 0 → P+0x50 += (a · P+0x24) / c; P+0x24 < 0 → P+0x50 += (b · P+0x24) / −c.
7. Other views: held bit 8 and a target → P+0x188 |= 1, qword P+0x60 = W+0; else P+0x188 &= ~1. qword P+0x10 = 0. View 0: shake (X = P+0x10, Y = P+0x14, c = *0x0040DE68). P+0x10 += ((float)(s32)(K+0x18 − K+0x19) · *0x0040DE6C · 0.5) / 255; P+0x14 += ((float)(s32)(K+0x1B − K+0x1A) · *0x0040DE6C · 0.5) / 255, and += *0x0040DE6C when held bit 0x20. qword P+0x50 = A+0x20, P+0x30 = A+0, P+0x40 = A+0x10. View 2 additionally: P+0x44 −= P+0 · 60 · 5; P+0x70 = −P+0 · 6; P+0x74 = −P+4 · 12; P+0x78 = −P+8 · 3; P+0x40 −= P+4 · 60 · 5; P+0x48 −= (P+0x240 − 0.5) · 10; shake (X = P+0x40, Y = P+0x44, c = *0x0040DE70).
8. P+0x14 > *0x0040DE74 → −= *0x0040DE78; then < *0x0040DE7C → += *0x0040DE80. View ≠ 0 → P+0x18C = 0. View 0: held bit 8 → v = (float)K+0x1C when the pad mode word (main context + 0x210 + 0x50·p) − 5 < 2 (unsigned), else 0; v = (v − 128) · 0.0078125 when v ≥ 128, else 0; P+0x18C = (P+0x18C · 3 + v) · 0.25 (pressure zoom); not held → P+0x18C = 0.

#### Projectile-follow states (0x00410680[2], replay and missile cameras)
Record r: +0 missile record, +4 special-weapon record, +8 age, +0xC flags (1 active, 2 special weapon, 0x100 replay-locked), +0x10 missile slot, +0x14 special slot. Projectile block *0x004106AC.
- `FollowProjectile(n, kind, slot)`: in replay (main sub 0x11) with r+0xC bit 0 set → nothing. kind 0: flags = (flags | 1) & ~2, +0x10 = slot, +0 = block + 0x4000 + 0x60·slot. kind 1: when byte block + 0x4C5A + 0x80·slot == 0x10 → return; else +0x14 = slot, flags |= 3, +4 = block + 0x4C00 + 0x80·slot. Then (also for other kinds) in sub 0x11 `func_00136770(n, kind ∈ {0, 1})`; +8 = 0.
- `func_00136770(n, on)`: flag 0x100 set when on == 1, else cleared.
- `func_001365A0` (per frame, both records): when active: missile kind: when bit +0x10 of ~(word block+0x8A28) is set (the missile slot is free) → flags &= ~1, +0x10 = −1, +0 = 0; special kind: same test on block+0x8A2C with +0x14 → flags &= ~1, +0x14 = −1, +4 = 0; in every active case +8 += 1 (also on the frame the slot was dropped). The missile test shifts a 64-bit value by the slot, the special test a 32-bit one by slot & 31.

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

The texts come from the executable, read at runtime: `DrawHudWarning` 0x0016D348 takes 4 bytes per id from table 0x002B7450 (text index of line 1, text index of line 2 or 0xFF, colour flag, padding). The texts are a table of 8-byte entries {string pointer, two half-widths for the two HUD layouts} at 0x002B73D8. The colour flag is set only for id 1. The box fades in and out over 8 steps (blink phase +0x70, counter +0x64). Id 5 was confirmed on screen in PCSX2 (`../pcsx2/sessions.md` run 04), which also confirms the table decoding.

## Things the port must keep

- HUD drawing happens in the draw-list phase. It reads simulation state but never changes it, except the radar mode byte (+0x72, pad input in `DrawHud`) and the kill-message ring read side. These are game state and must still run once per simulated frame, not once per presented frame (ADR-002 interpolation: HUD logic stays on the simulation tick; only its drawing may be re-run).
- Camera shake uses the random table (`NextRandomFloat`), not `rand`.
- 16:9 layout rules above.

## Open points

None.
