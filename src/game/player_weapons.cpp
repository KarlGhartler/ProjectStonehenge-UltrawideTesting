// Player targeting, lock-on and weapon state (slice S5d): target choice and cycling, missile /
// special-weapon lock slots (state +0x154..+0x157, lock flags +0x188 bits 8-11 locked, 12-15
// locking, lock angles +0x194 / +0x1A4), lock-related radio calls, weapon parameters by the special
// weapon (0x0011ED28), reload timers and weapon models (0x00124740). docs/subsystems/flight.md,
// docs/subsystems/weapons.md.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

// EE: 0x0011E5E0
// Explosion of a destroyed unit (not model 0xB2): kind from table 0x002A1F10 by unit +0x9A, else
// 0x81 for aircraft / 0xB5 for ground units; replay event 4.
void func_0011E5E0(u32 u) {
    if (GRef<u8>(u + 0x69) == 0xB2) return;
    const u8 e = GRef<u8>(u + 0x9A);
    u32 kind;
    if (GRef<u8>(u + 0x54) < 2)
        kind = e == 0xFF ? 0x81u : W(0x002A1F10 + 4u * e);
    else
        kind = e == 0xFF ? 0xB5u : W(0x002A1F10 + 4u * e);
    func_001F0910(u, u + 0x30, u + 0x20, kind);
    func_00170C40(4, 0xFF, GRef<u8>(u + 0x55), u, u + 0x30, u + 0x20);
}

// EE: 0x0011ED28
// Weapon parameters of player state s for the selected weapon: gun / missile (view flag +0x14C
// 0x8000 clear) or the special weapon kind +0x173: lock range +0x1B4, cone cos +0x1B8 / +0x1BC
// (C 0x0040DD64-0x0040DD80), lock slots +0x152 (1 or the special's count +0x175), target classes
// +0x188 0x10000 air / 0x20000 ground.
void func_0011ED28(u32 s) {
    const u32 f = W(s + 0x188);
    f32 range, cone;
    if ((W(s + 0x14C) & 0x8000u) == 0) {
        cone = F(0x0040DD80);
        GRef<f32>(s + 0x1BC) = cone;
        GRef<u8>(s + 0x152) = 1;
        GRef<u32>(s + 0x188) = f | 0x30000u;
        range = 8192.0f;
    } else {
        const s32 k = static_cast<s8>(GRef<u8>(s + 0x173) - 3);
        switch (static_cast<u32>(k) < 0xC ? k : -1) {
            case 7:  // kind 10
                cone = F(0x0040DD64);
                GRef<f32>(s + 0x1BC) = cone;
                GRef<u32>(s + 0x188) = (f | 0x20000u) & 0xFFFEFFFFu;
                GRef<u8>(s + 0x152) = GRef<u8>(s + 0x175);
                range = 12288.0f;
                break;
            case 10:  // kind 13
                cone = F(0x0040DD68);
                GRef<f32>(s + 0x1BC) = cone;
                GRef<u32>(s + 0x188) = (f | 0x10000u) & 0xFFFDFFFFu;
                GRef<u8>(s + 0x152) = GRef<u8>(s + 0x175);
                range = 24576.0f;
                break;
            case 9:  // kind 12
                cone = F(0x0040DD6C);
                GRef<f32>(s + 0x1BC) = cone;
                GRef<u32>(s + 0x188) = (f | 0x10000u) & 0xFFFDFFFFu;
                GRef<u8>(s + 0x152) = GRef<u8>(s + 0x175);
                range = 16384.0f;
                break;
            case 0:  // kinds 3, 9
            case 6:
                cone = F(0x0040DD70);
                GRef<f32>(s + 0x1BC) = cone;
                GRef<u32>(s + 0x188) = (f | 0x20000u) & 0xFFFEFFFFu;
                GRef<u8>(s + 0x152) = 1;
                range = 12288.0f;
                break;
            case 8:  // kind 11
                cone = F(0x0040DD74);
                GRef<f32>(s + 0x1BC) = cone;
                GRef<u32>(s + 0x188) = (f | 0x20000u) & 0xFFFEFFFFu;
                GRef<u8>(s + 0x152) = 1;
                range = 24576.0f;
                break;
            case 11: {  // kind 14
                GRef<u32>(s + 0x188) = (f | 0x10000u) & 0xFFFDFFFFu;
                GRef<f32>(s + 0x1BC) = -0.5f;
                GRef<u8>(s + 0x152) = 1;
                GRef<f32>(s + 0x1B4) = 8192.0f;
                GRef<f32>(s + 0x1B8) = F(0x0040DD78);
                return;
            }
            default:
                cone = F(0x0040DD7C);
                GRef<f32>(s + 0x1BC) = cone;
                GRef<u32>(s + 0x188) = (f | 0x20000u) & 0xFFFEFFFFu;
                GRef<f32>(s + 0x1B4) = 8192.0f;
                GRef<u8>(s + 0x152) = 0;
                GRef<f32>(s + 0x1B8) = cone;
                return;
        }
    }
    GRef<f32>(s + 0x1B4) = range;
    GRef<f32>(s + 0x1B8) = cone;
}

// EE: 0x0011EF40
// Clears the weapon reload timers (+0x15E/+0x160 and the six special slots +0x166..+0x170).
void func_0011EF40(u32 s) {
    for (s32 i = 1; i >= 0; --i) GRef<u16>(s + 0x15E + 2u * static_cast<u32>(i)) = 0;
    for (s32 i = 5; i >= 0; --i) GRef<u16>(s + 0x166 + 2u * static_cast<u32>(i)) = 0;
    GRef<u8>(s + 0x17C) = 0;
    GRef<u8>(s + 0x17E) = 0;
}

