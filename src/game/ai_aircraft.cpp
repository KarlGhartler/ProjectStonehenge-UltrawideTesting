// AI aircraft (slice S5e): UpdateAIAircraft 0x0012A880 and its command, behaviour, maneuver,
// steering, rotation, speed and weapon functions. The current unit is kept in globals
// (0x004117AC unit, 0x004117B0 AI state = unit +0x44, 0x004117B4 definition = unit +0x40,
// 0x004117B8 AI parameter record = first word of the state). Docs: docs/subsystems/ai.md;
// per-function notes: docs/port/s5-blockB-inventory.csv.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 AU() { return W(0x004117AC); }  // unit
u32 AS() { return W(0x004117B0); }  // AI state
u32 AD() { return W(0x004117B4); }  // unit definition record
u32 AP() { return W(0x004117B8); }  // AI parameters
u32 Setup() { return W(0x004104CC); }

f32 I2F(s32 v) { return fpu::IntToFloat(v); }
void Or8(u32 a, u8 v) { GRef<u8>(a) = static_cast<u8>(GRef<u8>(a) | v); }
void Inc8(u32 a, s32 d = 1) { GRef<u8>(a) = static_cast<u8>(GRef<u8>(a) + d); }

// Shared "turn to angle" start of several maneuvers: +0x78 = a - roll wrapped by (lim, sub,
// low, add); returns after storing.
// fpucheck: 3 adds (a - z, - sub, + add)
void SetRollTarget(f32 a, u32 lim, u32 sub, u32 lo, u32 add) {
    const u32 s = AS();
    const f32 v = fpu::Sub(a, F(AU() + 0x18));
    GRef<f32>(s + 0x78) = v;
    if (F(lim) < v) GRef<f32>(s + 0x78) = fpu::Sub(v, F(sub));
    if (F(s + 0x78) < F(lo)) GRef<f32>(s + 0x78) = fpu::Add(F(s + 0x78), F(add));
}

// The per-axis rate smoothing of UpdateAIRotation: remaining angle `rem`, rate at `rate_a`,
// limit L; accel cap / decel cap with optional limit flag (unit +0x56 bit 0).
// fpucheck: 5 adds per axis (2 * rate twice, rate + L, rate - L)
void RotAxis(f32 rem, u32 rate_a, f32 L, f32 accel_cap, f32 decel_cap, bool flag_acc, bool flag_dec) {
    const u32 u = AU();
    f32 r = F(rate_a);
    if (L < rem && fpu::Add(r, r) < rem) {
        r = r < -L ? r * 0.5f : fpu::Add(r, L);
        GRef<f32>(rate_a) = r;
        if (accel_cap < F(rate_a)) {
            GRef<f32>(rate_a) = accel_cap;
            if (flag_acc) GRef<u16>(u + 0x56) = static_cast<u16>(GRef<u16>(u + 0x56) | 1);
        }
        return;
    }
    if (rem < -L) {
        r = F(rate_a);
        if (rem < fpu::Add(r, r)) {
            r = L < r ? r * 0.5f : fpu::Sub(r, L);
            GRef<f32>(rate_a) = r;
            if (F(rate_a) < decel_cap) {
                GRef<f32>(rate_a) = decel_cap;
                if (flag_dec) GRef<u16>(u + 0x56) = static_cast<u16>(GRef<u16>(u + 0x56) | 1);
            }
            return;
        }
    }
    r = F(rate_a);
    if (-L < r && r < L)
        GRef<f32>(rate_a) = rem;
    else
        GRef<f32>(rate_a) = r * 0.5f;
}

}  // namespace

// EE: 0x0012A488
// Starts maneuver record m (12 bytes: kind, variant, s16 params) on the current AI unit: maneuver
// +0x46, variant +0x48, parameters +0x50..+0x5C by kind (angles from degrees, frame counts),
// speed mode 4 with the target speed +0xE4 = AP+0xC * m+10 * C 0x0040E018 * C 0x0040E01C.
void StartAIManeuver(u32 m) {
    const u32 s = AS();
    GRef<u8>(s + 0x46) = GRef<u8>(m);
    GRef<u8>(s + 0x48) = GRef<u8>(m + 1);
    const s32 p1 = GRef<s16>(m + 2), p2 = GRef<s16>(m + 4), p3 = GRef<s16>(m + 6), p4 = GRef<s16>(m + 8);
    switch (static_cast<s8>(GRef<u8>(m) - 1)) {
        case 0:
        case 4: GRef<f32>(s + 0x50) = I2F(p1); break;
        case 1:
            GRef<f32>(s + 0x50) = I2F(p1) * F(0x0040DFFC);
            GRef<f32>(s + 0x54) = I2F(p2) * F(0x0040DFFC);
            GRef<f32>(s + 0x58) = I2F(p3);
            break;
        case 2:
            GRef<f32>(s + 0x50) = I2F(p1) * F(0x0040E000) / 180.0f;
            GRef<f32>(s + 0x54) = I2F(p2) * F(0x0040E004);
            GRef<f32>(s + 0x58) = I2F(p3) * F(0x0040E004);
            GRef<f32>(s + 0x5C) = I2F(p4);
            break;
        case 3:
        case 5:
        case 6:
            GRef<f32>(s + 0x50) = I2F(p1) * F(0x0040E008) / 180.0f;
            GRef<f32>(s + 0x54) = I2F(p2) * F(0x0040E00C);
            GRef<f32>(s + 0x58) = I2F(p3) * F(0x0040E00C);
            break;
        case 7: GRef<f32>(s + 0x50) = I2F(p1) * F(0x0040E010); break;
        case 8:
            GRef<f32>(s + 0x50) = I2F(p1) * F(0x0040E014) / 180.0f;
            GRef<f32>(s + 0x54) = I2F(p2) * F(0x0040E014) / 180.0f;
            GRef<f32>(s + 0x58) = I2F(p3);
            break;
        default: break;
    }
    GRef<u8>(s + 0xE0) = 4;
    GRef<f32>(AS() + 0xE4) = F(AP() + 0xC) * I2F(GRef<s16>(m + 10)) * F(0x0040E018) * F(0x0040E01C);
}

// EE: 0x0012A880
// AI aircraft per frame (docs/subsystems/ai.md): hit reactions and kill credit (centre messages,
// chatter, 0x0011E560 notifications), destruction (0x200000: state 6, 0x0011E5E0, sounds, replay
// event), mission specials (ids 1, 7, 0xB, 0x12), events, target, route command and behaviour by
// command +0x1B, behaviour +0x3E -> handler, maneuver +0x46 -> handler, rotation, speed, weapons.
void UpdateAIAircraft(s32 i) {
    GRef<u32>(0x004117AC) = Unit(i);
    GRef<u32>(0x004117B0) = W(AU() + 0x44);
    GRef<u32>(0x004117B4) = W(AU() + 0x40);
    GRef<u32>(0x004117B8) = W(AS());
    u32 u = AU();
    if ((W(u + 0x50) & 0x20000u) != 0 && (W(u + 0x4C) & 0x1000000u) != 0) {
        if (GRef<u8>(u + 0x63) < Players()) {
            if (GRef<s16>(u + 0x5E) == 9999 && GRef<u8>(u + 0x69) != 0x12)
                ShowCenterMessage(GRef<u8>(u + 0x63), 0xD, 0);
            else
                ShowCenterMessage(GRef<u8>(u + 0x63), 0xC, 0);
        }
        u = AU();
        if (MissionKind() == 0 && Game(0x29) != 3 && GRef<s8>(u + 0x63) != -1 && GRef<s16>(u + 0x5E) != 9999) {
            const u32 side = W(u + 0x48) & 3u;
            const u32 by = Unit(GRef<u8>(u + 0x63));
            const u32 by_side = W(by + 0x48) & 3u;
            if (side == 0) {
                if (by_side != 0) func_0011E560(3, u, by);
            } else if (by_side == 0) {
                if (GRef<u8>(u + 0x63) < Players())
                    QueueRadioChatter(0x10, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(u + 0x99), 0, 0);
                else
                    func_0011E560(8, by, u);
            }
        }
    }
    u = AU();
    if ((W(u + 0x4C) & 0x200000u) != 0) {
        GRef<u8>(AS() + 0x3E) = 6;
        func_0011E5E0(AU());
        GRef<s32>(AU() + 0x84) = -1;
        PlayEventSe(1, AU());
        func_00171158(3, 0, 1, AU(), 0);
        u = AU();
        if (GRef<u8>(u + 0x63) < Players()) {
            ShowCenterMessage(GRef<u8>(u + 0x63), Game(0x27) == 0x15 ? 10 : 0xB, 0);
            u = AU();
            if (MissionKind() == 0 && Game(0x29) != 3) {
                if (GRef<s32>(u + 0x48) < 0) {
                    QueueRadioChatter(0x12, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(u + 0x99), 0, 0);
                    func_0011E560(0xB, 0, AU());
                } else if ((W(u + 0x48) & 3u) != 0) {
                    QueueRadioChatter(0x11, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(u + 0x99), 0, 0);
                }
            }
        } else if (GRef<u8>(u + 0x63) != 0xFF) {
            const u32 side = W(u + 0x48) & 3u;
            const u32 by = Unit(GRef<u8>(u + 0x63));
            const u32 by_side = W(by + 0x48) & 3u;
            if (side != by_side) {
                if (by_side == 0)
                    func_0011E560(9, by, AU());
                else if (side == 0)
                    func_0011E560(4, AU(), by);
            }
        }
    }
    u = AU();
    u32 s = AS();
    if ((W(u + 0x4C) & 0x1000000u) != 0) {
        if (Game(0x27) == 1 && GRef<u8>(u + 0x69) == 0x34) func_0017B8A0(i);
        if (Game(0x27) == 0xB && GRef<u8>(AU() + 0x69) == 0x32) {
            const u32 fl = UnitList() + 0x8B;
            if (GRef<u8>(AU() + 0x98) == 0) {
                if ((GRef<u8>(fl) & 4) != 0) Or8(fl, 4);
            } else if ((~GRef<u8>(fl) & 4) != 0) {
                Or8(fl, 4);
                func_00252FB8(0x1A, 1, 0);
            }
        }
        if (Game(0x27) == 7 && GRef<u8>(AU() + 0x69) == 0x12 && (W(AU() + 0x50) & 0xF0000u) != 0 &&
            GRef<u8>(AU() + 0x63) == 0 && (~StatusFlags() & 0x80000u) != 0) {
            GRef<u32>(Status() + 0x20) |= 0x280000u;
            QueueRadioMessage(0x1E);
            QueueRadioMessage(0x21);
        }
        if (Game(0x27) == 0x12) {
            const u8 m = GRef<u8>(AU() + 0x69);
            if (m == 0x3B) {
                func_00135C68(F(AU() + 8));
            } else if (m == 0x11 && (W(AU() + 0x50) & 0x70000000u) != 0 && (~GRef<u8>(UnitList() + 0x8B) & 4) != 0) {
                Or8(UnitList() + 0x8B, 4);
                func_00252FB8(0x12, 1, 0);
            }
        }
        DetectAIEvents();
        UpdateAITarget();
        s = AS();
        if (MissionKind() == 0) {
            if (GRef<u8>(AU() + 0x62) == 0 && GRef<u8>(Unit(0) + 0x62) == GRef<u8>(AU() + 0x55) && GRef<u8>(s + 0x3D) == 2)
                GRef<s32>(s + 0xF0) += 1;
            else
                GRef<u32>(s + 0xF0) = 0;
        }
        if ((GRef<u8>(s + 0x19) & 0x80) != 0) {
            FetchAIRouteCommand();
            s = AS();
        }
        if (GRef<u8>(s + 0x1B) == 6) GRef<u32>(s + 0x10) = 0;
        if (W(s + 0x10) == 0) {
            switch (GRef<u8>(s + 0x1B)) {
                case 0: func_0012BC90(); break;
                case 1: func_0012BE78(); break;
                case 2: func_0012C0B8(); break;
                case 3: break;
                case 4: func_0012BD88(); break;
                case 5: func_0012C190(); break;
                case 6: func_0012C230(); break;
                case 7: func_0012BF88(); break;
                case 0xFF: func_0012BBE0(); break;
                default: break;
            }
        } else {
            GRef<u8>(AU() + 0x62) = 0xFF;
            SelectAIReaction();
        }
        s = AS();
        GRef<s32>(s + 0x20) += 1;
    }
    if (GRef<u8>(s + 0x3D) != GRef<u8>(s + 0x3E)) {
        GRef<u8>(s + 0x3D) = GRef<u8>(s + 0x3E);
        GRef<u8>(AS() + 0x3F) = 0;
        GRef<u32>(AS() + 0x38) = 0;
        s = AS();
    }
    switch (GRef<u8>(s + 0x3D)) {
        case 0: func_0012C330(); break;
        case 1: func_0012C358(); break;
        case 2: func_0012C3A0(); break;
        case 3: func_0012C428(); break;
        case 4: func_0012C678(); break;
        case 5: func_0012C8A8(); break;
        case 6: func_0012C970(); break;
        case 7: func_0012CA40(); break;
        case 8: func_0012CDB0(); break;
        case 9: func_0012CDB8(); break;
        default: break;
    }
    s = AS();
    GRef<s32>(s + 0x38) += 1;
    if (GRef<u8>(s + 0x45) != GRef<u8>(s + 0x46)) {
        GRef<u8>(s + 0x45) = GRef<u8>(s + 0x46);
        GRef<u8>(AS() + 0x44) = 0;
        GRef<u8>(AS() + 0x47) = 0;
        s = AS();
        GRef<f32>(s + 0x88) = 1.0f;
        GRef<u32>(s + 0x40) = 0;
        GRef<f32>(s + 0x80) = 1.0f;
        GRef<f32>(s + 0x84) = 1.0f;
    }
    switch (GRef<u8>(s + 0x45)) {
        case 0: func_0012CFA8(); break;
        case 1: func_0012CFB0(); break;
        case 2: func_0012D058(); break;
        case 3: func_0012D4F8(); break;
        case 4: func_0012D6F8(); break;
        case 5: func_0012D7D0(); break;
        case 6: func_0012DA90(); break;
        case 7: func_0012D920(); break;
        case 8: func_0012DC90(); break;
        case 9: func_0012DD50(); break;
        case 0x10: func_0012E1D0(); break;
        case 0x11: func_0012D150(); break;
        case 0x12: func_0012D320(); break;
        case 0x20: func_0012E1F8(); break;
        case 0x21: func_0012E3C8(); break;
        case 0x22: func_0012E448(); break;
        case 0x23: func_0012E5C0(); break;
        default: break;
    }
    GRef<s32>(AS() + 0x40) += 1;
    UpdateAIRotation();
    UpdateAISpeed();
    if ((W(AU() + 0x4C) & 0x1000000u) != 0 && GRef<u8>(AS() + 0x1B) != 6 && (StatusFlags() & 0x100u) != 0)
        UpdateAIWeapons();
}

