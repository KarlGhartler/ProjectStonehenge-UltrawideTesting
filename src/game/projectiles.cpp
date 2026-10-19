// Projectiles (slice S5d): the pools of block *0x004106AC (docs/subsystems/weapons.md "Projectile
// pools"): bullets, missiles, flak bursts, shells, bombs, cannon shells and special weapons; per
// frame AgeProjectiles 0x0014BAF8 then UpdateProjectiles 0x0014BB40. Slot numbers must match the
// original (replay events refer to them).
#include "game/game.h"

#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Pool() { return W(0x004106AC); }
u32 Bullet(u32 i) { return Pool() + i * 0x40; }
u32 Missile(u32 i) { return Pool() + i * 0x60 + 0x4000; }
u32 Flak(u32 i) { return Pool() + i * 0x30 + 0x5C00; }
u32 Shell(u32 i) { return Pool() + i * 0x40 + 0x6800; }

// 64-bit occupancy masks (EE ld/sd, dsllv).
u64& Mask64(u32 off) { return GRef<u64>(Pool() + off); }
bool Busy64(u32 off, u32 i) { return ((Mask64(off) >> (i & 0x3F)) & 1u) != 0; }
void Free64(u32 off, u32 i) { Mask64(off) &= ~(1ull << (i & 0x3F)); }

}  // namespace

// EE: 0x0014BAF8
// Projectile pre-step of every kind (lifetimes, gravity).
void AgeProjectiles() {
    func_0014BCB0();
    func_0014CC08();
    func_0014F5E8();
    func_0014DAD0();
    func_0014DF78();
    func_0014E8C8();
    func_0014ECD8();
}

// EE: 0x0014BB40
// Projectile movement and hit tests of every kind.
void UpdateProjectiles() {
    func_0014BE10();
    func_0014D138();
    func_0014F7C8();
    func_0014DB48();
    func_0014DFF0();
    func_0014E940();
    func_0014EDE0();
}

// EE: 0x0014BCB0
// Ages every live bullet (4 x 64 mask words at +0x8A00).
void func_0014BCB0() {
    for (u32 w = 0; w < 4; ++w) {
        if (GRef<u64>(Pool() + 0x8A00 + 8 * w) == 0) continue;
        for (u32 b = 0; b < 0x40; ++b)
            if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A00 + 8 * w)) >> b & 1) == 0) func_0014BD88(w * 0x40 + b);
    }
}

// EE: 0x0014BD88
// Bullet i: age +0x34; freed when age * C 0x0040E51C reaches C 0x0040E520, else gravity on vy.
// fpucheck: 1 add
void func_0014BD88(u32 i) {
    const u32 b = Bullet(i);
    const s32 age = GRef<s32>(b + 0x34) + 1;
    GRef<s32>(b + 0x34) = age;
    if (F(0x0040E520) <= fpu::IntToFloat(age) * F(0x0040E51C)) {
        GRef<u64>(Pool() + 0x8A00 + 8 * (i >> 6)) &= ~(1ull << (i & 0x3F));
        return;
    }
    GRef<f32>(b + 0x24) = fpu::Add(F(b + 0x24), F(0x0040E524));
}

// EE: 0x0014BE10
// Bullet hit tests: first the per-unit "hittable" bytes in the scratchpad (0x70000000 + i: in
// range +0x4C 0x4000, active 0x10000000, alive or a part (+0x6A 9), updated 0x100, bullet target
// class +0x48 0x200000), then every live bullet.
void func_0014BE10() {
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 sp = 0x70000000u + static_cast<u32>(i);
        const u32 u = Unit(i);
        GRef<u8>(sp) = 1;
        u32 f = W(u + 0x4C);
        if ((~f & 0x4000u) != 0) {
            GRef<u8>(sp) = 0;
            f = W(u + 0x4C);
        }
        if ((~f & 0x10000000u) != 0) {
            GRef<u8>(sp) = 0;
            f = W(u + 0x4C);
        }
        if (GRef<u8>(u + 0x6A) != 9 && (~f & 0x1000000u) != 0) {
            GRef<u8>(sp) = 0;
            f = W(u + 0x4C);
        }
        if ((~f & 0x100u) != 0) GRef<u8>(sp) = 0;
        if ((~W(u + 0x48) & 0x200000u) != 0) GRef<u8>(sp) = 0;
    }
    for (u32 w = 0; w < 4; ++w) {
        if (GRef<u64>(Pool() + 0x8A00 + 8 * w) == 0) continue;
        for (u32 b = 0; b < 0x40; ++b)
            if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A00 + 8 * w)) >> b & 1) == 0) func_0014BFC0(w * 0x40 + b);
    }
}

// EE: 0x0014BFC0
// Bullet i against the hittable units (not the shooter; an AI shooter's own side skipped): a hit
// sets hit flag 1 (enemies), the attacker, clears the target's push vector, frees the bullet with a
// spark (0x00154558 kind 3) and SE 5; a player hitting its own side gets radio call 0x15. Then
// against the terrain (spark by material, SE 3 / 4), else the bullet moves.
void func_0014BFC0(u32 i) {
    const u32 b = Bullet(i);
    const u32 sh = Unit(GRef<s32>(b + 0x38));
    const s8 sk = GRef<s8>(sh + 0x54);
    const u32 side = GRef<u8>(sh + 0x48) & 3u;
    guest::GuestFrame fr(0x190);
    const u32 col = fr.Address(0), hit = fr.Address(0x160), nrm = fr.Address(0x170), attr = fr.Address(0x180);
    func_0017C758(col, b);
    const u32 w = i >> 6, bit = i & 0x3F;
    for (s32 j = 0; j < UnitCount(); ++j) {
        if (GRef<u8>(0x70000000u + static_cast<u32>(j)) == 0) continue;
        if (GRef<s32>(b + 0x38) == j) continue;
        const u32 o = Unit(j);
        if (sk != 0 && side == (W(o + 0x48) & 3u)) continue;
        if (func_0017C778(col, o) != 1) continue;
        if (side != (W(o + 0x48) & 3u)) {
            GRef<u32>(o + 0x50) |= 1u;
        } else if (MissionKind() == 0 && Game(0x29) != 3) {
            QueueRadioChatter(0x15, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
        }
        GRef<u8>(o + 0x63) = GRef<u8>(b + 0x38);
        GRef<u64>(Pool() + 0x8A00 + 8 * w) &= ~(1ull << bit);
        ZeroQ(o + 0x30);
        ZeroQ(hit);
        SpawnImpactEffect(b, hit, hit, 3);
        PlayEventSe(5, b);
        break;
    }
    if (((GRef<u64>(Pool() + 0x8A00 + 8 * w) >> bit) & 1) == 0) return;
    if (func_0017C8D0(col, hit, attr, nrm) == 1) {
        const s32 m = GRef<s32>(attr);
        if (m == 1) {
            SpawnImpactEffect(hit, b + 0x20, nrm, 0);
            PlayEventSe(3, hit);
        } else if (m == 2) {
            SpawnImpactEffect(hit, b + 0x20, nrm, 1);
            PlayEventSe(4, hit);
        } else if (m == 3) {
            SpawnImpactEffect(hit, b + 0x20, nrm, 2);
            PlayEventSe(3, hit);
        } else {
            SpawnImpactEffect(hit, b + 0x20, nrm, 3);
            PlayEventSe(3, hit);
        }
        GRef<u64>(Pool() + 0x8A00 + 8 * w) &= ~(1ull << bit);
    }
    if (((GRef<u64>(Pool() + 0x8A00 + 8 * w) >> bit) & 1) != 0) sceVu0AddVector(b, b, b + 0x20);
}

// EE: 0x0014C8F0
// Fires a bullet for unit `owner` (players 0/1 and others have their own slot ranges and round-robin
// cursors +0x8A4E / +0x8A4F / +0x8A4D; first free slot from the cursor); replay event in sub 0xB.
u32 FireBullet(u8 owner, u32 pos, u32 ang, u32 vel) {
    const u32 P = Pool();
    u32 s;
    if (owner == 0)
        s = GRef<u8>(P + 0x8A4E);
    else if (owner == 1)
        s = GRef<u8>(P + 0x8A4F);
    else
        s = GRef<u8>(P + 0x8A4D);
    if (!(s < 0x100)) s = 0;
    auto busy = [&](u32 k) { return ((~GRef<u64>(P + 0x8A00 + 8 * (k >> 6)) >> (k & 0x3F)) & 1) == 0; };
    if (busy(s)) {
        for (s32 n = 1;; ++n) {
            ++s;
            if (!(n < 0x100)) break;
            if (!(s < 0x100)) s = 0;
            if (!busy(s)) break;
        }
    }
    if (owner == 0) {
        GRef<u8>(P + 0x8A4E) = static_cast<u8>(s);
        if (!(GRef<u8>(P + 0x8A4E) < 0x40)) GRef<u8>(P + 0x8A4E) = 0;
    } else if (owner == 1) {
        GRef<u8>(P + 0x8A4F) = static_cast<u8>(s);
        if (GRef<s8>(P + 0x8A4F) < 0) GRef<u8>(P + 0x8A4F) = 0x40;
    } else {
        GRef<u8>(P + 0x8A4D) = static_cast<u8>(s);
        if (!(GRef<s8>(P + 0x8A4D) < 0)) GRef<u8>(P + 0x8A4D) = 0x80;
    }
    func_0014CAE0(s, owner, pos, ang, vel);
    if (Sub() == 0xB) func_00170240(0.0f, static_cast<s32>(s), 0xFF, owner, 0, pos, ang, vel, 0);
    return 0;
}

// EE: 0x0014CAE0
// Bullet slot setup: owner, age 0, position, angles, the shooter's velocity plus the muzzle speed
// C 0x0040E540 along the nose.
// fpucheck: 1 add
void func_0014CAE0(u32 i, s32 owner, u32 pos, u32 ang, u32 vel) {
    const u32 b = Bullet(i);
    GRef<s32>(b + 0x38) = owner;
    GRef<u64>(Pool() + 0x8A00 + 8 * (i >> 6)) |= 1ull << (i & 0x3F);
    GRef<u32>(b + 0x34) = 0;
    CopyQ(b, pos);
    CopyQ(b + 0x10, ang);
    CopyQ(b + 0x20, vel);
    guest::GuestFrame fr(0xC0);
    const u32 m = fr.Address(0), inv = fr.Address(0x40), loc = fr.Address(0x80);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, b + 0x10);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(loc, inv, b + 0x20);
    GRef<f32>(loc + 8) = fpu::Add(F(loc + 8), F(0x0040E540));
    sceVu0ApplyMatrix(b + 0x20, m, loc);
}

// EE: 0x0014CC08
// Missile guidance of every live missile (mask +0x8A28).
void func_0014CC08() {
    for (u32 i = 0; i < 0x20; ++i)
        if (((static_cast<s32>(~W(Pool() + 0x8A28)) >> i) & 1) == 0) func_0014CC70(i);
}

// EE: 0x0014CC70
// Missile i per frame: after a hit the slot lingers 180 frames (+0x5A); life 6 s (age * C
// 0x0040E544, then a "missed" message 0xD / call 0xF for the player or notification 7 for an AI
// friend); seeker after C 0x0040E564: the target's predicted point in the missile frame, off-axis
// angle within the cone -> incoming flag 0x100 and missile index on the target, turn limited per
// frame; outside the cone the lock is lost (missed message once, flag 0x10). Player missiles use
// constants, AI missiles the difficulty tables 0x002B67C0 (cone) / 0x002B67D8 (turn); speed +0x38
// accelerates to its maximum; drift damped by C 0x0040E570.
// fpucheck: 2 static adds
void func_0014CC70(u32 i) {
    const u32 m = Missile(i);
    if ((W(m + 0x5C) & 1u) != 0) {
        const u16 t = static_cast<u16>(GRef<u16>(m + 0x5A) + 1);
        GRef<u16>(m + 0x5A) = t;
        if (t < 0xB4) return;
        GRef<u16>(m + 0x5A) = 0;
        GRef<u32>(Pool() + 0x8A28) = W(Pool() + 0x8A28) & ~(1u << i);
        return;
    }
    const s32 age = GRef<s32>(m + 0x50) + 1;
    GRef<s32>(m + 0x50) = age;
    if (6.0f <= fpu::IntToFloat(age) * F(0x0040E544)) {
        GRef<u32>(Pool() + 0x8A28) = W(Pool() + 0x8A28) & ~(1u << i);
        const u8 tg = GRef<u8>(m + 0x59);
        if (tg == 0xFF) return;
        if ((W(Unit(tg) + 0x4C) & 0x1000000u) == 0) return;
        const u8 ow = GRef<u8>(m + 0x58);
        const u32 o = Unit(ow);
        if (GRef<u8>(o + 0x54) == 0) {
            if ((~W(m + 0x5C) & 0x10u) == 0) return;
            GRef<u32>(m + 0x5C) = W(m + 0x5C) | 0x10u;
            ShowCenterMessage(ow, 0xD, 0);
            QueueRadioChatter(0xF, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            return;
        }
        if (GRef<u8>(o + 0x54) != 1) return;
        if ((W(o + 0x48) & 3u) != 0) return;
        func_0011E560(7, o, 0);
        return;
    }
    f32 vmax, acc, cone, turn;
    if (GRef<u8>(Unit(GRef<u8>(m + 0x58)) + 0x54) == 0) {
        vmax = F(0x0040E548);
        acc = F(0x0040E54C);
        cone = F(0x0040E550);
        turn = F(0x0040E554);
    } else {
        vmax = F(0x0040E558);
        acc = F(0x0040E55C);
        turn = F(0x002B67D8 + 4u * Game(0x28));
        cone = F(0x002B67C0 + 4u * Game(0x28));
    }
    guest::GuestFrame fr(0xA0);
    const u32 rm = fr.Address(0), inv = fr.Address(0x40), d = fr.Address(0x80), aim = fr.Address(0x90);
    sceVu0UnitMatrix(rm);
    func_00102D28(rm, rm, m + 0x10);
    sceVu0InversMatrix(inv, rm);
    const u8 tg = GRef<u8>(m + 0x59);
    if (tg != 0xFF && F(0x0040E564) < fpu::IntToFloat(GRef<s32>(m + 0x50)) * F(0x0040E560)) {
        const u32 t = Unit(tg);
        CopyQ(aim, t + 0x20);
        sceVu0AddVector(aim, aim, t);
        sceVu0SubVector(d, aim, m);
        sceVu0ApplyMatrix(d, inv, d);
        const f32 yaw = ee::atan2f(F(d), -F(d + 4));
        // sqrt.S; the original's NaN fallback to sqrtf cannot trigger on the EE.
        const f32 off = ee::atan2f(fpu::Sqrt(fpu::Add(F(d) * F(d), F(d + 4) * F(d + 4))), F(d + 8));
        if (off < cone && -cone < off) {
            GRef<u8>(t + 0x66) = static_cast<u8>(i);
            GRef<u32>(t + 0x50) = W(t + 0x50) | 0x100u;
            if (turn < off)
                GRef<f32>(m + 0x40) = turn;
            else if (off < -turn)
                GRef<f32>(m + 0x40) = -turn;
            else
                GRef<f32>(m + 0x40) = off;
            GRef<f32>(m + 0x18) = yaw;
            GRef<u32>(m + 0x44) = 0;
            GRef<u32>(m + 0x48) = 0;
            GRef<u32>(m + 0x4C) = 0;
            func_00102EF0(m + 0x10, m + 0x10, m + 0x40);
            GRef<u32>(m + 0x18) = 0;
        } else if (F(0x0040E568) < off || off < F(0x0040E56C)) {
            const u8 ow = GRef<u8>(m + 0x58);
            if (GRef<u8>(Unit(ow) + 0x54) == 0 && (~W(m + 0x5C) & 0x10u) != 0) {
                GRef<u32>(m + 0x5C) = W(m + 0x5C) | 0x10u;
                ShowCenterMessage(ow, 0xD, 0);
                QueueRadioChatter(0xF, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            }
        }
    }
    if (F(m + 0x38) < vmax) GRef<f32>(m + 0x38) = fpu::Add(F(m + 0x38), acc);
    const f32 k = F(0x0040E570);
    GRef<f32>(m + 0x30) = F(m + 0x30) * k;
    GRef<f32>(m + 0x34) = F(m + 0x34) * k;
    sceVu0ApplyMatrix(m + 0x20, rm, m + 0x30);
}

// EE: 0x0014D138
// Hit tests of every live missile still flying.
void func_0014D138() {
    for (u32 i = 0; i < 0x20; ++i)
        if (((static_cast<s32>(~W(Pool() + 0x8A28)) >> i) & 1) == 0 && (W(Missile(i) + 0x5C) & 1u) == 0) func_0014D1C0(i);
}

// EE: 0x0014D1C0
// Missile i: swept segment against units (active, alive or parts; AI missiles skip their side):
// hit flag 2 on enemies, the attacker, the push vector = missile velocity, explosion 0xB5 (ground,
// SE 0) or 0x81 (air, SE 1) and the slot lingers; else terrain (0x00154428, missed message for
// players); else it moves with its smoke trail (colour 0x30 in mission kind 2, 0xD2 players,
// 0x60 AI).
void func_0014D1C0(u32 i) {
    const u32 m = Missile(i);
    const u32 vel = m + 0x20;
    const u8 ow = GRef<u8>(m + 0x58);
    const u32 sh = Unit(ow);
    const u8 sk = GRef<u8>(sh + 0x54);
    const u32 side = GRef<u8>(sh + 0x48) & 3u;
    guest::GuestFrame fr(0x190);
    const u32 col = fr.Address(0), hit = fr.Address(0x160), nrm = fr.Address(0x170), attr = fr.Address(0x180);
    func_0017C940(col, m + 0x10, m, vel, ow);
    for (s32 j = 0; j < UnitCount(); ++j) {
        if (GRef<u8>(m + 0x58) == static_cast<u32>(j)) continue;
        const u32 o = Unit(j);
        if ((~W(o + 0x4C) & 0x10000000u) != 0) continue;
        if (GRef<u8>(o + 0x6A) != 9 && (~W(o + 0x4C) & 0x1000000u) != 0) continue;
        if (sk != 0 && side == (W(o + 0x48) & 3u)) continue;
        if (func_0017C9B8(col, o, hit, nrm, j) != 1) continue;
        if (side != (W(o + 0x48) & 3u)) GRef<u32>(o + 0x50) |= 2u;
        GRef<u8>(o + 0x63) = GRef<u8>(m + 0x58);
        CopyQ(o + 0x30, vel);
        GRef<u32>(m + 0x5C) = W(m + 0x5C) | 1u;
        if ((W(o + 0x48) & 0x1000000u) == 0) {
            PlayEventSe(0, m);
            func_001E94F8(m, vel, 0xB5);
        } else {
            PlayEventSe(1, m);
            func_001E9518(m, o + 0x20, vel, 0x81);
        }
        break;
    }
    if ((W(m + 0x5C) & 1u) != 0) return;
    if (func_0017CB70(col, hit, nrm, attr) == 1) {
        GRef<u32>(m + 0x5C) = W(m + 0x5C) | 1u;
        func_00154428(hit, vel, nrm, GRef<s32>(attr));
        const u8 o2 = GRef<u8>(m + 0x58);
        if (GRef<u8>(Unit(o2) + 0x54) == 0 && (~W(m + 0x5C) & 0x10u) != 0) {
            GRef<u32>(m + 0x5C) = W(m + 0x5C) | 0x10u;
            ShowCenterMessage(o2, 0xD, 0);
        }
    }
    if ((W(m + 0x5C) & 1u) != 0) return;
    sceVu0AddVector(m, m, vel);
    s32 colr;
    if (GRef<s8>(Scene() + 0x1653) == 2)
        colr = 0x30;
    else
        colr = GRef<u8>(m + 0x58) < 2 ? 0xD2 : 0x60;
    GRef<u32>(m + 0x54) = func_001DAB18(W(m + 0x54), m, vel, colr, 1);
}

// EE: 0x0014D8B0
// Fires a missile from unit owner at target (ring counter +0x8A44, overwrites the oldest); replay
// event in sub 0xB. Returns the slot.
u32 FireMissile(u8 owner, u8 target, u32 pos, u32 rot, u32 vel) {
    const u32 P = Pool();
    const s16 n = GRef<s16>(P + 0x8A44);
    GRef<s16>(P + 0x8A44) = static_cast<s16>(n + 1);
    if (static_cast<s16>(n + 1) == 0x20) GRef<u16>(P + 0x8A44) = 0;
    func_0014D990(n, owner, target, pos, rot, vel);
    if (Sub() == 0xB) func_00170240(0.0f, n, 0xFE, owner, target, pos, rot, vel, 0);
    return static_cast<u32>(static_cast<s32>(n));
}

// EE: 0x0014D990
// Missile slot setup: owner, target, trail none, age/flags 0, position / rotation / velocity, the
// velocity in the owner's frame (+0x30), turn angles 0.
void func_0014D990(s32 n, s32 owner, u8 target, u32 pos, u32 rot, u32 vel) {
    const u32 m = Missile(static_cast<u32>(n));
    GRef<u32>(Pool() + 0x8A28) = W(Pool() + 0x8A28) | (1u << (n & 0x1F));
    const u32 o = Unit(owner);
    GRef<u8>(m + 0x58) = static_cast<u8>(owner);
    GRef<s32>(m + 0x54) = -1;
    GRef<u8>(m + 0x59) = target;
    GRef<u32>(m + 0x50) = 0;
    GRef<u32>(m + 0x5C) = 0;
    GRef<u16>(m + 0x5A) = 0;
    CopyQ(m, pos);
    CopyQ(m + 0x10, rot);
    CopyQ(m + 0x20, vel);
    guest::GuestFrame fr(0x80);
    const u32 rm = fr.Address(0), inv = fr.Address(0x40);
    sceVu0UnitMatrix(rm);
    func_00102D28(rm, rm, o + 0x10);
    sceVu0InversMatrix(inv, rm);
    sceVu0ApplyMatrix(m + 0x30, inv, m + 0x20);
    GRef<u32>(m + 0x40) = 0;
    GRef<u32>(m + 0x48) = 0;
    GRef<u32>(m + 0x44) = 0;
}

// EE: 0x0014DAD0
// Ages every live flak burst (mask +0x8A20).
void func_0014DAD0() {
    if (GRef<u64>(Pool() + 0x8A20) == 0) return;
    for (u32 i = 0; i < 0x40; ++i)
        if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A20)) >> i & 1) == 0) func_0014DBC8(i);
}

