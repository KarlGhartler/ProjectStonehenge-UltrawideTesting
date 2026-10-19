// Effect spawning (slice S5d): the "spawn or refresh" functions of the effect pools
// (docs/subsystems/effects.md "Spawning"). A handle of -1 allocates the lowest free record (the
// pool's allocator, effect_update.cpp) and runs the record init (effect_pools.cpp); a full pool
// returns -1 and nothing is created. An index past the pool (never returned by the allocators)
// selects the pool's last record, as in the original.
#include "game/game.h"

#include <cmath>
#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/flight_common.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Block() { return W(0x004106B4); }
u32 EffectClock() { return W(Block() + 0x6249C); }
u8 MissionId() { return Game(0x27); }

// Record `i` of the pool at block offset `off`, or its last record for an index past the pool.
u32 Rec(u32 off, u32 i, u32 stride, u32 count) {
    return Block() + off + (i < count ? i : count - 1) * stride;
}

void AddScaledXyz(u32 dst, u32 v, f32 k) {
    const f32 x = vu0::Mul(F(v), k), y = vu0::Mul(F(v + 4), k), z = vu0::Mul(F(v + 8), k);
    GRef<f32>(dst) = vu0::Add(F(dst), x);
    GRef<f32>(dst + 4) = vu0::Add(F(dst + 4), y);
    GRef<f32>(dst + 8) = vu0::Add(F(dst + 8), z);
}

void ScaleXyz(u32 dst, f32 k) {
    GRef<f32>(dst) = vu0::Mul(F(dst), k);
    GRef<f32>(dst + 4) = vu0::Mul(F(dst + 4), k);
    GRef<f32>(dst + 8) = vu0::Mul(F(dst + 8), k);
}

}  // namespace

// EE: 0x00181BB0
// Spawn or refresh in pool +0x5EAB0 (32 x 0x40): position, angles +0x10, kind +0x24 (u16), +0x2C.
u32 func_00181BB0(u32 h, u32 pos, u32 rot, s32 kind, s32 idx) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_00184828(Block() + 0x5EAB0);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        func_0017F6D8(Rec(0x5EAB0, i, 0x40, 32));
    }
    const u32 r = Rec(0x5EAB0, i, 0x40, 32);
    CopyQ(r, pos);
    CopyQ(r + 0x10, rot);
    GRef<s32>(r + 0x2C) = idx;
    GRef<u16>(r + 0x24) = static_cast<u16>(kind);
    return i;
}

// EE: 0x001828D0
// New record in pool +0x5D070 (64 x 0x20) at pos.
void func_001828D0(u32 pos) {
    const s32 n = func_00184920(Block() + 0x5D070);
    if (n == -1) return;
    const u32 r = Rec(0x5D070, static_cast<u32>(n), 0x20, 64);
    func_00181CB8(r);
    CopyQ(r, pos);
}

// EE: 0x001836B8
// New record in pool +0x60D00 (16 x 0x30): position (w = 1), velocity (w = 0).
void func_001836B8(u32 pos, u32 vel) {
    const s32 n = func_00184A20(Block() + 0x60D00);
    if (n == -1) return;
    const u32 r = Rec(0x60D00, static_cast<u32>(n), 0x30, 16);
    func_00182948(r);
    CopyQ(r, pos);
    CopyQ(r + 0x10, vel);
    GRef<u32>(r + 0x1C) = 0;
    GRef<f32>(r + 0xC) = 1.0f;
}

// EE: 0x00184638
// Spawn or refresh in pool +0x61630 (16 x 0x30): position (w = 1), velocity (w = 0), +0x24, +0x2C.
u32 func_00184638(u32 h, u32 pos, u32 vel, s32 a, s32 b) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_00184B20(Block() + 0x61630);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        func_00183760(Rec(0x61630, i, 0x30, 16));
    }
    const u32 r = Rec(0x61630, i, 0x30, 16);
    CopyQ(r, pos);
    CopyQ(r + 0x10, vel);
    GRef<s32>(r + 0x2C) = b;
    GRef<f32>(r + 0xC) = 1.0f;
    GRef<s32>(r + 0x24) = a;
    GRef<u32>(r + 0x1C) = 0;
    return i;
}

// EE: 0x00185818
// Spawn or refresh a particle burst (pool +0x4A830, 128 x 0x210; 16 slots, see 0x00184E08): a new
// record starts with slot 0 (velocity x C 0x0040EDC8); every 4th refresh opens the next slot
// (dropping the oldest when the ring is full), and the current slot gets the position and the
// velocity x C 0x0040EDCC.
u32 func_00185818(u32 h, u32 pos, u32 vel) {
    u32 i = h;
    u32 r;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_00186B78(Block() + 0x4A830);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        r = Rec(0x4A830, i, 0x210, 128);
        func_00184DF0(r);
        sceVu0CopyVector(r, pos);
        sceVu0CopyVector(r + 0x100, vel);
        ScaleXyz(r + 0x100, F(0x0040EDC8));
    } else {
        r = Rec(0x4A830, i, 0x210, 128);
    }
    if ((W(r + 0x204) & 3u) == 0) {
        const u16 last = static_cast<u16>(GRef<u16>(r + 0x20A) + 1);
        GRef<u16>(r + 0x20A) = last;
        const u16 first = GRef<u16>(r + 0x208);
        if ((last & 0xF) == (first & 0xF)) GRef<u16>(r + 0x208) = static_cast<u16>(first + 1);
    }
    sceVu0CopyVector(r + (GRef<u16>(r + 0x20A) & 0xFu) * 16, pos);
    sceVu0CopyVector(r + (GRef<u16>(r + 0x20A) & 0xFu) * 16 + 0x100, vel);
    ScaleXyz(r + (GRef<u16>(r + 0x20A) & 0xFu) * 16 + 0x100, F(0x0040EDCC));
    GRef<s32>(r + 0x204) += 1;
    return i;
}

// EE: 0x00186928
// Spawn or refresh in pool +0x604F0 (32 x 0x40): three vectors, +0x34 = 1; a new record also gets
// a random interval 4..35 in +0x38 (1 random read).
// fpucheck: 1 add
u32 func_00186928(u32 h, u32 a, u32 b, u32 c) {
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_00186CA8(Block() + 0x604F0);
        if (n == -1) return 0xFFFFFFFFu;
        const u32 r = Rec(0x604F0, static_cast<u32>(n), 0x40, 32);
        func_001859D8(r);
        CopyQ(r, a);
        CopyQ(r + 0x10, b);
        CopyQ(r + 0x20, c);
        const f32 rnd = NextRandomFloat();
        GRef<s32>(r + 0x34) = 1;
        const s32 v = static_cast<s16>(fpu::FloatToInt(fpu::Add(rnd, 1.0f) * 128.0f));
        GRef<s16>(r + 0x38) = static_cast<s16>(v % 32 + 4);
        return static_cast<u32>(n);
    }
    const u32 r = Rec(0x604F0, h, 0x40, 32);
    CopyQ(r, a);
    CopyQ(r + 0x10, b);
    CopyQ(r + 0x20, c);
    GRef<s32>(r + 0x34) = 1;
    return h;
}

// EE: 0x00188A50
// New record in pool +0x5FAD0 (32 x 0x20) at pos with a random frame (0..66) and a random value
// (2 random reads).
// fpucheck: 1 add
void func_00188A50(u32 pos) {
    const s32 n = func_0019F518(Block() + 0x5FAD0);
    if (n == -1) return;
    const u32 r = Rec(0x5FAD0, static_cast<u32>(n), 0x20, 32);
    func_00186DA0(r);
    CopyQ(r, pos);
    const s32 v = static_cast<s16>(fpu::FloatToInt(fpu::Add(NextRandomFloat(), 1.0f) * 128.0f));
    GRef<s16>(r + 0x18) = static_cast<s16>(v % 0x43);
    GRef<f32>(r + 0x14) = NextRandomFloat();
}

// EE: 0x0018EBE8
// New record in pool +0x61AD0 (8 x 0x20) at pos with kind +0x1A.
void func_0018EBE8(s32 kind, u32 pos) {
    const s32 n = func_0019F810(Block() + 0x61AD0);
    if (n == -1) return;
    const u32 r = Rec(0x61AD0, static_cast<u32>(n), 0x20, 8);
    func_0018AF40(r);
    CopyQ(r, pos);
    GRef<u16>(r + 0x1A) = static_cast<u16>(kind);
}

// EE: 0x00196760
// Impact debris in pool +0x447D0 (256 x 0x30): position (w = 1), the surface normal with its y
// negated and normalised, kind +0x2A. The velocity argument is not used.
void func_00196760(s32 kind, u32 pos, u32 vel, u32 nrm) {
    (void)vel;
    const s32 n = func_0019F910(Block() + 0x447D0);
    if (n == -1) return;
    const u32 r = Rec(0x447D0, static_cast<u32>(n), 0x30, 256);
    func_0018EC70(r);
    CopyQ(r, pos);
    GRef<f32>(r + 0xC) = 1.0f;
    CopyQ(r + 0x10, nrm);
    GRef<f32>(r + 0x14) = -F(r + 0x14);
    sceVu0Normalize(r + 0x10, r + 0x10);
    GRef<u16>(r + 0x2A) = static_cast<u16>(kind);
}

// EE: 0x00196828
// Water splash in the same pool: position (w = 1), velocity, +0x2C / +0x2E, kind 6 for attr 2
// (water) else 7.
void func_00196828(s32 attr, u32 pos, u32 vel, s32 k, s32 n2) {
    const s32 n = func_0019F910(Block() + 0x447D0);
    if (n == -1) return;
    const u32 r = Rec(0x447D0, static_cast<u32>(n), 0x30, 256);
    func_0018EC70(r);
    CopyQ(r, pos);
    GRef<u16>(r + 0x2C) = static_cast<u16>(k);
    GRef<f32>(r + 0xC) = 1.0f;
    GRef<u16>(r + 0x2E) = static_cast<u16>(n2);
    CopyQ(r + 0x10, vel);
    GRef<u16>(r + 0x2A) = attr == 2 ? 6 : 7;
}

