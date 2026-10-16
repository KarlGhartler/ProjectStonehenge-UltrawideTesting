// Hangar (scene 4 subs 27-36; docs/port/s4-inventory.md §1): aircraft and special weapon selection,
// purchase and sale, drawn over the aircraft viewer (aircraft_viewer.cpp).
//
// Light parameters (0x40 bytes, used by the viewer): +0 three light colours (RGBA words, 0 = off),
// +0xC ambient colour, +0x10 three light directions (16 bytes each).
#include "game/game.h"

#include <cstring>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_HangarState, 0x00411048)    // gp-0x49A8
AC04_GUEST_VAR(u32, g_AircraftViewer, 0x00410EDC)  // gp-0x4B14
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)       // gp-0x4B68

constexpr u32 kSpr = 0x70000000;  // scratchpad work area

f32 K(u32 address) { return GRef<f32>(address); }

void StoreVf0(u32 a) {  // sqc2 vf0: (0, 0, 0, 1)
    GRef<u32>(a) = 0;
    GRef<u32>(a + 4) = 0;
    GRef<u32>(a + 8) = 0;
    GRef<u32>(a + 12) = 0x3F800000;
}

void CopyQword(u32 dst, u32 src) {
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(dst + i) = GRef<u32>(src + i);
}

f32 BitsToFloat(u32 b) {
    f32 f;
    std::memcpy(&f, &b, 4);
    return f;
}

// cvt.s.w of the difference of two pad bytes.
f32 PadAxis(u32 pad, u32 plus, u32 minus) {
    return fpu::IntToFloat(static_cast<s32>(GRef<u8>(pad + plus)) - static_cast<s32>(GRef<u8>(pad + minus)));
}

}  // namespace

// EE: 0x00231000
// Menu object `obj` in `mode` from a model file (container): item 0 is the model, item `item` (1 when
// 0) the texture; texture at TBP 0x2D00, CLUT at 0x3800. Items above 3 are ignored.
void CreateMenuObject(u32 obj, u32 file, u32 item, s32 mode) {
    if (item >= 4) return;
    guest::GuestFrame frame(0x10);
    const u32 reader = frame.Address();
    InitContainerReader(reader, file);
    u32 model, tex;
    if (item == 0) {
        model = GetContainerItem(reader, 0);
        tex = GetContainerItem(reader, 1);
    } else {
        model = GetContainerItem(reader, 0);
        tex = GetContainerItem(reader, static_cast<s32>(item));
    }
    InitMenuObject(obj, mode);
    BindMenuObject(obj, model, tex, 0, 0);
    SetMenuObjectTexturePos(obj, 0x2D00, 0x3800);
}

// EE: 0x002310C8
// Light parameters: ambient colour, three lights off with direction (0, 0, 0, 1).
void InitLights(u32 lights, u32 ambient) {
    GRef<u32>(lights + 0xC) = ambient;
    for (u32 i = 0; i < 3; ++i) {
        GRef<u32>(lights + 4 * i) = 0;
        StoreVf0(lights + 0x10 + 16 * i);
    }
}

// EE: 0x00231100
// Light `index` (0-2): colour and direction (0, 0, -1, 1) rotated about X by `rx`, then Y by `ry`.
void SetLightDirection(f32 rx, f32 ry, u32 lights, u32 index, u32 rgba) {
    if (index >= 3) return;
    guest::GuestFrame frame(0x50);
    const u32 m = frame.Address(), v = m + 0x40;
    GRef<u32>(lights + index * 4) = rgba;
    StoreVf0(v);
    GRef<f32>(v + 8) = -1.0f;
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, rx);
    sceVu0RotMatrixY(m, m, ry);
    sceVu0ApplyMatrix(lights + index * 16 + 0x10, m, v);
}

// EE: 0x002311B8
// Light set `ls` from the light parameters: ambient, and each light that is on.
void ApplyLights(u32 ls, u32 lights) {
    SetAmbient(ls, GRef<u32>(lights + 0xC));
    for (s32 i = 0; i < 3; ++i) {
        const u32 rgba = GRef<u32>(lights + static_cast<u32>(i) * 4);
        if (rgba != 0) SetLight(ls, lights + 0x10 + static_cast<u32>(i) * 16, rgba, i);
    }
}

// EE: 0x00231238
// As ApplyLights with the directions transformed by matrix `m`.
void ApplyLightsTransformed(u32 ls, u32 lights, u32 m) {
    SetAmbient(ls, GRef<u32>(lights + 0xC));
    guest::GuestFrame frame(0x10);
    const u32 dir = frame.Address();
    for (s32 i = 0; i < 3; ++i) {
        const u32 rgba = GRef<u32>(lights + static_cast<u32>(i) * 4);
        if (rgba != 0) {
            sceVu0ApplyMatrix(dir, m, lights + 0x10 + static_cast<u32>(i) * 16);
            SetLight(ls, dir, rgba, i);
        }
    }
}

// EE: 0x002312E0
// Light matrix `m` for an object at `pos` (scaled x10) seen from the camera position `cam_pos`:
// direction ((cx - px) * 3.5, cz - pz, -cy - py + 3200), its angles, and m = RotZ(-ay) RotX(-ax).
void BuildShadowMatrix(u32 m, u32 cam_pos, u32 pos) {
    guest::GuestFrame frame(0x20);
    const u32 dir = frame.Address(), p = dir + 0x10;
    sceVu0ScaleVector(p, pos, 10.0f);
    const f32 dx = fpu::Sub(GRef<f32>(cam_pos), GRef<f32>(p));
    const f32 dy = fpu::Sub(-GRef<f32>(cam_pos + 4), GRef<f32>(p + 4));
    const f32 dz = fpu::Sub(GRef<f32>(cam_pos + 8), GRef<f32>(p + 8));
    GRef<f32>(dir + 4) = dz;
    GRef<f32>(dir) = fpu::Mul(dx, 3.5f);
    GRef<f32>(dir + 8) = fpu::Add(dy, 3200.0f);
    GetDirectionAngles(p, dir);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixZ(m, m, -GRef<f32>(p + 4));
    sceVu0RotMatrixX(m, m, -GRef<f32>(p));
}

// EE: 0x002313B8
// Rotation matrix `rotation` (VU0 angles `rot`) and the full matrix `full` with translation `trans`.
void BuildModelMatrices(u32 rotation, u32 full, u32 rot, u32 trans) {
    SetUnitMatrixVu0(rotation);
    RotateMatrix(rotation, rotation, rot);
    sceVu0TransMatrix(full, rotation, trans);
}

// EE: 0x00231420
// Free camera (presets 7/8): the player's analog axes as a cubic speed (VU0: v^3 x k), turned by the
// look-at's heading (a camera built at scratchpad +0x90) and added to the eye; the eye kept at a
// distance from the preset's target (table 0x0039DF70 by `preset` & 3: 200; for 0, 740 from the
// target at height 160); eye and target blended (0.8 / 0.9).
void UpdateFreeCamera(u32 look, u32 preset) {
    const u32 p3 = preset & 3;
    CopyQword(kSpr + 0x10, 0x0039DF70 + p3 * 16);
    CopyQword(kSpr, look);
    const u32 pad = kMainContextAddress + (GRef<u16>(g_HangarState() + 0xA0) != 0 ? 0x260u : 0x210u);
    GRef<f32>(kSpr + 0x30) = PadAxis(pad, 0x2C, 0x2B);
    GRef<f32>(kSpr + 0x34) = PadAxis(pad, 0x2F, 0x2E);
    GRef<f32>(kSpr + 0x38) = PadAxis(pad, 0x19, 0x1A);
    {
        const f32 k1 = BitsToFloat(0x37018305u), k2 = BitsToFloat(0x36818305u);
        f32 c[4];
        for (u32 i = 0; i < 4; ++i) {
            const f32 a = GRef<f32>(kSpr + 0x30 + 4 * i);
            c[i] = vu0::Mul(vu0::Mul(a, a), a);
        }
        c[0] = vu0::Mul(c[0], k1);
        c[2] = vu0::Mul(c[2], k1);
        c[1] = vu0::Mul(c[1], k2);
        for (u32 i = 0; i < 4; ++i) GRef<f32>(kSpr + 0x30 + 4 * i) = c[i];
    }
    StoreVf0(kSpr + 0x90);
    StoreVf0(kSpr + 0xA0);
    SetCameraLookAt(kSpr + 0x90, look);
    StoreVf0(kSpr + 0x40);
    GRef<f32>(kSpr + 0x40) = GRef<f32>(kSpr + 0x30);
    GRef<f32>(kSpr + 0x44) = GRef<f32>(kSpr + 0x34);
    GRef<f32>(kSpr + 0x48) = GRef<f32>(kSpr + 0x38);
    f32 dist, limit;
    if (p3 > 0) {
        dist = GetDistanceXZ(kSpr, kSpr + 0x10);
        limit = 200.0f;
    } else {
        CopyQword(kSpr + 0x30, kSpr + 0x10);
        GRef<f32>(kSpr + 0x38) = 160.0f;
        dist = GetDistanceXZ(kSpr, kSpr + 0x30);
        limit = 740.0f;
    }
    if (fpu::Sub(dist, GRef<f32>(kSpr + 0x48)) < limit) GRef<f32>(kSpr + 0x48) = fpu::Sub(dist, limit);
    // Identity at +0x50 (vmr32 rotations of vf0), then the heading about Y.
    StoreVf0(kSpr + 0x80);
    for (u32 i = 0; i < 16; i += 4) {
        GRef<u32>(kSpr + 0x50 + i) = 0;
        GRef<u32>(kSpr + 0x60 + i) = 0;
        GRef<u32>(kSpr + 0x70 + i) = 0;
    }
    GRef<f32>(kSpr + 0x78) = 1.0f;
    GRef<f32>(kSpr + 0x64) = 1.0f;
    GRef<f32>(kSpr + 0x50) = 1.0f;
    sceVu0RotMatrixY(kSpr + 0x50, kSpr + 0x50, GRef<f32>(kSpr + 0xA4));
    sceVu0ApplyMatrix(kSpr + 0x40, kSpr + 0x50, kSpr + 0x40);
    for (u32 i = 0; i < 12; i += 4) GRef<f32>(kSpr + i) = vu0::Add(GRef<f32>(kSpr + i), GRef<f32>(kSpr + 0x40 + i));
    sceVu0InterVector(look, look, kSpr, K(0x004101E4));
    sceVu0InterVector(look + 0x10, look + 0x10, kSpr + 0x10, K(0x004101E8));
    GRef<f32>(look + 0xC) = 1.0f;
    GRef<f32>(look + 0x1C) = 1.0f;
}

// EE: 0x002316D0
// Free camera limits (preset 7): the eye clamped (twice) into the box 0x0039DFC0..0x0039DFD0.
void ClampFreeCamera(u32 look) {
    ClampVector(look, 0x0039DFC0, 0x0039DFD0);
    ClampVector(look, 0x0039DFC0, 0x0039DFD0);
}

// EE: 0x00231728
// Free camera limits (preset 8): the eye clamped (twice) into 0x0039DFE0..0x0039DFF0, then kept above
// the floor near the aircraft (down to -340 within 740 of 0x0039DFB0, easing from 592) and the
// weapon rack (down to -86 within 400 of 0x0039DF90).
void ClampFreeCameraSpecial(u32 look) {
    ClampVector(look, 0x0039DFE0, 0x0039DFF0);
    ClampVector(look, 0x0039DFE0, 0x0039DFF0);
    CopyQword(kSpr, look);
    CopyQword(kSpr + 0x20, 0x0039DFB0);
    f32 d = GetDistanceXZ(kSpr + 0x20, kSpr);
    if (d < 740.0f) {
        d = fpu::Sub(d, 592.0f);
        f32 t;
        if (0.0f < d) {
            t = fpu::Sub(1.0f, fpu::Mul(d, K(0x004101EC)));
        } else {
            t = 1.0f;
        }
        t = fpu::Mul(t, -340.0f);
        if (t < GRef<f32>(kSpr + 4)) GRef<f32>(kSpr + 4) = t;
    }
    CopyQword(kSpr + 0x20, 0x0039DF90);
    d = GetDistanceXZ(kSpr + 0x20, kSpr);
    if (d < 400.0f) {
        d = fpu::Sub(d, K(0x004101F0));
        f32 t;
        if (0.0f < d) {
            t = fpu::Sub(1.0f, fpu::Mul(d, K(0x004101F4)));
        } else {
            t = 1.0f;
        }
        t = fpu::Mul(t, -86.0f);
        if (t < GRef<f32>(kSpr + 4)) GRef<f32>(kSpr + 4) = t;
    }
    CopyQword(look, kSpr);
    GRef<f32>(look + 0xC) = 1.0f;
}