// EE: 0x0014DB48
// Hit tests of every live flak burst.
void func_0014DB48() {
    if (GRef<u64>(Pool() + 0x8A20) == 0) return;
    for (u32 i = 0; i < 0x40; ++i)
        if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A20)) >> i & 1) == 0) func_0014DC38(i);
}

// EE: 0x0014DBC8
// Flak burst i: freed when age * C 0x0040E574 reaches C 0x0040E578.
void func_0014DBC8(u32 i) {
    const u32 f = Flak(i);
    const s32 age = GRef<s32>(f + 4) + 1;
    GRef<s32>(f + 4) = age;
    if (F(0x0040E578) <= fpu::IntToFloat(age) * F(0x0040E574)) Free64(0x8A20, i);
}

// EE: 0x0014DC38
// Flak burst i against the players (not the owner; +0x48 0x200000): within 200 sets hit flag 8,
// the attacker and the push direction (+0x30), and frees the burst.
void func_0014DC38(u32 i) {
    const u32 f = Flak(i);
    guest::GuestFrame fr(0x10);
    const u32 d = fr.Address();
    for (s32 j = 0; j < Players(); ++j) {
        const u32 o = Unit(j);
        if (GRef<s32>(f + 8) == j) continue;
        if ((~W(o + 0x4C) & 0x10000000u) != 0) continue;
        if ((~W(o + 0x48) & 0x200000u) != 0) continue;
        sceVu0SubVector(d, o, f + 0x10);
        const f32 x = F(d), y = F(d + 4), z = F(d + 8);
        if (200.0f < x || x < -200.0f || 200.0f < y || y < -200.0f || 200.0f < z || z < -200.0f) continue;
        const f32 len = GetVectorLength(d);
        if (!(len < 200.0f)) continue;
        GRef<u32>(o + 0x50) = W(o + 0x50) | 8u;
        GRef<u8>(o + 0x63) = GRef<u8>(f + 8);
        sceVu0ScaleVector(o + 0x30, d, 1.0f / len);
        Free64(0x8A20, i);
    }
}

// EE: 0x0014DE08
// Spawns a flak burst of unit owner at pos (ring counter +0x8A48); replay event in sub 0xB.
s16 SpawnFlakBurst(u8 owner, u32 pos) {
    const u32 P = Pool();
    const s16 n = GRef<s16>(P + 0x8A48);
    GRef<s16>(P + 0x8A48) = static_cast<s16>(n + 1);
    if (static_cast<u16>(n + 1) == 0x40) GRef<u16>(P + 0x8A48) = 0;
    func_0014DEB8(n, owner, pos);
    if (Sub() == 0xB) {
        guest::GuestFrame fr(0x10);
        ZeroQ(fr.Address());
        func_00170240(0.0f, n, 0xFD, owner, 0, pos, fr.Address(), fr.Address(), 0);
    }
    return n;
}

// EE: 0x0014DEB8
// Flak burst slot setup with its explosion (0x001F08E8 kind 0x81) and SE 7.
void func_0014DEB8(s32 n, s32 owner, u32 pos) {
    const u32 f = Flak(static_cast<u32>(n));
    GRef<u64>(Pool() + 0x8A20) |= 1ull << (n & 0x3F);
    GRef<s32>(f + 8) = owner;
    GRef<u32>(f + 4) = 0;
    CopyQ(f + 0x10, pos);
    guest::GuestFrame fr(0x10);
    ZeroQ(fr.Address());
    func_001F08E8(f + 0x10, fr.Address(), 0x81);
    PlayEventSe(7, f + 0x10);
}

// EE: 0x0014DF78
// Ages every live shell (mask +0x8A30).
void func_0014DF78() {
    if (GRef<u64>(Pool() + 0x8A30) == 0) return;
    for (u32 i = 0; i < 0x40; ++i)
        if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A30)) >> i & 1) == 0) func_0014E070(i);
}

// EE: 0x0014DFF0
// Moves every live shell.
void func_0014DFF0() {
    if (GRef<u64>(Pool() + 0x8A30) == 0) return;
    for (u32 i = 0; i < 0x40; ++i)
        if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A30)) >> i & 1) == 0) func_0014E0E8(i);
}

// EE: 0x0014E070
// Shell i: gravity C 0x0040E57C on vy, age +4; kind 3 shells leave a trail (0x001DF858).
// fpucheck: 1 add
void func_0014E070(u32 i) {
    const u32 s = Shell(i);
    const f32 vy = fpu::Add(F(s + 0x34), F(0x0040E57C));
    GRef<s32>(s + 4) += 1;
    GRef<f32>(s + 0x34) = vy;
    if (GRef<s16>(s + 0xE) == 3) {
        guest::GuestFrame fr(0x10);
        sceVu0AddVector(fr.Address(), s + 0x10, s + 0x30);
        GRef<u16>(s + 2) = static_cast<u16>(func_001DF858(GRef<s16>(s + 2), fr.Address()));
    }
}

// EE: 0x0014E0E8
// Shell i moves; at the end of its life or on terrain contact it explodes (SE 6, explosion 0xB5).
void func_0014E0E8(u32 i) {
    const u32 s = Shell(i);
    guest::GuestFrame fr(0x40);
    const u32 nx = fr.Address(0), nrm = fr.Address(0x10), z = fr.Address(0x20), attr = fr.Address(0x30);
    sceVu0AddVector(nx, s + 0x10, s + 0x30);
    if (GRef<s32>(s + 8) < GRef<s32>(s + 4) || func_0013D698(Terrain(), nx, attr, s + 0x10, nx, nrm) == 1) {
        Free64(0x8A30, i);
        PlayEventSe(6, nx);
        ZeroQ(z);
        func_001F08E8(nx, z, 0xB5);
    }
    CopyQ(s + 0x10, nx);
}

// EE: 0x0014E1C8
// Fires a shell (first free slot from cursor +0x8A4A; none free: -1) with flight time t (seconds),
// owner, position, velocity and kind; replay event in sub 0xB.
s32 FireShell(f32 t, s32 owner, u32 pos, u32 vel, u8 kind) {
    const u32 P = Pool();
    u32 s = GRef<u8>(P + 0x8A4A);
    s32 n = 0;
    if (s == 0x40) s = 0;
    bool found = true;
    while (((GRef<u64>(P + 0x8A30) >> (s & 0x3F)) & 1) != 0) {
        ++n;
        ++s;
        if (!(n < 0x40)) {
            found = false;
            break;
        }
        if (s == 0x40) s = 0;
    }
    if (found) GRef<u8>(P + 0x8A4A) = static_cast<u8>(s);
    if (n == 0x40) return -1;
    GRef<u8>(P + 0x8A4A) = static_cast<u8>(GRef<u8>(P + 0x8A4A) + 1);
    if (GRef<u8>(P + 0x8A4A) == 0x40) GRef<u8>(P + 0x8A4A) = 0;
    func_0014E310(t, static_cast<s32>(s), owner, pos, vel, kind);
    if (Sub() == 0xB) {
        guest::GuestFrame fr(0x10);
        ZeroQ(fr.Address());
        func_00170240(0.0f, static_cast<s32>(s), 0xFC, owner, 0, pos, fr.Address(), vel, kind);
    }
    return static_cast<s32>(s);
}

// EE: 0x0014E310
// Shell slot setup: trail none, life t * 60 frames, owner, kind, first step taken, SE 8.
void func_0014E310(f32 t, s32 n, s32 owner, u32 pos, u32 vel, u8 kind) {
    const u32 s = Shell(static_cast<u32>(n));
    GRef<s16>(s + 2) = -1;
    GRef<s32>(s + 8) = fpu::FloatToInt(t * 60.0f);
    GRef<u64>(Pool() + 0x8A30) |= 1ull << (n & 0x3F);
    GRef<u16>(s + 0xC) = static_cast<u16>(owner);
    GRef<u16>(s + 0xE) = kind;
    GRef<u16>(s) = 0;
    GRef<u32>(s + 4) = 0;
    CopyQ(s + 0x10, pos);
    CopyQ(s + 0x30, vel);
    sceVu0AddVector(s + 0x10, s + 0x10, s + 0x30);
    PlayEventSe(8, s + 0x10);
}

// EE: 0x0014E3C8
// Ground-unit shell aimed by flight time t (seconds) at tgt.
// fpucheck: 1 add
void func_0014E3C8(f32 t, u8 owner, u8 kind, u32 pos, u32 tgt) {
    guest::GuestFrame fr(0x20);
    const u32 d = fr.Address(0), v = fr.Address(0x10);
    sceVu0SubVector(d, tgt, pos);
    const f32 h = func_00104FA8(d);
    const f32 k = F(0x0040E580);
    const f32 hs = h / t * k;
    GRef<u32>(v + 0xC) = 0;
    const f32 vy = fpu::Sub(F(d + 4) / t, t * 24.5f) * k;
    GRef<f32>(v + 4) = vy;
    GRef<f32>(v + 8) = hs * F(d + 8) / h;
    GRef<f32>(v) = hs * F(d) / h;
    FireShell(t, owner, pos, v, kind);
}

// EE: 0x0014E4A8
// Ground-unit shell aimed by horizontal speed sp.
// fpucheck: 1 add
void func_0014E4A8(f32 sp, u8 owner, u8 kind, u32 pos, u32 tgt) {
    guest::GuestFrame fr(0x20);
    const u32 d = fr.Address(0), v = fr.Address(0x10);
    sceVu0SubVector(d, tgt, pos);
    const f32 h = func_00104FA8(d);
    const f32 t = h / sp;
    const f32 k = F(0x0040E584);
    const f32 s2 = sp * k;
    GRef<u32>(v + 0xC) = 0;
    GRef<f32>(v + 8) = s2 * F(d + 8) / h;
    GRef<f32>(v) = s2 * F(d) / h;
    GRef<f32>(v + 4) = fpu::Sub(F(d + 4) / t, t * 24.5f) * k;
    FireShell(t, owner, pos, v, kind);
}

// EE: 0x0014E588
// Ground-unit shell aimed by launch angle a (radians): t = sqrt(2 sin a / 49 * (h / cos a + dy /
// sin a)).
// fpucheck: 3 adds
void func_0014E588(f32 a, u8 owner, u8 kind, u32 pos, u32 tgt) {
    const f32 sn = ee::sinf(a);
    const f32 cs = ee::cosf(a);
    guest::GuestFrame fr(0x20);
    const u32 d = fr.Address(0), v = fr.Address(0x10);
    sceVu0SubVector(d, tgt, pos);
    const f32 h = func_00104FA8(d);
    const f32 hc = h / cs;
    const f32 ys = F(d + 4) / sn;
    const f32 q = fpu::Add(sn, sn) / 49.0f;
    // sqrt.S; the original's NaN fallback to sqrtf cannot trigger on the EE.
    const f32 t = fpu::Sqrt(q * fpu::Add(hc, ys));
    const f32 k = F(0x0040E588);
    const f32 hs = h / t * k;
    GRef<u32>(v + 0xC) = 0;
    GRef<f32>(v + 4) = fpu::Sub(F(d + 4) / t, t * 24.5f) * k;
    GRef<f32>(v + 8) = hs * F(d + 8) / h;
    GRef<f32>(v) = hs * F(d) / h;
    FireShell(t, owner, pos, v, kind);
}

