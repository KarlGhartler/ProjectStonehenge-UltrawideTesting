// libvu0 (SDK 2.3) matrix and vector functions the game calls, translated from the VU0 macro code
// (0x00262F10-0x00263820) with the VU0 arithmetic of common/vu0_math.h (chop rounding, separate
// multiply and add). Matrices are 4x4 floats, column-major as libvu0 stores them (m + 16 * column,
// 16-byte aligned); vectors are 4 floats.
#include <cmath>

#include "common/ee_math.h"
#include "common/guest_ref.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "platform/log.h"
#include "sce/sce.h"

using ac04::guest::GRef;
namespace vu0 = ac04::vu0;
namespace fpu = ac04::fpu;

namespace {

vu0::Vec4 LoadV(u32 a) { return {GRef<f32>(a), GRef<f32>(a + 4), GRef<f32>(a + 8), GRef<f32>(a + 12)}; }

void StoreV(u32 a, const vu0::Vec4& v) {
    GRef<f32>(a) = v.x;
    GRef<f32>(a + 4) = v.y;
    GRef<f32>(a + 8) = v.z;
    GRef<f32>(a + 12) = v.w;
}

// m0 column i = (c0 c1 c2 c3) * m1 column i, columns of m1 read one at a time (as the loops do).
void MulColumns(u32 m0, const vu0::Vec4& c0, const vu0::Vec4& c1, const vu0::Vec4& c2, const vu0::Vec4& c3, u32 m1) {
    for (u32 i = 0; i < 4; ++i) StoreV(m0 + 16 * i, vu0::MulMatrix(c0, c1, c2, c3, LoadV(m1 + 16 * i)));
}

// _sceVu0ecossin (0x00263258): for t = pi/2 -+ angle, s = t + c3 t^3 + c2 t^5 + c1 t^7 + c0 t^9
// (coefficients 0x003FAB90, sums in the order of the code) = cos(angle); c = sqrt(1 - s^2), negated
// when `negate` = |sin(angle)| with the angle's sign. Returns (sin, cos).
void EcosSin(f32 t, bool negate, f32& sin_out, f32& cos_out) {
    const f32 c0 = GRef<f32>(0x003FAB90), c1 = GRef<f32>(0x003FAB94);
    const f32 c2 = GRef<f32>(0x003FAB98), c3 = GRef<f32>(0x003FAB9C);
    const f32 t2 = vu0::Mul(t, t);
    // vf8 = c * t, then * t2 (all), * t2 (xyz), * t2 (xy), * t2 (x).
    f32 p0 = vu0::Mul(c0, t), p1 = vu0::Mul(c1, t), p2 = vu0::Mul(c2, t), p3 = vu0::Mul(c3, t);
    p0 = vu0::Mul(p0, t2);
    p1 = vu0::Mul(p1, t2);
    p2 = vu0::Mul(p2, t2);
    p3 = vu0::Mul(p3, t2);
    p0 = vu0::Mul(p0, t2);
    p1 = vu0::Mul(p1, t2);
    p2 = vu0::Mul(p2, t2);
    f32 s = vu0::Add(t, p3);
    p0 = vu0::Mul(p0, t2);
    p1 = vu0::Mul(p1, t2);
    s = vu0::Add(s, p2);
    p0 = vu0::Mul(p0, t2);
    s = vu0::Add(s, p1);
    s = vu0::Add(s, p0);
    const f32 q = vu0::Sqrt(vu0::Sub(1.0f, vu0::Mul(s, s)));
    sin_out = negate ? vu0::Sub(0.0f, q) : q;
    cos_out = s;
}

// The angle argument of the RotMatrix functions: pi/2 + a for a < 0 (then the sine is negated),
// else pi/2 - a (EE FPU add/sub).
f32 EcosSinArg(f32 a, bool& negate) {
    const f32 half_pi = 1.57079637f;  // 0x3FC90FDB
    negate = a < 0.0f;
    return negate ? fpu::Add(half_pi, a) : fpu::Sub(half_pi, a);
}

}  // namespace

