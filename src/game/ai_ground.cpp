// Ground and naval units (slice S5e): UpdateGroundUnit 0x0012FD28 with its route commands,
// target choice, weapons and the per-kind movement/aim handlers (unit +0x6A: 2 static guns,
// 3 turrets, 4 vehicles, 5 ships, 6 turrets on a parent, 7 tanks, 8 the boss sub, 9 parts of a
// parent, 10 mission specials). Current unit in globals: 0x004117BC unit, 0x004117C0 state
// (unit +0x44), 0x004117C4 definition (unit +0x40). Docs: docs/subsystems/ai.md; notes:
// docs/port/s5-blockB-inventory.csv.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 GU() { return W(0x004117BC); }  // unit
u32 GS() { return W(0x004117C0); }  // state
u32 GD() { return W(0x004117C4); }  // definition
u32 Setup() { return W(0x004104CC); }

f32 I2F(s32 v) { return fpu::IntToFloat(v); }
f32 Deg(s32 v, f32 k) { return I2F(v) * k / 180.0f; }
void Or8(u32 a, u8 v) { GRef<u8>(a) = static_cast<u8>(GRef<u8>(a) | v); }
void Or16(u32 a, u16 v) { GRef<u16>(a) = static_cast<u16>(GRef<u16>(a) | v); }

// Slew `cur` toward `cur + d` by at most `rate`.
// fpucheck: 1 add per call
f32 Slew(f32 cur, f32 d, f32 rate) {
    if (rate < d) return fpu::Add(cur, rate);
    if (d < -rate) return fpu::Sub(cur, rate);
    return fpu::Add(cur, d);
}

// Turret elevation/azimuth toward the target (state +0x30 target in the body frame, +0x84
// distance) with the limits of definition fields lo/hi (degrees * k / 180); the azimuth limits use
// k2 when the elevation was clamped (as the original). Returns (elevation, azimuth).
struct Aim {
    f32 el, az, k;
    bool clamped;
};
Aim AimLimited(u32 def, u32 elo, u32 ehi, u32 alo, u32 ahi, f32 k1, f32 k2) {
    Aim a{};
    a.el = ee::asinf(-F(GS() + 0x34) / F(GS() + 0x84));
    a.az = ee::atan2f(F(GS() + 0x30), F(GS() + 0x38));
    a.k = k1;
    const f32 lo = Deg(GRef<s16>(def + elo), k1);
    if (a.el < lo) {
        a.el = lo;
        a.k = k2;
    } else {
        const f32 hi = Deg(GRef<s16>(def + ehi), k1);
        if (hi < a.el) {
            a.el = hi;
            a.k = k2;
        }
    }
    const f32 l2 = Deg(GRef<s16>(def + alo), a.k);
    if (a.az < l2) {
        a.az = l2;
    } else {
        const f32 h2 = Deg(GRef<s16>(def + ahi), a.k);
        if (h2 < a.az) a.az = h2;
    }
    return a;
}

void CopyPosToAimOrigin() {
    const u32 s = GS();
    GRef<u32>(s + 0x28) = 0;
    GRef<u32>(s + 0x2C) = 0;
    CopyQ(s + 0x10, GU());
}

}  // namespace

// EE: 0x0012FD28
// Ground unit per frame: hit credit and destruction (centre messages, chatter, 0x0011E560
// notifications, explosion effects by model families), route commands (as the aircraft, list
// Setup()+0x64, commands Setup()+0x60; 0xFFDF/0xFFDE remove/destroy), command handlers
// +0x43, target choice 0x00130758, the alive part animation +0xC0 (0x001354C0 / 0x001355B8 /
// 0x001356C8 / 0x00135458), the kind handler +0x6A, weapons 0x00130BF8.
// fpucheck: 3 static adds (heading spin of the destroyed models 0x7D/0x96)
void UpdateGroundUnit(s32 i) {
    GRef<u32>(0x004117BC) = Unit(i);
    GRef<u32>(0x004117C0) = W(GU() + 0x44);
    GRef<u32>(0x004117C4) = W(GU() + 0x40);
    u32 u = GU();
    if ((W(u + 0x50) & 0x20000u) != 0 && (W(u + 0x4C) & 0x1000000u) != 0) {
        if (GRef<u8>(u + 0x63) < Players())
            ShowCenterMessage(GRef<u8>(u + 0x63), GRef<s16>(u + 0x5E) == 9999 ? 0xD : 0xC, 0);
        u = GU();
        if (MissionKind() == 0 && Game(0x29) != 3 && GRef<s8>(u + 0x63) != -1 && GRef<s16>(u + 0x5E) != 9999 &&
            (W(Unit(GRef<u8>(u + 0x63)) + 0x48) & 3u) == 0 && (W(u + 0x48) & 3u) != 0) {
            if (GRef<u8>(u + 0x63) < Players())
                QueueRadioChatter(0x10, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(u + 0x99), 0, 0);
            else
                func_0011E560(8, u, Unit(GRef<u8>(u + 0x63)));
        }
    }
    u = GU();
    u32 f = W(u + 0x4C);
    if ((f & 0x200000u) != 0) {
        GRef<u32>(u + 0x4C) = f | 0x400000u;
        const u8 by = GRef<u8>(u + 0x63);
        if (by < Players()) {
            if (Game(0x27) == 0x15)
                ShowCenterMessage(by, 10, 0);
            else
                ShowCenterMessage(GRef<u8>(u + 0x63), GRef<u8>(u + 0x69) == 0xB2 ? 0xE : 0xB, 0);
            u = GU();
            if (MissionKind() == 0 && Game(0x29) != 3) {
                if (GRef<s32>(u + 0x48) < 0) {
                    QueueRadioChatter(0x12, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(u + 0x99), 0, 0);
                    func_0011E560(0xB, 0, GU());
                } else if ((W(u + 0x48) & 3u) != 0) {
                    QueueRadioChatter(0x11, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(u + 0x99), 0, 0);
                }
            }
        } else if (by != 0xFF && MissionKind() == 0 && Game(0x29) != 3) {
            const u32 o = Unit(by);
            if ((W(o + 0x48) & 3u) == 0 && (W(u + 0x48) & 3u) != 0) func_0011E560(10, o, u);
        }
        u = GU();
        const u8 m = GRef<u8>(u + 0x69);
        if (m == 0x7D || m == 0x96) {
            const f32 y = fpu::Add(F(u + 0x14), fpu::UnsignedToFloat(W(Scene() + 0x165C) & 3u) * F(0x0040E204));
            GRef<f32>(u + 0x14) = y;
            if (F(0x0040E208) < y) GRef<f32>(u + 0x14) = fpu::Sub(y, F(0x0040E20C));
        }
        func_0011E5E0(GU());
        if (GRef<u8>(GU() + 0x69) != 0xB2) {
            PlayEventSe(0, GU());
            func_00171158(3, 0, 0, GU(), 0);
        }
        const u8 m2 = GRef<u8>(GU() + 0x69);
        if (static_cast<u8>(m2 + 0x74) < 3 || m2 == 0x92 || m2 == 0x98 || m2 == 0xAF || m2 == 0x93) {
            func_001D32A0(GU());
            func_00170AA0(5, 0xFF, 0, GU());
        }
        const u8 m3 = GRef<u8>(GU() + 0x69);
        if (m3 == 0x75 || m3 == 0xA1 || m3 == 0xA2 || m3 == 0xA3 || m3 == 0xA4) {
            func_001BFEF0(GU(), GU() + 0x30);
            func_00170B60(6, 0xFF, 0, GU(), GU() + 0x30);
        }
        const u8 m4 = GRef<u8>(GU() + 0x69);
        auto big = [](u8 v) {
            return v == 0xD0 || v == 0xE6 || v == 0xD1 || v == 0xD2 || v == 0xD4 || v == 0xD5 || v == 0xD6 || v == 0xD7 ||
                   v == 0xD8 || v == 0xD9 || v == 0xDE || v == 0xE0 || v == 0xE1 || v == 0xE2;
        };
        if ((m4 == 0x49 || big(m4)) && !(Game(0x27) == 5 && GRef<u8>(GU() + 0x69) == 0xE0)) {
            func_00188A50(GU());
            func_00170AA0(7, 0xFF, 0, GU());
        }
        const u8 m5 = GRef<u8>(GU() + 0x69);
        if (big(m5) || m5 == 0xD3) {
            func_001979F0(GU(), GRef<u8>(GU() + 0x69));
            func_00170AA0(0xD, 0xFF, GRef<u8>(GU() + 0x69), GU());
        }
    }
    u = GU();
    if ((W(u + 0x4C) & 0x1000000u) != 0) {
        u32 s = GS();
        if ((GRef<u8>(s + 0x41) & 0x80) != 0) {
            GRef<u8>(s + 0x41) = static_cast<u8>(GRef<u8>(s + 0x41) & 0x7F);
            GRef<u8>(s + 0x42) = 0;
            GRef<u32>(s + 0x48) = 0;
            u32 cmd = 0xFFFF;
            if (GRef<u8>(s + 0x40) < 0x20) {
                const u32 route = GRef<u8>(static_cast<u32>(i) * 0x18 + W(Setup() + 4) + 0x16);
                if (route != 0xFF) {
                    const u32 list = route * 0x40 + W(Setup() + 100);
                    const u16 c = GRef<u16>(list + static_cast<u32>(GRef<u8>(s + 0x40)) * 2);
                    cmd = c;
                    if (c == 0xFFFF || c < 0xFFE0) {
                        if (c == 0xFFDF) {
                            func_0011BCC0(GRef<u8>(GU() + 0x55));
                            return;
                        }
                        if (c == 0xFFDE) {
                            func_0011E708(GU());
                            func_0011BCC0(GRef<u8>(GU() + 0x55));
                            GRef<u16>(GU() + 0x5E) = 0;
                            GRef<u32>(GU() + 0x4C) &= 0xFEFFFFFFu;
                            return;
                        }
                    } else {
                        GRef<u8>(s + 0x40) = static_cast<u8>(c & 0x1F);
                        cmd = GRef<u16>(list + static_cast<u32>(GRef<u8>(GS() + 0x40)) * 2);
                        s = GS();
                    }
                }
            }
            if (cmd == 0xFFFF) {
                for (u32 k = 0; k < 3; ++k) GRef<u8>(GS() + 0x44 + k) = 0;
                for (s32 k = 4; k >= 0; --k) GRef<u32>(GS() + 0x4C + 4u * static_cast<u32>(k)) = 0;
                s = GS();
                GRef<u8>(s + 0x43) = 3;
                GRef<u32>(GS() + 0x4C) = 0x1A5E0;
                s = GS();
            } else {
                const u32 c = W(Setup() + 0x60) + cmd * 0x18;
                GRef<u8>(s + 0x43) = GRef<u8>(c);
                for (u32 k = 0; k < 3; ++k) GRef<u8>(GS() + 0x44 + k) = GRef<u8>(c + 1 + k);
                for (u32 k = 0; k < 5; ++k) GRef<u32>(GS() + 0x4C + 4 * k) = W(c + 4 + 4 * k);
                s = GS();
                GRef<u8>(s + 0x40) = static_cast<u8>(GRef<u8>(s + 0x40) + 1);
                s = GS();
            }
        }
        switch (GRef<u8>(s + 0x43)) {
            case 0: func_001313D0(); break;
            case 1: func_00131628(); break;
            case 2: func_00131708(); break;
            case 3: func_00131778(); break;
            case 4: func_00131500(); break;
            default: break;
        }
        GRef<s32>(GS() + 0x48) += 1;
        UpdateGroundUnitTarget();
    }
    u = GU();
    if ((~W(u + 0x4C) & 0x1000000u) == 0 || GRef<s8>(u + GRef<u8>(u + 0x5A) + 0x58) == -1) {
        GRef<u32>(GS() + 0xC0) = 0;
    } else {
        if (F(GS() + 0xC0) < 1.0f) {
            const u8 m = GRef<u8>(GU() + 0x69);
            if (m == 0x72)
                func_001354C0(GU(), GS());
            else if (m == 0x7D)
                func_001355B8(GU(), GS());
            else if (m == 0x84)
                func_001356C8(GU(), GS());
            else
                func_00135458(GU(), GS());
        }
        func_00145D08(F(GS() + 0xC0), UnitModels(), GRef<u8>(GU() + 0x55), 1);
        u = GU();
    }
    switch (GRef<u8>(u + 0x6A)) {
        case 2: func_001317A8(); break;
        case 3: func_00131A30(); break;
        case 4: func_00131DC8(); break;
        case 5: func_001329B0(); break;
        case 6: func_00132D18(); break;
        case 7: func_00133228(); break;
        case 8: func_00133BB0(); break;
        case 9: func_00134288(); break;
        case 10: func_00134810(); break;
        default: break;
    }
    u = GU();
    if ((W(u + 0x4C) & 0x1000000u) != 0 && (StatusFlags() & 0x100u) != 0 && (~W(u + 0x4C) & 0x2000000u) != 0)
        UpdateGroundUnitWeapons();
}

