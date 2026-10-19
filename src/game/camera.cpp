// Camera library (core module 0x001026B8-0x001031C0; docs/subsystems/camera-hud.md "Camera object"):
// per-frame matrices, the look-at helpers and the clip matrix. Init, projection and viewport are in
// render2d.cpp.
//
// Camera object: +0 position, +0x10 rotation (pitch, yaw, roll), +0x20 world->view, +0x60
// view->screen, +0xA0 clip (view->clip), +0xE0 world->screen, +0x120 world->clip, +0x160/+0x164
// screen width/height, +0x168 distance, +0x16C near, +0x170 far, +0x178/+0x17C screen centre,
// +0x180/+0x184 x/y scale, +0x188/+0x18A viewport x/y, +0x18C/+0x18E viewport w/h, +0x190/+0x194 z
// range, +0x198 flags (bit 0 viewport scale pending), +0x1A0/+0x1B0 SCISSOR values.
// Look-at record: +0 eye, +0x10 target, +0x20 distance, +0x24 roll, +0x28/+0x2C screen offsets.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00102870
// Dead (no reference): screen centre (+0x178 x, +0x17C y).
void SetCameraScreenCentre(u32 cam, f32 x, f32 y) {
    GRef<f32>(cam + 0x17C) = y;
    GRef<f32>(cam + 0x178) = x;
}

// EE: 0x00102880
// Dead (no reference): x/y scale (+0x180/+0x184), clears the viewport-scale-pending flag and sets
// the screen size to viewport size / scale.
void SetCameraScale(u32 cam, f32 sx, f32 sy) {
    const f32 w = static_cast<f32>(GRef<s16>(cam + 0x18C));
    const f32 h = static_cast<f32>(GRef<s16>(cam + 0x18E));
    GRef<f32>(cam + 0x180) = sx;
    GRef<f32>(cam + 0x184) = sy;
    GRef<u32>(cam + 0x198) &= 0xFFFFFFFEu;
    // div.s: a zero scale gives +-FLT_MAX on the EE.
    GRef<f32>(cam + 0x160) = fpu::Div(w, sx);
    GRef<f32>(cam + 0x164) = fpu::Div(h, sy);
}

// EE: 0x00102798
// Dead (no reference): diagonal field of view: 2 * atan2(|half screen size|, distance +0x168).
f32 GetCameraDiagonalFov(u32 cam) {
    const f32 hw = GRef<f32>(cam + 0x160) * 0.5f;
    const f32 hh = GRef<f32>(cam + 0x164) * 0.5f;
    // sqrt.s (the NaN fallback to sqrtf never triggers on the EE).
    const f32 r = fpu::Sqrt(fpu::Add(hw * hw, hh * hh));
    const f32 a = ee::atan2f(r, GRef<f32>(cam + 0x168));
    return fpu::Add(a, a);
}

// EE: 0x001029E0
// Per frame: world->view = inverse(translate(position) * rotation(+0x10)); the viewport scale when
// flagged (viewport size / screen size); view->screen (sceVu0ViewScreenMatrix with far + k), the
// clip matrix, world->screen (+0xE0) and world->clip (+0x120); SCISSOR from the viewport.
void UpdateCameraMatrices(u32 cam) {
    const u32 view = cam + 0x20;
    sceVu0UnitMatrix(view);
    func_00102D28(view, view, cam + 0x10);
    sceVu0TransMatrix(view, view, cam);
    sceVu0InversMatrix(view, view);
    if ((GRef<u32>(cam + 0x198) & 1) != 0) {
        const f32 sx = static_cast<f32>(GRef<s16>(cam + 0x18C)) / GRef<f32>(cam + 0x160);
        const f32 sy = static_cast<f32>(GRef<s16>(cam + 0x18E)) / GRef<f32>(cam + 0x164);
        GRef<f32>(cam + 0x180) = sx;
        GRef<f32>(cam + 0x184) = sy;
    }
    const u32 screen = cam + 0x60;
    const u32 clip = cam + 0xA0;
    sceVu0UnitMatrix(screen);
    const f32 farz = fpu::Add(GRef<f32>(cam + 0x170), GRef<f32>(0x0040DA10));
    sceVu0ViewScreenMatrix(screen, GRef<f32>(cam + 0x168), GRef<f32>(cam + 0x180), GRef<f32>(cam + 0x184),
                           GRef<f32>(cam + 0x178), GRef<f32>(cam + 0x17C), GRef<f32>(cam + 0x190),
                           GRef<f32>(cam + 0x194), GRef<f32>(cam + 0x16C), farz);
    BuildClipMatrix(clip, GRef<f32>(cam + 0x160), GRef<f32>(cam + 0x164), GRef<f32>(cam + 0x168),
                    GRef<f32>(cam + 0x16C), GRef<f32>(cam + 0x170));
    sceVu0MulMatrix(cam + 0xE0, screen, view);
    sceVu0MulMatrix(cam + 0x120, clip, view);
    const s64 x = GRef<s16>(cam + 0x188);
    const s64 y = GRef<s16>(cam + 0x18A);
    const s64 x1 = static_cast<s32>(x + GRef<s16>(cam + 0x18C) - 1);
    const s64 y1 = static_cast<s32>(y + GRef<s16>(cam + 0x18E) - 1);
    const u64 scissor = static_cast<u64>(x) | static_cast<u64>(y1) << 48 | static_cast<u64>(y) << 32 |
                        static_cast<u64>(x1) << 16;
    GRef<u64>(cam + 0x1A0) = scissor;
    GRef<u64>(cam + 0x1B0) = scissor;
}