// EE: 0x0014E700
// Ground-unit shell aimed by apex height y: t = sqrt(2 (y - pos.y) / -49) + sqrt(2 (y - tgt.y) / -49).
// fpucheck: 7 adds
void func_0014E700(f32 y, u8 owner, u8 kind, u32 pos, u32 tgt) {
    guest::GuestFrame fr(0x20);
    const u32 d = fr.Address(0), v = fr.Address(0x10);
    sceVu0SubVector(d, tgt, pos);
    const f32 h = func_00104FA8(d);
    const f32 a = fpu::Sub(y, F(pos + 4));
    const f32 t1 = fpu::Sqrt(fpu::Add(a, a) / -49.0f);
    const f32 b = fpu::Sub(y, F(tgt + 4));
    const f32 t2 = fpu::Sqrt(fpu::Add(b, b) / -49.0f);
    const f32 t = fpu::Add(t1, t2);
    const f32 k = F(0x0040E58C);
    const f32 hs = h / t * k;
    GRef<u32>(v + 0xC) = 0;
    GRef<f32>(v + 4) = fpu::Sub(F(d + 4) / t, t * 24.5f) * k;
    GRef<f32>(v + 8) = hs * F(d + 8) / h;
    GRef<f32>(v) = hs * F(d) / h;
    FireShell(t, owner, pos, v, kind);
}

// EE: 0x0014E8C8
// Ages every live bomb (mask +0x8A38).
void func_0014E8C8() {
    if (GRef<u64>(Pool() + 0x8A38) == 0) return;
    for (u32 i = 0; i < 0x40; ++i)
        if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A38)) >> i & 1) == 0) func_0014E9C0(i);
}

// EE: 0x0014E940
// Moves every live bomb.
void func_0014E940() {
    if (GRef<u64>(Pool() + 0x8A38) == 0) return;
    for (u32 i = 0; i < 0x40; ++i)
        if ((static_cast<s64>(~GRef<u64>(Pool() + 0x8A38)) >> i & 1) == 0) func_0014EA28(i);
}

// EE: 0x0014E9C0
// Bomb i: freed after its life (+8), else gravity C 0x0040E590 on vy.
// fpucheck: 1 add
void func_0014E9C0(u32 i) {
    const u32 b = Pool() + i * 0x40 + 0x7800;
    const s32 age = GRef<s32>(b + 4) + 1;
    GRef<s32>(b + 4) = age;
    if (GRef<s32>(b + 8) < age) {
        Free64(0x8A38, i);
        return;
    }
    GRef<f32>(b + 0x34) = fpu::Add(F(b + 0x34), F(0x0040E590));
}

// EE: 0x0014EA28
// Bomb i moves; below the terrain height at its position it explodes (0xB5, SE 0).
void func_0014EA28(u32 i) {
    const u32 b = Pool() + i * 0x40 + 0x7800;
    guest::GuestFrame fr(0x20);
    const u32 nx = fr.Address(0), z = fr.Address(0x10);
    sceVu0AddVector(nx, b + 0x10, b + 0x30);
    if (func_0013E338(Terrain(), b + 0x10) < F(b + 0x14)) {
        Free64(0x8A38, i);
        ZeroQ(z);
        func_001F08E8(nx, z, 0xB5);
        PlayEventSe(0, nx);
    }
    CopyQ(b + 0x10, nx);
}

// EE: 0x0014EAE8
// Drops a bomb (AI; first free slot from cursor +0x8A4C; none free: -1); replay event in sub 0xB.
// The fourth argument is not used.
s32 DropBomb(u8 owner, u32 pos, u32 vel, u8 /*unused*/) {
    const u32 P = Pool();
    u32 s = GRef<u8>(P + 0x8A4C);
    s32 n = 0;
    if (s == 0x40) s = 0;
    bool found = true;
    while (((GRef<u64>(P + 0x8A38) >> (s & 0x3F)) & 1) != 0) {
        ++n;
        ++s;
        if (!(n < 0x40)) {
            found = false;
            break;
        }
        if (s == 0x40) s = 0;
    }
    if (found) GRef<u8>(P + 0x8A4C) = static_cast<u8>(s);
    if (n == 0x40) return -1;
    GRef<u8>(P + 0x8A4C) = static_cast<u8>(GRef<u8>(P + 0x8A4C) + 1);
    if (GRef<u8>(P + 0x8A4C) == 0x40) GRef<u8>(P + 0x8A4C) = 0;
    func_0014EC28(static_cast<s32>(s), owner, pos, vel);
    if (Sub() == 0xB) {
        guest::GuestFrame fr(0x10);
        ZeroQ(fr.Address());
        func_00170240(0.0f, static_cast<s32>(s), 0xFA, owner, 0, pos, fr.Address(), vel, 0);
    }
    return static_cast<s32>(s);
}

// EE: 0x0014EC28
// Bomb slot setup: life 1800 frames, owner, first step taken.
void func_0014EC28(s32 n, s32 owner, u32 pos, u32 vel) {
    const u32 b = Pool() + static_cast<u32>(n) * 0x40 + 0x7800;
    GRef<u32>(b + 8) = 0x708;
    GRef<u64>(Pool() + 0x8A38) |= 1ull << (n & 0x3F);
    GRef<u16>(b + 0xC) = static_cast<u16>(owner);
    GRef<u32>(b + 4) = 0;
    CopyQ(b + 0x10, pos);
    CopyQ(b + 0x30, vel);
    sceVu0AddVector(b + 0x10, b + 0x10, b + 0x30);
}

// EE: 0x0014ECD8
// Ages the cannon shells (8, mask +0x8A40) and passes the nearest one's distance to player 0
// (state +0x290) to the rumble (0x002576C8 kind 7).
void func_0014ECD8() {
    f32 near = 524288.0f;
    for (u32 i = 0; i < 8; ++i) {
        if (((static_cast<s32>(~W(Pool() + 0x8A40)) >> i) & 1) != 0) continue;
        func_0014EE80(i);
        const f32 d = func_00104F10(Pool() + 0x8800 + i * 0x40 + 0x10, Unit(0));
        if (d < near) near = d;
    }
    GRef<f32>(W(Unit(0) + 0x44) + 0x290) = near;
    if (near != 524288.0f) func_002576C8(0, 7, 0.0f, near);
}

// EE: 0x0014EDE0
// Moves every live cannon shell.
void func_0014EDE0() {
    if (W(Pool() + 0x8A40) == 0) return;
    for (u32 i = 0; i < 8; ++i)
        if (((static_cast<s32>(~W(Pool() + 0x8A40)) >> i) & 1) == 0) func_0014F1A0(i);
}

// EE: 0x0014EE80
// Cannon shell i: freed outside +-C 0x0040E594/0x0040E598 or above 0 / below -131072; in flight
// (+7 = 0) it climbs toward the target's height (capped at C 0x0040E59C) and bursts by mode +6
// (0: |dz| < 8192, compared in double by the original: exact as a float compare; 1: dz < -16384;
// >= 2: after +0 * C 0x0040E5A0 > 1, SE 0xC); a burst calls the mission radio (6: 0xD + rand % 11,
// 9: 0x19 + rand % 6) and the flash 0x0020C2A0; bursting (+7 = 1) the blast radius +8 grows by 500
// up to C 0x0040E5A8 while the shell slows (C 0x0040E5AC).
// fpucheck: 6 static adds
void func_0014EE80(u32 i) {
    const u32 s = Pool() + i * 0x40 + 0x8800;
    const f32 x = F(s + 0x10), z = F(s + 0x18), y = F(s + 0x14);
    if (F(0x0040E594) < x || x < F(0x0040E598) || F(0x0040E594) < z || z < F(0x0040E598) || 0.0f < y ||
        y < -131072.0f) {
        GRef<u32>(Pool() + 0x8A40) = W(Pool() + 0x8A40) & ~(1u << i);
        return;
    }
    const u8 st = GRef<u8>(s + 7);
    if (st == 0) {
        bool burst = false;
        const u8 mode = GRef<u8>(s + 6);
        if (mode < 2) {
            const u8 tg = GRef<u8>(s + 5);
            if (tg != 0xFF) {
                const u32 t = Unit(tg);
                const f32 ty = F(t + 4), c = F(0x0040E59C);
                const f32 dy = ty < c ? fpu::Sub(ty, y) : fpu::Sub(c, y);
                GRef<f32>(s + 0x34) = fpu::Add(F(s + 0x34), dy);
                if (mode == 1) {
                    if (fpu::Sub(z, F(t + 8)) < -16384.0f) {
                        GRef<u8>(s + 7) = 1;
                        burst = true;
                    }
                } else {
                    f32 dz = fpu::Sub(z, F(t + 8));
                    if (dz < 0.0f) dz = -dz;
                    if (dz < 8192.0f) {
                        GRef<u8>(s + 7) = 1;
                        burst = true;
                    }
                }
            }
        } else {
            const s32 n = GRef<s32>(s) + 1;
            GRef<s32>(s) = n;
            if (1.0f < fpu::IntToFloat(n) * F(0x0040E5A0)) {
                GRef<u8>(s + 7) = 1;
                PlayEventSe(0xC, s + 0x10);
                burst = GRef<u8>(s + 7) == 1;
            }
        }
        if (!burst) return;
        if (Game(0x27) == 6)
            func_00252FB8(0xD, 0xB, 0);
        else if (Game(0x27) == 9)
            func_00252FB8(0x19, 6, 0);
        guest::GuestFrame fr(0x10);
        sceVu0AddVector(fr.Address(), s + 0x10, s + 0x30);
        func_0020C2A0(F(0x0040E5A4), fr.Address(), s + 0x30, 1);
        return;
    }
    if (st != 1) return;
    if (F(s + 8) < F(0x0040E5A8))
        GRef<f32>(s + 8) = fpu::Add(F(s + 8), 500.0f);
    else
        GRef<u32>(Pool() + 0x8A40) = W(Pool() + 0x8A40) & ~(1u << i);
    const f32 k = F(0x0040E5AC);
    GRef<f32>(s + 0x30) = F(s + 0x30) * k;
    GRef<f32>(s + 0x34) = F(s + 0x34) * k;
    GRef<f32>(s + 0x38) = F(s + 0x38) * k;
}

// EE: 0x0014F1A0
// Cannon shell i moves (stopped at the terrain: burst), area damage of its blast radius (kind
// 0x20), and its smoke trail (0x001836B8 start, 0x0020A3A0) while live.
void func_0014F1A0(u32 i) {
    const u32 s = Pool() + i * 0x40 + 0x8800;
    guest::GuestFrame fr(0x10);
    const u32 nx = fr.Address();
    sceVu0AddVector(nx, s + 0x10, s + 0x30);
    const f32 h = func_0013E338(Terrain(), nx);
    if (h < F(nx + 4)) {
        GRef<u8>(s + 7) = 1;
        GRef<f32>(nx + 4) = h;
    }
    ApplyAreaDamage(F(s + 8), nx, GRef<u8>(s + 4), 0x20, 0);
    CopyQ(s + 0x10, nx);
    if (((W(Pool() + 0x8A40) >> i) & 1) == 0) return;
    if (GRef<s32>(s + 0xC) == -1) func_001836B8(s + 0x10, s + 0x20);
    GRef<u32>(s + 0xC) = func_0020A3A0(W(s + 0xC), s + 0x10, s + 0x30, 0x78);
}

// EE: 0x0014F2A0
// Fires a cannon shell (ring counter +0x8A4B, 8 slots); replay event in sub 0xB. Returns the slot.
s8 FireCannonShell(s32 owner, s32 target, u32 pos, u32 ang, u8 kind) {
    const u32 P = Pool();
    const s8 n = GRef<s8>(P + 0x8A4B);
    GRef<s8>(P + 0x8A4B) = static_cast<s8>(n + 1);
    if (GRef<s8>(P + 0x8A4B) == 8) GRef<u8>(P + 0x8A4B) = 0;
    func_0014F398(static_cast<u32>(static_cast<s32>(n)), static_cast<u8>(owner), static_cast<u8>(target), pos, ang, kind);
    if (Sub() == 0xB) {
        guest::GuestFrame fr(0x10);
        ZeroQ(fr.Address());
        func_00170240(0.0f, n, 0xFB, owner, target, pos, ang, fr.Address(), kind);
    }
    return n;
}

// EE: 0x0014F398
// Cannon shell slot setup: blast radius 500, trail none, mode, owner, target, flying, position,
// rotation, velocity C 0x0040E5B0 along the nose.
void func_0014F398(u32 n, u8 owner, u8 target, u32 pos, u32 ang, u8 mode) {
    const u32 s = Pool() + n * 0x40 + 0x8800;
    GRef<u32>(s + 8) = 0x43FA0000u;  // 500.0
    GRef<u32>(Pool() + 0x8A40) = W(Pool() + 0x8A40) | (1u << (n & 0x1F));
    GRef<s32>(s + 0xC) = -1;
    GRef<u8>(s + 6) = mode;
    GRef<u8>(s + 4) = owner;
    GRef<u8>(s + 5) = target;
    GRef<u8>(s + 7) = 0;
    GRef<u32>(s) = 0;
    CopyQ(s + 0x10, pos);
    CopyQ(s + 0x20, ang);
    guest::GuestFrame fr(0x50);
    const u32 loc = fr.Address(0), m = fr.Address(0x10);
    ZeroQ(loc);
    GRef<u32>(loc + 8) = W(0x0040E5B0);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x20);
    sceVu0ApplyMatrix(s + 0x30, m, loc);
}

// ---- Special weapons: 32 slots x 0x80 at +0x4C00, mask +0x8A2C. Slot: +0 pos, +0x10 rotation,
// +0x20 velocity, +0x30 local velocity (z = speed +0x38), +0x40 turn angles, +0x50 age, +0x54 life,
// +0x58 owner, +0x59 target, +0x5A weapon id, +0x5B kind, +0x5C linger, +0x5E state, +0x5F extra,
// +0x60 blast radius max, +0x64 blast radius, +0x68 trail handle, +0x70 flags (bit 0 hit).

namespace {

u32 Special(u32 i) { return Pool() + i * 0x80 + 0x4C00; }

}  // namespace

// EE: 0x0014F5E8
// Special weapons pre-step: lingering slots (+0x70 bit 0) are freed after 180 frames, others after
// their life (+0x54), else the per-kind step (+0x5B).
void func_0014F5E8() {
    for (u32 i = 0; i < 0x20; ++i) {
        const u32 msk = W(Pool() + 0x8A2C);
        if (((static_cast<s32>(~msk) >> i) & 1) != 0) continue;
        const u32 s = Special(i);
        if ((W(s + 0x70) & 1u) != 0) {
            const u16 t = static_cast<u16>(GRef<u16>(s + 0x5C) + 1);
            GRef<u16>(s + 0x5C) = t;
            if (0xB3 < t) {
                GRef<u16>(s + 0x5C) = 0;
                GRef<u32>(Pool() + 0x8A2C) = msk & ~(1u << i);
            }
            continue;
        }
        const s32 age = GRef<s32>(s + 0x50) + 1;
        GRef<s32>(s + 0x50) = age;
        if (!(age < GRef<s32>(s + 0x54))) {
            GRef<u32>(Pool() + 0x8A2C) = W(Pool() + 0x8A2C) & ~(1u << i);
            continue;
        }
        switch (GRef<u8>(s + 0x5B)) {
            case 1: func_00150378(i); break;
            case 2: func_00150090(i); break;
            case 3: func_00150948(i); break;
            case 4: func_00150FD0(i); break;
            case 5: func_001515F8(i); break;
            case 6: func_00151BD0(i); break;
            case 7: func_00151FE8(i); break;
            case 8: func_00152520(i); break;
            case 9: func_00152CC8(i); break;
            case 10: func_00153228(i); break;
            case 11: func_00153A58(i); break;
            case 12: func_001522C0(i); break;
            default: func_0014FD48(i); break;
        }
    }
}

// EE: 0x0014F7C8
// Special weapons: per-kind movement and hit tests of the flying slots.
void func_0014F7C8() {
    for (u32 i = 0; i < 0x20; ++i) {
        if (((static_cast<s32>(~W(Pool() + 0x8A2C)) >> i) & 1) != 0) continue;
        const u32 s = Special(i);
        if ((W(s + 0x70) & 1u) != 0) continue;
        switch (GRef<u8>(s + 0x5B)) {
            case 1: func_001506D8(i); break;
            case 2: func_001500B8(i); break;
            case 3: func_00150CD0(i); break;
            case 4: func_001512B0(i); break;
            case 5: func_00151600(i); break;
            case 6: func_00151C10(i); break;
            case 7: func_00152020(i); break;
            case 8: func_00152998(i); break;
            case 9: func_00152F78(i); break;
            case 10: func_001536B8(i); break;
            case 11: func_00153AF8(i); break;
            case 12: func_00152300(i); break;
            default: func_0014FD88(i); break;
        }
    }
}