// EE: 0x00130758
// Target choice every 64 frames: the N nearest enemies (N by definition +0xB & 0xF: 1 the
// farthest in range, 2-4 an insertion-sorted list) of the allowed kinds within +0xC; the last
// valid entry becomes the target; then target vector +0x30 in the body frame and distance +0x84
// (with lead by the target velocity when state +1 == 1).
// fpucheck: 1 add
void UpdateGroundUnitTarget() {
    const u32 def = GD();
    u32 u = GU();
    if ((GRef<u8>(def + 0xB) & 0xC0) == 0) return;
    if (GRef<s8>(u + 0x62) != -1) GRef<s32>(u + 0x6C) += 1;
    u32 t = GRef<u8>(u + 0x62);
    if ((GRef<u8>(u + 0x55) & 0x3Fu) == (W(Scene() + 0x165C) & 0x3Fu)) {
        const u32 mode = GRef<u8>(def + 0xB) & 0xF;
        s32 n;
        f32 dist[4];
        u32 idx[4];
        if (mode == 1) {
            n = 1;
            dist[0] = 0.0f;
        } else {
            n = mode == 2 ? 2 : mode == 3 ? 3 : mode == 4 ? 4 : 1;
            for (s32 k = 0; k < n; ++k) dist[k] = 524288.0f;
        }
        for (s32 k = 0; k < n; ++k) idx[k] = 0xFF;
        GRef<u8>(u + 0x68) = 0;
        const u8 my = GRef<u8>(u + 0x48);
        for (s32 k = 0; k < UnitCount(); ++k) {
            const u32 o = Unit(k);
            const u32 f48 = W(o + 0x48);
            if ((f48 & 3u) == (my & 3u)) continue;
            if ((~W(o + 0x4C) & 0x10000000u) != 0) continue;
            if ((~W(o + 0x4C) & 0x1000000u) != 0) continue;
            u8 bit;
            if ((f48 & 0x1000000u) != 0) {
                bit = static_cast<u8>(~GRef<u8>(GD() + 0xB) & 0x40);
            } else {
                if ((f48 & 0x2000000u) == 0) continue;
                bit = static_cast<u8>(~GRef<u8>(GD() + 0xB) & 0x80);
            }
            if (bit != 0) continue;
            const f32 d = func_00104F10(GU(), o);
            if (F(GD() + 0xC) < d) continue;
            GRef<u8>(GU() + 0x68) = static_cast<u8>(GRef<u8>(GU() + 0x68) + 1);
            if (mode == 1) {
                if (dist[0] < d) {
                    dist[0] = d;
                    idx[0] = static_cast<u32>(k);
                }
            } else {
                for (s32 j = n - 1; j >= 0; --j) {
                    if (!(d < dist[j])) break;
                    if (j != n - 1) {
                        dist[j + 1] = dist[j];
                        idx[j + 1] = idx[j];
                    }
                    dist[j] = d;
                    idx[j] = static_cast<u32>(k);
                }
            }
        }
        u = GU();
        if (idx[0] == 0xFF) {
            GRef<u8>(u + 0x62) = 0xFF;
            GRef<u32>(u + 0x6C) = 0;
        } else {
            for (s32 j = n - 1; j >= 0; --j) {
                if (idx[j] == 0xFF) continue;
                if (GRef<u8>(u + 0x62) != idx[j]) {
                    GRef<u8>(u + 0x62) = static_cast<u8>(idx[j]);
                    GRef<u32>(u + 0x6C) = 0;
                }
                break;
            }
        }
        t = GRef<u8>(GU() + 0x62);
    }
    if (t == 0xFF) return;
    const u32 o = Unit(static_cast<s32>(t));
    guest::GuestFrame fr(0x80);
    const u32 d = fr.Address(0), a = fr.Address(0x10), m = fr.Address(0x20), p2 = fr.Address(0x60);
    sceVu0SubVector(d, o, GU());
    f32 len = GetVectorLength(d);
    GRef<f32>(GS() + 0x84) = len;
    if (GRef<u8>(GS() + 1) == 1) {
        const f32 c = F(0x0040E210);
        const f32 t1 = len / c;
        sceVu0ScaleVector(a, o + 0x20, t1);
        sceVu0AddVector(p2, d, a);
        const f32 l2 = GetVectorLength(p2);
        GRef<f32>(GS() + 0x84) = l2;
        sceVu0ScaleVector(a, o + 0x20, fpu::Add(t1, l2 / c) * 0.5f);
        sceVu0AddVector(d, d, a);
        GRef<f32>(GS() + 0x84) = GetVectorLength(d);
    }
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, GU() + 0x10);
    sceVu0TransposeMatrix(m, m);
    sceVu0ApplyMatrix(GS() + 0x30, m, d);
}

// EE: 0x00130BF8
// Ground weapons by state +1: 1 gun (bursts with spread, 2 random reads per shot; firing flag
// +2 bit 0), 2 missile (lock count, launch with the muzzle angles), 3 flak at the predicted target
// point (3 random reads), 4 artillery barrage around the target (2 random reads per shell, kinds
// 0-3 by the definition), each with its definition at +0x80, counters +8 (burst) and +0xC (reload).
// fpucheck: 17 static adds
void UpdateGroundUnitWeapons() {
    u32 s = GS();
    const u8 kind = GRef<u8>(s + 1);
    GRef<u16>(s + 2) = static_cast<u16>(GRef<u16>(s + 2) & 0xFFFE);
    if (kind == 0) return;
    if (kind == 1) {
        if ((W(GU() + 0x4C) & 0x1000u) == 0) return;
        const u32 def = W(s + 0x80);
        if (GRef<s32>(s + 0xC) < GRef<s16>(def + 6)) {
            GRef<s32>(s + 0xC) += 1;
            return;
        }
        if (GRef<u8>(GU() + 0x62) == 0xFF) return;
        const u32 t = Unit(GRef<u8>(GU() + 0x62));
        const f32 c = F(def + 0xC);
        if (!(F(s + 0x84) < F(def + 8)) || F(s + 0x88) <= -c || c <= F(s + 0x88) || F(s + 0x8C) <= -c ||
            !(F(s + 0x8C) < c)) {
            GRef<u32>(s + 8) = 0;
            return;
        }
        const u32 fc = W(Scene() + 0x165C) + GRef<u8>(GU() + 0x55);
        if (fc % static_cast<u32>(static_cast<s32>(GRef<s8>(def + 1))) == 0) {  // divu
            guest::GuestFrame f(0x10);
            const u32 a = f.Address();
            func_00102EF0(a, GU() + 0x10, s + 0x20);
            GRef<f32>(a) = fpu::Add(F(a), fpu::Sub(NextRandomFloat(), 0.5f) * F(def + 0x10));
            GRef<f32>(a + 4) = fpu::Add(F(a + 4), fpu::Sub(NextRandomFloat(), 0.5f) * F(def + 0x10));
            FireBullet(GRef<u8>(GU() + 0x55), GS() + 0x10, a, GU() + 0x20);
            s = GS();
        }
        const s32 n = GRef<s32>(s + 8) + 1;
        GRef<s32>(s + 8) = n;
        if (!(n < GRef<s16>(def + 4))) {
            GRef<u32>(s + 0xC) = 0;
            GRef<u32>(s + 8) = 0;
        }
        GRef<u32>(t + 0x50) |= 0x1000u;
        Or16(s + 2, 1);
        return;
    }
    if (kind == 2) {
        const u32 def = W(s + 0x80);
        const s16 rl = GRef<s16>(def + 6);
        if (GRef<s32>(s + 0xC) < rl) GRef<s32>(s + 0xC) += 1;
        const u32 u = GU();
        if (GRef<u8>(u + 0x62) == 0xFF) return;
        const u32 t = Unit(GRef<u8>(u + 0x62));
        const f32 c = F(def + 0xC);
        if (F(def + 8) <= F(s + 0x84) || F(s + 0x88) <= -c || c <= F(s + 0x88) || F(s + 0x8C) <= -c || c <= F(s + 0x8C)) {
            GRef<u32>(s + 8) = 0;
            return;
        }
        const s32 n = GRef<s32>(s + 8) + 1;
        GRef<s32>(s + 8) = n;
        if (rl <= GRef<s32>(s + 0xC)) {
            if (n >= GRef<s16>(def + 4)) {
                guest::GuestFrame f(0x10);
                const u32 a = f.Address();
                func_00102EF0(a, u + 0x10, s + 0x20);
                if ((GRef<u8>(t + 100) & 4) == 0)
                    FireMissile(GRef<u8>(GU() + 0x55), GRef<u8>(GU() + 0x62), GS() + 0x10, a, GU() + 0x20);
                else
                    FireMissile(GRef<u8>(GU() + 0x55), 0xFF, GS() + 0x10, a, GU() + 0x20);
                s = GS();
                GRef<u32>(s + 8) = 0;
                GRef<u32>(s + 0xC) = 0;
            }
        }
        GRef<u32>(t + 0x50) |= 0x2000u;
        Or16(s + 2, 1);
        if (GRef<u8>(t + 0x55) < Players() && GRef<s8>(W(t + 0x44) + 0x2F2) == -1)
            GRef<u8>(W(t + 0x44) + 0x2F2) = GRef<u8>(GU() + 0x55);
        return;
    }
    if (kind == 3) {
        if ((W(GU() + 0x4C) & 0x1000u) == 0) return;
        const u32 def = W(s + 0x80);
        if (GRef<s8>(GU() + 0x62) == -1) return;
        const u32 t = Unit(GRef<u8>(GU() + 0x62));
        if (F(def + 0xC) <= F(s + 0x84)) return;
        const u32 fc = W(Scene() + 0x165C) + GRef<u8>(GU() + 0x55);
        if (fc % static_cast<u32>(static_cast<s32>(GRef<s8>(def + 1))) != 0) return;  // divu
        guest::GuestFrame f(0x30);
        const u32 p = f.Address(0), q = f.Address(0x10), off = f.Address(0x20);
        sceVu0ScaleVector(q, t + 0x20, NextRandomFloat() * 1.5f * 60.0f);
        sceVu0AddVector(q, q, t);
        const f32 h = func_00104FA8(GS() + 0x30);
        if (I2F(GRef<s16>(def + 4)) < h) {
            sceVu0SubVector(p, q, GU());
            sceVu0ScaleVector(p, p, I2F(GRef<s16>(def + 4)) / h);
            sceVu0AddVector(p, p, GU());
        } else {
            GRef<u32>(p) = W(q);
            GRef<u32>(p + 8) = W(q + 8);
            GRef<u32>(p + 0xC) = W(q + 0xC);
        }
        GRef<f32>(p + 4) = 0.0f;
        const f32 r1 = fpu::Sub(NextRandomFloat(), 0.5f);
        GRef<f32>(off) = fpu::Add(r1, r1) * I2F(GRef<s16>(def + 10));
        const f32 r2 = NextRandomFloat();
        GRef<f32>(off + 4) = fpu::Add(r2 * I2F(GRef<s16>(def + 8) - GRef<s16>(def + 6)), I2F(GRef<s16>(def + 6)));
        const f32 r3 = fpu::Sub(NextRandomFloat(), 0.5f);
        GRef<f32>(off + 8) = fpu::Add(r3, r3) * I2F(GRef<s16>(def + 10));
        sceVu0AddVector(p, p, off);
        GRef<f32>(p + 0xC) = 1.0f;
        SpawnFlakBurst(GRef<u8>(GU() + 0x55), p);
        return;
    }
    if (kind == 4) {
        const u32 def = W(s + 0x80);
        if (GRef<u8>(GU() + 0x62) == 0xFF) return;
        const u32 t = Unit(GRef<u8>(GU() + 0x62));
        if (GRef<s32>(s + 0xC) == 0) {
            GRef<s32>(s + 8) = static_cast<s32>(GRef<s8>(def + 2)) * static_cast<s32>(GRef<s8>(def + 3));
            const f32 r = NextRandomFloat();
            s = GS();
            GRef<s32>(s + 0xC) =
                GRef<s16>(def + 8) + fpu::FloatToInt(I2F(GRef<s16>(def + 10)) * fpu::Sub(fpu::Add(r, r), 1.0f));
        }
        GRef<s32>(s + 0xC) -= 1;
        if (GRef<s32>(s + 8) < 1) return;
        Or16(s + 2, 1);
        if (GRef<s32>(s + 8) % static_cast<s32>(GRef<s8>(def + 3)) == 0) {
            guest::GuestFrame f(0x10);
            const u32 p = f.Address();
            f32 r = I2F(GRef<s16>(def + 6)) * fpu::Sub(NextRandomFloat(), 0.5f);
            GRef<f32>(p) = fpu::Add(fpu::Add(F(t), r), r);
            r = I2F(GRef<s16>(def + 6)) * fpu::Sub(NextRandomFloat(), 0.5f);
            GRef<f32>(p + 8) = fpu::Add(fpu::Add(F(t + 8), r), r);
            GRef<f32>(p + 4) = func_0013E338(Terrain(), p);
            GRef<f32>(p + 0xC) = 1.0f;
            const u8 sub = GRef<u8>(def + 1);
            const s32 v = GRef<s16>(def + 4);
            if (sub == 1)
                func_0014E3C8(I2F(v) * F(0x0040E214), GRef<u8>(GU() + 0x55), GRef<u8>(def), GU(), p);
            else if (sub == 0)
                func_0014E4A8(I2F(v) * 5.0f, GRef<u8>(GU() + 0x55), GRef<u8>(def), GU(), p);
            else if (sub == 2)
                func_0014E588(I2F(v) * F(0x0040E218) / 180.0f, GRef<u8>(GU() + 0x55), GRef<u8>(def), GU(), p);
            else if (sub == 3)
                func_0014E700(I2F(v), GRef<u8>(GU() + 0x55), GRef<u8>(def), GU(), p);
            func_001828D0(GU());
            func_00170AA0(8, 0xFF, 0, GU());
            s = GS();
        }
        GRef<s32>(s + 8) -= 1;
    }
}

