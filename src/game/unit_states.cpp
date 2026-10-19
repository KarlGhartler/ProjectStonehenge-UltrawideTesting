// Unit state step of the flight frame (slice S5d): damage from last frame's hit flags, deaths and
// score, VS respawns, per-player proximity flags and radio warnings, the battle-state chatter
// request, mission-specific per-frame hooks, and the per-unit updates (player, AI aircraft, ground).
// docs/subsystems/weapons.md "Damage", docs/subsystems/mission-flow.md.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

// Hit damage of one unit (hit flags of the previous frame, shifted into +0x50 bits 16-23).
void ApplyDamage(s32 i, u32 u) {
    if (GRef<u8>(u + 0x54) == 1 && GRef<u8>(u + 0x62) != 0xFF) {
        const u32 t = Unit(GRef<u8>(u + 0x62));
        GRef<u8>(t + 0x98) = static_cast<u8>(GRef<u8>(t + 0x98) + 1);
    }
    GRef<u32>(u + 0x50) = W(u + 0x50) << 16;
    u32 hp = GRef<u16>(u + 0x5E);
    if (0 < GRef<s16>(u + 0x5E)) GRef<u32>(u + 0x4C) |= 0x1000000u;
    u32 hit = W(u + 0x50);
    s32 dmg = 0;
    if ((hit & 0x10000u) != 0) {
        dmg = 5;
        if (!(GRef<u8>(u + 0x63) < Players())) {
            const f32 r = NextRandomFloat();
            hit = W(u + 0x50);
            hp = GRef<u16>(u + 0x5E);
            dmg = fpu::FloatToInt(r * F(0x0040DD28)) + 1;
        }
    }
    if ((hit & 0x20000u) != 0) {
        if (Game(0x28) != 0 || MissionKind() != 0 || GRef<u8>(u + 0x63) != 0)
            dmg += 0x28;
        else
            dmg += 100;
    }
    if ((hit & 0x40000u) != 0) dmg += 100;
    if ((hit & 0x80000u) != 0) dmg += 0x14;
    if ((hit & 0x200000u) != 0) dmg += 200;
    if ((W(u + 0x48) & 0x8C000000u) != 0 && !(GRef<u8>(u + 0x63) < Players())) dmg = 0;
    const u32 st = Status();
    if ((W(st + 0x20) & 0x80000u) != 0 && !(i < 2) && GRef<u8>(u + 0x69) == 0x12) dmg = 0;
    if (hp - 1u < 0x270Eu) GRef<u16>(u + 0x5E) = static_cast<u16>(hp - static_cast<u32>(dmg));
    u32 fl;
    if (GRef<u8>(u + 0x54) == 0) {
        if ((hit & 0x400000u) != 0) {
            GRef<u16>(u + 0x5E) = 0;
            if (MissionKind() == 0 && Game(0x29) != 3) {
                GRef<u32>(st + 0x20) |= 0x4000u;
                hit = W(u + 0x50);
            }
        }
        if ((hit & 0x800000u) != 0) {
            const u32 s = W(u + 0x44);
            const u16 lost = GRef<u16>(s + 0x2D4);
            GRef<u32>(s + 0x2D4) = 0;
            const u16 nh = static_cast<u16>(GRef<u16>(u + 0x5E) - lost);
            fl = W(u + 0x4C);
            GRef<u16>(u + 0x5E) = nh;
            if ((fl & 0x1000000u) == 0) return;
            if (!(0 < static_cast<s16>(nh)) && MissionKind() == 0 && Game(0x29) != 3) {
                GRef<u32>(st + 0x20) |= 0x4000u;
                fl = W(u + 0x4C);
            }
        } else {
            fl = W(u + 0x4C);
        }
    } else {
        if ((hit & 0x400000u) != 0 && GRef<u8>(u + 0x69) == 0xB2) {
            GRef<u16>(u + 0x5E) = 0;
            PlayMenuSe(0, 2);
        }
        fl = W(u + 0x4C);
    }
    if ((fl & 0x1000000u) == 0) return;
    if (!(0 < GRef<s16>(u + 0x5E))) {
        GRef<u16>(u + 0x5E) = 0;
        GRef<u32>(u + 0x4C) = (fl & 0xFEFFFFFFu) | 0x200000u;
        if (GRef<s32>(u + 0x48) < 0) GRef<u8>(Status() + 0x7C) = static_cast<u8>(GRef<u8>(Status() + 0x7C) + 1);
        func_0011E708(u);
        auto credit = [&](u32 by) {
            if (GRef<u8>(by + 0x54) != 0) return;
            if (GRef<s16>(u + 0x60) != 0) func_00162F48(GRef<u8>(u + 0x63), static_cast<u8>(i));
            if (MissionKind() == 1) func_0017BD68(GRef<s32>(Status() + 0x24), GRef<u16>(Unit(GRef<u8>(u + 0x63)) + 0x5C));
        };
        if ((W(u + 0x50) & 0xF0000u) != 0 && static_cast<u32>(Sub() - 10) < 2u && GRef<u8>(u + 0x63) != 0xFF &&
            GRef<u8>(u + 0x6A) != 8) {
            if (GRef<u8>(u + 0x54) != 0) {
                const u32 by = Unit(GRef<u8>(u + 0x63));
                GRef<u16>(by + 0x5C) = static_cast<u16>(GRef<u16>(by + 0x5C) + GRef<u16>(u + 0x60));
            }
            const u32 by = Unit(GRef<u8>(u + 0x63));
            if (GRef<u8>(by + 0x54) == 0 && GRef<u8>(u + 0x54) != 0) credit(by);
        }
        if (GRef<u8>(u + 0x69) == 0xB2 && (W(u + 0x50) & 0x400000u) != 0 && GRef<u8>(u + 0x63) != 0xFF) {
            const u32 by = Unit(GRef<u8>(u + 0x63));
            GRef<u16>(by + 0x5C) = static_cast<u16>(GRef<u16>(by + 0x5C) + GRef<u16>(u + 0x60));
            credit(Unit(GRef<u8>(u + 0x63)));
        }
        if (Game(0x27) == 0xB && GRef<u8>(u + 0x69) == 0x41)
            GRef<u8>(UnitList() + 0x83) = static_cast<u8>(GRef<u8>(UnitList() + 0x83) - 1);
    }
    if (Players() == 2 && GRef<u8>(u + 0x54) == 0 && (W(u + 0x4C) & 0x200000u) != 0) {
        if (static_cast<u32>(Sub() - 10) < 2u) {
            if (i == 0) {
                const u32 o = Unit(1);
                GRef<u16>(o + 0x5C) = static_cast<u16>(GRef<u16>(o + 0x5C) + 100);
                func_00162F48(1, 0);
            } else if (i == 1) {
                const u32 o = Unit(0);
                GRef<u16>(o + 0x5C) = static_cast<u16>(GRef<u16>(o + 0x5C) + 100);
                func_00162F48(0, 1);
            }
        }
        func_0010A130(1.0f, 3.0f, i);
    }
}

