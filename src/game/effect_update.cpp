// Effect simulation (slice S5d): UpdateEffects 0x00154DD0 runs every effect pool's record update
// inside the simulation step (docs/subsystems/effects.md "Pools"), plus the per-pool "lowest free
// record" allocators the spawn functions use. Pool layout: effect_pools.cpp (`count` records of
// `stride` bytes, then ceil(count / 32) occupancy words and a live counter).
//
// Every pool update wrapper (0x00157398...0x0015AD18) is the same loop: occupancy word by word,
// bit by bit from the lowest while bits remain (a snapshot of the word), calling the record update;
// a non-zero result frees the record (bit cleared in the live word, counter decremented).
#include "game/game.h"

#include <cmath>
#include <vector>

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

void UpdatePool(u32 p, s32 (*update)(u32), u32 stride, u32 count) {
    const u32 words = (count + 31) / 32;
    const u32 mask = p + count * stride;
    for (u32 w = 0; w < words; ++w) {
        u32 bits = W(mask + 4 * w);
        if (bits == 0) continue;
        const s32 n = static_cast<s32>(count) - static_cast<s32>(32 * w) < 32
                          ? static_cast<s32>(count) - static_cast<s32>(32 * w) : 32;
        for (s32 b = 0; b < n; ++b) {
            const bool live = (bits & 1u) != 0;
            bits >>= 1;
            if (live && update(p + (32 * w + static_cast<u32>(b)) * stride) != 0) {
                GRef<u32>(mask + 4 * w) = W(mask + 4 * w) & ~(1u << b);
                GRef<s32>(mask + 4 * words) -= 1;
            }
            if (bits == 0) break;
        }
    }
}

// Lowest free record of a pool: sets its bit, counts it, returns its index; -1 when full.
s32 AllocSlot(u32 p, u32 count, u32 counter_off) {
    const u32 words = (count + 31) / 32;
    const u32 mask = p + counter_off - 4 * words;
    for (u32 w = 0; w < words; ++w) {
        u32 bits = W(mask + 4 * w);
        if (bits == 0xFFFFFFFFu) continue;
        const s32 n = static_cast<s32>(count) - static_cast<s32>(32 * w) < 32
                          ? static_cast<s32>(count) - static_cast<s32>(32 * w) : 32;
        for (s32 b = 0; b < n; ++b) {
            if ((bits & 1u) == 0) {
                GRef<u32>(mask + 4 * w) = W(mask + 4 * w) | (1u << b);
                GRef<s32>(p + counter_off) += 1;
                return static_cast<s32>(32 * w) + b;
            }
            bits >>= 1;
        }
    }
    return -1;
}

// vadd.xyz / vmulx.xyz on guest vectors (w kept).
void AddXyz(u32 dst, u32 a, u32 b) {
    const f32 x = vu0::Add(F(a), F(b)), y = vu0::Add(F(a + 4), F(b + 4)), z = vu0::Add(F(a + 8), F(b + 8));
    GRef<f32>(dst) = x;
    GRef<f32>(dst + 4) = y;
    GRef<f32>(dst + 8) = z;
}
void ScaleXyz(u32 dst, u32 src, f32 k) {
    const f32 x = vu0::Mul(F(src), k), y = vu0::Mul(F(src + 4), k), z = vu0::Mul(F(src + 8), k);
    GRef<f32>(dst) = x;
    GRef<f32>(dst + 4) = y;
    GRef<f32>(dst + 8) = z;
}

// Releases particle `id` of the shared particle pool (+0x2010, 4096 records): ids >= 0x1000
// (unsigned) are ignored.
void FreeParticle(s16 id) {
    if (static_cast<u32>(static_cast<s32>(id)) >= 0x1000) return;
    const u32 mask = Block() + 0x22010;
    const u32 word = mask + (static_cast<u32>(static_cast<s32>(id)) >> 5) * 4;
    GRef<u32>(word) = W(word) & ~(1u << (id & 0x1F));
    GRef<s32>(mask + 0x200) -= 1;
}

}  // namespace

// ---- Record updates (return non-zero when the record is finished) --------------------------------

// EE: 0x001DB0C0
// Trail with a ring of particle ids (+0x10 s16[0x6A], head +0xE8, tail +0xEC): after +0xF8 frames
// one particle is released every other frame; finished when the ring is empty.
s32 func_001DB0C0(u32 r) {
    const s32 age = GRef<s32>(r + 0xF0) + 1;
    GRef<s32>(r + 0xF0) = age;
    if (age - GRef<s32>(r + 0xF8) < 0) return 0;
    if ((age & 1) == 0 && W(r + 0xE8) != W(r + 0xEC)) {
        FreeParticle(GRef<s16>(r + 0x10 + 2 * W(r + 0xE8)));
        const s32 head = GRef<s32>(r + 0xE8);
        if (head != GRef<s32>(r + 0xEC)) GRef<s32>(r + 0xE8) = (head + 1) % 0x6A;
    }
    return W(r + 0xEC) == W(r + 0xE8) ? 1 : 0;
}

// EE: 0x001F1308
// Moving point: finished after +0x24 frames; position += velocity, velocity x C 0x0040FE5C.
s32 func_001F1308(u32 r) {
    const s32 age = GRef<s32>(r + 0x20) + 1;
    GRef<s32>(r + 0x20) = age;
    if (GRef<s32>(r + 0x24) < age) return 1;
    AddXyz(r, r, r + 0x10);
    ScaleXyz(r + 0x10, r + 0x10, F(0x0040FE5C));
    return 0;
}