// EE: 0x001313D0
// Command 0 (wander inside an area): area centre/radius from the command words, first point
// = centre + random offset (+-2048 on x and z, 2 random reads); done by time or target count.
// fpucheck: 4 adds
void func_001313D0() {
    const u32 s = GS();
    if (GRef<u8>(s + 0x42) == 0) {
        GRef<f32>(s + 0x70) = I2F(GRef<s32>(s + 0x50));
        GRef<f32>(s + 0x74) = I2F(GRef<s32>(s + 0x54));
        GRef<f32>(s + 0x78) = I2F(GRef<s32>(s + 0x58));
        GRef<f32>(s + 0x7C) = I2F(GRef<s32>(s + 0x5C));
        const f32 r1 = NextRandomFloat();
        GRef<u32>(GS() + 100) = 0;
        GRef<f32>(GS() + 0x60) = fpu::Add(F(GS() + 0x70), fpu::Sub(r1, 0.5f) * 4096.0f);
        const f32 r2 = NextRandomFloat();
        GRef<u8>(GS() + 0x42) = static_cast<u8>(GRef<u8>(GS() + 0x42) + 1);
        GRef<f32>(GS() + 0x68) = fpu::Add(F(GS() + 0x78), fpu::Sub(r2, 0.5f) * 4096.0f);
        return;
    }
    if (GRef<u8>(s + 0x42) != 1) return;
    const u8 mode = GRef<u8>(s + 0x44);
    if (mode == 1) {
        if (GRef<s32>(s + 0x48) <= GRef<s32>(s + 0x4C)) return;
    } else if (mode == 2) {
        if (GRef<s32>(s + 0x4C) < static_cast<s32>(GRef<u8>(GU() + 0x68))) return;
    } else {
        return;
    }
    Or8(s + 0x41, 0x80);
}

// EE: 0x00131500
// Command 4 (wander around the current position).
// fpucheck: 4 adds
void func_00131500() {
    const u32 s = GS(), u = GU();
    if (GRef<u8>(s + 0x42) == 0) {
        const s32 r = GRef<s32>(s + 0x50);
        GRef<u32>(s + 0x70) = W(u);
        GRef<u32>(s + 0x74) = W(u + 4);
        GRef<f32>(s + 0x7C) = I2F(r);
        GRef<u32>(s + 0x78) = W(u + 8);
        const f32 r1 = NextRandomFloat();
        GRef<u32>(GS() + 100) = 0;
        GRef<f32>(GS() + 0x60) = fpu::Add(F(GS() + 0x70), fpu::Sub(r1, 0.5f) * 4096.0f);
        const f32 r2 = NextRandomFloat();
        GRef<u8>(GS() + 0x42) = static_cast<u8>(GRef<u8>(GS() + 0x42) + 1);
        GRef<f32>(GS() + 0x68) = fpu::Add(F(GS() + 0x78), fpu::Sub(r2, 0.5f) * 4096.0f);
        return;
    }
    if (GRef<u8>(s + 0x42) != 1) return;
    const u8 mode = GRef<u8>(s + 0x44);
    if (mode == 1) {
        if (GRef<s32>(s + 0x48) <= GRef<s32>(s + 0x4C)) return;
    } else if (mode == 2) {
        if (GRef<s32>(s + 0x4C) < static_cast<s32>(GRef<u8>(GU() + 0x68))) return;
    } else {
        return;
    }
    Or8(s + 0x41, 0x80);
}

// EE: 0x00131628
// Command 1 (drive to a point; ships at the terrain height minus +0x78); done within +0x58.
// fpucheck: 1 add
void func_00131628() {
    const u32 s = GS(), u = GU();
    if (GRef<u8>(s + 0x42) == 0) {
        GRef<f32>(s + 0x6C) = 1.0f;
        GRef<f32>(s + 0x60) = I2F(GRef<s32>(s + 0x4C));
        GRef<f32>(s + 100) = I2F(GRef<s32>(s + 0x50));
        GRef<f32>(s + 0x68) = I2F(GRef<s32>(s + 0x54));
        if (GRef<u8>(u + 0x6A) == 5) {
            const f32 h = func_0013E338(Terrain(), s + 0x60);
            GRef<f32>(GS() + 100) = fpu::Sub(h, F(GU() + 0x78));
        }
        GRef<u8>(GS() + 0x42) = static_cast<u8>(GRef<u8>(GS() + 0x42) + 1);
        return;
    }
    if (GRef<u8>(s + 0x42) == 1 && func_00104F10(s + 0x60, u) < I2F(GRef<s32>(GS() + 0x58))) Or8(GS() + 0x41, 0x80);
}

// EE: 0x00131708
// Command 2 (escort unit +0x44 for +0x50 frames at distance +0x4C).
void func_00131708() {
    const u32 s = GS();
    if (GRef<u8>(s + 0x42) == 0) {
        GRef<u8>(s + 0x47) = GRef<u8>(s + 0x44);
        GRef<u8>(s + 0x42) = static_cast<u8>(GRef<u8>(s + 0x42) + 1);
        GRef<f32>(s + 0x7C) = I2F(GRef<s32>(s + 0x4C));
        return;
    }
    if (GRef<u8>(s + 0x42) == 1 && GRef<s32>(s + 0x50) < GRef<s32>(s + 0x48)) Or8(s + 0x41, 0x80);
}

// EE: 0x00131778
// Command 3 (hold for +0x4C frames).
void func_00131778() {
    const u32 s = GS();
    if (GRef<s32>(s + 0x4C) <= GRef<s32>(s + 0x48)) Or8(s + 0x41, 0x80);
}

// EE: 0x001317A8
// Kind 2 (static guns): model 0x4D bobs (sin of a per-unit phase * 50 on the placement height);
// model 0x43 (the mission's rising object) climbs after mission time 0x4FB0 in phase 2 and is
// removed below -131072, with effect flags 0x100/0x200 from 0x4E0C; aim at the target.
// fpucheck: 6 static adds
void func_001317A8() {
    u32 u = GU();
    if ((~W(u + 0x4C) & 0x1000000u) != 0) return;
    u8 m = GRef<u8>(u + 0x69);
    if (m == 0x4D) {
        const s32 per = static_cast<s32>(GRef<u8>(u + 0x55)) * 2 + 0x200;
        const u32 ph = (static_cast<u32>(GRef<u8>(u + 0x55)) * 0xAD + W(Scene() + 0x165C)) % static_cast<u32>(per);  // divu
        f32 k = fpu::UnsignedToFloat(ph) / I2F(per);
        k = ee::sinf(fpu::Sub(fpu::Add(k, k), 1.0f) * F(0x0040E21C));
        u = GU();
        GRef<f32>(u + 4) = fpu::Add(F(static_cast<u32>(GRef<u8>(u + 0x55)) * 0x18 + W(Setup() + 4) + 8), k * 50.0f);
        m = GRef<u8>(u + 0x69);
    }
    if (m == 0x43) {
        GRef<u8>(u + 0x9B) = 0;
        const u32 s = GS();
        if (F(u + 4) <= -131072.0f) {
            GRef<s32>(u + 0x7C) = -1;
            ZeroQ(u);
        } else {
            if ((StatusFlags() & 4u) == 0) goto aim;
            if ((GRef<u8>(Status() + 0x7D) & 0x20) != 0) {
                GRef<u16>(u + 0x5E) = 0;
                return;
            }
            if (!(W(Status() + 0x24) < 0x4FB0)) {
                if (F(0x0040E220) < F(s + 0x90)) GRef<f32>(s + 0x90) = fpu::Add(F(s + 0x90), F(0x0040E224));
                const f32 vy = F(u + 0x24);
                if (-50.0f < vy) GRef<f32>(u + 0x24) = fpu::Add(vy, F(s + 0x90));
                GRef<u8>(u + 0x9B) = 7;
            }
            if ((StatusFlags() & 4u) == 0) goto aim;
            if (W(Status() + 0x24) < 0x4FB0) {
                if (W(Status() + 0x24) < 0x4E0C) goto aim;
                Or16(u + 0x56, 0x100);
            } else {
                Or16(u + 0x56, 0x200);
            }
        }
    }
aim:
    u = GU();
    if (GRef<s8>(u + 0x62) == -1) return;
    const u32 s = GS();
    GRef<f32>(s + 0x20) = ee::asinf(-F(s + 0x34) / F(s + 0x84));
    GRef<f32>(GS() + 0x24) = ee::atan2f(F(GS() + 0x30), F(GS() + 0x38));
    CopyPosToAimOrigin();
}

// EE: 0x00131A30
// Kind 3 (turrets): limited aim (definition +0x18/+0x1A elevation, +0x14/+0x16 azimuth, slew
// +0x1C * C / 180 * C), elevation clamp C 0x0040E238/0x0040E23C, azimuth wrap for full-circle
// turrets, model tracks 1 (azimuth) and 2 (elevation) when lockable.
// fpucheck: 15 static adds
void func_00131A30() {
    const u32 u = GU();
    const u32 def = W(u + 0x40);
    if (GRef<s8>(u + 0x62) == -1) return;
    const Aim a = AimLimited(def, 0x18, 0x1A, 0x14, 0x16, F(0x0040E228), F(0x0040E22C));
    const u32 s = GS();
    const f32 rate = Deg(GRef<s16>(def + 0x1C), F(0x0040E230)) * F(0x0040E234);
    const f32 de = fpu::Sub(a.el, F(s + 0x20));
    GRef<f32>(s + 0x88) = de;
    GRef<f32>(s + 0x20) = Slew(F(s + 0x20), de, rate);
    if (F(0x0040E238) < F(s + 0x20)) GRef<f32>(s + 0x20) = F(0x0040E238);
    if (F(s + 0x20) < F(0x0040E23C)) GRef<u32>(s + 0x20) = W(0x0040E23C);
    f32 da = fpu::Sub(a.az, F(s + 0x24));
    GRef<f32>(s + 0x8C) = da;
    if (0x167 < static_cast<s32>(GRef<s16>(def + 0x16)) - static_cast<s32>(GRef<s16>(def + 0x14))) {
        if (F(0x0040E240) < da)
            GRef<f32>(s + 0x8C) = fpu::Sub(da, F(0x0040E244));
        else if (da < F(0x0040E248))
            GRef<f32>(s + 0x8C) = fpu::Add(da, F(0x0040E24C));
    }
    GRef<f32>(s + 0x24) = Slew(F(s + 0x24), F(s + 0x8C), rate);
    f32 z = F(s + 0x24);
    if (F(0x0040E250) < z) {
        GRef<f32>(s + 0x24) = fpu::Sub(z, F(0x0040E254));
        z = F(s + 0x24);
    }
    if (z < F(0x0040E258)) GRef<f32>(s + 0x24) = fpu::Add(z, F(0x0040E25C));
    CopyPosToAimOrigin();
    GRef<f32>(GS() + 0x14) = fpu::Sub(F(GS() + 0x14), 15.0f);
    if ((W(GU() + 0x4C) & 0x20000000u) == 0) return;
    func_00145D08(fpu::Add(F(GS() + 0x24) / F(0x0040E260), 0.5f), UnitModels(), GRef<u8>(GU() + 0x55), 1);
    func_00145D08(fpu::Add(F(GS() + 0x20) / F(0x0040E250), 0.5f), UnitModels(), GRef<u8>(GU() + 0x55), 2);
}