// EE: 0x0012B1E0
// Event flags +0x10 of the AI unit: altitude limits (point 300 frames ahead above the ceiling
// AD+0x14 or below the terrain: 1; below the floor AD+0x18: 2), phase retreat (0x40), mission
// 7 unit 0x12 flag, out of area (|x|, |z| > 262144: 4), away from the guard point +0xB0 (radius
// +0xC0, or the guarded unit +0xCC: 8), attack timer +0x14 by the behaviour table (0x10), hit
// reactions (0x20).
// fpucheck: none (comparisons only)
void DetectAIEvents() {
    guest::GuestFrame f(0x20);
    const u32 ahead = f.Address(), d = f.Address(0x10);
    sceVu0ScaleVector(ahead, AU() + 0x20, 300.0f);
    sceVu0AddVector(ahead, AU(), ahead);
    u32 s = AS(), def = AD();
    if (F(def + 0x14) < F(ahead + 4) || func_0013E338(Terrain(), ahead) < F(ahead + 4)) {
        def = AD();
        s = AS();
        GRef<u32>(s + 0x10) |= 1u;
    } else {
        def = AD();
        s = AS();
    }
    if (F(ahead + 4) < F(def + 0x18)) GRef<u32>(s + 0x10) |= 2u;
    const u32 u = AU();
    const u32 st = Status();
    if ((W(st + 0x20) & 4u) != 0 && (W(u + 0x4C) & 0x4000000u) != 0) {
        const u32 side = W(u + 0x48) & 3u;
        if ((side == 1 && (W(Rules() + 0x10) & 4u) != 0) || (side == 0 && (W(Rules() + 0x10) & 8u) != 0))
            GRef<u32>(s + 0x10) |= 0x40u;
    }
    if (GRef<u8>(AU() + 0x69) == 0x12 && (W(st + 0x20) & 0x80000u) != 0) GRef<u32>(s + 0x10) |= 0x40u;
    if (GRef<u8>(s + 0x1B) != 1) {
        const f32 x = F(ahead), z = F(ahead + 8);
        if (x < -262144.0f || 262144.0f < x || z < -262144.0f || 262144.0f < z) GRef<u32>(s + 0x10) |= 4u;
        const s32 g = GRef<s32>(s + 0xCC);
        if (g != 0xFF) {
            CopyQ(s + 0xB0, Unit(g));
            s = AS();
        }
        sceVu0SubVector(d, ahead, s + 0xB0);
        def = AD();
        s = AS();
        const f32 r = F(s + 0xC0);
        const f32 dx = F(d), dz = F(d + 8);
        if (dx < -r || r < dx || dz < -r || r < dz) GRef<u32>(AS() + 0x10) |= 8u;
    }
    if ((~GRef<u8>(s + 0x3C) & 1) == 0 || (W(AU() + 0x4C) & 0x10000u) == 0 || GRef<s8>(def + 0x22) == -1) {
        GRef<u32>(s + 0x14) = 0;
    } else {
        const s32 t = GRef<s32>(s + 0x14) + 1;
        GRef<s32>(s + 0x14) = t;
        if (!(I2F(t) * F(0x0040E024) <= F(W(Setup() + 0x58) + static_cast<u32>(GRef<u8>(def + 0x22)) * 0xC))) {
            GRef<u32>(s + 0x14) = 0;
            GRef<u32>(s + 0x10) |= 0x10u;
        }
    }
    if ((GRef<u8>(s + 0x3C) & 1) != 0) GRef<u32>(s + 0x10) |= 0x10u;
    if (GRef<s8>(def + 0x23) != -1) {
        if ((W(AU() + 0x50) & 0x13010000u) != 0) GRef<u32>(s + 0x10) |= 0x20u;
        if ((GRef<u8>(s + 0x3C) & 2) != 0) GRef<u32>(s + 0x10) |= 0x20u;
    }
}

// EE: 0x0012B540
// Target choice every 64 frames (phase by unit index): the nearest unit of another side that is
// active and alive, aircraft (AD+0xB bit 0x40, +0x98 <= 1) or ground targets (bit 0x80), within
// AD+0xC; players are taken at once. Count of candidates +0x68; target age +0x6C dropped after
// 20 s. Every 4 frames: distance +0xA4, target in the body frame +0x90, cos of the angle +0xA8;
// first enemy aircraft within 32768 notifies 0x0011E560(0).
void UpdateAITarget() {
    u32 u = AU();
    if ((GRef<u8>(AD() + 0xB) & 0xC0) == 0) return;
    if (GRef<s8>(u + 0x62) != -1) GRef<s32>(u + 0x6C) += 1;
    if ((GRef<u8>(u + 0x55) & 0x3Fu) == (W(Scene() + 0x165C) & 0x3Fu)) {
        if (20.0f < I2F(GRef<s32>(u + 0x6C)) * F(0x0040E028)) GRef<u8>(u + 0x62) = 0xFF;
        u = AU();
        if (GRef<s8>(u + 0x62) != -1) goto track;
        GRef<u8>(u + 0x68) = 0;
        u32 best = 0xFF;
        const u8 my = GRef<u8>(u + 0x48);
        f32 bd = 524288.0f;
        for (u32 k = 0; static_cast<s32>(k) < UnitCount(); ++k) {
            const u32 o = Unit(static_cast<s32>(k));
            const u32 f48 = W(o + 0x48);
            if ((f48 & 3u) == (my & 3u)) continue;
            if ((~W(o + 0x4C) & 0x10000000u) != 0) continue;
            if ((~W(o + 0x4C) & 0x1000000u) != 0) continue;
            if ((f48 & 0x1000000u) == 0) {
                if ((f48 & 0x2000000u) == 0) continue;
                if ((~GRef<u8>(AD() + 0xB) & 0x80) != 0) continue;
            } else {
                if ((~GRef<u8>(AD() + 0xB) & 0x40) != 0) continue;
                if (1 < GRef<u8>(o + 0x98)) continue;
            }
            f32 dist = func_00104F10(AU(), o);
            if (F(AD() + 0xC) < dist) continue;
            Inc8(AU() + 0x68);
            u32 pick = k;
            if (bd <= dist) {
                dist = bd;
                pick = best;
            }
            best = pick;
            bd = dist;
            if (best < Players()) break;
        }
        GRef<u8>(AU() + 0x62) = static_cast<u8>(best);
        GRef<u32>(AU() + 0x6C) = 0;
        u = AU();
    }
    if (GRef<s8>(u + 0x62) == -1) return;
track:
    u = AU();
    if ((GRef<u8>(u + 0x55) & 3u) != (W(Scene() + 0x165C) & 3u)) return;
    const u32 t = Unit(GRef<u8>(u + 0x62));
    if ((~W(t + 0x4C) & 0x11000000u) != 0) {
        GRef<u8>(u + 0x62) = 0xFF;
        return;
    }
    GRef<f32>(AS() + 0xA4) = func_00104F10(u, t);
    guest::GuestFrame f(0x50);
    const u32 m = f.Address(), d = f.Address(0x40);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, AU() + 0x10);
    sceVu0InversMatrix(m, m);
    sceVu0SubVector(d, t, AU());
    sceVu0ApplyMatrix(AS() + 0x90, m, d);
    const u32 s = AS();
    GRef<f32>(s + 0xA8) = F(s + 0x98) / F(s + 0xA4);
    if ((~GRef<u8>(s + 0xF4) & 1) != 0 && (W(AU() + 0x48) & 3u) == 0 && GRef<u8>(t + 0x54) == 1 && F(s + 0xA4) < 32768.0f) {
        Or8(s + 0xF4, 1);
        func_0011E560(0, AU(), t);
    }
}

// EE: 0x0012B8A0
// Reaction to the event flags +0x10 (priority 1, 0x40, 4, 2, 8, 0x20, 0x10): next behaviour
// +0x3E (5 climb, 7 retreat, 1 return to the area origin / guard point, 4 evade, 3 attack) and
// the flag consumed.
void SelectAIReaction() {
    const u32 s = AS();
    const u32 f = W(s + 0x10);
    u32 clear;
    if ((f & 1u) != 0) {
        GRef<u8>(s + 0x3E) = 5;
        clear = 0xFFFFFFFEu;
    } else if ((f & 0x40u) != 0) {
        GRef<u8>(s + 0x3E) = 7;
        clear = 0xFFFFFFBFu;
    } else if ((f & 4u) != 0) {
        GRef<u32>(s + 0xD0) = 0;
        GRef<u8>(s + 0x3E) = 1;
        GRef<u32>(s + 0xD4) = 0;
        clear = 0xFFFFFFFBu;
        GRef<u32>(s + 0xD8) = 0;
        GRef<u8>(AS() + 0xE0) = 1;
    } else if ((f & 2u) != 0) {
        GRef<u8>(s + 0x3E) = 5;
        clear = 0xFFFFFFFDu;
    } else if ((f & 8u) != 0) {
        CopyQ(s + 0xD0, s + 0xB0);
        GRef<u8>(AS() + 0x3E) = 1;
        clear = 0xFFFFFFF7u;
        GRef<u8>(AS() + 0xE0) = 1;
    } else if ((f & 0x20u) != 0) {
        GRef<u8>(s + 0x3E) = 4;
        clear = 0xFFFFFFDFu;
    } else if ((f & 0x10u) != 0) {
        GRef<u8>(s + 0x3E) = 3;
        clear = 0xFFFFFFEFu;
    } else {
        return;
    }
    GRef<u32>(AS() + 0x10) &= clear;
}