// EE: 0x001F3910
// Smoke column: finished after +0x19C frames. The head +0x20 moves by +0x10 x speed +0x18C (x5 in
// the first two frames), rises by age x C 0x0040FE74 and the speed decays by C 0x0040FE78. Every
// even frame the head is pushed into the 16-entry point ring (+0x30, head +0x134, tail +0x138)
// with a random clock stamp in the parallel ring +0x140 (+0x184 / +0x188; 1 random read); past
// +0x194 points both rings drop their oldest. Odd frames overwrite the newest point.
// fpucheck: 2 adds
s32 func_001F3910(u32 r) {
    const s32 age = GRef<s32>(r + 0x198) + 1;
    GRef<s32>(r + 0x198) = age;
    if (GRef<s32>(r + 0x19C) < age) return 1;
    const u32 pos = r + 0x20;
    const f32 sp = F(r + 0x18C);
    const f32 k = age < 3 ? sp * 5.0f : sp;
    {
        const f32 vx = vu0::Mul(F(r + 0x10), k), vy = vu0::Mul(F(r + 0x14), k), vz = vu0::Mul(F(r + 0x18), k);
        GRef<f32>(pos) = vu0::Add(F(pos), vx);
        GRef<f32>(pos + 4) = vu0::Add(F(pos + 4), vy);
        GRef<f32>(pos + 8) = vu0::Add(F(pos + 8), vz);
    }
    const s32 a = GRef<s32>(r + 0x198);
    GRef<f32>(r + 0x24) = fpu::Add(F(r + 0x24), fpu::IntToFloat(a) * F(0x0040FE74));
    if (0.0f < sp) GRef<f32>(r + 0x18C) = sp * fpu::Sub(1.0f, F(0x0040FE78));
    const u32 ring = r + 0x30;
    if ((a & 1) == 0) {
        CopyQ(ring + 16 * W(r + 0x138), pos);
        s32 t = (GRef<s32>(r + 0x138) + 1) % 16;
        GRef<s32>(r + 0x138) = t;
        if (t == GRef<s32>(r + 0x134)) GRef<s32>(r + 0x134) = (t + 1) % 16;
        const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * F(0x0040FE7C));
        GRef<u32>(r + 0x140 + 4 * W(r + 0x188)) = v + EffectClock();
        t = (GRef<s32>(r + 0x188) + 1) % 16;
        GRef<s32>(r + 0x188) = t;
        if (t == GRef<s32>(r + 0x184)) GRef<s32>(r + 0x184) = (t + 1) % 16;
        const s32 head = GRef<s32>(r + 0x134), tail = GRef<s32>(r + 0x138);
        if ((tail - head + 16) % 16 < GRef<s32>(r + 0x194)) return 0;
        if (head != tail) GRef<s32>(r + 0x134) = (head + 1) % 16;
        const s32 h2 = GRef<s32>(r + 0x184);
        if (h2 != GRef<s32>(r + 0x188)) GRef<s32>(r + 0x184) = (h2 + 1) % 16;
    } else {
        const s32 head = GRef<s32>(r + 0x134);
        const s32 n = (GRef<s32>(r + 0x138) - head + 16) % 16;
        if (n > 0) CopyQ(ring + 16 * static_cast<u32>((head + n + 15) % 16), pos);
    }
    return 0;
}

// EE: 0x001F6980
s32 func_001F6980(u32 r) {
    const s32 age = GRef<s32>(r + 0x24) + 1;
    GRef<s32>(r + 0x24) = age;
    return GRef<s32>(r + 0x28) < age ? 1 : 0;
}

// EE: 0x001FC828
s32 func_001FC828(u32 r) {
    const s16 age = static_cast<s16>(GRef<u16>(r + 0x10) + 1);
    GRef<s16>(r + 0x10) = age;
    return age < GRef<s16>(r + 0x12) ? 0 : 1;
}

// EE: 0x001E45C8
// Flash with a 4-sample history (+0x28..+0x34, shifted every 4th effect frame): finished when its
// refresh flag +0x39 was not set again. Intensity +0x28 = (level - C 0x0040FDEC) x 127 x (r + 9.5)
// when the level +0x20 is above a random threshold (1 or 2 random reads); +0x24 eases toward +0x20.
// fpucheck: 5 adds
s32 func_001E45C8(u32 r) {
    if (GRef<u8>(r + 0x39) == 0) return 1;
    GRef<u8>(r + 0x39) = 0;
    if ((EffectClock() & 3) == 0) {
        for (u32 o = 0x34; o > 0x28; o -= 4) GRef<u32>(r + o) = W(r + o - 4);
    }
    const f32 base = F(0x0040FDEC);
    const f32 t = fpu::Add(NextRandomFloat() / 100.0f, base);
    f32 level = F(r + 0x20);
    if (t < level) {
        const f32 r2 = NextRandomFloat();
        level = F(r + 0x20);
        GRef<s32>(r + 0x28) = fpu::FloatToInt(fpu::Sub(level, base) * 127.0f * fpu::Add(r2, 9.5f));
    } else {
        GRef<u32>(r + 0x28) = 0;
    }
    const f32 cur = F(r + 0x24);
    GRef<f32>(r + 0x24) = fpu::Add(cur, fpu::Sub(level, cur) * 0.5f);
    return 0;
}

// EE: 0x001E8008
// Short debris: finished at frame 7; velocity +0x20 x C 0x0040FDF0, then position += velocity.
s32 func_001E8008(u32 r) {
    const u16 age = static_cast<u16>(GRef<u16>(r + 0x50) + 1);
    GRef<u16>(r + 0x50) = age;
    if (age >= 7) return 1;
    ScaleXyz(r + 0x20, r + 0x20, F(0x0040FDF0));
    AddXyz(r, r, r + 0x20);
    return 0;
}

// EE: 0x001FF7F0
s32 func_001FF7F0(u32 r) {
    const s16 age = static_cast<s16>(GRef<u16>(r + 0x20) + 1);
    GRef<s16>(r + 0x20) = age;
    return age < GRef<s16>(r + 0x22) ? 0 : 1;
}

// EE: 0x00203488
s32 func_00203488(u32 r) {
    const s32 age = GRef<s32>(r + 0x10) + 1;
    GRef<s32>(r + 0x10) = age;
    return age < GRef<s32>(r + 0x14) ? 0 : 1;
}

// EE: 0x002065F0
// After 180 frames past +0xC4 every particle of its ring (+0x34 s16[64], head +0xB8, tail +0xBC)
// is released and the record finishes.
s32 func_002065F0(u32 r) {
    const s32 age = GRef<s32>(r + 0xC0) + 1;
    GRef<s32>(r + 0xC0) = age;
    if (age - GRef<s32>(r + 0xC4) < 0xB5) return 0;
    for (s32 i = 0; i < (GRef<s32>(r + 0xBC) - GRef<s32>(r + 0xB8) + 0x40) % 0x40; ++i) {
        if (W(r + 0xB8) == W(r + 0xBC)) continue;
        FreeParticle(GRef<s16>(r + 0x34 + 2 * W(r + 0xB8)));
        const s32 head = GRef<s32>(r + 0xB8);
        if (head != GRef<s32>(r + 0xBC)) GRef<s32>(r + 0xB8) = (head + 1) % 0x40;
    }
    return 1;
}