// EE: 0x002318F8
// Viewer camera for `preset` at time `t` (jump table 0x004071B0): 0 slow orbit along the side (back
// and forth over 12000; distance 2000 while t < 0); 3 orbit around the aircraft by colour; 4-6
// around special weapon slot preset - 4; 7/8 free camera with its limits (`jump` passed as the free
// camera preset); others the origin. With `jump` the look-at is set at once, otherwise blended
// (0.9) and the distance approached.
void UpdateViewerCamera(f32 t, u32 look, u32 preset, u32 jump) {
    guest::GuestFrame frame(0x20);
    const u32 eye = frame.Address(), target = eye + 0x10;
    StoreVf0(eye);
    StoreVf0(target);
    GRef<u32>(look + 0x28) = 0;
    GRef<u32>(look + 0x2C) = 0;
    f32 dist = 920.0f;
    GRef<u32>(look + 0x24) = 0;
    switch (preset) {
        case 0: {
            const f32 period = K(0x004101FC);
            f32 x = ee::fmodf(fpu::Mul(t, K(0x004101F8)), period);
            if (K(0x00410200) < x) x = fpu::Sub(period, x);
            const f32 z = fpu::Add(x, K(0x00410204));
            GRef<f32>(eye) = K(0x00410208);
            GRef<f32>(eye + 4) = K(0x0041020C);
            GRef<f32>(eye + 8) = z;
            GRef<f32>(target) = -200.0f;
            GRef<f32>(target + 4) = -700.0f;
            GRef<f32>(target + 8) = 270.0f;
            dist = 920.0f;
            if (t < 0.0f) dist = 2000.0f;
            break;
        }
        case 3: {
            const s32 colour = GRef<s32>(g_AircraftViewer() + 0x988);
            f32 ex = 680.0f, ez = 1000.0f, ey = -64.0f;
            f32 a = fpu::Add(fpu::Mul(t, K(0x00410210)), 0.25f);
            if (colour == 1) {
                ey = -700.0f;
                a = fpu::Add(a, K(0x00410218));
                a = fpu::Mul(fpu::Sub(a, ee::floorf(a)), K(0x0041021C));
                ex = fpu::Add(fpu::Mul(ee::cosf(a), 680.0f), 0.0f);
                ez = fpu::Add(fpu::Mul(ee::sinf(a), 1000.0f), 240.0f);
                const f32 px = fpu::Mul(ex, K(0x00410220));
                const f32 pz = fpu::Mul(ez, K(0x00410224));
                ez = fpu::Mul(ez, K(0x00410228));
                ex = fpu::Add(px, pz);
            } else if (colour == 0 || colour == 2) {
                if (colour == 0) {
                    a = fpu::Add(a, K(0x00410214));
                } else {
                    ey = -400.0f;
                    a = fpu::Add(a, K(0x0041022C));
                }
                a = fpu::Mul(fpu::Sub(a, ee::floorf(a)), K(0x00410230));
                ex = fpu::Add(fpu::Mul(ee::cosf(a), 680.0f), 0.0f);
                ez = fpu::Add(fpu::Mul(ee::sinf(a), 1000.0f), 240.0f);
            }
            GRef<f32>(target + 4) = -160.0f;
            GRef<f32>(target + 8) = 240.0f;
            GRef<f32>(eye) = ex;
            GRef<f32>(eye + 4) = ey;
            GRef<f32>(eye + 8) = ez;
            dist = 920.0f;
            GRef<u32>(target) = 0;
            break;
        }
        case 4:
        case 5:
        case 6: {
            const s32 slot = static_cast<s32>(preset) - 4;
            dist = 920.0f;
            f32 a = fpu::Add(fpu::Mul(t, K(0x00410234)), K(0x00410238));
            const f32 fl = ee::floorf(a);
            const f32 x0 = fpu::Add(fpu::Mul(fpu::IntToFloat(slot), -120.0f), 350.0f);
            GRef<f32>(target + 8) = 1200.0f;
            a = fpu::Mul(fpu::Sub(a, fl), K(0x0041023C));
            GRef<f32>(target + 4) = -10.0f;
            GRef<f32>(target) = x0;
            const f32 ex = fpu::Add(x0, fpu::Mul(ee::cosf(a), 200.0f));
            const f32 sn = ee::sinf(a);
            GRef<f32>(eye) = ex;
            GRef<f32>(eye + 4) = -120.0f;
            GRef<f32>(eye + 8) = fpu::Add(fpu::Mul(sn, 300.0f), 1200.0f);
            break;
        }
        case 7:
            UpdateFreeCamera(look, jump);
            ClampFreeCamera(look);
            return;
        case 8:
            UpdateFreeCamera(look, jump);
            ClampFreeCameraSpecial(look);
            return;
        default:
            break;
    }
    if (jump == 0) {
        sceVu0InterVector(look, look, eye, K(0x00410240));
        sceVu0InterVector(look + 0x10, look + 0x10, target, K(0x00410244));
        GRef<f32>(look + 0x20) = ApproachValue(GRef<f32>(look + 0x20), dist, K(0x00410248));
    } else {
        CopyQword(look, eye);
        CopyQword(look + 0x10, target);
        GRef<f32>(look + 0x20) = dist;
    }
}

namespace {

// The "finished" flags the panel texts return: 3 once the slide-in is past 4 units, 1 past 0.
u32 TextDoneFlags(f32 rest) {
    if (4.0f < rest) return 3;
    if (0.0f < rest) return 1;
    return 0;
}

}  // namespace

// EE: 0x00231DE0
// Special weapon icon boxes into bucket `index`: `count` boxes down the right side, highlighted by
// the bits of `mask`.
void DrawSpecialIcons(u32 list, s32 index, s32 count, u32 mask) {
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    GRef<u64>(p) = 0x1000000000008001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x10) = 0;
    p += 0x20;
    s32 y = 0xBE0;
    for (; count > 0; --count, y += 0x1A0, mask >>= 1) p = BuildIconBox(p, 0x280, static_cast<u64>(y), 0x970, 0x160, static_cast<s32>(mask & 1));
    const u32 end = CloseDmaPacket(start, p);
    LinkPacket(list, index, start, end);
}

// EE: 0x00231ED0
// Statistic gauges into bucket `index`: `count` gauges with the levels at `levels`.
void DrawHangarGauges(u32 list, s32 index, s32 count, u32 levels) {
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    GRef<u64>(p) = 0x1000000000008001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x10) = 0;
    p += 0x20;
    s32 y = 0xBC0;
    for (; count > 0; --count, y += 0x160, levels += 4) p = BuildGauge(GRef<f32>(levels), p, 0x280, y, 0xA00);
    const u32 end = CloseDmaPacket(start, p);
    LinkPacket(list, index, start, end);
}

// EE: 0x00231FB8
// Special weapon panel texts, sliding in by `head` and `body` (x 80): the heading, the aircraft's
// standard missile and gun counts, and the special weapon `slot` (code and count; with `none` the
// placeholder texts). Returns the finished flags.
u32 DrawHangarSpecialText(f32 head, f32 body, u32 aircraft, u32 slot, s32 none) {
    if (0.0f < head) DrawMenuText(fpu::Sub(fpu::Mul(head, 80.0f), 8.0f), 0x2100, 0xC60, 4, 0x00407208);
    const f32 x = fpu::Sub(fpu::Mul(body, 80.0f), 16.0f);
    if (!(0.0f < x)) return 0;
    if (aircraft >= 0x15) aircraft = 0;
    if (slot >= 3) slot = 0;
    const u32 loadout = GetAircraftLoadout(static_cast<s64>(aircraft + 1));
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    DrawMenuText(x, 0x2100, 0xD10, 3, 0x00410FB8);
    u64 args[1] = {static_cast<u64>(static_cast<s64>(GRef<s16>(loadout)))};
    ee::sprintf(buf, 0x00410FC0, args, 1);
    const f32 x2 = fpu::Sub(x, 6.0f);
    DrawMenuText(x2, 0x2100, 0xDE0, 3, buf);
    DrawMenuText(fpu::Sub(x, 4.0f), 0x2100, 0xEF0, 3, 0x00410FC8);
    args[0] = static_cast<u64>(static_cast<s64>(GRef<s16>(loadout + 2)));
    ee::sprintf(buf, 0x00410FD0, args, 1);
    DrawMenuText(fpu::Sub(x2, 4.0f), 0x2100, 0xFC0, 3, buf);
    if (none != 0) {
        ee::sprintf(buf, 0x00410FD8, nullptr, 0);
        DrawMenuText(fpu::Sub(x, 8.0f), 0x2100, 0x10D0, 3, buf);
        ee::sprintf(buf, 0x00410FE0, nullptr, 0);
        DrawMenuText(fpu::Sub(x2, 8.0f), 0x2100, 0x11A0, 3, buf);
    } else {
        args[0] = GetSpecialWeaponCode(static_cast<u32>(static_cast<s32>(GRef<s8>(loadout + slot * 4 + 4))));
        ee::sprintf(buf, 0x00410FE8, args, 1);
        DrawMenuText(fpu::Sub(x, 8.0f), 0x2100, 0x10D0, 3, buf);
        args[0] = static_cast<u64>(static_cast<s64>(GRef<s16>(loadout + slot * 4 + 6)));
        ee::sprintf(buf, 0x00410FF0, args, 1);
        DrawMenuText(fpu::Sub(x2, 8.0f), 0x2100, 0x11A0, 3, buf);
    }
    return TextDoneFlags(fpu::Sub(x, 18.0f));
}

// EE: 0x00232260
// Aircraft panel heading and the aircraft's name and description (table 0x0039DA80, 0x3C bytes per
// aircraft). Returns the finished flags.
u32 DrawHangarNameText(f32 head, f32 body, u32 aircraft) {
    if (0.0f < head) {
        const f32 x = fpu::Mul(head, 80.0f);
        DrawMenuText(x, 0x1C0, 0x590, 4, 0x00410FF8);
        DrawMenuText(fpu::Sub(x, 8.0f), 0x280, 0x590, 4, 0x00407218);
    }
    f32 x = fpu::Sub(fpu::Mul(body, 80.0f), 12.0f);
    if (!(0.0f < x)) return 0;
    if (aircraft >= 0x15) aircraft = 0;
    const u32 e = 0x0039DA80 + aircraft * 0x3C;
    DrawMenuText(x, 0x280, 0x660, 2, GRef<u32>(e));
    DrawMenuText(fpu::Sub(x, 12.0f), 0x280, 0x790, 3, GRef<u32>(e + 4));
    const f32 len = fpu::UnsignedToFloat(ee::strlen(GRef<u32>(e + 4)));
    x = fpu::Sub(x, fpu::Add(len, 12.0f));
    return TextDoneFlags(x);
}