// EE: 0x0014FA88
// Fires special weapon `id` (ring counter +0x8A46, overwrites the oldest); replay event in sub 0xB.
// Returns the slot.
u32 FireSpecialWeapon(s32 id, s32 owner, u8 target, u32 pos, u32 ang, u32 vel, u8 extra) {
    const u32 P = Pool();
    const u16 n = GRef<u16>(P + 0x8A46);
    GRef<u16>(P + 0x8A46) = static_cast<u16>(n + 1);
    if (static_cast<u16>(n + 1) == 0x20) GRef<u16>(P + 0x8A46) = 0;
    func_0014FB88(n, id, owner, target, pos, ang, vel, extra);
    if (Sub() == 0xB) func_00170240(0.0f, n, id, owner, target, pos, ang, vel, extra);
    return n;
}

// EE: 0x0014FB88
// Special weapon slot setup and the per-id init (jump table 0x004047E0, whose case code
// 0x0014FC24-0x0014FD30 lies outside the Ghidra function body): ids 0-2 bombs 0x0014FFA8, 3
// 0x00150880, 4 0x00151F50, 5 0x00153D60, 6 0x00152298, 7 0x00151BA8, 8 0x001502F0, 9 0x00152C18,
// 10 0x00150F30, 11 0x00153958, 12/13 0x00151550 (1/0), 14 0x001531A0, 16 0x001524F0; id 15 has no
// init (its kind byte keeps the slot's previous value).
u32 func_0014FB88(u32 n, s32 id, s32 owner, u8 target, u32 pos, u32 ang, u32 vel, u8 extra) {
    const u32 s = Special(n);
    GRef<u32>(Pool() + 0x8A2C) = W(Pool() + 0x8A2C) | (1u << (n & 0x1F));
    GRef<u8>(s + 0x58) = static_cast<u8>(owner);
    GRef<u8>(s + 0x59) = target;
    GRef<u8>(s + 0x5F) = extra;
    GRef<u32>(s + 0x50) = 0;
    GRef<u8>(s + 0x5A) = static_cast<u8>(id);
    GRef<u32>(s + 0x70) = 0;
    GRef<u16>(s + 0x5C) = 0;
    CopyQ(s, pos);
    CopyQ(s + 0x10, ang);
    CopyQ(s + 0x20, vel);
    switch (static_cast<u32>(id) < 0x11 ? id : -1) {
        case 0: func_0014FFA8(s, 0); break;
        case 1: func_0014FFA8(s, 1); break;
        case 2: func_0014FFA8(s, 2); break;
        case 3: func_00150880(s); break;
        case 4: func_00151F50(s); break;
        case 5: func_00153D60(s); break;
        case 6: func_00152298(s); break;
        case 7: func_00151BA8(s); break;
        case 8: func_001502F0(s); break;
        case 9: func_00152C18(s); break;
        case 10: func_00150F30(s); break;
        case 11: func_00153958(s); break;
        case 12: func_00151550(s, 1); break;
        case 13: func_00151550(s, 0); break;
        case 14: func_001531A0(s); break;
        case 16: func_001524F0(s); break;
        default: break;
    }
    return n;
}

// EE: 0x0014FD48
// Kind 0 (bombs) pre-step: gravity C 0x0040E5C4 while falling, stopped once exploded.
// fpucheck: 1 add
void func_0014FD48(u32 i) {
    const u32 s = Special(i);
    if (GRef<u8>(s + 0x5E) == 0) {
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E5C4));
        return;
    }
    ZeroQ(s + 0x20);
}

// EE: 0x0014FD88
// Kind 0 (bombs): unit (0x001542C0) or terrain hit -> exploded state (life 600, velocity to the
// impact point), water splash (material 2: spark kind 5, SE 2) or explosion by id (0xBC-0xBF,
// default 0xB5, SE 0); while exploded the blast radius +0x64 grows by C 0x0040E5C8 to +0x60 (then
// the slot lingers) with area damage kind 4.
// fpucheck: 1 add
void func_0014FD88(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x30);
    const u32 nx = fr.Address(0), nrm = fr.Address(0x10), attr = fr.Address(0x20);
    sceVu0AddVector(nx, s, vel);
    u8 st = GRef<u8>(s + 0x5E);
    if (st == 0) {
        if (func_001542C0(GRef<u8>(s + 0x5A), nx, nrm, s, s + 0x10, vel, GRef<u8>(s + 0x58)) == 1 ||
            func_0013D698(Terrain(), nx, attr, s, nx, nrm) == 1) {
            GRef<u8>(s + 0x5E) = 1;
            GRef<u32>(s + 0x50) = 0;
            GRef<u32>(s + 0x54) = 600;
            sceVu0SubVector(vel, nx, s);
            if (GRef<s32>(attr) == 2) {
                SpawnImpactEffect(nx, vel, nrm, 5);
                PlayEventSe(2, nx);
            } else {
                const u8 id = GRef<u8>(s + 0x5A);
                s32 k = 0xB5;
                if (id == 0)
                    k = 0xBC;
                else if (id == 1)
                    k = 0xBD;
                else if (id == 2)
                    k = 0xBE;
                else if (id == 3)
                    k = 0xBF;
                func_001F08E8(nx, vel, k);
                PlayEventSe(0, nx);
            }
        }
        st = GRef<u8>(s + 0x5E);
    }
    if (st == 1) {
        if (F(s + 0x64) < F(s + 0x60)) {
            GRef<f32>(s + 0x64) = fpu::Add(F(s + 0x64), F(0x0040E5C8));
        } else {
            GRef<u32>(s + 0x64) = W(s + 0x60);
            GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        }
        ApplyAreaDamage(F(s + 0x64), nx, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
    }
    sceVu0AddVector(s, s, vel);
}

// EE: 0x0014FFA8
// Bomb init (ids 0-2): life 1200, kind 0, blast radius 500 / 700 / C 0x0040E5D4, release speed
// along the nose C 0x0040E5CC / 0x0040E5D0 / 0x0040E5D8 added to the velocity, first step taken.
void func_0014FFA8(u32 s, s32 id) {
    GRef<u32>(s + 0x54) = 0x4B0;
    GRef<u8>(s + 0x5B) = 0;
    GRef<u8>(s + 0x5E) = 0;
    GRef<u32>(s + 0x64) = 0;
    guest::GuestFrame fr(0x50);
    const u32 m = fr.Address(0), loc = fr.Address(0x40);
    ZeroQ(loc);
    if (id == 0) {
        GRef<f32>(s + 0x60) = 500.0f;
        GRef<u32>(loc + 8) = W(0x0040E5CC);
    } else if (id == 1) {
        GRef<f32>(s + 0x60) = 700.0f;
        GRef<u32>(loc + 8) = W(0x0040E5D0);
    } else if (id == 2) {
        GRef<u32>(s + 0x60) = W(0x0040E5D4);
        GRef<u32>(loc + 8) = W(0x0040E5D8);
    }
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0ApplyMatrix(loc, m, loc);
    sceVu0AddVector(s + 0x20, s + 0x20, loc);
    sceVu0AddVector(s, s, s + 0x20);
}

// EE: 0x00150090
// Kind 2 (rockets) pre-step: gravity C 0x0040E5DC.
// fpucheck: 1 add
void func_00150090(u32 i) {
    const u32 s = Special(i);
    GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E5DC));
}

// EE: 0x001500B8
// Kind 2 (rockets): swept segment against active units (special-weapon hit flag 4 on enemies,
// attacker, push = velocity, explosion 0x81, SE 0, slot lingers); else terrain (0x00154428, blast
// at full radius with area damage kind 4); else it moves with its trail (0x001DD9D8).
void func_001500B8(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x250);
    const u32 nx = fr.Address(0), col = fr.Address(0x10), hit = fr.Address(0x170), nrm = fr.Address(0x180),
              attr = fr.Address(0x190);
    sceVu0AddVector(nx, s, vel);
    const u8 ow = GRef<u8>(s + 0x58);
    const u8 sb = GRef<u8>(Unit(ow) + 0x48);
    func_0017C940(col, s + 0x10, s, vel, ow);
    for (s32 j = 0; j < UnitCount(); ++j) {
        const u32 o = Unit(j);
        if ((~W(o + 0x4C) & 0x10000000u) != 0) continue;
        if (func_0017C9B8(col, o, hit, nrm, j) != 1) continue;
        CopyQ(s, hit);
        if ((sb & 3u) != (W(o + 0x48) & 3u)) GRef<u32>(o + 0x50) |= 4u;
        GRef<u8>(o + 0x63) = GRef<u8>(s + 0x58);
        CopyQ(o + 0x30, vel);
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_001F08E8(s, vel, 0x81);
        PlayEventSe(0, s);
        break;
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    if (func_0017CB70(col, nx, hit, attr) == 1) {
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_00154428(nx, vel, hit, GRef<s32>(attr));
        GRef<u32>(s + 0x64) = W(s + 0x60);
        ApplyAreaDamage(F(s + 0x60), nx, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    CopyQ(s, nx);
    GRef<u32>(s + 0x68) = func_001DD9D8(W(s + 0x68), s, vel);
}

// EE: 0x001502F0
// Rocket init (id 8): kind 2, life 300, blast radius 210, trail none, speed C 0x0040E5E0 along the
// nose (replaces the velocity).
void func_001502F0(u32 s) {
    GRef<u8>(s + 0x5B) = 2;
    GRef<u32>(s + 0x54) = 300;
    GRef<f32>(s + 0x60) = 210.0f;
    GRef<s32>(s + 0x68) = -1;
    guest::GuestFrame fr(0x50);
    const u32 loc = fr.Address(0), m = fr.Address(0x10);
    ZeroQ(loc);
    GRef<u32>(loc + 8) = W(0x0040E5E0);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0ApplyMatrix(s + 0x20, m, loc);
}

// EE: 0x00150378
// Kind 1 (guided air-to-ground) pre-step: without a target it pitches down (C 0x0040E64C, limit
// C 0x0040E650) and falls; with one, after C 0x0040E5E8 it steers toward the predicted point
// (pitch/yaw errors wrapped, within the seeker window C 0x0040E604-0x0040E60C: limited turns,
// pitch clamp C 0x0040E61C, angle wraps), outside it pitches down (C 0x0040E640 / limit
// 0x0040E644); local drift damped by C 0x0040E648; velocity from the rotation.
// fpucheck: 19 static adds
void func_00150378(u32 i) {
    const u32 s = Special(i);
    if (GRef<u8>(s + 0x59) == 0xFF) {
        const f32 rx = fpu::Add(F(s + 0x10), F(0x0040E64C));
        GRef<f32>(s + 0x10) = rx;
        if (rx < F(0x0040E650)) GRef<u32>(s + 0x10) = W(0x0040E650);
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E654));
        return;
    }
    const u32 t = Unit(GRef<u8>(s + 0x59));
    if (F(0x0040E5E8) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * F(0x0040E5E4)) {
        guest::GuestFrame fr(0x20);
        const u32 d = fr.Address(0), aim = fr.Address(0x10);
        CopyQ(aim, t + 0x20);
        sceVu0AddVector(aim, aim, t);
        sceVu0SubVector(d, aim, s);
        const f32 len = GetVectorLength(d);
        f32 el = fpu::Sub(ee::asinf(-F(d + 4) / len), F(s + 0x10));
        const f32 ry = F(s + 0x14);
        f32 az = fpu::Sub(ee::atan2f(F(d), F(d + 8)), ry);
        const f32 hi = F(0x0040E5EC), lo = F(0x0040E5F4);
        if (hi < el) el = fpu::Sub(el, F(0x0040E5F0));
        if (el < lo) el = fpu::Add(el, F(0x0040E5F8));
        if (hi < az) az = fpu::Sub(az, F(0x0040E5FC));
        if (az < lo) az = fpu::Add(az, F(0x0040E600));
        const f32 w = F(0x0040E604);
        if (el < w && F(0x0040E608) < el && az < w && F(0x0040E60C) < az) {
            const f32 tp = F(0x0040E610);
            f32 rx = F(s + 0x10);
            if (tp < el) {
                if (0.0f < rx) {
                    rx = fpu::Add(rx, tp);
                    GRef<f32>(s + 0x10) = rx;
                }
            } else if (el < F(0x0040E614)) {
                rx = fpu::Sub(rx, F(0x0040E618));
                GRef<f32>(s + 0x10) = rx;
            } else {
                rx = fpu::Add(rx, el);
                GRef<f32>(s + 0x10) = rx;
            }
            if (rx < F(0x0040E61C)) {
                rx = F(0x0040E61C);
                GRef<f32>(s + 0x10) = rx;
            }
            const f32 ty = F(0x0040E620);
            f32 nry;
            if (ty < az)
                nry = fpu::Add(ry, ty);
            else if (!(az < F(0x0040E624)))
                nry = fpu::Add(ry, az);
            else
                nry = fpu::Sub(ry, ty);
            GRef<f32>(s + 0x14) = nry;
            const f32 wr = F(0x0040E628), wl = F(0x0040E630);
            if (wr < rx) {
                rx = fpu::Sub(rx, F(0x0040E62C));
                GRef<f32>(s + 0x10) = rx;
            }
            if (rx < wl) GRef<f32>(s + 0x10) = fpu::Add(rx, F(0x0040E634));
            f32 y2 = F(s + 0x14);
            if (wr < y2) {
                y2 = fpu::Sub(y2, F(0x0040E638));
                GRef<f32>(s + 0x14) = y2;
            }
            if (y2 < wl) GRef<f32>(s + 0x14) = fpu::Add(y2, F(0x0040E63C));
        } else {
            const f32 rx = fpu::Add(F(s + 0x10), F(0x0040E640));
            GRef<f32>(s + 0x10) = rx;
            if (rx < F(0x0040E644)) GRef<u32>(s + 0x10) = W(0x0040E644);
        }
    }
    const f32 k = F(0x0040E648);
    GRef<f32>(s + 0x34) = F(s + 0x34) * k;
    GRef<f32>(s + 0x30) = F(s + 0x30) * k;
    guest::GuestFrame fm(0x40);
    const u32 m = fm.Address();
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0ApplyMatrix(s + 0x20, m, s + 0x30);
}

// EE: 0x001506D8
// Kind 1 (guided air-to-ground): swept segment against active units, else the terrain; a hit
// blasts at full radius (0x00154428, area damage kind 4) and the slot lingers.
void func_001506D8(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x250);
    const u32 nx = fr.Address(0), col = fr.Address(0x10), hit = fr.Address(0x170), nrm = fr.Address(0x180),
              attr = fr.Address(0x190);
    bool hitu = false;
    sceVu0AddVector(nx, s, vel);
    func_0017C940(col, s + 0x10, s, vel, GRef<u8>(s + 0x58));
    for (s32 j = 0; j < UnitCount(); ++j) {
        if ((~W(Unit(j) + 0x4C) & 0x10000000u) != 0) continue;
        if (func_0017C9B8(col, Unit(j), hit, nrm, j) == 1) {
            CopyQ(s, hit);
            hitu = true;
            break;
        }
    }
    if (hitu || func_0013D698(Terrain(), hit, attr, s, nx, nrm) == 1) {
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_00154428(hit, vel, nrm, GRef<s32>(attr));
        GRef<u32>(s + 0x64) = W(s + 0x60);
        ApplyAreaDamage(F(s + 0x60), nx, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
    }
    CopyQ(s, nx);
}

// EE: 0x00150880
// Guided air-to-ground init (id 3): kind 1, life 1200, roll 0, blast radius 700, release speed
// C 0x0040E658 along the nose added; local velocity = velocity in the weapon frame.
void func_00150880(u32 s) {
    GRef<u8>(s + 0x5B) = 1;
    GRef<u32>(s + 0x54) = 0x4B0;
    guest::GuestFrame fr(0x90);
    const u32 m = fr.Address(0), inv = fr.Address(0x40), loc = fr.Address(0x80);
    ZeroQ(loc);
    GRef<u32>(loc + 8) = W(0x0040E658);
    GRef<u32>(s + 0x18) = 0;
    GRef<f32>(s + 0x60) = 700.0f;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(loc, m, loc);
    sceVu0AddVector(s + 0x20, s + 0x20, loc);
    sceVu0ApplyMatrix(s + 0x30, inv, s + 0x20);
}