// EE: 0x0012B9B0
// Next route command of the AI unit (route table of the placement record, block 22 +0x16 ->
// Setup()+0x64 lists of u16 command indices, commands 0x18 bytes at Setup()+0x60): 0xFFE0+n jumps
// to step n, 0xFFDF removes the unit, 0xFFDE destroys it (events) and removes it, 0xFFFF / no
// route: command 0xFF
// with the placement as guard point; else command kind +0x1B, 3 parameter bytes +0x1C, 5 words
// +0x24..+0x34, step +0x18 + 1.
void FetchAIRouteCommand() {
    u32 s = AS();
    GRef<u8>(s + 0x19) = static_cast<u8>(GRef<u8>(s + 0x19) & 0x7F);
    GRef<u8>(s + 0x1A) = 0;
    GRef<u32>(s + 0x20) = 0;
    u32 cmd = 0xFFFF;
    if (GRef<u8>(s + 0x18) < 0x20) {
        const u32 route = GRef<u8>(static_cast<u32>(GRef<u8>(AU() + 0x55)) * 0x18 + W(Setup() + 4) + 0x16);
        if (route != 0xFF) {
            const u32 list = route * 0x40 + W(Setup() + 100);
            const u16 c = GRef<u16>(list + static_cast<u32>(GRef<u8>(s + 0x18)) * 2);
            cmd = c;
            if (c == 0xFFFF || c < 0xFFE0) {
                if (c == 0xFFDF || c == 0xFFDE) {
                    if (c == 0xFFDE) func_0011E708(AU());
                    func_0011BCC0(GRef<u8>(AU() + 0x55));
                    const u32 u = AU();
                    GRef<u16>(u + 0x5E) = 0;
                    GRef<u32>(u + 0x4C) &= 0xFEFFFFFFu;
                    return;
                }
            } else {
                // 0xFFE0..0xFFFE: jump to step (c & 0x1F) of the route.
                GRef<u8>(s + 0x18) = static_cast<u8>(c & 0x1F);
                cmd = GRef<u16>(list + static_cast<u32>(GRef<u8>(AS() + 0x18)) * 2);
                s = AS();
            }
        }
    }
    if (cmd == 0xFFFF) {
        GRef<u8>(s + 0x1B) = 0xFF;
        for (u32 k = 0; k < 3; ++k) GRef<u8>(AS() + 0x1C + k) = 0;
        GRef<u32>(AS() + 0x24) = 0;
        s = AS();
        const u32 p = W(Setup() + 4) + static_cast<u32>(GRef<u8>(AU() + 0x55)) * 0x18;
        GRef<u32>(s + 0x34) = 0x8000;
        GRef<s32>(s + 0x30) = fpu::FloatToInt(F(p + 0xC));
        GRef<s32>(s + 0x28) = fpu::FloatToInt(F(p + 4));
        GRef<s32>(s + 0x2C) = fpu::FloatToInt(F(p + 8));
    } else {
        const u32 c = W(Setup() + 0x60) + cmd * 0x18;
        GRef<u8>(s + 0x1B) = GRef<u8>(c);
        for (u32 k = 0; k < 3; ++k) GRef<u8>(AS() + 0x1C + k) = GRef<u8>(c + 1 + k);
        for (u32 k = 0; k < 5; ++k) GRef<u32>(AS() + 0x24 + 4 * k) = W(c + 4 + 4 * k);
        s = AS();
        Inc8(s + 0x18);
    }
}

// EE: 0x0012BBE0
// Command 0xFF (hold the placement): guard point and radius, then attack (2) when a target
// exists, else return (1).
void func_0012BBE0() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x1A);
    if (st == 0) {
        GRef<u32>(s + 0xCC) = 0xFF;
        GRef<f32>(s + 0xB0) = I2F(GRef<s32>(s + 0x28));
        GRef<f32>(s + 0xB4) = I2F(GRef<s32>(s + 0x2C));
        GRef<f32>(s + 0xB8) = I2F(GRef<s32>(s + 0x30));
        GRef<f32>(s + 0xC0) = I2F(GRef<s32>(s + 0x34));
        CopyQ(s + 0xD0, s + 0xB0);
        GRef<u8>(AS() + 0x3E) = 0;
        Inc8(AS() + 0x1A);
        return;
    }
    if (st != 1) return;
    if (GRef<s8>(AU() + 0x62) == -1) {
        GRef<u8>(AS() + 0x3E) = 1;
        GRef<u8>(AS() + 0xE0) = 1;
        return;
    }
    GRef<u8>(AS() + 0x3E) = 2;
}

// EE: 0x0012BC90
// Command 0 (patrol a point for a time or until N targets): guard point from the command words.
void func_0012BC90() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x1A);
    if (st == 0) {
        GRef<u8>(s + 0x3E) = 0;
        GRef<u32>(s + 0xCC) = 0xFF;
        GRef<f32>(s + 0xB0) = I2F(GRef<s32>(s + 0x28));
        GRef<f32>(s + 0xB4) = I2F(GRef<s32>(s + 0x2C));
        GRef<f32>(s + 0xB8) = I2F(GRef<s32>(s + 0x30));
        GRef<f32>(s + 0xC0) = I2F(GRef<s32>(s + 0x34));
        Inc8(AS() + 0x1A);
        return;
    }
    if (st != 1) return;
    GRef<u8>(s + 0x3E) = GRef<s8>(AU() + 0x62) == -1 ? 0 : 2;
    const u32 t = AS();
    const u8 mode = GRef<u8>(t + 0x1C);
    if (mode == 1) {
        if (GRef<s32>(t + 0x20) <= GRef<s32>(t + 0x24)) return;
    } else if (mode == 2) {
        if (GRef<s32>(t + 0x24) < static_cast<s32>(GRef<u8>(AU() + 0x68))) return;
    } else {
        return;
    }
    Or8(t + 0x19, 0x80);
}

// EE: 0x0012BD88
// Command 4 (hold the current position).
void func_0012BD88() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x1A);
    if (st == 0) {
        const u32 u = AU();
        GRef<u32>(s + 0xCC) = 0xFF;
        GRef<u32>(s + 0xB0) = W(u);
        GRef<u32>(s + 0xB4) = W(u + 4);
        GRef<u8>(s + 0x3E) = 0;
        GRef<u32>(s + 0xB8) = W(u + 8);
        GRef<f32>(s + 0xC0) = I2F(GRef<s32>(s + 0x28));
        Inc8(AS() + 0x1A);
        return;
    }
    if (st != 1) return;
    GRef<u8>(s + 0x3E) = GRef<s8>(AU() + 0x62) == -1 ? 0 : 2;
    const u32 t = AS();
    const u8 mode = GRef<u8>(t + 0x1C);
    if (mode == 1) {
        if (GRef<s32>(t + 0x20) <= GRef<s32>(t + 0x24)) return;
    } else if (mode == 2) {
        if (GRef<s32>(t + 0x24) < static_cast<s32>(GRef<u8>(AU() + 0x68))) return;
    } else {
        return;
    }
    Or8(t + 0x19, 0x80);
}

// EE: 0x0012BE78
// Command 1 (fly to a point): go there at speed +0x34 (mode 4, scaled by C 0x0040E02C /
// 0x0040E030); done within radius +0x30.
void func_0012BE78() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x1A);
    if (st == 0) {
        ZeroQ(s + 0xB0);
        const u32 t = AS();
        GRef<u8>(t + 0x3E) = 1;
        GRef<u32>(t + 0xC0) = 0x48800000u;
        GRef<u32>(t + 0xCC) = 0xFF;
        GRef<f32>(t + 0xD0) = I2F(GRef<s32>(t + 0x24));
        GRef<f32>(t + 0xD4) = I2F(GRef<s32>(t + 0x28));
        GRef<f32>(t + 0xD8) = I2F(GRef<s32>(t + 0x2C));
        Inc8(AS() + 0x1A);
    } else if (st == 1) {
        GRef<u8>(s + 0x3E) = 1;
        if (func_00104F10(AS() + 0xD0, AU()) < I2F(GRef<s32>(AS() + 0x30))) Or8(AS() + 0x19, 0x80);
    }
    GRef<u8>(AS() + 0xE0) = 4;
    GRef<f32>(AS() + 0xE4) = I2F(GRef<s32>(AS() + 0x34)) * 5.0f / F(0x0040E02C) * F(0x0040E030);
}

// EE: 0x0012BF88
// Command 7 (fly to an offset from the current position).
// fpucheck: 3 adds
void func_0012BF88() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x1A);
    if (st == 0) {
        ZeroQ(s + 0xB0);
        const u32 t = AS(), u = AU();
        GRef<u32>(t + 0xC0) = 0x48800000u;
        GRef<u32>(t + 0xCC) = 0xFF;
        GRef<f32>(t + 0xD0) = fpu::Add(F(u), I2F(GRef<s32>(t + 0x24)));
        GRef<f32>(t + 0xD4) = fpu::Add(F(u + 4), I2F(GRef<s32>(t + 0x28)));
        GRef<u8>(t + 0x3E) = 1;
        GRef<f32>(t + 0xD8) = fpu::Add(F(u + 8), I2F(GRef<s32>(t + 0x2C)));
        Inc8(AS() + 0x1A);
    } else if (st == 1) {
        GRef<u8>(s + 0x3E) = 1;
        if (func_00104F10(AS() + 0xD0, AU()) < I2F(GRef<s32>(AS() + 0x30))) Or8(AS() + 0x19, 0x80);
    }
    GRef<u8>(AS() + 0xE0) = 4;
    GRef<f32>(AS() + 0xE4) = I2F(GRef<s32>(AS() + 0x34)) * 5.0f / F(0x0040E034) * F(0x0040E038);
}

// EE: 0x0012C0B8
// Command 2 (escort unit +0x1C for a time; ends when it dies).
void func_0012C0B8() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x1A);
    if (st == 0) {
        const s32 r = GRef<s32>(s + 0x24);
        GRef<u8>(s + 0x3E) = 0;
        GRef<u32>(s + 0xCC) = GRef<u8>(s + 0x1C);
        GRef<f32>(s + 0xC0) = I2F(r);
        Inc8(AS() + 0x1A);
        return;
    }
    if (st != 1) return;
    GRef<u8>(s + 0x3E) = GRef<s8>(AU() + 0x62) == -1 ? 0 : 2;
    if (GRef<s32>(AS() + 0x28) < GRef<s32>(AS() + 0x20)) Or8(AS() + 0x19, 0x80);
    if ((~W(Unit(GRef<s32>(AS() + 0xCC)) + 0x4C) & 0x1000000u) != 0) Or8(AS() + 0x19, 0x80);
}

// EE: 0x0012C190
// Command 5 (straight flight for +0x20 frames at speed +0x28).
void func_0012C190() {
    GRef<u8>(AS() + 0x3E) = 8;
    GRef<u8>(AS() + 0x46) = 1;
    const u32 s = AS();
    GRef<f32>(s + 0x50) = I2F(GRef<s32>(s + 0x24));
    if (GRef<s32>(s + 0x28) == 0) {
        GRef<u8>(s + 0xE0) = 1;
    } else {
        GRef<u8>(s + 0xE0) = 4;
        GRef<f32>(AS() + 0xE4) = I2F(GRef<s32>(AS() + 0x28)) * 5.0f / F(0x0040E03C) * F(0x0040E040);
    }
    const u32 t = AS();
    if (GRef<s32>(t + 0x24) < GRef<s32>(t + 0x20)) Or8(t + 0x19, 0x80);
}

// EE: 0x0012C230
// Command 6 (scripted flight to a point with maneuver 0x12).
void func_0012C230() {
    const u8 st = GRef<u8>(AS() + 0x1A);
    if (st == 0) {
        GRef<u8>(AS() + 0x3E) = 8;
        GRef<u8>(AS() + 0x46) = 0x12;
        ZeroQ(AS() + 0xB0);
        const u32 s = AS();
        GRef<u32>(s + 0xC0) = 0x49000000u;
        Inc8(s + 0x1A);
        GRef<u32>(s + 0xCC) = 0xFF;
        GRef<f32>(s + 0xD0) = I2F(GRef<s32>(s + 0x24));
        GRef<f32>(s + 0xD4) = I2F(GRef<s32>(s + 0x28));
        GRef<f32>(s + 0xD8) = I2F(GRef<s32>(s + 0x2C));
        GRef<f32>(s + 0x50) = I2F(GRef<s32>(s + 0x34));
    } else if (st == 1) {
        GRef<u8>(AS() + 0x3E) = 8;
        GRef<u8>(AS() + 0x46) = 0x12;
        if (func_00104F10(AS() + 0xD0, AU()) < I2F(GRef<s32>(AS() + 0x30))) Or8(AS() + 0x19, 0x80);
    }
    GRef<u8>(AS() + 0xE0) = 0;
}

// EE: 0x0012C330
// Behaviour 0 (cruise): maneuver 1, speed mode 1, +0x50 = 660.
void func_0012C330() {
    GRef<u8>(AS() + 0x46) = 1;
    GRef<u8>(AS() + 0xE0) = 1;
    GRef<u32>(AS() + 0x50) = 0x44250000u;
}