// EE: 0x00131DC8
// Kind 4 (vehicles): every 64 frames stop near enemy vehicles (+0x22); movement by command
// (wander with random points: 2 random reads, drive, escort), heading slew and speed (random
// cruise: 1 random read), terrain-following vertical speed, coasting, aim (turreted models
// 0x61/0x62 with limits), body turn toward the target when not moving, model tracks, dust/wake
// emitter (+0x7C handle; model 0x60: 0x0019F230 + replay event).
// fpucheck: 35 static adds
void func_00131DC8() {
    u32 u = GU();
    const u32 def = W(u + 0x40);
    bool moving = false;
    if ((~W(u + 0x4C) & 0x1000000u) != 0) {
        ZeroQ(u + 0x20);
        return;
    }
    const f32 ovx = F(u + 0x20), ovz = F(u + 0x28);
    guest::GuestFrame fr(0x20);
    const u32 d = fr.Address(0), nx = fr.Address(0x10);
    u32 s = GS();
    if ((GRef<u8>(u + 0x55) & 0x3Fu) == (W(Scene() + 0x165C) & 0x3Fu)) {
        const u32 my = W(u + 0x48);
        GRef<u16>(s + 2) = static_cast<u16>(GRef<u16>(s + 2) & 0xFEFF);
        for (s32 k = 0; k < UnitCount(); ++k) {
            const u32 o = Unit(k);
            if ((~W(o + 0x4C) & 0x10000000u) != 0 || (~W(o + 0x4C) & 0x1000000u) != 0) continue;
            if ((W(o + 0x48) & 3u) == (my & 3u)) continue;
            if (GRef<u8>(o + 0x6A) != 4) continue;
            if (func_00104F10(o, GU()) < fpu::IntToFloat(GRef<u16>(def + 0x22))) {
                Or16(GS() + 2, 0x100);
                break;
            }
        }
        s = GS();
    }
    u32 target = 0;
    bool to_target = false;
    switch (GRef<u8>(s + 0x43)) {
        case 0:
        case 4:
            if ((GRef<u16>(s + 2) & 1) != 0) Or16(s + 2, 0x100);
            if ((~GRef<u16>(s + 2) & 0x100) != 0) {
                const s32 per = static_cast<s32>(GRef<u8>(GU() + 0x55)) + 0xF0;
                const s32 r = static_cast<s32>(W(Scene() + 0x165C) % static_cast<u32>(per));  // divu
                if (r < 0x79) {
                    if (F(GS() + 0x7C) * F(0x0040E264) <= func_00104F10(GU(), GS() + 0x70)) {
                        target = GS() + 0x70;
                        to_target = true;
                    } else if (r == 0) {
                        moving = true;
                        const f32 r1 = NextRandomFloat();
                        GRef<f32>(d + 4) = 0.0f;
                        GRef<f32>(d) = fpu::Sub(r1, 0.5f) * 4096.0f;
                        const f32 r2 = NextRandomFloat();
                        GRef<u32>(d + 0xC) = 0;
                        GRef<f32>(d + 8) = fpu::Sub(r2, 0.5f) * 4096.0f;
                        sceVu0AddVector(GS() + 0x60, d, GU());
                    } else {
                        target = GS() + 0x60;
                        to_target = true;
                    }
                }
            }
            break;
        case 1: {
            if (GRef<u8>(s + 0x44) == 1 && (GRef<u16>(s + 2) & 1) != 0) Or16(s + 2, 0x100);
            if ((~GRef<u16>(s + 2) & 0x100) != 0) {
                target = s + 0x60;
                to_target = true;
            }
            break;
        }
        case 2: {
            const u32 lead = Unit(GRef<u8>(s + 0x47));
            const f32 dl = func_00104F10(lead, GU());
            s = GS();
            if (dl < F(s + 0x7C)) {
                if ((GRef<u16>(s + 2) & 1) != 0) Or16(s + 2, 0x100);
                if ((~GRef<u16>(s + 2) & 0x100) != 0) {
                    if (1024.0f <= dl) {
                        target = lead;
                        to_target = true;
                    } else if (GRef<u8>(GU() + 0x62) != 0xFF) {
                        sceVu0SubVector(d, Unit(GRef<u8>(GU() + 0x62)), GU());
                        moving = true;
                    }
                }
            } else if ((~GRef<u16>(s + 2) & 0x100) != 0) {
                moving = true;
                sceVu0SubVector(d, lead, GU());
                GRef<u32>(d + 0xC) = 0;
            }
            break;
        }
        default: break;
    }
    if (to_target) {
        sceVu0SubVector(d, target, GU());
        moving = true;
    }
    u = GU();
    if (moving) {
        f32 yd = fpu::Sub(ee::atan2f(F(d), F(d + 8)), F(u + 0x14));
        if (F(0x0040E268) < yd) yd = fpu::Sub(yd, F(0x0040E26C));
        if (yd < F(0x0040E270)) yd = fpu::Add(yd, F(0x0040E274));
        const f32 rate = I2F(GRef<s16>(def + 0x16)) * F(0x0040E278) * F(0x0040E268) / 180.0f;
        GRef<f32>(u + 0x14) = Slew(F(u + 0x14), yd, rate);
        s = GS();
        if (F(0x0040E27C) < yd || yd < F(0x0040E280)) {
            if (0.0f < F(s + 0x90))
                GRef<f32>(s + 0x90) = fpu::Sub(F(s + 0x90), I2F(GRef<s16>(def + 0x14)) * F(0x0040E284) * F(0x0040E284));
        } else if (GRef<u8>(s + 0x43) == 1 && GRef<s32>(s + 0x5C) != 0) {
            GRef<f32>(s + 0x90) = I2F(GRef<s32>(s + 0x5C)) * F(0x0040E288);
        } else {
            const f32 r = NextRandomFloat();
            GRef<f32>(GS() + 0x90) = I2F(GRef<s16>(def + 0x14)) * F(0x0040E290) * fpu::Add(r * F(0x0040E28C), F(0x0040E294));
        }
        u = GU();
        GRef<f32>(u + 0x20) = ee::sinf(F(u + 0x14)) * F(GS() + 0x90);
        GRef<f32>(u + 0x28) = ee::cosf(F(u + 0x14)) * F(GS() + 0x90);
        sceVu0AddVector(nx, u, u + 0x20);
        const f32 h = func_0013E338(Terrain(), nx);
        u = GU();
        GRef<f32>(u + 0x24) = fpu::Sub(fpu::Sub(h, F(u + 0x78)), F(u + 4));
    } else if (0.0f < F(GS() + 0x90)) {
        // Coasting: slow down and keep turning (odd unit ids one way, even the other).
        const f32 k = F(0x0040E298);
        s = GS();
        GRef<f32>(s + 0x90) = fpu::Sub(F(s + 0x90), I2F(GRef<s16>(def + 0x14)) * k * k);
        GRef<f32>(u + 0x20) = ee::sinf(F(u + 0x14)) * F(GS() + 0x90);
        GRef<f32>(u + 0x28) = ee::cosf(F(u + 0x14)) * F(GS() + 0x90);
        sceVu0AddVector(nx, u, u + 0x20);
        const f32 h = func_0013E338(Terrain(), nx);
        u = GU();
        GRef<f32>(u + 0x24) = fpu::Sub(fpu::Sub(h, F(u + 0x78)), F(u + 4));
        if ((GRef<u8>(u + 0x55) & 1) == 0)
            GRef<f32>(u + 0x14) = fpu::Sub(F(u + 0x14), Deg(GRef<s16>(def + 0x16), F(0x0040E2A0)) * k);
        else
            GRef<f32>(u + 0x14) = fpu::Add(F(u + 0x14), Deg(GRef<s16>(def + 0x16), F(0x0040E29C)) * k);
    } else {
        ZeroQ(u + 0x20);
    }
    u = GU();
    if (200.0f < F(u + 0x24) || F(u + 0x24) < -200.0f) GRef<u32>(u + 0x24) = 0;
    f32 el, az;
    s32 slew_def;
    f32 kslew = F(0x0040E2A4);
    if (GRef<s8>(u + 0x62) == -1) {
        el = 0.0f;
        az = 0.0f;
        slew_def = GRef<s16>(def + 0x20);
    } else if (static_cast<u32>(GRef<u8>(u + 0x69) - 0x61) < 2) {
        const Aim a = AimLimited(def, 0x1C, 0x1E, 0x18, 0x1A, F(0x0040E2A4), F(0x0040E2A8));
        el = a.el;
        az = a.az;
        slew_def = GRef<s16>(def + 0x20);
    } else {
        el = ee::asinf(-F(GS() + 0x34) / F(GS() + 0x84));
        az = ee::atan2f(F(GS() + 0x30), F(GS() + 0x38));
        slew_def = GRef<s16>(def + 0x20);
    }
    (void)kslew;
    s = GS();
    const f32 rate = I2F(slew_def) * F(0x0040E2AC) / 180.0f * F(0x0040E2B0);
    const f32 de = fpu::Sub(el, F(s + 0x20));
    GRef<f32>(s + 0x88) = de;
    GRef<f32>(s + 0x20) = Slew(F(s + 0x20), de, rate);
    if (F(0x0040E2B4) < F(s + 0x20))
        GRef<f32>(s + 0x20) = F(0x0040E2B4);
    else if (F(s + 0x20) < F(0x0040E2B8))
        GRef<u32>(s + 0x20) = W(0x0040E2B8);
    u = GU();
    bool slew_turret = true;
    if (static_cast<u32>(GRef<u8>(u + 0x69) - 0x61) >= 2 && !moving) {
        GRef<f32>(s + 0x8C) = az;
        const f32 r2 = I2F(GRef<s16>(def + 0x16)) * F(0x0040E2DC) * F(0x0040E2E0) / 180.0f;
        GRef<f32>(u + 0x14) = Slew(F(u + 0x14), az, r2);
        GRef<u32>(s + 0x24) = 0;
        slew_turret = false;
    }
    if (slew_turret) {
        f32 da = fpu::Sub(az, F(s + 0x24));
        GRef<f32>(s + 0x8C) = da;
        if (0x167 < static_cast<s32>(GRef<s16>(def + 0x1A)) - static_cast<s32>(GRef<s16>(def + 0x18))) {
            if (F(0x0040E2BC) < da)
                GRef<f32>(s + 0x8C) = fpu::Sub(da, F(0x0040E2C0));
            else if (da < F(0x0040E2C4))
                GRef<f32>(s + 0x8C) = fpu::Add(da, F(0x0040E2C8));
        }
        GRef<f32>(s + 0x24) = Slew(F(s + 0x24), F(s + 0x8C), rate);
        const f32 z = F(s + 0x24);
        if (F(0x0040E2CC) < z)
            GRef<f32>(s + 0x24) = fpu::Sub(z, F(0x0040E2D0));
        else if (z < F(0x0040E2D4))
            GRef<f32>(s + 0x24) = fpu::Add(z, F(0x0040E2D8));
    }
    CopyPosToAimOrigin();
    GRef<f32>(GS() + 0x14) = fpu::Sub(F(GS() + 0x14), 15.0f);
    u = GU();
    if ((W(u + 0x4C) & 0x20000000u) != 0) {
        func_00145D08(fpu::Add(F(GS() + 0x24) / F(0x0040E2E4), 0.5f), UnitModels(), GRef<u8>(u + 0x55), 1);
        func_00145D08(fpu::Add(F(GS() + 0x20) / F(0x0040E2E8), 0.5f), UnitModels(), GRef<u8>(GU() + 0x55), 2);
        u = GU();
    }
    if (F(u + 0x20) == 0.0f && F(u + 0x28) == 0.0f) {
        GRef<s32>(u + 0x7C) = -1;
        return;
    }
    if (ovx == 0.0f && ovz == 0.0f) GRef<s32>(u + 0x7C) = -1;
    sceVu0AddVector(nx, u, u + 0x20);
    if (GRef<u8>(GU() + 0x69) == 0x60) {
        func_0019F230(nx, GU() + 0x10, GU() + 0x20, 0x60);
        func_00170C40(9, 0xFF, GRef<u8>(GU() + 0x69), nx, GU() + 0x10, GU() + 0x20);
    } else {
        Or16(GU() + 0x56, 0x2000);
    }
}