// EE: 0x001979F0
// Except in mission 9: new record in pool +0x61DF0 (8 x 0x20) at pos (w = 1) with +0x1C.
void func_001979F0(u32 pos, u32 v) {
    if (MissionId() == 9) return;
    const s32 n = func_0019FA10(Block() + 0x61DF0);
    if (n == -1) return;
    const u32 r = Rec(0x61DF0, static_cast<u32>(n), 0x20, 8);
    func_00196908(r);
    CopyQ(r, pos);
    GRef<u32>(r + 0x1C) = v;
    GRef<f32>(r + 0xC) = 1.0f;
}

// EE: 0x001FC6C0
// Flash in pool +0x22220 (32 x 0x50): position, kind +0x1C (kinds 0-2: 30 frames, scale 1;
// 3-4: 120 frames, scale 512; other kinds keep the record's old values), size +0x18, frame 0,
// random clock stamp +0x40 (1 random read), bytes +0x20..+0x39 cleared. Returns the index.
s32 func_001FC6C0(f32 size, u32 pos, s32 kind) {
    const s32 n = func_001FF510(Block() + 0x22220);
    if (n == -1) return -1;
    const u32 r = Rec(0x22220, static_cast<u32>(n), 0x50, 32);
    CopyQ(r, pos);
    const u16 k = static_cast<u16>(kind);
    GRef<u16>(r + 0x1C) = k;
    if (k < 5) {
        const bool big = k >= 3;
        GRef<u16>(r + 0x12) = big ? 120 : 30;
        GRef<f32>(r + 0x14) = big ? 512.0f : 1.0f;
    }
    GRef<f32>(r + 0x18) = size;
    GRef<u16>(r + 0x10) = 0;
    const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * F(0x0040FEC0));
    GRef<u32>(r + 0x40) = v + EffectClock();
    for (u32 o = 0x39; o >= 0x20; --o) GRef<u8>(r + o) = 0;
    return n;
}

// EE: 0x0020C2A0
// Smoke puff in pool +0x28400 (256 x 0x30) with a random variant 0..3 (1 random read). Returns the
// index.
s32 func_0020C2A0(f32 k, u32 pos, u32 vel, s32 kind) {
    const s32 n = func_00210C88(Block() + 0x28400);
    if (n == -1) return -1;
    const u32 r = Rec(0x28400, static_cast<u32>(n), 0x30, 256);
    func_0020C378(r);
    const s32 var = fpu::FloatToInt(NextRandomFloat() * 4.0f);
    func_00210DB8(k, r, pos, vel, var, kind);
    return n;
}

// EE: 0x00210DB8
// Smoke puff record: position, velocity, size +0x24, variant +0x28, kind +0x2C; kind 1 starts
// C 0x0040FF70 of a velocity step ahead.
void func_00210DB8(f32 k, u32 r, u32 pos, u32 vel, s32 var, s32 kind) {
    CopyQ(r, pos);
    CopyQ(r + 0x10, vel);
    GRef<f32>(r + 0x24) = k;
    GRef<s32>(r + 0x28) = var;
    GRef<s32>(r + 0x2C) = kind;
    if (kind == 1) AddScaledXyz(r, r + 0x10, F(0x0040FF70));
}

// EE: 0x00210E08
// Refreshes a burning wreck (pool +0x274F0 record, see 0x0020A4B8): pos (its w set to 1 in the
// caller's vector) goes into the 10-point ring (a new point while it holds fewer than 2 or every
// 12th frame, else it replaces the newest), velocity +0xC0, life min(n, 120), size 32, speed
// |velocity| +0xE0, refresh count +0xD4.
// fpucheck: 2 adds (madda / madd)
void func_00210E08(u32 r, u32 pos, u32 vel, s32 n) {
    const s32 tail = GRef<s32>(r + 0xA8), head = GRef<s32>(r + 0xA4);
    GRef<f32>(pos + 0xC) = 1.0f;
    const s32 cnt = (tail - head + 10) % 10;
    if (cnt < 2 || GRef<s32>(r + 0xD0) % 12 == 0) {
        CopyQ(r + static_cast<u32>(tail) * 16, pos);
        const s32 t = (GRef<s32>(r + 0xA8) + 1) % 10;
        GRef<s32>(r + 0xA8) = t;
        if (t == GRef<s32>(r + 0xA4)) GRef<s32>(r + 0xA4) = (t + 1) % 10;
    } else {
        CopyQ(r + static_cast<u32>((head + cnt + 9) % 10) * 16, pos);
    }
    CopyQ(r + 0xC0, vel);
    const f32 x = F(r + 0xC0), y = F(r + 0xC4), z = F(r + 0xC8);
    const f32 len = fpu::Sqrt(fpu::MulAdd(fpu::MulAdd(x * x, y, y), z, z));
    GRef<f32>(r + 0xDC) = 32.0f;
    GRef<f32>(r + 0xE0) = len;
    GRef<s32>(r + 0xD8) = n < 0x78 ? n : 0x78;
    GRef<s32>(r + 0xD4) += 1;
}

// EE: 0x00199688
// Mission 0xD, every 44th scene frame (unsigned): a record of pool +0x39F40 (512 x 0x30) with
// position and velocity; its group id +0x28 is `id`, or for -1 the next of the 64 rotating ids
// 0x00410BA8. Returns the group id, `id` when nothing is spawned, -1 when the pool is full.
s32 func_00199688(s32 id, u32 pos, u32 unused, u32 vel) {
    (void)unused;
    if (W(Scene() + 0x1660) % 0x2Cu != 0) return id;
    if (MissionId() != 0xD) return id;
    const s32 n = func_0019FB10(Block() + 0x39F40);
    if (n == -1) return -1;
    const u32 r = Rec(0x39F40, static_cast<u32>(n), 0x30, 512);
    CopyQ(r, pos);
    CopyQ(r + 0x10, vel);
    func_00197A98(r);
    if (id == -1) {
        const u32 g = W(0x00410BA8);
        GRef<u32>(r + 0x28) = g;
        GRef<u32>(0x00410BA8) = (g + 1) & 0x3F;
    } else {
        GRef<s32>(r + 0x28) = id;
    }
    return GRef<s32>(r + 0x28);
}

// EE: 0x0019D7E0
// Spawn or refresh the single active record of pool +0x5FEE0 (32 x 0x30; a new one first resets
// the whole pool, so it is always record 0): position, velocity with vy + C 0x0040F35C (refresh:
// C 0x0040F360), speed |v| +0x24, +0x2C.
// fpucheck: 6 adds
u32 func_0019D7E0(u32 h, u32 pos, u32 vel, u32 v, s32 k) {
    const auto len = [v] {
        const f32 x = F(v), y = F(v + 4), z = F(v + 8);
        return fpu::Sqrt(fpu::Add(fpu::Add(x * x, y * y), z * z));
    };
    if (h == 0xFFFFFFFFu) {
        func_00156758(Block() + 0x5FEE0);
        const s32 n = func_0019FD98(Block() + 0x5FEE0);
        if (n == -1) return 0xFFFFFFFFu;
        const u32 r = Rec(0x5FEE0, static_cast<u32>(n), 0x30, 32);
        sceVu0CopyVector(r, pos);
        sceVu0CopyVector(r + 0x10, vel);
        GRef<f32>(r + 0x14) = fpu::Add(F(r + 0x14), F(0x0040F35C));
        GRef<f32>(r + 0x24) = len();
        func_0019C888(r);
        GRef<s32>(r + 0x2C) = k;
        return static_cast<u32>(n);
    }
    const u32 r = Rec(0x5FEE0, h, 0x30, 32);
    sceVu0CopyVector(r, pos);
    sceVu0CopyVector(r + 0x10, vel);
    GRef<f32>(r + 0x14) = fpu::Add(F(r + 0x14), F(0x0040F360));
    GRef<s32>(r + 0x2C) = k;
    GRef<f32>(r + 0x24) = len();
    return h;
}

// EE: 0x0019F230
// Periodic emitter by kind (period in scene frames, unsigned: 0x60 -> 12, 0xDF -> 10, 0 and 0xE7
// -> 18, 0xD4 -> 70, 0xD0 / 0xD3 / 0xD5 / 0xE0 / 0xE1 / 0xE6 -> 60, others 80): a record of pool
// +0x3FF90 (384 x 0x30) with position, velocity, the length of `dir` (dir / normalised dir on x, or
// z when x is 0; nothing for a vertical or zero dir), kind +0x2E and its own index +0x28, then the
// kind's init 0x0019D9C0.
void func_0019F230(u32 pos, u32 vel, u32 dir, s32 kind) {
    u32 period;
    switch (kind) {
        case 0x60: period = 12; break;
        case 0xDF: period = 10; break;
        case 0x00:
        case 0xE7: period = 18; break;
        case 0xD4: period = 70; break;
        case 0xD0:
        case 0xD3:
        case 0xD5:
        case 0xE0:
        case 0xE1:
        case 0xE6: period = 60; break;
        default: period = 80; break;
    }
    if (W(Scene() + 0x1660) % period != 0) return;
    guest::GuestFrame fr(0x10);
    const u32 nd = fr.Address(0);
    sceVu0Normalize(nd, dir);
    f32 len;
    if (F(nd) == 0.0f) {
        if (F(nd + 8) == 0.0f) return;
        len = F(dir + 8) / F(nd + 8);
    } else {
        len = F(dir) / F(nd);
    }
    const s32 n = func_0019FE90(Block() + 0x3FF90);
    if (n == -1) return;
    const u32 r = Rec(0x3FF90, static_cast<u32>(n), 0x30, 384);
    sceVu0CopyVector(r, pos);
    sceVu0CopyVector(r + 0x10, vel);
    GRef<f32>(r + 0x24) = len;
    GRef<u16>(r + 0x2E) = static_cast<u16>(kind);
    GRef<s32>(r + 0x28) = n;
    func_0019D9C0(r);
}