// EE: 0x00102B68
// Resets a look-at record: eye and target (0, 0, 0, 1), distance 512, roll and offsets 0.
void ResetCameraLookAt(u32 look) {
    for (u32 i = 0; i < 2; ++i) {
        GRef<f32>(look + 0x10 * i) = 0.0f;
        GRef<f32>(look + 0x10 * i + 4) = 0.0f;
        GRef<f32>(look + 0x10 * i + 8) = 0.0f;
        GRef<f32>(look + 0x10 * i + 12) = 1.0f;
    }
    GRef<u32>(look + 0x2C) = 0;
    GRef<f32>(look + 0x20) = 512.0f;
    GRef<u32>(look + 0x24) = 0;
    GRef<u32>(look + 0x28) = 0;
}

// EE: 0x00102B90
// Pitch and yaw of direction `dir`: out[0] = -atan2(y, sqrt(x^2 + z^2)) (the root on VU0, read back
// from Q), out[1] = atan2(x, z).
void GetDirectionAngles(u32 out, u32 dir) {
    const f32 x = GRef<f32>(dir), y = GRef<f32>(dir + 4), z = GRef<f32>(dir + 8);
    const f32 r = vu0::Sqrt(vu0::Add(vu0::Mul(x, x), vu0::Mul(z, z)));
    GRef<f32>(out) = -ee::atan2f(y, r);
    GRef<f32>(out + 4) = ee::atan2f(x, z);
}

// EE: 0x00102C08
// Points camera `cam` from the look-at record: position = eye, pitch/yaw from target - eye, roll and
// distance copied. With screen offsets, the position moves by (-ox, -oy) * |target - eye| /
// distance in the camera's rotated frame.
void SetCameraLookAt(u32 cam, u32 look) {
    guest::GuestFrame frame(0x50);
    const u32 dir = frame.Address();
    const u32 m = dir + 0x10;
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(cam + i) = GRef<u32>(look + i);
    GRef<f32>(dir) = vu0::Sub(GRef<f32>(look + 0x10), GRef<f32>(look));
    GRef<f32>(dir + 4) = vu0::Sub(GRef<f32>(look + 0x14), GRef<f32>(look + 4));
    GRef<f32>(dir + 8) = vu0::Sub(GRef<f32>(look + 0x18), GRef<f32>(look + 8));
    GRef<u32>(dir + 12) = GRef<u32>(look + 0x1C);
    GetDirectionAngles(cam + 0x10, dir);
    GRef<u32>(cam + 0x18) = GRef<u32>(look + 0x24);
    GRef<u32>(cam + 0x168) = GRef<u32>(look + 0x20);
    if (GRef<f32>(look + 0x28) == 0.0f && GRef<f32>(look + 0x2C) == 0.0f) return;
    const f32 scale = GetVectorLength(dir) / GRef<f32>(look + 0x20);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, cam + 0x10);
    sceVu0TransMatrix(m, m, cam);
    GRef<f32>(dir) = 0.0f;
    GRef<f32>(dir + 4) = 0.0f;
    GRef<f32>(dir + 8) = 0.0f;
    GRef<f32>(dir + 12) = 1.0f;
    GRef<f32>(dir + 4) = -GRef<f32>(look + 0x2C) * scale;
    GRef<f32>(dir) = -GRef<f32>(look + 0x28) * scale;
    sceVu0ApplyMatrix(cam, m, dir);
}

// EE: 0x00103150
// Dead (no reference): world->view matrix `out` = inverse(translate(trans) * rotation(rot)).
void func_00103150(u32 out, u32 trans, u32 rot) {
    guest::GuestFrame frame(0x40);
    const u32 m = frame.Address();
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    sceVu0TransMatrix(m, m, trans);
    sceVu0InversMatrix(out, m);
}

// EE: 0x001031C0
// Clip matrix: unit with m00 = 2d / w, m11 = 2d / h, m22 = (far + near) / (far - near), m23 = 1,
// m32 = -2 far near / (far - near), m33 = 0.
void BuildClipMatrix(u32 m, f32 w, f32 h, f32 d, f32 near_z, f32 far_z) {
    sceVu0UnitMatrix(m);
    const f32 range = fpu::Sub(far_z, near_z);
    const f32 d2 = fpu::Add(d, d);
    f32 zw = far_z * -2.0f;
    GRef<u32>(m + 0x3C) = 0;
    const f32 zz = fpu::Add(far_z, near_z);
    GRef<f32>(m + 0x2C) = 1.0f;
    const f32 sy = d2 / h;
    zw = zw * near_z;
    const f32 sx = d2 / w;
    GRef<f32>(m + 0x14) = sy;
    GRef<f32>(m) = sx;
    GRef<f32>(m + 0x28) = zz / range;
    GRef<f32>(m + 0x38) = zw / range;
}

// EE: 0x00104F80
// Length of the vector at `v` (mula/madda/madd in the order x, y, z, then sqrt).
f32 GetVectorLength(u32 v) {
    const f32 x = GRef<f32>(v), y = GRef<f32>(v + 4), z = GRef<f32>(v + 8);
    return fpu::Sqrt(fpu::MulAdd(fpu::MulAdd(fpu::Mul(x, x), y, y), z, z));
}

}  // namespace ac04::game
