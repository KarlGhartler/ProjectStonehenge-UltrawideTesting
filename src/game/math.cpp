// Math helpers: Euler rotations, angles and lengths (boot path and flight).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00102D28
// Rotation matrix from Euler angles at `angles` (x, y, z): each wrapped into (-pi, pi] with the
// .lit4 constants 0x0040DA14.., then RotZ(z) applied to m1 -> m0, RotX(x), RotY(y).
void func_00102D28(u32 m0, u32 m1, u32 angles) {
    f32 x = GRef<f32>(angles);
    f32 y = GRef<f32>(angles + 4);
    f32 z = GRef<f32>(angles + 8);
    while (GRef<f32>(0x0040DA14) < x) x = fpu::Sub(x, GRef<f32>(0x0040DA18));
    while (x < GRef<f32>(0x0040DA1C)) x = fpu::Add(x, GRef<f32>(0x0040DA20));
    while (GRef<f32>(0x0040DA24) < y) y = fpu::Sub(y, GRef<f32>(0x0040DA28));
    while (y < GRef<f32>(0x0040DA2C)) y = fpu::Add(y, GRef<f32>(0x0040DA30));
    while (GRef<f32>(0x0040DA34) < z) z = fpu::Sub(z, GRef<f32>(0x0040DA38));
    while (z < GRef<f32>(0x0040DA3C)) z = fpu::Add(z, GRef<f32>(0x0040DA40));
    sceVu0RotMatrixZ(m0, m1, z);
    sceVu0RotMatrixX(m0, m0, x);
    sceVu0RotMatrixY(m0, m0, y);
}

// EE: 0x00104F10
// Distance between two points (x, y, z): mula/madda/madd in the order x, y, z, then sqrt.
f32 func_00104F10(u32 a, u32 b) {
    const f32 dz = fpu::Sub(GRef<f32>(a + 8), GRef<f32>(b + 8));
    const f32 dx = fpu::Sub(GRef<f32>(a), GRef<f32>(b));
    const f32 dy = fpu::Sub(GRef<f32>(a + 4), GRef<f32>(b + 4));
    f32 acc = fpu::Mul(dx, dx);
    acc = fpu::MulAdd(acc, dy, dy);
    return fpu::Sqrt(fpu::MulAdd(acc, dz, dz));
}

// EE: 0x00102EF0
// Euler angles (x pitch, y yaw, z roll) of the rotation R(a) * R(b) (b applied first, as
// func_00102D28 builds it); copies a when b is zero.
void func_00102EF0(u32 out, u32 a, u32 b) {
    if (GRef<f32>(b) == 0.0f && GRef<f32>(b + 4) == 0.0f && GRef<f32>(b + 8) == 0.0f) {
        sceVu0CopyVector(out, a);
        return;
    }
    guest::GuestFrame frame(0x40);
    const u32 m = frame.Address();
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, b);
    func_00102D28(m, m, a);
    f32 s = -GRef<f32>(m + 0x24);
    if (1.0f < s) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    GRef<f32>(out) = ee::asinf(s);
    const f32 yaw = ee::atan2f(GRef<f32>(m + 0x20), GRef<f32>(m + 0x28));
    GRef<f32>(out + 4) = yaw;
    sceVu0RotMatrixY(m, m, -yaw);
    sceVu0RotMatrixX(m, m, -GRef<f32>(out));
    f32 c = GRef<f32>(m);
    if (1.0f < c) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    const f32 roll = ee::acosf(c);
    GRef<f32>(out + 8) = roll;
    if (GRef<f32>(m + 4) < 0.0f) GRef<f32>(out + 8) = -roll;
}

// EE: 0x00103070
// out = base + R(euler) * offset.
void func_00103070(u32 out, u32 base, u32 euler, u32 offset) {
    guest::GuestFrame frame(0x50);
    const u32 m = frame.Address(), v = frame.Address(0x40);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, euler);
    sceVu0ApplyMatrix(v, m, offset);
    sceVu0AddVector(out, base, v);
}