// PS2 (0x00262F10): v0 = m * v1 (vmulax/vmadday/vmaddaz/vmaddw).
void sceVu0ApplyMatrix(u32 v0, u32 m, u32 v1) {
    StoreV(v0, vu0::MulMatrix(LoadV(m), LoadV(m + 0x10), LoadV(m + 0x20), LoadV(m + 0x30), LoadV(v1)));
}

// PS2 (0x00262F40): m0 = m1 * m2, column by column (m1 is loaded first; m0 may alias m2).
void sceVu0MulMatrix(u32 m0, u32 m1, u32 m2) {
    MulColumns(m0, LoadV(m1), LoadV(m1 + 0x10), LoadV(m1 + 0x20), LoadV(m1 + 0x30), m2);
}

// PS2 (0x00263050): inverse of a rotation + translation matrix: columns 0-2 = the transposed 3x3
// with w = 0; column 3 = -(R^T * t) with the w of m1's column 3.
void sceVu0InversMatrix(u32 m0, u32 m1) {
    const vu0::Vec4 a = LoadV(m1), b = LoadV(m1 + 0x10), c = LoadV(m1 + 0x20), t = LoadV(m1 + 0x30);
    const vu0::Vec4 r0{a.x, b.x, c.x, 0.0f}, r1{a.y, b.y, c.y, 0.0f}, r2{a.z, b.z, c.z, 0.0f};
    vu0::Vec4 out;
    out.x = vu0::Sub(0.0f, vu0::MulAdd(vu0::MulAdd(vu0::Mul(r0.x, t.x), r1.x, t.y), r2.x, t.z));
    out.y = vu0::Sub(0.0f, vu0::MulAdd(vu0::MulAdd(vu0::Mul(r0.y, t.x), r1.y, t.y), r2.y, t.z));
    out.z = vu0::Sub(0.0f, vu0::MulAdd(vu0::MulAdd(vu0::Mul(r0.z, t.x), r1.z, t.y), r2.z, t.z));
    out.w = t.w;
    StoreV(m0, r0);
    StoreV(m0 + 0x10, r1);
    StoreV(m0 + 0x20, r2);
    StoreV(m0 + 0x30, out);
}

// PS2 (0x00263100): v0 = v1 * t + v2 * (1 - t), per component (vmulax then vmaddx; 1 - t by vsub.x
// from vaddw.x = 1).
void sceVu0InterVector(u32 v0, u32 v1, u32 v2, f32 t) {
    const f32 u = vu0::Sub(1.0f, t);
    const vu0::Vec4 a = LoadV(v1), b = LoadV(v2);
    StoreV(v0, {vu0::MulAdd(vu0::Mul(a.x, t), b.x, u), vu0::MulAdd(vu0::Mul(a.y, t), b.y, u),
                vu0::MulAdd(vu0::Mul(a.z, t), b.z, u), vu0::MulAdd(vu0::Mul(a.w, t), b.w, u)});
}

// PS2 (0x00263188): m0 = m1 with tv added to the translation (column 3 xyz).
void sceVu0TransMatrix(u32 m0, u32 m1, u32 tv) {
    vu0::Vec4 t = LoadV(m1 + 0x30);
    const vu0::Vec4 v = LoadV(tv);
    const vu0::Vec4 a = LoadV(m1), b = LoadV(m1 + 0x10), c = LoadV(m1 + 0x20);
    t.x = vu0::Add(t.x, v.x);
    t.y = vu0::Add(t.y, v.y);
    t.z = vu0::Add(t.z, v.z);
    StoreV(m0, a);
    StoreV(m0 + 0x10, b);
    StoreV(m0 + 0x20, c);
    StoreV(m0 + 0x30, t);
}

// PS2 (0x00263170): v0 = v1 * t (vmulx.xyzw).
void sceVu0ScaleVector(u32 v0, u32 v1, f32 t) {
    const vu0::Vec4 a = LoadV(v1);
    StoreV(v0, {vu0::Mul(a.x, t), vu0::Mul(a.y, t), vu0::Mul(a.z, t), vu0::Mul(a.w, t)});
}

// PS2 (0x002631B8): v0 = v1 (128-bit copy).
void sceVu0CopyVector(u32 v0, u32 v1) {
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(v0 + i) = GRef<u32>(v1 + i);
}