// EE: 0x0020A4B8
// Burning wreck with a 10-entry point ring (+0x00, head +0xA4, tail +0xA8): until frame +0xD8 - 1
// it spawns smoke 0x0020C2A0 at the newest point (frame 1: size 30 + 15r, frames 2-11 and every 5th
// after: 10 + 15r; 1 random read each); then every 12th frame the oldest point is dropped and it
// finishes when the ring is empty.
s32 func_0020A4B8(u32 r) {
    const s32 age = GRef<s32>(r + 0xD0) + 1;
    GRef<s32>(r + 0xD0) = age;
    if (age < GRef<s32>(r + 0xD8) - 1) {
        f32 base;
        if (age == 1) {
            base = 30.0f;
        } else {
            if (age >= 0xC && age % 5 != 0) return 0;
            base = 10.0f;
        }
        const s32 head = GRef<s32>(r + 0xA4);
        const s32 idx = (head + ((GRef<s32>(r + 0xA8) - head + 10) % 10 + 9)) % 10;
        const f32 rnd = NextRandomFloat();
        func_0020C2A0(fpu::Add(rnd * 15.0f, base), r + static_cast<u32>(idx) * 16, r + 0xC0, 0);
        return 0;
    }
    const s32 tail = GRef<s32>(r + 0xA8);
    if (age % 12 == 0) {
        const s32 head = GRef<s32>(r + 0xA4);
        if (head == tail) {
            GRef<u32>(r + 0xD0) = 0;
            return 1;
        }
        GRef<s32>(r + 0xA4) = (head + 1) % 10;
    }
    if (GRef<s32>(r + 0xA4) != tail) return 0;
    GRef<u32>(r + 0xD0) = 0;
    return 1;
}

// EE: 0x0020C380
// Fire by kind +0x2C: kind 0 lasts 120 frames; kind 1 180 frames and kind 2 420 frames, both
// spawning a flash 0x001FC6C0 at its position (size +0x24 x C 0x0040FF60 / 0x0040FF64) at frame
// 27; other kinds
// never finish.
s32 func_0020C380(u32 r) {
    const s32 age = GRef<s32>(r + 0x20) + 1;
    GRef<s32>(r + 0x20) = age;
    const s32 kind = GRef<s32>(r + 0x2C);
    if (kind == 1) {
        if (age >= 0xB5) return 1;
        if (age == 0x1B) func_001FC6C0(F(r + 0x24) * F(0x0040FF60), r, 1);
        return 0;
    }
    if (kind == 0) return age < 0x79 ? 0 : 1;
    if (kind == 2) {
        if (age >= 0x1A5) return 1;
        if (age == 0x1B) func_001FC6C0(F(r + 0x24) * F(0x0040FF64), r, 1);
        return 0;
    }
    return 0;
}

// EE: 0x001E9808
s32 func_001E9808(u32 r) {
    const s16 age = static_cast<s16>(GRef<u16>(r + 0x20) + 1);
    GRef<s16>(r + 0x20) = age;
    return GRef<s16>(r + 0x22) < age ? 1 : 0;
}

// EE: 0x001EB710
s32 func_001EB710(u32 r) {
    const s32 age = GRef<s32>(r + 0x110) + 1;
    GRef<s32>(r + 0x110) = age;
    return age < 0xF1 ? 0 : 1;
}

// EE: 0x001EC4D8
s32 func_001EC4D8(u32 r) {
    const s32 age = GRef<s32>(r + 0x20) + 1;
    GRef<s32>(r + 0x20) = age;
    return GRef<s32>(r + 0x24) < age ? 1 : 0;
}

// EE: 0x001ED3D0
s32 func_001ED3D0(u32 r) {
    const s32 age = GRef<s32>(r + 0x30) + 1;
    GRef<s32>(r + 0x30) = age;
    return age < 0xF1 ? 0 : 1;
}

// EE: 0x001DDC40
s32 func_001DDC40(u32 r) {
    const s32 age = GRef<s32>(r + 0xF0) + 1;
    GRef<s32>(r + 0xF0) = age;
    return GRef<s32>(r + 0xF4) < age ? 1 : 0;
}

// EE: 0x001DF9B8
s32 func_001DF9B8(u32 r) {
    const s32 age = GRef<s32>(r + 0x40) + 1;
    GRef<s32>(r + 0x40) = age;
    return age - GRef<s32>(r + 0x44) < 4 ? 0 : 1;
}

// EE: 0x001CD458
// fpucheck: 1 add
s32 func_001CD458(u32 r) {
    const f32 t = fpu::Add(F(r + 0x20), 1.0f);
    GRef<f32>(r + 0x20) = t;
    return F(r + 0x24) <= t ? 1 : 0;
}

// EE: 0x0019DC78
// fpucheck: 1 add
s32 func_0019DC78(u32 r) {
    const f32 t = F(r + 0x20);
    if (F(r + 0x1C) < t) return 1;
    GRef<f32>(r + 0x20) = fpu::Add(t, 1.0f);
    return 0;
}

// EE: 0x0019C8D0
// Never finishes. fpucheck: 1 add
s32 func_0019C8D0(u32 r) {
    GRef<f32>(r + 0x20) = fpu::Add(F(r + 0x20), 1.0f);
    return 0;
}