// EE: 0x00232408
// Statistics panel labels (heading and six statistic names). Returns the finished flags.
u32 DrawHangarStatsText(f32 head) {
    if (!(0.0f < head)) return 0;
    const f32 x = fpu::Mul(head, 80.0f);
    DrawMenuText(x, 0x1C0, 0xA40, 4, 0x00410FF8);
    DrawMenuText(fpu::Sub(x, 8.0f), 0x280, 0xA40, 4, 0x00407228);
    DrawMenuText(fpu::Sub(x, 16.0f), 0x280, 0xAF0, 3, 0x00411000);
    DrawMenuText(fpu::Sub(x, 24.0f), 0x280, 0xC50, 3, 0x00407238);
    DrawMenuText(fpu::Sub(x, 32.0f), 0x280, 0xDB0, 3, 0x00407248);
    DrawMenuText(fpu::Sub(x, 40.0f), 0x280, 0xF10, 3, 0x00407258);
    DrawMenuText(fpu::Sub(x, 48.0f), 0x280, 0x1070, 3, 0x00407268);
    DrawMenuText(fpu::Sub(x, 56.0f), 0x280, 0x11D0, 3, 0x00411008);
    return TextDoneFlags(fpu::Sub(x, 63.0f));
}

// EE: 0x002325A8
// Special weapon heading and the selected weapon's code and name (with `none` the placeholder).
// Returns the finished flags.
u32 DrawHangarWeaponText(f32 head, f32 body, u32 aircraft, u32 slot, s32 none) {
    const f32 hx = fpu::Mul(head, 80.0f);
    if (0.0f < hx) {
        DrawMenuText(hx, 0x1C0, 0x590, 4, 0x00410FF8);
        DrawMenuText(fpu::Sub(hx, 8.0f), 0x280, 0x590, 4, 0x00407208);
    }
    f32 x = fpu::Sub(fpu::Mul(body, 80.0f), 10.0f);
    if (!(0.0f < x)) return 0;
    if (aircraft >= 0x15) aircraft = 0;
    if (slot >= 3) slot = 0;
    if (none != 0) {
        DrawMenuText(fpu::Sub(x, 0.0f), 0x280, 0x660, 2, 0x00411010);
        x = fpu::Sub(x, 6.0f);
    } else {
        const u32 loadout = GetAircraftLoadout(static_cast<s64>(aircraft + 1));
        const u32 id = static_cast<u32>(static_cast<s32>(GRef<s8>(loadout + slot * 4 + 4)));
        DrawMenuText(fpu::Sub(x, 0.0f), 0x280, 0x660, 2, GetSpecialWeaponCode(id));
        const u32 name = GetSpecialWeaponName(id);
        DrawMenuText(fpu::Sub(x, 12.0f), 0x280, 0x790, 3, name);
        const f32 len = fpu::UnsignedToFloat(ee::strlen(name));
        x = fpu::Sub(x, fpu::Add(len, 12.0f));
    }
    return TextDoneFlags(x);
}

// EE: 0x002327A0
// Colour panel: heading and five colour lines (names 0x0039E000 for the bits of `owned` unless
// `none`, else the placeholder 0x0039E014). Returns the finished flags.
u32 DrawHangarColourText(f32 head, f32 body, u32 owned, s32 none) {
    const u32 bits = none == 0 ? owned : 0;
    const f32 hx = fpu::Sub(fpu::Mul(head, 80.0f), 8.0f);
    if (0.0f < hx) {
        DrawMenuText(hx, 0x1C0, 0xB30, 4, 0x00410FF8);
        DrawMenuText(fpu::Sub(hx, 8.0f), 0x280, 0xB30, 4, 0x00407228);
    }
    const f32 x = fpu::Sub(fpu::Mul(body, 80.0f), 8.0f);
    if (!(0.0f < x)) return 0;
    for (s32 i = 0; i < 5; ++i) {
        const u32 str = ((static_cast<s32>(bits) >> i) & 1) != 0 ? GRef<u32>(0x0039E000 + static_cast<u32>(i) * 4)
                                                                   : GRef<u32>(0x0039E014);
        DrawMenuText(fpu::Sub(x, fpu::Mul(fpu::IntToFloat(i), 8.0f)), 0x300, 0xC50 + i * 0x1A0, 3, str);
    }
    return TextDoneFlags(fpu::Sub(x, 50.0f));
}

// EE: 0x00232930
// Ownership marks of the aircraft list (style 9, '/' + state) for the 21 aircraft of row 0, and
// rows 1/2 for `aircraft`. Returns the finished flags.
u32 DrawHangarOwnedMarks(f32 t, u32 aircraft, u32 rows) {
    if (!(0.0f < t)) return 0;
    u64 bits = GRef<u64>(rows);
    const u32 a = aircraft < 0x15 ? aircraft : 0;
    guest::GuestFrame frame(0x10);
    const u32 buf = frame.Address();
    GRef<u8>(buf + 1) = 0;
    const u32 shift = a * 2;
    f32 x = fpu::Mul(t, 80.0f);
    s32 sx = 0xB40;
    for (s32 i = 0x14; i >= 0; --i, sx += 0xE0) {
        const u32 s = static_cast<u32>(bits & 3);
        if (s != 0) {
            const f32 cur = x;
            GRef<u8>(buf) = static_cast<u8>(s + 0x2F);
            x = fpu::Sub(x, 1.0f);
            bits >>= 2;
            DrawMenuText(cur, sx, 0xC40, 9, buf);
        }
    }
    const s32 ax = static_cast<s32>(a) * 0xE0 + 0xB40;
    const u32 s1 = static_cast<u32>((GRef<u64>(rows + 8) >> shift) & 3);
    x = fpu::Sub(x, 1.0f);
    if (s1 != 0) {
        GRef<u8>(buf) = static_cast<u8>(s1 + 0x2F);
        DrawMenuText(x, ax, 0xB70, 9, buf);
    }
    const u32 s2 = static_cast<u32>((GRef<u64>(rows + 0x10) >> shift) & 3);
    if (s2 != 0) {
        GRef<u8>(buf) = static_cast<u8>(s2 + 0x2F);
        DrawMenuText(x, ax, 0xD10, 9, buf);
    }
    return TextDoneFlags(fpu::Sub(x, 1.0f));
}

// EE: 0x00232AD0
// Ownership marks of the special weapon slots (rows 3-5 of `rows`) for `aircraft`. Returns the
// finished flags.
u32 DrawHangarSpecialMarks(f32 t, u32 aircraft, s32 slot, u32 rows) {
    (void)slot;
    if (!(0.0f < t)) return 0;
    const u32 a = aircraft < 0x15 ? aircraft : 0;
    guest::GuestFrame frame(0x10);
    const u32 buf = frame.Address();
    GRef<u8>(buf + 1) = 0;
    f32 x = fpu::Mul(t, 80.0f);
    s32 sx = 0x1240;
    u32 row = rows + 0x18;
    for (s32 i = 2; i >= 0; --i, row += 8, sx += 0x140) {
        const u32 s = static_cast<u32>((GRef<u64>(row) >> (a * 2)) & 3);
        if (s != 0) {
            const f32 cur = x;
            GRef<u8>(buf) = static_cast<u8>(s + 0x2F);
            x = fpu::Sub(x, 1.0f);
            DrawMenuText(cur, sx, 0xC20, 8, buf);
        }
    }
    return TextDoneFlags(x);
}

// EE: 0x00232BE8
// Price panel: "BUY"/"SELL" (pages 3 and 6 buy) and the credits, then the price change (or the
// "no change" text) and the credits after it; red when `short_of_credits` once `body` is past k.
// Returns the finished flags.
u32 DrawHangarPriceText(f32 head, f32 body, s32 page, s32 credits, s32 change, s32 short_of_credits) {
    u32 color, shadow;
    if (short_of_credits != 0 && K(0x00410250) < body) {
        color = 0x1067;
        shadow = 0x40430;
    } else {
        color = 0xC1B199;
        shadow = 0x606060;
    }
    (void)shadow;  // passed in t1, which DrawMenuTextColored ignores
    const bool buy = page == 3 || page == 6;
    guest::GuestFrame frame(0x40);
    const u32 buf = frame.Address();
    u64 args[1];
    const f32 hx = fpu::Mul(head, 80.0f);
    if (0.0f < hx) {
        ee::sprintf(buf, buy ? 0x00411020u : 0x00411028u, nullptr, 0);
        DrawMenuTextColored(hx, 0x1E00, 0x610, 4, buf, color);
        args[0] = static_cast<u64>(static_cast<s64>(credits));
        ee::sprintf(buf, 0x00411030, args, 1);
        DrawMenuTextColored(fpu::Sub(hx, 8.0f), 0x1E80, 0x700, 6, buf, color);
    }
    f32 x = fpu::Sub(fpu::Mul(body, 80.0f), 16.0f);
    if (!(0.0f < x)) return 0;
    x = fpu::Sub(x, 8.0f);
    if (change != 0) {
        credits += change;
        if (change < 0) {
            GRef<u8>(buf) = '-';
            change = -change;
        } else {
            GRef<u8>(buf) = '+';
        }
        args[0] = static_cast<u64>(static_cast<s64>(change));
        ee::sprintf(buf | 1, 0x00411038, args, 1);
        DrawMenuTextColored(x, 0x1E80, 0x810, 6, buf, color);
    } else {
        DrawMenuTextColored(x, 0x2240, 0x810, 6, 0x00411040, color);
    }
    x = fpu::Sub(x, 8.0f);
    args[0] = static_cast<u64>(static_cast<s64>(credits));
    ee::sprintf(buf, 0x00411030, args, 1);
    DrawMenuTextColored(x, 0x1E80, 0x9A0, 6, buf, color);
    return TextDoneFlags(fpu::Sub(x, 9.0f));
}

// EE: 0x00232E70
// Price box outline (rect 0x0039E018) sliding in by `t`, red when `red`; filled once t >= k.
void DrawPriceBox(f32 t, u32 list, s32 index, s32 red) {
    const u32 color = red != 0 ? 0x1067u : 0xD1C1A9u;
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildMenuDrawEnv(start + 0x10, Ctx()->draw_w, Ctx()->draw_h);
    GRef<u64>(p + 0x10) = 0x8000000068ull;
    GRef<u64>(p) = 0x1000000000008001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x43;
    p = BuildFrameOutline(t, p + 0x20, 0x0039E018, color);
    if (K(0x00410254) <= t) p = BuildFilledBox(p, color, 0x7B108A00, 0x7B1091A0);
    const u32 end = CloseDmaPacket(start, p);
    LinkPacket(list, index, start, end);
}

// EE: 0x00232F80
// The panel text layer: each text whose bit is set in `parts` (1 name, 2 statistics, 4 weapon, 8
// colours, 0x10 special, 0x20 price, 0x100 aircraft marks, 0x200 special marks). Returns the panel
// texts' finished flags | the marks' flags << 2.
u32 DrawHangarTexts(u32 list, s32 index, u32 h, u32 parts) {
    u32 done = 3, marks = 3;
    s32 none = 0;
    BeginMenuTextLayer(list, index);
    const u16 page = GRef<u16>(h + 0xA2);
    if (page < 8 && page >= 5) none = (GetSpecialArrows(h) & 0x10) == 0 ? 1 : 0;
    const f32 head = GRef<f32>(h + 0x100), body = GRef<f32>(h + 0x104);
    if ((parts & 1) != 0) done = DrawHangarNameText(head, body, static_cast<u32>(static_cast<s32>(GRef<s16>(h + 0xEC)))) & 3;
    if ((parts & 2) != 0) done &= DrawHangarStatsText(GRef<f32>(h + 0x100));
    if ((parts & 4) != 0)
        done &= DrawHangarWeaponText(GRef<f32>(h + 0x100), GRef<f32>(h + 0x104), static_cast<u32>(static_cast<s32>(GRef<s16>(h + 0xEC))),
                                     static_cast<u32>(static_cast<s32>(GRef<s16>(h + 0xF0))), none);
    if ((parts & 8) != 0) done &= DrawHangarColourText(GRef<f32>(h + 0x100), GRef<f32>(h + 0x104), GRef<u32>(h + 0x150), none);
    if ((parts & 0x10) != 0)
        done &= DrawHangarSpecialText(GRef<f32>(h + 0x100), GRef<f32>(h + 0x104), static_cast<u32>(static_cast<s32>(GRef<s16>(h + 0xEC))),
                                      static_cast<u32>(static_cast<s32>(GRef<s16>(h + 0xF0))), none);
    if ((parts & 0x20) != 0) {
        const s32 change = GetPurchasePrice(h + 0x70, GRef<u16>(h + 0xA2), GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEC),
                                            GRef<s16>(h + 0xEE), GRef<s16>(h + 0xF0), GRef<s32>(h + 0x148), 0);
        const s32 credits = GRef<s32>(h + 0x148);
        done &= DrawHangarPriceText(GRef<f32>(h + 0x100), GRef<f32>(h + 0x104), GRef<u16>(h + 0xA2), credits, change,
                                    credits + change < 0 ? 1 : 0);
    }
    if ((parts & 0x100) != 0) marks = DrawHangarOwnedMarks(GRef<f32>(h + 0x108), GRef<u32>(h + 0xE8), h + 0x70) & 3;
    if ((parts & 0x200) != 0) marks &= DrawHangarSpecialMarks(GRef<f32>(h + 0x108), GRef<u32>(h + 0xE8), GRef<s16>(h + 0xF0), h + 0x70);
    EndMenuTextLayer();
    return done | marks << 2;
}

