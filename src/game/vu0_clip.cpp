// VU0 visibility helpers of the world renderer (docs/subsystems/rendering.md "VU0 visibility
// helpers"): func_001367B0 loads a clip matrix into VU0 vf8-vf11 and the clip tests that follow use
// it. The registers are kept here as a host copy (vf8-vf11). Every caller loads the matrix right
// before its tests and calls no other VU0 code that writes vf8-vf11 in between (checked for
// 0x0013A6B8, SelectUnitModelLods, 0x0014AA78 and 0x0014ACB0 when they were translated).
#include "game/game.h"

#include <cmath>

#include "common/vu0_math.h"

namespace ac04::game {

using guest::GRef;

namespace {

vu0::Vec4 g_ClipMatrix[4];  // VU0 vf8-vf11

vu0::Vec4 Load(u32 p) { return {GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; }

void Store(u32 p, const vu0::Vec4& v) {
    GRef<f32>(p) = v.x;
    GRef<f32>(p + 4) = v.y;
    GRef<f32>(p + 8) = v.z;
    GRef<f32>(p + 12) = v.w;
}

vu0::Vec4 Transform(const vu0::Vec4& v) {
    return vu0::MulMatrix(g_ClipMatrix[0], g_ClipMatrix[1], g_ClipMatrix[2], g_ClipMatrix[3], v);
}

// vclipw.xyz: bits 0-5 = x > |w|, x < -|w|, y > |w|, y < -|w|, z > |w|, z < -|w|.
u32 ClipBits(const vu0::Vec4& v) {
    const f32 w = std::fabs(v.w);
    u32 b = 0;
    if (v.x > w) b |= 0x01;
    if (v.x < -w) b |= 0x02;
    if (v.y > w) b |= 0x04;
    if (v.y < -w) b |= 0x08;
    if (v.z > w) b |= 0x10;
    if (v.z < -w) b |= 0x20;
    return b;
}

}  // namespace

// EE: 0x001367B0
// Loads the 4 x 4 clip matrix m (column-major) into VU0 vf8-vf11.
void func_001367B0(u32 m) {
    for (u32 i = 0; i < 4; ++i) g_ClipMatrix[i] = Load(m + 0x10 * i);
}

// EE: 0x001367C8
// v = M * p (written to out); returns 1 when any clip bit is set (outside the frustum), else 0.
s32 func_001367C8(u32 p, u32 out) {
    const vu0::Vec4 v = Transform(Load(p));
    const u32 bits = ClipBits(v);
    Store(out, v);
    return bits != 0 ? 1 : 0;
}

// EE: 0x00136818
// Two points (a box's corners): bit 0 = b outside, bit 1 = a outside; out = the transformed corner
// with the smaller w (the MAC flag of `vsub.w vf0, va, vb`: Zw or Sw -> va).
s32 func_00136818(u32 a, u32 b, u32 out) {
    const vu0::Vec4 va = Transform(Load(a));
    const vu0::Vec4 vb = Transform(Load(b));
    const u32 ca = ClipBits(va), cb = ClipBits(vb);
    s32 r = 0;
    if (cb != 0) r |= 1;
    if (ca != 0) r |= 2;
    const f32 dw = vu0::Sub(va.w, vb.w);
    Store(out, dw <= 0.0f ? va : vb);
    return r;
}

}  // namespace ac04::game