// EE: 0x001329B0
// Kind 5 (ships): with command 1 steer to +0x60 (heading slew, random cruise speed: 1 random
// read), terrain-following height; aim with limits C 0x0040E310 / 0x0040E314 (no slew).
// fpucheck: 9 static adds
void func_001329B0() {
    u32 u = GU();
    const u32 def = W(u + 0x40);
    if ((~W(u + 0x4C) & 0x1000000u) != 0) {
        ZeroQ(u + 0x20);
        return;
    }
    guest::GuestFrame fr(0x20);
    const u32 d = fr.Address(0), nx = fr.Address(0x10);
    if (GRef<u8>(GS() + 0x43) == 1) {
        sceVu0SubVector(d, GS() + 0x60, GU());
        u = GU();
        f32 yd = fpu::Sub(ee::atan2f(F(d), F(d + 8)), F(u + 0x14));
        if (F(0x0040E2EC) < yd) yd = fpu::Sub(yd, F(0x0040E2F0));
        if (yd < F(0x0040E2F4)) yd = fpu::Add(yd, F(0x0040E2F8));
        const f32 rate = I2F(GRef<s16>(def + 0x16)) * F(0x0040E2FC) * F(0x0040E2EC) / 180.0f;
        GRef<f32>(u + 0x14) = Slew(F(u + 0x14), yd, rate);
        const u32 s = GS();
        if (GRef<u8>(s + 0x43) == 1 && GRef<s32>(s + 0x5C) != 0) {
            GRef<f32>(s + 0x90) = I2F(GRef<s32>(s + 0x5C)) * F(0x0040E300);
        } else {
            const f32 r = NextRandomFloat();
            GRef<f32>(GS() + 0x90) = I2F(GRef<s16>(def + 0x14)) * F(0x0040E308) * fpu::Add(r * F(0x0040E304), F(0x0040E30C));
        }
        u = GU();
        GRef<f32>(u + 0x20) = ee::sinf(F(u + 0x14)) * F(GS() + 0x90);
        GRef<f32>(u + 0x28) = ee::cosf(F(u + 0x14)) * F(GS() + 0x90);
        sceVu0AddVector(nx, u, u + 0x20);
        const f32 h = func_0013E338(Terrain(), nx);
        u = GU();
        GRef<f32>(u + 0x24) = fpu::Sub(fpu::Sub(h, F(u + 0x78)), F(u + 4));
    } else {
        ZeroQ(u + 0x20);
    }
    u = GU();
    if (GRef<s8>(u + 0x62) != -1) {
        const u32 s = GS();
        GRef<u32>(s + 0x88) = 0;
        GRef<u32>(s + 0x8C) = 0;
        GRef<f32>(s + 0x20) = ee::asinf(-F(s + 0x34) / F(s + 0x84));
        GRef<f32>(GS() + 0x24) = ee::atan2f(F(GS() + 0x30), F(GS() + 0x38));
        const u32 t = GS();
        const f32 k = F(0x0040E310);
        const f32 lo = Deg(GRef<s16>(def + 0x1C), k);
        if (F(t + 0x20) < lo) {
            GRef<f32>(t + 0x20) = lo;
        } else {
            const f32 hi = Deg(GRef<s16>(def + 0x1E), k);
            if (hi < F(t + 0x20)) GRef<f32>(t + 0x20) = hi;
        }
        const f32 alo = Deg(GRef<s16>(def + 0x18), F(0x0040E314));
        if (F(t + 0x24) < alo) {
            GRef<f32>(t + 0x24) = alo;
        } else {
            const f32 ahi = Deg(GRef<s16>(def + 0x1A), F(0x0040E314));
            if (ahi < F(t + 0x24)) GRef<f32>(t + 0x24) = ahi;
        }
    }
    CopyQ(GS() + 0x10, GU());
}

// EE: 0x00132D18
// Kind 6 (turret mounted on parent unit def+0x14 at offset def+0x16..+0x1A; model 0x4B is
// removed with its parent): position/velocity from the parent, limited aim with elevation slew
// and the turret azimuth +0x90 slewed relative to the parent heading (+def+0x26 degrees).
// fpucheck: 19 static adds
void func_00132D18() {
    u32 u = GU();
    const u32 def = W(u + 0x40);
    const u32 par = Unit(GRef<u8>(def + 0x14));
    if (GRef<u8>(u + 0x69) == 0x4B && (~W(par + 0x4C) & 0x10000000u) != 0) {
        GRef<u32>(u + 0x4C) &= 0xCEFFFFFFu;
        return;
    }
    guest::GuestFrame fr(0x70);
    const u32 off = fr.Address(0), m = fr.Address(0x10), d = fr.Address(0x50);
    GRef<f32>(off) = I2F(GRef<s16>(def + 0x16));
    GRef<f32>(off + 4) = I2F(GRef<s16>(def + 0x18));
    GRef<u32>(off + 0xC) = 0;
    GRef<f32>(off + 8) = I2F(GRef<s16>(def + 0x1A));
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, par + 0x10);
    sceVu0ApplyMatrix(off, m, off);
    sceVu0AddVector(GU(), off, par);
    u = GU();
    CopyQ(u + 0x20, par + 0x20);
    const f32 py = F(par + 0x14);
    const f32 k = F(0x0040E318);
    const f32 base = I2F(GRef<s16>(def + 0x26)) * k;
    if (GRef<u8>(u + 0x62) != 0xFF) {
        sceVu0SubVector(d, Unit(GRef<u8>(u + 0x62)), GU());
        f32 el = ee::asinf(-F(GS() + 0x34) / F(GS() + 0x84));
        f32 az = ee::atan2f(F(d), F(d + 8));
        const u32 s = GS();
        az = fpu::Sub(az, fpu::Add(py, base / 180.0f));
        if (k < az) az = fpu::Sub(az, F(0x0040E31C));
        if (az < F(0x0040E320)) az = fpu::Add(az, F(0x0040E324));
        const f32 elo = I2F(GRef<s16>(def + 0x20)) * k / 180.0f;
        if (el < elo) {
            el = elo;
        } else {
            const f32 ehi = I2F(GRef<s16>(def + 0x22)) * k / 180.0f;
            if (ehi < el) el = ehi;
        }
        const f32 alo = Deg(GRef<s16>(def + 0x1C), F(0x0040E328));
        if (az < alo) {
            az = alo;
        } else {
            const f32 ahi = Deg(GRef<s16>(def + 0x1E), F(0x0040E328));
            if (ahi < az) az = ahi;
        }
        const f32 rate = Deg(GRef<s16>(def + 0x24), F(0x0040E32C)) * F(0x0040E330);
        const f32 de = fpu::Sub(el, F(s + 0x20));
        GRef<f32>(s + 0x88) = de;
        GRef<f32>(s + 0x20) = Slew(F(s + 0x20), de, rate);
        if (F(0x0040E334) < F(s + 0x20)) GRef<f32>(s + 0x20) = F(0x0040E334);
        if (F(s + 0x20) < F(0x0040E338)) GRef<u32>(s + 0x20) = W(0x0040E338);
        const f32 da = fpu::Sub(az, F(s + 0x90));
        GRef<f32>(s + 0x8C) = da;
        if (0x167 < static_cast<s32>(GRef<s16>(def + 0x1E)) - static_cast<s32>(GRef<s16>(def + 0x1C))) {
            if (F(0x0040E33C) < da)
                GRef<f32>(s + 0x8C) = fpu::Sub(da, F(0x0040E340));
            else if (da < F(0x0040E344))
                GRef<f32>(s + 0x8C) = fpu::Add(da, F(0x0040E348));
        }
        GRef<f32>(s + 0x90) = Slew(F(s + 0x90), F(s + 0x8C), rate);
        GRef<u32>(s + 0x24) = 0;
        CopyPosToAimOrigin();
        if ((W(GU() + 0x4C) & 0x20000000u) != 0)
            func_00145D08(fpu::Add(F(GS() + 0x20) / F(0x0040E34C), 0.5f), UnitModels(), GRef<u8>(GU() + 0x55), 2);
    }
    u = GU();
    CopyQ(u + 0x10, par + 0x10);
    const f32 y = fpu::Add(fpu::Add(F(u + 0x14), Deg(GRef<s16>(def + 0x26), F(0x0040E350))), F(GS() + 0x90));
    GRef<f32>(u + 0x14) = y;
    if (F(0x0040E350) < y) GRef<f32>(u + 0x14) = fpu::Sub(y, F(0x0040E354));
    if (F(u + 0x14) < F(0x0040E358)) GRef<f32>(u + 0x14) = fpu::Add(F(u + 0x14), F(0x0040E35C));
}

// EE: 0x00133228
// Kind 7 (tanks): wrecks keep falling (impulse +0x30 * C, gravity, ground hit flag 0x80000000);
// model 0x47 animates its track 2 by speed (the horizontal/vertical speed compare is done in
// double precision by the original: exact as a float compare); movement by command (wander:
// 2 random reads per new point), heading and bank slew, speed toward the command/definition
// speed by cosf of the turn, braking when not moving; aim angles +0x88/+0x8C without slew.
// fpucheck: 20 static adds
void func_00133228() {
    u32 u = GU();
    const u32 def = W(u + 0x40);
    guest::GuestFrame fr(0x60);
    const u32 d = fr.Address(0), tmp = fr.Address(0x10), hit = fr.Address(0x20), attr = fr.Address(0x30), nrm = fr.Address(0x40);
    if ((~W(u + 0x4C) & 0x1000000u) != 0) {
        if ((W(u + 0x4C) & 0x20000000u) == 0) return;
        sceVu0ScaleVector(tmp, u + 0x30, F(0x0040E360));
        sceVu0AddVector(GU() + 0x20, GU() + 0x20, tmp);
        u = GU();
        ZeroQ(u + 0x30);
        GRef<f32>(u + 0x24) = fpu::Add(F(u + 0x24), F(0x0040E364));
        sceVu0AddVector(tmp, u, u + 0x20);
        if (func_0013D698(Terrain(), hit, attr, GU(), tmp, nrm) != 1) return;
        GRef<u32>(GU() + 0x4C) |= 0x80000000u;
        return;
    }
    if (GRef<u8>(u + 0x69) == 0x47) {
        f32 tr = func_00145DA0(UnitModels(), GRef<u8>(u + 0x55), 2);
        const f32 hs = func_00104FA8(GU() + 0x20);
        const f32 vy = F(GU() + 0x24);
        const f32 avy = vy < 0.0f ? -vy : vy;
        if (avy < hs) {
            tr = fpu::Add(tr, F(0x0040E368));
            if (1.0f < tr) tr = 1.0f;
        } else {
            tr = fpu::Sub(tr, F(0x0040E36C));
            if (tr < 0.0f) tr = 0.0f;
        }
        func_00145D08(tr, UnitModels(), GRef<u8>(GU() + 0x55), 2);
    }
    bool mv = false;
    u32 s = GS();
    switch (GRef<u8>(s + 0x43)) {
        case 0:
        case 4: {
            mv = (GRef<u16>(s + 2) & 1) == 0;
            if (F(GS() + 0x7C) * F(0x0040E370) <= func_00104F10(GU(), GS() + 0x70)) {
                sceVu0SubVector(d, GS() + 0x70, GU());
                GRef<u32>(d + 0xC) = 0;
                GRef<f32>(d + 4) = 0.0f;
            } else if (GRef<u8>(GU() + 0x62) == 0xFF) {
                const u32 per = static_cast<u32>(GRef<u8>(GU() + 0x55)) + 300;
                if (W(Scene() + 0x165C) % per == 0) {
                    const f32 r1 = NextRandomFloat();
                    GRef<f32>(d + 4) = 0.0f;
                    GRef<f32>(d) = fpu::Sub(r1, 0.5f) * 4096.0f;
                    const f32 r2 = NextRandomFloat();
                    GRef<f32>(d + 8) = fpu::Sub(r2, 0.5f) * 4096.0f;
                    sceVu0AddVector(GS() + 0x60, d, GU());
                } else {
                    sceVu0SubVector(d, GS() + 0x60, GU());
                }
                GRef<u32>(d + 0xC) = 0;
                GRef<f32>(d + 4) = 0.0f;
            } else {
                sceVu0SubVector(d, Unit(GRef<u8>(GU() + 0x62)), GU());
                const f32 len = GetVectorLength(d);
                if (1024.0f < len) {
                    sceVu0ScaleVector(d, d, fpu::Sub(len, 1024.0f) / len);
                    GRef<f32>(d + 4) = 0.0f;
                } else {
                    mv = false;
                    GRef<f32>(d + 4) = 0.0f;
                }
            }
            break;
        }
        case 1:
            if (GRef<u8>(s + 0x44) != 1 || (GRef<u16>(s + 2) & 1) == 0) {
                mv = true;
                sceVu0SubVector(d, s + 0x60, GU());
                GRef<u32>(d + 0xC) = 0;
            }
            break;
        case 2: {
            const u32 lead = Unit(GRef<u8>(s + 0x47));
            if (GetDistanceXZ(lead, GU()) < F(GS() + 0x7C)) {
                mv = (GRef<u16>(GS() + 2) & 1) == 0;
                if (GRef<u8>(GU() + 0x62) != 0xFF) {
                    sceVu0SubVector(d, Unit(GRef<u8>(GU() + 0x62)), GU());
                    GRef<f32>(d + 4) = 0.0f;
                } else {
                    mv = false;
                }
            } else {
                mv = true;
                sceVu0SubVector(d, lead, GU());
                GRef<f32>(d + 4) = 0.0f;
                GRef<u32>(d + 0xC) = 0;
            }
            break;
        }
        default: break;
    }
    if (mv) {
        u = GU();
        f32 yd = fpu::Sub(ee::atan2f(F(d), F(d + 8)), F(u + 0x14));
        if (F(0x0040E374) < yd) yd = fpu::Sub(yd, F(0x0040E378));
        if (yd < F(0x0040E37C)) yd = fpu::Add(yd, F(0x0040E380));
        const f32 rmax = I2F(GRef<s16>(def + 0x16)) * F(0x0040E384) * F(0x0040E374) / 180.0f;
        f32 step = rmax;
        if (yd <= rmax) {
            step = -rmax;
            if (-rmax <= yd) step = yd;
        }
        const f32 bank = F(u + 0x18);
        const f32 bt = step * F(0x0040E388) / rmax;
        GRef<f32>(u + 0x14) = fpu::Add(F(u + 0x14), step);
        if (bank < bt)
            GRef<f32>(u + 0x18) = fpu::Add(bank, F(0x0040E38C));
        else if (bt < bank)
            GRef<f32>(u + 0x18) = fpu::Sub(bank, F(0x0040E390));
        f32 base, mul;
        if (GRef<u8>(GS() + 0x43) == 1 && GRef<s32>(GS() + 0x5C) != 0) {
            base = I2F(GRef<s32>(GS() + 0x5C));
            mul = F(0x0040E394);
        } else {
            base = I2F(GRef<s16>(def + 0x14));
            mul = F(0x0040E398);
        }
        const f32 sp = base * mul;
        f32 tv = sp * ee::cosf(yd);
        if (tv < 0.0f) tv = 0.0f;
        s = GS();
        const f32 cur = F(s + 0x90);
        if (cur < tv)
            GRef<f32>(s + 0x90) = fpu::Add(cur, sp * F(0x0040E39C) * 0.5f);
        else if (tv < cur)
            GRef<f32>(s + 0x90) = fpu::Sub(cur, sp * F(0x0040E3A0) * 0.5f);
        const f32 len = GetVectorLength(d);
        if (len == 0.0f) {
            mv = false;
        } else {
            const f32 h = func_00104FA8(d);
            u = GU();
            const f32 hs = F(GS() + 0x90) * h / len;
            GRef<f32>(u + 0x20) = ee::sinf(F(u + 0x14)) * hs;
            GRef<f32>(u + 0x24) = F(GS() + 0x90) * F(d + 4) / len;
            GRef<f32>(u + 0x28) = ee::cosf(F(u + 0x14)) * hs;
        }
    }
    if (!mv) {
        const f32 k = F(0x0040E3A4);
        const f32 sp = GetVectorLength(GU() + 0x20);
        f32 n = fpu::Sub(sp, I2F(GRef<s16>(def + 0x14)) * k * k * 0.5f);
        if (n < 0.0f) n = 0.0f;
        sceVu0ScaleVector(GU() + 0x20, GU() + 0x20, fpu::Div(n, sp));
        u = GU();
        GRef<f32>(u + 0x18) = fpu::Sub(F(u + 0x18), F(u + 0x18) * k);
    }
    u = GU();
    if (GRef<s8>(u + 0x62) != -1) {
        const f32 el = ee::asinf(-F(GS() + 0x34) / F(GS() + 0x84));
        const f32 az = ee::atan2f(F(GS() + 0x30), F(GS() + 0x38));
        s = GS();
        GRef<u32>(s + 0x20) = 0;
        GRef<u32>(s + 0x24) = 0;
        const f32 z0 = F(s + 0x20);
        GRef<f32>(s + 0x88) = fpu::Sub(el, z0);
        if (F(0x0040E3A8) < F(s + 0x88)) GRef<f32>(s + 0x88) = F(0x0040E3A8);
        if (F(s + 0x88) < F(0x0040E3AC)) GRef<u32>(s + 0x88) = W(0x0040E3AC);
        const f32 da = fpu::Sub(az, F(s + 0x24));
        GRef<f32>(s + 0x8C) = da;
        if (F(0x0040E3B0) < da) GRef<f32>(s + 0x8C) = fpu::Sub(da, F(0x0040E3B4));
        if (F(s + 0x8C) < F(0x0040E3B8)) GRef<f32>(s + 0x8C) = fpu::Add(F(s + 0x8C), F(0x0040E3BC));
        GRef<f32>(s + 0x2C) = z0;
        GRef<f32>(s + 0x28) = z0;
    }
    CopyQ(GS() + 0x10, GU());
}