// EE: 0x001CF438
// Mission 7/9 one-record swarm (+0x4F0 points at +0x00, per-axis velocity +0x180 and timers +0x300,
// per-point weight +0x490, direction table 0x002B91C0 indexed by the seed +0x4F8, wave table
// 0x002B90BC): finished at 2400 frames. Running axis timers count up; a point whose distance to
// the leader on an axis exceeds 8x its expected spread starts a correction on that axis (leader
// distance x C 0x0040FBE0/E4/E8), which ramps in over 60 frames, holds, then fades after 400 and
// stops at 520 (both scaled by the weight). Every point follows the leader's velocity by its
// weight plus a sideways wave; the leader drifts by C 0x0040FBEC, sinks by C 0x0040FBF0 (faster
// and drifting further after 1800 frames). Points stay below -500 - 10 x weight.
// fpucheck: 40 adds
s32 func_001CF438(u32 r) {
    const s32 n = GRef<s32>(r + 0x4F0);
    GRef<f32>(r + 0x4F4) = fpu::Add(F(r + 0x4F4), 1.0f);
    for (s32 i = 0; i < n; ++i) {
        const u32 t = r + 0x300 + 16u * static_cast<u32>(i);
        for (u32 c = 0; c < 12; c += 4) {
            if (0.0f < F(t + c)) GRef<f32>(t + c) = fpu::Add(F(t + c), 1.0f);
        }
    }
    if (2400.0f <= F(r + 0x4F4)) return 1;
    const u32 dir = 0x002B91C0;
    const u32 wave = 0x002B90BC;
    const u32 wgt = r + 0x490;
    std::vector<f32> d1(n > 0 ? 3 * static_cast<size_t>(n) : 0), d2(d1.size());
    if (n > 0) {
        const f32 x0 = F(r), y0 = F(r + 4), z0 = F(r + 8);
        const f32 w0 = F(wgt);
        const f32 ax = F(r + 0x480), ay = F(r + 0x484), az = F(r + 0x488);
        const u16 seed = GRef<u16>(r + 0x4F8);
        for (s32 i = 0; i < n; ++i) {
            const u32 p = r + 16u * static_cast<u32>(i);
            d1[3 * i + 0] = std::fabs(fpu::Sub(x0, F(p)));
            d1[3 * i + 1] = std::fabs(fpu::Sub(y0, F(p + 4)));
            d1[3 * i + 2] = std::fabs(fpu::Sub(z0, F(p + 8)));
            const u32 di = dir + 16u * (static_cast<u32>(static_cast<s16>(seed) + i) & 0x7F);
            const u32 d0 = dir + 16u * (seed & 0x7Fu);
            const f32 dw = fpu::Sub(w0, F(wgt + 4u * static_cast<u32>(i))) * 28.0f;
            d2[3 * i + 0] = std::fabs(fpu::Add(dw * ax, fpu::Sub(F(d0), F(di)) * 320.0f));
            d2[3 * i + 1] = std::fabs(fpu::Add(dw * ay, fpu::Sub(F(d0 + 4), F(di + 4)) * 320.0f));
            d2[3 * i + 2] = std::fabs(fpu::Add(dw * az, fpu::Sub(F(d0 + 8), F(di + 8)) * 320.0f));
        }
    }
    const f32 lvx = F(r + 0x180);
    const f32 nlvz = -F(r + 0x188);
    for (s32 i = 0; i < n; ++i) {
        const u32 ui = static_cast<u32>(i);
        f32 acc[3] = {0.0f, 0.0f, 0.0f};
        const u32 p = r + 16 * ui;
        if (i != 0) {
            const u32 t = r + 0x300 + 16 * ui, v = r + 0x180 + 16 * ui;
            const u32 kc[3] = {0x0040FBE0, 0x0040FBE4, 0x0040FBE8};
            for (u32 c = 0; c < 3; ++c) {
                if (F(t + 4 * c) == 0.0f && d2[3 * i + c] * 8.0f < d1[3 * i + c]) {
                    GRef<f32>(t + 4 * c) = 1.0f;
                    GRef<f32>(v + 4 * c) = fpu::Sub(F(r + 4 * c), F(p + 4 * c)) * F(kc[c]);
                }
            }
            f32 w0 = F(wgt);
            for (u32 c = 0; c < 3; ++c) {
                const f32 tc = F(t + 4 * c);
                if (!(0.0f < tc)) continue;
                if (tc < 60.0f) {
                    acc[c] = F(v + 4 * c) * tc / 60.0f;
                } else if (400.0f < tc) {
                    const f32 span = fpu::Add(F(wgt + 4 * ui) * 120.0f / w0, 120.0f);
                    acc[c] = F(v + 4 * c) * fpu::Sub(1.0f, fpu::Sub(tc, 400.0f) / span);
                } else {
                    acc[c] = F(v + 4 * c);
                }
                if (fpu::Add(F(wgt + 4 * ui) * 120.0f / w0, 520.0f) <= F(t + 4 * c)) {
                    GRef<u32>(t + 4 * c) = 0;
                    GRef<u32>(v + 4 * c) = 0;
                    w0 = F(wgt);
                }
            }
        }
        const f32 w0 = F(wgt);
        const f32 k = fpu::Add(w0, F(wgt + 4 * ui)) * 0.5f / w0;
        const u32 wa = wave + 4 * (ui & 0xF), wb = wave + 4 * ((ui + 6) & 0xF);
        const f32 sx = fpu::Add(F(wa), F(wb)) * 0.5f * nlvz;
        f32 px = fpu::Add(F(p), fpu::Add(k * F(r + 0x180), sx));
        GRef<f32>(p) = px;
        f32 py = fpu::Add(F(p + 4), k * F(r + 0x184));
        GRef<f32>(p + 4) = py;
        const f32 sz = fpu::Add(F(wa), F(wb)) * 0.5f * lvx;
        f32 pz = fpu::Add(F(p + 8), fpu::Add(k * F(r + 0x188), sz));
        GRef<f32>(p + 8) = pz;
        if (i == 0) {
            const f32 c = F(0x0040FBEC);
            const f32 vz = F(r + 0x188), vx = F(r + 0x180);
            const f32 nx = fpu::Add(F(r), vz * c);
            const f32 nz = fpu::Add(F(r + 8), vx * c);
            const f32 ny = fpu::Sub(F(r + 4), F(0x0040FBF0));
            GRef<f32>(r) = nx;
            GRef<f32>(r + 8) = nz;
            GRef<f32>(r + 4) = ny;
            const f32 tm = F(r + 0x4F4);
            if (1800.0f < tm) {
                const f32 f = fpu::Sub(tm, 1800.0f) / 600.0f * F(0x0040FBF4);
                GRef<f32>(r + 4) = fpu::Sub(ny, 1.0f);
                GRef<f32>(r + 8) = fpu::Add(nz, f * vx);
                GRef<f32>(r) = fpu::Add(nx, f * vz);
            }
        } else {
            GRef<f32>(p) = fpu::Add(px, acc[0]);
            GRef<f32>(p + 4) = fpu::Add(py, acc[1]);
            GRef<f32>(p + 8) = fpu::Add(pz, acc[2]);
        }
        const f32 floor = fpu::Sub(-500.0f, F(wgt + 4 * ui) * 10.0f);
        if (floor < F(p + 4)) GRef<f32>(p + 4) = floor;
    }
    return 0;
}