// EE: 0x00233138
// Hangar panels into bucket `index`: the special weapon's unlock byte (0x002A1F00 + id) into
// +0x150; frame outlines (with the price box when the page has a price), crosshair, gauges and
// special icons once shown, the texts (clearing the animation flags they finish), and the pulsing
// selection boxes (aircraft colour, special slot) once settled.
void DrawHangarPanels(u32 list, s32 index, u32 h) {
    const u32 panels = GRef<u16>(h + 0xF8);
    const u32 anim = GRef<u16>(h + 0xFA);
    const u32 loadout = GetAircraftLoadout(GRef<s16>(h + 0xEC) + 1);
    const s32 id = GRef<s8>(loadout + static_cast<u32>(GRef<s16>(h + 0xF0)) * 4 + 4);
    GRef<u32>(h + 0x150) = GRef<u8>(0x002A1F00 + static_cast<u32>(id));
    if ((panels & 0x20) != 0) {
        DrawAllFrameOutlines(list, index, GRef<s16>(h + 0xF2) - 1, GRef<u32>(h + 0x144), h + 0x124);
        s32 red = 0;
        if (K(0x00410258) < GRef<f32>(h + 0x104)) {
            const s32 change = GetPurchasePrice(h + 0x70, GRef<u16>(h + 0xA2), GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEC),
                                                GRef<s16>(h + 0xEE), GRef<s16>(h + 0xF0), GRef<s32>(h + 0x148), 0);
            red = GRef<s32>(h + 0x148) + change < 0 ? 1 : 0;
        }
        DrawPriceBox(GRef<f32>(h + 0x124 + static_cast<u32>(GRef<s16>(h + 0xF2) - 1) * 4), list, index, red);
    } else {
        DrawAllFrameOutlines(list, index, GRef<s16>(h + 0xF2), GRef<u32>(h + 0x144), h + 0x124);
    }
    if ((panels & 0x300) != 0) DrawCrosshair(list, index, h);
    if ((anim & 0x400) != 0) {
        if ((panels & 2) != 0) DrawHangarGauges(list, index, 6, h + 0x10C);
        if ((panels & 8) != 0) {
            u32 mask = 0;
            if (K(0x0041025C) < GRef<f32>(h + 0x104)) mask = GRef<u32>(h + 0x150);
            const u16 page = GRef<u16>(h + 0xA2);
            if (page < 8 && page >= 5 && (GetSpecialArrows(h) & 0x10) == 0) mask = 0;
            DrawSpecialIcons(list, index, 5, mask);
        }
    }
    const u32 r = DrawHangarTexts(list, index, h, panels);
    if ((anim & 0x2000) != 0 && (r & 5) == 5) GRef<u16>(h + 0xFA) &= ~0x2000;
    if ((anim & 0x20) != 0 && (r & 2) != 0) GRef<u16>(h + 0xFA) &= ~0x20;
    if ((anim & 0x80) != 0 && (r & 8) != 0) GRef<u16>(h + 0xFA) = static_cast<u16>((GRef<u16>(h + 0xFA) & ~0x80) | 0x100);
    if ((anim & 0x100) == 0) return;
    if (GRef<u16>(h) != 0) return;
    if ((panels & 0x100) != 0) {
        u32 c = static_cast<u32>(static_cast<s32>(GRef<s16>(h + 0xEE)));
        if (c >= 4) c = 0;
        DrawPulseBox(list, index, 0x1067, GRef<s32>(h + 0xE8) * 0xE0 + 0xB40, GRef<s32>(0x0039E028 + c * 4), 0xC0,
                     static_cast<s32>(Ctx()->frame_count));
    }
    if ((panels & 0x200) != 0)
        DrawPulseBox(list, index, 0x1067, GRef<s16>(h + 0xF0) * 0x140 + 0x1260, 0xC38, 0xC0, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00233450
// HYPOTHESIS (help line hint on the sell page): 0 when another colour of the aircraft is for sale
// (rows (colour + 1) % 3 and (colour + 2) % 3 in state 3), else 1 when special weapon row 4 or 5 is
// for sale, else 0.
s32 CheckHangarSaleHint(u32 h) {
    const u32 shift = static_cast<u32>(GRef<s32>(h + 0xE8)) * 2;
    const s32 colour = GRef<s16>(h + 0xEE);
    for (s32 i = 1; i < 3; ++i) {
        const u64 row = GRef<u64>(h + 0x70 + static_cast<u32>((colour + i) % 3) * 8);
        if (((row >> (shift & 63)) & 3) == 3) return 0;
    }
    for (u32 r = 4; r < 6; ++r) {
        if (((GRef<u64>(h + 0x70 + r * 8) >> (shift & 63)) & 3) == 3) return 1;
    }
    return 0;
}

// EE: 0x00233720
// Purchase state rows from save order to list order: field i of each row (2 bits) moves to the
// aircraft's list position (s16 at +8 of its 0x3C-byte entry in table 0x0039DA80).
void PurchaseStateToList(u32 dst, u32 src) {
    for (u32 r = 0; r < 6; ++r) {
        u64 v = GRef<u64>(src + r * 8);
        u64 out = 0;
        for (u32 e = 0x0039DA80; e < 0x0039DF6C; e += 0x3C) {
            const u64 field = v & 3;
            v >>= 2;
            out |= field << ((static_cast<u32>(GRef<s16>(e + 8)) * 2) & 63);
        }
        GRef<u64>(dst + r * 8) = out;
    }
}

// EE: 0x00233790
// Purchase state rows from list order back to save order (s16 positions at 0x0039DA50, 1-based).
void PurchaseStateToSave(u32 dst, u32 src) {
    for (u32 r = 0; r < 6; ++r) {
        u64 v = GRef<u64>(src + r * 8);
        u64 out = 0;
        u32 e = 0x0039DA50;
        for (s32 i = 0x14; i >= 0; --i, e += 2) {
            const u64 field = v & 3;
            v >>= 2;
            out |= field << ((static_cast<u32>(GRef<s16>(e) - 1) * 2) & 63);
        }
        GRef<u64>(dst + r * 8) = out;
    }
}

// EE: 0x00233808
// Page and leave targets by sub (jump table 0x004072E0): 0x1B campaign (aircraft page; accept goes to
// the briefing, mission id of g_MissionNo, game[0x25] = game[0x29] = 1); 0x1C-0x1F free selection
// (cancel to 0x1C/0x1D/0x1E); 0x20-0x23 the shop pages with their mode.
void SetHangarPageForSub(u32 h) {
    GRef<u16>(h + 0xAE) = 5;
    GRef<u16>(h + 0xAA) = 5;
    GRef<u16>(h + 0xA8) = 0;
    GRef<u16>(h + 0xAC) = 0;
    u16 target;
    switch (Ctx()->sub) {
        case 0x1B: {
            GRef<u16>(h + 0xA2) = 0;
            GRef<u16>(h + 0xA8) = 3;
            GRef<u32>(h + 0xE0) = 0;
            const s16 id = GetMissionId(0, GRef<u16>(0x002A15B8));
            const u32 game = GRef<u32>(0x004104A4);
            GRef<u8>(game + 0x27) = static_cast<u8>(id);
            GRef<u8>(game + 0x25) = 1;
            GRef<u8>(game + 0x29) = 1;
            return;
        }
        case 0x1C: target = 0x1C; break;
        case 0x1D: target = 0x1D; break;
        case 0x1E:
        case 0x1F: target = 0x1E; break;
        case 0x20:
            GRef<u16>(h + 0xA2) = 3;
            GRef<u32>(h + 0xE0) = 1;
            return;
        case 0x21:
            GRef<u16>(h + 0xA2) = 1;
            GRef<u32>(h + 0xE0) = 3;
            return;
        case 0x22:
            GRef<u32>(h + 0xE0) = 2;
            GRef<u16>(h + 0xA2) = 4;
            return;
        case 0x23:
            GRef<u32>(h + 0xE0) = 4;
            GRef<u16>(h + 0xA2) = 2;
            return;
        default:
            return;
    }
    GRef<u16>(h + 0xA2) = 0;
    GRef<u16>(h + 0xAE) = target;
    GRef<u32>(h + 0xE0) = 0;
    GRef<u16>(h + 0xAA) = target;
}

// EE: 0x00233908
// Bit mask of the 21 aircraft whose 2-bit field in `row` equals `state`.
u64 GetStateMask(u64 row, u64 state) {
    u64 mask = 0;
    for (u32 i = 0; i < 0x15; ++i, row >>= 2) {
        if (state == (row & 3)) mask |= 1ull << i;
    }
    return mask;
}

// EE: 0x00233950
// Availability masks (6 x u64) from the purchase rows by shop mode (jump table 0x00407310): 0
// select and 2 sell: state 3; 1 buy: state 2; 3 special buy: rows 0-2 state 3 (low 32 bits), rows
// 3-5 state 2; 4 special sell: rows 0-5 state 3 (low 32 bits), then rows 3-5 state 3 again.
void BuildAvailabilityMasks(u32 mode, u32 rows, u32 masks) {
    auto fill = [&](u32 first, u32 count, u64 state, bool low32) {
        for (u32 i = first; i < first + count; ++i) {
            u64 m = GetStateMask(GRef<u64>(rows + i * 8), state);
            if (low32) m &= 0xFFFFFFFFull;
            GRef<u64>(masks + i * 8) = m;
        }
    };
    switch (mode) {
        case 0: fill(0, 6, 3, false); break;
        case 1: fill(0, 6, 2, false); break;
        case 2: fill(0, 6, 3, false); break;
        case 3:
            fill(0, 3, 3, true);
            fill(3, 3, 2, false);
            break;
        case 4:
            fill(0, 6, 3, true);
            fill(3, 3, 3, false);
            break;
        default: break;
    }
}

// EE: 0x00233B48
// Writes the indices (below `n`) of the set bits of `bits` to the byte list `list` (when not null);
// returns how many.
s32 ListSetBits(u32 list, s32 n, u64 bits) {
    s32 count = 0;
    if (n <= 0 || bits == 0) return 0;
    for (s32 i = 0;;) {
        if ((bits & 1) != 0) {
            if (list != 0) GRef<u8>(list + static_cast<u32>(count)) = static_cast<u8>(i);
            ++count;
        }
        ++i;
        if (i >= n) break;
        bits >>= 1;
        if (bits == 0) break;
    }
    return count;
}

// EE: 0x00233BA0
// Moves the selection to an available aircraft (the first available at or after the current one,
// else the last), then an available colour of it and special slot 0. Returns the change flags (1
// aircraft, 2 colour, 4 special); none available sets +0x154.
u32 SelectFirstAvailable(u32 h) {
    guest::GuestFrame frame(0x20);
    const u32 list = frame.Address();
    const s32 n = ListSetBits(list, 0x15, GRef<u64>(h + 0xB0) | GRef<u64>(h + 0xB8) | GRef<u64>(h + 0xC0));
    if (n == 0) {
        GRef<u32>(h + 0x154) = 1;
        return 0;
    }
    u32 changes = 0;
    const s32 last = n - 1;
    const u32 cur = GRef<u32>(h + 0xE8);
    GRef<u32>(h + 0x154) = 0;
    u32 chosen;
    if (last > 0 && GRef<u8>(list) < (cur & 0xFF)) {
        s32 i = 1;
        while (i < last && GRef<u8>(list + static_cast<u32>(i)) < (cur & 0xFF)) ++i;
        chosen = GRef<u8>(list + static_cast<u32>(i));
    } else {
        chosen = GRef<u8>(list);
    }
    const u16 colour = GRef<u16>(h + 0xEE);
    if (chosen != cur) {
        GRef<u32>(h + 0xE8) = chosen;
        changes = 1;
    }
    const u64 bit = 1ull << chosen;
    if ((bit & GRef<u64>(h + 0xB0 + static_cast<u32>(static_cast<s16>(colour)) * 8)) == 0) {
        s32 c = 0;
        while (c < 3 && (bit & GRef<u64>(h + 0xB0 + static_cast<u32>(c) * 8)) == 0) ++c;
        if (c % 3 != static_cast<s16>(colour)) {
            GRef<s16>(h + 0xEE) = static_cast<s16>(c % 3);
            changes |= 2;
        }
    }
    if ((changes & 1) != 0) {
        const s32 index = GetAircraftAtListPosition(GRef<s32>(h + 0xE8)) - 1;
        GRef<s16>(h + 0xEC) = static_cast<s16>(index);
        GRef<s16>(h + 0xF6) = static_cast<s16>(GetSpecialWeaponCount(static_cast<s16>(index) + 1));
    }
    if (GRef<s16>(h + 0xF0) != 0) {
        GRef<s16>(h + 0xF0) = 0;
        changes |= 4;
    }
    return changes;
}

// EE: 0x00233D48
// Special slot to the first one available for the aircraft (masks +0xC8..). Returns 4 on a change.
u32 SelectAvailableSpecial(u32 h) {
    s32 slot = GRef<s16>(h + 0xF0);
    const u64 bit = 1ull << (GRef<u32>(h + 0xE8) & 63);
    if ((bit & GRef<u64>(h + 0xB0 + static_cast<u32>(slot + 3) * 8)) == 0) {
        s32 i = 0;
        while (i < 3 && (bit & GRef<u64>(h + 0xC8 + static_cast<u32>(i) * 8)) == 0) ++i;
        slot = i % 3;
    }
    const s16 old = GRef<s16>(h + 0xF0);
    GRef<s16>(h + 0xF0) = static_cast<s16>(slot);
    return slot != old ? 4 : 0;
}

// EE: 0x00233DF0
// Crosshair target by page: aircraft pages (0-4) the list row of `aircraft` and the colour column;
// special pages (5-7) the slot.
void MoveHangarCrosshair(u32 c, s32 page, s32 aircraft, s32 colour, s32 slot) {
    if (page < 0) return;
    if (page < 5) {
        const s32 y = colour == 1 ? 0xBD0 : (colour == 2 ? 0xD70 : 0xCA0);
        MoveCrosshairAuto(c, aircraft * 0xE0 + 0xB90, y);
    } else if (page < 8) {
        MoveCrosshairAuto(c, slot * 0x140 + 0x12C0, 0xC80);
    }
}

// EE: 0x00233E58
void ClearValues(s32 n, u32 values) {
    for (; n > 0; --n, values += 4) GRef<u32>(values) = 0;
}

// EE: 0x00233E88
// Each of `n` values approaches its target (ApproachValue, rate k).
void ApproachValues(s32 n, u32 values, u32 targets) {
    for (; n > 0; --n, values += 4, targets += 4) GRef<f32>(values) = ApproachValue(GRef<f32>(values), GRef<f32>(targets), K(0x0041026C));
}

// EE: 0x00233EF0
s32 GetSpecialWeaponCount(s32 aircraft) { return GRef<s32>(GetAircraftSpecialWeapons(aircraft)); }

// EE: 0x00233F10
// Writes the selection to the player's game slot (aircraft list index + 1, colour, special slot);
// for player 1 also to 0x002A15BE.. and validates the campaign selection. When the credits checksum
// matches, the credits and the purchase state (back in save order) go to the save data.
void CommitHangarSelection(u32 h) {
    debug::OnGuestCheckpoint(0x00233F10);
    const u32 player = GRef<u16>(h + 0xA0);
    const u32 slot = GRef<u32>(0x004104A4) + player * 16;
    GRef<u8>(slot) = static_cast<u8>(GRef<u8>(h + 0xEC) + 1);
    GRef<u8>(slot + 1) = GRef<u8>(h + 0xEE);
    GRef<u8>(slot + 3) = GRef<u8>(h + 0xF0);
    if (player == 0) {
        GRef<u16>(0x002A15BE) = static_cast<u16>(GRef<u16>(h + 0xEC) + 1);
        GRef<u16>(0x002A15C0) = GRef<u16>(h + 0xEE);
        GRef<u16>(0x002A15C2) = GRef<u16>(h + 0xF0);
        ValidateCampaignSelection();
    }
    const u32 credits = GRef<u32>(h + 0x148);
    if (credits == (GRef<u32>(h + 0x168) ^ 0x5963)) {
        GRef<u32>(0x002A0D30) = credits;
        PurchaseStateToSave(h + 0x40, h + 0x70);
        for (u32 i = 0; i < 0x30; i += 8) GRef<u64>(0x002A0320 + i) = GRef<u64>(h + 0x40 + i);
    }
}

// EE: 0x00234000
// Copies the panel entries (16 bytes each, table 0x0039E148) selected by `flags` << 2 | 3 (the first
// two always) to `dst`; returns how many.
s32 BuildPanelList(u32 flags, u32 dst) {
    u64 bits = static_cast<u64>(flags) << 2 | 3;
    u32 src = 0x0039E148;
    s32 count = 0;
    for (s32 i = 7; i >= 0; --i, src += 0x10, bits >>= 1) {
        if ((bits & 1) != 0) {
            for (u32 b = 0; b < 0x10; ++b) GRef<u8>(dst + b) = GRef<u8>(src + b);  // ldl/ldr, sdl/sdr
            ++count;
            dst += 0x10;
        }
    }
    return count;
}

// EE: 0x00234078
// Page set-up: crosshair reset, gauges, times and panel levels cleared; for pages below 8 the panel
// flags (table 0x0039E128), the panel list and the crosshair target; no purchase pending.
void OpenHangarPage(u32 h) {
    const u16 page = GRef<u16>(h + 0xA2);
    InitCrosshair(h);
    ClearValues(6, h + 0x10C);
    GRef<u32>(h + 0xFC) = 0;
    GRef<u32>(h + 0x100) = 0;
    GRef<u32>(h + 0x104) = 0;
    GRef<u32>(h + 0x108) = 0;
    for (u32 o = 0x140; o >= 0x124; o -= 4) GRef<u32>(h + o) = 0;
    GRef<u16>(h + 0xF2) = 0;
    GRef<u16>(h + 0xF8) = 0;
    GRef<u16>(h + 0xFA) = 0;
    if (page < 8) {
        GRef<u16>(h + 0xFA) = 0x12;
        const u16 flags = GRef<u16>(0x0039E128 + static_cast<u32>(page) * 2);
        GRef<u16>(h + 0xF8) = flags;
        const s32 n = BuildPanelList(flags, GRef<u32>(h + 0x144));
        GRef<u16>(h + 0xF2) = static_cast<u16>(n);
        MoveHangarCrosshair(h, GRef<u16>(h + 0xA2), GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEE), GRef<s16>(h + 0xF0));
    }
    GRef<u32>(h + 0x14C) = 0;
}