// EE: 0x0012C358
// Behaviour 1 (go to the point +0xD0): maneuver 0x10; unit 0x12 in the escape phase dodges.
void func_0012C358() {
    GRef<u8>(AS() + 0x46) = 0x10;
    if (GRef<u8>(AU() + 0x69) == 0x12 && (StatusFlags() & 0x200000u) != 0) func_0012CBD8();
}

// EE: 0x0012C3A0
// Behaviour 2 (chase the target): point = target position (64 above), speed mode 5 (target speed,
// distance).
// fpucheck: 1 add
void func_0012C3A0() {
    GRef<u8>(AS() + 0x46) = 0x10;
    const u32 t = Unit(GRef<u8>(AU() + 0x62));
    CopyQ(AS() + 0xD0, t);
    const u32 s = AS();
    GRef<u8>(s + 0xE0) = 5;
    GRef<f32>(s + 0xD4) = fpu::Sub(F(s + 0xD4), 64.0f);
    GRef<f32>(AS() + 0xE4) = GetVectorLength(t + 0x20);
    GRef<u32>(AS() + 0xE8) = W(AS() + 0xA4);
}

// EE: 0x0012C428
// Behaviour 3 (attack maneuvers): first frame picks a maneuver set by the threat distance (the
// two missile-threat tables 0x00410648/0x0041064C when the unit is targeted), then a maneuver of
// the set by the cumulative chance table (frame + index) % 100; when it ends, back to behaviour 0
// once the attack time AS+0x38 * C exceeds the set's limit.
void func_0012C428() {
    const u32 tbl = W(Setup() + 0x58) + static_cast<u32>(GRef<u8>(AD() + 0x22)) * 0xC;
    GRef<u8>(AU() + 0x62) = 0xFF;
    f32 d = 65536.0f;
    const u32 s = AS();
    if (GRef<u8>(s + 0x3F) == 0) {
        Or8(s + 0x3C, 1);
        const u32 u = AU();
        const u32 idx = static_cast<u32>(GRef<u8>(u + 0x55)) * 0xC;
        if ((W(u + 0x4C) & 0x20000u) != 0) d = F(idx + W(0x00410648) + 4);
        if ((W(u + 0x4C) & 0x40000u) != 0) {
            const f32 e = F(idx + W(0x0041064C) + 4);
            if (e < d) d = e;
        }
        const u32 set = 16384.0f < d ? GRef<u8>(tbl + 8) : (8192.0f < d ? GRef<u8>(tbl + 9) : GRef<u8>(tbl + 10));
        const u32 sp = W(Setup() + 0x54) + set * 0x20;
        const s32 r = static_cast<s32>(W(Scene() + 0x165C) + GRef<u8>(AU() + 0x55)) % 100;
        u32 m = W(Setup() + 0x50);  // no entry matches: maneuver 0 of the table
        if (r < static_cast<s32>(GRef<u8>(sp + 0x10))) {
            m = W(Setup() + 0x50) + static_cast<u32>(GRef<u8>(sp)) * 0xC;
        } else {
            for (u32 k = 1; k <= 0xF; ++k) {
                if (r < static_cast<s32>(GRef<u8>(sp + k + 0x10))) {
                    m = W(Setup() + 0x50) + static_cast<u32>(GRef<u8>(sp + k)) * 0xC;
                    break;
                }
            }
        }
        StartAIManeuver(m);
        Inc8(AS() + 0x3F);
    } else if (GRef<u8>(s + 0x3F) == 1 && (GRef<u8>(s + 0x44) & 0x80) != 0) {
        GRef<u8>(s + 0x46) = 0;
        Inc8(AS() + 0x3F, -1);
        if (F(tbl + 4) < I2F(GRef<s32>(AS() + 0x38)) * F(0x0040E044)) {
            GRef<u8>(AS() + 0x3E) = 0;
            GRef<u8>(AS() + 0x3C) = static_cast<u8>(GRef<u8>(AS() + 0x3C) & 0xFE);
        }
    }
    if (GRef<u8>(AU() + 0x69) == 0x12 && (StatusFlags() & 0x200000u) != 0) func_0012CBD8();
}

// EE: 0x0012C678
// Behaviour 4 (evade): after the start delay (+0x3F 0 -> 1 when the attack time exceeds the
// evasion table's start), an evasion maneuver by the chance table (frame + index) % 100; when it
// ends, back to behaviour 0 after the table's time. Unit 0x12 in the escape phase is pushed by
// a rotation of an uninitialised stack matrix in the original (see the note below).
void func_0012C678() {
    const u32 tb = W(Setup() + 0x5C) + static_cast<u32>(GRef<u8>(AD() + 0x23)) * 0x18;
    const u8 st = GRef<u8>(AS() + 0x3F);
    if (st == 1) {
        const s32 r = static_cast<s32>(W(Scene() + 0x165C) + GRef<u8>(AU() + 0x55)) % 100;
        u32 m = 0x002A2548;  // fallback when no entry matches (only reachable with bad data)
        if (r < static_cast<s32>(GRef<u8>(tb + 0x10))) {
            m = W(Setup() + 0x50) + static_cast<u32>(GRef<u8>(tb + 8)) * 0xC;
        } else {
            for (u32 k = 1; k <= 7; ++k) {
                if (r < static_cast<s32>(GRef<u8>(tb + k + 0x10))) {
                    m = W(Setup() + 0x50) + static_cast<u32>(GRef<u8>(tb + k + 8)) * 0xC;
                    break;
                }
            }
        }
        StartAIManeuver(m);
        Inc8(AS() + 0x3F);
    } else if (1 < st) {
        if (st == 2 && (GRef<u8>(AS() + 0x44) & 0x80) != 0) {
            GRef<u8>(AS() + 0x46) = 0;
            Inc8(AS() + 0x3F, -1);
            if (F(tb + 4) < I2F(GRef<s32>(AS() + 0x38)) * F(0x0040E04C)) {
                GRef<u8>(AS() + 0x3E) = 0;
                GRef<u8>(AS() + 0x3C) = static_cast<u8>(GRef<u8>(AS() + 0x3C) & 0xFD);
            }
        }
    } else {
        Or8(AS() + 0x3C, 2);
        if (!(I2F(GRef<s32>(AS() + 0x38)) * F(0x0040E048) <= F(tb))) Inc8(AS() + 0x3F);
    }
    if (GRef<u8>(AU() + 0x69) == 0x12 && (StatusFlags() & 0x200000u) != 0) {
        // The original rotates the matrix at sp+0x10 without initialising it first (no
        // sceVu0UnitMatrix): the result depends on what earlier calls left on the stack. The port
        // uses the guest stack contents the same way; they can differ from the PS2.
        guest::GuestFrame f(0x50);
        const u32 v = f.Address(), m = f.Address(0x10);
        ZeroQ(v);
        GRef<f32>(v + 4) = -2.0f;
        func_00102D28(m, m, AU() + 0x10);
        sceVu0ApplyMatrix(v, m, v);
        sceVu0AddVector(AU(), AU(), v);
    }
}

// EE: 0x0012C8A8
// Behaviour 5 (climb or dive back into the altitude band): point 120 frames ahead at the middle
// altitude; speed mode 1 when above it, else 2 (nose up) or 3.
// fpucheck: 5 adds
void func_0012C8A8() {
    GRef<u8>(AS() + 0x46) = 0x10;
    const u32 s = AS(), u = AU(), d = AD();
    GRef<f32>(s + 0xD4) = fpu::Add(F(d + 0x18), F(d + 0x14)) * 0.5f;
    GRef<f32>(s + 0xD0) = fpu::Add(F(u), fpu::Add(F(u + 0x20), F(u + 0x20)) * 60.0f);
    GRef<f32>(s + 0xD8) = fpu::Add(F(u + 8), fpu::Add(F(u + 0x28), F(u + 0x28)) * 60.0f);
    GRef<u8>(u + 0x62) = 0xFF;
    if (F(AU() + 4) <= F(AS() + 0xD4)) {
        GRef<u8>(AS() + 0xE0) = 1;
        return;
    }
    GRef<u8>(AS() + 0xE0) = 0.0f <= F(AU() + 0x10) ? 2 : 3;
}

// EE: 0x0012C970
// Behaviour 6 (destroyed): crash maneuver 0x20 (or 0x22/0x23/0x20 by frame % 3 when AP bit 0 is
// clear), wingtip vapour off, speed mode 0.
void func_0012C970() {
    if (GRef<s32>(AS() + 0x38) == 0) {
        u8 m = 0x20;
        if ((W(AP()) & 1u) == 0) {
            const s32 r = GRef<s32>(Scene() + 0x165C) % 3;
            m = r == 1 ? 0x22 : (r == 2 ? 0x23 : 0x20);
        }
        GRef<u8>(AS() + 0x46) = m;
    }
    GRef<u16>(AU() + 0x56) = static_cast<u16>(GRef<u16>(AU() + 0x56) & 0xFFFE);
    GRef<u8>(AS() + 0xE0) = 0;
}

// EE: 0x0012CA40
// Behaviour 7 (retreat): fly out of the map (friendly side: south; enemies: to the nearer east or
// west edge, else south); beyond +-294912 the unit is retired (flags 0x31000000 cleared).
// fpucheck: 3 adds (262144 - z, x + 262144, 262144 - x)
void func_0012CA40() {
    GRef<u8>(AS() + 0x46) = 0x10;
    u32 u = AU();
    CopyQ(AS() + 0xD0, u);
    if ((W(u + 0x48) & 3u) == 0) {
        GRef<u32>(AS() + 0xD8) = 0xC8900000u;
    } else {
        const f32 x = F(u);
        const f32 zd = fpu::Sub(262144.0f, F(u + 8));
        if (x < 0.0f) {
            if (zd <= fpu::Add(x, 262144.0f))
                GRef<u32>(AS() + 0xD8) = 0x48900000u;
            else
                GRef<u32>(AS() + 0xD0) = 0xC8900000u;
        } else {
            if (zd <= fpu::Sub(262144.0f, x))
                GRef<u32>(AS() + 0xD8) = 0x48900000u;
            else
                GRef<u32>(AS() + 0xD0) = 0x48900000u;
        }
    }
    GRef<u8>(u + 0x62) = 0xFF;
    u = AU();
    if (F(u) < -294912.0f || F(u + 8) < -294912.0f || 294912.0f < F(u) || 294912.0f < F(u + 8))
        GRef<u32>(u + 0x4C) &= 0xCEFFFFFFu;
    if (GRef<u8>(u + 0x69) == 0x12 && (StatusFlags() & 0x200000u) != 0) func_0012CBD8();
    GRef<u8>(AS() + 0xE0) = 2;
}

// EE: 0x0012CBD8
// Unit 0x12 (mission 7) dodges the nearest player missile / special weapon or its own hit: when
// closer than 5120 the unit is pushed 4 units along that direction and starts maneuver 3 (random
// side by NextRandomFloat > 0.5).
void func_0012CBD8() {
    guest::GuestFrame f(0x60);
    const u32 ang = f.Address(0), v = f.Address(0x10), m = f.Address(0x20);
    f32 d = 262144.0f;
    const u32 u = AU();
    if ((W(u + 0x50) & 0x1000000u) != 0) {
        const u32 p = W(0x004106AC) + static_cast<u32>(GRef<u8>(u + 0x66)) * 0x60 + 0x4000;
        d = func_00104F10(u, p);
        CopyQ(ang, p + 0x10);
    }
    if ((W(AU() + 0x50) & 0x2000000u) != 0) {
        const u32 p = W(0x004106AC) + static_cast<u32>(GRef<u8>(AU() + 0x67)) * 0x80 + 0x4C00;
        const f32 e = func_00104F10(AU(), p);
        if (e < d) {
            CopyQ(ang, p + 0x10);
            d = e;
        }
    }
    if ((W(AU() + 0x50) & 0x10010000u) != 0 && 8192.0f < d) {
        CopyQ(ang, AU() + 0x10);
        d = 0.0f;
    }
    if (!(d < 5120.0f)) return;
    ZeroQ(v);
    GRef<f32>(v + 4) = -4.0f;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, ang);
    sceVu0ApplyMatrix(v, m, v);
    sceVu0AddVector(AU(), AU(), v);
    GRef<u8>(AS() + 0x46) = 3;
    if (GRef<u8>(AS() + 0x45) != 3) {
        GRef<u8>(AS() + 0x48) = 1;
        const f32 r = NextRandomFloat();
        const u32 s = AS();
        GRef<u32>(s + 0x50) = 0.5f < r ? W(0x0040E050) : W(0x0040E054);
        const u32 k = W(0x0040E058);
        GRef<u8>(s + 0xE0) = 2;
        GRef<u32>(s + 0x58) = k;
        GRef<u32>(s + 0x5C) = 0x43960000u;
        GRef<u32>(s + 0x54) = k;
    }
}