// EE: 0x001D0B78
// fpucheck: 1 add
s32 func_001D0B78(u32 r) {
    const f32 t = F(r + 0x10);
    if (1200.0f <= t) return 1;
    GRef<f32>(r + 0x10) = fpu::Add(t, 1.0f);
    return 0;
}

// EE: 0x001D4208
s32 func_001D4208(u32) { return 0; }

// EE: 0x001C98E0
// From mission 0x13 on it drifts by +0x10 x C 0x0040FB00 (computed in the scratchpad at +0x510,
// cursor 0x00410BAC reset to its start).
// fpucheck: 1 add
s32 func_001C98E0(u32 r) {
    GRef<f32>(r + 0x20) = fpu::Add(F(r + 0x20), 1.0f);
    if (MissionId() >= 0x13) {
        GRef<u32>(0x00410BAC) = 0x70000000;
        CopyQ(0x70000510, r + 0x10);
        const u32 tmp = W(0x00410BAC) + 0x510;
        ScaleXyz(tmp, tmp, F(0x0040FB00));
        AddXyz(r, r, W(0x00410BAC) + 0x510);
    }
    return F(r + 0x24) <= F(r + 0x20) ? 1 : 0;
}

// EE: 0x001C0060
// Drifts by (0, +0x28 x C 0x0040FA08, 0) turned by its angles +0x10 (0x00102D28 on scratchpad
// vectors at cursor + 0x240 / + 0x280; the cursor is read, not reset). Its compare of +0x2C with 1
// sets a flag nothing reads.
// fpucheck: 1 add
s32 func_001C0060(u32 r) {
    GRef<f32>(r + 0x20) = fpu::Add(F(r + 0x20), 1.0f);
    const u32 cur = W(0x00410BAC);
    GRef<u32>(cur + 0x240) = 0;
    GRef<f32>(cur + 0x24C) = 1.0f;
    GRef<u32>(cur + 0x248) = 0;
    GRef<f32>(cur + 0x244) = F(r + 0x28) * F(0x0040FA08);
    func_00102D28(cur + 0x280, cur + 0x240, r + 0x10);
    AddXyz(r, r, W(0x00410BAC) + 0x280);
    return F(r + 0x24) <= F(r + 0x20) ? 1 : 0;
}

// EE: 0x001D3570
// fpucheck: 1 add
s32 func_001D3570(u32 r) {
    const f32 t = fpu::Add(F(r + 4), 1.0f);
    GRef<f32>(r + 4) = t;
    return F(r + 8) <= t ? 1 : 0;
}

// EE: 0x001BE780
// fpucheck: 1 add
s32 func_001BE780(u32 r) {
    const f32 t = fpu::Add(F(r + 0x30), 1.0f);
    GRef<f32>(r + 0x30) = t;
    return 180.0f <= t ? 1 : 0;
}

// EE: 0x001A1C18
// fpucheck: 1 add
s32 func_001A1C18(u32 r) {
    const f32 t = fpu::Add(F(r + 0x10), 1.0f);
    GRef<f32>(r + 0x10) = t;
    return 120.0f <= t ? 1 : 0;
}

// EE: 0x00184E08
// Particle burst (16 slots: positions +0x000, velocities +0x100): slots first..last (s16 +0x208 /
// +0x20A) move with velocity damped by C 0x0040EDC0; the first live slot retires when the age
// +0x200 is 60 frames past 4 x its index. Finished when first == last.
s32 func_00184E08(u32 r) {
    const s32 age = GRef<s32>(r + 0x200) + 1;
    GRef<s32>(r + 0x200) = age;
    s16 first = GRef<s16>(r + 0x208);
    for (s32 i = first; i <= GRef<s16>(r + 0x20A); ++i) {
        const u32 o = (static_cast<u32>(i) & 0xF) * 16;
        ScaleXyz(r + 0x100 + o, r + 0x100 + o, F(0x0040EDC0));
        AddXyz(r + o, r + o, r + 0x100 + o);
    }
    first = GRef<s16>(r + 0x208);
    const u16 last = GRef<u16>(r + 0x20A);
    u32 v;
    if (static_cast<u32>(age - static_cast<s32>(first) * 4) < 0x3C) {
        v = static_cast<u16>(first) ^ last;
    } else {
        GRef<s16>(r + 0x208) = static_cast<s16>(first + 1);
        v = static_cast<u16>(first + 1) ^ last;
    }
    return (v & 0xFFFF) == 0 ? 1 : 0;
}

// EE: 0x001859E0
// Counts down +0x34 (finished below 0); every +0x38 frames the interval is re-rolled to 4..35
// (1 random read).
// fpucheck: 1 add
s32 func_001859E0(u32 r) {
    const s32 left = GRef<s32>(r + 0x34) - 1;
    GRef<s32>(r + 0x34) = left;
    if (left < 0) return 1;
    const s32 t = GRef<s32>(r + 0x30) + 1;
    GRef<s32>(r + 0x30) = t;
    if (GRef<s16>(r + 0x38) < t) {
        GRef<u32>(r + 0x30) = 0;
        const s32 v = static_cast<s16>(fpu::FloatToInt(fpu::Add(NextRandomFloat(), 1.0f) * 128.0f));
        GRef<s16>(r + 0x38) = static_cast<s16>(v % 32 + 4);
    }
    return 0;
}

// EE: 0x0018ECE0
// Lifetime by kind +0x2A: kind 3 while 0x00410BA4 == 1 lasts C 0x0040EF3C, kinds >= 6 480 frames,
// kind 4 240, the others 144.
// fpucheck: 1 add
s32 func_0018ECE0(u32 r) {
    const s16 kind = GRef<s16>(r + 0x2A);
    const f32 t = F(r + 0x20);
    f32 lim;
    if (kind == 3 && GRef<s32>(0x00410BA4) == 1)
        lim = F(0x0040EF3C);
    else if (kind >= 6)
        lim = 480.0f;
    else
        lim = kind == 4 ? 240.0f : 144.0f;
    if (lim <= t) return 1;
    GRef<f32>(r + 0x20) = fpu::Add(t, 1.0f);
    return 0;
}

// Timer records: finished once the float timer reached the limit, else timer + 1.
// fpucheck: 1 add each
namespace {
s32 CountTo(u32 t, f32 lim) {
    const f32 v = F(t);
    if (lim <= v) return 1;
    GRef<f32>(t) = fpu::Add(v, 1.0f);
    return 0;
}
s32 CountPast(u32 t, f32 lim) {
    const f32 v = fpu::Add(F(t), 1.0f);
    GRef<f32>(t) = v;
    return lim <= v ? 1 : 0;
}
}  // namespace