// PS2 (0x002631C8): m0 = m1 (four 128-bit loads, then four stores).
void sceVu0CopyMatrix(u32 m0, u32 m1) {
    u32 w[16];
    for (u32 i = 0; i < 16; ++i) w[i] = GRef<u32>(m1 + 4 * i);
    for (u32 i = 0; i < 16; ++i) GRef<u32>(m0 + 4 * i) = w[i];
}

// PS2 (0x00263230): identity.
void sceVu0UnitMatrix(u32 m) {
    StoreV(m + 0x30, {0.0f, 0.0f, 0.0f, 1.0f});
    StoreV(m + 0x20, {0.0f, 0.0f, 1.0f, 0.0f});
    StoreV(m + 0x10, {0.0f, 1.0f, 0.0f, 0.0f});
    StoreV(m, {1.0f, 0.0f, 0.0f, 0.0f});
}

// PS2 (0x002632D0): m0 = RotZ(rz) * m1, RotZ columns (cos, sin, 0, 0), (-sin, cos, 0, 0), z, w.
void sceVu0RotMatrixZ(u32 m0, u32 m1, f32 rz) {
    bool neg = false;
    const f32 t = EcosSinArg(rz, neg);
    f32 s, c;
    EcosSin(t, neg, s, c);
    MulColumns(m0, {c, s, 0.0f, 0.0f}, {vu0::Sub(0.0f, s), c, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
               {0.0f, 0.0f, 0.0f, 1.0f}, m1);
}

// PS2 (0x00263378): m0 = RotX(rx) * m1, RotX columns x, (0, cos, sin, 0), (0, -sin, cos, 0), w.
void sceVu0RotMatrixX(u32 m0, u32 m1, f32 rx) {
    bool neg = false;
    const f32 t = EcosSinArg(rx, neg);
    f32 s, c;
    EcosSin(t, neg, s, c);
    MulColumns(m0, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, c, s, 0.0f}, {0.0f, vu0::Sub(0.0f, s), c, 0.0f},
               {0.0f, 0.0f, 0.0f, 1.0f}, m1);
}