// EE: 0x00150948
// Kind 3 (homing missiles; id 11 with its own speeds C 0x0040E65C-0x0040E664, others
// C 0x0040E668-0x0040E670) pre-step: boost until C 0x0040E678; then toward the target's predicted
// point: the speed eases between its minimum and current value by the remaining distance (beyond
// 60 frames of flight, with a terrain-clearance pull-up), the target gets flag 0x200 and +0x67,
// turn limited to C 0x0040E67C / 0x0040E680; velocity from the (pre-turn) rotation; drift damped
// by C 0x0040E684.
// fpucheck: 7 static adds
void func_00150948(u32 i) {
    const u32 s = Special(i);
    guest::GuestFrame fr(0xA0);
    const u32 m = fr.Address(0), inv = fr.Address(0x40), d = fr.Address(0x80), aim = fr.Address(0x90);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    f32 vmax, vmin, acc;
    if (GRef<u8>(s + 0x5A) == 0xB) {
        vmax = F(0x0040E65C);
        vmin = F(0x0040E660);
        acc = F(0x0040E664);
    } else {
        vmax = F(0x0040E668);
        vmin = F(0x0040E66C);
        acc = F(0x0040E670);
    }
    if (F(0x0040E678) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * F(0x0040E674)) {
        const u8 tg = GRef<u8>(s + 0x59);
        if (tg != 0xFF) {
            const u32 t = Unit(tg);
            CopyQ(aim, t + 0x20);
            sceVu0AddVector(aim, aim, t);
            sceVu0SubVector(d, aim, s);
            const f32 h = func_00104FA8(d);
            if (!(h < F(s + 0x38) * 60.0f) && !(h < F(d + 4))) {
                if (F(d + 4) < 256.0f || fpu::Sub(func_0013E2B0(Terrain(), s), F(s + 4)) < 256.0f)
                    GRef<f32>(d + 4) = -256.0f;
                else
                    GRef<u32>(d + 4) = 0;
                const f32 sp = F(s + 0x38);
                f32 k = fpu::Sub(h, sp * 60.0f) * 0.0009765625f;
                if (1.0f < k) k = 1.0f;
                if (k < 0.0f) k = 0.0f;
                const f32 ns = fpu::Add(k * fpu::Sub(sp, vmin), vmin);
                GRef<f32>(s + 0x38) = ns;
                if (k == 1.0f && GRef<u8>(s + 0x5A) == 0xB && ns < vmax) GRef<f32>(s + 0x38) = fpu::Add(ns, acc);
            }
            sceVu0ApplyMatrix(d, inv, d);
            const f32 yaw = ee::atan2f(F(d), -F(d + 4));
            // sqrt.S; the original's NaN fallback to sqrtf cannot trigger on the EE.
            const f32 off = ee::atan2f(fpu::Sqrt(fpu::Add(F(d) * F(d), F(d + 4) * F(d + 4))), F(d + 8));
            GRef<u8>(t + 0x67) = static_cast<u8>(i);
            GRef<u32>(t + 0x50) = W(t + 0x50) | 0x200u;
            const f32 c1 = F(0x0040E67C), c2 = F(0x0040E680);
            if (c1 < off)
                GRef<f32>(s + 0x40) = c1;
            else if (off < c2)
                GRef<f32>(s + 0x40) = c2;
            else
                GRef<f32>(s + 0x40) = off;
            GRef<f32>(s + 0x18) = yaw;
            GRef<u32>(s + 0x44) = 0;
            GRef<u32>(s + 0x48) = 0;
            GRef<u32>(s + 0x4C) = 0;
            func_00102EF0(s + 0x10, s + 0x10, s + 0x40);
            GRef<u32>(s + 0x18) = 0;
        }
        sceVu0ApplyMatrix(s + 0x20, m, s + 0x30);
    } else {
        if (F(s + 0x38) < vmax) GRef<f32>(s + 0x38) = fpu::Add(F(s + 0x38), acc);
        sceVu0ApplyMatrix(s + 0x20, m, s + 0x30);
    }
    const f32 k = F(0x0040E684);
    GRef<f32>(s + 0x30) = F(s + 0x30) * k;
    GRef<f32>(s + 0x34) = F(s + 0x34) * k;
}

// EE: 0x00150CD0
// Kind 3 (homing missiles): as 0x001500B8 with explosion 0x81 and SE 1; a terrain hit shows the
// owner's "missed" message (flag 0x10); the trail is 0x001DAB18 colour 0x24 (0x12 in mission
// kind 2).
void func_00150CD0(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x240);
    const u32 col = fr.Address(0), hit = fr.Address(0x160), nrm = fr.Address(0x170), attr = fr.Address(0x180);
    const u8 ow = GRef<u8>(s + 0x58);
    const u8 sb = GRef<u8>(Unit(ow) + 0x48);
    func_0017C940(col, s + 0x10, s, vel, ow);
    for (s32 j = 0; j < UnitCount(); ++j) {
        const u32 o = Unit(j);
        if ((~W(o + 0x4C) & 0x10000000u) != 0) continue;
        if (func_0017C9B8(col, o, hit, nrm, j) != 1) continue;
        CopyQ(s, hit);
        if ((sb & 3u) != (W(o + 0x48) & 3u)) GRef<u32>(o + 0x50) |= 4u;
        GRef<u8>(o + 0x63) = GRef<u8>(s + 0x58);
        CopyQ(o + 0x30, vel);
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_001F08E8(s, vel, 0x81);
        PlayEventSe(1, s);
        break;
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    if (func_0017CB70(col, hit, nrm, attr) == 1) {
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_00154428(hit, vel, nrm, GRef<s32>(attr));
        if ((~W(s + 0x70) & 0x10u) != 0) {
            GRef<u32>(s + 0x70) = W(s + 0x70) | 0x10u;
            ShowCenterMessage(GRef<u8>(s + 0x58), 0xD, 0);
        }
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    sceVu0AddVector(s, s, vel);
    const s32 colr = GRef<s8>(Scene() + 0x1653) == 2 ? 0x12 : 0x24;
    GRef<u32>(s + 0x68) = func_001DAB18(W(s + 0x68), s, vel, colr, 1);
}

// EE: 0x00150F30
// Homing air-to-air init (id 10): kind 3, life 600, local velocity with the speed boosted by
// C 0x0040E688, trail none, turn angles 0.
// fpucheck: 1 add
void func_00150F30(u32 s) {
    GRef<u32>(s + 0x18) = 0;
    GRef<u8>(s + 0x5B) = 3;
    GRef<u32>(s + 0x54) = 600;
    guest::GuestFrame fr(0x80);
    const u32 m = fr.Address(0), inv = fr.Address(0x40);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(s + 0x30, inv, s + 0x20);
    const f32 sp = fpu::Add(F(s + 0x38), F(0x0040E688));
    GRef<s32>(s + 0x68) = -1;
    GRef<u32>(s + 0x40) = 0;
    GRef<u32>(s + 0x48) = 0;
    GRef<f32>(s + 0x38) = sp;
    GRef<u32>(s + 0x44) = 0;
}

namespace {

// Double constant of .rodata (read from the disc image's data like every table).
double D64(u32 a) {
    const u64 b = GRef<u64>(a);
    double d;
    std::memcpy(&d, &b, sizeof d);
    return d;
}

// Bounce of a special weapon off a surface (normal nrm at the hit point hit): the velocity is
// reflected through two outer products, normalised to the old speed plus the normal; the
// position goes back along the new velocity by the unused fraction, then both are halved.
void Bounce(u32 s, u32 vel, u32 hit, u32 nrm, u32 tmp, u32 back) {
    const f32 sp = GetVectorLength(vel);
    f32 k = func_00104F10(hit, s) / sp;
    if (1.0f < k) k = 1.0f;
    if (k < 0.0f) k = 0.0f;
    sceVu0OuterProduct(tmp, vel, nrm);
    sceVu0OuterProduct(tmp, nrm, tmp);
    sceVu0Normalize(tmp, tmp);
    sceVu0ScaleVector(tmp, tmp, sp);
    sceVu0AddVector(tmp, tmp, nrm);
    sceVu0CopyVector(vel, tmp);
    sceVu0ScaleVector(back, tmp, -k);
    sceVu0AddVector(back, back, hit);
    sceVu0ScaleVector(tmp, tmp, 0.5f);
    sceVu0AddVector(s, back, tmp);
    sceVu0ScaleVector(vel, vel, 0.5f);
}

}  // namespace

// EE: 0x00150FD0
// Kind 4 (ids 12/13) pre-step: drift damped (C 0x0040E690); dropping until age * C 0x0040E68C
// passes +0x6C (gravity C 0x0040E6AC, local velocity follows; at the switch the sound event
// 0x002B67F0); then homing like kind 3 (seeker window C 0x0040E694 / 0x0040E698, turn limits
// C 0x0040E69C / 0x0040E6A0, flag 0x200 on the target) and accelerating to C 0x0040E6A4 by
// C 0x0040E6A8; velocity from the rotation.
// fpucheck: 3 adds
void func_00150FD0(u32 i) {
    const u32 s = Special(i);
    guest::GuestFrame fr(0xA0);
    const u32 m = fr.Address(0), inv = fr.Address(0x40), d = fr.Address(0x80), aim = fr.Address(0x90);
    sceVu0UnitMatrix(m);
    const f32 kt = F(0x0040E68C);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    const f32 dk = F(0x0040E690);
    const f32 lx = F(s + 0x30) * dk;
    GRef<f32>(s + 0x34) = F(s + 0x34) * dk;
    GRef<f32>(s + 0x30) = lx;
    if (!(F(s + 0x6C) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * kt)) {
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E6AC));
        sceVu0ApplyMatrix(s + 0x30, inv, s + 0x20);
        if (!(F(s + 0x6C) < fpu::IntToFloat(GRef<s32>(s + 0x50) + 1) * kt)) return;
        GRef<u32>(0x002B67F0) = GRef<u8>(s + 0x58);
        GRef<s32>(0x002B67F8) = GRef<s8>(W(Unit(GRef<u8>(s + 0x58)) + 0x44) + 0x149);
        GRef<u32>(0x002B67F4) = GRef<u8>(s + 0x5A);
        CopyQ(0x002B6800, s);
        func_0021AE30(0x002B67F0);
        return;
    }
    const u8 tg = GRef<u8>(s + 0x59);
    if (tg != 0xFF) {
        const u32 t = Unit(tg);
        CopyQ(aim, t + 0x20);
        sceVu0AddVector(aim, aim, t);
        sceVu0SubVector(d, aim, s);
        sceVu0ApplyMatrix(d, inv, d);
        const f32 yaw = ee::atan2f(F(d), -F(d + 4));
        // sqrt.S; the original's NaN fallback to sqrtf cannot trigger on the EE.
        const f32 off = ee::atan2f(fpu::Sqrt(fpu::Add(F(d) * F(d), F(d + 4) * F(d + 4))), F(d + 8));
        if (off < F(0x0040E694) && F(0x0040E698) < off) {
            GRef<u8>(t + 0x67) = static_cast<u8>(i);
            GRef<u32>(t + 0x50) = W(t + 0x50) | 0x200u;
            const f32 c1 = F(0x0040E69C), c2 = F(0x0040E6A0);
            if (c1 < off)
                GRef<f32>(s + 0x40) = c1;
            else if (off < c2)
                GRef<f32>(s + 0x40) = c2;
            else
                GRef<f32>(s + 0x40) = off;
            GRef<f32>(s + 0x18) = yaw;
            GRef<u32>(s + 0x44) = 0;
            GRef<u32>(s + 0x48) = 0;
            GRef<u32>(s + 0x4C) = 0;
            func_00102EF0(s + 0x10, s + 0x10, s + 0x40);
            GRef<u32>(s + 0x18) = 0;
        }
    }
    if (F(s + 0x38) < F(0x0040E6A4)) GRef<f32>(s + 0x38) = fpu::Add(F(s + 0x38), F(0x0040E6A8));
    sceVu0ApplyMatrix(s + 0x20, m, s + 0x30);
}

// EE: 0x001512B0
// Kind 4 (ids 12/13): as 0x00150CD0 (hit point w = 1, explosion 0x81, SE 1, missed message on
// terrain), the trail (colour 0x78, 0x30 in mission kind 2) only after the drop (+0x6C - C
// 0x0040E6B4 < age * C 0x0040E6B0).
void func_001512B0(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x240);
    const u32 col = fr.Address(0), hit = fr.Address(0x160), nrm = fr.Address(0x170), attr = fr.Address(0x180);
    const u8 ow = GRef<u8>(s + 0x58);
    const u8 sb = GRef<u8>(Unit(ow) + 0x48);
    func_0017C940(col, s + 0x10, s, vel, ow);
    for (s32 j = 0; j < UnitCount(); ++j) {
        const u32 o = Unit(j);
        if ((~W(o + 0x4C) & 0x10000000u) != 0) continue;
        if (func_0017C9B8(col, o, hit, nrm, j) != 1) continue;
        CopyQ(s, hit);
        GRef<f32>(s + 0xC) = 1.0f;
        if ((sb & 3u) != (W(o + 0x48) & 3u)) GRef<u32>(o + 0x50) |= 4u;
        GRef<u8>(o + 0x63) = GRef<u8>(s + 0x58);
        CopyQ(o + 0x30, vel);
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_001F08E8(s, vel, 0x81);
        PlayEventSe(1, s);
        break;
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    if (func_0017CB70(col, hit, nrm, attr) == 1) {
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_00154428(hit, vel, nrm, GRef<s32>(attr));
        if ((~W(s + 0x70) & 0x10u) != 0) {
            GRef<u32>(s + 0x70) = W(s + 0x70) | 0x10u;
            ShowCenterMessage(GRef<u8>(s + 0x58), 0xD, 0);
        }
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    sceVu0AddVector(s, s, vel);
    if (fpu::Sub(F(s + 0x6C), F(0x0040E6B4)) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * F(0x0040E6B0)) {
        const s32 colr = GRef<s8>(Scene() + 0x1653) == 2 ? 0x30 : 0x78;
        GRef<u32>(s + 0x68) = func_001DAB18(W(s + 0x68), s, vel, colr, 1);
    }
}

// EE: 0x00151550
// Homing missile init (id 12: k = 1, life 540; id 13: k = 0, life 720): kind 4, trail none, turn
// angles 0, +0x6C = cos(roll) (the drop time), local velocity.
void func_00151550(u32 s, s32 k) {
    GRef<u8>(s + 0x5B) = 4;
    if (k == 0)
        GRef<u32>(s + 0x54) = 0x2D0;
    else if (k == 1)
        GRef<u32>(s + 0x54) = 0x21C;
    GRef<s32>(s + 0x68) = -1;
    GRef<u32>(s + 0x40) = 0;
    GRef<u32>(s + 0x48) = 0;
    GRef<u32>(s + 0x44) = 0;
    GRef<f32>(s + 0x6C) = ee::cosf(F(s + 0x18));
    guest::GuestFrame fr(0x80);
    const u32 m = fr.Address(0), inv = fr.Address(0x40);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(s + 0x30, inv, s + 0x20);
}

// EE: 0x001515F8
// Kind 5 pre-step: none.
void func_001515F8(u32 /*i*/) {}

// EE: 0x00151600
// Kind 5 (id 7, spinning dispenser): spins (pitch C 0x0040E6B8, wrapped) and falls until it hits;
// then bounces with friction (C 0x0040E6C8), its spin slowed in double precision by the ground
// speed (libgcc soft-double: IEEE, the host gives the same bits); unit (0x001542C0) and terrain
// contacts bounce it (first contact: state 1, life 120, SE 0); in state 1 the blast grows by
// C 0x0040E6D8 to +0x60 with area damage kind 4 and a burst 0x001E9570 every 8 frames and at the
// end.
// fpucheck: 6 static adds
void func_00151600(u32 i) {
    const u32 s = Special(i);
    if (GRef<u8>(s + 0x5E) == 0) {
        const f32 rx = fpu::Add(F(s + 0x10), F(0x0040E6B8));
        const f32 vy = fpu::Add(F(s + 0x24), F(0x0040E6BC));
        GRef<f32>(s + 0x10) = rx;
        GRef<f32>(s + 0x24) = vy;
        if (F(0x0040E6C0) < rx) GRef<f32>(s + 0x10) = fpu::Sub(rx, F(0x0040E6C4));
    } else {
        const f32 k = F(0x0040E6C8);
        const f32 vz = F(s + 0x28) * k;
        const f32 vy = fpu::Add(F(s + 0x24), F(0x0040E6CC));
        GRef<f32>(s + 0x20) = F(s + 0x20) * k;
        GRef<f32>(s + 0x28) = vz;
        GRef<f32>(s + 0x24) = vy;
        const double rx = static_cast<double>(F(s + 0x10));
        double ax = static_cast<double>(F(s + 0x20));
        if (ax < 0.0) ax = 0.0 - ax;
        double az = static_cast<double>(F(s + 0x28));
        if (az < 0.0) az = 0.0 - az;
        const double spin = (ax + az) * 5.0 * D64(0x00404828) / D64(0x00404830) * D64(0x00404838);
        f32 r = static_cast<f32>(rx - spin);
        GRef<f32>(s + 0x10) = r;
        const f32 lo = F(0x0040E6D0);
        while (r < lo) {
            r = fpu::Add(F(s + 0x10), F(0x0040E6D4));
            GRef<f32>(s + 0x10) = r;
        }
    }
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x60);
    const u32 hit = fr.Address(0), nrm = fr.Address(0x10), nx = fr.Address(0x20), tmp = fr.Address(0x30),
              back = fr.Address(0x40), attr = fr.Address(0x50);
    sceVu0AddVector(nx, s, vel);
    auto first = [&]() {
        if (GRef<u8>(s + 0x5E) != 0) return;
        GRef<u32>(s + 0x50) = 0;
        GRef<u8>(s + 0x5E) = 1;
        GRef<u32>(s + 0x54) = 0x78;
        PlayEventSe(0, hit);
    };
    if (func_001542C0(GRef<u8>(s + 0x5A), hit, nrm, s, s + 0x10, vel, GRef<u8>(s + 0x58)) == 1) {
        first();
        Bounce(s, vel, hit, nrm, tmp, back);
    } else {
        sceVu0CopyVector(hit, nx);
    }
    if (func_0013D698(Terrain(), hit, attr, s, nx, nrm) == 1) {
        first();
        Bounce(s, vel, hit, nrm, tmp, back);
    } else {
        sceVu0CopyVector(hit, nx);
    }
    if (GRef<u8>(s + 0x5E) == 1) {
        const f32 mx = F(s + 0x60);
        f32 r = F(s + 0x64);
        if (r < mx) {
            r = fpu::Add(r, F(0x0040E6D8));
            GRef<f32>(s + 0x64) = r;
        }
        if (mx <= r) GRef<f32>(s + 0x64) = mx;
        ApplyAreaDamage(F(s + 0x64), hit, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
        const u32 age = W(s + 0x50);
        if ((age & 7u) == 0 || age == W(s + 0x54)) func_001E9570(F(s + 0x64), hit, vel);
    }
    sceVu0AddVector(s, s, vel);
}

// EE: 0x00151BA8
// Dispenser init (id 7): kind 5, blast radius 500, life 1200, state 0.
void func_00151BA8(u32 s) {
    GRef<u8>(s + 0x5B) = 5;
    GRef<f32>(s + 0x60) = 500.0f;
    GRef<u32>(s + 0x54) = 0x4B0;
    GRef<u8>(s + 0x5E) = 0;
    GRef<u32>(s + 0x64) = 0;
}

// EE: 0x00151BD0
// Kind 6 pre-step: gravity C 0x0040E6DC while falling, stopped once burst.
// fpucheck: 1 add
void func_00151BD0(u32 i) {
    const u32 s = Special(i);
    if (GRef<u8>(s + 0x5E) == 0) {
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E6DC));
        return;
    }
    ZeroQ(s + 0x20);
}