// EE: 0x0012CDB0
// Behaviour 8 (scripted): empty; route commands 5/6 drive the manoeuvre.
void func_0012CDB0() {}

// EE: 0x0012CDB8
// Behaviour 9 (wingman formation): far from the player (threat table +8 > C 0x0040E05C): point
// 8192 behind the player; else the formation slot by index % 5 (angle C 0x0040E060 * k wrapped,
// radius 256) around the player, speed mode 5 (player speed, distance).
// fpucheck: 1 add
void func_0012CDB8() {
    const u32 p0 = Unit(0);
    guest::GuestFrame f(0x60);
    const u32 v = f.Address(0), ang = f.Address(0x10), m = f.Address(0x20);
    const u32 idx = static_cast<u32>(GRef<u8>(AU() + 0x55)) * 0xC;
    if (F(0x0040E05C) < F(idx + W(0x00410648) + 8)) {
        GRef<u8>(AS() + 0x46) = 0x10;
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, p0 + 0x10);
        ZeroQ(v);
        GRef<u32>(v + 4) = 0xC6000000u;
        sceVu0ApplyMatrix(v, m, v);
        sceVu0AddVector(AS() + 0xD0, v, p0);
        GRef<u8>(AS() + 0xE0) = 2;
        return;
    }
    GRef<u8>(AS() + 0x46) = 0x10;
    f32 a = fpu::IntToFloat(static_cast<s32>(GRef<u8>(AU() + 0x55) % 5)) * F(0x0040E060);
    if (F(0x0040E064) < a) a = fpu::Sub(a, F(0x0040E068));
    StoreVf0(v);
    GRef<f32>(v) = ee::sinf(a) * 256.0f;
    GRef<f32>(v + 4) = ee::cosf(a) * 256.0f;
    GRef<u32>(ang) = W(p0 + 0x10);
    GRef<u32>(ang + 4) = W(p0 + 0x14);
    // ang.z is not written by the original (sp+0x58: whatever earlier calls left on the stack);
    // the port leaves the guest stack word as it is, which can differ from the PS2.
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, ang);
    sceVu0ApplyMatrix(v, m, v);
    sceVu0AddVector(AS() + 0xD0, p0, v);
    GRef<u8>(AS() + 0xE0) = 5;
    GRef<f32>(AS() + 0xE4) = GetVectorLength(p0 + 0x20);
    GRef<u32>(AS() + 0xE8) = W(static_cast<u32>(GRef<u8>(AU() + 0x55)) * 0xC + W(0x00410648) + 4);
}

// EE: 0x0012CFA8
// Manoeuvre 0: none (empty).
void func_0012CFA8() {}

// EE: 0x0012CFB0
// Maneuver 1 (straight): optional level-out, then +0x50 frames.
void func_0012CFB0() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x47);
    if (st == 1) {
        const s32 t = GRef<s32>(s + 0x4C) + 1;
        GRef<s32>(s + 0x4C) = t;
        if (fpu::FloatToInt(F(s + 0x50)) <= t) Inc8(s + 0x47);
    } else if (st == 0) {
        if (GRef<u8>(s + 0x48) == 1) GRef<f32>(s + 0x78) = -F(AU() + 0x18);
        GRef<u32>(s + 0x4C) = 0;
        Inc8(s + 0x47);
    } else if (st == 2) {
        Or8(s + 0x44, 0x80);
    }
}

// EE: 0x0012D058
// Maneuver 2 (level out, pitch out, hold +0x58 frames).
void func_0012D058() {
    const u32 s = AS(), u = AU();
    switch (GRef<u8>(s + 0x47)) {
        case 0:
            GRef<u32>(s + 0x70) = 0;
            GRef<u32>(s + 0x74) = 0;
            Inc8(s + 0x47);
            GRef<u32>(s + 0x80) = W(s + 0x54);
            GRef<f32>(s + 0x78) = -F(u + 0x18);
            GRef<u32>(s + 0x88) = W(s + 0x50);
            GRef<u32>(s + 0x4C) = 0;
            return;
        case 1:
            if (F(s + 0x78) == 0.0f) Inc8(s + 0x47);
            return;
        case 2:
            Inc8(s + 0x47);
            GRef<f32>(s + 0x70) = -F(u + 0x10);
            return;
        case 3:
            if (F(s + 0x70) == 0.0f) Inc8(s + 0x47);
            return;
        case 4: {
            const s32 t = GRef<s32>(s + 0x4C) + 1;
            GRef<s32>(s + 0x4C) = t;
            if (fpu::FloatToInt(F(s + 0x58)) <= t) Inc8(s + 0x47);
            return;
        }
        case 5: Or8(s + 0x44, 0x80); return;
        default: return;
    }
}

// EE: 0x0012D150
// Maneuver 0x11 (break away from the incoming missile +0x66): roll away from its body-frame
// direction, then pull (pitch toward it).
// fpucheck: 4 static adds
void func_0012D150() {
    guest::GuestFrame f(0x50);
    const u32 d = f.Address(0), m = f.Address(0x10);
    sceVu0SubVector(d, W(0x004106AC) + static_cast<u32>(GRef<u8>(AU() + 0x66)) * 0x60 + 0x4000, AU());
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, AU() + 0x10);
    sceVu0InversMatrix(m, m);
    sceVu0ApplyMatrix(d, m, d);
    f32 a = ee::atan2f(F(d), -F(d + 4));
    u32 s = AS();
    const u8 st = GRef<u8>(s + 0x47);
    GRef<f32>(s + 0x88) = 2.0f;
    GRef<f32>(s + 0x80) = 2.0f;
    if (st == 1) {
        if (F(0x0040E074) < F(s + 0x78) || F(s + 0x78) < F(0x0040E078)) GRef<u8>(s + 0x47) = 2;
    } else if (st == 0) {
        GRef<u32>(s + 0x70) = 0;
        GRef<u32>(s + 0x74) = 0;
        if (F(s + 0x70) < a)
            a = fpu::Sub(a, F(0x0040E06C));
        else
            a = fpu::Add(a, F(0x0040E070));
        GRef<f32>(s + 0x78) = a;
        Inc8(s + 0x47);
    } else if (st == 2) {
        const f32 h = GetVectorLength(d);
        const f32 p = ee::atan2f(-F(d + 4), h);
        s = AS();
        if (F(d + 8) < 0.0f)
            GRef<f32>(s + 0x70) = fpu::Sub(F(0x0040E07C), p);
        else
            GRef<f32>(s + 0x70) = fpu::Add(p, F(0x0040E080));
    } else {
        GRef<u8>(s + 0x47) = 0;
    }
    GRef<u8>(AS() + 0xE0) = 2;
}

// EE: 0x0012D320
// Maneuver 0x12 (scripted glide toward +0xD0 for command 6): heading turned by at most
// +0x1C degrees * C per frame, velocity set directly (speed +0x50 * 5 / C 0x0040E084 * C).
// fpucheck: 6 static adds
void func_0012D320() {
    if (GRef<u8>(AS() + 0x1B) != 6) return;
    guest::GuestFrame f(0x10);
    const u32 d = f.Address();
    sceVu0SubVector(d, AS() + 0xD0, AU());
    const f32 len = GetVectorLength(d);
    if (len == 0.0f) return;
    const f32 sp = F(AS() + 0x50) * 5.0f / F(0x0040E084) * F(0x0040E088);
    if (!(sp <= len)) return;
    f32 k = F(0x0040E088);
    const f32 h = func_00104FA8(d);
    const f32 hs = sp * h / len;
    u32 u = AU();
    if (h != 0.0f) {
        f32 a = ee::atan2f(F(d), F(d + 8));
        u = AU();
        const f32 y = F(u + 0x14);
        a = fpu::Sub(a, y);
        if (F(0x0040E08C) < a) a = fpu::Sub(a, F(0x0040E090));
        if (a < F(0x0040E094)) a = fpu::Add(a, F(0x0040E098));
        k = fpu::IntToFloat(GRef<u8>(AS() + 0x1C)) * F(0x0040E08C) / 180.0f * k;
        if (k < a)
            GRef<f32>(u + 0x14) = fpu::Add(y, k);
        else if (a < -k)
            GRef<f32>(u + 0x14) = fpu::Sub(y, k);
        else
            GRef<f32>(u + 0x14) = fpu::Add(y, a);
    }
    GRef<f32>(u + 0x20) = hs * ee::sinf(F(u + 0x14));
    GRef<f32>(AU() + 0x24) = sp * F(d + 4) / len;
    GRef<f32>(AU() + 0x28) = hs * ee::cosf(F(AU() + 0x14));
}

// EE: 0x0012D4F8
// Maneuver 3 (roll to +0x50 and turn; variant 2: barrel roll on the yaw rate AP+0x18 for +0x5C
// frames).
// fpucheck: 4 static adds
void func_0012D4F8() {
    const u32 s = AS(), u = AU();
    if (GRef<u8>(s + 0x48) == 2) {
        const u8 st = GRef<u8>(s + 0x47);
        if (st == 0) {
            GRef<u32>(s + 0x70) = 0;
            GRef<u32>(s + 0x74) = 0;
            SetRollTarget(F(s + 0x50), 0x0040E09C, 0x0040E0A0, 0x0040E0A4, 0x0040E0A8);
            Inc8(s + 0x47);
            GRef<u32>(s + 0x88) = W(s + 0x54);
            GRef<u32>(s + 0x80) = W(s + 0x58);
            GRef<u32>(s + 0x4C) = 0;
            return;
        }
        if (st != 1) {
            GRef<u8>(s + 0x47) = 0;
            return;
        }
        const f32 z = F(u + 0x18);
        f32 r = z / F(0x0040E0AC);
        if (1.0f < r) r = 1.0f;
        if (r < -1.0f) r = -1.0f;
        const f32 y = F(u + 0x14);
        const f32 g = F(AP() + 0x18) * F(0x0040E0B0);
        const s32 t = GRef<s32>(s + 0x4C) + 1;
        GRef<s32>(s + 0x4C) = t;
        GRef<f32>(u + 0x14) = fpu::Add(y, g * r);
        if (fpu::FloatToInt(F(s + 0x5C)) <= t) {
            Or8(s + 0x44, 0x80);
            GRef<f32>(s + 0x78) = -z;
        }
        return;
    }
    const u8 st = GRef<u8>(s + 0x47);
    if (st == 1) {
        if (GRef<u8>(s + 0x48) == 1) GRef<u8>(s + 0x47) = 2;
        if (F(s + 0x78) == 0.0f) Inc8(s + 0x47);
    } else if (st == 0) {
        GRef<u32>(s + 0x70) = 0;
        GRef<u32>(s + 0x74) = 0;
        SetRollTarget(F(s + 0x50), 0x0040E09C, 0x0040E0A0, 0x0040E0A4, 0x0040E0A8);
        Inc8(s + 0x47);
        GRef<u32>(s + 0x88) = W(s + 0x54);
        GRef<u32>(s + 0x80) = W(s + 0x58);
        GRef<u32>(s + 0x4C) = 0;
    } else if (st == 2) {
        const f32 t5 = F(s + 0x5C);
        const s32 t = GRef<s32>(s + 0x4C) + 1;
        GRef<s32>(s + 0x4C) = t;
        GRef<u32>(s + 0x70) = W(0x0040E0B4);
        if (fpu::FloatToInt(t5) <= t) {
            GRef<u32>(s + 0x70) = 0;
            Or8(s + 0x44, 0x80);
        }
    } else {
        GRef<u8>(s + 0x47) = 0;
    }
}

// EE: 0x0012D6F8
// Maneuver 4 (roll to +0x50, then pitch to C 0x0040E0B8 until level).
// fpucheck: 2 adds
void func_0012D6F8() {
    const u32 s = AS(), u = AU();
    const u8 st = GRef<u8>(s + 0x47);
    if (st == 1) {
        if (F(s + 0x78) == 0.0f) {
            const f32 v = fpu::Sub(F(0x0040E0B8), F(u + 0x10));
            Inc8(s + 0x47);
            GRef<f32>(s + 0x70) = v;
        }
    } else if (st == 0) {
        GRef<u32>(s + 0x70) = 0;
        GRef<u32>(s + 0x74) = 0;
        Inc8(s + 0x47);
        GRef<f32>(s + 0x78) = fpu::Sub(F(s + 0x50), F(u + 0x18));
        GRef<u32>(s + 0x80) = W(s + 0x58);
        GRef<u32>(s + 0x88) = W(s + 0x54);
    } else if (st == 2) {
        if (F(s + 0x70) <= 0.0f) Or8(s + 0x44, 0x80);
    } else {
        GRef<u8>(s + 0x47) = 0;
    }
}