// EE: 0x0018AFD8
s32 func_0018AFD8(u32 r) { return CountTo(r + 0x10, F(r + 0x14)); }
// EE: 0x00197B20
s32 func_00197B20(u32 r) { return CountPast(r + 0xC, F(r + 0x1C)); }

// EE: 0x0017F738
s32 func_0017F738(u32 r) {
    const s32 t = GRef<s32>(r + 0x20);
    if (t == 3) return 1;
    GRef<s32>(r + 0x20) = t + 1;
    return 0;
}

// EE: 0x00181D10
// 32 frames in missions 4, 8, 12 and 18, else 8.
s32 func_00181D10(u32 r) {
    const u8 m = MissionId();
    const f32 lim = (m == 4 || m == 8 || m == 0xC || m == 0x12) ? 32.0f : 8.0f;
    return CountTo(r + 0x10, lim);
}

// EE: 0x001829A0
s32 func_001829A0(u32 r) { return CountTo(r + 0x20, 9.0f); }
// EE: 0x001997E0
s32 func_001997E0(u32 r) { return CountTo(r + 0xC, F(r + 0x1C)); }
// EE: 0x00186DA8
s32 func_00186DA8(u32 r) { return CountTo(r + 0x10, 1200.0f); }

// EE: 0x00188B88
// Counts +0x34 down to 0 while +0x30 counts up. fpucheck: 2 adds
s32 func_00188B88(u32 r) {
    const f32 left = F(r + 0x34);
    if (left == 0.0f) return 1;
    GRef<f32>(r + 0x34) = fpu::Sub(left, 1.0f);
    GRef<f32>(r + 0x30) = fpu::Add(F(r + 0x30), 1.0f);
    return 0;
}

// EE: 0x00189BF8
s32 func_00189BF8(u32 r) { return CountTo(r + 0x20, 108.0f); }
// EE: 0x00183798
s32 func_00183798(u32 r) { return CountTo(r + 0x20, 10.0f); }
// EE: 0x00196978
s32 func_00196978(u32 r) { return CountTo(r + 0x10, 480.0f); }

// ---- Pool update wrappers -----------------------------------------------------------------------

// EE: 0x00157398
void func_00157398(u32 p) { UpdatePool(p, func_001DB0C0, 0x100, 32); }

// EE: 0x001574E0
// The shared particle pool (4096 x 0x20) has no update function: live particles move by their
// velocity, which is scaled by its own w. Particles are released by their owners.
void func_001574E0(u32 p) {
    for (u32 w = 0; w < 128; ++w) {
        u32 bits = W(p + 0x20000 + 4 * w);
        for (u32 b = 0; bits != 0 && b < 32; ++b, bits >>= 1) {
            if ((bits & 1u) == 0) continue;
            const u32 e = p + (32 * w + b) * 0x20;
            AddXyz(e, e, e + 0x10);
            ScaleXyz(e + 0x10, e + 0x10, F(e + 0x1C));
        }
    }
}

// EE: 0x001575D8
void func_001575D8(u32 p) { UpdatePool(p, func_001F1308, 0x40, 64); }
// EE: 0x00157728
void func_00157728(u32 p) { UpdatePool(p, func_001F3910, 0x1B0, 64); }
// EE: 0x00157880
void func_00157880(u32 p) { UpdatePool(p, func_001F6980, 0x40, 32); }
// EE: 0x001579C8
void func_001579C8(u32 p) { UpdatePool(p, func_001FC828, 0x50, 32); }
// EE: 0x00157B18
void func_00157B18(u32 p) { UpdatePool(p, func_001E45C8, 0x40, 8); }
// EE: 0x00157C68
void func_00157C68(u32 p) { UpdatePool(p, func_001E8008, 0x60, 64); }
// EE: 0x00157DC0
void func_00157DC0(u32 p) { UpdatePool(p, func_001FF7F0, 0x30, 8); }
// EE: 0x00157F10
void func_00157F10(u32 p) { UpdatePool(p, func_001DB0C0, 0x100, 32); }
// EE: 0x00158058
void func_00158058(u32 p) { UpdatePool(p, func_00203488, 0x30, 64); }
// EE: 0x001581B0
void func_001581B0(u32 p) { UpdatePool(p, func_002065F0, 0xE0, 1); }
// EE: 0x00158300
void func_00158300(u32 p) { UpdatePool(p, func_0020A4B8, 0xF0, 16); }
// EE: 0x00158450
void func_00158450(u32 p) { UpdatePool(p, func_0020C380, 0x30, 256); }
// EE: 0x001585A8
void func_001585A8(u32 p) { UpdatePool(p, func_001E9808, 0x50, 8); }
// EE: 0x001586F8
void func_001586F8(u32 p) { UpdatePool(p, func_001EB710, 0x130, 32); }
// EE: 0x00158848
void func_00158848(u32 p) { UpdatePool(p, func_001EC4D8, 0x30, 32); }
// EE: 0x00158998
void func_00158998(u32 p) { UpdatePool(p, func_001ED3D0, 0x40, 8); }
// EE: 0x00158AE8
void func_00158AE8(u32 p) { UpdatePool(p, func_001DDC40, 0x110, 32); }
// EE: 0x00158C38
void func_00158C38(u32 p) { UpdatePool(p, func_001DF9B8, 0x50, 64); }
// EE: 0x00158D90
void func_00158D90(u32 p) { UpdatePool(p, func_001CD458, 0x30, 8); }
// EE: 0x00158EE0
void func_00158EE0(u32 p) { UpdatePool(p, func_0019DC78, 0x30, 384); }
// EE: 0x00159038
void func_00159038(u32 p) { UpdatePool(p, func_0019C8D0, 0x30, 32); }
// EE: 0x00159188
void func_00159188(u32 p) { UpdatePool(p, func_001CF438, 0x500, 1); }
// EE: 0x001592D8
void func_001592D8(u32 p) { UpdatePool(p, func_001D0B78, 0x20, 32); }
// EE: 0x00159420
void func_00159420(u32 p) { UpdatePool(p, func_001D4208, 0x20, 1); }
// EE: 0x00159568
void func_00159568(u32 p) { UpdatePool(p, func_001C98E0, 0x30, 64); }
// EE: 0x001596C0
void func_001596C0(u32 p) { UpdatePool(p, func_001C0060, 0x40, 8); }
// EE: 0x00159810
void func_00159810(u32 p) { UpdatePool(p, func_001D3570, 0x10, 32); }
// EE: 0x00159958
void func_00159958(u32 p) { UpdatePool(p, func_001BE780, 0x40, 32); }
// EE: 0x00159AA0
void func_00159AA0(u32 p) { UpdatePool(p, func_001A1C18, 0x20, 16); }
// EE: 0x00159BF0
void func_00159BF0(u32 p) { UpdatePool(p, func_00184E08, 0x210, 128); }
// EE: 0x00159D58
void func_00159D58(u32 p) { UpdatePool(p, func_001859E0, 0x40, 32); }
// EE: 0x00159EA0
void func_00159EA0(u32 p) { UpdatePool(p, func_0018ECE0, 0x30, 256); }
// EE: 0x00159FF8
void func_00159FF8(u32 p) { UpdatePool(p, func_0018AFD8, 0x20, 8); }
// EE: 0x0015A148
void func_0015A148(u32 p) { UpdatePool(p, func_00197B20, 0x30, 512); }
// EE: 0x0015A2A0
void func_0015A2A0(u32 p) { UpdatePool(p, func_0017F738, 0x40, 32); }
// EE: 0x0015A3E8
void func_0015A3E8(u32 p) { UpdatePool(p, func_00181D10, 0x20, 64); }
// EE: 0x0015A538
void func_0015A538(u32 p) { UpdatePool(p, func_001829A0, 0x30, 16); }
// EE: 0x0015A688
void func_0015A688(u32 p) { UpdatePool(p, func_001997E0, 0x40, 128); }
// EE: 0x0015A7D8
void func_0015A7D8(u32 p) { UpdatePool(p, func_00186DA8, 0x20, 32); }
// EE: 0x0015A920
void func_0015A920(u32 p) { UpdatePool(p, func_00188B88, 0x40, 16); }
// EE: 0x0015AA70
void func_0015AA70(u32 p) { UpdatePool(p, func_00189BF8, 0x30, 256); }
// EE: 0x0015ABC8
void func_0015ABC8(u32 p) { UpdatePool(p, func_00183798, 0x30, 16); }
// EE: 0x0015AD18
void func_0015AD18(u32 p) { UpdatePool(p, func_00196978, 0x20, 8); }