// EE: 0x0018AC00
// Spawn or refresh a smoke trail head (pool +0x61010, 16 x 0x40): position (w = 1), the reversed
// unit direction +0x20 (inline vector normalise, w = 0), size 48; a new head stamps its start
// frame in 0x00420750[index] and keeps its index in +0x3E. Every 4th scene frame it also drops a
// puff into pool +0x47800 (256 x 0x30): position, direction, owner index +0x2E, start frame +0x28,
// and 3 x the direction's length (direction / unit direction on the first non-zero axis, 1 if 0).
u32 func_0018AC00(u32 h, u32 pos, u32 dir) {
    u32 i = h;
    u32 r;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_0019F610(Block() + 0x61010);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        r = Rec(0x61010, i, 0x40, 16);
        func_00188B18(r);
    } else {
        r = Rec(0x61010, i, 0x40, 16);
    }
    CopyQ(r, pos);
    GRef<f32>(r + 0xC) = 1.0f;
    CopyQ(r + 0x20, dir);
    GRef<f32>(r + 0x2C) = 1.0f;
    sceVu0Normalize(r + 0x20, r + 0x20);
    ScaleXyz(r + 0x20, -1.0f);
    if (h == 0xFFFFFFFFu) {
        GRef<f32>(r + 0x34) = 48.0f;
        GRef<u32>(0x00420750 + 4 * i) = W(Scene() + 0x1660);
        GRef<u16>(r + 0x3E) = static_cast<u16>(i);
    } else {
        GRef<f32>(r + 0x34) = 48.0f;
    }
    if ((W(Scene() + 0x1660) & 3u) != 0) return i;
    const s32 n = func_0019F710(Block() + 0x47800);
    if (n == -1) return i;
    const u32 q = Rec(0x47800, static_cast<u32>(n), 0x30, 256);
    CopyQ(q, pos);
    CopyQ(q + 0x10, dir);
    func_00189B88(q);
    GRef<u16>(q + 0x2E) = static_cast<u16>(i);
    GRef<u32>(q + 0x28) = W(0x00420750 + 4 * i);
    guest::GuestFrame fr(0x10);
    const u32 nd = fr.Address(0);
    sceVu0Normalize(nd, dir);
    if (F(nd) != 0.0f)
        GRef<f32>(q + 0xC) = F(dir) / F(nd);
    else if (F(nd + 4) != 0.0f)
        GRef<f32>(q + 0xC) = F(dir + 4) / F(nd + 4);
    else if (F(nd + 8) != 0.0f)
        GRef<f32>(q + 0xC) = F(dir + 8) / F(nd + 8);
    else
        GRef<f32>(q + 0xC) = 1.0f;
    GRef<f32>(q + 0xC) = F(q + 0xC) * 3.0f;
    return i;
}

// EE: 0x0019C278
// Wake / spray emitter (not in missions 0x14 and 0x19) for a mover with speed L = |dir| (first
// non-zero axis of dir / unit dir; nothing below 1) at height -h above the surface. Mode 0 in
// missions 3 and 0xD (before the mission end flag) emits every max(220 / L, 2) frames; mode 1 (not
// in missions 9 / 0xB unless the end flag is set) every 3, 12 or 16 frames by speed (> 8, > 5,
// else); other modes every frame. It emits only within a height band: 1 < -h < C 0x0040F2F8 outside
// missions 3 / 0xD, < C 0x0040F2FC in 0xD, < C 0x0040F300 in 3. The record (pool +0x5B050, 128 x
// 0x40) gets position, velocity, its index +0x38, a random offset and lifetime (mode 0 in missions
// 3 / 0xD: 4 random reads, lifetime scaled by speed; mode 1: 3 random reads, +0x34 = 1), height
// +0x24 (at least 40), y + (-h) - 2, horizontal / total speed +0x2C and L +0x28.
// fpucheck: 22 adds
void func_0019C278(f32 h, u32 pos, u32 vel, u32 dir, s32 mode) {
    const u8 m0 = MissionId();
    if (m0 == 0x14 || m0 == 0x19) return;
    guest::GuestFrame fr(0x20);
    const u32 nd = fr.Address(0), hz = fr.Address(0x10);
    sceVu0Normalize(nd, dir);
    f32 num, den;
    if (F(nd) != 0.0f) {
        num = F(dir);
        den = F(nd);
    } else if (F(nd + 4) != 0.0f) {
        num = F(dir + 4);
        den = F(nd + 4);
    } else {
        if (F(nd + 8) == 0.0f) return;
        num = F(dir + 8);
        den = F(nd + 8);
    }
    const f32 L = num / den;
    if (L < 1.0f) return;
    GRef<f32>(hz + 8) = F(dir + 8);
    GRef<f32>(hz) = F(dir);
    GRef<f32>(hz + 0xC) = 1.0f;
    GRef<u32>(hz + 4) = 0;
    sceVu0Normalize(nd, hz);
    const f32 hl = F(nd) == 0.0f ? fpu::Div(F(hz + 8), F(nd + 8)) : fpu::Div(F(hz), F(nd));
    const f32 ratio = hl / L;
    const u32 frame = W(Scene() + 0x1660);
    const u8 m = MissionId();
    if (mode == 0 && (m == 3 || m == 0xD)) {
        if ((StatusFlags() & 0x10000000u) != 0) return;
        s32 p = fpu::FloatToInt(220.0f / L);
        if (!(1 < p)) p = 2;
        if (frame % static_cast<u32>(p) != 0) return;
    } else if (mode == 1) {
        if ((StatusFlags() & 0x10000000u) == 0 && (m == 9 || m == 0xB)) return;
        if (8.0f < L && frame % 3u != 0) return;
        if (L <= 8.0f && 5.0f < L && frame % 12u != 0) return;
        if (L <= 5.0f && (frame & 0xFu) != 0) return;
    }
    const f32 hh = -h;
    bool ok = false;
    if (1.0f < hh && hh < F(0x0040F2F8) && m != 3 && m != 0xD) ok = true;
    if (!ok && 1.0f < hh && hh < F(0x0040F2FC) && m == 0xD) ok = true;
    if (!ok && 1.0f < hh && hh < F(0x0040F300) && m == 3) ok = true;
    if (!ok) return;
    const s32 n = func_0019FC10(Block() + 0x5B050);
    if (n == -1) return;
    const u32 r = Rec(0x5B050, static_cast<u32>(n), 0x40, 128);
    sceVu0CopyVector(r, pos);
    sceVu0CopyVector(r + 0x10, vel);
    func_00199788(r);
    GRef<s32>(r + 0x38) = n;
    if (mode == 0 && (m == 3 || m == 0xD)) {
        GRef<f32>(r) = fpu::Add(F(r), fpu::Sub(0.5f, NextRandomFloat()) * 64.0f);
        GRef<f32>(r + 4) = fpu::Sub(F(r + 4), NextRandomFloat() * 16.0f);
        GRef<f32>(r + 8) = fpu::Add(F(r + 8), fpu::Sub(0.5f, NextRandomFloat()) * 64.0f);
        if (L < 32.0f) {
            const f32 base = fpu::Add(fpu::Sub(0.5f, NextRandomFloat()) * 60.0f * 0.25f, 60.0f);
            GRef<f32>(r + 0x1C) = fpu::Sub(2.0f, L * 0.03125f) * base;
        } else if (72.0f <= L) {
            const f32 rnd = NextRandomFloat();
            const f32 k = fpu::Sub(L, 72.0f) / 72.0f;
            const f32 base = fpu::Add(fpu::Sub(0.5f, rnd) * 60.0f * 0.25f, 60.0f);
            GRef<f32>(r + 0x1C) = fpu::Sub(1.0f, k) * base;
        } else {
            GRef<f32>(r + 0x1C) = fpu::Add(fpu::Sub(0.5f, NextRandomFloat()) * 60.0f * 0.25f, 60.0f);
        }
        GRef<u32>(r + 0x34) = 0;
    } else if (mode == 1) {
        GRef<f32>(r) = fpu::Add(F(r), fpu::Sub(0.5f, NextRandomFloat()) * 8.0f);
        GRef<f32>(r + 8) = fpu::Add(F(r + 8), fpu::Sub(0.5f, NextRandomFloat()) * 8.0f);
        const f32 rnd = NextRandomFloat();
        GRef<s32>(r + 0x34) = mode;
        GRef<f32>(r + 0x1C) = fpu::Add(fpu::Sub(0.5f, rnd) * 42.0f * F(0x0040F304), 42.0f);
    }
    GRef<f32>(r + 0x24) = hh;
    GRef<f32>(r + 4) = fpu::Sub(fpu::Add(F(r + 4), hh), 2.0f);
    if (hh < 40.0f) GRef<f32>(r + 0x24) = 40.0f;
    GRef<f32>(r + 0x2C) = ratio;
    GRef<f32>(r + 0x28) = L;
}

// EE: 0x001BFEF0
// New record in pool +0x5F2C0 (32 x 0x40): position 32 lower, the horizontal unit direction +0x20
// (w = 0), up vector +0x10 = (0, 1, 0, 1), a random value (rand() pair mean) +0x34.
// fpucheck: 1 add
void func_001BFEF0(u32 pos, u32 dir) {
    const s32 n = func_001D7FA0(Block() + 0x5F2C0);
    if (n == -1) return;
    const u32 r = Rec(0x5F2C0, static_cast<u32>(n), 0x40, 32);
    func_001BE778(r);
    CopyQ(r, pos);
    GRef<f32>(r + 4) = fpu::Sub(F(r + 4), 32.0f);
    CopyQ(r + 0x20, dir);
    GRef<u32>(r + 0x24) = 0;
    GRef<f32>(r + 0x2C) = 1.0f;
    sceVu0Normalize(r + 0x20, r + 0x20);
    GRef<u32>(r + 0x10) = 0;
    GRef<u32>(r + 0x18) = 0;
    GRef<f32>(r + 0x1C) = 1.0f;
    GRef<f32>(r + 0x14) = 1.0f;
    const s32 a = ee::rand();
    const s32 b = ee::rand();
    GRef<s16>(r + 0x34) = static_cast<s16>((a + b) / 2);
}