// EE: 0x00234150
// Phase 2: open the page; 4: start closing (times cleared, crosshair reset).
void SetHangarPhase(u32 h, s32 phase) {
    if (phase == 2) {
        OpenHangarPage(h);
    } else if (phase == 4) {
        GRef<u16>(h + 0xFA) = 4;
        GRef<u32>(h + 0xFC) = 0;
        GRef<u32>(h + 0x100) = 0;
        GRef<u32>(h + 0x104) = 0;
        GRef<u32>(h + 0x108) = 0;
        InitCrosshair(h);
    }
}

// EE: 0x00234190
// Panel animation. Opening (phase 2): with 0x10 the panel levels follow the time ((t - start) x 6);
// the last panel at 1 settles the slide (0x80), past 0.75 plays SE 0xD once (0x1000); the first text
// panel at 1 starts the texts (0x2220) and the gauge panel shows them (0x400); settled, the marks
// time runs; nothing left animating → phase 1. Closing (phase 4): levels 1 - t x 6; at 0 the next
// page opens, or for 10/11 the fades start and the viewer leaves (state 4/5). Texts slide in with
// 0x20; while they type (0x2000) SE 9 every other frame.
void AnimateHangarPanels(u32 h) {
    const u32 flags = GRef<u16>(h + 0xFA);
    const u32 phase = flags & 7;
    if (phase == 2) {
        s32 i = 0;
        if ((flags & 0x10) != 0) {
            const f32 t = fpu::Add(GRef<f32>(h + 0xFC), K(0x00410270));
            const s32 count = GRef<s16>(h + 0xF2);
            GRef<f32>(h + 0xFC) = t;
            if (count > 0) {
                const u32 list = GRef<u32>(h + 0x144);
                for (; i < count; ++i)
                    GRef<f32>(h + 0x124 + static_cast<u32>(i) * 4) =
                        fpu::Mul(fpu::Sub(t, GRef<f32>(list + static_cast<u32>(i) * 0x10 + 0xC)), 6.0f);
            }
            const u32 last = h + 0x124 + static_cast<u32>(i - 1) * 4;
            if ((flags & 0x80) == 0 && 1.0f <= GRef<f32>(last))
                GRef<u16>(h + 0xFA) = static_cast<u16>((GRef<u16>(h + 0xFA) & ~0x10) | 0x80);
            if ((flags & 0x1000) == 0 && 0.75f < GRef<f32>(last)) {
                PlayMenuSe(0, 0xD);
                GRef<u16>(h + 0xFA) |= 0x1000;
            }
            if ((flags & 0x20) == 0 && 1.0f <= GRef<f32>(h + 0x12C)) GRef<u16>(h + 0xFA) |= 0x2220;
            if ((flags & 0x400) == 0 && (GRef<u16>(h + 0xF8) & 0xA) != 0 && 1.0f <= GRef<f32>(h + 0x130))
                GRef<u16>(h + 0xFA) |= 0x400;
        }
        if ((flags & 0x80) != 0) GRef<f32>(h + 0x108) = fpu::Add(GRef<f32>(h + 0x108), K(0x00410274));
        if ((flags & 0xB0) == 0) GRef<u16>(h + 0xFA) = static_cast<u16>((GRef<u16>(h + 0xFA) & ~7) | 1);
    } else if (phase == 4) {
        const f32 t = fpu::Add(GRef<f32>(h + 0xFC), K(0x00410278));
        f32 level = fpu::Sub(1.0f, fpu::Mul(t, 6.0f));
        GRef<f32>(h + 0xFC) = t;
        if (level < 0.0f) {
            level = 0.0f;
            const u16 next = GRef<u16>(h + 0xA4);
            GRef<u16>(h + 0xFA) &= ~7;
            if (next == 10 || next == 11) {
                InitFade(g_pMenuFade(), 1);
                StartFade(2.0f, g_AircraftViewer() + 0x260, 1);
                GRef<u32>(g_AircraftViewer() + 0x97C) = next == 10 ? 4 : 5;
            } else {
                GRef<u16>(h + 0xA2) = next;
                SetHangarPhase(h, 2);
            }
        }
        const s32 count = GRef<s16>(h + 0xF2);
        for (s32 i = 0; i < count; ++i) GRef<f32>(h + 0x124 + static_cast<u32>(i) * 4) = level;
    }
    if ((flags & 0x20) != 0) {
        const f32 step = K(0x0041027C);
        const f32 a = fpu::Add(GRef<f32>(h + 0x100), step);
        const f32 b = fpu::Add(GRef<f32>(h + 0x104), step);
        GRef<f32>(h + 0x100) = a;
        GRef<f32>(h + 0x104) = b;
    }
    if ((flags & 0x2000) != 0 && (Ctx()->frame_count & 1) == 0 &&
        (GRef<f32>(h + 0x100) < 0.25f || K(0x00410280) < GRef<f32>(h + 0x104)))
        PlayMenuSe(0, 9);
}