// ---- Ambient spawners run by UpdateEffects -------------------------------------------------------

// EE: 0x001D0A78
// Missions 7 and 9, before the mission end flag: every 600th scene frame (unsigned), with a random
// read <= C 0x0040FC14, a swarm record is started (pool +0x61F00, record 0).
void func_001D0A78() {
    const u8 m = MissionId();
    if (m != 7 && m != 9) return;
    if ((StatusFlags() & 0x10000000u) != 0) return;
    if (W(Scene() + 0x1660) % 600u != 0) return;
    if (F(0x0040FC14) < NextRandomFloat()) return;
    if (func_001D8398(Block() + 0x61F00) == -1) return;
    func_001CF398(Block() + 0x61F00);
}

// EE: 0x001D4110
// Missions 0xF and 0x24: with the mean of 2 random reads below C 0x0040FCD8 (0x24: 0x0040FCD4)
// a record of pool +0x5E8A0 is started (index past the pool -> its last record).
// fpucheck: 2 adds (one per path)
void func_001D4110() {
    const u8 m = MissionId();
    if (m != 0xF && m != 0x24) return;
    const f32 a = NextRandomFloat();
    const f32 s = fpu::Add(a, NextRandomFloat());
    const f32 k = m == 0x24 ? F(0x0040FCD4) : F(0x0040FCD8);
    if (k <= s * 0.5f) return;
    const u32 pool = Block() + 0x5E8A0;
    const s32 i = func_001D8818(pool);
    if (i == -1) return;
    func_001D34F0(static_cast<u32>(i) < 0x20 ? pool + static_cast<u32>(i) * 0x10 : pool + 0x1F0);
}

// EE: 0x001A2B30
// Missions below 0x14 except 8, before the mission end flag and outside sub-scene 0x11: one
// record of pool +0x61420 is started every frame while there is room.
void func_001A2B30() {
    const u8 m = MissionId();
    if (m == 8 || m >= 0x14) return;
    if ((StatusFlags() & 0x10000000u) != 0) return;
    if (Sub() == 0x11) return;
    const u32 pool = Block() + 0x61420;
    const s32 i = func_001A40F8(pool);
    if (i == -1) return;
    func_001A1C08(static_cast<u32>(i) < 0x10 ? pool + static_cast<u32>(i) * 0x20 : pool + 0x1E0);
}

// ---- UpdateEffects -----------------------------------------------------------------------------

// EE: 0x00154DD0
void UpdateEffects() {
    const auto pool = [](u32 off) { return Block() + off; };
    func_00157398(pool(0));
    func_001574E0(pool(0x2010));
    func_001575D8(pool(0x30700));
    func_00157728(pool(0x31710));
    func_00157880(pool(0x38320));
    func_001579C8(pool(0x22220));
    func_00157B18(pool(0x22C30));
    func_00157C68(pool(0x22E40));
    func_00157DC0(pool(0x24650));
    func_00157F10(pool(0x247E0));
    func_00158058(pool(0x268E0));
    func_001581B0(pool(0x267F0));
    func_00158300(pool(0x274F0));
    func_00158450(pool(0x28400));
    func_001585A8(pool(0x2B430));
    func_001586F8(pool(0x2B6C0));
    func_00158848(pool(0x2DCD0));
    func_00158998(pool(0x2E2E0));
    func_00158AE8(pool(0x2E4F0));
    func_00158C38(pool(0x38B30));
    func_001D0A78();
    func_001D4110();
    if (GRef<u8>(Block() + 0x62451) != 0) func_001A2B30();
    func_00158D90(pool(0x61940));
    func_00158EE0(pool(0x3FF90));
    func_00159038(pool(0x5FEE0));
    func_00159188(pool(0x61F00));
    func_001592D8(pool(0x5E490));
    func_00159420(pool(0x62410));
    func_00159568(pool(0x5D880));
    func_001596C0(pool(0x61BE0));
    func_00159810(pool(0x5E8A0));
    func_00159958(pool(0x5F2C0));
    func_00159AA0(pool(0x61420));
    func_00159BF0(pool(0x4A830));
    func_00159D58(pool(0x604F0));
    func_00159EA0(pool(0x447D0));
    func_00159FF8(pool(0x61AD0));
    func_0015A148(pool(0x39F40));
    func_0015A2A0(pool(0x5EAB0));
    func_0015A3E8(pool(0x5D070));
    func_0015A538(pool(0x60D00));
    func_0015A688(pool(0x5B050));
    func_0015A7D8(pool(0x5FAD0));
    func_0015A920(pool(0x61010));
    func_0015AA70(pool(0x47800));
    func_0015ABC8(pool(0x61630));
    func_0015AD18(pool(0x61DF0));
}