// EE: 0x001CD360
// New record in pool +0x5D880 (64 x 0x30): position (w = 1) and velocity.
void func_001CD360(u32 pos, u32 vel) {
    const s32 n = func_001D8198(Block() + 0x5D880);
    if (n == -1) return;
    const u32 r = Rec(0x5D880, static_cast<u32>(n), 0x30, 64);
    func_001C9838(r);
    CopyQ(r, pos);
    GRef<f32>(r + 0xC) = 1.0f;
    CopyQ(r + 0x10, vel);
}

// EE: 0x001CF2E8
// Unless v.x is 0: new record in pool +0x61940 (8 x 0x30) with position and v (w = 0).
void func_001CF2E8(u32 pos, u32 v) {
    if (F(v) == 0.0f) return;
    const s32 n = func_001D8298(Block() + 0x61940);
    if (n == -1) return;
    const u32 r = Rec(0x61940, static_cast<u32>(n), 0x30, 8);
    func_001CD400(r);
    CopyQ(r, pos);
    CopyQ(r + 0x10, v);
    GRef<u32>(r + 0x1C) = 0;
}

// EE: 0x001D32A0
// New record in pool +0x5E490 (32 x 0x20) at pos.
void func_001D32A0(u32 pos) {
    const s32 n = func_001D8490(Block() + 0x5E490);
    if (n == -1) return;
    const u32 r = Rec(0x5E490, static_cast<u32>(n), 0x20, 32);
    func_001D0B40(r);
    CopyQ(r, pos);
}


namespace {

// vrnext: advances the VU0 R register and returns it as a float in [1, 2).
f32 RNextValue() {
    vu0::RNext();
    f32 f;
    const u32 b = vu0::RRegister();
    std::memcpy(&f, &b, 4);
    return f;
}

// Releases a particle id of the shared particle pool (ids >= 0x1000, unsigned, are ignored).
void FreeParticle(s16 id) {
    if (static_cast<u32>(static_cast<s32>(id)) >= 0x1000) return;
    const u32 mask = Block() + 0x22010;
    const u32 word = mask + (static_cast<u32>(static_cast<s32>(id)) >> 5) * 4;
    GRef<u32>(word) = W(word) & ~(1u << (id & 0x1F));
    GRef<s32>(mask + 0x200) -= 1;
}

// Particle record of an id (ids >= 0x1000, unsigned, use the pool's last record).
u32 Particle(s16 id) {
    const u32 v = static_cast<u32>(static_cast<s32>(id));
    return Block() + 0x2010 + (v < 0x1000 ? v << 5 : 0x1FFE0u);
}

// Shared tail of the trail steps 0x001DAC38 and 0x00200D58 (record: ring of particle ids +0x10
// s16[0x6A], head +0xE8, tail +0xEC, frame +0xF4, length +0xF8; the new particle's position and
// velocity at p0 / p1).
void TrailStep(u32 r, u32 sp0, u32 sp1) {
    const u32 ring = r + 0x10;
    const auto head = [r] { return GRef<s32>(r + 0xE8); };
    const auto tail = [r] { return GRef<s32>(r + 0xEC); };
    s32 n = (tail() - head() + 0x6A) % 0x6A;
    if (n - 1 < 0)
        StoreVf0(r);
    else
        CopyQ(r, Particle(GRef<s16>(ring + 2u * static_cast<u32>((head() + n + 0x69) % 0x6A))));
    n = (tail() - head() + 0x6A) % 0x6A;
    if (n >= 2 && (W(r + 0xF4) & 1u) != 0) {
        const u32 q = Particle(GRef<s16>(ring + 2u * static_cast<u32>((head() + n + 0x69) % 0x6A)));
        CopyQ(q, sp0);
        CopyQ(q + 0x10, sp1);
        GRef<s32>(r + 0xF4) += 1;
        return;
    }
    const s32 pid = func_001E0700(Block() + 0x2010);
    if (pid == -1) return;
    const u32 q = Particle(static_cast<s16>(pid));
    CopyQ(q, sp0);
    CopyQ(q + 0x10, sp1);
    const s32 t0 = tail();
    GRef<s16>(ring + 2u * static_cast<u32>(t0)) = static_cast<s16>(pid);
    const s32 t1 = (t0 + 1) % 0x6A;
    GRef<s32>(r + 0xEC) = t1;
    if (t1 == head()) {
        GRef<s32>(r + 0xE8) = (t1 + 1) % 0x6A;
        FreeParticle(GRef<s16>(ring + 2u * static_cast<u32>(t1)));
    }
    if ((tail() - head() + 0x6A) % 0x6A >= GRef<s32>(r + 0xF8) / 2 && head() != tail()) {
        FreeParticle(GRef<s16>(ring + 2u * static_cast<u32>(head())));
        const s32 h = head();
        if (h != tail()) GRef<s32>(r + 0xE8) = (h + 1) % 0x6A;
    }
    GRef<s32>(r + 0xF4) += 1;
}

}  // namespace

// EE: 0x001DAC38
// Missile trail step (record of pool +0x00: ring of particle ids +0x10 s16[0x6A], head +0xE8,
// tail +0xEC, frame +0xF4, length +0xF8). In the scratchpad: the point 8 units behind pos along
// the velocity (0x70000000) and a particle velocity (0x70000010) = a jitter vector from 4 VU0 R
// values (scaled by 2 x r x C 0x0040FD98 x C 0x0040FD9C, offset by -r x C 0x0040FD98 x
// C 0x0040FD9C; 1 random read) crossed with vel, plus the back offset x C 0x0040FDA0, w =
// C 0x0040FDA4. The record +0x00 becomes the newest particle position (or (0,0,0,1)). Every other
// frame, or while the ring holds fewer than 2, a new particle (0x001E0700) is pushed (the oldest
// freed when the ring wraps) and, once the ring reaches half the length, the oldest is freed; the
// other frames overwrite the newest particle. A full particle pool skips the frame entirely.
// fpucheck: 1 add
void func_001DAC38(u32 r, u32 pos, u32 vel, s32 len, s32 k) {
    (void)len;
    (void)k;
    const u32 sp0 = 0x70000000, sp1 = 0x70000010, sp2 = 0x70000020;
    {
        const f32 x = F(vel), y = F(vel + 4), z = F(vel + 8), w = F(vel + 12);
        const f32 s = vu0::Add(vu0::Add(vu0::Mul(x, x), vu0::Mul(y, y)), vu0::Mul(z, z));
        const f32 q = vu0::Div(1.0f, vu0::Sqrt(s));  // vrsqrt
        const f32 ww = vu0::Add(0.0f, w);
        GRef<f32>(sp2) = vu0::Mul(vu0::Mul(x, q), -8.0f);
        GRef<f32>(sp2 + 4) = vu0::Mul(vu0::Mul(y, q), -8.0f);
        GRef<f32>(sp2 + 8) = vu0::Mul(vu0::Mul(z, q), -8.0f);
        GRef<f32>(sp2 + 12) = vu0::Mul(ww, -8.0f);
    }
    sceVu0AddVector(sp0, pos, sp2);
    {
        const f32 rnd = NextRandomFloat();
        const f32 c = rnd * F(0x0040FD98);
        const f32 a = fpu::Add(c * F(0x0040FD9C), c * F(0x0040FD9C));
        const f32 b = -c * F(0x0040FD9C);
        const f32 r0 = RNextValue(), r1 = RNextValue(), r2 = RNextValue(), r3 = RNextValue();
        GRef<f32>(sp1) = vu0::Add(vu0::Mul(vu0::Sub(r0, 1.0f), a), b);
        GRef<f32>(sp1 + 4) = vu0::Add(vu0::Mul(vu0::Sub(r1, 1.0f), a), b);
        GRef<f32>(sp1 + 8) = vu0::Add(vu0::Mul(vu0::Sub(r2, 1.0f), a), b);
        GRef<f32>(sp1 + 12) = vu0::Sub(r3, 1.0f);
    }
    sceVu0OuterProduct(sp1, sp1, vel);
    {
        const f32 kk = F(0x0040FDA0);
        const f32 x = vu0::Add(F(sp1), vu0::Mul(F(sp2), kk));
        const f32 y = vu0::Add(F(sp1 + 4), vu0::Mul(F(sp2 + 4), kk));
        const f32 z = vu0::Add(F(sp1 + 8), vu0::Mul(F(sp2 + 8), kk));
        GRef<f32>(sp1) = x;
        GRef<f32>(sp1 + 4) = y;
        GRef<f32>(sp1 + 8) = z;
    }
    GRef<u32>(sp1 + 12) = W(0x0040FDA4);
    TrailStep(r, sp0, sp1);
}

// EE: 0x001DAB18
// Spawn or refresh a missile trail (pool +0x00, 32 x 0x100): a handle of -1 or >= 32 (signed)
// allocates (length min(len, 210) +0xF8, kind +0xFC); a handle whose record is no longer live
// returns -1. Then one trail step 0x001DAC38.
u32 func_001DAB18(u32 h, u32 pos, u32 vel, s32 len, s32 k) {
    const s16 kind = static_cast<s16>(k);
    u32 i = h;
    u32 r;
    if (static_cast<s32>(h) == -1 || !(static_cast<s32>(h) < 0x20)) {
        const s32 n = func_001E0608(Block());
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        r = Rec(0, i, 0x100, 32);
        func_001DB0A8(r);
        GRef<s16>(r + 0xFC) = kind;
        GRef<s32>(r + 0xF8) = len < 0xD2 ? len : 0xD2;
    } else {
        if (((W(Block() + 0x2000 + (h >> 5) * 4) >> (h & 0x1F)) & 1u) == 0) return 0xFFFFFFFFu;
        r = Rec(0, i, 0x100, 32);
    }
    func_001DAC38(r, pos, vel, len, kind);
    return i;
}