// EE: 0x00234588
// Menu tab arrows for the aircraft pages: 0x30 (0x3C with more than one aircraft available), | 3
// when more than one colour of the aircraft is.
u32 GetAircraftArrows(u32 h) {
    const s32 n = ListSetBits(0, 0x15, GRef<u64>(h + 0xB0) | GRef<u64>(h + 0xB8) | GRef<u64>(h + 0xC0));
    const u32 base = n < 2 ? 0x30u : 0x3Cu;
    const u64 bit = 1ull << (GRef<u32>(h + 0xE8) & 63);
    s32 colours = 0;
    for (u32 r = 0; r < 3; ++r) {
        if ((GRef<u64>(h + 0xB0 + r * 8) & bit) != 0) ++colours;
    }
    return colours < 2 ? base : base | 3;
}

// EE: 0x00234628
// Arrows for the special weapon pages: 0x20 (0x30 when one is available), | 0xC when more than one
// special weapon of the aircraft is available.
u32 GetSpecialArrows(u32 h) {
    const u32 id = GRef<u32>(h + 0xE8) & 63;
    const u64 bits = ((GRef<u64>(h + 0xC8) >> id) & 1) | ((GRef<u64>(h + 0xD0) >> id) & 1) << 1 |
                     ((GRef<u64>(h + 0xD8) >> id) & 1) << 2;
    const s32 n = ListSetBits(0, 3, bits);
    const u32 base = n < 1 ? 0x20u : 0x30u;
    return n < 2 ? base : base | 0xC;
}

// EE: 0x002346B0
// After a selection change (flags from SelectFirstAvailable/input): a new aircraft restarts the
// texts, its special count, slot 0; a new colour restarts the texts on the buy/sell pages (3/4) and
// sets camera preset 4 (the next camera update then jumps back to the colour orbit).
void ApplyHangarChange(u32 h, u32 changes) {
    if ((changes & 1) != 0) {
        GRef<u32>(h + 0x104) = 0;
        GRef<u16>(h + 0xFA) |= 0x2020;
        GRef<s16>(h + 0xF6) = static_cast<s16>(GetSpecialWeaponCount(GRef<s16>(h + 0xEC) + 1));
        GRef<s16>(h + 0xF0) = 0;
    }
    if ((changes & 2) != 0) {
        if (static_cast<u32>(GRef<u16>(h + 0xA2) - 3) < 2) {
            GRef<u32>(h + 0x104) = 0;
            GRef<u16>(h + 0xFA) |= 0x2020;
        }
        GRef<s32>(h + 0x160) = 4;
    }
}

// EE: 0x002334F8
// Help line by page (jump table 0x004072B0) once the first panel is up: per aircraft (tables
// 0x0039E038 select, 0x0039E068 buy, 0x0039E098 sell), per special weapon id (0x0039E0C8,
// 0x0039E0E8, 0x0039E108), or fixed messages (nothing for sale, confirm, not sellable ...); a new
// message restarts the type-out. Otherwise the line is hidden (+0x960 = 0).
void UpdateHangarHelp(u32 v, u32 h) {
    const u16 page = GRef<u16>(h + 0xA2);
    if (!(static_cast<u32>(page - 8) < 2) && GRef<f32>(h + 0x124) < K(0x00410260)) {
        GRef<u32>(v + 0x960) = 0;
        return;
    }
    if (page >= 10) {
        GRef<u32>(v + 0x960) = 0;
        return;
    }
    auto special_msg = [&](u32 table) {
        const u32 loadout = GetAircraftLoadout(GRef<s16>(h + 0xEC) + 1);
        u32 id = static_cast<u32>(static_cast<s32>(GRef<s8>(loadout + static_cast<u32>(GRef<s16>(h + 0xF0)) * 4 + 4)));
        if (id >= 0xF) id = 0;
        return static_cast<s32>(GRef<s16>(table + id * 2));
    };
    auto aircraft_msg = [&](u32 table) { return static_cast<s32>(GRef<s16>(table + static_cast<u32>(GRef<s16>(h + 0xEC)) * 2)); };
    s32 msg;
    switch (page) {
        case 0: msg = aircraft_msg(0x0039E038); break;
        case 1: msg = 0xF0; break;
        case 2: msg = 0x11C; break;
        case 3:
            if (GRef<u32>(h + 0x154) != 0) msg = 0xAC;
            else if (GRef<u32>(h + 0x14C) != 0) msg = 0x6C;
            else msg = aircraft_msg(0x0039E068);
            break;
        case 4:
            if (GRef<u32>(h + 0x14C) != 0) {
                msg = CheckHangarSaleHint(h) != 0 ? 0xEE : 0xAD;
            } else if ((GRef<u64>(h + 0xE8) & 0xFFFFFFFF00000000ull) == 0x100000000ull) {
                msg = 0xEA;  // the default aircraft in colour 0 cannot be sold
            } else {
                msg = aircraft_msg(0x0039E098);
            }
            break;
        case 5: msg = special_msg(0x0039E0C8); break;
        case 6:
            if (GRef<u32>(h + 0x14C) != 0) msg = 0x6C;
            else if ((GetSpecialArrows(h) & 0x10) == 0) msg = 0x11B;
            else msg = special_msg(0x0039E0E8);
            break;
        case 7:
            if (GRef<u32>(h + 0x14C) != 0) msg = 0xAD;
            else if (GRef<s16>(h + 0xF0) == 0) msg = 0x11D;
            else msg = special_msg(0x0039E108);
            break;
        default: msg = 0x69; break;  // 8, 9
    }
    if (GRef<s16>(v + 0x966) != msg) {
        SetMessageBoxText(v + 0x958, msg);
        GRef<f32>(v + 0x960) = K(0x00410264);
    }
    UpdateMessageBox(K(0x00410268), v + 0x958, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00234BC8
// Number of 2-bit fields in state 3 in purchase rows 0-2. Not called in the ELF.
s32 CountOwnedFields(u32 rows) {
    s32 n = 0;
    for (u32 r = 0; r < 3; ++r) {
        const u64 v = GRef<u64>(rows + r * 8);
        for (u64 b = (v >> 1) & v & 0x5555555555555555ull; b != 0; b &= b - 1) ++n;
    }
    return n;
}

// EE: 0x00234C50
// Price of the change of page kind `kind` (table 0x0039E1C8: -1/3 buy aircraft colour, -2/6 buy
// special slot, -3/4 sell aircraft colour, -4/7 sell special slot) for the aircraft at list position
// `pos` (table entry `index`, prices at +0x24 + field x 4): buying returns -price when the field is
// for sale (2) and, with `apply`, buys it when the credits suffice (else 0); selling returns +price
// when owned (3) and, with `apply`, puts it back on sale. Selling the last colour of an aircraft also
// sells its special weapons in rows 4-5. The default aircraft in colour 0 cannot be sold.
s32 GetPurchasePrice(u32 rows, s32 kind, s32 pos, s32 index, s32 colour, s32 slot, s32 credits, s32 apply) {
    const u32 shift = static_cast<u32>(pos * 2) & 63;
    const u64 mask = 3ull << shift;
    const u32 entry = 0x0039DA80 + static_cast<u32>(index) * 0x3C;
    const s32 srow = slot + 3;
    auto state = [&](u64 v) { return (v & mask) >> shift; };
    auto set = [&](u32 row, u64 value) { GRef<u64>(row) = (GRef<u64>(row) & ~mask) | value << shift; };
    switch (kind) {
        case -1:
        case 3:
        case -2:
        case 6: {  // buy
            const s32 field = (kind == -1 || kind == 3) ? colour : srow;
            const u32 row = rows + static_cast<u32>(field) * 8;
            const s32 price = GRef<s32>(entry + static_cast<u32>(field) * 4 + 0x24);
            if (state(GRef<u64>(row)) != 2) return 0;
            if (apply == 0) return -price;
            if (credits < price) return 0;
            set(row, 3);
            return -price;
        }
        case -3:
        case 4: {  // sell a colour
            if (index == 1 && colour == 0) return 0;
            const u32 row = rows + static_cast<u32>(colour) * 8;
            s32 price = GRef<s32>(entry + static_cast<u32>(colour) * 4 + 0x24);
            if (state(GRef<u64>(row)) != 3) return 0;
            if (apply != 0) set(row, 2);
            if (((GRef<u64>(rows + static_cast<u32>((colour + 1) % 3) * 8) >> shift) & 3) >= 3) return price;
            if (((GRef<u64>(rows + static_cast<u32>((colour + 2) % 3) * 8) >> shift) & 3) >= 3) return price;
            u32 p = entry + 0x34;
            for (u32 r = 4; r < 6; ++r, p += 4) {
                const u32 srow_addr = rows + r * 8;
                if (state(GRef<u64>(srow_addr)) == 3) {
                    if (apply != 0) set(srow_addr, 2);
                    price += GRef<s32>(p);
                }
            }
            return price;
        }
        case -4:
        case 7: {  // sell a special weapon
            if (srow < 4) return 0;
            const u32 row = rows + static_cast<u32>(srow) * 8;
            const s32 price = GRef<s32>(entry + static_cast<u32>(srow) * 4 + 0x24);
            if (state(GRef<u64>(row)) != 3) return 0;
            if (apply != 0) set(row, 2);
            return price;
        }
        default:
            return 0;
    }
}

// EE: 0x00234740
// Aircraft pages: tab arrows; with the pad's repeat bits, left/right (8/4) step through the available
// aircraft and up/down (1/2) through its available colours; the held bits move the crosshair, and a
// held left/right delays the viewer (5 frames) and marks it for a change. SE 4 on a change.
void HangarAircraftInput(u32 h, u32 repeat, u32 held) {
    if (GRef<u32>(h + 0x14C) != 0) {
        GRef<u16>(g_pMenuFade() + 0x32) = 0x30;
        return;
    }
    const u32 arrows = GetAircraftArrows(h);
    u32 allowed = 0;
    if ((arrows & 3) != 0) allowed = 3;
    if ((arrows & 0xC) != 0) allowed |= 0xC;
    GRef<u16>(g_pMenuFade() + 0x32) = static_cast<u16>(arrows);
    const u32 steps = repeat & allowed;
    u32 change = 0;
    if ((steps & 0xC) != 0) {
        guest::GuestFrame frame(0x20);
        const u32 list = frame.Address();
        const s32 n = ListSetBits(list, 0x15, GRef<u64>(h + 0xB0) | GRef<u64>(h + 0xB8) | GRef<u64>(h + 0xC0));
        if (n >= 2) {
            const u32 cur = GRef<u32>(h + 0xE8);
            s32 i = 0;
            if (n - 1 > 0 && GRef<u8>(list) < (cur & 0xFF)) {
                i = 1;
                while (i < n - 1 && GRef<u8>(list + static_cast<u32>(i)) < (cur & 0xFF)) ++i;
            }
            if ((repeat & 8) != 0) ++i;
            if ((repeat & 4) != 0) --i;
            const u32 chosen = GRef<u8>(list + static_cast<u32>((i + n) % n));
            if (chosen != cur) {
                GRef<u32>(h + 0xE8) = chosen;
                const s32 index = GetAircraftAtListPosition(chosen) - 1;
                change = 1;
                GRef<s16>(h + 0xEC) = static_cast<s16>(index);
                GRef<s16>(h + 0xF6) = static_cast<s16>(GetSpecialWeaponCount(static_cast<s16>(index) + 1));
            }
        }
    }
    const u32 moves = held & allowed;
    if (change != 0 || (steps & 3) != 0) {
        guest::GuestFrame frame(0x10);
        const u32 list = frame.Address();
        const u16 colour_raw = GRef<u16>(h + 0xEE);
        const s32 colour = static_cast<s16>(colour_raw);
        const u64 bit = 1ull << (GRef<u32>(h + 0xE8) & 63);
        s32 count = 0, sel = 0;
        for (s32 c = 0; c < 3; ++c) {
            if ((bit & GRef<u64>(h + 0xB0 + static_cast<u32>(c) * 8)) != 0) {
                if (!(colour < c)) sel = count;
                GRef<u8>(list + static_cast<u32>(count)) = static_cast<u8>(c);
                ++count;
            }
        }
        s32 chosen;
        if (count <= 0) {
            chosen = 0;
        } else {
            sel += static_cast<s32>(repeat & 1);
            if ((repeat & 2) != 0) --sel;
            chosen = GRef<u8>(list + static_cast<u32>((sel + count) % count));
        }
        GRef<s16>(h + 0xEE) = static_cast<s16>(chosen);
        if (chosen != colour) change = 2;
    }
    if (moves != 0) {
        MoveHangarCrosshair(h, GRef<u16>(h + 0xA2), GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEE), GRef<s16>(h + 0xF0));
        if ((moves & 0xC) != 0) {
            const u32 v = g_AircraftViewer();
            GRef<s32>(h + 0x15C) = 5;
            if (GRef<s32>(v + 0x980) == 2) GRef<s32>(v + 0x980) = 3;
        }
    }
    if (change != 0) {
        PlayMenuSe(0, 4);
        ApplyHangarChange(h, change);
    }
}

// EE: 0x00234A08
// Special weapon pages: tab arrows; left/right step through the aircraft's available special
// weapons (texts restart, SE 4, crosshair).
void HangarSpecialInput(u32 h, u32 repeat) {
    if (GRef<u32>(h + 0x14C) != 0) {
        GRef<u16>(g_pMenuFade() + 0x32) = 0x30;
        return;
    }
    const u32 arrows = GetSpecialArrows(h);
    GRef<u16>(g_pMenuFade() + 0x32) = static_cast<u16>(arrows);
    const u32 steps = (arrows & 0xC) != 0 ? (repeat & 0xC) : 0;
    bool changed = false;
    if (steps != 0) {
        guest::GuestFrame frame(0x10);
        const u32 list = frame.Address();
        const u32 id = GRef<u32>(h + 0xE8) & 63;
        const u64 bits = ((GRef<u64>(h + 0xC8) >> id) & 1) | ((GRef<u64>(h + 0xD0) >> id) & 1) << 1 |
                         ((GRef<u64>(h + 0xD8) >> id) & 1) << 2;
        const s32 n = ListSetBits(list, 3, bits);
        if (n >= 2) {
            const s32 slot = GRef<s16>(h + 0xF0);
            s32 i = 0;
            if (n - 1 > 0 && GRef<u8>(list) < (static_cast<u32>(slot) & 0xFF)) {
                i = 1;
                while (i < n - 1 && GRef<u8>(list + static_cast<u32>(i)) < (static_cast<u32>(slot) & 0xFF)) ++i;
            }
            if ((repeat & 8) != 0) ++i;
            if ((repeat & 4) != 0) --i;
            const s32 chosen = GRef<u8>(list + static_cast<u32>((i + n) % n));
            if (slot != chosen) {
                const s32 count = GRef<s16>(h + 0xF6);
                changed = true;
                GRef<s16>(h + 0xF0) = static_cast<s16>((chosen + count) % count);
            }
        }
    }
    if (changed) {
        GRef<u32>(h + 0x104) = 0;
        GRef<u16>(h + 0xFA) |= 0x2020;
        PlayMenuSe(0, 4);
        MoveHangarCrosshair(h, GRef<u16>(h + 0xA2), GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEE), GRef<s16>(h + 0xF0));
    }
}