// Clock direction (1-36) of unit t seen from u, for the radio warnings.
s32 ClockDir(u32 t, u32 u, u32 tmp, bool second) {
    sceVu0SubVector(tmp, t, u);
    f32 a = ee::atan2f(F(tmp), F(tmp + 8));
    if (!second) {
        const f32 c = F(0x0040DD48);
        if (c < a)
            a = fpu::Sub(a, c);
        else if (a < 0.0f)
            a = fpu::Add(a, c);
        const s32 n = fpu::FloatToInt(a * F(0x0040DD4C));
        return n == 0 ? 0x24 : n;
    }
    const f32 c = F(0x0040DD50);
    if (c < a)
        a = fpu::Sub(a, c);
    else if (a < 0.0f)
        a = fpu::Add(a, c);
    if (a < 0.0f) a = fpu::Add(a, F(0x0040DD54));
    const s32 n = fpu::FloatToInt(a * F(0x0040DD58));
    return n == 0 ? 0x24 : n;
}

}  // namespace

// EE: 0x0011BE30
// Unit states per frame: lock counters (+0x98) cleared; tutorial (g_GameState[0x29] == 3) makes AI
// unlockable and ground units invulnerable; per unit the hit flags become damage (bullet 5 or
// 1 + random for AI shooters, missile 40/100, special 100, flak 20, cannon 200; HP only when
// 1-9998), deaths (score to the attacker, kill message 0x00162F48, trial track), VS respawn after
// the fade, then per player the proximity flags of every unit (0x1000 within 65536, 0x2000/0x4000
// within 32768, 0x200 within 16384, 0x8000 in front, 0x10000/0x20000/0x40000 tail-on enemy
// aircraft; table *0x00410648 + p: {flags, distance, cos}), incoming / enemy-aircraft radio
// warnings (state +0x282), situation chatter 0x00252C30 with the side counts, mission hooks (8, 10,
// 0xF, 0x12), the per-kind unit updates (death flags 0x400000 / 0x80000000), model hierarchies.
// fpucheck: 8 static adds
void UpdateUnitStates() {
    for (s32 i = 0; i < UnitCount(); ++i) GRef<u8>(Unit(i) + 0x98) = 0;
    if (Game(0x29) == 3) {
        for (s32 i = 0; i < UnitCount(); ++i) {
            const u32 u = Unit(i);
            if (GRef<u8>(u + 0x54) == 1) GRef<u32>(u + 0x4C) = W(u + 0x4C) & 0xCFFFFFFFu;
            if (GRef<u8>(u + 0x54) == 2) {
                GRef<u8>(u + 0x64) = 0;
                GRef<u16>(u + 0x5E) = 9999;
                GRef<u32>(u + 0x48) = W(u + 0x48) & 0xFCFFFFFFu;
            }
        }
    }
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        const u32 f = W(u + 0x4C) & 0xFF000000u;
        GRef<u16>(u + 0x56) = 0;
        GRef<u32>(u + 0x4C) = f;
        if ((f & 0x40000000u) != 0) {
            if (func_0011E250(i) == 0) continue;
            GRef<u32>(u + 0x4C) = W(u + 0x4C) & 0xBFFFFFFFu;
        }
        if ((~W(u + 0x4C) & 0x10000000u) != 0) continue;
        ApplyDamage(i, u);
    }
    if (Players() == 2) {
        for (s32 p = 0; p < 2; ++p) {
            const u32 u = Unit(p);
            if ((~W(u + 0x4C) & 0x1000000u) == 0) continue;
            if (!(F(Scene() + 0x1670 + 4u * static_cast<u32>(p)) == 1.0f)) continue;
            const u32 s = W(u + 0x44);
            func_0011BD10(u);
            ZeroQ(s);
            GRef<u32>(s + 0x138) = 0;
            GRef<u32>(s + 0x13C) = 0;
            GRef<u32>(s + 0x188) |= 0x800000u;
            GRef<u32>(u + 0x4C) |= 0x31000000u;
            const s32 hp = func_0011E878(GRef<u8>(u + 0x69));
            GRef<u32>(u + 0x50) = 0;
            GRef<u32>(s + 0x2D4) = 0;
            GRef<u32>(s + 0x188) |= 0x40u;
            GRef<u16>(u + 0x5E) = static_cast<u16>(hp);
            func_0010A130(0.0f, 0.5f, p);
            func_001279B0(p, 1);
        }
    }
    const u32 L = UnitList();
    GRef<u8>(L + 0x84) = 0;
    GRef<u8>(L + 0x87) = 0;
    GRef<u8>(L + 0x86) = 0;
    GRef<u8>(L + 0x85) = 0;
    guest::GuestFrame fr(0x60);
    const u32 m = fr.Address(0), loc = fr.Address(0x40), d = fr.Address(0x50);
    for (s32 p = 0; p < Players(); ++p) {
        const u32 u = Unit(p);
        const u32 s = W(u + 0x44);
        s32 near_air = -1, incoming = -1, near_any = -1, near_close = -1;
        GRef<u32>(s + 0x188) &= 0xFBFFFFFFu;
        const u32 side = W(u + 0x48) & 3u;
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, u + 0x10);
        sceVu0InversMatrix(m, m);
        for (s32 j = 0; j < UnitCount(); ++j) {
            const u32 rec = W(0x00410648 + 4u * static_cast<u32>(p)) + static_cast<u32>(j) * 0xC;
            const u32 o = Unit(j);
            GRef<u32>(rec) = 0;
            GRef<u32>(rec + 4) = 0x48800000u;  // 262144.0
            GRef<u32>(rec + 8) = 0xBF800000u;  // -1.0
            u32 of = W(o + 0x4C);
            if ((~of & 0x10000000u) != 0) continue;
            if ((W(o + 0x48) & 0x3000u) == 0x1000u) {
                of |= 0x300u;
                GRef<u32>(o + 0x4C) = of;
            }
            if ((of & 0x200000u) != 0) GRef<u32>(o + 0x4C) = of | 0x100u;
            sceVu0SubVector(d, o, u);
            const f32 dx = F(d), dy = F(d + 4), dz = F(d + 8);
            if (!(-65536.0f < dx) || !(dx < 65536.0f) || !(-65536.0f < dy) || !(dy < 65536.0f) || !(-65536.0f < dz) ||
                !(dz < 65536.0f)) {
                if ((StatusFlags() & 4u) == 0 || (W(o + 0x4C) & 0x4000000u) == 0 || (W(o + 0x48) & 3u) != 1) continue;
                if ((W(Rules() + 0x10) & 4u) == 0) continue;
                if (GRef<u8>(o + 0x54) == 1 || GRef<u8>(o + 0x6A) == 7) GRef<u32>(o + 0x4C) = W(o + 0x4C) & 0xCFFFFFFFu;
                continue;
            }
            GRef<u32>(o + 0x4C) |= 0x1000u;
            GRef<u32>(rec) |= 1u;
            if ((W(o + 0x48) & 0x3000u) == 0) GRef<u32>(o + 0x4C) |= 0x100u;
            const bool far_idx = !(j < 2);
            if (far_idx && GRef<u8>(UnitList() + 0x8A) == GRef<u8>(o + 0x69)) near_any = j;
            if (!(-32768.0f < dx) || !(dx < 32768.0f) || !(-32768.0f < dy) || !(dy < 32768.0f) || !(-32768.0f < dz) ||
                !(dz < 32768.0f))
                continue;
            GRef<u32>(o + 0x4C) |= 0x2000u;
            GRef<u32>(rec) |= 2u;
            if ((W(o + 0x48) & 0x3000u) == 0x2000u) GRef<u32>(o + 0x4C) |= 0x100u;
            const f32 dist = GetVectorLength(d);
            GRef<f32>(rec + 4) = dist;
            if (dist < 32768.0f) {
                GRef<u32>(o + 0x4C) |= 0x4000u;
                if (GRef<u8>(o + 0x54) == 1 && (W(o + 0x48) & 3u) != 0) incoming = j;
                if (far_idx && GRef<u8>(UnitList() + 0x8A) == GRef<u8>(o + 0x69)) near_close = j;
            }
            if (p == 0) {
                if ((W(o + 0x4C) & 0x1000000u) != 0) {
                    const u32 f48 = W(o + 0x48);
                    const u32 Lc = UnitList();
                    if ((f48 & 0x1000000u) != 0) {
                        if ((f48 & 3u) == side)
                            GRef<u8>(Lc + 0x84) = static_cast<u8>(GRef<u8>(Lc + 0x84) + 1);
                        else
                            GRef<u8>(Lc + 0x85) = static_cast<u8>(GRef<u8>(Lc + 0x85) + 1);
                    } else if ((f48 & 0x2000000u) != 0) {
                        if ((f48 & 3u) == side)
                            GRef<u8>(Lc + 0x86) = static_cast<u8>(GRef<u8>(Lc + 0x86) + 1);
                        else
                            GRef<u8>(Lc + 0x87) = static_cast<u8>(GRef<u8>(Lc + 0x87) + 1);
                    }
                }
                if (GRef<u8>(o + 0x54) == 1 && (W(o + 0x48) & 3u) != 0) near_air = j;
            }
            const f32 dd = F(rec + 4);
            if (dd < 16384.0f) GRef<u32>(o + 0x4C) |= 0x200u;
            if (Game(0x27) == 10 && (~StatusFlags() & 4u) != 0 && GRef<u8>(o + 0x69) == 0x73 &&
                (W(o + 0x4C) & 0x1000000u) != 0 && dd < 32768.0f)
                GRef<u32>(s + 0x188) |= 0x4000000u;
            sceVu0ApplyMatrix(loc, m, d);
            if (F(rec + 4) == 0.0f)
                GRef<f32>(rec + 8) = 1.0f;
            else
                GRef<f32>(rec + 8) = F(loc + 8) / F(rec + 4);
            if (F(0x0040DD2C) < F(rec + 8)) GRef<u32>(o + 0x4C) |= 0x8000u;
            if (GRef<u8>(o + 0x54) != 1) continue;
            if ((W(o + 0x48) & 3u) == (W(u + 0x48) & 3u)) continue;
            if ((W(o + 0x4C) & 0xC000u) != 0xC000u) continue;
            f32 a = fpu::Sub(F(o + 0x14), F(u + 0x14));
            if (F(0x0040DD30) < a) a = fpu::Sub(a, F(0x0040DD34));
            if (a < F(0x0040DD38)) a = fpu::Add(a, F(0x0040DD3C));
            if (!(a < F(0x0040DD40))) continue;
            if (!(F(0x0040DD44) < a)) continue;
            const u32 nf = W(o + 0x4C) | 0x10000u;
            GRef<u32>(o + 0x4C) = nf;
            GRef<u32>(o + 0x4C) = nf | (p == 0 ? 0x20000u : 0x40000u);
        }
        if (MissionKind() != 0 || Game(0x29) == 3) continue;
        if (incoming != -1) {
            const s32 clock = ClockDir(Unit(incoming), u, d, false);
            const u8 b = GRef<u8>(s + 0x282);
            if ((~b & 0x80) != 0) {
                GRef<u8>(s + 0x282) = static_cast<u8>(b | 0xC0);
                QueueRadioChatter(1, clock, GRef<u8>(Radio() + 0x83), 0x31, 0, 1);
            } else if ((~b & 0x20) != 0) {
                GRef<u8>(s + 0x282) = static_cast<u8>(b | 0x20);
                QueueRadioChatter(1, clock, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            }
        } else {
            const u8 b0 = GRef<u8>(s + 0x282);
            GRef<u8>(s + 0x282) = static_cast<u8>(b0 & 0xDF);
            if (near_air == -1) {
                GRef<u8>(s + 0x282) = static_cast<u8>(b0 & 0xCF);
            } else {
                const s32 clock = ClockDir(Unit(near_air), u, d, true);
                const u8 b = GRef<u8>(s + 0x282);
                if ((~b & 0x40) != 0) {
                    GRef<u8>(s + 0x282) = static_cast<u8>(b | 0x40);
                    QueueRadioChatter(0, clock, GRef<u8>(Radio() + 0x83), 0x31, 0, 1);
                } else if ((~b & 0x10) != 0) {
                    GRef<u8>(s + 0x282) = static_cast<u8>(b | 0x10);
                    QueueRadioChatter(0, clock, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
                }
            }
        }
        if (near_any != -1 && (~GRef<u8>(UnitList() + 0x8B) & 1) != 0)
            GRef<u8>(UnitList() + 0x8B) = static_cast<u8>(GRef<u8>(UnitList() + 0x8B) | 1);
        if (near_close != -1 && (~GRef<u8>(UnitList() + 0x8B) & 2) != 0) {
            GRef<u8>(UnitList() + 0x8B) = static_cast<u8>(GRef<u8>(UnitList() + 0x8B) | 2);
            const u8 mid = Game(0x27);
            if (mid == 0x10)
                func_00252FB8(4, 1, 0);
            else if (mid == 0xE)
                func_00252FB8(0x14, 1, 0);
            else if (mid == 0xF)
                func_00252FB8(6, 1, 0);
        }
    }
    if (MissionKind() == 0 && Game(0x29) != 3 && Sub() == 0xB) {
        if (GRef<u8>(Radio() + 0x80) != 0) func_0017B330();
        if (GRef<s32>(W(0x00410B2C) + 0x80) != 0) func_0017B740();
        u32 fl = (StatusFlags() & 4u) != 0 ? 8u : 4u;
        if (Game(0x27) == 0x10) {
            const u32 Lc = UnitList();
            for (s32 k = 0; k < GRef<u8>(Lc + 0x83); ++k)
                if ((W(Unit(GRef<u8>(Lc + 0x80 + static_cast<u32>(k))) + 0x4C) & 0x1000000u) != 0) fl |= 2u;
            if ((~fl & 2u) != 0) fl |= 1u;
        } else {
            fl |= 1u;
        }
        if (Game(0x27) == 7 && (StatusFlags() & 4u) != 0 && W(Status() + 0x24) - 0x4B00u < 0x4B1u) fl |= 0x20000000u;
        if (Game(0x27) == 0xF && GRef<u8>(W(Unit(0) + 0x44) + 0x183) == 3) fl |= 0x40000000u;
        const u32 Lc = UnitList();
        func_00252C30(GRef<u8>(Radio() + 0x83), GRef<u8>(Lc + 0x84), GRef<u8>(Lc + 0x86), GRef<u8>(Lc + 0x85),
                      GRef<u8>(Lc + 0x87), fl);
    }
    if (Game(0x27) == 8) func_0017B9B0();
    if (Game(0x27) == 10) func_00133AB8();
    if (Game(0x27) == 0xF) func_00135008();
    if (Game(0x27) == 0x12 && (~StatusFlags() & 0x10000000u) != 0) func_00135C90();
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        u32 f = W(u + 0x4C);
        if ((~f & 0x10000000u) != 0 || (~f & 0x100u) != 0) continue;
        const u8 k = GRef<u8>(u + 0x54);
        if (k == 1) {
            UpdateAIAircraft(i);
            f = W(u + 0x4C);
        } else if (k == 0) {
            UpdatePlayerAircraft(i);
            f = W(u + 0x4C);
        } else if (k == 2) {
            UpdateGroundUnit(i);
            f = W(u + 0x4C);
        }
        if ((f & 0x400000u) != 0) {
            const u32 c = f & 0xFFBFFFFFu;
            GRef<u32>(u + 0x4C) = c;
            f = c;
            const u8 part = GRef<u8>(u + 0x59);
            if (part != 0xFF) {
                if (part == 0xFE) {
                    f = (c & 0xDFFFFFFFu) | 0x80000000u;
                    GRef<u32>(u + 0x4C) = f;
                } else {
                    GRef<u8>(u + 0x5A) = 1;
                    func_00145D08(0.0f, UnitModels(), GRef<u8>(u + 0x55), 1);
                    f = W(u + 0x4C);
                }
            }
        }
        if (static_cast<s32>(f) < 0) {
            GRef<u32>(u + 0x4C) = f & 0x7FFFFFFFu;
            if (func_0011E4A0(i) != 0) GRef<u32>(u + 0x4C) |= 0x40000000u;
            GRef<u32>(u + 0x4C) = W(u + 0x4C) & 0xCFFFFFFFu;
        }
    }
    UpdateUnitModelHierarchies(UnitModels());
}

}  // namespace ac04::game