// EE: 0x0012D7D0
// Maneuver 5 (loop / split-S by variant +0x48: pitch C 0x0040E0BC with roll to C 0x0040E0C0 or
// the inverse, until the pitch passes C 0x0040E0C4 / C 0x0040E0C8, then level).
// fpucheck: 2 adds
void func_0012D7D0() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x47);
    if (st == 1) {
        const f32 v = F(s + 0x78);
        GRef<f32>(s + 0x70) = v;
        const u8 var = GRef<u8>(s + 0x48);
        if ((var == 0 && F(0x0040E0C4) < v) || (var == 1 && v < F(0x0040E0C8))) {
            Inc8(s + 0x47);
            GRef<f32>(s + 0x78) = -F(AU() + 0x18);
        }
        return;
    }
    if (st == 0) {
        const u8 var = GRef<u8>(s + 0x48);
        GRef<u32>(s + 0x70) = W(0x0040E0BC);
        GRef<u32>(s + 0x74) = 0;
        GRef<f32>(s + 0x78) = var == 0 ? fpu::Sub(F(0x0040E0C0), F(AU() + 0x18)) : fpu::Sub(F(0x0040E0BC), F(AU() + 0x18));
        Inc8(s + 0x47);
        GRef<f32>(s + 0x88) = 40.0f / F(s + 0x50);
        return;
    }
    if (st == 2) {
        const f32 v = F(s + 0x78);
        GRef<f32>(s + 0x70) = v;
        const u8 var = GRef<u8>(s + 0x48);
        if ((var == 0 && !(v < 0.0f)) || (var == 1 && !(0.0f < v))) {
            Or8(s + 0x44, 0x80);
            GRef<u32>(AS() + 0x70) = 0;
        }
        return;
    }
    GRef<u8>(s + 0x47) = 0;
}

// EE: 0x0012D920
// Maneuver 7 (roll to +0x50, pitch C 0x0040E0DC, level out, pitch out).
// fpucheck: 3 adds
void func_0012D920() {
    const u32 s = AS();
    switch (GRef<u8>(s + 0x47)) {
        case 0:
            GRef<u32>(s + 0x70) = 0;
            GRef<u32>(s + 0x74) = 0;
            SetRollTarget(F(s + 0x50), 0x0040E0CC, 0x0040E0D0, 0x0040E0D4, 0x0040E0D8);
            Inc8(s + 0x47);
            GRef<u32>(s + 0x88) = W(s + 0x54);
            GRef<u32>(s + 0x80) = W(s + 0x58);
            return;
        case 1:
            if (F(s + 0x78) == 0.0f) {
                Inc8(s + 0x47);
                GRef<u32>(s + 0x70) = W(0x0040E0DC);
            }
            return;
        case 2:
            if (F(s + 0x70) == 0.0f) {
                Inc8(s + 0x47);
                GRef<f32>(s + 0x78) = -F(AU() + 0x18);
            }
            return;
        case 3:
            if (F(s + 0x78) == 0.0f) {
                Inc8(s + 0x47);
                GRef<f32>(s + 0x70) = -F(AU() + 0x10);
            }
            return;
        case 4:
            if (F(s + 0x70) == 0.0f) Or8(s + 0x44, 0x80);
            return;
        default: GRef<u8>(s + 0x47) = 0; return;
    }
}

// EE: 0x0012DA90
// Maneuver 6 (as 7 with a level start: when pitched beyond C 0x0040E0E0/0x0040E0E4 it first
// steers to the point 300 frames ahead at its altitude).
// fpucheck: 3 adds
void func_0012DA90() {
    const u32 s = AS();
    switch (GRef<u8>(s + 0x47)) {
        case 0: {
            const f32 x = F(AU() + 0x10);
            if (F(0x0040E0E0) <= x || x <= F(0x0040E0E4)) {
                sceVu0ScaleVector(AS() + 0xD0, AU() + 0x20, 300.0f);
                sceVu0AddVector(AS() + 0xD0, AS() + 0xD0, AU());
                GRef<u32>(AS() + 0xD4) = W(AU() + 4);
                SteerAIToPoint();
                return;
            }
            Inc8(s + 0x47);
            return;
        }
        case 1:
            GRef<u32>(s + 0x70) = 0;
            GRef<u32>(s + 0x74) = 0;
            SetRollTarget(F(s + 0x50), 0x0040E0E8, 0x0040E0EC, 0x0040E0F0, 0x0040E0F4);
            Inc8(s + 0x47);
            GRef<u32>(s + 0x88) = W(s + 0x54);
            GRef<u32>(s + 0x80) = W(s + 0x58);
            return;
        case 2:
            if (F(s + 0x78) == 0.0f) {
                Inc8(s + 0x47);
                GRef<u32>(s + 0x70) = W(0x0040E0F8);
            }
            return;
        case 3:
            if (F(s + 0x70) == 0.0f) {
                Inc8(s + 0x47);
                GRef<f32>(s + 0x78) = -F(AU() + 0x18);
            }
            return;
        case 4:
            if (F(s + 0x78) == 0.0f) {
                Inc8(s + 0x47);
                GRef<f32>(s + 0x70) = -F(AU() + 0x10);
            }
            return;
        case 5:
            if (F(s + 0x70) == 0.0f) Or8(s + 0x44, 0x80);
            return;
        default: GRef<u8>(s + 0x47) = 0; return;
    }
}

// EE: 0x0012DC90
// Maneuver 8 (pitch pulse: +0x70 = C 0x0040E0FC mirrored to the roll, then C 0x0040E104).
void func_0012DC90() {
    const u32 s = AS();
    const u8 st = GRef<u8>(s + 0x47);
    if (st == 1) {
        const f32 v = F(s + 0x70);
        GRef<f32>(s + 0x78) = v;
        if (v <= 0.0f) {
            Inc8(s + 0x47);
            GRef<u32>(s + 0x70) = W(0x0040E104);
        }
    } else if (st == 0) {
        Inc8(s + 0x47);
        GRef<u32>(s + 0x70) = W(0x0040E0FC);
        GRef<u32>(s + 0x88) = W(0x0040E100);
        GRef<u32>(s + 0x74) = 0;
        GRef<u32>(s + 0x78) = 0;
    } else if (st == 2) {
        const f32 v = F(s + 0x70);
        GRef<f32>(s + 0x78) = -v;
        if (v <= 0.0f) Or8(s + 0x44, 0x80);
    } else {
        GRef<u8>(s + 0x47) = 0;
    }
}

// EE: 0x0012DD50
// Maneuver 9 (high-g turn with speed bleed, variants 1/2 for the exit; uses the local thrust
// vector (0, 0, C 0x0040E108)).
// fpucheck: 7 static adds
void func_0012DD50() {
    guest::GuestFrame f(0x50);
    const u32 v = f.Address(0), m = f.Address(0x10);
    ZeroQ(v);
    GRef<u32>(v + 8) = W(0x0040E108);
    u32 s = AS();
    switch (GRef<u8>(s + 0x47)) {
        case 0: {
            GRef<u8>(s + 0xE0) = 3;
            GRef<u32>(s + 0x70) = 0;
            GRef<u32>(s + 0x74) = 0;
            const f32 z = F(AU() + 0x18);
            Inc8(s + 0x47);
            GRef<f32>(s + 0x78) = -z;
            return;
        }
        case 1:
            if (F(s + 0x78) == 0.0f) Inc8(s + 0x47);
            return;
        case 2: {
            const f32 x = F(AU() + 0x10);
            Inc8(s + 0x47);
            GRef<f32>(s + 0x70) = -x;
            return;
        }
        case 3:
            if (F(s + 0x70) != 0.0f) return;
            if (GRef<u8>(s + 0x48) != 1)
                Inc8(s + 0x47, 3);
            else
                Inc8(s + 0x47);
            return;
        case 4:
            SetRollTarget(F(s + 0x54), 0x0040E10C, 0x0040E110, 0x0040E114, 0x0040E118);
            Inc8(s + 0x47);
            return;
        case 5:
            if (F(0x0040E11C) <= F(s + 0x78)) return;
            if (F(0x0040E120) < F(s + 0x78)) Inc8(s + 0x47);
            return;
        case 6: {
            GRef<u8>(s + 0xE0) = 0;
            const u32 r = W(s + 0x50);
            GRef<u32>(s + 0x80) = 0x40A00000u;
            Inc8(s + 0x47);
            GRef<u32>(s + 0x70) = r;
            return;
        }
        case 7: {
            const f32 sp = GetVectorLength(AU() + 0x20);
            const f32 q = sp / F(0x0040E124);
            sceVu0ScaleVector(AU() + 0x20, AU() + 0x20, fpu::Add(fpu::Sub(q, 1.0f) * F(0x0040E128), 1.0f) / q);
            s = AS();
            if (F(s + 0x70) == 0.0f) {
                GRef<u32>(s + 0x4C) = 0;
                Inc8(s + 0x47);
            }
            return;
        }
        case 8: {
            GetVectorLength(AU() + 0x20);
            s = AS();
            const s32 t = GRef<s32>(s + 0x4C) + 1;
            GRef<s32>(s + 0x4C) = t;
            if (fpu::FloatToInt(F(s + 0x58)) <= t) {
                const u8 var = GRef<u8>(s + 0x48);
                if (var == 1) {
                    const f32 z = F(AU() + 0x18);
                    Inc8(s + 0x47);
                    GRef<f32>(s + 0x88) = 1.0f;
                    GRef<f32>(s + 0x78) = -z;
                } else if (var == 2) {
                    const f32 p = fpu::Sub(F(0x0040E12C), F(s + 0x50));
                    Inc8(s + 0x47, 2);
                    GRef<f32>(s + 0x70) = p;
                    GRef<u32>(s + 0x80) = 0x41000000u;
                } else {
                    Inc8(s + 0x47, 2);
                    GRef<f32>(s + 0x70) = -F(s + 0x50);
                    GRef<u32>(s + 0x80) = 0x41000000u;
                }
            }
            return;
        }
        case 9: {
            sceVu0UnitMatrix(m);
            func_00102D28(m, m, AU() + 0x10);
            sceVu0ApplyMatrix(v, m, v);
            sceVu0SubVector(v, v, AU() + 0x20);
            sceVu0ScaleVector(v, v, F(0x0040E130));
            sceVu0AddVector(AU() + 0x20, v, AU() + 0x20);
            const u32 u = AU();
            GRef<u32>(u + 0x2C) = 0;
            s = AS();
            GRef<f32>(u + 0x24) = fpu::Add(F(u + 0x24), F(0x0040E134));
            if (F(s + 0x78) == F(u + 0x2C)) {
                Inc8(s + 0x47);
                GRef<f32>(s + 0x70) = -F(u + 0x10);
                GRef<u32>(s + 0x80) = 0x41000000u;
            }
            return;
        }
        case 10: {
            sceVu0UnitMatrix(m);
            func_00102D28(m, m, AU() + 0x10);
            sceVu0ApplyMatrix(v, m, v);
            sceVu0SubVector(v, v, AU() + 0x20);
            sceVu0ScaleVector(v, v, F(0x0040E138));
            sceVu0AddVector(AU() + 0x20, v, AU() + 0x20);
            s = AS();
            const u32 u = AU();
            GRef<u32>(u + 0x2C) = 0;
            if (F(s + 0x70) == F(u + 0x2C)) {
                GRef<u8>(s + 0xE0) = 1;
                Or8(AS() + 0x44, 0x80);
            }
            return;
        }
        default: GRef<u8>(s + 0x47) = 0; return;
    }
}

// EE: 0x0012E1D0
// Maneuver 0x10: follow steering (AP bit 0) or point steering.
void func_0012E1D0() {
    if ((W(AP()) & 1u) != 0)
        func_0012E7F0();
    else
        SteerAIToPoint();
}