// EE: 0x001232D8
// Player p targeting per frame: drops an invalid target; the 16 best candidates (ground-lock mode
// when the special weapon has a ground aim point 0x00153FD8: 262144 - distance to that point; else
// cone class 98304/65536/32768 + 32768 - distance, +1 for players within 32768) with near-target
// pruning; target cycling (pad +4 bit 0x40, best +0x181 kept +0x182 frames, +0x184 frames out of
// the cone); the 600-frame "target at n o'clock" call; lock slots (count +0x152, limited by the
// weapon count, none while flag 0x4000000 or when the aircraft is upside down / steep 0x40000):
// slot 0 the target, others the best free candidate by cone; locking turns the lock angles toward
// the target by C 0x0040DE0C per frame until within it (flags 0x1000 -> 0x100; special kinds
// 12/13 stop after slot 0); locked units get flag 1/2 (+0x4C) and 0x2000/0x4000 (+0x50); radio
// calls 8 (tail-on), 9 (missile lock), 10 (formation), state +0x282 bits 0-2.
// fpucheck: 18 static adds
void UpdatePlayerTargeting(s32 p) {
    const u32 u = Unit(p);
    const u32 s = W(u + 0x44);
    const u8 old = GRef<u8>(u + 0x62);
    const u32 pad = PadExt(p);
    const u32 tbl = W(0x00410648 + 4u * static_cast<u32>(p));
    auto rec = [&](u32 j) { return tbl + j * 0xC; };
    if (old != 0xFF) {
        const u32 t = Unit(old);
        bool drop = (~W(t + 0x4C) & 0x1000000u) != 0 || GRef<u8>(t + 0x64) == 0;
        if (!drop) {
            const u32 f48 = W(t + 0x48);
            if ((f48 & 0x1000000u) != 0)
                drop = (~W(s + 0x188) & 0x10000u) != 0;
            else if ((f48 & 0x2000000u) != 0)
                drop = (~W(s + 0x188) & 0x20000u) != 0;
        }
        if (drop) GRef<u8>(u + 0x62) = 0xFF;
    }
    const u32 side = GRef<u8>(u + 0x48) & 3u;
    guest::GuestFrame fr(0x100);
    const u32 sp = fr.Address(0), dir = fr.Address(0x10), m = fr.Address(0xA0), dv = fr.Address(0xE0);
    s32 ground = 0;
    if ((W(s + 0x14C) & 0x8000u) != 0) {
        const s8 k = GRef<s8>(s + 0x173);
        if (k != 0xD && k != 0xC && k != 0xE && k != 0xA && k != 0xB && k != 9) {
            func_00154198(dir, u + 0x10, u + 0x20, k);
            f32 h;
            if (GRef<u8>(u + 0x62) == 0xFF)
                h = func_0013E338(Terrain(), u);
            else
                h = F(Unit(GRef<u8>(u + 0x62)) + 4);
            ground = func_00153FD8(h, sp, u, dir) < 0 ? 0 : 1;
        }
    }
    f32 score[16];
    u32 idx[16];
    for (s32 k = 0; k < 16; ++k) {
        score[k] = 0.0f;
        idx[k] = 0xFF;
    }
    for (s32 j = 0; j < UnitCount(); ++j) {
        if (j == p) continue;
        const u32 o = Unit(j);
        const u32 of = W(o + 0x4C);
        if ((~of & 0x10000000u) != 0) continue;
        const u32 f48 = W(o + 0x48);
        if ((f48 & 3u) == side) continue;
        if ((~of & 0x1000000u) != 0) continue;
        const u8 kind = GRef<u8>(o + 0x54);
        if (kind != 0 && (~of & 0x4000u) != 0) continue;
        if (GRef<u8>(o + 0x64) == 0) continue;
        u32 need;
        if ((f48 & 0x1000000u) != 0)
            need = 0x10000u;
        else if ((f48 & 0x2000000u) != 0)
            need = 0x20000u;
        else
            continue;
        if ((~W(s + 0x188) & need) != 0) continue;
        f32 sc;
        if (ground == 1) {
            if (kind == 0 && (of & 0x4000u) != 0)
                sc = 0.0f;
            else
                sc = fpu::Sub(262144.0f, func_00104F10(sp, o));
        } else {
            const f32 c = F(rec(static_cast<u32>(j)) + 8);
            if (F(0x0040DDE8) < c)
                sc = 98304.0f;
            else if (F(0x0040DDEC) < c)
                sc = 65536.0f;
            else
                sc = F(0x0040DDF0) < c ? 32768.0f : 0.0f;
            if (kind == 0 && (of & 0x4000u) != 0)
                sc = fpu::Add(sc, 1.0f);
            else
                sc = fpu::Add(sc, fpu::Sub(32768.0f, F(rec(static_cast<u32>(j)) + 4)));
        }
        for (s32 k = 15; k >= 0; --k) {
            if (!(score[k] < sc)) break;
            if (k < 15) {
                score[k + 1] = score[k];
                idx[k + 1] = idx[k];
            }
            score[k] = sc;
            idx[k] = static_cast<u32>(j);
        }
    }
    if (65536.0f < score[0]) {
        for (s32 k = 0; k < 16; ++k)
            if (score[k] < 65536.0f) {
                score[k] = 0.0f;
                idx[k] = 0xFF;
            }
    } else if (32768.0f < score[0]) {
        for (s32 k = 0; k < 16; ++k)
            if (score[k] < 32768.0f) {
                score[k] = 0.0f;
                idx[k] = 0xFF;
            }
        for (s32 k = 0; k < 16; ++k) {
            const f32 thr = fpu::Sub(65536.0f, fpu::IntToFloat(k) * 1024.0f);
            if (thr < score[0] && score[k] < thr) {
                score[k] = 0.0f;
                idx[k] = 0xFF;
            }
        }
    }
    // Next candidate after the current target in the list (or the best one).
    const u32 cur = GRef<u8>(u + 0x62);
    s32 pos = 16;
    if (GRef<s32>(u + 0x6C) < 0x3C || cur == idx[0]) {
        for (s32 k = 0; k < 16; ++k)
            if (idx[k] == cur) pos = k + 1;
    }
    u32 next = idx[0];
    if (pos < 16 && idx[pos] != 0xFF) next = idx[pos];
    if (GRef<u8>(s + 0x181) != idx[0]) {
        GRef<u8>(s + 0x182) = 0;
        GRef<u8>(s + 0x181) = static_cast<u8>(idx[0]);
    } else if (65536.0f < score[0] && GRef<u8>(s + 0x182) < 0xFF) {
        GRef<u8>(s + 0x182) = static_cast<u8>(GRef<u8>(s + 0x182) + 1);
    }
    if (GRef<u8>(u + 0x62) != 0xFF) {
        if (F(rec(GRef<u8>(u + 0x62)) + 8) < F(0x0040DDF4)) {
            if (GRef<u8>(s + 0x184) < 0xFF) GRef<u8>(s + 0x184) = static_cast<u8>(GRef<u8>(s + 0x184) + 1);
        } else {
            GRef<u8>(s + 0x184) = 0;
        }
    }
    const bool cycle = (W(pad + 4) & 0x40u) != 0;
    auto keep_or_best = [&]() {
        if (!(GRef<u8>(s + 0x182) < 0x78) && !(GRef<u8>(s + 0x184) < 0x78)) GRef<u8>(u + 0x62) = GRef<u8>(s + 0x181);
    };
    if (ground == 1) {
        if (cycle || GRef<u8>(u + 0x62) == 0xFF)
            GRef<u8>(u + 0x62) = GRef<u8>(s + 0x181);
        else
            keep_or_best();
    } else if (!cycle && GRef<u8>(u + 0x62) != 0xFF) {
        keep_or_best();
    } else {
        const u8 t = GRef<u8>(u + 0x62);
        if (t == 0xFF || !(F(0x0040DDF8) <= F(rec(t) + 8)) || F(0x0040DDF8) <= F(rec(next) + 8))
            GRef<u8>(u + 0x62) = static_cast<u8>(next);
    }
    const u8 tgt = GRef<u8>(u + 0x62);
    if (old != tgt) {
        for (u32 k = 0; k < 4; ++k) {
            GRef<u32>(s + 0x194 + 4 * k) = 0;
            GRef<u32>(s + 0x1A4 + 4 * k) = 0;
        }
        GRef<u32>(s + 0x188) = W(s + 0x188) & 0xFFFF00FFu;
        GRef<u32>(u + 0x6C) = 0;
        GRef<u8>(s + 0x184) = 0;
    } else if (tgt != 0xFF) {
        const s32 n = GRef<s32>(u + 0x6C) + 1;
        GRef<s32>(u + 0x6C) = n;
        if (MissionKind() == 0 && Game(0x29) != 3) {
            const u32 t = Unit(tgt);
            if (GRef<u8>(t + 0x54) == 1 && n % 600 == 300 && (W(t + 0x4C) & 0x4000u) != 0 &&
                16384.0f < F(W(0x00410648 + 4u * static_cast<u32>(p)) + tgt * 0xCu + 4)) {
                sceVu0SubVector(dir, t, u);
                f32 a = fpu::Sub(ee::atan2f(F(dir), F(dir + 8)), F(u + 0x14));
                const f32 c = F(0x0040DDFC);
                if (c < a)
                    a = fpu::Sub(a, c);
                else if (a < 0.0f)
                    a = fpu::Add(a, c);
                s32 clock = fpu::FloatToInt(a * F(0x0040DE00));
                if (clock == 0) clock = 0x24;
                QueueRadioChatter(0x18, clock, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            }
        }
    }
    GRef<u32>(s + 0x188) = W(s + 0x188) & 0xFFFBFFFFu;
    if ((W(s + 0x14C) & 0x8080u) == 0x8080u) {
        bool steep = F(0x0040DE04) < F(u + 0x10);
        if (!steep) {
            f32 z = F(u + 0x18);
            if (z < 0.0f) z = -z;
            steep = F(0x0040DE08) < fpu::Add(z, F(u + 0x10));
        }
        if (steep) GRef<u32>(s + 0x188) = W(s + 0x188) | 0x40000u;
    }
    auto clear_slot = [&](u32 k) {
        GRef<u32>(s + 0x188) = W(s + 0x188) & ~(0x100u << k) & ~(0x1000u << k);
    };
    if ((W(s + 0x188) & 0x40000u) != 0) {
        for (u32 k = 0; k < 4; ++k) {
            GRef<u8>(s + 0x154 + k) = 0xFF;
            clear_slot(k);
        }
    } else {
        const u8 t0 = GRef<u8>(u + 0x62);
        if (t0 != 0xFF && GRef<u8>(s + 0x154) != t0) {
            GRef<u8>(s + 0x154) = 0xFF;
            GRef<u32>(s + 0x188) = W(s + 0x188) & 0xFFFFEFFFu & 0xFFFFFEFFu;
            for (u32 k = 1; k < 4; ++k) {
                if (GRef<u8>(s + 0x154 + k) == GRef<u8>(u + 0x62)) {
                    GRef<u8>(s + 0x154 + k) = 0xFF;
                    clear_slot(k);
                }
            }
        }
        f32 nslots = fpu::IntToFloat(GRef<u8>(s + 0x152));
        if ((W(s + 0x188) & 0x4000000u) != 0) {
            nslots = 0.0f;
        } else {
            const u8 lim = (W(s + 0x14C) & 0x8000u) == 0 ? GRef<u8>(s + 0x15A) : GRef<u8>(s + 0x15B);
            const f32 l = fpu::IntToFloat(lim);
            if (l < nslots) nslots = l;
        }
        for (u32 k = 0; k < 4; ++k) {
            const u32 sl = s + 0x154 + k;
            bool clear = !(fpu::IntToFloat(static_cast<s32>(k)) < nslots) || GRef<u8>(sl) == 0xFF;
            if (!clear) {
                const u8 j = GRef<u8>(sl);
                if ((~W(Unit(j) + 0x4C) & 0x1004000u) != 0 || F(rec(j) + 8) < F(s + 0x1BC) || F(s + 0x1B4) < F(rec(j) + 4) ||
                    GRef<u8>(s + 0x154) == 0xFF)
                    clear = true;
            }
            if (clear) {
                GRef<u8>(sl) = 0xFF;
                clear_slot(k);
            }
        }
        bool stop = false;
        for (u32 k = 0; fpu::IntToFloat(static_cast<s32>(k)) < nslots; ++k) {
            const u32 sl = s + 0x154 + k;
            if (k == 0) {
                const u8 t = GRef<u8>(u + 0x62);
                if (t != 0xFF && GRef<u8>(s + 0x154) != t) {
                    const u8 lk = GRef<u8>(Unit(t) + 0x64);
                    if (lk != 0 && (~lk & 4) != 0 && F(s + 0x1B8) <= F(rec(t) + 8) && F(rec(t) + 4) <= F(s + 0x1B4)) {
                        GRef<u8>(s + 0x154) = t;
                        GRef<u32>(s + 0x194) = 0;
                        GRef<u32>(s + 0x188) = W(s + 0x188) | 0x1000u;
                        GRef<u32>(s + 0x1A4) = 0;
                    }
                }
            } else if (GRef<u8>(sl) == 0xFF && !stop) {
                u32 best = 0xFF, best1 = 0xFF;
                f32 bc = -1.0f, bc1 = -1.0f, mind = 524288.0f;
                for (s32 j = 0; j < UnitCount(); ++j) {
                    const u32 o = Unit(j);
                    const u32 of = W(o + 0x4C);
                    if ((~of & 0x10000000u) != 0) continue;
                    const u32 f48 = W(o + 0x48);
                    if ((f48 & 3u) == (GRef<u8>(u + 0x48) & 3u)) continue;
                    if ((~of & 0x1000000u) != 0) continue;
                    const u8 lk = GRef<u8>(o + 0x64);
                    if (lk == 0 || (lk & 4) != 0) continue;
                    const u32 ju = static_cast<u32>(j);
                    if (ju == GRef<u8>(s + 0x154) || ju == GRef<u8>(s + 0x155) || ju == GRef<u8>(s + 0x156) ||
                        ju == GRef<u8>(s + 0x157))
                        continue;
                    if ((f48 & 0x1000000u) != 0) {
                        if ((~W(s + 0x188) & 0x10000u) != 0) continue;
                    } else if ((f48 & 0x2000000u) != 0) {
                        if ((~W(s + 0x188) & 0x20000u) != 0) continue;
                    } else {
                        continue;
                    }
                    const f32 dd = F(rec(ju) + 4);
                    if (dd < mind) mind = dd;
                    if ((~of & 0x4000u) != 0) continue;
                    const f32 c = F(rec(ju) + 8);
                    if (c < F(s + 0x1B8)) continue;
                    if (F(s + 0x1B4) < dd) continue;
                    if (c < 1.0f && bc1 < c) {
                        bc1 = c;
                        best1 = ju;
                    }
                    if (bc < c) {
                        bc = c;
                        best = ju;
                    }
                }
                GRef<u8>(sl) = static_cast<u8>(best == 0xFF ? best1 : best);
                if (GRef<u8>(sl) != 0xFF) {
                    GRef<u32>(s + 0x188) = W(s + 0x188) | (0x1000u << k);
                    GRef<u32>(s + 0x194 + 4 * k) = 0;
                    GRef<u32>(s + 0x1A4 + 4 * k) = 0;
                }
            }
            if (0 < k && GRef<u8>(sl) == 0xFF) {
                stop = true;
                continue;
            }
            const u32 o = W(0x002A1F70 + 4u * GRef<u8>(sl));
            if ((W(s + 0x188) & (0x100u << k)) != 0) {
                if (p == 0)
                    GRef<u32>(o + 0x4C) = W(o + 0x4C) | 1u;
                else if (p == 1)
                    GRef<u32>(o + 0x4C) = W(o + 0x4C) | 2u;
                GRef<u32>(o + 0x50) = W(o + 0x50) | ((W(s + 0x14C) & 0x8000u) != 0 ? 0x4000u : 0x2000u);
                if (k == 0) {
                    GRef<u8>(s + 0x2F0) = GRef<u8>(o + 0x55);
                    if (GRef<u8>(s + 0x154) < Players()) GRef<u8>(W(o + 0x44) + 0x2F1) = GRef<u8>(u + 0x55);
                }
            } else if ((W(s + 0x188) & (0x1000u << k)) != 0) {
                sceVu0UnitMatrix(m);
                sceVu0RotMatrixX(m, m, F(u + 0x10));
                sceVu0RotMatrixY(m, m, F(u + 0x14));
                sceVu0InversMatrix(m, m);
                sceVu0SubVector(dv, o, u);
                sceVu0ApplyMatrix(dir, m, dv);
                const f32 len = GetVectorLength(dir);
                const f32 az0 = ee::atan2f(F(dir), F(dir + 8));
                const f32 el0 = 0.0f < len ? ee::asinf(-F(dir + 4) / len) : 0.0f;
                const u32 pe = s + 0x194 + 4 * k, pa = s + 0x1A4 + 4 * k;
                const f32 el = fpu::Sub(el0, F(pe));
                const f32 az = fpu::Sub(az0, F(pa));
                // sqrt.S (EE); the original's NaN fallback to sqrtf cannot trigger (no NaN on the EE).
                const f32 mag = fpu::Sqrt(fpu::Add(el * el, az * az));
                const f32 step = F(0x0040DE0C);
                if (step < mag) {
                    GRef<f32>(pe) = fpu::Add(F(pe), el * step / mag);
                    GRef<f32>(pa) = fpu::Add(F(pa), az * step / mag);
                } else {
                    GRef<u32>(s + 0x188) = (W(s + 0x188) | (0x100u << k)) & ~(0x1000u << k);
                    if (static_cast<u32>(GRef<u8>(s + 0x173) - 0xC) < 2u) break;
                }
            } else if (k == 0) {
                GRef<u32>(s + 0x194) = 0;
                GRef<u32>(s + 0x1A4) = 0;
                break;
            }
        }
    }
    const u8 t = GRef<u8>(u + 0x62);
    if (t == 0xFF) {
        GRef<u8>(s + 0x282) = static_cast<u8>(GRef<u8>(s + 0x282) & 0xF8);
        return;
    }
    const u32 o = Unit(t);
    if (MissionKind() == 0 && Game(0x29) != 3) {
        if (GRef<u8>(o + 0x54) == 1) {
            const u32 r = rec(GRef<u8>(o + 0x55));
            const f32 dd = F(r + 4);
            if (F(0x0040DE10) <= F(r + 8) && dd < 16384.0f && F(0x0040DE14) < dd) {
                const u8 b = GRef<u8>(s + 0x282);
                if ((~b & 1) == 0) {
                    GRef<u8>(s + 0x282) = static_cast<u8>(b & 0xFE);
                } else {
                    sceVu0SubVector(dir, o + 0x10, u + 0x10);
                    f32 ax = F(dir), ay = F(dir + 4);
                    if (F(0x0040DE18) < ax) {
                        ax = fpu::Sub(ax, F(0x0040DE1C));
                        GRef<f32>(dir) = ax;
                    } else if (ax < F(0x0040DE20)) {
                        ax = fpu::Add(ax, F(0x0040DE24));
                        GRef<f32>(dir) = ax;
                    }
                    if (F(0x0040DE28) < ay) {
                        ay = fpu::Sub(ay, F(0x0040DE2C));
                        GRef<f32>(dir + 4) = ay;
                    } else if (ay < F(0x0040DE30)) {
                        ay = fpu::Add(ay, F(0x0040DE34));
                        GRef<f32>(dir + 4) = ay;
                    }
                    const f32 lo = F(0x0040DE38), hi = F(0x0040DE3C);
                    if (lo < ax && ax < hi && lo < ay && ay < hi) {
                        GRef<u8>(s + 0x282) = static_cast<u8>(GRef<u8>(s + 0x282) | 1);
                        QueueRadioChatter(8, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(o + 0x99), 0, 0);
                    }
                }
            }
            if ((W(s + 0x14C) & 0x8000u) == 0 || (W(s + 0x188) & 0x100u) == 0) {
                GRef<u8>(s + 0x282) = static_cast<u8>(GRef<u8>(s + 0x282) & 0xFD);
            } else if ((~GRef<u8>(s + 0x282) & 2) != 0) {
                GRef<u8>(s + 0x282) = static_cast<u8>(GRef<u8>(s + 0x282) | 2);
                QueueRadioChatter(9, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            }
        } else {
            GRef<u8>(s + 0x282) = static_cast<u8>(GRef<u8>(s + 0x282) & 0xFC);
        }
    }
    if (F(rec(GRef<u8>(u + 0x62)) + 4) < 4000.0f && (W(o + 0x4C) & 0x8000u) != 0) {
        if ((W(pad) & 1u) != 0) GRef<u32>(o + 0x50) = W(o + 0x50) | 0x1000u;
        if (MissionKind() != 0 || Game(0x29) == 3 || GRef<u8>(o + 0x54) != 1) return;
        if ((~GRef<u8>(s + 0x282) & 4) == 0) return;
        sceVu0SubVector(dir, o + 0x20, u + 0x20);
        if (!(GetVectorLength(dir) < 3.5f)) return;
        GRef<u8>(s + 0x282) = static_cast<u8>(GRef<u8>(s + 0x282) | 4);
        QueueRadioChatter(10, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
        return;
    }
    GRef<u8>(s + 0x282) = static_cast<u8>(GRef<u8>(s + 0x282) & 0xFB);
}

// EE: 0x00124740
// Reload timers of player p (gun +0x15E, missile +0x160, special slots +0x166 counting down
// for the slots in use) and the weapon models shown on the aircraft (0x0014AA38 on *0x004106A8:
// group 0 gun/missile, group 1 the six special slots, hidden while reloading; kinds 6/8 never
// shown); +0x186 gets the shown bits, +0x176/+0x177 count/mask of loaded special slots.
void func_00124740(s32 p, u32 s) {
    const u32 wm = W(0x004106A8);
    const u8 b = GRef<u8>(s + 0x15A);
    if (!(b < 2) && 0 < GRef<s16>(s + 0x15E)) GRef<u16>(s + 0x15E) = static_cast<u16>(GRef<u16>(s + 0x15E) - 1);
    if (b != 0 && 0 < GRef<s16>(s + 0x160)) GRef<u16>(s + 0x160) = static_cast<u16>(GRef<u16>(s + 0x160) - 1);
    if (!(b < 2) && GRef<s16>(s + 0x15E) == 0) {
        func_0014AA38(wm, p, 0, 0, 1);
        GRef<u8>(s + 0x186) = static_cast<u8>(GRef<u8>(s + 0x186) | 0x80);
    } else {
        func_0014AA38(wm, p, 0, 0, 0);
    }
    if (GRef<u8>(s + 0x15A) != 0 && GRef<s16>(s + 0x160) == 0) {
        func_0014AA38(wm, p, 0, 1, 1);
        GRef<u8>(s + 0x186) = static_cast<u8>(GRef<u8>(s + 0x186) | 0x40);
    } else {
        func_0014AA38(wm, p, 0, 1, 0);
    }
    GRef<u8>(s + 0x176) = 0;
    GRef<u8>(s + 0x177) = 0;
    for (s32 k = 0; k < static_cast<s32>(GRef<s8>(s + 0x175)); ++k) {
        const u32 t = s + 0x166 + 2u * static_cast<u32>(k);
        if (!(static_cast<s32>(GRef<u8>(s + 0x15B)) < static_cast<s32>(GRef<s8>(s + 0x175)) - k) && 0 < GRef<s16>(t))
            GRef<u16>(t) = static_cast<u16>(GRef<u16>(t) - 1);
        if (GRef<s16>(t) == 0) {
            GRef<u8>(s + 0x176) = static_cast<u8>(GRef<u8>(s + 0x176) + 1);
            GRef<u8>(s + 0x177) = static_cast<u8>(GRef<u8>(s + 0x177) | (1u << (k & 0x1F)));
        }
    }
    for (s32 k = 0; k < 6; ++k) {
        if (((static_cast<s32>(GRef<s8>(s + 0x177)) >> (k & 0x1F)) & 1) == 0) {
            func_0014AA38(wm, p, 1, k, 0);
            continue;
        }
        const s8 kind = GRef<s8>(s + 0x173);
        func_0014AA38(wm, p, 1, k, (kind == 8 || kind == 6) ? 0u : 1u);
        GRef<u8>(s + 0x186) = static_cast<u8>(GRef<u8>(s + 0x186) | (1u << (k & 0x1F)));
    }
}

namespace {

// The four aircraft with animated weapon bays / wings (models 0x11, 0xC, 0x13, 0x15).
bool BayModel(u8 m) { return m == 0x11 || m == 0xC || m == 0x13 || m == 0x15; }

// Animation channel value v / n * 0.5 + 0.5 shown on channel ch of player p; returns the value.
// fpucheck: 1 add
f32 BayTrack(s32 ch, s32 p, u8 v, f32 n) {
    const f32 f = fpu::Add(fpu::IntToFloat(v) / n * 0.5f, 0.5f);
    func_00149EB8(ch, p, f);
    return f;
}

}  // namespace

// EE: 0x001249A0
// Player p weapons per frame: gun (fire held, every 4th frame a bullet from the muzzle node(s)
// 0x30/0x31 with a random spread of C 0x0040DE48 degrees: 2 random-table reads, 4 VU0 R steps;
// muzzle flashes 0x00181BB0 for 3 frames; "out of ammo" call 0x14), bay/wing animation tracks
// 0x20-0x22 of the models with bays, standard missile (+0x14C 0x200|0x800: node 0x32/0x33 by the
// missile count parity, reload 240, calls 0xB/0x13, sound event 0x004205D0) and special weapon
// (fire request 0x2000 per kind; bursts for kind 8; each shot from node 0x34/0x3A/0x3E + slot,
// FireSpecialWeapon 0x0014FA88, camera 0x00136668, sound event 0x004205F0, calls 0xC-0xE,
// slot reload 300), reload timers 0x00124740, infinite ammo in mission kind 3.
// fpucheck: 11 static adds
void FirePlayerWeapons(s32 p) {
    const u32 u = Unit(p);
    const u32 pad = PadExt(p);
    const u32 s = W(u + 0x44);
    guest::GuestFrame fr(0xC0);
    const u32 sp = fr.Address(0), muz = fr.Address(0x10), m30 = fr.Address(0x30), mrot = fr.Address(0x60),
              node = fr.Address(0x70), ang = fr.Address(0xA0), org = fr.Address(0xB0);
    s32 fire = 0;
    if ((W(u + 0x4C) & 0x1000000u) != 0 && (W(pad) & 1u) != 0 && GRef<u16>(s + 0x158) != 0) {
        if ((W(Scene() + 0x165C) & 3u) == 0) {
            GRef<u8>(s + 0x15D) = 0;
            GRef<u8>(s + 0x15C) = 0x1E;
            GRef<s32>(s + 0x2B4) = -1;
            GRef<s32>(s + 0x2BC) = 3;
            fire = 1;
        }
        if ((W(Scene() + 0x165C) & 3u) == 2) {
            GRef<s32>(s + 0x2B8) = -1;
            GRef<s32>(s + 0x2C0) = 3;
        }
    }
    if (fire == 1 || 0 < GRef<s32>(s + 0x2BC) || 0 < GRef<s32>(s + 0x2C0)) {
        const u8 m = GRef<u8>(u + 0x69);
        const s32 guns = (m == 1 || m == 6 || m == 7) ? 2 : 1;
        for (s32 g = 0; g < guns; ++g) {
            StoreVf0(org);
            func_00149EC8(node, p, g == 0 ? 0x30 : 0x31, s);
            sceVu0ApplyMatrix(sp, node, org);
            sceVu0UnitMatrix(m30);
            func_00102D28(m30, m30, u + 0x10);
            const u32 pos = muz + 0x10u * static_cast<u32>(g);
            sceVu0ApplyMatrix(pos, m30, sp);
            sceVu0AddVector(pos, u, pos);
            sceVu0AddVector(pos, u + 0x20, pos);
            const u32 cnt = s + 0x2BC + 4u * static_cast<u32>(g);
            GRef<f32>(pos + 0xC) = 1.0f;
            if (0 < GRef<s32>(cnt)) {
                const u32 h = s + 0x2B4 + 4u * static_cast<u32>(g);
                const s32 fk = GRef<s8>(0x002A2370 + GRef<u8>(u + 0x69));
                GRef<u32>(h) = func_00181BB0(W(h), pos, u + 0x10, fk, GRef<u8>(u + 0x55));
                func_00170B60(0xE, GRef<u8>(h),
                              static_cast<u8>(GRef<u8>(0x002A2370 + GRef<u8>(u + 0x69)) | (GRef<u8>(u + 0x55) << 4)), pos,
                              u + 0x10);
                GRef<s32>(cnt) -= 1;
            }
        }
        if (fire == 1) {
            CopyQ(node, u + 0x10);
            // Four VU0 R steps; the original's spread vector built from them (stored at sp+0x80) is
            // never read.
            for (s32 k = 0; k < 4; ++k) vu0::RNext();
            const f32 c = F(0x0040DE48);
            const f32 r1 = fpu::Sub(NextRandomFloat(), 0.5f);
            GRef<f32>(node) = fpu::Add(F(node), r1 * c / 180.0f);
            const f32 r2 = fpu::Sub(NextRandomFloat(), 0.5f);
            GRef<f32>(node + 4) = fpu::Add(F(node + 4), r2 * c / 180.0f);
            if (guns == 2 && (GRef<u16>(s + 0x158) & 1) != 0)
                FireBullet(static_cast<u8>(p), muz + 0x10, node, u + 0x20);
            else
                FireBullet(static_cast<u8>(p), muz, node, u + 0x20);
            const u16 ammo = static_cast<u16>(GRef<u16>(s + 0x158) - 1);
            GRef<u16>(s + 0x158) = ammo;
            if (MissionKind() == 0 && Game(0x29) != 3 && ammo == 0)
                QueueRadioChatter(0x14, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
        }
    }
    if (BayModel(GRef<u8>(u + 0x69))) {
        const u8 t = GRef<u8>(u + 0x62);
        if (t != 0xFF && F(W(0x00410648 + 4u * static_cast<u32>(p)) + t * 0xCu + 4) <= 4000.0f &&
            (W(Unit(t) + 0x4C) & 0x8000u) != 0) {
            if (GRef<u8>(s + 0x15C) < 0x1E) GRef<u8>(s + 0x15C) = static_cast<u8>(GRef<u8>(s + 0x15C) + 1);
            GRef<u8>(s + 0x15D) = 0;
        } else if (GRef<u16>(s + 0x15C) == 0x3C1E) {
            GRef<u8>(s + 0x15C) = static_cast<u8>(GRef<u8>(s + 0x15C) - 1);
        } else if (GRef<u8>(s + 0x15C) == 0x1E) {
            GRef<u8>(s + 0x15D) = static_cast<u8>(GRef<u8>(s + 0x15D) + 1);
        } else if (GRef<u8>(s + 0x15C) != 0) {
            GRef<u8>(s + 0x15C) = static_cast<u8>(GRef<u8>(s + 0x15C) - 1);
        }
        const f32 f = BayTrack(0x20, p, GRef<u8>(s + 0x15C), 30.0f);
        GRef<u8>(s + 0x286) = static_cast<u8>(fpu::FloatToInt(f * 255.0f));
    }
    StoreVf0(sp);
    u32 fl = W(s + 0x14C);
    if ((W(u + 0x4C) & 0x1000000u) != 0 && (W(pad + 4) & 2u) != 0) {
        if ((fl & 0x8000u) == 0) {
            GRef<u32>(s + 0x14C) = fl | 0x800u;
            fl = BayModel(GRef<u8>(u + 0x69)) ? (W(s + 0x14C) | 0x400u) : (fl | 0xA00u);
            GRef<u32>(s + 0x14C) = fl;
        } else if ((~W(s + 0x188) & 0x40000u) != 0) {
            const s8 kind = GRef<s8>(s + 0x173);
            const u32 lk = W(s + 0x188);
            switch (static_cast<u32>(kind) < 0xF ? kind : -1) {
                case 3:
                case 9:
                case 11:
                case 14:
                    fl |= 0x2000u;
                    GRef<u8>(s + 0x180) = 1;
                    GRef<u32>(s + 0x14C) = fl;
                    GRef<u8>(s + 0x154) = GRef<u8>(u + 0x62);
                    break;
                case 8:
                    if (0 < GRef<s8>(s + 0x176) && (~fl & 0x4000u) != 0) {
                        GRef<u8>(s + 0x17D) = 8;
                        fl |= 0x6000u;
                        GRef<u8>(s + 0x17C) = 7;
                        GRef<u8>(s + 0x17F) = 0xC;
                        GRef<u8>(s + 0x180) = 1;
                        GRef<u8>(s + 0x154) = 0xFF;
                        GRef<u32>(s + 0x14C) = fl;
                        GRef<u8>(s + 0x17E) = 0;
                    }
                    break;
                case 10:
                case 12:
                case 13: {
                    fl |= 0x2000u;
                    GRef<u32>(s + 0x14C) = fl;
                    GRef<u8>(s + 0x180) = 0;
                    u8 n = 0;
                    for (s32 k = 0; k < static_cast<s32>(GRef<s8>(s + 0x176)); ++k) {
                        if (GRef<u8>(s + 0x154 + static_cast<u32>(k)) != 0xFF && (lk & (0x100u << k)) != 0) {
                            ++n;
                            GRef<u8>(s + 0x180) = n;
                        }
                    }
                    if (n == 0) GRef<u8>(s + 0x180) = 1;
                    break;
                }
                default:
                    fl |= 0x2000u;
                    GRef<u8>(s + 0x180) = 1;
                    GRef<u8>(s + 0x154) = 0xFF;
                    GRef<u32>(s + 0x14C) = fl;
                    break;
            }
            if (!BayModel(GRef<u8>(u + 0x69))) {
                fl |= 0x1000u;
                GRef<u32>(s + 0x14C) = fl;
            }
        }
    }
    fl = W(s + 0x14C);
    if ((fl & 0xA00u) == 0xA00u) {
        const u8 n = GRef<u8>(s + 0x15A);
        fl &= 0xFFFFF7FFu;
        GRef<u32>(s + 0x14C) = fl;
        const u32 side = n & 1u;
        const u32 tm = s + 0x15E + 2 * side;
        if (n != 0 && GRef<s16>(tm) == 0) {
            sceVu0UnitMatrix(mrot);
            func_00102D28(mrot, mrot, u + 0x10);
            func_00149EC8(fr.Address(0x20), p, side != 0 ? 0x33 : 0x32, s);
            sceVu0MulMatrix(fr.Address(0x20), mrot, fr.Address(0x20));
            sceVu0ApplyMatrix(muz, fr.Address(0x20), sp);
            sceVu0AddVector(muz, u, muz);
            GRef<f32>(muz + 0xC) = 1.0f;
            CopyQ(ang, u + 0x10);
            const u8 tgt = (W(s + 0x188) & 0xF00u) != 0 ? GRef<u8>(u + 0x62) : 0xFF;
            GRef<u16>(s + 0x162) = static_cast<u16>(FireMissile(static_cast<u8>(p), tgt, muz, ang, u + 0x20));
            FollowProjectile(p, 0, GRef<s16>(s + 0x162));
            GRef<s32>(0x004205D0) = p;
            GRef<u32>(0x004205D4) = 0xF;
            GRef<s32>(0x004205D8) = GRef<s8>(s + 0x149);
            CopyQ(0x004205E0, muz);
            func_0021AE30(0x004205D0);
            if (MissionKind() == 0 && Game(0x29) != 3) QueueRadioChatter(0xB, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            GRef<u16>(tm) = 0xF0;
            const u8 left = static_cast<u8>(GRef<u8>(s + 0x15A) - 1);
            GRef<u8>(s + 0x15A) = left;
            if (MissionKind() == 0 && Game(0x29) != 3 && left == 0)
                QueueRadioChatter(0x13, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            fl = W(s + 0x14C);
        }
    }
    if ((fl & 0x4000u) != 0) {
        if (GRef<u8>(s + 0x17C) == 0) {
            GRef<u32>(s + 0x14C) = fl & 0xFFFFBFFFu;
        } else {
            u8 a = GRef<u8>(s + 0x17E);
            if (!(a < GRef<u8>(s + 0x17F))) {
                GRef<u8>(s + 0x17E) = 0;
                GRef<u8>(s + 0x17C) = static_cast<u8>(GRef<u8>(s + 0x17C) - 1);
                GRef<u32>(s + 0x14C) = fl | 0x2000u;
                a = 0;
            }
            GRef<u8>(s + 0x17E) = static_cast<u8>(a + 1);
        }
    }
    if ((W(s + 0x14C) & 0x3000u) == 0x3000u) {
        GRef<u32>(s + 0x14C) = W(s + 0x14C) & 0xFFFFDFFFu;
        for (u32 k = 0; k < GRef<u8>(s + 0x180); ++k) {
            if (GRef<u8>(s + 0x15B) == 0 || !(0 < GRef<s8>(s + 0x176))) continue;
            u32 slot, nd;
            if (GRef<s8>(s + 0x174) == 1) {
                slot = 0;
                nd = GRef<u8>(s + 0x15B) & 1u;
            } else {
                const s32 c = GRef<s8>(s + 0x175);
                const s32 r = static_cast<s32>(GRef<u8>(s + 0x15B)) % c;
                slot = r != 0 ? static_cast<u32>(c - r) : 0u;
                nd = slot;
            }
            const s8 sk = GRef<s8>(s + 0x172);
            const s32 base = sk == 1 ? 0x3A : (sk == 2 ? 0x3E : 0x34);
            sceVu0UnitMatrix(mrot);
            func_00102D28(mrot, mrot, u + 0x10);
            const u32 nm = fr.Address(0x20);
            func_00149EC8(nm, p, base + static_cast<s32>(nd), s);
            sceVu0MulMatrix(nm, mrot, nm);
            sceVu0ApplyMatrix(muz, nm, sp);
            sceVu0AddVector(muz, u, muz);
            GRef<f32>(muz + 0xC) = 1.0f;
            func_00103290(ang, nm);
            if (GRef<s8>(s + 0x173) == 8) {
                const f32 c = F(0x0040DE4C);
                const f32 r1 = fpu::Sub(NextRandomFloat(), 0.5f);
                GRef<f32>(ang) = fpu::Add(F(ang), r1 * 3.0f * c / 180.0f);
                const f32 r2 = fpu::Sub(NextRandomFloat(), 0.5f);
                GRef<f32>(ang + 4) = fpu::Add(F(ang + 4), r2 * 3.0f * c / 180.0f);
            }
            const u8 tgt = (W(s + 0x188) & (0x100u << k)) != 0 ? GRef<u8>(s + 0x154 + k) : 0xFF;
            GRef<u16>(s + 0x178) = static_cast<u16>(
                FireSpecialWeapon(GRef<s8>(s + 0x173), p, tgt, muz, ang, u + 0x20, GRef<u8>(s + 0x183)));
            const s8 kind = GRef<s8>(s + 0x173);
            if (kind == 6) GRef<u8>(s + 0x289) = 0x3C;
            if (kind == 8) {
                if (GRef<u8>(s + 0x17C) == 0) {
                    FollowProjectile(p, 1, GRef<s16>(s + 0x178));
                } else {
                    const u32 cam = W(0x00410680 + 4u * static_cast<u32>(p));
                    GRef<u32>(cam + 0xC) = W(cam + 0xC) & 0xFFFFFFFEu;
                }
                GRef<u8>(s + 0x289 + nd) = 10;
            } else {
                FollowProjectile(p, 1, GRef<s16>(s + 0x178));
            }
            GRef<s32>(0x004205F0) = p;
            GRef<s32>(0x004205F8) = GRef<s8>(s + 0x149);
            GRef<s32>(0x004205F4) = GRef<s8>(s + 0x173);
            CopyQ(0x00420600, muz);
            const s32 ev = GRef<s32>(0x004205F4);
            if (ev == 9 || ev == 0xD || ev == 0xC || ev == 0xB) GRef<s32>(0x004205F4) = 0x10;
            func_0021AE30(0x004205F0);
            if (MissionKind() == 0 && Game(0x29) != 3) {
                const s8 k2 = GRef<s8>(s + 0x173);
                if (static_cast<u32>(k2) < 0xF) {
                    if (k2 == 0xC)
                        QueueRadioChatter(0xC, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
                    else if (k2 == 0xD)
                        QueueRadioChatter(0xD, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
                    else if (k2 < 8 || k2 == 9)
                        QueueRadioChatter(0xE, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
                }
            }
            if (GRef<u8>(s + 0x17C) == 0) GRef<u16>(s + 0x166 + 2 * slot) = 300;
            GRef<u8>(s + 0x15B) = static_cast<u8>(GRef<u8>(s + 0x15B) - 1);
        }
    }
    if (BayModel(GRef<u8>(u + 0x69))) {
        u32 t0 = W(s + 0x14C);
        if ((~t0 & 0x8000u) != 0 && (W(s + 0x188) & 0x1100u) != 0) {
            t0 |= 0x400u;
            GRef<u32>(s + 0x14C) = t0;
        }
        u8 a;
        if ((t0 & 0x400u) != 0) {
            a = GRef<u8>(s + 0x164);
            if (a < 0x18) {
                a = static_cast<u8>(a + 1);
                GRef<u8>(s + 0x164) = a;
            } else {
                t0 &= 0xFFFFFBFFu;
                GRef<u32>(s + 0x14C) = t0;
            }
            GRef<u8>(s + 0x165) = 0;
        } else if (GRef<u16>(s + 0x164) == 0xB418) {
            a = static_cast<u8>(GRef<u8>(s + 0x164) - 1);
            GRef<u8>(s + 0x164) = a;
        } else {
            a = GRef<u8>(s + 0x164);
            if (a == 0x18) {
                GRef<u8>(s + 0x165) = static_cast<u8>(GRef<u8>(s + 0x165) + 1);
            } else if (a != 0) {
                a = static_cast<u8>(a - 1);
                GRef<u8>(s + 0x164) = a;
            }
        }
        GRef<u32>(s + 0x14C) = a < 0x18 ? (t0 & 0xFFFFFDFFu) : (t0 | 0x200u);
        const f32 f = BayTrack(0x21, p, GRef<u8>(s + 0x164), 24.0f);
        GRef<u8>(s + 0x287) = static_cast<u8>(fpu::FloatToInt(f * 255.0f));
    }
    if (BayModel(GRef<u8>(u + 0x69))) {
        const u32 t0 = W(s + 0x14C);
        u8 v = GRef<u8>(s + 0x17A);
        if ((t0 & 0x8000u) != 0) {
            if (v < 0x18) {
                v = static_cast<u8>(v + 1);
                GRef<u8>(s + 0x17A) = v;
            }
        } else if (v != 0) {
            v = static_cast<u8>(v - 1);
            GRef<u8>(s + 0x17A) = v;
        }
        GRef<u32>(s + 0x14C) = v < 0x18 ? (t0 & 0xFFFFEFFFu) : (t0 | 0x1000u);
        const f32 f = BayTrack(0x22, p, GRef<u8>(s + 0x17A), 24.0f);
        GRef<u8>(s + 0x288) = static_cast<u8>(fpu::FloatToInt(f * 255.0f));
        const s8 sk = GRef<s8>(s + 0x172);
        s32 base, n;
        if (sk == 1) {
            base = 0x3A;
            n = 6;
        } else {
            base = sk == 2 ? 0x3E : 0x34;
            n = 4;
        }
        for (s32 k = 0; k < n; ++k) func_00149EB8(base + k, p, f);
    }
    GRef<u8>(s + 0x186) = 0;
    func_00124740(p, s);
    if (MissionKind() == 3) {
        const u32 ld = W(s + 0x110);
        if (GRef<u8>(s + 0x15A) == 0) GRef<u8>(s + 0x15A) = GRef<u8>(ld + 2);
        if (GRef<u16>(s + 0x158) == 0) GRef<u16>(s + 0x158) = GRef<u16>(ld);
        if (GRef<u8>(s + 0x15B) == 0)
            GRef<u8>(s + 0x15B) = GRef<u8>(ld + static_cast<u32>(static_cast<s32>(GRef<s8>(s + 0x172)) * 4) + 6);
    }
}

}  // namespace ac04::game
