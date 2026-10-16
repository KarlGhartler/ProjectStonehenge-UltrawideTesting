# Math helpers (game code)

Small vector/angle helpers used by the flight scene, cameras and sky. All use VU0 macro mode through the SDK (`sceVu0*`, `src/common/vu0_math.h`, round to nearest) or the EE FPU (`src/common/ee_math.h`). Angles are radians. A vector is 4 floats (x, y, z, w).

Notation: `R(a)` = the rotation matrix of the Euler angles a = (x, y, z) built by `func_00102D28` (each angle wrapped into range with the constants at 0x0040DA14…, then `sceVu0RotMatrixZ(z)`, `sceVu0RotMatrixX(x)`, `sceVu0RotMatrixY(y)` applied in that order to the input matrix; translated in `src/game/math.cpp`). `RotX/RotY(m, θ)` = `sceVu0RotMatrixX/Y` applied to m.

| Function | Behaviour |
|---|---|
| `func_00102D28(m0, m1, a)` | m0 = R(a) applied to m1 (translated). |
| `func_00102EF0(out, a, b)` | Euler angles of the composition R(a) · R(b). When b.x = b.y = b.z = 0: `sceVu0CopyVector(out, a)`. Else M = unit, `func_00102D28(M, M, b)`, `func_00102D28(M, M, a)`; out.x = asinf(clamp(−M[2][1], −1, 1)) (word M+0x24), out.y = atan2f(M[2][0], M[2][2]) (M+0x20, M+0x28); M = RotY(M, −out.y), M = RotX(M, −out.x); out.z = acosf(clamp(M[0][0], −1, 1)), negated when M[0][1] < 0. out.w unchanged. |
| `func_00103290(out, M)` | Euler angles of a rotation matrix: out.y = atan2f(M+0x20, M+0x28), out.x = asinf(−M+0x24) (no clamp), T = RotX(RotY(M, −out.y), −out.x), out.z = acosf(T[0][0]) (no clamp), negated when T[0][1] < 0. |
| `func_00103070(out, base, a, v)` | out = base + R(a) · v (`sceVu0ApplyMatrix`, `sceVu0AddVector`, all four fields). |
| `func_001030F0(out, a, v)` | out = R(a) · v. |
| `func_00103818(out, p, a, b)` | Orientation looking from the midpoint of a, b towards p with roll from the a→b line: out = 0 (128-bit); m = (a + b) · 0.5 (`sceVu0AddVector`, `sceVu0ScaleVector(0.5)`); d = p − m; out.y = atan2f(d.x, d.z); out.x = asinf(−d.y / `GetVectorLength`(d)); M = RotX(RotY(unit, −out.y), −out.x); e = M · (b − a); out.z = −atan2f(−e.y, e.x). |
| `func_00103948(yaw, a, b)` | returns atan2f(−d.y, d.z) with d = RotY(unit, −yaw) · (a − b) (relative pitch). yaw in f12, result in f0. |
| `func_001039D8(yaw, a, b)` | returns atan2f(d.y, d.x) with d = RotY(unit, −yaw) · (b − a). |
| `func_00104FA8(v)` | sqrtf(x·x + z·z): `mula.s x, x`, `madd.s z, z`, `sqrt.s` (horizontal length; the accumulate is one fused EE operation pair, use `fpu::Mul`/`fpu::MulAdd`). |
| `func_00104F10(a, b)` | distance between two points (translated, `src/game/math.cpp`). |
| `func_001050D8(out, a, b, t)` | 16-byte colour lerp (MMI): t16 = (s16) cvt.w.s(t · 4096.0); for each of the 16 bytes: out = clamp(a + (((b − a) · t16) >> 12), 0, 255) where the difference is a 16-bit lane, the product 32-bit (`pmulth`, low/high lanes recombined with `pmfhl.uw`/`pinteh`), the shift arithmetic, the add saturating 16-bit (`paddsh`); packed back with `ppacb`. t in f12; a, b, out are 16-byte aligned. |
| `func_001025C8(pad, on, strength)` | vibration request: unless pad byte +0x44 bit 0 (locked) is set or word +0 == 6: byte +0x45 = (on ≠ 0), byte +0x46 = strength, words +0x48 = +0x4C = 16. Returns 0. |