// EE: 0x0012E1F8
// Maneuver 0x20 (crash dive): drag, gravity C 0x0040E140, nose follows the velocity with a random
// lag (NextRandomFloat * C 0x0040E144), smoke 0x001E9518 every 50 units of height, ground impact
// explosion 0x001E94F8 (state +0x47 + 1, flag 0x80000000).
// fpucheck: 3 adds
void func_0012E1F8() {
    u32 u = AU();
    if (GRef<u8>(AS() + 0x47) != 0) return;
    const f32 k = F(0x0040E13C);
    const f32 vz = F(u + 0x28) * k;
    const f32 vy = fpu::Add(F(u + 0x24), F(0x0040E140));
    GRef<f32>(u + 0x20) = F(u + 0x20) * k;
    GRef<f32>(u + 0x28) = vz;
    GRef<f32>(u + 0x24) = vy;
    f32 s = GetVectorLength(u + 0x20);
    s = -F(AU() + 0x24) / s;
    if (1.0f < s) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    const f32 a = ee::asinf(s);
    const f32 r = NextRandomFloat();
    u = AU();
    const f32 y = F(u + 4);
    GRef<u16>(u + 0x56) = static_cast<u16>(GRef<u16>(u + 0x56) | 2);
    GRef<f32>(u + 0x10) = fpu::Add(fpu::Sub(a, F(u + 0x10)) * r * F(0x0040E144), F(u + 0x10));
    if (fpu::FloatToInt(y) % 0x32 == 0) {
        func_001E9518(u, u + 0x20, u + 0x30, 0x80);
        func_00170C40(1, 0xFF, 0x80, AU(), AU() + 0x20, AU() + 0x30);
    }
    const f32 h = func_0013E2B0(Terrain(), AU());
    if (h <= F(AU() + 4)) {
        func_001E94F8(AU(), AU() + 0x20, 0xB5);
        func_00170B60(2, 0xFF, 0xB5, AU(), AU() + 0x20);
        const u32 w = AU();
        GRef<f32>(w + 4) = h;
        GRef<u32>(w + 0x2C) = 0;
        GRef<u32>(w + 0x4C) |= 0x80000000u;
        GRef<u32>(w + 0x20) = 0;
        GRef<u32>(w + 0x24) = 0;
        GRef<u32>(w + 0x28) = 0;
        Inc8(AS() + 0x47);
    }
}

// EE: 0x0012E3C8
// Maneuver 0x21 (tumble): while above y 0, spin by (0.005, 0.005, 0.005) and fall.
// fpucheck: 1 add
void func_0012E3C8() {
    const u32 u = AU();
    guest::GuestFrame f(0x10);
    const u32 sp = f.Address();
    GRef<u32>(sp) = 0x3BA3D70Au;
    GRef<u32>(sp + 4) = 0x3BA3D70Au;
    GRef<u32>(sp + 8) = 0x3BA3D70Au;
    GRef<u32>(sp + 12) = 0x3F800000u;
    if (F(u + 4) < 0.0f) {
        func_00102EF0(u + 0x10, u + 0x10, sp);
        GRef<f32>(u + 0x24) = fpu::Add(F(u + 0x24), F(0x0040E148));
    } else {
        GRef<u32>(u + 0x2C) = 0;
        GRef<u32>(u + 0x20) = 0;
        GRef<u32>(u + 0x24) = 0;
        GRef<u32>(u + 0x28) = 0;
    }
}

// EE: 0x0012E448
// Maneuver 0x22 (falling wreck): gravity, ground impact; mid-air explosion after
// (index & 63) * 2 + 90 frames when no threat is near.
// fpucheck: 1 add
void func_0012E448() {
    const u32 u = AU();
    if (GRef<u8>(AS() + 0x47) != 0) return;
    const f32 vy = fpu::Add(F(u + 0x24), F(0x0040E14C));
    GRef<u16>(u + 0x56) = static_cast<u16>(GRef<u16>(u + 0x56) | 2);
    GRef<f32>(u + 0x24) = vy;
    const f32 h = func_0013E2B0(Terrain(), u);
    if (h <= F(AU() + 4)) {
        func_001E94F8(AU(), AU() + 0x20, 0xB5);
        func_00170B60(2, 0xFF, 0xB5, AU(), AU() + 0x20);
        const u32 w = AU();
        GRef<f32>(w + 4) = h;
        GRef<u32>(w + 0x2C) = 0;
        GRef<u32>(w + 0x4C) |= 0x80000000u;
        GRef<u32>(w + 0x20) = 0;
        GRef<u32>(w + 0x24) = 0;
        GRef<u32>(w + 0x28) = 0;
        Inc8(AS() + 0x47);
    }
    if (static_cast<s32>((GRef<u8>(AU() + 0x55) & 0x3Fu) * 2 + 0x5A) <= GRef<s32>(AS() + 0x40) &&
        4096.0f < F(static_cast<u32>(GRef<u8>(AU() + 0x55)) * 0xC + W(0x00410648) + 4)) {
        func_001E9518(AU(), AU() + 0x20, AU() + 0x30, 0xC2);
        func_00170C40(1, 0xFF, 0xC2, AU(), AU() + 0x20, AU() + 0x30);
        GRef<u32>(AU() + 0x4C) |= 0x80000000u;
        Inc8(AS() + 0x47);
    }
}

// EE: 0x0012E5C0
// Maneuver 0x23 (explode at once).
void func_0012E5C0() {
    func_001E9518(AU(), AU() + 0x20, AU() + 0x30, 0xC2);
    func_00170C40(1, 0xFF, 0xC2, AU(), AU() + 0x20, AU() + 0x30);
    GRef<u32>(AU() + 0x4C) |= 0x80000000u;
}

// EE: 0x0012E618
// Point steering: the target +0xD0 in the body frame; within C 0x0040E150 of straight ahead only
// level the wings, else roll toward it (with the wrap C 0x0040E158..0x0040E16C when nearly
// behind) and pull (+0x70 pitch, +0x74 yaw by atan2) unless the roll is still beyond
// C 0x0040E170 / 0x0040E174.
// fpucheck: 4 static adds (sqrt sum x2, wrap)
void SteerAIToPoint() {
    guest::GuestFrame f(0x50);
    const u32 d = f.Address(0), m = f.Address(0x10);
    sceVu0SubVector(d, AS() + 0xD0, AU());
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, AU() + 0x10);
    sceVu0InversMatrix(m, m);
    sceVu0ApplyMatrix(d, m, d);
    const f32 dx = F(d), dy = F(d + 4), dz = F(d + 8);
    const f32 off = ee::atan2f(fpu::Sqrt(fpu::Add(dx * dx, dy * dy)), dz);
    if (!(off < F(0x0040E150))) {
        const f32 roll = ee::atan2f(dx, -dy);
        const u32 s = AS();
        GRef<f32>(s + 0x78) = roll;
        if (off < F(0x0040E154) && roll < F(0x0040E158) && F(0x0040E15C) < roll) {
            f32 r = roll;
            if (F(0x0040E160) < r) {
                r = fpu::Sub(r, F(0x0040E164));
                GRef<f32>(s + 0x78) = r;
            }
            if (r < F(0x0040E168)) GRef<f32>(s + 0x78) = fpu::Add(r, F(0x0040E16C));
        }
        const f32 r = F(s + 0x78);
        if (F(0x0040E170) <= r || r <= F(0x0040E174)) {
            GRef<u32>(s + 0x70) = 0;
            return;
        }
    } else {
        GRef<f32>(AS() + 0x78) = fpu::Sub(0.0f, F(AU() + 0x18));
    }
    GRef<f32>(AS() + 0x70) = ee::atan2f(-F(d + 4), F(d + 8));
    GRef<f32>(AS() + 0x74) = ee::atan2f(F(d), F(d + 8));
}

// EE: 0x0012E7F0
// Follow steering (AP bit 0, the slow aircraft): heading toward +0xD0 with direct yaw/pitch/
// bank changes (rates AP+0x18/+0x1C/+0x2C with C 0x0040E178..0x0040E1B8), then the position is
// nudged by +-1 per axis toward the point.
// fpucheck: 23 static adds
void func_0012E7F0() {
    guest::GuestFrame f(0x10);
    const u32 d = f.Address();
    sceVu0SubVector(d, AS() + 0xD0, AU());
    const f32 len = GetVectorLength(d);
    f32 yaw = ee::atan2f(F(d), F(d + 8));
    u32 u = AU();
    const u32 ap = AP();
    yaw = fpu::Sub(yaw, F(u + 0x14));
    if (F(0x0040E178) < yaw) yaw = fpu::Sub(yaw, F(0x0040E17C));
    if (yaw < F(0x0040E180)) yaw = fpu::Add(yaw, F(0x0040E184));
    const f32 dy = F(d + 4);
    auto pitch_to = [&](u32 hi, u32 lo) {
        GRef<f32>(AS() + 0x78) = -F(AU() + 0x18);
        const f32 a = ee::asinf(-F(d + 4) / len);
        const u32 w = AU();
        f32 p;
        if (F(hi) < a)
            p = fpu::Sub(F(hi), F(w + 0x10));
        else if (a < F(lo))
            p = fpu::Sub(F(lo), F(w + 0x10));
        else
            p = fpu::Sub(a, F(w + 0x10));
        GRef<f32>(AS() + 0x70) = p;
    };
    auto yaw_step = [&](u32 kpos) {
        const u32 w = AU();
        if (0.0f < yaw)
            GRef<f32>(w + 0x14) = fpu::Add(F(w + 0x14), F(AP() + 0x2C) * F(kpos));
        else if (yaw < 0.0f)
            GRef<f32>(w + 0x14) = fpu::Sub(F(w + 0x14), F(AP() + 0x2C) * F(0x0040E1B8));
    };
    auto bank = [&](u32 target, u32 kz, u32 k, bool add) {
        const u32 w = AU();
        GRef<f32>(AS() + 0x78) = fpu::Sub(F(target), F(w + 0x18));
        const f32 q = F(w + 0x18) / F(kz);
        const f32 step = F(ap + 0x18) * F(k) * q;
        const f32 x = F(w + 0x10);
        GRef<f32>(w + 0x14) = add ? fpu::Add(F(w + 0x14), step) : fpu::Sub(F(w + 0x14), step);
        if (0.0f < x)
            GRef<f32>(w + 0x10) = fpu::Add(x, F(ap + 0x1C) * F(k));
        else if (x < 0.0f)
            GRef<f32>(w + 0x10) = fpu::Add(x, F(ap + 0x18) * F(k));
    };
    if (2048.0f < dy || dy < -2048.0f) {
        pitch_to(0x0040E188, 0x0040E18C);
        yaw_step(0x0040E190);
    } else if (F(0x0040E194) < yaw) {
        bank(0x0040E198, 0x0040E198, 0x0040E19C, true);
    } else if (yaw < F(0x0040E1A0)) {
        bank(0x0040E1A4, 0x0040E1A4, 0x0040E1A8, false);
    } else {
        pitch_to(0x0040E1AC, 0x0040E1B0);
        yaw_step(0x0040E1B4);
    }
    u = AU();
    const f32 ex = F(d), ey = F(d + 4), ez = F(d + 8);
    if (1.0f < ex)
        GRef<f32>(u) = fpu::Add(F(u), 1.0f);
    else if (ex < -1.0f)
        GRef<f32>(u) = fpu::Sub(F(u), 1.0f);
    if (1.0f < ey)
        GRef<f32>(u + 4) = fpu::Add(F(u + 4), 1.0f);
    else if (ey < -1.0f)
        GRef<f32>(u + 4) = fpu::Sub(F(u + 4), 1.0f);
    if (1.0f < ez)
        GRef<f32>(u + 8) = fpu::Add(F(u + 8), 1.0f);
    else if (ez < -1.0f)
        GRef<f32>(u + 8) = fpu::Sub(F(u + 8), 1.0f);
}