// EE: 0x00151C10
// Kind 6 (id 4, cluster bomb): on its first unit/terrain contact it bursts (life 128, splash
// 0x00196828 on water, else blast 0x001E95C0 r 300, SE 0x11); then on odd frames a sub-blast on a
// rotating ring (angle from (age >> 1) & 7 * 135 degrees; distance 0 on the burst frame, 2 x radius
// until frame 16, then random up to 1400; 1 random read) dropped to the terrain, with area damage
// kind 4 and a visible blast every 14th frame.
// fpucheck: 5 static adds
void func_00151C10(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x60);
    const u32 nx = fr.Address(0), hit = fr.Address(0x10), p = fr.Address(0x20), to = fr.Address(0x30),
              nrm2 = fr.Address(0x40), attr = fr.Address(0x50);
    bool burst = false;
    sceVu0AddVector(nx, s, vel);
    u8 st = GRef<u8>(s + 0x5E);
    if (st == 0) {
        if (func_001542C0(GRef<u8>(s + 0x5A), nx, hit, s, s + 0x10, vel, GRef<u8>(s + 0x58)) == 1 ||
            func_0013D698(Terrain(), nx, attr, s, nx, hit) == 1) {
            GRef<u8>(s + 0x5E) = 1;
            GRef<u32>(s + 0x50) = 0;
            GRef<u32>(s + 0x54) = 0x80;
            sceVu0SubVector(vel, nx, s);
            if (GRef<s32>(attr) == 2)
                func_00196828(2, nx, vel, 3, 0x200);
            else
                func_001E95C0(300.0f, nx, vel);
            PlayEventSe(0x11, s);
            burst = true;
        }
        st = GRef<u8>(s + 0x5E);
    }
    if (st == 1) {
        const f32 r = F(s + 0x60);
        const s32 age = GRef<s32>(s + 0x50);
        GRef<f32>(s + 0x64) = r;
        if ((age & 1) != 0 || burst) {
            const s32 deg = ((age >> 1) & 7) * 0x87 % 0x168 - 0xB4;
            const f32 a = fpu::IntToFloat(deg) * F(0x0040E6E0) / 180.0f;
            f32 d;
            if (burst)
                d = 0.0f;
            else if (age < 0x11)
                d = fpu::Add(r, r);
            else
                d = NextRandomFloat() * fpu::Sub(1400.0f, F(s + 0x64));
            CopyQ(p, nx);
            GRef<f32>(p) = fpu::Add(F(p), ee::sinf(a) * d);
            GRef<f32>(p + 8) = fpu::Add(F(p + 8), ee::cosf(a) * d);
            CopyQ(to, p);
            GRef<f32>(to + 4) = 64.0f;
            GRef<f32>(p + 4) = fpu::Sub(F(nx + 4), 80.0f);
            if (func_0013D698(Terrain(), hit, attr + 4, p, to, nrm2) == 0) {
                CopyQ(hit, p);
                GRef<u32>(hit + 4) = W(nx + 4);
            }
            ApplyAreaDamage(F(s + 0x64), hit, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
            if (GRef<s32>(s + 0x50) % 0xE == 0xD) {
                if (GRef<s32>(attr + 4) == 2)
                    func_00196828(2, hit, vel, 3, 0x200);
                else
                    func_001E95C0(500.0f, hit, vel);
            }
        }
        st = GRef<u8>(s + 0x5E);
    }
    if (st == 9) GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
    sceVu0AddVector(s, s, vel);
}

// EE: 0x00151F50
// Cluster bomb init (id 4): kind 6, life 1200, blast radius 180, state 0 (the release vector it
// adds is zero).
void func_00151F50(u32 s) {
    GRef<u8>(s + 0x5B) = 6;
    GRef<u32>(s + 0x54) = 0x4B0;
    GRef<f32>(s + 0x60) = 180.0f;
    GRef<u8>(s + 0x5E) = 0;
    GRef<u32>(s + 0x64) = 0;
    guest::GuestFrame fr(0x50);
    const u32 m = fr.Address(0), loc = fr.Address(0x40);
    ZeroQ(loc);
    GRef<u32>(loc + 8) = 0;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0ApplyMatrix(loc, m, loc);
    sceVu0AddVector(s + 0x20, s + 0x20, loc);
}

// EE: 0x00151FE8
// Kind 7 pre-step: gravity C 0x0040E6E4 until it lands.
// fpucheck: 1 add
void func_00151FE8(u32 i) {
    const u32 s = Special(i);
    if (GRef<u8>(s + 0x5E) == 0) GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E6E4));
}

// EE: 0x00152020
// Kind 7 (id 6, scattered blasts): on terrain contact it bursts (life 128, splash or blast
// 0x001E9598 r 200, SE 0xF); then every other frame (by age + slot) a blast at a random point
// within 3 x radius (2 random reads, the ground point by a vertical ray), area damage kind 4, a
// visible blast every 16th frame.
// fpucheck: 4 adds
void func_00152020(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x60);
    const u32 nx = fr.Address(0), hit = fr.Address(0x10), p = fr.Address(0x20), to = fr.Address(0x30),
              nrm2 = fr.Address(0x40), attr = fr.Address(0x50);
    sceVu0AddVector(nx, s, vel);
    u8 st = GRef<u8>(s + 0x5E);
    if (st == 0) {
        if (func_0013D698(Terrain(), nx, attr, s, nx, hit) == 1) {
            GRef<u8>(s + 0x5E) = 1;
            GRef<u32>(s + 0x54) = 0x80;
            GRef<u32>(s + 0x50) = 0;
            if (GRef<s32>(attr) == 2)
                func_00196828(2, nx, vel, 0x20, 0x200);
            else
                func_001E9598(200.0f, nx, vel);
            PlayEventSe(0xF, s);
        }
        st = GRef<u8>(s + 0x5E);
    }
    if (st == 1) {
        GRef<u32>(s + 0x64) = W(s + 0x60);
        if (((GRef<s32>(s + 0x50) + static_cast<s32>(i)) & 1) != 0) {
            const f32 r1 = NextRandomFloat();
            const f32 a = fpu::Sub(fpu::Add(r1, r1), 1.0f) * F(0x0040E6E8);
            func_00104FA8(vel);  // result unused
            const f32 d = NextRandomFloat() * F(s + 0x60) * 3.0f;
            CopyQ(p, nx);
            GRef<f32>(p) = fpu::Add(F(p), ee::sinf(a) * d);
            GRef<f32>(p + 8) = fpu::Add(F(p + 8), ee::cosf(a) * d);
            CopyQ(to, p);
            GRef<u32>(p + 4) = 0xC6A00000u;  // -20480
            GRef<f32>(to + 4) = 64.0f;
            func_0013D698(Terrain(), hit, attr + 4, p, to, nrm2);
            ApplyAreaDamage(F(s + 0x64), hit, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
            if (((GRef<s32>(s + 0x50) + static_cast<s32>(i)) & 0xF) == 0xF) {
                if (GRef<s32>(attr + 4) == 2)
                    func_00196828(2, hit, vel, 0x20, 0x200);
                else
                    func_001E9598(200.0f, hit, vel);
            }
        }
    }
    sceVu0AddVector(s, s, vel);
}

// EE: 0x00152298
// Scattered blasts init (id 6): kind 7, blast radius 128, life 1200, state 0.
void func_00152298(u32 s) {
    GRef<u8>(s + 0x5B) = 7;
    GRef<f32>(s + 0x60) = 128.0f;
    GRef<u32>(s + 0x54) = 0x4B0;
    GRef<u8>(s + 0x5E) = 0;
    GRef<u32>(s + 0x64) = 0;
}

// EE: 0x001522C0
// Kind 12 pre-step: gravity C 0x0040E6EC while falling, stopped once burning.
// fpucheck: 1 add
void func_001522C0(u32 i) {
    const u32 s = Special(i);
    if (GRef<u8>(s + 0x5E) == 0) {
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E6EC));
        return;
    }
    ZeroQ(s + 0x20);
}

// EE: 0x00152300
// Kind 12 (id 16, ground fire): on terrain contact it ignites for 16 frames (splash 0x00196828
// by material, SE 0x10); then every other frame (age + slot) area damage kind 4 at a random ground
// point within 3 x radius (2 random reads, height 0x0013E2B0); while flying it moves with its trail
// 0x001EC268.
// fpucheck: 4 adds
void func_00152300(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x30);
    const u32 nx = fr.Address(0), p = fr.Address(0x10), attr = fr.Address(0x20);
    sceVu0AddVector(nx, s, vel);
    u8 st = GRef<u8>(s + 0x5E);
    if (st == 0) {
        if (func_0013D698(Terrain(), nx, attr, s, nx, p) == 1) {
            GRef<u8>(s + 0x5E) = 1;
            GRef<u32>(s + 0x54) = 0x10;
            GRef<u32>(s + 0x50) = 0;
            const s32 a = GRef<s32>(attr);
            func_00196828(a == 2 ? 2 : a, nx, vel, 2, 0x200);
            PlayEventSe(0x10, s);
        }
        st = GRef<u8>(s + 0x5E);
    }
    if (st == 1) {
        GRef<u32>(s + 0x64) = W(s + 0x60);
        if (((GRef<s32>(s + 0x50) + static_cast<s32>(i)) & 1) != 0) {
            CopyQ(p, nx);
            const f32 r1 = NextRandomFloat();
            const f32 a = fpu::Sub(fpu::Add(r1, r1), 1.0f) * F(0x0040E6F0);
            const f32 d = NextRandomFloat() * F(s + 0x60) * 3.0f;
            GRef<f32>(p) = fpu::Add(F(p), ee::sinf(a) * d);
            GRef<f32>(p + 8) = fpu::Add(F(p + 8), ee::cosf(a) * d);
            GRef<f32>(p + 4) = func_0013E2B0(Terrain(), p);
            ApplyAreaDamage(F(s + 0x64), p, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
        }
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    sceVu0AddVector(s, s, vel);
    GRef<u32>(s + 0x68) = func_001EC268(W(s + 0x68), s, s + 0x10, vel);
}

// EE: 0x001524F0
// Ground fire init (id 16): kind 12, trail none, life 1200, radius 96, state 0.
void func_001524F0(u32 s) {
    GRef<u8>(s + 0x5B) = 0xC;
    GRef<s32>(s + 0x68) = -1;
    GRef<u32>(s + 0x54) = 0x4B0;
    GRef<f32>(s + 0x60) = 96.0f;
    GRef<u8>(s + 0x5E) = 0;
    GRef<u32>(s + 0x64) = 0;
}

// EE: 0x00152520
// Kind 8 pre-step (id 9): the roll (+0x18) is taken out and decays back toward 0 by C 0x0040E724
// at the end. While age x C 0x0040E6F4 <= +0x6C it climbs (vy += C 0x0040E720, local velocity
// kept in +0x30) and fires event 0x0021AE30 on the last such frame. After that, with a target, it
// steers toward the target (aim point 2.5 x its +0x70 below centre, vertical doubled, in local
// space): far away or off the 8-frame beat it turns by atan2 (only inside the C 0x0040E708..704
// cone; marks the target locked, +0x50 |= 0x200 and +0x67 = slot; pitch clamped to C 0x0040E710..
// 0x0040E70C); within 2048 on the beat it fires a sub-weapon (id 0x10) sideways (side by age bit 3).
// The local speed +0x38 is pulled toward C 0x0040E714 and the world velocity rebuilt.
// fpucheck: 9 adds
void func_00152520(u32 i) {
    const u32 s = Special(i);
    const u32 rot = s + 0x10;
    const u32 vel = s + 0x20;
    f32 roll = F(s + 0x18);
    const f32 k = F(0x0040E6F4);
    guest::GuestFrame fr(0xB0);
    const u32 m = fr.Address(0), inv = fr.Address(0x40), d = fr.Address(0x80), aim = fr.Address(0x90),
              fv = fr.Address(0xA0);
    GRef<u32>(s + 0x18) = 0;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    sceVu0InversMatrix(inv, m);
    GRef<u32>(s + 0x70) = W(s + 0x70) & 0xFFFFFFDFu;
    if (!(F(s + 0x6C) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * k)) {
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E720));
        sceVu0ApplyMatrix(s + 0x30, inv, vel);
        if (F(s + 0x6C) < fpu::IntToFloat(GRef<s32>(s + 0x50) + 1) * k) {
            const u32 ev = 0x002B67F0;
            const u8 owner = GRef<u8>(s + 0x58);
            GRef<u32>(ev) = owner;
            GRef<s32>(ev + 8) = GRef<s8>(W(Unit(owner) + 0x44) + 0x149);
            GRef<u32>(ev + 4) = GRef<u8>(s + 0x5A);
            CopyQ(ev + 0x10, s);
            func_0021AE30(ev);
        }
    } else {
        const u8 tgt = GRef<u8>(s + 0x59);
        if (tgt != 0xFF) {
            const u32 u = Unit(tgt);
            CopyQ(aim, u + 0x20);
            sceVu0AddVector(aim, aim, u);
            GRef<f32>(aim + 4) = fpu::Sub(F(aim + 4), F(u + 0x70) * 2.5f);
            sceVu0SubVector(d, aim, s);
            GRef<f32>(d + 4) = fpu::Add(F(d + 4), F(d + 4));
            sceVu0ApplyMatrix(d, inv, d);
            const f32 len = GetVectorLength(d);
            if (len < 2048.0f && (GRef<s32>(s + 0x50) & 7) == 0) {
                const f32 kx = F(0x0040E6F8);
                CopyQ(fv, vel);
                const f32 h = func_00104FA8(fv);
                const f32 c = F(0x0040E700);
                const f32 x = F(fv), z = F(fv + 8);
                const f32 sx = x * kx / h;
                const f32 sz = z * kx / h;
                GRef<f32>(fv + 4) = fpu::Add(fpu::Add(F(d + 4), 128.0f), F(0x0040E6FC)) * k;
                if ((GRef<s32>(s + 0x50) & 8) == 0) {
                    GRef<f32>(fv) = fpu::Sub(x * c, sz);
                    GRef<f32>(fv + 8) = fpu::Add(z * c, sx);
                } else {
                    GRef<f32>(fv) = fpu::Add(x * c, sz);
                    GRef<f32>(fv + 8) = fpu::Sub(z * c, sx);
                }
                GRef<u32>(s + 0x70) = W(s + 0x70) | 0x20u;
                FireSpecialWeapon(0x10, GRef<u8>(s + 0x58), GRef<u8>(s + 0x59), s, rot, fv, GRef<u8>(s + 0x5F));
            } else {
                const f32 dx = F(d), dy = F(d + 4);
                const f32 yaw = ee::atan2f(dx, -dy);
                const f32 pitch = ee::atan2f(fpu::Sqrt(fpu::Add(dx * dx, dy * dy)), F(d + 8));
                if (pitch < F(0x0040E704) && F(0x0040E708) < pitch) {
                    GRef<u8>(u + 0x67) = static_cast<u8>(i);
                    GRef<u32>(u + 0x50) = W(u + 0x50) | 0x200u;
                    f32 p = pitch;
                    if (F(0x0040E70C) < pitch)
                        p = F(0x0040E70C);
                    else if (pitch < F(0x0040E710))
                        p = F(0x0040E710);
                    GRef<f32>(s + 0x40) = p;
                    GRef<f32>(s + 0x18) = yaw;
                    GRef<u32>(s + 0x44) = 0;
                    GRef<u32>(s + 0x48) = 0;
                    GRef<u32>(s + 0x4C) = 0;
                    func_00102EF0(rot, rot, s + 0x40);
                    GRef<u32>(s + 0x18) = 0;
                }
            }
        }
        f32 sp = F(s + 0x38);
        const f32 cap = F(0x0040E714);
        if (sp < cap) {
            sp = fpu::Add(sp, F(0x0040E718));
            GRef<f32>(s + 0x38) = sp;
        }
        if (cap < sp) GRef<f32>(s + 0x38) = fpu::Sub(sp, F(0x0040E71C));
        sceVu0ApplyMatrix(vel, m, s + 0x30);
    }
    const f32 step = F(0x0040E724);
    if (step < roll)
        roll = fpu::Sub(roll, step);
    else if (roll < F(0x0040E728))
        roll = fpu::Add(roll, step);
    else
        roll = 0.0f;
    GRef<f32>(s + 0x18) = roll;
}