// EE: 0x001030F0
// out = R(euler) * v.
void func_001030F0(u32 out, u32 euler, u32 v) {
    guest::GuestFrame frame(0x40);
    const u32 m = frame.Address();
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, euler);
    sceVu0ApplyMatrix(out, m, v);
}

// EE: 0x00103290
// Euler angles of rotation matrix m: yaw = atan2(m20, m22), pitch = asin(-m21), roll from the
// matrix with yaw and pitch removed (acos of m00, sign of m01). No clamping here.
void func_00103290(u32 out, u32 m) {
    const f32 yaw = ee::atan2f(GRef<f32>(m + 0x20), GRef<f32>(m + 0x28));
    GRef<f32>(out + 4) = yaw;
    GRef<f32>(out) = ee::asinf(-GRef<f32>(m + 0x24));
    guest::GuestFrame frame(0x40);
    const u32 t = frame.Address();
    sceVu0RotMatrixY(t, m, -GRef<f32>(out + 4));
    sceVu0RotMatrixX(t, t, -GRef<f32>(out));
    const f32 roll = ee::acosf(GRef<f32>(t));
    GRef<f32>(out + 8) = roll;
    if (GRef<f32>(t + 4) < 0.0f) GRef<f32>(out + 8) = -roll;
}

// EE: 0x00103818
// Orientation of a line from point p towards the midpoint of q and r: yaw and pitch of
// p - (q + r) / 2, roll from r - q in that frame. out.w = 0.
void func_00103818(u32 out, u32 p, u32 q, u32 r) {
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(out + o) = 0;
    guest::GuestFrame frame(0x60);
    const u32 v = frame.Address(), mid = frame.Address(0x10), m = frame.Address(0x20);
    sceVu0AddVector(mid, q, r);
    sceVu0ScaleVector(mid, mid, 0.5f);
    sceVu0SubVector(v, p, mid);
    GRef<f32>(out + 4) = ee::atan2f(GRef<f32>(v), GRef<f32>(v + 8));
    const f32 len = GetVectorLength(v);
    GRef<f32>(out) = ee::asinf(fpu::Div(-GRef<f32>(v + 4), len));
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixY(m, m, -GRef<f32>(out + 4));
    sceVu0RotMatrixX(m, m, -GRef<f32>(out));
    sceVu0SubVector(v, r, q);
    sceVu0ApplyMatrix(v, m, v);
    GRef<f32>(out + 8) = -ee::atan2f(-GRef<f32>(v + 4), GRef<f32>(v));
}

// EE: 0x00103948
// Pitch of a - b in the frame turned by `yaw`: atan2(-v.y, v.z).
f32 func_00103948(f32 yaw, u32 a, u32 b) {
    guest::GuestFrame frame(0x50);
    const u32 v = frame.Address(), m = frame.Address(0x10);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixY(m, m, -yaw);
    sceVu0SubVector(v, a, b);
    sceVu0ApplyMatrix(v, m, v);
    return ee::atan2f(-GRef<f32>(v + 4), GRef<f32>(v + 8));
}

// EE: 0x001039D8
// Angle of b - a in the frame turned by `yaw`: atan2(v.y, v.x).
f32 func_001039D8(f32 yaw, u32 a, u32 b) {
    guest::GuestFrame frame(0x50);
    const u32 v = frame.Address(), m = frame.Address(0x10);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixY(m, m, -yaw);
    sceVu0SubVector(v, b, a);
    sceVu0ApplyMatrix(v, m, v);
    return ee::atan2f(GRef<f32>(v + 4), GRef<f32>(v));
}

// EE: 0x00104FA8
// Horizontal length sqrt(x^2 + z^2) (mula, madd, sqrt.s).
f32 func_00104FA8(u32 v) {
    const f32 x = GRef<f32>(v), z = GRef<f32>(v + 8);
    return fpu::Sqrt(fpu::MulAdd(fpu::Mul(x, x), z, z));
}