// EE: 0x00133AB8
// Boss-sub torpedo slot timers (gp 0x00410674: 2 timers, slot ids +8).
// fpucheck: 1 add
void func_00133AB8() {
    const f32 k = F(0x0040E3C0);
    for (u32 i = 0; i < 2; ++i) {
        const u32 t = W(0x00410674) + 4 * i;
        if (0.0f < F(t)) {
            const f32 v = fpu::Sub(F(t), k);
            GRef<f32>(t) = v;
            if (v <= 0.0f) {
                GRef<f32>(t) = 0.0f;
                GRef<u8>(W(0x00410674) + i + 8) = 0xFF;
            }
        }
    }
}

// EE: 0x00133B20
// Reserves a boss-sub launch slot for unit id; 1 when it holds one (timer 5.0).
s32 func_00133B20(s8 id) {
    for (u32 i = 0; i < 2; ++i)
        if (GRef<s8>(W(0x00410674) + 8 + i) == id) return 1;
    for (u32 i = 0; i < 2; ++i) {
        const u32 c = W(0x00410674) + 8 + i;
        if (GRef<s8>(c) == -1) {
            GRef<s8>(c) = id;
            GRef<u32>(W(0x00410674) + 4 * i) = 0x40A00000u;
            return 1;
        }
    }
    return 0;
}

// EE: 0x00133BB0
// Kind 8 (the submarine boss parts): hp kept in state +0xC4 (unit hp pinned to 9999), the
// raise/launch cycle (+0x90 timer, hatch +0x94) with the launch slot (0x00133B20), special
// weapon launch from model node 4 (0x0014F2A0 kind 2, sound 10, replay event) and a splash
// (node 3: 0x001CF2E8 + replay event), then aim (+0xB0/+0xB4 remaining) slewed by
// definition +0x1C; model tracks 1-3.
// fpucheck: 23 static adds
void func_00133BB0() {
    u32 u = GU(), s = GS();
    const u32 def = W(u + 0x40);
    const s16 hp = GRef<s16>(u + 0x5E);
    if (hp != 9999) {
        GRef<s16>(u + 0x5E) = 9999;
        GRef<s32>(s + 0xC4) = hp;
    }
    if (GRef<s32>(s + 0xC4) == 0) GRef<u16>(u + 0x5E) = 0;
    if ((~W(u + 0x4C) & 0x1000000u) != 0) return;
    s32 lo_deg;
    const f32 t90 = F(s + 0x90);
    bool slew = false;
    if (t90 < 10.0f) {
        GRef<f32>(s + 0x90) = fpu::Add(t90, F(0x0040E3C4));
        if (F(s + 0x94) != 0.0f) GRef<f32>(s + 0x94) = fpu::Add(F(s + 0x94), F(0x0040E3C8));
        if (1.0f < F(s + 0x94)) GRef<u32>(s + 0x94) = 0;
    } else if (F(s + 0xB0) != 0.0f || F(s + 0xB4) != 0.0f) {
        slew = true;
    } else if (t90 < 12.0f) {
        GRef<f32>(s + 0x90) = fpu::Add(t90, F(0x0040E3CC));
    } else if ((GRef<u16>(s + 2) & 0x200) == 0) {
        if (func_00133B20(GRef<s8>(u + 0x55)) == 1) Or16(GS() + 2, 0x200);
    } else {
        f32 h = F(s + 0x94);
        if (F(0x0040E3D0) <= h && h < F(0x0040E3D4)) {
            GRef<u16>(s + 2) = static_cast<u16>(GRef<u16>(s + 2) & 0xFDFF);
            if ((StatusFlags() & 0x100u) != 0) {
                guest::GuestFrame f(0xC0);
                const u32 p = f.Address(0), ang = f.Address(0x10), nm = f.Address(0x20), m = f.Address(0x60), q = f.Address(0xA0),
                          a2 = f.Address(0xB0);
                GRef<u32>(ang) = W(GS() + 0x20);
                GRef<u32>(ang + 4) = W(GU() + 0x14);
                StoreVf0(p);
                func_00146680(UnitModels(), nm, GRef<u8>(GU() + 0x55), 4);
                sceVu0ApplyMatrix(p, nm, p);
                sceVu0UnitMatrix(m);
                func_00102D28(m, m, GU() + 0x10);
                sceVu0ApplyMatrix(p, m, p);
                sceVu0AddVector(p, GU(), p);
                GRef<f32>(GU() + 0xC) = 1.0f;
                FireCannonShell(GRef<u8>(GU() + 0x55), GRef<u8>(GU() + 0x62), p, ang, 2);
                PlayEventSe(10, p);
                func_00171158(3, 0, 10, p, 0);
                StoreVf0(q);
                func_00146680(UnitModels(), nm, GRef<u8>(GU() + 0x55), 3);
                sceVu0ApplyMatrix(q, nm, q);
                sceVu0UnitMatrix(m);
                func_00102D28(m, m, GU() + 0x10);
                sceVu0ApplyMatrix(q, m, q);
                sceVu0AddVector(q, GU(), q);
                GRef<u32>(a2 + 8) = W(ang + 8);
                GRef<u32>(a2 + 0xC) = W(ang + 0xC);
                f32 y = fpu::Add(F(ang + 4), F(0x0040E3D8));
                GRef<f32>(a2) = -F(ang);
                if (F(0x0040E3D8) < y) y = fpu::Sub(y, F(0x0040E3DC));
                GRef<f32>(a2 + 4) = y;
                func_001CF2E8(q, a2);
                func_00170B60(10, 0xFF, 0, q, a2);
            }
            u = GU();
            s = GS();
            GRef<u32>(s + 0x90) = 0;
            if (GRef<s8>(u + 0x62) == -1) {
                GRef<u32>(s + 0xB0) = W(0x0040E3E4);
                GRef<u32>(s + 0xB4) = W(0x0040E3E8);
            } else {
                const f32 el = ee::asinf(-F(s + 0x34) / F(s + 0x84));
                GRef<f32>(GS() + 0xB0) = el;
                if (el < 0.0f)
                    GRef<f32>(GS() + 0xB0) = 0.0f;
                else if (F(0x0040E3E0) < el)
                    GRef<f32>(GS() + 0xB0) = F(0x0040E3E0);
                GRef<f32>(GS() + 0xB4) = ee::atan2f(F(GS() + 0x30), F(GS() + 0x38));
                s = GS();
                u = GU();
            }
            GRef<f32>(s + 0xB0) = fpu::Sub(F(s + 0xB0), F(s + 0x20));
            const f32 dy = fpu::Sub(F(s + 0xB4), F(u + 0x14));
            GRef<f32>(s + 0xB4) = dy;
            if (F(0x0040E3EC) < dy) GRef<f32>(s + 0xB4) = fpu::Sub(dy, F(0x0040E3F0));
            if (F(s + 0xB4) < F(0x0040E3F4)) GRef<f32>(s + 0xB4) = fpu::Add(F(s + 0xB4), F(0x0040E3F8));
            h = F(s + 0x94);
        }
        GRef<f32>(s + 0x94) = fpu::Add(h, F(0x0040E3FC));
    }
    if (slew) {
        s = GS();
        u = GU();
        const f32 rate = Deg(GRef<s16>(def + 0x1C), F(0x0040E400)) * F(0x0040E404);
        const f32 e = F(s + 0xB0);
        if (rate < e) {
            GRef<f32>(s + 0x20) = fpu::Add(F(s + 0x20), rate);
            GRef<f32>(s + 0xB0) = fpu::Sub(e, rate);
        } else if (e < -rate) {
            GRef<f32>(s + 0x20) = fpu::Sub(F(s + 0x20), rate);
            GRef<f32>(s + 0xB0) = fpu::Add(e, rate);
        } else {
            GRef<u32>(s + 0xB0) = 0;
            GRef<f32>(s + 0x20) = fpu::Add(F(s + 0x20), e);
        }
        if (F(0x0040E408) < F(s + 0x20)) GRef<f32>(s + 0x20) = F(0x0040E408);
        if (F(s + 0x20) < F(0x0040E40C)) GRef<u32>(s + 0x20) = W(0x0040E40C);
        const f32 b = F(s + 0xB4);
        if (rate < b) {
            GRef<f32>(u + 0x14) = fpu::Add(F(u + 0x14), rate);
            GRef<f32>(s + 0xB4) = fpu::Sub(F(s + 0xB4), rate);
        } else if (b < -rate) {
            GRef<f32>(u + 0x14) = fpu::Sub(F(u + 0x14), rate);
            GRef<f32>(s + 0xB4) = fpu::Add(F(s + 0xB4), rate);
        } else {
            GRef<f32>(u + 0x14) = fpu::Add(F(u + 0x14), b);
            GRef<u32>(s + 0xB4) = 0;
        }
        f32 y = F(u + 0x14);
        if (F(0x0040E410) < y) {
            GRef<f32>(u + 0x14) = fpu::Sub(y, F(0x0040E414));
            y = F(u + 0x14);
        }
        if (y < F(0x0040E418)) GRef<f32>(u + 0x14) = fpu::Add(y, F(0x0040E41C));
    }
    s = GS();
    u = GU();
    lo_deg = GRef<s16>(def + 0x18);
    const f32 k = F(0x0040E420);
    const f32 lo = I2F(lo_deg) * k / 180.0f;
    if (F(s + 0x20) < lo) GRef<f32>(s + 0x20) = lo;
    const f32 hi = I2F(GRef<s16>(def + 0x1A)) * k / 180.0f;
    if (hi < F(s + 0x20)) GRef<f32>(s + 0x20) = hi;
    const f32 t = fpu::Add(F(s + 0x20) / k, 0.5f);
    GRef<f32>(s + 0x98) = t;
    if (t < 0.0f) GRef<u32>(s + 0x98) = 0;
    if (1.0f < F(s + 0x98)) GRef<f32>(s + 0x98) = 1.0f;
    func_00145D08(F(s + 0x98), UnitModels(), GRef<u8>(u + 0x55), 2);
    func_00145D08(F(GS() + 0x98), UnitModels(), GRef<u8>(GU() + 0x55), 1);
    func_00145D08(F(GS() + 0x94), UnitModels(), GRef<u8>(GU() + 0x55), 3);
}