// PS2 (0x00263420): m0 = RotY(ry) * m1, RotY columns (cos, 0, -sin, 0), y, (sin, 0, cos, 0), w.
void sceVu0RotMatrixY(u32 m0, u32 m1, f32 ry) {
    bool neg = false;
    const f32 t = EcosSinArg(ry, neg);
    f32 s, c;
    EcosSin(t, neg, s, c);
    MulColumns(m0, {c, 0.0f, vu0::Sub(0.0f, s), 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {s, 0.0f, c, 0.0f},
               {0.0f, 0.0f, 0.0f, 1.0f}, m1);
}

// PS2 (0x00263718): view-to-screen matrix. With d = zmax - zmin (EE FPU), the depth row terms are
// zz = farz * nearz * d / (farz - nearz) and zw = (-zmax * nearz + zmin * farz) / (farz - nearz).
// m = unit with m00 = m11 = scrz, m22 = m33 = 0, m23 = m32 = 1; then m = S * m with S = unit,
// S00 = ax, S11 = ay, S22 = zz, S30 = cx, S31 = cy, S32 = zw (S on the guest stack, as the SDK).
void sceVu0ViewScreenMatrix(u32 m, f32 scrz, f32 ax, f32 ay, f32 cx, f32 cy, f32 zmin, f32 zmax, f32 nearz,
                            f32 farz) {
    const f32 d = fpu::Add(-zmin, zmax);
    f32 zz = farz * nearz;
    f32 zw = -zmax * nearz;
    const f32 zf = zmin * farz;
    zz = zz * d;
    zw = fpu::Add(zw, zf);
    const f32 den = fpu::Add(-nearz, farz);
    zz = zz / den;
    zw = zw / den;
    sceVu0UnitMatrix(m);
    GRef<f32>(m + 0x14) = scrz;
    GRef<f32>(m) = scrz;
    GRef<u32>(m + 0x28) = 0;
    GRef<u32>(m + 0x3C) = 0;
    GRef<f32>(m + 0x2C) = 1.0f;
    GRef<f32>(m + 0x38) = 1.0f;
    ac04::guest::GuestFrame frame(0x40);
    const u32 s = frame.Address();
    sceVu0UnitMatrix(s);
    GRef<f32>(s) = ax;
    GRef<f32>(s + 0x14) = ay;
    GRef<f32>(s + 0x28) = zz;
    GRef<f32>(s + 0x30) = cx;
    GRef<f32>(s + 0x34) = cy;
    GRef<f32>(s + 0x38) = zw;
    sceVu0MulMatrix(m, s, m);
}

// PS2 (0x00263128 / 0x00263140 / 0x00263158): out = a + b / a - b / a * b (vadd / vsub / vmul .xyzw).
void sceVu0AddVector(u32 out, u32 a, u32 b) {
    const vu0::Vec4 x = LoadV(a), y = LoadV(b);
    StoreV(out, {vu0::Add(x.x, y.x), vu0::Add(x.y, y.y), vu0::Add(x.z, y.z), vu0::Add(x.w, y.w)});
}

void sceVu0SubVector(u32 out, u32 a, u32 b) {
    const vu0::Vec4 x = LoadV(a), y = LoadV(b);
    StoreV(out, {vu0::Sub(x.x, y.x), vu0::Sub(x.y, y.y), vu0::Sub(x.z, y.z), vu0::Sub(x.w, y.w)});
}

void sceVu0MulVector(u32 out, u32 a, u32 b) {
    const vu0::Vec4 x = LoadV(a), y = LoadV(b);
    StoreV(out, {vu0::Mul(x.x, y.x), vu0::Mul(x.y, y.y), vu0::Mul(x.z, y.z), vu0::Mul(x.w, y.w)});
}

namespace {

// vftoi0: truncation toward zero, saturated to the 32-bit range.
s32 FloatToInt0(f32 v) {
    const double d = static_cast<double>(v);
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return static_cast<s32>(d);
}

}  // namespace

// PS2 (0x002631F0 / 0x00263200): vftoi4 / vftoi0 .xyzw.
void sceVu0FTOI4Vector(u32 out, u32 v) {
    const vu0::Vec4 x = LoadV(v);
    GRef<s32>(out) = vu0::FloatToInt4(x.x);
    GRef<s32>(out + 4) = vu0::FloatToInt4(x.y);
    GRef<s32>(out + 8) = vu0::FloatToInt4(x.z);
    GRef<s32>(out + 12) = vu0::FloatToInt4(x.w);
}

void sceVu0FTOI0Vector(u32 out, u32 v) {
    const vu0::Vec4 x = LoadV(v);
    GRef<s32>(out) = FloatToInt0(x.x);
    GRef<s32>(out + 4) = FloatToInt0(x.y);
    GRef<s32>(out + 8) = FloatToInt0(x.z);
    GRef<s32>(out + 12) = FloatToInt0(x.w);
}

// PS2 (0x00262FD0): out = v.xyz / |v.xyz|, out.w = 0. s = x^2 + y^2 + z^2 (vmul.xyz, vaddy.x,
// vaddz.x); t = 0 + sqrt(s) (vaddq.x); Q = 1 / t (vdiv vf0.w); out.xyz = v.xyz * Q.
void sceVu0Normalize(u32 out, u32 v) {
    const vu0::Vec4 x = LoadV(v);
    const f32 s = vu0::Add(vu0::Add(vu0::Mul(x.x, x.x), vu0::Mul(x.y, x.y)), vu0::Mul(x.z, x.z));
    const f32 t = vu0::Add(0.0f, vu0::Sqrt(s));
    const f32 q = vu0::Div(1.0f, t);
    StoreV(out, {vu0::Mul(x.x, q), vu0::Mul(x.y, q), vu0::Mul(x.z, q), 0.0f});
}

// PS2 (0x00263A08): t = m * v; t.xyz *= 1 / t.w; out = vftoi4(t); mode != 0: out.zw = vftoi0(t.zw).
void sceVu0RotTransPers(u32 out, u32 m, u32 v, s32 mode) {
    vu0::Vec4 t = vu0::MulMatrix(LoadV(m), LoadV(m + 0x10), LoadV(m + 0x20), LoadV(m + 0x30), LoadV(v));
    const f32 q = vu0::Div(1.0f, t.w);
    t.x = vu0::Mul(t.x, q);
    t.y = vu0::Mul(t.y, q);
    t.z = vu0::Mul(t.z, q);
    GRef<s32>(out) = vu0::FloatToInt4(t.x);
    GRef<s32>(out + 4) = vu0::FloatToInt4(t.y);
    GRef<s32>(out + 8) = vu0::FloatToInt4(t.z);
    GRef<s32>(out + 12) = vu0::FloatToInt4(t.w);
    if (mode != 0) {
        GRef<s32>(out + 8) = FloatToInt0(t.z);
        GRef<s32>(out + 12) = FloatToInt0(t.w);
    }
}

// PS2 (0x00263820), mode 0 (parallel light; the only mode the game uses), EE FPU:
// d = (a x + b y) + c z, r = -1 / d; columns (r(ax - d), r ay, r az, 0), (r bx, r(by - d), r bz, 0),
// (r cx, r cy, r(cz - d), 0), (-r x, -r y, -r z, -r d) with (x, y, z) = lp.
void sceVu0DropShadowMatrix(u32 m, u32 lp, f32 a, f32 b, f32 c, s32 mode) {
    if (mode != 0) {
        ac04::log::WriteOnce("vu0-dropshadow-mode", ac04::log::Level::Warning, "sce",
                             "sceVu0DropShadowMatrix: point-light mode %d not implemented", mode);
        return;
    }
    const f32 x = GRef<f32>(lp), y = GRef<f32>(lp + 4), z = GRef<f32>(lp + 8);
    const f32 ax = a * x, by = b * y, cz = c * z;
    const f32 d = fpu::Add(fpu::Add(ax, by), cz);
    const f32 r = fpu::Div(-1.0f, d);
    GRef<f32>(m + 0x00) = r * fpu::Sub(ax, d);
    GRef<f32>(m + 0x04) = r * (a * y);
    GRef<f32>(m + 0x08) = r * (a * z);
    GRef<u32>(m + 0x0C) = 0;
    GRef<f32>(m + 0x10) = r * (b * x);
    GRef<f32>(m + 0x14) = r * fpu::Sub(by, d);
    GRef<f32>(m + 0x18) = r * (b * z);
    GRef<u32>(m + 0x1C) = 0;
    GRef<f32>(m + 0x20) = r * (c * x);
    GRef<f32>(m + 0x24) = r * (c * y);
    GRef<f32>(m + 0x28) = r * fpu::Sub(cz, d);
    GRef<u32>(m + 0x2C) = 0;
    GRef<f32>(m + 0x30) = r * -x;
    GRef<f32>(m + 0x34) = r * -y;
    GRef<f32>(m + 0x38) = r * -z;
    GRef<f32>(m + 0x3C) = r * -d;
}

// PS2 (0x00263008): out = m transposed (MMI word moves; bit-exact, out may alias m).
void sceVu0TransposeMatrix(u32 out, u32 m) {
    u32 t[16];
    for (u32 i = 0; i < 16; ++i) t[i] = GRef<u32>(m + 4 * i);
    for (u32 c = 0; c < 4; ++c)
        for (u32 r = 0; r < 4; ++r) GRef<u32>(out + 16 * c + 4 * r) = t[4 * r + c];
}

// PS2 (0x00262F88): out = a x b (vopmula / vopmsub), out.w = 0 (vsub.w of itself).
void sceVu0OuterProduct(u32 out, u32 a, u32 b) {
    const vu0::Vec4 x = LoadV(a), y = LoadV(b);
    StoreV(out, {vu0::Sub(vu0::Mul(x.y, y.z), vu0::Mul(y.y, x.z)), vu0::Sub(vu0::Mul(x.z, y.x), vu0::Mul(y.z, x.x)),
                 vu0::Sub(vu0::Mul(x.x, y.y), vu0::Mul(y.x, x.y)), 0.0f});
}

// PS2 (0x00262FA8): a.xyz . b.xyz (vmul.xyz, vaddy.x, vaddz.x).
f32 sceVu0InnerProduct(u32 a, u32 b) {
    const vu0::Vec4 x = LoadV(a), y = LoadV(b);
    return vu0::Add(vu0::Add(vu0::Mul(x.x, y.x), vu0::Mul(x.y, y.y)), vu0::Mul(x.z, y.z));
}