// EE: 0x0012EC08
// Rotation rates +0x60..+0x68 toward the remaining angles +0x70..+0x78 with acceleration limits
// (AP+0x20/+0x30/+0x28 * C^2 * per-maneuver scale +0x80..+0x88) and rate caps (AP+0x18/0x1C,
// +0x2C, +0x24); the pitch cap is reduced at difficulty 0-2 while the player keeps the unit in
// sight (+0xF0 frames / 600, 900, 1200, * C 0x0040E1C0); capped rates set +0x56 bit 0 (vapour);
// then the remaining angles are reduced by the rates and the Euler angles integrated.
// fpucheck: 14 static adds (RotAxis x3 + the difficulty term)
void UpdateAIRotation() {
    const u32 u = AU();
    GRef<u16>(u + 0x56) = static_cast<u16>(GRef<u16>(u + 0x56) & 0xFFFE);
    const u32 ap = AP(), s = AS();
    f32 g = 1.0f;
    if (MissionKind() == 0 && GRef<u8>(u + 0x69) != 0x12 && W(s + 0xF0) != 0 && Game(0x28) < 3) {
        const u8 diff = Game(0x28);
        const f32 lim = diff == 2 ? 1200.0f : (diff == 1 ? 900.0f : 600.0f);
        f32 r = fpu::UnsignedToFloat(W(s + 0xF0)) / lim;
        if (1.0f < r) r = 1.0f;
        g = fpu::Sub(1.0f, r * F(0x0040E1C0));
    }
    const f32 kx = F(0x0040E1C4), ky = F(0x0040E1D0), kz = F(0x0040E1DC);
    RotAxis(F(s + 0x70), s + 0x60, F(ap + 0x20) * kx * kx * F(s + 0x80), F(ap + 0x18) * F(0x0040E1C8) * F(s + 0x80) * g,
            F(ap + 0x1C) * F(0x0040E1CC) * F(s + 0x80), true, false);
    RotAxis(F(s + 0x74), s + 0x64, F(ap + 0x30) * ky * ky * F(s + 0x84), F(ap + 0x2C) * F(0x0040E1D4) * F(s + 0x84),
            -F(ap + 0x2C) * F(0x0040E1D8) * F(s + 0x84), false, false);
    RotAxis(F(s + 0x78), s + 0x68, F(ap + 0x28) * kz * kz * F(s + 0x88), F(ap + 0x24) * F(0x0040E1E0) * F(s + 0x88),
            -F(ap + 0x24) * F(0x0040E1E4) * F(s + 0x88), true, true);
    sceVu0SubVector(s + 0x70, s + 0x70, s + 0x60);
    func_00102EF0(AU() + 0x10, AU() + 0x10, AS() + 0x60);
}

// EE: 0x0012F120
// Speed by mode +0xE0 (0 none, 1 cruise AP+0xC, 2 max AP+4, 3 min AP+8, 4 +0xE4, 5 follow +0xE4
// + distance +0xE8 / 4096 * C), clamped, applied as acceleration AP+0x10 along the body z axis;
// side velocities damped by C 0x0040E1FC.
// fpucheck: 3 static adds
void UpdateAISpeed() {
    const u32 s = AS(), ap = AP();
    f32 v;
    switch (GRef<u8>(s + 0xE0)) {
        case 0: return;
        case 2: v = F(ap + 4) * F(0x0040E1E8); break;
        case 3: v = F(ap + 8) * F(0x0040E1EC); break;
        case 4: v = F(s + 0xE4); break;
        case 5: v = fpu::Add(F(s + 0xE4), F(s + 0xE8) * 0.00024414062f * F(0x0040E1F0)); break;
        default: v = F(ap + 0xC) * F(0x0040E1F4); break;
    }
    const f32 k = F(0x0040E1F8);
    const f32 hi = F(ap + 4) * k;
    if (hi < v) v = hi;
    const f32 lo = F(ap + 8) * k;
    if (v < lo) v = lo;
    guest::GuestFrame f(0x50);
    const u32 lv = f.Address(0), m = f.Address(0x10);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, AU() + 0x10);
    sceVu0InversMatrix(m, m);
    sceVu0ApplyMatrix(lv, m, AU() + 0x20);
    GRef<f32>(lv) = F(lv) * F(0x0040E1FC);
    GRef<f32>(lv + 4) = F(lv + 4) * F(0x0040E1FC);
    const f32 z = F(lv + 8);
    if (z < v)
        GRef<f32>(lv + 8) = fpu::Add(z, F(ap + 0x10) * k * k);
    else if (v < z)
        GRef<f32>(lv + 8) = fpu::Sub(z, F(ap + 0x10) * k * k);
    GRef<u32>(lv + 0xC) = 0;
    func_001030F0(AU() + 0x20, AU() + 0x10, lv);
}

// EE: 0x0012F2D0
// The two weapon slots of the AI unit (+4 kind: 1 gun, 2 missile, 5 bomb; +8 definition, +0xF8
// burst counter, +0x100 reload counter): gun bursts at the target inside range/cone with random
// spread (2 NextRandomFloat reads per shot), missile lock (radio warnings 2/3 to the targeted
// player, AI notifications) and launch, bomb release when the ballistic impact point is within
// the target radius of one of two ground points.
// fpucheck: 8 static adds
void UpdateAIWeapons() {
    for (u32 k = 0; k < 2; ++k) {
        const u32 s = AS();
        const u8 kind = GRef<u8>(s + 4 + k);
        const u32 reload = s + 0x100 + 4 * k;
        const u32 def = W(s + 8 + 4 * k);
        if (kind == 1) {
            if ((W(AU() + 0x4C) & 0x1000u) == 0 && Game(0x27) != 0x11) continue;
            const s32 r = GRef<s32>(reload);
            if (r < GRef<s16>(def + 6)) {
                GRef<s32>(reload) = r + 1;
                continue;
            }
            if (GRef<u8>(AU() + 0x62) == 0xFF) continue;
            const u32 t = Unit(GRef<u8>(AU() + 0x62));
            if (!(F(s + 0xA4) < F(def + 8)) || !(ee::cosf(F(def + 0xC)) < F(AS() + 0xA8))) {
                GRef<u32>(AS() + 0xF8 + 4 * k) = 0;
                continue;
            }
            const u32 fc = W(Scene() + 0x165C) + GRef<u8>(AU() + 0x55);
            if (fc % static_cast<u32>(static_cast<s32>(GRef<s8>(def + 1))) == 0) {
                guest::GuestFrame f(0x10);
                const u32 a = f.Address();
                CopyQ(a, AU() + 0x10);
                const f32 r1 = fpu::Sub(NextRandomFloat(), 0.5f);
                GRef<f32>(a) = fpu::Add(F(a), r1 * F(def + 0x10));
                const f32 r2 = fpu::Sub(NextRandomFloat(), 0.5f);
                GRef<f32>(a + 4) = fpu::Add(F(a + 4), r2 * F(def + 0x10));
                FireBullet(GRef<u8>(AU() + 0x55), AU(), a, AU() + 0x20);
            }
            const u32 b = AS() + 0xF8 + 4 * k;
            const s32 n = GRef<s32>(b) + 1;
            GRef<s32>(b) = n;
            if (!(n < GRef<s16>(def + 4))) {
                GRef<u32>(AS() + 0x100 + 4 * k) = 0;
                GRef<u32>(b) = 0;
            }
            GRef<u32>(t + 0x50) |= 0x1000u;
        } else if (kind == 2) {
            if (GRef<s32>(reload) < GRef<s16>(def + 6)) GRef<s32>(reload) += 1;
            if (GRef<u8>(AU() + 0x62) == 0xFF) continue;
            const u32 t = Unit(GRef<u8>(AU() + 0x62));
            if (MissionKind() == 0 && Game(0x29) != 3) {
                bool warn = false;
                if (GRef<u8>(t + 0x55) < Players() &&
                    (GRef<u64>(AU() + 0x48) & 0x0000400000000003ull) == 0x0000400000000001ull) {
                    const u32 rec = static_cast<u32>(GRef<u8>(AU() + 0x55)) * 0xC + W(0x00410648 + 4u * GRef<u8>(t + 0x55));
                    if (F(rec + 4) < 16384.0f && F(rec + 8) < 0.5f && ee::cosf(F(def + 0xC)) < F(AS() + 0xA8)) {
                        warn = true;
                        if (F(def + 8) < F(AS() + 0xA4) && (~GRef<u8>(AS() + 0xF4) & 2) != 0) {
                            Or8(AS() + 0xF4, 2);
                            QueueRadioChatter(2, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(AU() + 0x99), 0, 0);
                        }
                    }
                }
                if (!warn) GRef<u8>(AS() + 0xF4) = static_cast<u8>(GRef<u8>(AS() + 0xF4) & 0xFD);
            }
            if (!(F(AS() + 0xA4) < F(def + 8)) || !(ee::cosf(F(def + 0xC)) < F(AS() + 0xA8))) {
                GRef<u32>(AS() + 0xF8 + 4 * k) = 0;
                continue;
            }
            GRef<s32>(AS() + 0xF8 + 4 * k) += 1;
            GRef<u32>(t + 0x50) |= 0x2000u;
            if (GRef<u8>(t + 0x55) < Players()) {
                const u32 tp = W(t + 0x44);
                if (GRef<u8>(tp + 0x2F1) == 0xFF) GRef<u8>(tp + 0x2F1) = GRef<u8>(AU() + 0x55);
                if (MissionKind() == 0 && Game(0x29) != 3 && (~W(t + 0x50) & 0x20000000u) != 0)
                    QueueRadioChatter(3, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(AU() + 0x99), 0, 0);
            } else if (GRef<u8>(t + 0x54) == 1) {
                if ((W(t + 0x48) & 3u) == 0 && (~W(t + 0x50) & 0x20000000u) != 0) func_0011E560(1, t, AU());
                if ((W(AU() + 0x48) & 3u) == 0 && GRef<s32>(AS() + 0xF8 + 4 * k) == 2) func_0011E560(5, AU(), t);
            }
            const u32 s2 = AS();
            if (GRef<s32>(s2 + 0x100 + 4 * k) < GRef<s16>(def + 6)) continue;
            if (GRef<s32>(s2 + 0xF8 + 4 * k) < GRef<s16>(def + 4)) continue;
            const u32 u = AU();
            if ((GRef<u8>(t + 0x64) & 4) == 0 && 0 <= GRef<s32>(t + 0x48)) {
                FireMissile(GRef<u8>(u + 0x55), GRef<u8>(u + 0x62), u, u + 0x10, u + 0x20);
                if (MissionKind() == 0 && Game(0x29) != 3) {
                    if (GRef<u8>(AU() + 0x62) < Players())
                        QueueRadioChatter(4, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(AU() + 0x99), 0, 0);
                    else if ((W(t + 0x48) & 3u) == 0)
                        func_0011E560(2, t, AU());
                }
            } else {
                FireMissile(GRef<u8>(u + 0x55), 0xFF, u, u + 0x10, u + 0x20);
            }
            const u32 s3 = AS();
            GRef<u32>(s3 + 0xF8 + 4 * k) = 0;
            GRef<u32>(s3 + 0x100 + 4 * k) = 0;
            if ((W(AU() + 0x48) & 3u) == 0) func_0011E560(6, AU(), t);
        } else if (kind == 5) {
            const s32 r = GRef<s32>(reload);
            if (r < GRef<s16>(def + 0x1C)) {
                GRef<s32>(reload) = r + 1;
                continue;
            }
            const u32 fc = W(Scene() + 0x165C) + GRef<u8>(AU() + 0x55);
            if (fc % static_cast<u32>(static_cast<s32>(GRef<s8>(def + 2))) != 0) continue;
            guest::GuestFrame f(0x20);
            const u32 pt = f.Address(), hit = f.Address(0x10);
            const f32 c = F(0x0040E200);
            for (u32 j = 0; j < 2; ++j) {
                StoreVf0(pt);
                f32 rad;
                if (j == 0) {
                    GRef<u32>(pt) = W(def + 4);
                    GRef<u32>(pt + 8) = W(def + 8);
                    rad = F(def + 0xC);
                } else {
                    GRef<u32>(pt) = W(def + 0x10);
                    GRef<u32>(pt + 8) = W(def + 0x14);
                    rad = F(def + 0x18);
                }
                const f32 h = func_0013E338(Terrain(), pt);
                GRef<f32>(pt + 4) = h;
                const u32 u = AU();
                const f32 hv = F(u + 0x24) * 0.5f;
                const f32 disc = fpu::Sub(hv * hv, fpu::Sub(F(u + 4), h) * c);
                const f32 tt = fpu::Add(-hv, fpu::Sqrt(disc)) / c;
                sceVu0ScaleVector(hit, u + 0x20, tt);
                sceVu0AddVector(hit, AU(), hit);
                if (GetDistanceXZ(hit, pt) < rad) {
                    DropBomb(GRef<u8>(AU() + 0x55), AU(), AU() + 0x20, 0);
                    GRef<s32>(AS() + 0xF8 + 4 * k) += 1;
                    break;
                }
            }
            const u32 b = AS() + 0xF8 + 4 * k;
            if (static_cast<s32>(GRef<s8>(def + 1)) <= GRef<s32>(b)) {
                GRef<u32>(b) = 0;
                GRef<u32>(AS() + 0x100 + 4 * k) = 0;
            }
        }
    }
}

}  // namespace ac04::game