// EE: 0x00134288
// Kind 9 (parts of a parent unit def+0x14): attached parts copy the parent's position and
// rotation (part +0x5A == 1: taking over lockability with collision kinds 0x770000 and the radius
// of the parent); models 0xC6/0xC8 share the parent's hp (+0xC4), 0xC6 hangs 380 lower and 120
// back, 0xC7 sits on node 4 of the parent; destroyed parts detach (+0x5A = 1, sound 13, replay
// event). Once lockable, 0x00135848 animates it.
// fpucheck: 3 static adds
void func_00134288() {
    u32 u = GU();
    const u32 par = Unit(GRef<u8>(W(u + 0x40) + 0x14));
    const u32 ps = W(par + 0x44);
    auto copy_parent = [&]() {
        CopyQ(GU(), par);
        CopyQ(GU() + 0x10, par + 0x10);
    };
    auto detach = [&]() {
        GRef<u8>(GU() + 0x5A) = 1;
        copy_parent();
        const u32 hv = W(ps + 0x98);
        GRef<u32>(GS() + 0x98) = hv;
        GRef<u32>(GS() + 0x94) = W(ps + 0x94);
        func_00145D08(F(GS() + 0x98), UnitModels(), GRef<u8>(GU() + 0x55), 1);
        func_00145D08(F(GS() + 0x98), UnitModels(), GRef<u8>(GU() + 0x55), 2);
        func_00145D08(F(GS() + 0x94), UnitModels(), GRef<u8>(GU() + 0x55), 3);
        PlayEventSe(0xD, GU());
        func_00171158(3, 0, 0xD, GU(), 0);
    };
    if (GRef<u8>(u + 0x5A) == 1) {
        copy_parent();
        u = GU();
        GRef<u32>(par + 0x4C) &= 0xDFFFFFFFu;
        const u32 f = W(u + 0x4C);
        if ((~f & 0x20000000u) != 0) {
            const u32 f48 = W(u + 0x48);
            GRef<u32>(GS() + 0x9C) = 0;
            GRef<u32>(u + 0x4C) = f | 0x20000000u;
            const u32 r = W(par + 0x74);
            GRef<u32>(u + 0x48) = f48 | 0x770000u;
            GRef<u32>(u + 0x74) = r;
            GRef<u32>(u + 0x70) = W(par + 0x74);
        }
    }
    u = GU();
    const u32 f = W(u + 0x4C);
    if ((f & 0x20000000u) != 0) {
        func_00135848(u, GS());
        return;
    }
    const u8 m = GRef<u8>(u + 0x69);
    if (m == 0xC6 || m == 0xC8) {
        const s16 hp = GRef<s16>(u + 0x5E);
        const s32 shared = GRef<s32>(ps + 0xC4);
        if (hp < shared) {
            GRef<s32>(ps + 0xC4) = hp;
            if (hp == 0) detach();
        } else {
            GRef<u16>(u + 0x5E) = static_cast<u16>(shared);
            if (shared == 0) GRef<u32>(u + 0x4C) = f & 0xCEFFFFFFu;
        }
        copy_parent();
        if (m == 0xC6) {
            u = GU();
            GRef<f32>(u + 4) = fpu::Sub(F(u + 4), 380.0f);
            GRef<f32>(u) = fpu::Add(F(u), ee::sinf(F(u + 0x14)) * -120.0f);
            const u32 w = GU();
            GRef<f32>(w + 8) = fpu::Add(F(w + 8), ee::cosf(F(w + 0x14)) * -120.0f);
        }
    } else if (m == 0xC7) {
        if (GRef<s32>(ps + 0xC4) == 0) {
            GRef<u16>(u + 0x5E) = 0;
            GRef<u32>(u + 0x4C) = f & 0xCEFFFFFFu;
        } else if (GRef<s16>(u + 0x5E) == 0) {
            GRef<u32>(ps + 0xC4) = 0;
            detach();
        }
        guest::GuestFrame fr(0x90);
        const u32 p = fr.Address(0), nm = fr.Address(0x10), rm = fr.Address(0x50);
        StoreVf0(p);
        func_00146680(UnitModels(), nm, GRef<u8>(par + 0x55), 4);
        sceVu0ApplyMatrix(p, nm, p);
        sceVu0UnitMatrix(rm);
        func_00102D28(rm, rm, par + 0x10);
        sceVu0ApplyMatrix(p, rm, p);
        sceVu0AddVector(GU(), par, p);
        GRef<f32>(GU() + 0xC) = 1.0f;
        sceVu0MulMatrix(nm, rm, nm);
        func_00103290(GU() + 0x10, nm);
    }
}

// EE: 0x00134810
// Kind 10 (mission specials by model: 0xB4/0xB5 lifts rising with phase 2, 0xB6/0xB7 gates
// sliding by the mission 0xF canyon state, 0x4B the falling satellite parts (time-based
// descent), 0x4C the dropping container on the shared schedule 0x00410670).
// fpucheck: 20 static adds
void func_00134810() {
    u32 u = GU(), s = GS();
    const u8 m = GRef<u8>(u + 0x69);
    const u32 plc = static_cast<u32>(GRef<u8>(u + 0x55)) * 0x18 + W(Setup() + 4);
    if (m == 0xB4) {
        if ((StatusFlags() & 4u) == 0)
            GRef<u32>(s + 0x90) = 0;
        else if (F(s + 0x90) < 1.0f)
            GRef<f32>(s + 0x90) = fpu::Add(F(s + 0x90), F(0x0040E424));
        GRef<f32>(u + 4) = fpu::Add(F(plc + 8), F(s + 0x90) * 372.0f);
        GRef<f32>(u + 8) = fpu::Sub(F(plc + 0xC), F(s + 0x90) * F(0x0040E428));
        return;
    }
    if (m == 0xB5) {
        if ((StatusFlags() & 4u) == 0)
            GRef<u32>(s + 0x90) = 0;
        else if (F(s + 0x90) < 1.0f)
            GRef<f32>(s + 0x90) = fpu::Add(F(s + 0x90), F(0x0040E42C));
        if (1.0f < F(s + 0x90)) GRef<f32>(s + 0x90) = 1.0f;
        GRef<f32>(u + 4) = fpu::Add(F(plc + 8), F(s + 0x90) * 198.0f);
        return;
    }
    if (m == 0xB6 || m == 0xB7) {
        if ((W(u + 0x4C) & 0x1000u) != 0 && GRef<u8>(W(Unit(0) + 0x44) + 0x183) == 3) {
            GRef<f32>(s + 0x90) =
                fpu::Sub(1.0f, F(static_cast<u32>(GRef<u8>(u + 0x55)) * 0xC + W(0x00410648) + 4) * 0.00024414062f);
        } else if ((~StatusFlags() & 0x40000u) != 0 && 0.0f < F(s + 0x90)) {
            GRef<f32>(s + 0x90) = fpu::Sub(F(s + 0x90), F(m == 0xB6 ? 0x0040E430 : 0x0040E434));
        }
        if (F(s + 0x90) < 0.0f) GRef<u32>(s + 0x90) = 0;
        if (m == 0xB6)
            GRef<f32>(u) = fpu::Sub(F(plc + 4), F(s + 0x90) * 194.0f);
        else
            GRef<f32>(u) = fpu::Add(F(plc + 4), F(s + 0x90) * 191.0f);
        return;
    }
    if (m == 0x4A) return;
    if (m == 0x4B) {
        GRef<u8>(u + 0x9B) = 0;
        if ((StatusFlags() & 4u) == 0) {
            GRef<u32>(s + 0x90) = 0;
            GRef<s32>(u + 0x7C) = -1;
            return;
        }
        const f32 x = F(u);
        const f32 t = fpu::UnsignedToFloat(W(Status() + 0x24));
        f32 lim, v;
        if (F(0x0040E438) < x) {
            lim = 20.0f;
            v = t * F(0x0040E43C);
        } else if (F(0x0040E440) < x) {
            lim = 45.0f;
            v = t * F(0x0040E444);
        } else if (8000.0f < x) {
            lim = 50.0f;
            v = t * F(0x0040E448);
        } else {
            lim = 35.0f;
            v = t * F(0x0040E44C);
        }
        if (lim < v && F(s + 0x90) == 0.0f) GRef<u32>(s + 0x90) = W(0x0040E450);
        f32 sp = F(s + 0x90);
        if (sp == 0.0f) {
            GRef<s32>(u + 0x7C) = -1;
            return;
        }
        if (sp < 0.5f) GRef<f32>(s + 0x90) = fpu::Add(sp, F(0x0040E454));
        if (-100.0f < F(u + 0x24)) GRef<f32>(u + 0x24) = fpu::Sub(F(u + 0x24), F(s + 0x90));
        GRef<u8>(u + 0x9B) = 7;
        Or16(u + 0x56, 0x400);
        if (F(GU() + 4) < -131072.0f || F(0x0040E458) < func_00104F10(Unit(0), GU()))
            GRef<u32>(GU() + 0x4C) &= 0xCEFFFFFFu;
        return;
    }
    if (m != 0x4C) return;
    GRef<u8>(u + 0x9B) = 0;
    const u32 sch = W(0x00410670);
    if (((static_cast<s32>(GRef<s8>(sch + 0x45)) >> (static_cast<s32>(GRef<s8>(sch + 0x44)) & 0x1F)) & 1) != 0)
        Or16(s + 2, 0x1000);
    if (F(0x0040E45C) < F(static_cast<u32>(GRef<u8>(u + 0x55)) * 0xC + W(0x00410648) + 4)) {
        GRef<u16>(s + 2) = static_cast<u16>(GRef<u16>(s + 2) & 0xEFFF);
        ZeroQ(u + 0x20);
        GRef<u32>(GS() + 0x94) = 0;
        GRef<u32>(GS() + 0x90) = 0;
        func_0011BD10(GU());
        u = GU();
        s = GS();
        GRef<s32>(u + 0x7C) = -1;
        GRef<u16>(u + 0x56) = static_cast<u16>(GRef<u16>(u + 0x56) & 0xF7FF);
    }
    if ((GRef<u16>(s + 2) & 0x1000) == 0) {
        GRef<s32>(u + 0x7C) = -1;
    } else {
        const bool low = F(u + 4) < F(0x0040E460);
        GRef<u32>(u + 0x14) = W(0x0040E464);
        if (low && F(0x0040E468) < F(u + 0x10)) GRef<f32>(u + 0x10) = fpu::Sub(F(u + 0x10), F(0x0040E46C));
        if (F(s + 0x94) < 0.5f) GRef<f32>(s + 0x94) = fpu::Add(F(s + 0x94), F(0x0040E470));
        GRef<f32>(s + 0x90) = fpu::Add(F(s + 0x90), F(s + 0x94));
        guest::GuestFrame fr(0x50);
        const u32 v = fr.Address(0), mm = fr.Address(0x10);
        ZeroQ(v);
        GRef<f32>(v + 4) = -F(GS() + 0x90);
        sceVu0UnitMatrix(mm);
        func_00102D28(mm, mm, GU() + 0x10);
        sceVu0ApplyMatrix(GU() + 0x20, mm, v);
        u = GU();
        GRef<u8>(u + 0x9B) = 6;
        Or16(u + 0x56, 0x800);
    }
    GRef<s8>(W(0x00410670) + 0x44) = static_cast<s8>(GRef<s8>(W(0x00410670) + 0x44) + 1);
}

