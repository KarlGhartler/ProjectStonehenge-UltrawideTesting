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

}  // namespace ac04::game