// EE: 0x001DD9D8
// Spawn or refresh a smoke trail (pool +0x2E4F0, 32 x 0x110), then 0x001DDAC0.
u32 func_001DD9D8(u32 h, u32 pos, u32 vel) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_001E0828(Block() + 0x2E4F0);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        func_001DDBD0(Rec(0x2E4F0, i, 0x110, 32));
    }
    func_001DDAC0(Rec(0x2E4F0, i, 0x110, 32), pos, vel);
    return i;
}

// EE: 0x001DDAC0
// Smoke trail refresh: current position +0xD0 and velocity +0xE0; during frames +0x100 ..
// +0x100 + +0x104 the position goes into the 12-point ring (+0x00, head +0xC4, tail +0xC8): a new
// point on even frames, else it replaces the newest. Frame +0xF4 + 1.
void func_001DDAC0(u32 r, u32 pos, u32 vel) {
    CopyQ(r + 0xD0, pos);
    CopyQ(r + 0xE0, vel);
    const s32 f = GRef<s32>(r + 0xF4);
    const s32 start = GRef<s32>(r + 0x100);
    if (start <= f && f < start + GRef<s32>(r + 0x104)) {
        if ((f & 1) != 0) {
            const s32 head = GRef<s32>(r + 0xC4);
            const s32 slot = (head + ((GRef<s32>(r + 0xC8) - head + 12) % 12 + 11)) % 12;
            CopyQ(r + static_cast<u32>(slot) * 16, pos);
        } else {
            CopyQ(r + W(r + 0xC8) * 16, pos);
            const s32 t = (GRef<s32>(r + 0xC8) + 1) % 12;
            GRef<s32>(r + 0xC8) = t;
            if (t == GRef<s32>(r + 0xC4)) GRef<s32>(r + 0xC4) = (t + 1) % 12;
        }
    }
    GRef<s32>(r + 0xF4) = GRef<s32>(r + 0xF4) + 1;
}

// EE: 0x001DF858
// Spawn or refresh in pool +0x38B30 (64 x 0x50), then 0x001DF930.
u32 func_001DF858(u32 h, u32 pos) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_001E0920(Block() + 0x38B30);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        func_001DF9A0(Rec(0x38B30, i, 0x50, 64));
    }
    func_001DF930(Rec(0x38B30, i, 0x50, 64), pos);
    return i;
}

// EE: 0x001DF930
// Pushes pos into the 3-point ring (+0x00, head +0x34, tail +0x38) and counts the refresh +0x44.
void func_001DF930(u32 r, u32 pos) {
    CopyQ(r + W(r + 0x38) * 16, pos);
    const s32 t = (GRef<s32>(r + 0x38) + 1) % 3;
    GRef<s32>(r + 0x38) = t;
    if (t == GRef<s32>(r + 0x34)) GRef<s32>(r + 0x34) = (t + 1) % 3;
    GRef<s32>(r + 0x44) += 1;
}

// EE: 0x001E43D8
// Spawn or refresh a flash in pool +0x22C30 (8 x 0x40), then 0x001E44E8 (`k` is passed on but
// not used).
u32 func_001E43D8(f32 level, u32 h, u32 pos, u32 rot, u8 b, s32 k) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_001E92C0(Block() + 0x22C30);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        func_001E4588(Rec(0x22C30, i, 0x40, 8));
    }
    func_001E44E8(level, Rec(0x22C30, i, 0x40, 8), pos, rot, b);
    (void)k;
    return i;
}

// EE: 0x001E44E8
// Flash record: position moved C 0x0040FDE8 forward along its angles, angles +0x10, level +0x20,
// +0x38 = b, refresh flag +0x39 = 1.
void func_001E44E8(f32 level, u32 r, u32 pos, u32 rot, u8 b) {
    CopyQ(r, pos);
    CopyQ(r + 0x10, rot);
    GRef<f32>(r + 0x20) = level;
    GRef<u8>(r + 0x38) = b;
    GRef<u8>(r + 0x39) = 1;
    guest::GuestFrame fr(0x50);
    const u32 m = fr.Address(0), v = fr.Address(0x40);
    SetUnitMatrixVu0(m);
    func_00102D28(m, m, rot);
    StoreVf0(v);
    GRef<u32>(v + 8) = W(0x0040FDE8);
    sceVu0ApplyMatrix(v, m, v);
    GRef<f32>(r) = vu0::Add(F(r), F(v));
    GRef<f32>(r + 4) = vu0::Add(F(r + 4), F(v + 4));
    GRef<f32>(r + 8) = vu0::Add(F(r + 8), F(v + 8));
}

// EE: 0x001E7CF8
// New debris record in pool +0x22E40 (64 x 0x60), set up by 0x001E7DC0. Returns 0, or -1 when the
// pool is full.
s32 func_001E7CF8(f32 size, u32 pos, u32 rot, u32 vel, u8 b) {
    const s32 n = func_001E93C0(Block() + 0x22E40);
    if (n == -1) return -1;
    const u32 r = Rec(0x22E40, static_cast<u32>(n), 0x60, 64);
    func_001E7FF0(r);
    func_001E7DC0(size, r, pos, rot, vel, b);
    return 0;
}

// EE: 0x001E7DC0
// Debris record: angles +0x10, velocity +0x20, size +0x40, +0x52 = b. On even effect frames the
// side offsets +0x48 / +0x4C drift toward a random value in +-size/2 (2 random reads). The position
// is pos plus (offsets, 10 - 1.25 x (clock & 7)) turned by the angles, minus the velocity; the
// velocity gets a random sideways push of up to +-5 (2 random reads); +0x30 = (0, 0, -16) turned;
// +0x44 = 2.
// fpucheck: 10 adds
void func_001E7DC0(f32 size, u32 r, u32 pos, u32 rot, u32 vel, u8 b) {
    CopyQ(r, pos);
    CopyQ(r + 0x10, rot);
    CopyQ(r + 0x20, vel);
    GRef<f32>(r + 0x40) = size;
    GRef<u8>(r + 0x52) = b;
    guest::GuestFrame fr(0x50);
    const u32 m = fr.Address(0), v = fr.Address(0x40);
    SetUnitMatrixVu0(m);
    func_00102D28(m, m, r + 0x10);
    StoreVf0(v);
    if ((EffectClock() & 1u) == 0) {
        f32 d = fpu::Sub(NextRandomFloat(), 0.5f) * F(r + 0x40);
        GRef<f32>(r + 0x48) = fpu::Add(F(r + 0x48), fpu::Add(d, d)) * 0.5f;
        d = fpu::Sub(NextRandomFloat(), 0.5f) * F(r + 0x40);
        GRef<f32>(r + 0x4C) = fpu::Add(F(r + 0x4C), fpu::Add(d, d)) * 0.5f;
    }
    GRef<u32>(v) = W(r + 0x48);
    GRef<u32>(v + 4) = W(r + 0x4C);
    const f32 c = -fpu::IntToFloat(static_cast<s32>(EffectClock() & 7u));
    GRef<f32>(v + 8) = fpu::Add(c * 0.125f * 10.0f, 10.0f);
    sceVu0ApplyMatrix(v, m, v);
    for (u32 o = 0; o < 12; o += 4) GRef<f32>(r + o) = vu0::Add(F(r + o), F(v + o));
    for (u32 o = 0; o < 12; o += 4) GRef<f32>(r + o) = vu0::Sub(F(r + o), F(vel + o));
    StoreVf0(v);
    GRef<f32>(v) = fpu::Sub(NextRandomFloat(), 0.5f) * 10.0f;
    GRef<f32>(v + 4) = fpu::Sub(NextRandomFloat(), 0.5f) * 10.0f;
    sceVu0ApplyMatrix(v, m, v);
    for (u32 o = 0; o < 12; o += 4) GRef<f32>(r + 0x20 + o) = vu0::Add(F(r + 0x20 + o), F(v + o));
    StoreVf0(v);
    GRef<f32>(v + 8) = -16.0f;
    sceVu0ApplyMatrix(r + 0x30, m, v);
    GRef<s32>(r + 0x44) = 2;
}

namespace {

// The explosion setup's own LCG (0x00410C18, x * 0x10DCD + 1): two steps per value, the high half
// of the first and of the second.
u32 ExplosionRand() {
    const u32 u1 = W(0x00410C18) * 0x10DCDu + 1u;
    const u32 u2 = u1 * 0x10DCDu + 1u;
    GRef<u32>(0x00410C18) = u2;
    return (u1 & 0xFFFF0000u) | (u2 >> 16);
}

// The value as a float in [0, 1): unsigned convert, x 2^-32.
f32 ExplosionRandUnit() { return fpu::UnsignedToFloat(ExplosionRand()) * 2.3283064e-10f; }

// Inline VU normalise with vrsqrt: xyz / |xyz|, w = 0 + w.
void NormRsqrt(u32 dst, u32 src) {
    const f32 x = F(src), y = F(src + 4), z = F(src + 8), w = F(src + 12);
    const f32 s = vu0::Add(vu0::Add(vu0::Mul(x, x), vu0::Mul(y, y)), vu0::Mul(z, z));
    const f32 q = vu0::Div(1.0f, vu0::Sqrt(s));
    GRef<f32>(dst) = vu0::Mul(x, q);
    GRef<f32>(dst + 4) = vu0::Mul(y, q);
    GRef<f32>(dst + 8) = vu0::Mul(z, q);
    GRef<f32>(dst + 12) = vu0::Add(0.0f, w);
}

}  // namespace