// EE: 0x00234EB8
// X/Circle (kinds from table 0x0039E1C8 by page): a purchase page asks first (SE 2, pending; SE 5
// when not possible) and applies on the second press (SE 0xF buy / 0x10 sell), updating the credits
// and checksum, the masks and the selection; another page goes to its next page (SE 2), unless the
// selection is not available (SE 5).
void HangarAccept(u32 h) {
    const u16 page = GRef<u16>(h + 0xA2);
    if (page >= 8) return;
    const s32 kind = GRef<s32>(0x0039E1C8 + static_cast<u32>(page) * 4);
    if (kind < 0) {
        if (GRef<u32>(h + 0x14C) != 0) {
            const s32 change = GetPurchasePrice(h + 0x70, kind, GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEC), GRef<s16>(h + 0xEE),
                                                GRef<s16>(h + 0xF0), GRef<s32>(h + 0x148), 1);
            if (change == 0) {
                PlayMenuSe(0, 5);
                return;
            }
            PlayMenuSe(0, change >= 0 ? 0x10 : 0xF);
            GRef<u32>(h + 0x14C) = 0;
            const s32 credits = GRef<s32>(h + 0x148) + change;
            GRef<s32>(h + 0x148) = credits;
            GRef<u32>(h + 0x168) = static_cast<u32>(credits) ^ 0x5963;
            BuildAvailabilityMasks(GRef<u32>(h + 0xE0), h + 0x70, h + 0xB0);
            const u16 p = GRef<u16>(h + 0xA2);
            u32 r;
            if (p < 8 && p >= 5) {
                r = SelectAvailableSpecial(h);
                if ((r & 4) != 0) {
                    GRef<u32>(h + 0x104) = 0;
                    GRef<u16>(h + 0xFA) |= 0x2020;
                }
            } else {
                r = SelectFirstAvailable(h);
            }
            if (r == 0) return;
            ApplyHangarChange(h, r);
            MoveHangarCrosshair(h, GRef<u16>(h + 0xA2), GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEE), GRef<s16>(h + 0xF0));
            return;
        }
        const s32 change = GetPurchasePrice(h + 0x70, kind, GRef<s32>(h + 0xE8), GRef<s16>(h + 0xEC), GRef<s16>(h + 0xEE),
                                            GRef<s16>(h + 0xF0), GRef<s32>(h + 0x148), 0);
        if (change != 0 && GRef<s32>(h + 0x148) + change >= 0) {
            PlayMenuSe(0, 2);
            GRef<u32>(h + 0x14C) = 1;
            return;
        }
        PlayMenuSe(0, 5);
        return;
    }
    const u64 bit = 1ull << (GRef<u32>(h + 0xE8) & 63);
    if (page == 0) {
        if ((GRef<u64>(h + 0xB0 + static_cast<u32>(GRef<s16>(h + 0xEE)) * 8) & bit) == 0) {
            PlayMenuSe(0, 5);
            return;
        }
    } else if (page == 5) {
        if ((GRef<u64>(h + 0xB0 + static_cast<u32>(GRef<s16>(h + 0xF0) + 3) * 8) & bit) == 0) {
            PlayMenuSe(0, 5);
            return;
        }
    }
    PlayMenuSe(0, 2);
    if (kind < 8 && kind >= 5 && SelectAvailableSpecial(h) != 0) {
        GRef<u32>(h + 0x104) = 0;
        GRef<u16>(h + 0xFA) |= 0x2020;
    }
    GRef<u16>(h + 0xA4) = static_cast<u16>(kind);
    SetHangarPhase(h, 4);
}

// EE: 0x002351B0
// Triangle: SE 3; cancels a pending purchase, else goes to the page from table 0x0039E1E8.
void HangarCancel(u32 h) {
    const u16 page = GRef<u16>(h + 0xA2);
    if (page >= 8) return;
    PlayMenuSe(0, 3);
    if (GRef<u32>(h + 0x14C) != 0) {
        GRef<u32>(h + 0x14C) = 0;
        return;
    }
    GRef<u16>(h + 0xA4) = GRef<u16>(0x0039E1E8 + static_cast<u32>(page) * 4);
    SetHangarPhase(h, 4);
}

// EE: 0x00235238
// The player's pad (player 2 also takes player 1's Triangle): accept, cancel, Start (pages 0/5 with
// the shown aircraft: the free camera pages 8/9), then the aircraft and special inputs by panel.
void HangarInput(u32 h) {
    const u32 player = GRef<u16>(h + 0xA0);
    const u32 panels = GRef<u16>(h + 0xF8);
    MainContext* ctx = Ctx();
    u32 held = ctx->pad_buttons[player][0];
    u32 repeat = ctx->pad_buttons[player][2];
    u32 pressed = ctx->pad_buttons[player][1];
    if (player != 0) {
        held |= ctx->pad_buttons[0][0] & 0x20;
        repeat |= ctx->pad_buttons[0][2] & 0x20;
        pressed |= ctx->pad_buttons[0][1] & 0x20;
    }
    if ((pressed & 0x50) != 0) {
        HangarAccept(h);
        return;
    }
    if ((pressed & 0x20) != 0) {
        HangarCancel(h);
        return;
    }
    if ((pressed & 0x2000) != 0) {
        const u32 v = g_AircraftViewer();
        if (GRef<s32>(v + 0x980) == 2 && GRef<s32>(v + 0x984) == GRef<s16>(h + 0xEC) + 1) {
            const s16 page = GRef<s16>(h + 0xA2);
            if (page == 0) {
                GRef<u16>(h + 0xA6) = 0;
                GRef<u16>(h + 0xA4) = 8;
                SetHangarPhase(h, 4);
                return;
            }
            if (page == 5) {
                GRef<u16>(h + 0xA6) = 5;
                GRef<u16>(h + 0xA4) = 9;
                SetHangarPhase(h, 4);
                return;
            }
        }
    }
    if ((panels & 0x100) != 0) HangarAircraftInput(h, repeat, held);
    if ((panels & 0x200) != 0) HangarSpecialInput(h, repeat);
}

// EE: 0x002353E8
// Free camera pages 8/9: Start goes back to the page in +0xA6.
void HangarConfirmInput(u32 h) {
    const u32 player = GRef<u16>(h + 0xA0);
    u32 pressed = Ctx()->pad_buttons[player][1];
    if (player != 0) pressed |= Ctx()->pad_buttons[0][1] & 0x20;
    if ((pressed & 0x2000) != 0) {
        GRef<u16>(h + 0xA4) = GRef<u16>(h + 0xA6);
        SetHangarPhase(h, 4);
    }
}

// EE: 0x00235438
// Panel animation; on the free camera pages 8/9 the page fade goes out and the tabs are hidden;
// otherwise the page fades in, the crosshair moves, the gauges approach the aircraft's statistics
// (0x0039DA8C + 0x3C x aircraft) and the input runs once the panels are settled.
void HangarUpdate(u32 h) {
    AnimateHangarPanels(h);
    const u32 fade = g_pMenuFade();
    if (static_cast<u32>(GRef<u16>(h + 0xA2) - 8) < 2) {
        f32 f = fpu::Sub(GRef<f32>(h + 0x158), K(0x00410284));
        if (f < 0.0f) f = 0.0f;
        GRef<f32>(h + 0x158) = f;
        GRef<u16>(fade + 0x32) = 0;
        GRef<u16>(fade + 0x30) = 0;
        HangarConfirmInput(h);
        return;
    }
    f32 f = fpu::Add(GRef<f32>(h + 0x158), K(0x00410288));
    if (1.0f < f) f = 1.0f;
    GRef<f32>(h + 0x158) = f;
    GRef<u16>(fade + 0x30) = 1;
    u16 flags = GRef<u16>(h + 0xFA);
    if ((flags & 0x200) != 0) {
        UpdateCrosshair(h);
        flags = GRef<u16>(h + 0xFA);
    }
    if ((flags & 0x100) != 0) {
        ApproachValues(6, h + 0x10C, 0x0039DA8C + static_cast<u32>(GRef<s16>(h + 0xEC) * 0x3C));
        HangarInput(h);
    } else {
        GRef<u16>(g_pMenuFade() + 0x32) = 0;
    }
}

// EE: 0x00235540
// Hangar BGM: the campaign progress track for sub 0x1B, else track 9.
void PlayHangarBgm(u32 sub) {
    s32 track = 9;
    if (sub == 0x1B) track = GetProgressBgmTrack(2, GRef<u16>(0x002A15B8));
    PlayMenuBgm(static_cast<s16>(track));
}