// ---- Lowest-free allocators (one per pool shape; argument: the pool) -----------------------------

// EE: 0x0019F518
s32 func_0019F518(u32 p) { return AllocSlot(p, 32, 0x404); }
// EE: 0x0019F610
s32 func_0019F610(u32 p) { return AllocSlot(p, 16, 0x404); }
// EE: 0x0019F710
s32 func_0019F710(u32 p) { return AllocSlot(p, 256, 0x3020); }
// EE: 0x0019F810
s32 func_0019F810(u32 p) { return AllocSlot(p, 8, 0x104); }
// EE: 0x0019F910
s32 func_0019F910(u32 p) { return AllocSlot(p, 256, 0x3020); }
// EE: 0x0019FA10
s32 func_0019FA10(u32 p) { return AllocSlot(p, 8, 0x104); }
// EE: 0x0019FB10
s32 func_0019FB10(u32 p) { return AllocSlot(p, 512, 0x6040); }
// EE: 0x0019FC10
s32 func_0019FC10(u32 p) { return AllocSlot(p, 128, 0x2010); }
// EE: 0x0019FD98
s32 func_0019FD98(u32 p) { return AllocSlot(p, 32, 0x604); }
// EE: 0x0019FE90
s32 func_0019FE90(u32 p) { return AllocSlot(p, 384, 0x4830); }
// EE: 0x001A40F8
s32 func_001A40F8(u32 p) { return AllocSlot(p, 16, 0x204); }
// EE: 0x00184828
s32 func_00184828(u32 p) { return AllocSlot(p, 32, 0x804); }
// EE: 0x00184920
s32 func_00184920(u32 p) { return AllocSlot(p, 64, 0x808); }
// EE: 0x00184A20
s32 func_00184A20(u32 p) { return AllocSlot(p, 16, 0x304); }
// EE: 0x00184B20
s32 func_00184B20(u32 p) { return AllocSlot(p, 16, 0x304); }
// EE: 0x00186B78
s32 func_00186B78(u32 p) { return AllocSlot(p, 128, 0x10810); }
// EE: 0x00186CA8
s32 func_00186CA8(u32 p) { return AllocSlot(p, 32, 0x804); }
// EE: 0x001D7FA0
s32 func_001D7FA0(u32 p) { return AllocSlot(p, 32, 0x804); }
// EE: 0x001D8198
s32 func_001D8198(u32 p) { return AllocSlot(p, 64, 0xC08); }
// EE: 0x001D8298
s32 func_001D8298(u32 p) { return AllocSlot(p, 8, 0x184); }
// EE: 0x001D8398
s32 func_001D8398(u32 p) { return AllocSlot(p, 1, 0x504); }
// EE: 0x001D8490
s32 func_001D8490(u32 p) { return AllocSlot(p, 32, 0x404); }
// EE: 0x001D8818
s32 func_001D8818(u32 p) { return AllocSlot(p, 32, 0x204); }
// EE: 0x001E0608
s32 func_001E0608(u32 p) { return AllocSlot(p, 32, 0x2004); }
// EE: 0x001E0700
s32 func_001E0700(u32 p) { return AllocSlot(p, 4096, 0x20200); }
// EE: 0x001E0828
s32 func_001E0828(u32 p) { return AllocSlot(p, 32, 0x2204); }
// EE: 0x001E0920
s32 func_001E0920(u32 p) { return AllocSlot(p, 64, 0x1408); }
// EE: 0x001E92C0
s32 func_001E92C0(u32 p) { return AllocSlot(p, 8, 0x204); }
// EE: 0x001E93C0
s32 func_001E93C0(u32 p) { return AllocSlot(p, 64, 0x1808); }
// EE: 0x001FEE28
s32 func_001FEE28(u32 p) { return AllocSlot(p, 8, 0x284); }
// EE: 0x001FEF28
s32 func_001FEF28(u32 p) { return AllocSlot(p, 32, 0x2604); }
// EE: 0x001FF020
s32 func_001FF020(u32 p) { return AllocSlot(p, 32, 0x604); }
// EE: 0x001FF118
s32 func_001FF118(u32 p) { return AllocSlot(p, 8, 0x204); }
// EE: 0x001FF218
s32 func_001FF218(u32 p) { return AllocSlot(p, 64, 0x1008); }
// EE: 0x001FF318
s32 func_001FF318(u32 p) { return AllocSlot(p, 64, 0x6C08); }
// EE: 0x001FF418
s32 func_001FF418(u32 p) { return AllocSlot(p, 32, 0x804); }
// EE: 0x001FF510
s32 func_001FF510(u32 p) { return AllocSlot(p, 32, 0xA04); }
// EE: 0x00210670
s32 func_00210670(u32 p) { return AllocSlot(p, 8, 0x184); }
// EE: 0x00210770
s32 func_00210770(u32 p) { return AllocSlot(p, 32, 0x2004); }
// EE: 0x00210868
// Dead (no reference): lowest free record of a 0x1000-record pool (masks +0x20000, counter
// +0x20200).
s32 func_00210868(u32 p) { return AllocSlot(p, 0x1000, 0x20200); }
// EE: 0x00210990
s32 func_00210990(u32 p) { return AllocSlot(p, 64, 0xC08); }
// EE: 0x00210A90
s32 func_00210A90(u32 p) { return AllocSlot(p, 1, 0xE4); }
// EE: 0x00210B88
s32 func_00210B88(u32 p) { return AllocSlot(p, 16, 0xF04); }
// EE: 0x00210C88
s32 func_00210C88(u32 p) { return AllocSlot(p, 256, 0x3020); }

}  // namespace ac04::game