// EE: 0x001F08E8
// SpawnExplosion: explosion `kind` at pos with velocity vel and no direction. Returns the record
// index or -1.
s32 func_001F08E8(u32 pos, u32 vel, s32 kind) {
    guest::GuestFrame fr(0x10);
    const u32 dir = fr.Address(0);
    StoreVf0(dir);
    return func_001F0910(pos, vel, dir, static_cast<u32>(kind));
}

// EE: 0x001F0910
// SpawnMovingExplosion: new record in pool +0x30700 (64 x 0x40), set up by 0x001F09C8.
u32 func_001F0910(u32 pos, u32 vel, u32 dir, u32 kind) {
    const s32 n = func_001FF218(Block() + 0x30700);
    if (n == -1) return 0xFFFFFFFFu;
    const u32 r = Rec(0x30700, static_cast<u32>(n), 0x40, 64);
    func_001F1300(r);
    func_001F09C8(r, pos, vel, dir, kind);
    return static_cast<u32>(n);
}

// EE: 0x001F09C8
// Explosion setup. Kind bits: 0-3 type (definition 0x002B856C + type x 0x3A0, or with bit 0x100
// the pointer table 0x002BB2B8), 0x10 ground burst (2 lower, debris only upward), 0x20 flash
// 0x001FC6C0, 0x40 debris. Seeds the explosion LCG 0x00410C18 from a random clock stamp, life
// = definition +0x8C x 60, a second random clock stamp +0x2C (2 random reads), velocity +0x10 =
// dir x C 0x0040FE4C. Debris (definition +0x29C pieces): random size, direction (around the
// velocity when moving: crossed, normalised and turned by a random angle, size raised by the
// speed), start offset, then smoke columns 0x001F3700 with LCG-chosen parameters.
// fpucheck: 19 adds
void func_001F09C8(u32 r, u32 pos, u32 vel, u32 dir, u32 kind) {
    const u32 type = kind & 0xF;
    GRef<u32>(r + 0x28) = kind;
    if ((kind & 0x100) != 0)
        GRef<u32>(r + 0x30) = W(0x002BB2B8 + type * 4);
    else
        GRef<u32>(r + 0x30) = W(0x002B856C) + type * 0x3A0;
    const f32 k = F(0x0040FE48);
    {
        const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * k);
        GRef<u32>(0x00410C18) = (v + EffectClock()) * 0xEE29u + 1u;
    }
    GRef<s32>(r + 0x24) = fpu::FloatToInt(F(W(r + 0x30) + 0x8C) * 60.0f);
    GRef<u32>(r + 0x2C) = fpu::FixUnsSfSi(NextRandomFloat() * k) + EffectClock();
    guest::GuestFrame fr(0x40);
    const u32 v0 = fr.Address(0), s0 = fr.Address(0x10), s1 = fr.Address(0x20), s3 = fr.Address(0x30);
    CopyQ(v0, vel);
    const bool ground = (kind & 0x10) != 0;
    CopyQ(r, pos);
    if (ground) {
        GRef<f32>(r + 0xC) = 1.0f;
        GRef<f32>(r + 4) = fpu::Sub(F(r + 4), 2.0f);
    }
    sceVu0ScaleVectorXYZ(r + 0x10, dir, F(0x0040FE4C));
    if ((kind & 0x40) != 0) {
        const s32 count = GRef<s32>(W(r + 0x30) + 0x29C);
        u32 seq = ExplosionRand() % 16u;
        for (s32 i = 0; i < count; ++i) {
            const u32 def = W(r + 0x30);
            f32 size = fpu::Add(F(def + 0x2AC), ExplosionRandUnit() * F(def + 0x2B0));
            GRef<f32>(s1) = fpu::Sub(ExplosionRandUnit(), 0.5f);
            GRef<f32>(s1 + 4) = fpu::Sub(ExplosionRandUnit(), 0.5f);
            GRef<f32>(s1 + 8) = fpu::Sub(ExplosionRandUnit(), 0.5f);
            GRef<f32>(s1 + 12) = 1.0f;
            if (ground) {
                CopyQ(s0, s1);
                GRef<f32>(s0 + 4) = -std::fabs(F(s0 + 4));
            } else if (F(vel) == 0.0f && F(vel + 4) == 0.0f && F(vel + 8) == 0.0f) {
                CopyQ(s0, s1);
            } else {
                sceVu0OuterProduct(s1, s1, v0);
                NormRsqrt(s1, s1);
                NormRsqrt(s0, v0);
                const f32 a = fpu::Add(ExplosionRandUnit() * F(0x0040FE50), F(0x0040FE50)) * F(0x0040FE54);
                const f32 c = ee::cosf(a);
                for (u32 o = 0; o < 12; o += 4) GRef<f32>(s0 + o) = vu0::Mul(F(s0 + o), c);
                const f32 sn = ee::sinf(a);
                for (u32 o = 0; o < 12; o += 4) GRef<f32>(s0 + o) = vu0::Add(F(s0 + o), vu0::Mul(F(s1 + o), sn));
                const f32 x = F(v0), y = F(v0 + 4), z = F(v0 + 8);
                const f32 len = fpu::Sqrt(fpu::MulAdd(fpu::MulAdd(x * x, y, y), z, z));
                size = fpu::Add(size * 0.5f, len * F(0x0040FE58));
            }
            NormRsqrt(s0, s0);
            const f32 reach = F(0x002BB2C8 + 4 * type) * fpu::Add(ExplosionRandUnit() * 0.5f, 0.5f);
            for (u32 o = 0; o < 12; o += 4) GRef<f32>(s3 + o) = vu0::Add(F(pos + o), vu0::Mul(F(s0 + o), reach));
            GRef<u32>(s3 + 12) = W(pos + 12);
            for (u32 o = 0; o < 12; o += 4) GRef<f32>(s0 + o) = vu0::Mul(F(s0 + o), size);
            const f32 lim = size < 8.0f ? size : 8.0f;
            const f32 twice = fpu::Add(lim, lim);
            const u32 def2 = W(r + 0x30);
            const f32 span = fpu::Add(F(def2 + 0x2A4), ExplosionRandUnit() * F(def2 + 0x2A8));
            const f32 f = span < twice ? span : twice;
            s32 m = fpu::FloatToInt(f) + 4;
            if (!(m < 16)) m = 16;
            const u32 ra = ExplosionRand() % 64u;
            const u32 rb = ExplosionRand() % W(def2 + 0x2A0);
            const s32 lim2 = m < static_cast<s32>(rb) ? static_cast<s32>(rb) : m;
            func_001F3700(f, s3, s0, static_cast<s32>(ra) + 0x60, lim2, static_cast<s32>(seq));
            seq = static_cast<u32>((static_cast<s32>(seq) + 1) % 16);
        }
    }
    if ((kind & 0x20) != 0) func_001FC6C0(F(W(r + 0x30) + 0x2B4), pos, 0);
}

// EE: 0x001F3700
// New smoke column in pool +0x31710 (64 x 0x1B0), set up by 0x001F37E0.
s32 func_001F3700(f32 size, u32 pos, u32 vel, s32 life, s32 m, s32 seq) {
    const s32 n = func_001FF318(Block() + 0x31710);
    if (n == -1) return -1;
    const u32 r = Rec(0x31710, static_cast<u32>(n), 0x1B0, 64);
    func_001F38F0(r);
    func_001F37E0(size, r, pos, vel, life, m, seq);
    return n;
}

// EE: 0x001F37E0
// Smoke column record: position, speed |vel| +0x18C and unit direction +0x10, size +0x190, life
// +0x19C, random clock stamp +0x1A0 (1 random read), sequence +0x1A4 = seq % 16, ring limit +0x194
// = min(m, table 0x002BB308[seq]), head +0x20 = (0, 0, 0, 1).
// fpucheck: 2 adds (madda / madd)
void func_001F37E0(f32 size, u32 r, u32 pos, u32 vel, s32 life, s32 m, s32 seq) {
    CopyQ(r, pos);
    const f32 x = F(vel), y = F(vel + 4), z = F(vel + 8);
    const f32 len = fpu::Sqrt(fpu::MulAdd(fpu::MulAdd(x * x, y, y), z, z));
    GRef<f32>(r + 0x18C) = len;
    sceVu0ScaleVectorXYZ(r + 0x10, vel, fpu::Div(1.0f, len));
    GRef<f32>(r + 0x190) = size;
    GRef<s32>(r + 0x19C) = life;
    const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * F(0x0040FE70));
    const s32 s = seq % 16;
    GRef<u32>(r + 0x1A0) = v + EffectClock();
    GRef<s32>(r + 0x1A4) = s;
    const s32 t = GRef<s32>(0x002BB308 + 16u * static_cast<u32>(s));
    GRef<s32>(r + 0x194) = t < m ? t : m;
    StoreVf0(r + 0x20);
}

// EE: 0x001F67B8
// New record in pool +0x38320 (32 x 0x40), set up by 0x001F6880.
s32 func_001F67B8(f32 size, u32 pos, u32 vel, s32 kind, s32 n2) {
    const s32 n = func_001FF418(Block() + 0x38320);
    if (n == -1) return -1;
    const u32 r = Rec(0x38320, static_cast<u32>(n), 0x40, 32);
    func_001F6978(r);
    func_001F6880(size, r, pos, vel, kind, n2);
    return n;
}

// EE: 0x001F6880
// Shock ring record: random clock stamp +0x20 (1 random read), position, velocity, size +0x30,
// kind +0x2C, +0x34 = n; life +0x28 = 180 + n (kind 0) or 180 + 2n (kinds 1-3), else unchanged.
void func_001F6880(f32 size, u32 r, u32 pos, u32 vel, s32 kind, s32 n) {
    const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * F(0x0040FE80));
    GRef<u32>(r + 0x20) = v + EffectClock();
    CopyQ(r, pos);
    CopyQ(r + 0x10, vel);
    GRef<f32>(r + 0x30) = size;
    GRef<s32>(r + 0x2C) = kind;
    GRef<s32>(r + 0x34) = n;
    if (kind == 0)
        GRef<s32>(r + 0x28) = n + 0xB4;
    else if (kind >= 1 && kind <= 3)
        GRef<s32>(r + 0x28) = n * 2 + 0xB4;
}