// EE: 0x00109598
// Dead (no reference): out = out * (RotX(a.x) RotZ(a.z) applied to the unit matrix, times the
// inverse of the same for b), with a / b the Euler angles of directions `dir_a` / `dir_b`
// (func_00103330): the rotation that turns direction b into direction a, appended to `out`.
void func_00109598(u32 out, u32 dir_a, u32 dir_b) {
    guest::GuestFrame frame(0xA0);
    const u32 m0 = frame.Address(), m1 = m0 + 0x40, a = m0 + 0x80, b = m0 + 0x90;
    func_00103330(a, dir_a);
    func_00103330(b, dir_b);
    sceVu0UnitMatrix(m1);
    sceVu0RotMatrixZ(m1, m1, GRef<f32>(b + 8));
    sceVu0RotMatrixX(m1, m1, GRef<f32>(b));
    sceVu0InversMatrix(m1, m1);
    sceVu0UnitMatrix(m0);
    sceVu0RotMatrixZ(m0, m0, GRef<f32>(a + 8));
    sceVu0RotMatrixX(m0, m0, GRef<f32>(a));
    sceVu0MulMatrix(m0, m1, m0);
    sceVu0MulMatrix(out, out, m0);
}

// EE: 0x00109658
// VU0 unit normal of triangle (a, b, c): n = (c - b) x (a - b) (vopmula / vopmsub), n.w = a.w - b.w;
// out = n / |n.xyz| (all four lanes).
void func_00109658(u32 out, u32 a, u32 b, u32 c) {
    auto ld = [](u32 p) { return vu0::Vec4{GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; };
    const vu0::Vec4 va = ld(a), vb = ld(b), vc = ld(c);
    const vu0::Vec4 e1{vu0::Sub(va.x, vb.x), vu0::Sub(va.y, vb.y), vu0::Sub(va.z, vb.z), vu0::Sub(va.w, vb.w)};
    const vu0::Vec4 e2{vu0::Sub(vc.x, vb.x), vu0::Sub(vc.y, vb.y), vu0::Sub(vc.z, vb.z), vu0::Sub(vc.w, vb.w)};
    vu0::Vec4 n;
    n.x = vu0::Sub(vu0::Mul(e2.y, e1.z), vu0::Mul(e1.y, e2.z));
    n.y = vu0::Sub(vu0::Mul(e2.z, e1.x), vu0::Mul(e1.z, e2.x));
    n.z = vu0::Sub(vu0::Mul(e2.x, e1.y), vu0::Mul(e1.x, e2.y));
    n.w = e1.w;
    const f32 s = vu0::Add(vu0::Add(vu0::Mul(n.x, n.x), vu0::Mul(n.y, n.y)), vu0::Mul(n.z, n.z));
    const f32 len = vu0::Mul(1.0f, vu0::Sqrt(s));
    const f32 q = vu0::Div(1.0f, len);
    GRef<f32>(out) = vu0::Mul(n.x, q);
    GRef<f32>(out + 4) = vu0::Mul(n.y, q);
    GRef<f32>(out + 8) = vu0::Mul(n.z, q);
    GRef<f32>(out + 12) = vu0::Mul(n.w, q);
}

// EE: 0x00103330
// Dead (called only by the unreferenced 0x00109598): Euler angles of direction v: z = atan2(v.x,
// -v.y), x = atan2(|v.xy|, v.z), y = atan2(v.x, v.z); v.x = v.y = 0 gives (0, 0, 0) or (0, -pi, -pi)
// by the sign of v.z (K 0x0040DA44). The original's NaN fallback to the library sqrtf after
// sqrt.s never applies on the EE (no NaN).
void func_00103330(u32 out, u32 v) {
    const f32 x = GRef<f32>(v);
    if (x == 0.0f && GRef<f32>(v + 4) == 0.0f) {
        GRef<f32>(out) = 0.0f;
        if (0.0f <= GRef<f32>(v + 8)) {
            GRef<f32>(out + 4) = 0.0f;
            GRef<f32>(out + 8) = 0.0f;
        } else {
            GRef<f32>(out + 4) = GRef<f32>(0x0040DA44);
            GRef<f32>(out + 8) = GRef<f32>(0x0040DA44);
        }
        return;
    }
    GRef<f32>(out + 8) = ee::atan2f(x, -GRef<f32>(v + 4));
    const f32 vx = GRef<f32>(v), vy = GRef<f32>(v + 4);
    const f32 d = fpu::Sqrt(fpu::Add(vx * vx, vy * vy));
    GRef<f32>(out) = ee::atan2f(d, GRef<f32>(v + 8));
    GRef<f32>(out + 4) = ee::atan2f(GRef<f32>(v), GRef<f32>(v + 8));
}

// EE: 0x00103420
// Dead (only callers unreferenced): spheres (a, r1) and (b, r2) overlap: |a - b|^2 <= (r1 + r2)^2.
// fpucheck: verified 3 (1 add for the radii, 2 for the squared length)
s32 func_00103420(u32 a, u32 b, f32 r1, f32 r2) {
    guest::GuestFrame frame(0x10);
    const u32 d = frame.Address();
    sceVu0SubVector(d, a, b);
    sceVu0MulVector(d, d, d);
    const f32 r = fpu::Add(r1, r2);
    const f32 sum = fpu::Add(fpu::Add(GRef<f32>(d), GRef<f32>(d + 4)), GRef<f32>(d + 8));
    return sum <= r * r ? 1 : 0;
}

// EE: 0x001034A0
// Dead (no reference): segment p0-p1 touches sphere (c, r1 + r2): either end inside, or the
// closest point of the segment (projection parameter 0..1) inside. Equal ends test p0 only.
// fpucheck: verified 7 (2 + 2 dot products, 3 for the closest point)
s32 func_001034A0(u32 p0, u32 p1, u32 c, f32 r1, f32 r2) {
    if (GRef<f32>(p0) == GRef<f32>(p1) && GRef<f32>(p0 + 4) == GRef<f32>(p1 + 4) &&
        GRef<f32>(p0 + 8) == GRef<f32>(p1 + 8))
        return func_00103420(p0, c, r1, r2);
    if (func_00103420(p0, c, r1, r2) == 1) return 1;
    if (func_00103420(p1, c, r1, r2) == 1) return 1;
    guest::GuestFrame frame(0x30);
    const u32 d = frame.Address(), e = d + 0x10;
    sceVu0SubVector(d, p1, p0);
    sceVu0SubVector(e, c, p0);
    const f32 dx = GRef<f32>(d), dy = GRef<f32>(d + 4), dz = GRef<f32>(d + 8);
    const f32 ex = GRef<f32>(e), ey = GRef<f32>(e + 4), ez = GRef<f32>(e + 8);
    const f32 len2 = fpu::Add(fpu::Add(dx * dx, dy * dy), dz * dz);
    const f32 dot = fpu::Add(fpu::Add(ex * dx, ey * dy), ez * dz);
    // div.s: len2 is not 0 for distinct points (a 0 gives +-FLT_MAX on the EE).
    const f32 t = fpu::Div(dot, len2);
    if (t < 0.0f) return 0;
    if (1.0f < t) return 0;
    const u32 q = frame.Address() + 0x20;
    GRef<f32>(q + 8) = fpu::Add(dz * t, GRef<f32>(p0 + 8));
    GRef<f32>(q) = fpu::Add(dx * t, GRef<f32>(p0));
    GRef<f32>(q + 4) = fpu::Add(dy * t, GRef<f32>(p0 + 4));
    return func_00103420(q, c, r1, r2);
}

// EE: 0x00103660
// Dead (no reference): as func_001034A0 for the segment from p along d (end p + d); a zero d tests
// p only.
// fpucheck: verified 7 (2 + 2 dot products, 3 for the closest point; the end p + d on VU0)
s32 func_00103660(u32 p, u32 d, u32 c, f32 r1, f32 r2) {
    if (GRef<f32>(d) == 0.0f && GRef<f32>(d + 4) == 0.0f && GRef<f32>(d + 8) == 0.0f)
        return func_00103420(p, c, r1, r2);
    guest::GuestFrame frame(0x30);
    const u32 end = frame.Address(), e = end + 0x10;
    sceVu0AddVector(end, p, d);
    if (func_00103420(p, c, r1, r2) == 1) return 1;
    if (func_00103420(end, c, r1, r2) == 1) return 1;
    sceVu0SubVector(e, c, p);
    const f32 dx = GRef<f32>(d), dy = GRef<f32>(d + 4), dz = GRef<f32>(d + 8);
    const f32 ex = GRef<f32>(e), ey = GRef<f32>(e + 4), ez = GRef<f32>(e + 8);
    const f32 len2 = fpu::Add(fpu::Add(dx * dx, dy * dy), dz * dz);
    const f32 dot = fpu::Add(fpu::Add(ex * dx, ey * dy), ez * dz);
    // div.s: len2 is not 0 for distinct points (a 0 gives +-FLT_MAX on the EE).
    const f32 t = fpu::Div(dot, len2);
    if (t < 0.0f) return 0;
    if (1.0f < t) return 0;
    const u32 q = frame.Address() + 0x20;
    GRef<f32>(q + 8) = fpu::Add(dz * t, GRef<f32>(p + 8));
    GRef<f32>(q) = fpu::Add(dx * t, GRef<f32>(p));
    GRef<f32>(q + 4) = fpu::Add(dy * t, GRef<f32>(p + 4));
    return func_00103420(q, c, r1, r2);
}

// EE: 0x00103A60
// Dead (only callers unreferenced): acos polynomial pi/2 - (x + x^3 (k0 x^6 + k1 x^4 + k2 x^2 + k3))
// (constants 0x0040DA48-0x0040DA58).
// fpucheck: verified 5 (4 adds, 1 sub)
f32 func_00103A60(f32 x) {
    const f32 x2 = x * x;
    f32 p = fpu::Add(x2 * GRef<f32>(0x0040DA48), GRef<f32>(0x0040DA4C));
    p = fpu::Add(x2 * p, GRef<f32>(0x0040DA50));
    p = fpu::Add(x2 * p, GRef<f32>(0x0040DA54));
    const f32 r = fpu::Add(x2 * p * x, x);
    return fpu::Sub(GRef<f32>(0x0040DA58), r);
}

// EE: 0x00103AA8
// Dead (no reference): odd series x (1 - x^2 (1/3 - x^2 (1/5 - x^2 (1/7 - x^2 / 9)))) (constants
// 0x0040DA5C-0x0040DA68).
// fpucheck: verified 4 (4 subs)
f32 func_00103AA8(f32 x) {
    const f32 x2 = x * x;
    const f32 a = fpu::Sub(GRef<f32>(0x0040DA60), x2 * GRef<f32>(0x0040DA5C));
    const f32 b = fpu::Sub(GRef<f32>(0x0040DA64), x2 * a);
    const f32 c = fpu::Sub(GRef<f32>(0x0040DA68), x2 * b);
    return x * fpu::Sub(1.0f, x2 * c);
}

// EE: 0x00103AF0
// Dead (no reference): runs acosf and the acos polynomial func_00103A60 over -1 .. 1 in steps of
// 0.01 (k 0x0040DA6C); results discarded (a timing test).
// fpucheck: verified 1
void func_00103AF0() {
    const f32 step = GRef<f32>(0x0040DA6C);
    f32 x = -1.0f;
    do {
        ee::acosf(x);
        const f32 a = x;
        x = fpu::Add(x, step);
        func_00103A60(a);
    } while (x < 1.0f);
}

// EE: 0x00103B50
// Dead (no reference): acosf over 0 .. 1 in steps of 0.0001 (k 0x0040DA70); results discarded.
// fpucheck: verified 1
void func_00103B50() {
    const f32 step = GRef<f32>(0x0040DA70);
    f32 x = 0.0f;
    do {
        const f32 a = x;
        x = fpu::Add(x, step);
        ee::acosf(a);
    } while (x < 1.0f);
}

// EE: 0x00103BA8
// Dead (no reference): func_00103A60 over 0 .. 1 in steps of 0.0001 (k 0x0040DA74); results
// discarded.
// fpucheck: verified 1
void func_00103BA8() {
    const f32 step = GRef<f32>(0x0040DA74);
    f32 x = 0.0f;
    do {
        const f32 a = x;
        x = fpu::Add(x, step);
        func_00103A60(a);
    } while (x < 1.0f);
}

}  // namespace ac04::game