// EE: 0x00135008
// The dropping-container schedule of model 0x4C (gp 0x00410670): every 300 frames three new drops
// (ids from table 0x002A2618 by frame & 15, random positions: 3 random reads each), then three
// shells with trail emitters 0x001FF6C0 and replay events for 120 frames, then the drop bits
// +0x45 for the ids at 60/120/180 frames.
// fpucheck: 7 static adds
void func_00135008() {
    u32 p = W(0x00410670);
    if ((StatusFlags() & 0x10000000u) != 0) return;
    GRef<u8>(p + 0x45) = 0;
    GRef<s32>(p + 0x40) += 1;
    const u8 st = GRef<u8>(p + 0x3C);
    if (st == 0) {
        if (GRef<s32>(p + 0x40) == 300) {
            GRef<u8>(p + 0x3C) = 1;
            GRef<u32>(p + 0x40) = 0;
            const f32 kx = F(0x0040E474), ky = F(0x0040E478);
            u32 src = 0x002A2618 + (GRef<u8>(Scene() + 0x165C) & 0xF);
            for (u32 k = 0; k < 3; ++k, ++src) {
                GRef<u8>(p + k + 0x3D) = GRef<u8>(src);
                p = W(0x00410670);
                GRef<s32>(p + 0x30 + 4 * k) = -1;
                ZeroQ(p);
                GRef<f32>(W(0x00410670) + 0x10 * k) = NextRandomFloat() * kx;
                GRef<f32>(W(0x00410670) + 0x10 * k + 8) = -NextRandomFloat() * kx;
                p = W(0x00410670);
                GRef<f32>(p + 0x10 * k + 4) = fpu::Sub(-NextRandomFloat() * ky, 2000.0f);
            }
        }
        GRef<u8>(W(0x00410670) + 0x44) = 0;
        return;
    }
    if (st == 1) {
        s32 n = GRef<s32>(p + 0x40) / 0x12;
        if (3 < n) n = 3;
        for (s32 k = 0; k < n; ++k) {
            p = W(0x00410670);
            guest::GuestFrame f(0x10);
            const u32 a = f.Address();
            const s8 code = GRef<s8>(p + 0x3D + static_cast<u32>(k));
            StoreVf0(a);
            GRef<u32>(a) = W(0x00410668 + 4u * static_cast<u32>(code / 3));
            GRef<f32>(a + 4) = -260.0f;
            GRef<u32>(a + 8) = W(0x002A2608 + 4u * static_cast<u32>(code % 3));
            const u32 q = p + 0x10u * static_cast<u32>(k);
            const f32 c = F(0x0040E47C), g = F(0x0040E480);
            const f32 ny = fpu::Sub(fpu::Add(F(q + 4), 8000.0f) * g, 8000.0f);
            const f32 nz = fpu::Sub(fpu::Add(F(q + 8), c) * g, c);
            GRef<f32>(q) = fpu::Add(fpu::Sub(F(q), c) * g, c);
            GRef<f32>(q + 8) = nz;
            GRef<f32>(q + 4) = ny;
            const u32 h = func_001FF6C0(W(p + 0x30 + 4u * static_cast<u32>(k)), a, q, 10);
            GRef<u32>(W(0x00410670) + 0x30 + 4u * static_cast<u32>(k)) = h;
            func_00170B60(0xB, static_cast<u8>(h), 10, a, W(0x00410670) + 0x10u * static_cast<u32>(k));
        }
        p = W(0x00410670);
        if (GRef<s32>(p + 0x40) < 0x79) {
            GRef<u8>(p + 0x44) = 0;
            return;
        }
        GRef<u8>(p + 0x3C) = 2;
        GRef<u32>(p + 0x40) = 0;
        GRef<u8>(p + 0x44) = 0;
        return;
    }
    if (st != 2) {
        GRef<u8>(p + 0x44) = 0;
        return;
    }
    const s32 t = GRef<s32>(p + 0x40);
    s8 id;
    if (t == 0x3C)
        id = GRef<s8>(p + 0x3D);
    else if (t == 0x78)
        id = GRef<s8>(p + 0x3E);
    else if (t == 0xB4) {
        GRef<u8>(p + 0x45) = static_cast<u8>(GRef<u8>(p + 0x45) | (1u << (static_cast<s32>(GRef<s8>(p + 0x3F)) & 0x1F)));
        GRef<u8>(p + 0x3C) = 0;
        GRef<u32>(p + 0x40) = 0;
        GRef<u8>(p + 0x44) = 0;
        return;
    } else {
        GRef<u8>(p + 0x44) = 0;
        return;
    }
    GRef<u8>(p + 0x45) = static_cast<u8>(GRef<u8>(p + 0x45) | (1u << (static_cast<s32>(id) & 0x1F)));
    GRef<u8>(p + 0x44) = 0;
}

// EE: 0x00135428
// Whether t lies in [a, a + w].
// fpucheck: 1 add
s32 func_00135428(f32 t, f32 a, f32 w) { return (a <= t && t <= fpu::Add(a, w)) ? 1 : 0; }

// EE: 0x00135458
// Part animation +0xC0 of a unit: + C 0x0040E488 (models 0xD3, 0xD4, 0xD7, 0xDE, 0xDF) or
// C 0x0040E484, capped at 1.
// fpucheck: 1 add
void func_00135458(u32 u, u32 s) {
    const u8 m = GRef<u8>(u + 0x69);
    const f32 k = (m == 0xD3 || m == 0xD4 || m == 0xD7 || m == 0xDE || m == 0xDF) ? F(0x0040E488) : F(0x0040E484);
    const f32 v = fpu::Add(F(s + 0xC0), k);
    GRef<f32>(s + 0xC0) = v;
    if (1.0f < v) GRef<f32>(s + 0xC0) = 1.0f;
}

// EE: 0x001354C0
// Model 0x72 part animation with a dust burst (0x0018EBE8 kind 1 + replay event) 300 ahead at
// animation time C 0x0040E48C (mission 0x15: C 0x0040E490).
// fpucheck: 3 static adds
void func_001354C0(u32 u, u32 s) {
    const f32 at = Game(0x27) == 0x15 ? F(0x0040E490) : F(0x0040E48C);
    const f32 k = F(0x0040E494);
    if (func_00135428(at, F(s + 0xC0), k) == 1) {
        guest::GuestFrame f(0x10);
        const u32 p = f.Address();
        CopyQ(p, u);
        GRef<f32>(p + 4) = 0.0f;
        GRef<f32>(p) = fpu::Add(F(p), ee::sinf(F(u + 0x14)) * 300.0f);
        GRef<f32>(p + 8) = fpu::Add(F(p + 8), ee::cosf(F(u + 0x14)) * 300.0f);
        func_0018EBE8(1, p);
        func_00170AA0(0xF, 0xFF, 1, p);
    }
    const f32 v = fpu::Add(F(s + 0xC0), k);
    GRef<f32>(s + 0xC0) = v;
    if (1.0f < v) GRef<f32>(s + 0xC0) = 1.0f;
}

// EE: 0x001355B8
// Model 0x7D part animation with a dust burst to the side (heading + C 0x0040E4A0, wrapped).
// fpucheck: 5 static adds
void func_001355B8(u32 u, u32 s) {
    const f32 k = F(0x0040E49C);
    if (func_00135428(F(0x0040E498), F(s + 0xC0), k) == 1) {
        f32 a = fpu::Add(F(u + 0x14), F(0x0040E4A0));
        if (F(0x0040E4A4) < a) a = fpu::Sub(a, F(0x0040E4A8));
        guest::GuestFrame f(0x10);
        const u32 p = f.Address();
        CopyQ(p, u);
        GRef<f32>(p) = fpu::Add(F(p), ee::sinf(a) * 300.0f);
        GRef<f32>(p + 8) = fpu::Add(F(p + 8), ee::cosf(a) * 300.0f);
        func_0018EBE8(0, p);
        func_00170AA0(0xF, 0xFF, 0, p);
    }
    const f32 v = fpu::Add(F(s + 0xC0), k);
    GRef<f32>(s + 0xC0) = v;
    if (1.0f < v) GRef<f32>(s + 0xC0) = 1.0f;
}

// EE: 0x001356C8
// Model 0x84 part animation with four dust bursts along z at four animation times.
// fpucheck: 5 static adds
void func_001356C8(u32 u, u32 s) {
    const f32 k = F(0x0040E4AC);
    guest::GuestFrame f(0x10);
    const u32 p = f.Address();
    CopyQ(p, u);
    GRef<u32>(p + 4) = 0;
    const u32 times[4] = {0x0040E4B0, 0x0040E4B8, 0x0040E4C0, 0x0040E4C8};
    for (u32 j = 0; j < 4; ++j) {
        if (func_00135428(F(times[j]), F(s + 0xC0), k) != 1) continue;
        if (j == 0)
            GRef<f32>(p + 8) = fpu::Sub(F(p + 8), F(0x0040E4B4));
        else if (j == 1)
            GRef<f32>(p + 8) = fpu::Sub(F(p + 8), F(0x0040E4BC));
        else if (j == 2)
            GRef<f32>(p + 8) = fpu::Add(F(p + 8), F(0x0040E4C4));
        else
            GRef<f32>(p + 8) = fpu::Add(F(p + 8), F(0x0040E4CC));
        func_0018EBE8(1, p);
        func_00170AA0(0xF, 0xFF, 1, p);
    }
    const f32 v = fpu::Add(F(s + 0xC0), k);
    GRef<f32>(s + 0xC0) = v;
    if (1.0f < v) GRef<f32>(s + 0xC0) = 1.0f;
}

// EE: 0x00135848
// Detached boss part: rotor/arm wind-down (+0x98 falling by an accelerating step +0x9C with a
// bounce at C 0x0040E4D0), a dust burst 1200 ahead with sound 14 at C 0x0040E4E0, hatch +0x94
// closing; model tracks 1-3.
// fpucheck: 6 static adds
void func_00135848(u32 u, u32 s) {
    const f32 lo = F(0x0040E4D0);
    f32 h;
    if (lo < F(s + 0x98)) {
        const f32 st = fpu::Add(F(s + 0x9C), F(0x0040E4D4));
        const f32 r = fpu::Sub(F(s + 0x98), st);
        GRef<f32>(s + 0x9C) = st;
        GRef<f32>(s + 0x98) = r;
        if (r <= lo) {
            GRef<f32>(s + 0x98) = lo;
            if (F(0x0040E4D8) < st) {
                const f32 b = st * F(0x0040E4DC);
                GRef<f32>(s + 0x9C) = b;
                GRef<f32>(s + 0x98) = fpu::Sub(lo, b);
            }
        }
        if (func_00135428(F(0x0040E4E0), F(s + 0x98), F(s + 0x9C)) == 1) {
            guest::GuestFrame f(0x10);
            const u32 p = f.Address();
            CopyQ(p, u);
            GRef<f32>(p) = fpu::Add(F(p), ee::sinf(F(u + 0x14)) * 1200.0f);
            GRef<f32>(p + 8) = fpu::Add(F(p + 8), ee::cosf(F(u + 0x14)) * 1200.0f);
            func_0018EBE8(0, p);
            func_00170AA0(0xF, 0xFF, 0, p);
            PlayEventSe(0xE, p);
        }
        h = F(s + 0x94);
    } else {
        GRef<f32>(s + 0x98) = lo;
        h = F(s + 0x94);
    }
    if (0.0f < h)
        GRef<f32>(s + 0x94) = fpu::Sub(h, F(0x0040E4E4));
    else
        GRef<u32>(s + 0x94) = 0;
    func_00145D08(F(s + 0x98), UnitModels(), GRef<u8>(u + 0x55), 1);
    func_00145D08(F(s + 0x98), UnitModels(), GRef<u8>(u + 0x55), 2);
    func_00145D08(F(s + 0x94), UnitModels(), GRef<u8>(u + 0x55), 3);
}

// EE: 0x00135C68
// Mission 0x12: the lowest z of the pursuing units relative to the line rules +0x1C (minimum
// kept in 0x004117C8).
// fpucheck: 1 add
void func_00135C68(f32 z) {
    const f32 d = fpu::Sub(z, F(Rules() + 0x1C));
    if (d < GRef<f32>(0x004117C8)) GRef<f32>(0x004117C8) = d;
}

// EE: 0x00135C90
// Mission 0x12 per frame: when the pursuers crossed the line (0x004117C8 < 0 while the previous
// distance 0x004117CC >= 0) the mission fails (status 0x440); otherwise radio warnings for the
// six distance marks of table 0x002A2630 (ids 0x002A2648) passed since the last frame.
void func_00135C90() {
    const f32 prev = GRef<f32>(0x004117CC), cur = GRef<f32>(0x004117C8);
    if (prev < 0.0f || 0.0f <= cur) {
        f32 c = cur;
        for (u32 k = 0; k < 6; ++k) {
            const f32 mk = F(0x002A2630 + 4 * k);
            if (mk <= GRef<f32>(0x004117CC) && c < mk) {
                func_00252FB8(GRef<s32>(0x002A2648 + 4 * k), 1, 0);
                c = GRef<f32>(0x004117C8);
            }
        }
        GRef<f32>(0x004117C8) = 524288.0f;
        GRef<f32>(0x004117CC) = c;
    } else {
        GRef<u32>(Status() + 0x20) |= 0x440u;
    }
}

// EE: 0x0014F470
// Event op 0x50 (returns 0, unused): a special weapon (kind k, from unit `owner`) launched at unit `target`'s
// position 65536 ahead (kind 1: random side offset C 0x0040E5B8 / 0x0040E5BC by NextRandomFloat),
// height capped at C 0x0040E5C0, with sound 11 and (gameplay) a replay event.
// fpucheck: 3 static adds
void func_0014F470(s32 owner, s32 target, s32 k) {
    guest::GuestFrame f(0x20);
    const u32 ang = f.Address(0), p = f.Address(0x10);
    ZeroQ(ang);
    GRef<u32>(ang + 4) = W(0x0040E5B4);
    CopyQ(p, Unit(target));
    if (k == 1) {
        if (NextRandomFloat() < 0.5f)
            GRef<f32>(p) = fpu::Add(F(p), F(0x0040E5B8));
        else
            GRef<f32>(p) = fpu::Sub(F(p), F(0x0040E5BC));
    }
    GRef<f32>(p + 8) = fpu::Add(F(p + 8), 65536.0f);
    if (F(0x0040E5C0) < F(p + 4)) GRef<u32>(p + 4) = W(0x0040E5C0);
    FireCannonShell(owner, target, p, ang, static_cast<u8>(k));
    PlayEventSe(0xB, p);
    if (Sub() == 0xB) func_00171158(3, 0, 0xB, p, 0);
}

}  // namespace ac04::game