// EE: 0x00235580
// Opens the first page (phase 2).
void InitHangarPanels(u32 h) { SetHangarPhase(h, 2); }

// EE: 0x00235588
// Hangar state for `player` with `aircraft` (1-23, else 2) and `colour` (0-2, else 0): cleared (the
// panel list pointer kept), list position, special count, credits and checksum, page by sub, the
// purchase state in list order, masks, an available selection, the first page, page fade 1.
void InitHangarState(u32 h, u16 player, u32 aircraft, u32 colour) {
    const u32 panel_list = GRef<u32>(h + 0x144);
    ee::memset(h, 0, 0x170);
    GRef<u32>(h + 0x144) = panel_list;
    if (static_cast<u32>(aircraft - 1) >= 0x17) aircraft = 2;
    if (colour >= 3) colour = 0;
    const s16 pos = GetAircraftField8(static_cast<s32>(aircraft));
    GRef<u16>(h + 0xA0) = player;
    GRef<s16>(h + 0xEE) = static_cast<s16>(colour);
    GRef<s16>(h + 0xF0) = 0;
    GRef<s32>(h + 0xE8) = pos;
    GRef<s16>(h + 0xEC) = static_cast<s16>(aircraft - 1);
    GRef<u16>(h + 0xF4) = 0x15;
    GRef<s16>(h + 0xF6) = static_cast<s16>(GetSpecialWeaponCount(static_cast<s32>(aircraft)));
    const u32 credits = GRef<u32>(0x002A0D30);
    GRef<u32>(h + 0x148) = credits;
    GRef<u32>(h + 0x168) = credits ^ 0x5963;
    SetHangarPageForSub(h);
    for (u32 i = 0; i < 0x30; i += 8) GRef<u64>(h + 0x40 + i) = GRef<u64>(0x002A0320 + i);
    PurchaseStateToList(h + 0x70, h + 0x40);
    BuildAvailabilityMasks(GRef<u32>(h + 0xE0), h + 0x70, h + 0xB0);
    SelectFirstAvailable(h);
    InitHangarPanels(h);
    GRef<f32>(h + 0x158) = 1.0f;
}

// EE: 0x002356D8
// Subs 27-35: work heap back to the mark; the hangar state (player 2 for sub 0x1F) from the player's
// game slot; the viewer for its aircraft and colour (part mode 1 on pages 1/3, 2 on pages 2/4);
// camera preset 0 at once; BGM; menu fade frame; sub 0x24.
void HangarEnter() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    const u16 player = Ctx()->sub == 0x1F ? 1 : 0;
    const u32 slot = GRef<u32>(0x004104A4) + static_cast<u32>(player) * 16;
    const u32 colour = GRef<u8>(slot + 1);
    const u32 aircraft = GRef<u8>(slot);
    const u32 h = WorkHeapAlloc(0x170, 0x10);
    GRef<u32>(h + 0x144) = WorkHeapAlloc(0x80, 0x10);
    g_HangarState() = h;
    InitHangarState(h, player, aircraft, colour);
    const u32 v = CreateAircraftViewer(GRef<s16>(h + 0xEC) + 1, static_cast<u32>(static_cast<s32>(GRef<s16>(h + 0xEE))));
    const u16 page = GRef<u16>(h + 0xA2);
    g_AircraftViewer() = v;
    if (page == 2 || page == 4) {
        GRef<u32>(g_AircraftViewer() + 0x9B8) = 2;
    } else if (page == 1 || page == 3) {
        GRef<u32>(g_AircraftViewer() + 0x9B8) = 1;
    }
    UpdateViewerCamera(-1.0f, g_AircraftViewer() + 0x430, 0, 1);
    PlayHangarBgm(Ctx()->sub);
    DrawMenuFadeFrame(g_AircraftViewer(), g_pMenuFade());
    RequestSubState(0x24);
}

// EE: 0x00235828
// Viewer follows the selection: colour (pending while the viewer delay runs), aircraft (reload)
// once the delay is over, change pending → shown; highlight mask of the aircraft (bit 0: its colour
// for sale/owned by page) and its special weapons (bits 1-3).
void SyncHangarViewer(u32 v, u32 h) {
    const s32 colour = GRef<s16>(h + 0xEE);
    const u16 colour_raw = GRef<u16>(h + 0xEE);
    if (GRef<s32>(v + 0x988) != colour) {
        if (GRef<s32>(h + 0x15C) > 0) {
            GRef<s32>(v + 0x9B0) = colour;
        } else {
            GRef<s32>(v + 0x988) = colour;
        }
    }
    if (GRef<s32>(h + 0x15C) > 0) {
        GRef<s32>(h + 0x15C) -= 1;
    } else {
        const s32 aircraft = GRef<s16>(h + 0xEC) + 1;
        if (GRef<s32>(v + 0x984) != aircraft) {
            SetAircraftViewerAircraft(v, aircraft, static_cast<u32>(static_cast<s32>(static_cast<s16>(colour_raw))));
        } else if (GRef<s32>(g_AircraftViewer() + 0x980) == 3) {
            GRef<s32>(g_AircraftViewer() + 0x980) = 2;
        }
    }
    const u16 page = GRef<u16>(h + 0xA2);
    const u32 wanted = (page == 3 || page == 6) ? 2 : 3;
    u32 mask = 0;
    if (GRef<u32>(h + 0x154) == 0) {
        const u32 shift = (GRef<u32>(h + 0xE8) * 2) & 63;
        mask = ((GRef<u64>(h + 0x70 + static_cast<u32>(GRef<s16>(h + 0xEE)) * 8) >> shift) & 3) == wanted ? 1 : 0;
        for (u32 i = 0; i < 3; ++i) {
            if (((GRef<u64>(h + 0x70 + (i + 3) * 8) >> shift) & 3) == wanted) mask = (mask | (1u << (i + 1))) & 0xFFFF;
        }
    }
    if (GRef<u16>(v + 0x9AC) != mask) GRef<u16>(v + 0x9AC) = static_cast<u16>(mask);
}

// EE: 0x00235988
// Once the viewer shows something: help line, hangar update, viewer sync, camera; then the viewer.
void UpdateHangar(u32 v, u32 h) {
    if (GRef<u32>(v + 0x980) > 1) {
        GRef<u32>(v + 0x9B4) = 0;
        UpdateHangarHelp(v, h);
        HangarUpdate(h);
        SyncHangarViewer(v, h);
        UpdateHangarCamera(v + 0x430, h);
    }
    UpdateAircraftViewer(v);
}

// EE: 0x002359F0
// The viewer object's bucket list (7) into the VIF1 list: viewer scene, page fade bar, panels, help
// line, menu tabs and the black fade.
void DrawHangar(u32 v, u32 h) {
    const u32 fade = g_pMenuFade();
    InitBucketList(v, 7);
    SetPacketListBuckets(kMainContextAddress + 0x60, v);
    DrawAircraftViewer(v, v);
    DrawPageFadeBar(GRef<f32>(h + 0x158), v, 6);
    if (GRef<u32>(v + 0x980) > 1) {
        DrawHangarPanels(v, 6, g_HangarState());
        UploadMessageBoxGlyphs(v, 6, v + 0x958);
        DrawMessageBox(v, 6, v + 0x958);
    }
    DrawMenuTabs(v, 6, fade + 0x14);
    DrawBlackFade(GRef<f32>(fade + 0xC), v, 6);
}

// EE: 0x00235AB8
// Sub 36: update and draw; once the viewer leaves (state 3/4 accept, 5 cancel) the menu fade runs
// out, then the selection is committed and the menu goes to the accept or cancel target (clear
// colour black). Accepting into the mission (kinds 2/3): mission 1 with the default aircraft starts
// with game[0x27] = 0x25 and aircraft 0x17; mission 18 with game[0x27] = 0x24.
void AircraftSelectRun() {
    debug::OnGuestCheckpoint(0x00235AB8);
    const u32 h = g_HangarState();
    const u32 v = g_AircraftViewer();
    UpdateHangar(v, h);
    DrawHangar(v, h);
    const u32 state = GRef<u32>(v + 0x97C);
    const u32 fade = GRef<u32>(g_pMenuFade());
    if (state < 3) return;
    if (state < 5) {
        if ((fade & 8) == 0) {
            if ((fade & 2) == 0 && GRef<s32>(v + 0x980) == 2) StartFade(0.5f, g_pMenuFade(), 1);
            return;
        }
        CommitHangarSelection(h);
        MenuGoTo(GRef<u16>(h + 0xA8), GRef<u16>(h + 0xAA));
        GRef<u32>(kMainContextAddress + 0x78) = 0;
        BuildDrawEnvPackets();
        if (static_cast<u32>(GRef<u16>(h + 0xA8) - 2) < 2) {
            const u16 mission = GRef<u16>(0x002A15B8);
            const u32 game = GRef<u32>(0x004104A4);
            if (mission == 1 && (GRef<u64>(game) & 0xFF00FFFFull) == 2) {
                GRef<u8>(game + 0x27) = 0x25;
                GRef<u8>(GRef<u32>(0x004104A4)) = 0x17;
            } else if (mission == 0x12) {
                GRef<u8>(GRef<u32>(0x004104A4) + 0x27) = 0x24;
            }
        }
        return;
    }
    if (state == 5) {
        if ((fade & 8) != 0) {
            CommitHangarSelection(g_HangarState());
            MenuGoTo(GRef<u16>(g_HangarState() + 0xAC), GRef<u16>(g_HangarState() + 0xAE));
            GRef<u32>(kMainContextAddress + 0x78) = 0;
            BuildDrawEnvPackets();
            return;
        }
        if ((fade & 2) == 0 && GRef<s32>(v + 0x980) == 2) StartFade(0.5f, g_pMenuFade(), 1);
    }
}

// EE: 0x00231CE0
// Camera preset by hangar page (jump table 0x004071E0) while the viewer shows the selected aircraft:
// pages 0-4 orbit (3), special pages 5-7 their slot (4 + slot), 8 free camera (7), 9 free special
// camera (8, slot + 1); a change of preset jumps (0x100 marks the free cameras) and resets the time
// where needed. Otherwise preset 0 with the shadows off. The time advances by k per frame.
void UpdateHangarCamera(u32 look, u32 h) {
    const u32 v = g_AircraftViewer();
    const s32 cur = GRef<s32>(h + 0x160);
    s32 preset = cur;
    u32 flags = 0;
    if (GRef<u32>(v + 0x980) < 3 && GRef<s32>(v + 0x984) == GRef<s16>(h + 0xEC) + 1) {
        const u16 page = GRef<u16>(h + 0xA2);
        if (page <= 4) {
            preset = 3;
            if (cur != 3) {
                flags = 1;
                if (static_cast<u32>(cur - 3) >= 4) GRef<f32>(h + 0x164) = 0.0f;
            }
        } else if (page <= 7) {
            preset = GRef<s16>(h + 0xF0) + 4;
            flags = static_cast<u32>(cur - 4) < 3 ? 0 : 1;
        } else if (page == 8) {
            flags = cur == 7 ? 0 : 0x100;
            preset = 7;
        } else if (page == 9) {
            preset = 8;
            flags = static_cast<u32>(GRef<s16>(h + 0xF0) + 1);
            if (cur != 8) flags |= 0x100;
        }
    } else {
        preset = 0;
        GRef<u32>(v + 0x9B4) = 1;
        flags = cur != 0 ? 1 : 0;
    }
    GRef<s32>(h + 0x160) = preset;
    const f32 time = fpu::Add(GRef<f32>(h + 0x164), K(0x0041024C));
    GRef<f32>(h + 0x164) = time;
    UpdateViewerCamera(time, look, static_cast<u32>(preset), flags);
}

AC04_GUEST_FUNCTION(0x002356D8, HangarEnter);
AC04_GUEST_FUNCTION(0x00235AB8, AircraftSelectRun);

}  // namespace ac04::game