// EE: 0x00152998
// Kind 8 step: swept hit against every live unit (first hit: moved to the hit point, unit gets the
// knock velocity and the hit flag 4 when it is not on the owner's team, impact effect 0x81), else
// terrain (impact 0x00154428, owner message 0xD once); still flying it moves and, past +0x6C -
// C 0x0040E730, leaves a trail (0x18 in scene type 2, else 0x3C).
void func_00152998(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x190);
    const u32 col = fr.Address(0), hit = fr.Address(0x160), nrm = fr.Address(0x170), attr = fr.Address(0x180);
    const u32 team = W(Unit(GRef<u8>(s + 0x58)) + 0x48) & 3u;
    func_0017C940(col, s + 0x10, s, vel, GRef<u8>(s + 0x58));
    for (s32 n = 0; n < GRef<s32>(W(0x00410640)); ++n) {
        const u32 u = Unit(n);
        if ((~W(u + 0x4C) & 0x10000000u) != 0) continue;
        if (func_0017C9B8(col, u, hit, nrm, n) != 1) continue;
        CopyQ(s, hit);
        if (team != (W(u + 0x48) & 3u)) GRef<u32>(u + 0x50) = W(u + 0x50) | 4u;
        GRef<u8>(u + 0x63) = GRef<u8>(s + 0x58);
        CopyQ(u + 0x30, vel);
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_001F08E8(s, vel, 0x81);
        break;
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    if (func_0017CB70(col, hit, nrm, attr) == 1) {
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_00154428(hit, vel, nrm, GRef<s32>(attr));
        if ((W(s + 0x70) & 0x10u) == 0) {
            GRef<u32>(s + 0x70) = W(s + 0x70) | 0x10u;
            ShowCenterMessage(GRef<u8>(s + 0x58), 0xD, 0);
        }
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    sceVu0AddVector(s, s, vel);
    if (fpu::Sub(F(s + 0x6C), F(0x0040E730)) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * F(0x0040E72C)) {
        const s32 len = GRef<s8>(Scene() + 0x1653) == 2 ? 0x18 : 0x3C;
        GRef<u32>(s + 0x68) = func_001DAB18(W(s + 0x68), s, vel, len, 1);
    }
}

// EE: 0x00152C18
// Kind 8 init (id 9): kind 8, life 1200, climb time +0x6C = cos(+0x18), local velocity +0x30,
// no trail, steering angles cleared. (It also fills an unused stack vector.)
void func_00152C18(u32 s) {
    GRef<u8>(s + 0x5B) = 8;
    GRef<u32>(s + 0x54) = 0x4B0;
    GRef<f32>(s + 0x6C) = ee::cosf(F(s + 0x18));
    guest::GuestFrame fr(0x80);
    const u32 m = fr.Address(0), inv = fr.Address(0x40);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(s + 0x30, inv, s + 0x20);
    GRef<s32>(s + 0x68) = -1;
    GRef<f32>(s + 0x40) = 0.0f;
    GRef<f32>(s + 0x48) = 0.0f;
    GRef<f32>(s + 0x44) = 0.0f;
}

// EE: 0x00152CC8
// Kind 9 pre-step (id 14): local velocity x,y damped by C 0x0040E738. Once age x C 0x0040E73C
// passes C 0x0040E740 it homes on a target with flag 0x01000000 (marks it locked, +0x50 |= 0x200,
// +0x67 = slot; yaw by atan2, pitch clamped to C 0x0040E748..0x0040E744) and slows down by
// C 0x0040E760 down to C 0x0040E75C unless the pitch is inside C 0x0040E74C..0x0040E750; otherwise
// it speeds up by C 0x0040E758 up to C 0x0040E754. The world velocity is rebuilt.
// fpucheck: 3 adds
void func_00152CC8(u32 i) {
    const u32 s = Special(i);
    const u32 rot = s + 0x10;
    guest::GuestFrame fr(0xA0);
    const u32 m = fr.Address(0), inv = fr.Address(0x40), d = fr.Address(0x80), aim = fr.Address(0x90);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    sceVu0InversMatrix(inv, m);
    const f32 damp = F(0x0040E738);
    GRef<f32>(s + 0x30) = F(s + 0x30) * damp;
    GRef<f32>(s + 0x34) = F(s + 0x34) * damp;
    s32 mode = 1;
    const u8 tgt = GRef<u8>(s + 0x59);
    if (tgt != 0xFF) {
        const u32 u = Unit(tgt);
        if ((W(u + 0x4C) & 0x01000000u) != 0 &&
            F(0x0040E740) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * F(0x0040E73C)) {
            sceVu0ScaleVector(aim, u + 0x20, 1.0f);
            sceVu0AddVector(aim, aim, u);
            sceVu0SubVector(d, aim, s);
            sceVu0ApplyMatrix(d, inv, d);
            const f32 dx = F(d), dy = F(d + 4);
            const f32 yaw = ee::atan2f(dx, -dy);
            const f32 pitch = ee::atan2f(fpu::Sqrt(fpu::Add(dx * dx, dy * dy)), F(d + 8));
            GRef<u32>(u + 0x50) = W(u + 0x50) | 0x200u;
            GRef<u8>(u + 0x67) = static_cast<u8>(i);
            f32 p = pitch;
            if (F(0x0040E744) < pitch)
                p = F(0x0040E744);
            else if (pitch < F(0x0040E748))
                p = F(0x0040E748);
            GRef<f32>(s + 0x40) = p;
            GRef<f32>(s + 0x18) = yaw;
            GRef<u32>(s + 0x44) = 0;
            GRef<u32>(s + 0x48) = 0;
            GRef<u32>(s + 0x4C) = 0;
            func_00102EF0(rot, rot, s + 0x40);
            mode = 2;
            GRef<u32>(s + 0x18) = 0;
            if (F(0x0040E74C) < pitch && pitch < F(0x0040E750)) mode = 1;
        }
    }
    if (mode == 1) {
        if (F(s + 0x38) < F(0x0040E754)) GRef<f32>(s + 0x38) = fpu::Add(F(s + 0x38), F(0x0040E758));
    } else if (mode == 2) {
        if (F(0x0040E75C) < F(s + 0x38)) GRef<f32>(s + 0x38) = fpu::Sub(F(s + 0x38), F(0x0040E760));
    }
    sceVu0ApplyMatrix(s + 0x20, m, s + 0x30);
}

// EE: 0x00152F78
// Kind 9 step: like kind 8 (0x00152998) but a unit hit also calls 0x00219188(1, s), the terrain
// message 0xD is sent on every terrain hit, and the trail length is always 0x1E from the start.
void func_00152F78(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x190);
    const u32 col = fr.Address(0), hit = fr.Address(0x160), nrm = fr.Address(0x170), attr = fr.Address(0x180);
    const u32 team = W(Unit(GRef<u8>(s + 0x58)) + 0x48) & 3u;
    func_0017C940(col, s + 0x10, s, vel, GRef<u8>(s + 0x58));
    for (s32 n = 0; n < GRef<s32>(W(0x00410640)); ++n) {
        const u32 u = Unit(n);
        if ((~W(u + 0x4C) & 0x10000000u) != 0) continue;
        if (func_0017C9B8(col, u, hit, nrm, n) != 1) continue;
        CopyQ(s, hit);
        if (team != (W(u + 0x48) & 3u)) GRef<u32>(u + 0x50) = W(u + 0x50) | 4u;
        GRef<u8>(u + 0x63) = GRef<u8>(s + 0x58);
        CopyQ(u + 0x30, vel);
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_001F08E8(s, vel, 0x81);
        PlayEventSe(1, s);
        break;
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    if (func_0017CB70(col, hit, nrm, attr) == 1) {
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        func_00154428(hit, vel, nrm, GRef<s32>(attr));
        ShowCenterMessage(GRef<u8>(s + 0x58), 0xD, 0);
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    sceVu0AddVector(s, s, vel);
    GRef<u32>(s + 0x68) = func_001DAB18(W(s + 0x68), s, vel, 0x1E, 1);
}

// EE: 0x001531A0
// Kind 9 init (id 14): kind 9, life 1440, local velocity +0x30, no trail, steering angles cleared.
void func_001531A0(u32 s) {
    GRef<u8>(s + 0x5B) = 9;
    GRef<u32>(s + 0x54) = 0x5A0;
    guest::GuestFrame fr(0x80);
    const u32 m = fr.Address(0), inv = fr.Address(0x40);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(s + 0x30, inv, s + 0x20);
    GRef<s32>(s + 0x68) = -1;
    GRef<f32>(s + 0x40) = 0.0f;
    GRef<f32>(s + 0x48) = 0.0f;
    GRef<f32>(s + 0x44) = 0.0f;
}

// Kind chosen when a kind-10 weapon ends its climb: 10 (keep) against a unit of type 6 or with
// +0x69 >= 0xD0, else 3. Shared by 0x00153228 and 0x00153958 (inlined in both).
static u8 Kind10Follow(u32 s) {
    const u8 tgt = GRef<u8>(s + 0x59);
    if (tgt == 0xFF) return 3;
    const u32 u = Unit(tgt);
    if (GRef<u8>(u + 0x6A) == 6) return 10;
    return GRef<u8>(u + 0x69) >= 0xD0 ? 10 : 3;
}

// EE: 0x00153228
// Kind 10 pre-step (id 11): while age x C 0x0040E764 <= +0x6C it climbs (vy += C 0x0040E7BC, local
// velocity kept); on the last such frame it switches to kind 3 or stays 10 (Kind10Follow) and fires
// event 0x0021AE30. Afterwards, with a target, it steers: pitch toward the target (asin of the
// height difference; beyond 4096 toward terrain + 256 sixty frames ahead, at least C 0x0040E768),
// yaw by atan2, both wrapped and turned by at most C 0x0040E784 / 0x0040E790 a frame, pitch kept
// above C 0x0040E78C, angles wrapped. Speed +0x38 grows by C 0x0040E7B4 up to C 0x0040E7B0; local
// x,y damped by C 0x0040E7B8; world velocity rebuilt.
// fpucheck: 21 adds
void func_00153228(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0xB0);
    const u32 m = fr.Address(0), inv = fr.Address(0x40), d = fr.Address(0x80), aim = fr.Address(0x90),
              ahead = fr.Address(0xA0);
    sceVu0UnitMatrix(m);
    const f32 k = F(0x0040E764);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    if (!(F(s + 0x6C) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * k)) {
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E7BC));
        sceVu0ApplyMatrix(s + 0x30, inv, vel);
        if (!(F(s + 0x6C) < fpu::Add(fpu::IntToFloat(GRef<s32>(s + 0x50)) * k, k))) return;
        GRef<u8>(s + 0x5B) = Kind10Follow(s);
        const u32 ev = 0x002B67F0;
        const u8 owner = GRef<u8>(s + 0x58);
        GRef<u32>(ev) = owner;
        GRef<s32>(ev + 8) = GRef<s8>(W(Unit(owner) + 0x44) + 0x149);
        GRef<u32>(ev + 4) = GRef<u8>(s + 0x5A);
        CopyQ(ev + 0x10, s);
        func_0021AE30(ev);
        return;
    }
    const u8 tgt = GRef<u8>(s + 0x59);
    if (tgt != 0xFF) {
        const u32 u = Unit(tgt);
        CopyQ(aim, u + 0x20);
        sceVu0AddVector(aim, aim, u);
        sceVu0SubVector(d, aim, s);
        const f32 len = GetVectorLength(d);
        f32 dp;
        if (len < 4096.0f) {
            dp = fpu::Sub(ee::asinf(-F(d + 4) / len), F(s + 0x10));
        } else {
            sceVu0ScaleVector(ahead, vel, 60.0f);
            sceVu0AddVector(ahead, s, ahead);
            GRef<f32>(ahead + 4) = fpu::Sub(func_0013E2B0(Terrain(), ahead), 256.0f);
            sceVu0SubVector(ahead, ahead, s);
            const f32 l2 = GetVectorLength(ahead);
            dp = ee::asinf(-F(ahead + 4) / l2);
            if (dp < F(0x0040E768)) dp = F(0x0040E768);
            dp = fpu::Sub(dp, F(s + 0x10));
        }
        const f32 yaw0 = F(s + 0x14);
        f32 dy = fpu::Sub(ee::atan2f(F(d), F(d + 8)), yaw0);
        const f32 hi = F(0x0040E76C), lo = F(0x0040E774);
        if (hi < dp) dp = fpu::Sub(dp, F(0x0040E770));
        if (dp < lo) dp = fpu::Add(dp, F(0x0040E778));
        if (hi < dy) dy = fpu::Sub(dy, F(0x0040E77C));
        if (dy < lo) dy = fpu::Add(dy, F(0x0040E780));
        const f32 tp = F(0x0040E784);
        if (tp < dp)
            GRef<f32>(s + 0x10) = fpu::Add(F(s + 0x10), tp);
        else if (dp < F(0x0040E788))
            GRef<f32>(s + 0x10) = fpu::Sub(F(s + 0x10), tp);
        else
            GRef<f32>(s + 0x10) = fpu::Add(F(s + 0x10), dp);
        f32 px = F(s + 0x10);
        if (px < F(0x0040E78C)) {
            GRef<f32>(s + 0x10) = F(0x0040E78C);
            px = F(0x0040E78C);
        }
        const f32 ty = F(0x0040E790);
        if (ty < dy)
            GRef<f32>(s + 0x14) = fpu::Add(yaw0, ty);
        else if (dy < F(0x0040E794))
            GRef<f32>(s + 0x14) = fpu::Sub(yaw0, ty);
        else
            GRef<f32>(s + 0x14) = fpu::Add(yaw0, dy);
        const f32 wh = F(0x0040E798), wl = F(0x0040E7A0);
        if (wh < px) {
            px = fpu::Sub(px, F(0x0040E79C));
            GRef<f32>(s + 0x10) = px;
        }
        if (px < wl) GRef<f32>(s + 0x10) = fpu::Add(px, F(0x0040E7A4));
        f32 py = F(s + 0x14);
        if (wh < py) {
            py = fpu::Sub(py, F(0x0040E7A8));
            GRef<f32>(s + 0x14) = py;
        }
        if (py < wl) GRef<f32>(s + 0x14) = fpu::Add(py, F(0x0040E7AC));
    }
    f32 sp = F(s + 0x38);
    const f32 cap = F(0x0040E7B0);
    if (sp < cap) {
        sp = fpu::Add(sp, F(0x0040E7B4));
        GRef<f32>(s + 0x38) = sp;
    }
    if (cap < sp) GRef<f32>(s + 0x38) = cap;
    const f32 damp = F(0x0040E7B8);
    GRef<f32>(s + 0x30) = F(s + 0x30) * damp;
    GRef<f32>(s + 0x34) = F(s + 0x34) * damp;
    sceVu0ApplyMatrix(vel, m, s + 0x30);
}