// EE: 0x001E94F8
s32 func_001E94F8(u32 pos, u32 vel, s32 kind) { return func_001F08E8(pos, vel, kind); }

// EE: 0x001E9518
// SpawnMovingExplosion with the vel and dir arguments swapped.
u32 func_001E9518(u32 pos, u32 dir, u32 vel, s32 kind) { return func_001F0910(pos, vel, dir, static_cast<u32>(kind)); }

// EE: 0x001E9570
s32 func_001E9570(f32 r, u32 pos, u32 vel) {
    func_001F67B8(r, pos, vel, 0, 4);
    return 0;
}

// EE: 0x001E9598
s32 func_001E9598(f32 r, u32 pos, u32 vel) {
    func_001F67B8(r, pos, vel, 1, 10);
    return 0;
}

// EE: 0x001E95C0
s32 func_001E95C0(f32 r, u32 pos, u32 vel) {
    func_001F67B8(r, pos, vel, 2, 10);
    return 0;
}

// EE: 0x001E95E8
// Dead (no reference): as func_001E9598 with kind 3.
s32 func_001E95E8(f32 r, u32 pos, u32 vel) {
    func_001F67B8(r, pos, vel, 3, 10);
    return 0;
}

// EE: 0x00155AE8
void func_00155AE8() {}

// EE: 0x001E9610
// Spawn or refresh in pool +0x2B430 (8 x 0x50), then 0x001E9708.
u32 func_001E9610(u32 h, u32 pos, u32 vel, s32 unit) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_001FEE28(Block() + 0x2B430);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        func_001E97F0(Rec(0x2B430, i, 0x50, 8));
    }
    func_001E9708(Rec(0x2B430, i, 0x50, 8), pos, vel, unit);
    return i;
}

// EE: 0x001E9708
// Unit fire record: position, velocity, unit +0x24 and its type +0x28 (unit +0x69), refresh count
// +0x22, and a random clock stamp (1 random read) pushed into the 4-entry ring +0x2C (head +0x40,
// tail +0x44).
void func_001E9708(u32 r, u32 pos, u32 vel, s32 unit) {
    CopyQ(r, pos);
    CopyQ(r + 0x10, vel);
    GRef<s32>(r + 0x24) = unit;
    GRef<u16>(r + 0x22) = static_cast<u16>(GRef<u16>(r + 0x22) + 1);
    GRef<u32>(r + 0x28) = GRef<u8>(Unit(unit) + 0x69);
    const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * F(0x0040FDFC));
    GRef<u32>(r + 0x2C + 4 * W(r + 0x44)) = v + EffectClock();
    const s32 t = (GRef<s32>(r + 0x44) + 1) % 4;
    GRef<s32>(r + 0x44) = t;
    if (t == GRef<s32>(r + 0x40)) GRef<s32>(r + 0x40) = (t + 1) % 4;
}

// EE: 0x001EB380
// New record in pool +0x2B6C0 (32 x 0x130), set up by 0x001EB440.
s32 func_001EB380(u32 pos, u32 rot, u32 vel, s32 unit) {
    const s32 n = func_001FEF28(Block() + 0x2B6C0);
    if (n == -1) return -1;
    const u32 r = Rec(0x2B6C0, static_cast<u32>(n), 0x130, 32);
    func_001EB708(r);
    func_001EB440(r, pos, rot, vel, unit);
    return n;
}

// EE: 0x001EB440
// Wreck debris strip of a unit (type +0x120 = unit +0x69; type 0x15 uses the offsets 0x002BA320 /
// 0x002BA340 and no spread, others 0x002BA310 / 0x002BA330 with spread C 0x0040FE00 and scale
// C 0x0040FE04): 4 pairs of points along the turned step, each pair with its two side points
// (+-spread turned, plus the scaled velocity), velocity +0x80, random clock stamp +0x124 (1 random
// read).
// fpucheck: 2 adds (the unsigned convert's add, r x k + clock)
void func_001EB440(u32 r, u32 pos, u32 rot, u32 vel, s32 unit) {
    GRef<s32>(r + 0x11C) = unit;
    GRef<u32>(r + 0x120) = GRef<u8>(Unit(unit) + 0x69);
    guest::GuestFrame fr(0x70);
    const u32 m = fr.Address(0), p = fr.Address(0x40), d = fr.Address(0x50), e = fr.Address(0x60);
    SetUnitMatrixVu0(m);
    func_00102D28(m, m, rot);
    const bool t21 = W(r + 0x120) == 0x15;
    sceVu0ApplyMatrix(p, m, t21 ? 0x002BA320 : 0x002BA310);
    sceVu0ApplyMatrix(d, m, t21 ? 0x002BA340 : 0x002BA330);
    for (u32 o = 0; o < 12; o += 4) GRef<f32>(p + o) = vu0::Add(F(p + o), F(pos + o));
    for (u32 o = 0; o < 12; o += 4) GRef<f32>(p + o) = vu0::Sub(F(p + o), F(vel + o));
    GRef<f32>(p + 12) = 1.0f;
    const f32 spread = t21 ? 0.0f : F(0x0040FE00);
    const f32 scale = t21 ? 1.0f : F(0x0040FE04);
    sceVu0ScaleVectorXYZ(e, vel, 1.0f);
    for (u32 k = 0; k < 4; ++k) {
        const u32 a = r + 0x20 * k, sa = r + 0x90 + 0x20 * k, b = r + 0x10 + 0x20 * k, sb = r + 0xA0 + 0x20 * k;
        CopyQ(a, p);
        StoreVf0(sa);
        GRef<f32>(sa) = spread;
        sceVu0ApplyMatrix(sa, m, sa);
        for (u32 o = 0; o < 12; o += 4) GRef<f32>(sa + o) = vu0::Add(F(sa + o), F(e + o));
        CopyQ(b, p);
        StoreVf0(sb);
        GRef<f32>(sb) = -spread;
        sceVu0ApplyMatrix(sb, m, sb);
        for (u32 o = 0; o < 12; o += 4) GRef<f32>(sb + o) = vu0::Add(F(sb + o), F(e + o));
        for (u32 o = 0; o < 12; o += 4) GRef<f32>(p + o) = vu0::Add(F(p + o), F(d + o));
        for (u32 o = 0; o < 12; o += 4) GRef<f32>(e + o) = vu0::Mul(F(e + o), scale);
    }
    CopyQ(r + 0x80, vel);
    const f32 t = NextRandomFloat() * F(0x0040FE08);
    GRef<u32>(r + 0x124) = fpu::FixUnsSfSi(fpu::Add(t, fpu::UnsignedToFloat(EffectClock())));
}

// EE: 0x001EC268
// Spawn or refresh a falling burning piece (pool +0x2DCD0, 32 x 0x30): a new one needs a positive
// fall time t = (sqrt(vy^2 - 98 y) - vy) / 49 (kept in +0x2C), else nothing. Then 0x001EC3B8.
// fpucheck: 2 adds
u32 func_001EC268(u32 h, u32 pos, u32 rot, u32 vel) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const f32 vy = F(vel + 4);
        const f32 sq = fpu::Sqrt(fpu::Sub(vy * vy, F(pos + 4) * 98.0f));
        const f32 t = fpu::Add(-vy, sq) / 49.0f;
        if (t < 0.0f) return 0xFFFFFFFFu;
        const s32 n = func_001FF020(Block() + 0x2DCD0);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        const u32 r = Rec(0x2DCD0, i, 0x30, 32);
        func_001EC458(r);
        GRef<f32>(r + 0x2C) = t;
    }
    func_001EC3B8(Rec(0x2DCD0, i, 0x30, 32), pos, rot, vel);
    return i;
}

// EE: 0x001EC3B8
// Falling piece refresh: the first refresh takes the position; later ones move it by 3 x its
// velocity once frame x C 0x0040FE14 reaches the fall time x C 0x0040FE18 (else it only rises by
// half its vertical speed and keeps the old velocity). Refresh count +0x24.
// fpucheck: 1 add
void func_001EC3B8(u32 r, u32 pos, u32 rot, u32 vel) {
    (void)rot;
    const s32 n = GRef<s32>(r + 0x24);
    if (n != 0) {
        const f32 a = fpu::IntToFloat(GRef<s32>(r + 0x20)) * F(0x0040FE14);
        const f32 b = F(r + 0x2C) * F(0x0040FE18);
        if (!(b <= a)) {
            GRef<f32>(r + 4) = fpu::Add(F(r + 4), F(r + 0x14) * 0.5f);
            GRef<s32>(r + 0x24) = n + 1;
            return;
        }
        for (u32 o = 0; o < 12; o += 4) GRef<f32>(r + o) = vu0::Add(F(r + o), vu0::Mul(F(r + 0x10 + o), 3.0f));
    } else {
        CopyQ(r, pos);
    }
    CopyQ(r + 0x10, vel);
    GRef<s32>(r + 0x24) = GRef<s32>(r + 0x24) + 1;
}

// EE: 0x001ED2E8
// New record in pool +0x2E2E0 (8 x 0x40): position, angles, velocity, size +0x34, random phase
// +0x38 (1 random read).
s32 func_001ED2E8(f32 size, u32 pos, u32 rot, u32 vel) {
    const s32 n = func_001FF118(Block() + 0x2E2E0);
    if (n == -1) return -1;
    const u32 r = Rec(0x2E2E0, static_cast<u32>(n), 0x40, 8);
    func_001ED3C8(r);
    CopyQ(r, pos);
    CopyQ(r + 0x10, rot);
    CopyQ(r + 0x20, vel);
    GRef<f32>(r + 0x34) = size;
    GRef<u32>(r + 0x38) = fpu::FixUnsSfSi(NextRandomFloat() * 65536.0f);
    return n;
}