// EE: 0x001536B8
// Kind 10 step: swept unit hit (as kind 8, plus 0x00219188(0, s)) or terrain hit (impact
// 0x00154428, owner message 0xD); either one ends in impact effect 0xB5 and area damage
// C 0x0040E7C0 kind 4. Still flying it moves and, past +0x6C - C 0x0040E7C8, leaves a trail
// (0x30 in scene type 2, else 0x78).
void func_001536B8(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x1B0);
    const u32 col = fr.Address(0), uhit = fr.Address(0x160), unrm = fr.Address(0x170), hit = fr.Address(0x180),
              nrm = fr.Address(0x190), attr = fr.Address(0x1A0);
    bool hitAny = false;
    const u32 team = W(Unit(GRef<u8>(s + 0x58)) + 0x48) & 3u;
    func_0017C940(col, s + 0x10, s, vel, GRef<u8>(s + 0x58));
    for (s32 n = 0; n < GRef<s32>(W(0x00410640)); ++n) {
        const u32 u = Unit(n);
        if ((~W(u + 0x4C) & 0x10000000u) != 0) continue;
        if (func_0017C9B8(col, u, uhit, unrm, n) != 1) continue;
        CopyQ(s, uhit);
        if (team != (W(u + 0x48) & 3u)) GRef<u32>(u + 0x50) = W(u + 0x50) | 4u;
        GRef<u8>(u + 0x63) = GRef<u8>(s + 0x58);
        CopyQ(u + 0x30, vel);
        hitAny = true;
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        PlayEventSe(0, s);
        break;
    }
    if ((W(s + 0x70) & 1u) == 0 && func_0017CB70(col, hit, nrm, attr) == 1) {
        GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        hitAny = true;
        func_00154428(hit, vel, nrm, GRef<s32>(attr));
        ShowCenterMessage(GRef<u8>(s + 0x58), 0xD, 0);
    }
    if (hitAny) {
        func_001F08E8(s, vel, 0xB5);
        ApplyAreaDamage(F(0x0040E7C0), s, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
    }
    if ((W(s + 0x70) & 1u) != 0) return;
    sceVu0AddVector(s, s, vel);
    if (fpu::Sub(F(s + 0x6C), F(0x0040E7C8)) < fpu::IntToFloat(GRef<s32>(s + 0x50)) * F(0x0040E7C4)) {
        const s32 len = GRef<s8>(Scene() + 0x1653) == 2 ? 0x30 : 0x78;
        GRef<u32>(s + 0x68) = func_001DAB18(W(s + 0x68), s, vel, len, 1);
    }
}

// EE: 0x00153958
// Kind 10 init (id 11): kind 10, life 960, climb time +0x6C = cos(+0x18), local velocity, roll and
// steering angles cleared, no trail. Without a climb (+0x6C <= 0) it picks kind 3 or 10 at once.
void func_00153958(u32 s) {
    GRef<u8>(s + 0x5B) = 10;
    GRef<u32>(s + 0x54) = 0x3C0;
    GRef<f32>(s + 0x6C) = ee::cosf(F(s + 0x18));
    guest::GuestFrame fr(0x80);
    const u32 m = fr.Address(0), inv = fr.Address(0x40);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, s + 0x10);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(s + 0x30, inv, s + 0x20);
    GRef<u32>(s + 0x18) = 0;
    GRef<s32>(s + 0x68) = -1;
    GRef<f32>(s + 0x48) = 0.0f;
    GRef<f32>(s + 0x44) = 0.0f;
    GRef<f32>(s + 0x40) = 0.0f;
    if (F(s + 0x6C) <= 0.0f) GRef<u8>(s + 0x5B) = Kind10Follow(s);
}

// EE: 0x00153A58
// Kind 11 pre-step: gravity C 0x0040E7CC while falling, else velocity scaled by C 0x0040E7D0;
// pitch +0x10 = asin(-vy / |v|), +0x18 = atan2(vx, vz).
// fpucheck: 1 add
void func_00153A58(u32 i) {
    const u32 s = Special(i);
    const u32 vel = s + 0x20;
    if (GRef<u8>(s + 0x5E) == 0)
        GRef<f32>(s + 0x24) = fpu::Add(F(s + 0x24), F(0x0040E7CC));
    else
        sceVu0ScaleVector(vel, vel, F(0x0040E7D0));
    const f32 len = GetVectorLength(vel);
    GRef<f32>(s + 0x10) = ee::asinf(-F(s + 0x24) / len);
    GRef<f32>(s + 0x18) = ee::atan2f(F(s + 0x20), F(s + 0x28));
}

// EE: 0x00153AF8
// Kind 11 step (id 5): while falling, within 512 above the terrain it shows its effect
// 0x001ED2E8 (once); a unit or terrain hit (or that height) stops it at the contact point (state 2,
// 600 frames, velocity = contact - position). After age x C 0x0040E7D4 > 1 in state 2 it detonates
// (state 1, 600 frames, 0x00219188(0x12, s)); while detonated the blast radius +0x64 grows by
// 4 x +0x60 x C 0x0040E7D8 a frame up to +0x60 (then it ends, +0x70 |= 1) with area damage kind 4
// at the next position clamped down to the terrain.
// fpucheck: 2 adds
void func_00153AF8(u32 i) {
    const u32 s = Special(i);
    const u32 rot = s + 0x10;
    const u32 vel = s + 0x20;
    guest::GuestFrame fr(0x40);
    const u32 nx = fr.Address(0), hit = fr.Address(0x10), nrm = fr.Address(0x20), attr = fr.Address(0x30);
    sceVu0AddVector(nx, s, vel);
    const f32 h = func_0013E2B0(Terrain(), nx);
    u8 st = GRef<u8>(s + 0x5E);
    if (st == 0) {
        bool shown = false;
        if (-512.0f < fpu::Sub(F(nx + 4), h)) {
            shown = true;
            func_001ED2E8(F(s + 0x60), nx, rot, vel);
        }
        if (func_001542C0(GRef<u8>(s + 0x5A), hit, nrm, s, rot, vel, GRef<u8>(s + 0x58)) == 1 ||
            func_0013D698(Terrain(), hit, attr, s, nx, nrm) == 1) {
            CopyQ(nx, hit);
            if (!shown) {
                shown = true;
                func_001ED2E8(F(s + 0x60), nx, rot, vel);
            }
        }
        if (shown) {
            GRef<u32>(s + 0x50) = 0;
            GRef<u8>(s + 0x5E) = 2;
            GRef<u32>(s + 0x54) = 600;
            sceVu0SubVector(vel, nx, s);
        }
        st = GRef<u8>(s + 0x5E);
    }
    if (st == 2 && 1.0f < fpu::IntToFloat(GRef<s32>(s + 0x50)) * F(0x0040E7D4)) {
        GRef<u32>(s + 0x50) = 0;
        GRef<u8>(s + 0x5E) = 1;
        GRef<u32>(s + 0x54) = 600;
        PlayEventSe(0x12, s);
        st = GRef<u8>(s + 0x5E);
    }
    if (st == 1) {
        if (h < F(nx + 4)) GRef<f32>(nx + 4) = h;
        const f32 cur = F(s + 0x64), full = F(s + 0x60);
        if (cur < full) {
            GRef<f32>(s + 0x64) = fpu::Add(cur, full * F(0x0040E7D8) * 4.0f);
        } else {
            GRef<f32>(s + 0x64) = full;
            GRef<u32>(s + 0x70) = W(s + 0x70) | 1u;
        }
        ApplyAreaDamage(F(s + 0x64), nx, GRef<u8>(s + 0x58), 4, GRef<u8>(s + 0x5F));
    }
    sceVu0AddVector(s, s, vel);
}

// EE: 0x00153D60
// Kind 11 init (id 5): kind 11, full blast radius 1400, life 1200, state 0, radius 0.
void func_00153D60(u32 s) {
    GRef<u8>(s + 0x5B) = 0xB;
    GRef<f32>(s + 0x60) = 1400.0f;
    GRef<u32>(s + 0x54) = 0x4B0;
    GRef<u8>(s + 0x5E) = 0;
    GRef<u32>(s + 0x64) = 0;
}

// EE: 0x00153D88
// Area damage: every live, damageable (+0x48 bit 0x200000) unit other than the owner, not a
// teammate of a human owner (owner < player count; owner 0xFF counts as team 1), not of type 8/9
// for kind 0x20, and matching the +0x48 bit 0x800000 filter (x == 0: bit clear; else bit set),
// within radius + its own radius (box test, then distance 0x00104F10) gets +0x50 |= kind, the
// attacker id and a unit push direction in +0x30. Returns the number of units hit.
// fpucheck: 1 add
s32 ApplyAreaDamage(f32 radius, u32 pos, u8 owner, s32 kind, s32 x) {
    const u32 team = owner == 0xFF ? 1u : (W(Unit(owner) + 0x48) & 3u);
    const bool inverted = static_cast<u8>(x) != 0;
    s32 hits = 0;
    guest::GuestFrame fr(0x10);
    const u32 d = fr.Address(0);
    for (s32 n = 0; n < UnitCount(); ++n) {
        const u32 u = Unit(n);
        if (static_cast<s32>(owner) == n) continue;
        if ((~W(u + 0x4C) & 0x10000000u) != 0) continue;
        const u32 f48 = W(u + 0x48);
        if ((~f48 & 0x200000u) != 0) continue;
        if (owner < Players() && team == (f48 & 3u)) continue;
        if (kind == 0x20 && static_cast<u32>(GRef<u8>(u + 0x6A)) - 8u < 2u) continue;
        if (((inverted ? ~f48 : f48) & 0x800000u) != 0) continue;
        sceVu0SubVector(d, u, pos);
        const f32 r = fpu::Add(radius, F(u + 0x70));
        const f32 nr = -r;
        if (r < F(d) || F(d) < nr) continue;
        if (r < F(d + 4) || F(d + 4) < nr) continue;
        if (r < F(d + 8) || F(d + 8) < nr) continue;
        const f32 dist = func_00104F10(pos, u);
        if (dist < r) {
            GRef<u8>(u + 0x63) = owner;
            GRef<u32>(u + 0x50) = W(u + 0x50) | static_cast<u32>(kind);
            ++hits;
            sceVu0ScaleVector(u + 0x30, d, 1.0f / dist);
        }
    }
    return hits;
}

// EE: 0x00153FD8
// Ballistic landing point for a drop from u with velocity dir (gravity 24.5 per 60-frame second
// squared, time unit 1/60): returns -1 when it starts below h going up or never comes down to h,
// else 0 with the point in out. First it advances to the top of the arc (t = -vy x 60 / 49), then
// falls from there with vertical speed 0.
// fpucheck: 4 adds
s32 func_00153FD8(f32 h, u32 out, u32 u, u32 dir) {
    guest::GuestFrame fr(0x30);
    const u32 p = fr.Address(0), v = fr.Address(0x10), tmp = fr.Address(0x20);
    CopyQ(p, u);
    CopyQ(v, dir);
    if (h < F(u + 4) && !(F(dir + 4) < 0.0f)) return -1;
    const f32 t = -(F(v + 4) * 60.0f) / 49.0f;
    sceVu0ScaleVector(tmp, v, t * 60.0f);
    sceVu0AddVector(p, p, tmp);
    if (0.0f < t)
        GRef<f32>(p + 4) = fpu::Sub(F(p + 4), t * 24.5f * t);
    else if (t < 0.0f)
        GRef<f32>(p + 4) = fpu::Add(F(p + 4), t * 24.5f * t);
    GRef<u32>(v + 4) = 0;
    if (h < F(p + 4)) return -1;
    const f32 fall = fpu::Sqrt(fpu::Sub(F(p + 4), h) / 24.5f);
    sceVu0ScaleVector(tmp, v, fall * 60.0f);
    sceVu0AddVector(out, p, tmp);
    GRef<f32>(out + 4) = fpu::Add(F(out + 4), fall * 24.5f * fall);
    return 0;
}

// EE: 0x00154198
// Release velocity for weapon type kind (jump table 0x00404840): types 0-3 and 8 add a forward
// push (C 0x0040E7DC / E0 / E4 / E8 along local z) to vel, 8 uses only the push (85, C 0x0040E7EC),
// types 4-7 keep vel; other types return -1 with out unchanged.
// fpucheck: 0 adds (vector adds only)
s32 func_00154198(u32 out, u32 rot, u32 vel, s32 kind) {
    guest::GuestFrame fr(0x50);
    const u32 m = fr.Address(0), push = fr.Address(0x40);
    ZeroQ(push);
    switch (static_cast<u32>(kind)) {
        case 0: GRef<u32>(push + 8) = W(0x0040E7DC); break;
        case 1: GRef<u32>(push + 8) = W(0x0040E7E0); break;
        case 2: GRef<u32>(push + 8) = W(0x0040E7E4); break;
        case 3: GRef<u32>(push + 8) = W(0x0040E7E8); break;
        case 4:
        case 5:
        case 6:
        case 7: CopyQ(out, vel); return 0;
        case 8:
            GRef<u32>(push + 8) = W(0x0040E7EC);
            sceVu0UnitMatrix(m);
            func_00102D28(m, m, rot);
            sceVu0ApplyMatrix(out, m, push);
            return 0;
        default: return -1;
    }
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    sceVu0ApplyMatrix(push, m, push);
    sceVu0AddVector(out, vel, push);
    return 0;
}

// EE: 0x001542C0
// Swept special-weapon hit against units: every live unit other than the owner with +0x48 bit
// 0x400000 is tested (0x0017C9B8, hit point / normal out). A unit without bit 0x40000 hit by type
// 7 bounces: the normal becomes the unit-to-weapon direction and the hit point the weapon
// position. Returns 1 on the first hit, else 0.
s32 func_001542C0(u8 id, u32 hit, u32 nrm, u32 s, u32 rot, u32 vel, u8 owner) {
    guest::GuestFrame fr(0x160);
    const u32 col = fr.Address(0);
    func_0017C940(col, rot, s, vel, owner);
    for (s32 n = 0; n < UnitCount(); ++n) {
        const u32 u = Unit(n);
        if (static_cast<s32>(owner) == n) continue;
        if ((~W(u + 0x4C) & 0x10000000u) != 0) continue;
        if ((~W(u + 0x48) & 0x400000u) != 0) continue;
        if (func_0017C9B8(col, u, hit, nrm, n) != 1) continue;
        if ((~W(u + 0x48) & 0x40000u) != 0 && id == 7) {
            sceVu0SubVector(nrm, hit, s);
            sceVu0Normalize(nrm, nrm);
            CopyQ(hit, s);
        }
        return 1;
    }
    return 0;
}

// EE: 0x00154428
// Special-weapon terrain impact: water (attr 2) splash effect 5 and sound 2, else ground effect 4
// and sound 0 (0x00219188).
void func_00154428(u32 hit, u32 vel, u32 nrm, s32 attr) {
    if (attr == 2) {
        SpawnImpactEffect(hit, vel, nrm, 5);
        PlayEventSe(2, hit);
        return;
    }
    SpawnImpactEffect(hit, vel, nrm, 4);
    PlayEventSe(0, hit);
}

// EE: 0x00154558
// Impact effect by kind (jump table 0x00404880): 0-2 -> 0x00196760 type 0-2, 3 -> 5, 5 -> 3,
// 9 -> 4 (with the normal); 4/6 -> effect 0xB4, 7 -> 0x80, 8/10 -> 0xB6, 11 -> 0x82 (0x001F08E8).
void SpawnImpactEffect(u32 pos, u32 vel, u32 nrm, s32 kind) {
    switch (static_cast<u32>(kind)) {
        case 0: func_00196760(0, pos, vel, nrm); return;
        case 1: func_00196760(1, pos, vel, nrm); return;
        case 2: func_00196760(2, pos, vel, nrm); return;
        case 3: func_00196760(5, pos, vel, nrm); return;
        case 4:
        case 6: func_001F08E8(pos, vel, 0xB4); return;
        case 5: func_00196760(3, pos, vel, nrm); return;
        case 7: func_001F08E8(pos, vel, 0x80); return;
        case 8:
        case 10: func_001F08E8(pos, vel, 0xB6); return;
        case 9: func_00196760(4, pos, vel, nrm); return;
        case 11: func_001F08E8(pos, vel, 0x82); return;
        default: return;
    }
}

}  // namespace ac04::game