// EE: 0x001FF668
// Dead (no reference, inline copy): GIFtag low word 0x9400000000008000 (EOP, 9 registers, REGLIST, NLOOP 0).
u64 func_001FF668() { return 0x9400000000008000ull; }

// EE: 0x001FF680
// Dead (no reference, inline copy): GIFtag register list 0x5353D3D31.
u64 func_001FF680() { return 0x5353D3D31ull; }

// EE: 0x001FF6A0
// Dead (no reference, inline copy): 0x114.
u32 func_001FF6A0() { return 0x114; }

// EE: 0x001FF6C0
// Spawn or refresh in pool +0x24650 (8 x 0x30): position, unit direction +0x10 (vrsqrt
// normalise), kind +0x24, end frame +0x22 = frame +0x20 + 40.
u32 func_001FF6C0(u32 h, u32 pos, u32 dir, s32 kind) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_00210670(Block() + 0x24650);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        func_001FF7E8(Rec(0x24650, i, 0x30, 8));
    }
    const u32 r = Rec(0x24650, i, 0x30, 8);
    CopyQ(r, pos);
    NormRsqrt(r + 0x10, dir);
    GRef<s16>(r + 0x24) = static_cast<s16>(kind);
    GRef<u16>(r + 0x22) = static_cast<u16>(GRef<u16>(r + 0x20) + 0x28);
    return i;
}

// EE: 0x00203340
// New record in pool +0x268E0 (64 x 0x30), set up by 0x002033F0.
s32 func_00203340(u32 pos, s32 kind, s32 n) {
    const s32 i = func_00210990(Block() + 0x268E0);
    if (i == -1) return -1;
    const u32 r = Rec(0x268E0, static_cast<u32>(i), 0x30, 64);
    func_00203480(r);
    func_002033F0(r, pos, static_cast<s16>(kind), n);
    return i;
}

// EE: 0x002033F0
// Record: kind +0x18, position, lifetime +0x14 (n, or for -1 the kind's default from 0x004059E8),
// +0x1A = (kind + 1) x 4, random clock stamp +0x1C (1 random read).
void func_002033F0(u32 r, u32 pos, s16 kind, s32 n) {
    GRef<s16>(r + 0x18) = kind;
    CopyQ(r, pos);
    GRef<u32>(r + 0x14) = n == -1 ? W(0x004059E8 + 4u * static_cast<u32>(static_cast<s32>(kind))) : static_cast<u32>(n);
    GRef<s16>(r + 0x1A) = static_cast<s16>((kind + 1) * 4);
    const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * F(0x0040FEFC));
    GRef<u32>(r + 0x1C) = v + EffectClock();
}

// EE: 0x002061B0
// Spawn or refresh the single record of pool +0x267F0, then 0x00206270.
s32 func_002061B0(s32 h, u32 pos, u32 vel, s32 kind) {
    const u32 r = Block() + 0x267F0;
    if (h == -1) {
        h = func_00210A90(r);
        if (h == -1) return -1;
        func_00206588(r);
    }
    func_00206270(r, pos, vel, kind);
    return h;
}

// EE: 0x00206270
// Burning smoke trail: the emitter point 200 behind along -vel (kind 5: 200 below) plus vel
// (scratchpad 0x70000000, particle velocity 0x70000010 = (0, 0, 0, 1)). Every 32nd frame +0xC0 a
// particle is pushed into the 64-entry ring +0x34 (head +0xB8, tail +0xBC; the oldest freed when it
// wraps), other frames overwrite the newest. Position +0x00 (first refresh), emitter +0x10,
// velocity +0x20, refresh count +0xC4; kind 4 also drives a trail 0x00200B00 (+0xD4) and counts
// +0xC8. +0xCC = 0x800, kind +0xD0.
void func_00206270(u32 r, u32 pos, u32 vel, s32 kind) {
    const u32 sp0 = 0x70000000, sp1 = 0x70000010;
    GRef<u32>(r + 0xCC) = 0x800;
    GRef<u16>(r + 0xD0) = static_cast<u16>(kind);
    if (kind == 5) {
        StoreVf0(sp0);
        GRef<f32>(sp0 + 4) = -200.0f;
    } else {
        NormRsqrt(sp0, vel);
        for (u32 o = 0; o < 16; o += 4) GRef<f32>(sp0 + o) = vu0::Mul(F(sp0 + o), 200.0f);
    }
    sceVu0SubVector(sp0, pos, sp0);
    sceVu0AddVector(sp0, sp0, vel);
    StoreVf0(sp1);
    const u32 ring = r + 0x34;
    if ((W(r + 0xC0) & 0x1Fu) == 0) {
        const s32 pid = func_001E0700(Block() + 0x2010);
        if (pid != -1) {
            const u32 q = Particle(static_cast<s16>(pid));
            CopyQ(q, sp0);
            CopyQ(q + 0x10, sp1);
            const s32 t0 = GRef<s32>(r + 0xBC);
            GRef<s16>(ring + 2u * static_cast<u32>(t0)) = static_cast<s16>(pid);
            const s32 t1 = (t0 + 1) % 0x40;
            GRef<s32>(r + 0xBC) = t1;
            if (t1 == GRef<s32>(r + 0xB8)) {
                GRef<s32>(r + 0xB8) = (t1 + 1) % 0x40;
                FreeParticle(GRef<s16>(ring + 2u * static_cast<u32>(t1)));
            }
        }
    } else {
        const s32 head = GRef<s32>(r + 0xB8);
        const s32 n = (GRef<s32>(r + 0xBC) - head + 0x40) % 0x40;
        const u32 q = Particle(GRef<s16>(ring + 2u * static_cast<u32>((head + n + 0x3F) % 0x40)));
        CopyQ(q, sp0);
        CopyQ(q + 0x10, sp1);
    }
    if (W(r + 0xC4) == 0) CopyQ(r, pos);
    CopyQ(r + 0x10, sp0);
    CopyQ(r + 0x20, vel);
    GRef<s32>(r + 0xC4) += 1;
    if (kind == 4) {
        GRef<s32>(r + 0xC8) += 1;
        GRef<u32>(r + 0xD4) = func_00200B00(W(r + 0xD4), pos, vel, 4, 0xD2);
    }
}

// EE: 0x0020A3A0
// Spawn or refresh a burning wreck (pool +0x274F0, 16 x 0xF0; a new one keeps its start position
// in +0xB0), then 0x00210E08.
u32 func_0020A3A0(u32 h, u32 pos, u32 vel, s32 n) {
    u32 i = h;
    if (h == 0xFFFFFFFFu) {
        const s32 k = func_00210B88(Block() + 0x274F0);
        if (k == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(k);
        const u32 r = Rec(0x274F0, i, 0xF0, 16);
        func_0020A4A0(r);
        CopyQ(r + 0xB0, pos);
    }
    func_00210E08(Rec(0x274F0, i, 0xF0, 16), pos, vel, n);
    return i;
}

// EE: 0x00200B00
// Spawn or refresh a smoke trail (pool +0x247E0, 32 x 0x100; kind outside 1..5 counts as 1; a new
// one gets length min(len, 210) and the kind; a handle whose record is no longer live returns -1),
// then one step 0x00200D58. Kinds 1 and 2, above -2000 and every 30th trail frame, also leave a
// mark 0x00203340 330 / 320 below; kind 3 leaves one at the start.
u32 func_00200B00(u32 h, u32 pos, u32 vel, s32 kind, s32 len) {
    const s32 k = static_cast<u32>(kind - 1) < 5u ? kind : 1;
    u32 i = h;
    u32 r;
    if (h == 0xFFFFFFFFu) {
        const s32 n = func_00210770(Block() + 0x247E0);
        if (n == -1) return 0xFFFFFFFFu;
        i = static_cast<u32>(n);
        r = Rec(0x247E0, i, 0x100, 32);
        func_001DB0A8(r);
        GRef<s16>(r + 0xFC) = static_cast<s16>(k);
        GRef<s32>(r + 0xF8) = len < 0xD2 ? len : 0xD2;
    } else {
        if (((W(Block() + 0x247E0 + 0x2000 + (h >> 5) * 4) >> (h & 0x1F)) & 1u) == 0) return 0xFFFFFFFFu;
        r = Rec(0x247E0, i, 0x100, 32);
    }
    func_00200D58(r, pos, vel, k, len);
    guest::GuestFrame fr(0x10);
    const u32 tmp = fr.Address(0);
    CopyQ(tmp, pos);
    if (k == 1 || k == 2) {
        if (!(-2000.0f < F(pos + 4))) return i;
        if (GRef<s32>(r + 0xF0) % 30 != 0) return i;
        GRef<f32>(tmp + 4) = k == 1 ? -330.0f : -320.0f;
        func_00203340(tmp, k, -1);
    } else if (k == 3) {
        if (h == 0xFFFFFFFFu) func_00203340(pos, 3, -1);
    }
    return i;
}

// EE: 0x00200D58
// Smoke trail step: the emitter point (pos - unit vel x the kind's offset 0x00405988[kind] + vel)
// with a zero particle velocity, then the shared ring step (as 0x001DAC38).
void func_00200D58(u32 r, u32 pos, u32 vel, s32 kind, s32 len) {
    (void)len;
    const u32 sp0 = 0x70000000, sp1 = 0x70000010;
    const f32 k = F(0x00405988 + 4u * static_cast<u32>(kind));
    NormRsqrt(sp0, vel);
    for (u32 o = 0; o < 16; o += 4) GRef<f32>(sp0 + o) = vu0::Mul(F(sp0 + o), k);
    sceVu0SubVector(sp0, pos, sp0);
    sceVu0AddVector(sp0, sp0, vel);
    StoreVf0(sp1);
    TrailStep(r, sp0, sp1);
}
}  // namespace ac04::game
