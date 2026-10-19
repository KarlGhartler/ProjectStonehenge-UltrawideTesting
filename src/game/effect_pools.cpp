// Effect pools: the effect block (*0x004106B4, 0x625A0 bytes) and its reset at every mission start
// and retry (InitEffects). docs/subsystems/mission-load.md "Effect-pool resets" and "Per-record init
// functions"; random reads: docs/game/random.md (table reads r, newlib rand R, the VU0 R seed).
//
// A pool is `count` records of `stride` bytes, then an occupancy mask of ceil(count / 32) words and
// a live counter word. The reset initialises the records in ascending order, clears the mask from
// its highest word down and zeroes the counter.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/vu0_math.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

AC04_GUEST_VAR(u32, g_EffectBlock, 0x004106B4)

namespace {

constexpr u32 kEffectTables = 0x002B8530;  // 16 pointers into mission block 30 (func_00184C20)

u8 MissionId() { return GRef<u8>(g_GameState().addr + 0x27); }

// Effect frame clock word of the block (HYPOTHESIS: not written before the pool resets read it).
u32 EffectClock() { return GRef<u32>(g_EffectBlock() + 0x6249C); }

// cvt((r + 1) * 128): the index pattern shared by several inits.
s32 RandomByteIndex(f32 r) {
    return fpu::FloatToInt(fpu::Add(r, 1.0f) * 128.0f);
}

void ResetPool(u32 p, void (*init)(u32), u32 stride, u32 count, u32 mask_words) {
    u32 rec = p;
    for (u32 i = 0; i < count; ++i) {
        if (init != nullptr) init(rec);
        rec += stride;
    }
    const u32 mask = p + count * stride;
    for (s32 w = static_cast<s32>(mask_words) - 1; w >= 0; --w) GRef<u32>(mask + 4u * static_cast<u32>(w)) = 0;
    GRef<u32>(mask + 4 * mask_words) = 0;
}

}  // namespace

// ---- Per-record init functions (in pool order) ---------------------------------------------------

// EE: 0x001DB0A8
void func_001DB0A8(u32 r) {
    GRef<u32>(r + 0xF4) = 0;
    GRef<u32>(r + 0xF0) = 0;
    GRef<u32>(r + 0xE8) = 0;
    GRef<u32>(r + 0xEC) = 0;
}

// EE: 0x001F1300
void func_001F1300(u32 r) { GRef<u32>(r + 0x20) = 0; }

// EE: 0x001F38F0
void func_001F38F0(u32 r) {
    GRef<u32>(r + 0x198) = 0;
    GRef<u32>(r + 0x134) = 0;
    GRef<u32>(r + 0x138) = 0;
    GRef<u32>(r + 0x184) = 0;
    GRef<u32>(r + 0x188) = 0;
}

// EE: 0x001F6978
void func_001F6978(u32 r) { GRef<u32>(r + 0x24) = 0; }

// EE: 0x001FC818
void func_001FC818(u32 r) {
    GRef<u16>(r + 0x10) = 0;
    GRef<u16>(r + 0x12) = 0;
}

// EE: 0x001E4588
void func_001E4588(u32 r) {
    GRef<u8>(r + 0x39) = 0;
    GRef<u8>(r + 0x3A) = 0;
    GRef<u32>(r + 0x24) = 0;
    for (u32 o = 0x34; o >= 0x28; o -= 4) GRef<u32>(r + o) = 0;
}

// EE: 0x001E7FF0
void func_001E7FF0(u32 r) {
    GRef<u16>(r + 0x50) = 0;
    GRef<u32>(r + 0x48) = 0;
    GRef<u32>(r + 0x4C) = 0;
}

// EE: 0x001FF7E8
void func_001FF7E8(u32 r) { GRef<u16>(r + 0x20) = 0; }

// EE: 0x00203480
void func_00203480(u32 r) { GRef<u32>(r + 0x10) = 0; }

// EE: 0x00206588
void func_00206588(u32 r) {
    GRef<u32>(r + 0xC4) = 0;
    GRef<u32>(r + 0xC8) = 0;
    GRef<u32>(r + 0xC0) = 0;
    GRef<u32>(r + 0xB8) = 0;
    GRef<u32>(r + 0xBC) = 0;
    const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * GRef<f32>(0x0040FF10));
    GRef<u32>(r + 0xD4) = 0xFFFFFFFFu;
    GRef<u32>(r + 0x30) = v + EffectClock();
}

// EE: 0x0020A4A0
void func_0020A4A0(u32 r) {
    GRef<u32>(r + 0xA4) = 0;
    GRef<u32>(r + 0xD0) = 0;
    GRef<u32>(r + 0xD4) = 0;
    GRef<u32>(r + 0xA8) = 0;
}

// EE: 0x0020C378
void func_0020C378(u32 r) { GRef<u32>(r + 0x20) = 0; }

// EE: 0x001E97F0
void func_001E97F0(u32 r) {
    GRef<u16>(r + 0x20) = 0;
    GRef<u32>(r + 0x40) = 0;
    GRef<u16>(r + 0x22) = 0;
    GRef<u32>(r + 0x44) = 0;
}

// EE: 0x001EB708
void func_001EB708(u32 r) { GRef<u32>(r + 0x110) = 0; }

// EE: 0x001EC458
// fpucheck: 1 add (r * k + T, T converted as unsigned; the convert's own add is inside UnsignedToFloat)
void func_001EC458(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    GRef<u32>(r + 0x24) = 0;
    const f32 t = NextRandomFloat() * GRef<f32>(0x0040FE1C);
    GRef<u32>(r + 0x28) = fpu::FixUnsSfSi(fpu::Add(t, fpu::UnsignedToFloat(EffectClock())));
}

// EE: 0x001ED3C8
void func_001ED3C8(u32 r) { GRef<u32>(r + 0x30) = 0; }

// EE: 0x001DDBD0
void func_001DDBD0(u32 r) {
    GRef<u32>(r + 0xF0) = 0;
    GRef<u32>(r + 0xF4) = 0;
    GRef<u32>(r + 0xC8) = 0;
    GRef<u32>(r + 0xC4) = 0;
    GRef<u32>(r + 0x100) = 0;
    GRef<u32>(r + 0x104) = 12;
    GRef<u32>(r + 0xF8) = 60;
    const u32 v = fpu::FixUnsSfSi(NextRandomFloat() * GRef<f32>(0x0040FDB8));
    GRef<u32>(r + 0xFC) = v + EffectClock();
}

// EE: 0x001DF9A0
void func_001DF9A0(u32 r) {
    GRef<u32>(r + 0x44) = 0;
    GRef<u32>(r + 0x38) = 0;
    GRef<u32>(r + 0x34) = 0;
    GRef<u32>(r + 0x40) = 0;
}

// EE: 0x001CD400
void func_001CD400(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    const s32 a = ee::rand();
    const s32 b = ee::rand();
    GRef<u16>(r + 0x2C) = static_cast<u16>((a + b) & 0x7F);
    GRef<f32>(r + 0x28) = NextRandomFloat();
    GRef<u32>(r + 0x24) = 0x43B40000;  // 360.0f
}

// EE: 0x0019D9C0
// The kind comes from the s16 at +0x2E left in the record by earlier use (not reset here).
// fpucheck: 3 adds (r * k + 1 per path / the 0x60 path's two adds, r + 1 of the angle)
void func_0019D9C0(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    const s16 kind = GRef<s16>(r + 0x2E);
    f32 frames;
    if (kind == 0x60) {
        const f32 x = fpu::Add(NextRandomFloat() * 0.0f, 1.0f);
        frames = fpu::Add(x, x) * 60.0f;
    } else {
        u32 k_addr;
        f32 n;
        switch (kind) {
            case 0x00: k_addr = 0x0040F36C; n = 10.0f; break;
            case 0xD2: k_addr = 0x0040F388; n = 22.0f; break;
            case 0xD3: k_addr = 0x0040F384; n = 22.0f; break;
            case 0xD4: k_addr = 0x0040F378; n = 13.0f; break;
            case 0xD5: k_addr = 0x0040F374; n = 12.0f; break;
            case 0xDF: k_addr = 0x0040F364; n = GRef<f32>(0x0040F368); break;
            case 0xE0: k_addr = 0x0040F37C; n = 14.0f; break;
            case 0xE1: k_addr = 0x0040F380; n = 16.0f; break;
            case 0xE7: k_addr = 0x0040F370; n = 10.0f; break;
            default:   k_addr = 0x0040F38C; n = 18.0f; break;  // 0xD0, 0xE6 and every other kind
        }
        const f32 v = NextRandomFloat() * GRef<f32>(k_addr);
        frames = fpu::Add(v, 1.0f) * n * 60.0f;
    }
    GRef<f32>(r + 0x1C) = frames;
    GRef<s16>(r + 0x2C) = static_cast<s16>(RandomByteIndex(NextRandomFloat()) - 0x80);
}

// EE: 0x0019C888
void func_0019C888(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    const s32 a = ee::rand();
    const s32 b = ee::rand();
    GRef<u32>(r + 0x28) = static_cast<u32>(a + b) & 0x7F;
}

// EE: 0x001CF398
// The loop over +0x4F0 entries never runs: the count was just set to -1.
void func_001CF398(u32 r) {
    GRef<u32>(r + 0x4F4) = 0;
    GRef<u32>(r + 0x4F0) = 0xFFFFFFFFu;
    const s32 a = ee::rand();
    const s32 b = ee::rand();
    const s32 c = ee::rand();
    s32 n = GRef<s32>(r + 0x4F0);
    GRef<u16>(r + 0x4F8) = static_cast<u16>((a + b + c) & 0x1FF);
    for (u32 e = r; n > 0; --n, e += 0x10) {
        for (u32 o = 0; o < 0x10; o += 4) {
            GRef<u32>(e + 0x180 + o) = 0;
            GRef<u32>(e + 0x300 + o) = 0;
        }
    }
}

// EE: 0x001D0B40
void func_001D0B40(u32 r) {
    GRef<u32>(r + 0x10) = 0;
    GRef<s16>(r + 0x18) = static_cast<s16>(ee::rand() >> 15);
    GRef<f32>(r + 0x14) = NextRandomFloat();
}

// EE: 0x001D4200
void func_001D4200(u32 r) { GRef<u32>(r + 0x10) = 0; }

// EE: 0x001C9838
// fpucheck: 2 adds (f + f, f + 2f * r)
void func_001C9838(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    const s32 a = ee::rand();
    const s32 b = ee::rand();
    GRef<s16>(r + 0x2C) = static_cast<s16>((a + b) / 2);
    GRef<f32>(r + 0x28) = NextRandomFloat();
    if (MissionId() < 0x13) {
        GRef<u32>(r + 0x24) = 0x43340000;  // 180.0f
    } else {
        const f32 x = NextRandomFloat();
        const f32 f = GRef<f32>(GRef<u32>(kEffectTables + 8) + 0x20);
        GRef<f32>(r + 0x24) = fpu::Add(f, fpu::Add(f, f) * x) * 60.0f;
    }
}

// EE: 0x001C0010
void func_001C0010(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    const s32 a = ee::rand();
    const s32 b = ee::rand();
    GRef<u16>(r + 0x30) = static_cast<u16>((a + b) & 0x1FFF);
    GRef<u32>(r + 0x24) = 0x43700000;  // 240.0f
}

// EE: 0x001D34F0
// fpucheck: 2 adds (r + r, 4 - 2r)
void func_001D34F0(u32 r) {
    GRef<u32>(r + 4) = 0;
    GRef<u32>(r + 0) = 0xC2C80000;  // -100.0f
    const s32 a = ee::rand();
    const s32 b = ee::rand();
    GRef<s16>(r + 0xC) = static_cast<s16>((a + b) / 2);
    const f32 x = NextRandomFloat();
    GRef<f32>(r + 8) = fpu::Sub(4.0f, fpu::Add(x, x)) * 60.0f;
}

// EE: 0x001BE778
void func_001BE778(u32 r) { GRef<u32>(r + 0x30) = 0; }

// EE: 0x001A1C08
void func_001A1C08(u32 r) {
    GRef<u32>(r + 0x10) = 0;
    GRef<u16>(r + 0x14) = 0xFFFF;
}

// EE: 0x00184DF0
void func_00184DF0(u32 r) {
    GRef<u16>(r + 0x208) = 0;
    GRef<u32>(r + 0x204) = 0;
    GRef<u32>(r + 0x200) = 0;
    GRef<u16>(r + 0x20A) = 0;
}

// EE: 0x001859D8
void func_001859D8(u32 r) { GRef<u32>(r + 0x30) = 0; }

// EE: 0x0018EC70
// fpucheck: 1 add (r + 1)
void func_0018EC70(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    const s32 v = static_cast<s16>(RandomByteIndex(NextRandomFloat()));
    GRef<u16>(r + 0x28) = static_cast<u16>(v % 0x43);
    GRef<f32>(r + 0x24) = NextRandomFloat();
}

// EE: 0x0018AF40
// fpucheck: 1 add (r + 1). The 300.0f store of mission 4 is always overwritten by 132.0f.
void func_0018AF40(u32 r) {
    GRef<u32>(r + 0x10) = 0;
    const s32 v = static_cast<s16>(RandomByteIndex(NextRandomFloat()));
    GRef<u16>(r + 0x18) = static_cast<u16>(v % 0x43);
    if (MissionId() == 4) GRef<u32>(r + 0x14) = 0x43960000;  // 300.0f
    GRef<u32>(r + 0x14) = 0x43040000;                         // 132.0f
}

// EE: 0x00197A98
// fpucheck: 3 adds (0.5 - r, + 192, r + 1)
void func_00197A98(u32 r) {
    GRef<u32>(r + 0xC) = 0;
    const f32 a = NextRandomFloat();
    GRef<f32>(r + 0x1C) = fpu::Add(fpu::Sub(0.5f, a) * 192.0f * 0.25f, 192.0f);
    GRef<f32>(r + 0x20) = NextRandomFloat();
    GRef<s32>(r + 0x24) = RandomByteIndex(NextRandomFloat()) - 0x80;
}

// EE: 0x0017F6D8
// fpucheck: 1 add (r + 1)
void func_0017F6D8(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    GRef<s32>(r + 0x28) = RandomByteIndex(NextRandomFloat()) % 0x2B;
}

// EE: 0x00181CB8
// fpucheck: 1 add (r + 1)
void func_00181CB8(u32 r) {
    GRef<u32>(r + 0x10) = 0;
    GRef<s32>(r + 0x14) = RandomByteIndex(NextRandomFloat()) - 0x80;
    GRef<f32>(r + 0x18) = NextRandomFloat();
}

// EE: 0x00182948
// fpucheck: 1 add (r + 1)
void func_00182948(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    GRef<s32>(r + 0x24) = RandomByteIndex(NextRandomFloat()) - 0x80;
    GRef<f32>(r + 0x28) = NextRandomFloat();
}

// EE: 0x00199788
// fpucheck: 1 add (r + 1)
void func_00199788(u32 r) {
    GRef<u32>(r + 0xC) = 0;
    GRef<f32>(r + 0x20) = NextRandomFloat();
    GRef<s32>(r + 0x30) = RandomByteIndex(NextRandomFloat()) - 0x80;
}

// EE: 0x00186DA0
void func_00186DA0(u32 r) { GRef<u32>(r + 0x10) = 0; }

// EE: 0x00188B18
// fpucheck: 1 add (r + 1)
void func_00188B18(u32 r) {
    GRef<u32>(r + 0x30) = 0;
    const s32 v = static_cast<s16>(RandomByteIndex(NextRandomFloat()));
    GRef<u16>(r + 0x3C) = static_cast<u16>(v % 0x43);
    GRef<f32>(r + 0x38) = NextRandomFloat();
}

// EE: 0x00189B88
// fpucheck: 1 add (r + 1)
void func_00189B88(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    const s32 v = static_cast<s16>(RandomByteIndex(NextRandomFloat()));
    GRef<u16>(r + 0x2C) = static_cast<u16>(v % 0x43);
    GRef<f32>(r + 0x24) = NextRandomFloat();
}

// EE: 0x00183760
void func_00183760(u32 r) {
    GRef<u32>(r + 0x20) = 0;
    GRef<u32>(r + 0x28) = static_cast<u32>(ee::rand() >> 15) & 0x7F;
}

// EE: 0x00196908
// fpucheck: 1 add (r + 1)
void func_00196908(u32 r) {
    GRef<u32>(r + 0x10) = 0;
    const s32 v = static_cast<s16>(RandomByteIndex(NextRandomFloat()));
    GRef<s32>(r + 0x18) = v % 0x43;
    GRef<f32>(r + 0x14) = NextRandomFloat();
}

// ---- Pool resets (wrappers 0x00155BC8...0x00157310) -----------------------------------------------

// EE: 0x00155BC8
void func_00155BC8(u32 p) { ResetPool(p, func_001DB0A8, 0x100, 32, 1); }
// EE: 0x00155C50
// No per-record init (empty constructor): only an empty 4,096-iteration loop is left.
void func_00155C50(u32 p) { ResetPool(p, nullptr, 0x20, 4096, 128); }
// EE: 0x00155CB8
void func_00155CB8(u32 p) { ResetPool(p, func_001F1300, 0x40, 64, 2); }
// EE: 0x00155D40
void func_00155D40(u32 p) { ResetPool(p, func_001F38F0, 0x1B0, 64, 2); }
// EE: 0x00155DC8
void func_00155DC8(u32 p) { ResetPool(p, func_001F6978, 0x40, 32, 1); }
// EE: 0x00155E50
void func_00155E50(u32 p) { ResetPool(p, func_001FC818, 0x50, 32, 1); }
// EE: 0x00155ED8
void func_00155ED8(u32 p) { ResetPool(p, func_001E4588, 0x40, 8, 1); }
// EE: 0x00155F60
void func_00155F60(u32 p) { ResetPool(p, func_001E7FF0, 0x60, 64, 2); }
// EE: 0x00155FE8
void func_00155FE8(u32 p) { ResetPool(p, func_001FF7E8, 0x30, 8, 1); }
// EE: 0x00156070
void func_00156070(u32 p) { ResetPool(p, func_001DB0A8, 0x100, 32, 1); }
// EE: 0x001560F8
void func_001560F8(u32 p) { ResetPool(p, func_00203480, 0x30, 64, 2); }
// EE: 0x00156180
void func_00156180(u32 p) { ResetPool(p, func_00206588, 0xE0, 1, 1); }
// EE: 0x00156208
void func_00156208(u32 p) { ResetPool(p, func_0020A4A0, 0xF0, 16, 1); }
// EE: 0x00156290
void func_00156290(u32 p) { ResetPool(p, func_0020C378, 0x30, 256, 8); }
// EE: 0x00156318
void func_00156318(u32 p) { ResetPool(p, func_001E97F0, 0x50, 8, 1); }
// EE: 0x001563A0
void func_001563A0(u32 p) { ResetPool(p, func_001EB708, 0x130, 32, 1); }
// EE: 0x00156428
void func_00156428(u32 p) { ResetPool(p, func_001EC458, 0x30, 32, 1); }
// EE: 0x001564B0
void func_001564B0(u32 p) { ResetPool(p, func_001ED3C8, 0x40, 8, 1); }
// EE: 0x00156538
void func_00156538(u32 p) { ResetPool(p, func_001DDBD0, 0x110, 32, 1); }
// EE: 0x001565C0
void func_001565C0(u32 p) { ResetPool(p, func_001DF9A0, 0x50, 64, 2); }
// EE: 0x00156648
void func_00156648(u32 p) { ResetPool(p, func_001CD400, 0x30, 8, 1); }
// EE: 0x001566D0
void func_001566D0(u32 p) { ResetPool(p, func_0019D9C0, 0x30, 384, 12); }
// EE: 0x00156758
void func_00156758(u32 p) { ResetPool(p, func_0019C888, 0x30, 32, 1); }
// EE: 0x001567E0
void func_001567E0(u32 p) { ResetPool(p, func_001CF398, 0x500, 1, 1); }
// EE: 0x00156868
void func_00156868(u32 p) { ResetPool(p, func_001D0B40, 0x20, 32, 1); }
// EE: 0x001568F0
void func_001568F0(u32 p) { ResetPool(p, func_001D4200, 0x20, 1, 1); }
// EE: 0x00156978
void func_00156978(u32 p) { ResetPool(p, func_001C9838, 0x30, 64, 2); }
// EE: 0x00156A00
void func_00156A00(u32 p) { ResetPool(p, func_001C0010, 0x40, 8, 1); }
// EE: 0x00156A88
void func_00156A88(u32 p) { ResetPool(p, func_001D34F0, 0x10, 32, 1); }
// EE: 0x00156B10
void func_00156B10(u32 p) { ResetPool(p, func_001BE778, 0x40, 32, 1); }
// EE: 0x00156B98
void func_00156B98(u32 p) { ResetPool(p, func_001A1C08, 0x20, 16, 1); }
// EE: 0x00156C20
void func_00156C20(u32 p) { ResetPool(p, func_00184DF0, 0x210, 128, 4); }
// EE: 0x00156CB0
void func_00156CB0(u32 p) { ResetPool(p, func_001859D8, 0x40, 32, 1); }
// EE: 0x00156D38
void func_00156D38(u32 p) { ResetPool(p, func_0018EC70, 0x30, 256, 8); }
// EE: 0x00156DC0
void func_00156DC0(u32 p) { ResetPool(p, func_0018AF40, 0x20, 8, 1); }
// EE: 0x00156E48
void func_00156E48(u32 p) { ResetPool(p, func_00197A98, 0x30, 512, 16); }
// EE: 0x00156ED0
void func_00156ED0(u32 p) { ResetPool(p, func_0017F6D8, 0x40, 32, 1); }
// EE: 0x00156F58
void func_00156F58(u32 p) { ResetPool(p, func_00181CB8, 0x20, 64, 2); }
// EE: 0x00156FE0
void func_00156FE0(u32 p) { ResetPool(p, func_00182948, 0x30, 16, 1); }
// EE: 0x00157068
void func_00157068(u32 p) { ResetPool(p, func_00199788, 0x40, 128, 4); }
// EE: 0x001570F0
void func_001570F0(u32 p) { ResetPool(p, func_00186DA0, 0x20, 32, 1); }
// EE: 0x00157178
void func_00157178(u32 p) { ResetPool(p, func_00188B18, 0x40, 16, 1); }
// EE: 0x00157200
void func_00157200(u32 p) { ResetPool(p, func_00189B88, 0x30, 256, 8); }
// EE: 0x00157288
void func_00157288(u32 p) { ResetPool(p, func_00183760, 0x30, 16, 1); }
// EE: 0x00157310
void func_00157310(u32 p) { ResetPool(p, func_00196908, 0x20, 8, 1); }

// ---- Thunks (one `j wrapper` each; InitEffects calls these) ----------------------------------------

// EE: 0x0015E648
void thunk_func_00157310(u32 p) { func_00157310(p); }
// EE: 0x0015E650
void thunk_func_00157288(u32 p) { func_00157288(p); }
// EE: 0x0015E658
void thunk_func_00157200(u32 p) { func_00157200(p); }
// EE: 0x0015E660
void thunk_func_00157178(u32 p) { func_00157178(p); }
// EE: 0x0015E668
void thunk_func_001570F0(u32 p) { func_001570F0(p); }
// EE: 0x0015E670
void thunk_func_00157068(u32 p) { func_00157068(p); }
// EE: 0x0015E678
void thunk_func_00156FE0(u32 p) { func_00156FE0(p); }
// EE: 0x0015E680
void thunk_func_00156F58(u32 p) { func_00156F58(p); }
// EE: 0x0015E688
void thunk_func_00156ED0(u32 p) { func_00156ED0(p); }
// EE: 0x0015E690
void thunk_func_00156E48(u32 p) { func_00156E48(p); }
// EE: 0x0015E698
void thunk_func_00156DC0(u32 p) { func_00156DC0(p); }
// EE: 0x0015E6A0
void thunk_func_00156D38(u32 p) { func_00156D38(p); }
// EE: 0x0015E6A8
void thunk_func_00156CB0(u32 p) { func_00156CB0(p); }
// EE: 0x0015E6B0
void thunk_func_00156C20(u32 p) { func_00156C20(p); }
// EE: 0x0015E6B8
void thunk_func_00156B98(u32 p) { func_00156B98(p); }
// EE: 0x0015E6C0
void thunk_func_00156B10(u32 p) { func_00156B10(p); }
// EE: 0x0015E6C8
void thunk_func_00156A88(u32 p) { func_00156A88(p); }
// EE: 0x0015E6D0
void thunk_func_00156A00(u32 p) { func_00156A00(p); }
// EE: 0x0015E6D8
void thunk_func_00156978(u32 p) { func_00156978(p); }
// EE: 0x0015E6E0
void thunk_func_001568F0(u32 p) { func_001568F0(p); }
// EE: 0x0015E6E8
void thunk_func_00156868(u32 p) { func_00156868(p); }
// EE: 0x0015E6F0
void thunk_func_001567E0(u32 p) { func_001567E0(p); }
// EE: 0x0015E6F8
void thunk_func_00156758(u32 p) { func_00156758(p); }
// EE: 0x0015E700
void thunk_func_001566D0(u32 p) { func_001566D0(p); }
// EE: 0x0015E708
void thunk_func_00156648(u32 p) { func_00156648(p); }
// EE: 0x0015E710
void thunk_func_001565C0(u32 p) { func_001565C0(p); }
// EE: 0x0015E718
void thunk_func_00156538(u32 p) { func_00156538(p); }
// EE: 0x0015E720
void thunk_func_001564B0(u32 p) { func_001564B0(p); }
// EE: 0x0015E728
void thunk_func_00156428(u32 p) { func_00156428(p); }
// EE: 0x0015E730
void thunk_func_001563A0(u32 p) { func_001563A0(p); }
// EE: 0x0015E738
void thunk_func_00156318(u32 p) { func_00156318(p); }
// EE: 0x0015E740
void thunk_func_00156290(u32 p) { func_00156290(p); }
// EE: 0x0015E748
void thunk_func_00156208(u32 p) { func_00156208(p); }
// EE: 0x0015E750
void thunk_func_00156180(u32 p) { func_00156180(p); }
// EE: 0x0015E758
void thunk_func_001560F8(u32 p) { func_001560F8(p); }
// EE: 0x0015E760
void thunk_func_00156070(u32 p) { func_00156070(p); }
// EE: 0x0015E768
void thunk_func_00155FE8(u32 p) { func_00155FE8(p); }
// EE: 0x0015E770
void thunk_func_00155F60(u32 p) { func_00155F60(p); }
// EE: 0x0015E778
void thunk_func_00155ED8(u32 p) { func_00155ED8(p); }
// EE: 0x0015E780
void thunk_func_00155E50(u32 p) { func_00155E50(p); }
// EE: 0x0015E788
void thunk_func_00155DC8(u32 p) { func_00155DC8(p); }
// EE: 0x0015E790
void thunk_func_00155D40(u32 p) { func_00155D40(p); }
// EE: 0x0015E798
void thunk_func_00155CB8(u32 p) { func_00155CB8(p); }
// EE: 0x0015E7A0
void thunk_func_00155C50(u32 p) { func_00155C50(p); }
// EE: 0x0015E7A8
void thunk_func_00155BC8(u32 p) { func_00155BC8(p); }

// ---- Block allocation and InitEffects -------------------------------------------------------------

// EE: 0x00154638
void AllocEffectBlock() {
    g_EffectBlock() = WorkHeapAlloc(0x625A0, 0x10);
}

// EE: 0x00155AE0
void func_00155AE0() {}

// EE: 0x00184C20
// Resolves the 16 sub-table offsets of mission block 30 (stores in the code's order).
void func_00184C20(u32 block) {
    auto rel = [block](u32 o) { return block + GRef<u32>(block + o); };
    GRef<u32>(kEffectTables + 0x00) = rel(0x04);
    GRef<u32>(kEffectTables + 0x04) = rel(0x08);
    GRef<u32>(kEffectTables + 0x08) = rel(0x0C);
    GRef<u32>(kEffectTables + 0x0C) = rel(0x10);
    GRef<u32>(kEffectTables + 0x3C) = rel(0x40);
    for (u32 o = 0x14; o <= 0x3C; o += 4) GRef<u32>(kEffectTables + o - 4) = rel(o);
}

// EE: 0x001D8910
// Dead (no reference): resets a one-record pool of 0x20-byte records: the record (func_001D4200),
// the mask word +0x20 and the counter +0x24 (loops of one iteration in the original).
void func_001D8910(u32 pool) {
    func_001D4200(pool);
    GRef<u32>(pool + 0x20) = 0;
    GRef<u32>(pool + 0x24) = 0;
}

// EE: 0x001D8998
// Takes the first free record of a one-record pool (mask word at +0x20, counter +0x24): returns
// its index, or -1 when it is in use.
s32 func_001D8998(u32 pool) {
    const u32 bits = GRef<u32>(pool + 0x20);
    if (bits != 0xFFFFFFFFu && (bits & 1) == 0) {
        GRef<u32>(pool + 0x20) |= 1;
        GRef<s32>(pool + 0x24) += 1;
        return 0;
    }
    return -1;
}

// EE: 0x001D6518
// Mission 0x12: pool 26 reset again, its single record taken and cleared.
void func_001D6518() {
    const u32 pool = g_EffectBlock() + 0x62410;
    func_001568F0(pool);
    if (func_001D8998(pool) != -1) func_001D4200(pool);
}

// EE: 0x0019FF90
// 1,024 words at 0x00420850 = rand() >> 15 (arithmetic).
void func_0019FF90() {
    GRef<u32>(0x004117E8) = 2;
    GRef<u32>(0x004117EC) = 1;
    for (u32 i = 0; i < 0x400; ++i) GRef<s32>(0x00420850 + 4 * i) = ee::rand() >> 15;
}

// EE: 0x001BDAD8
// 128 random directions (1 - 2r per component, w = 1), then mission 0x12 / 8 extras.
// fpucheck: 6 adds (r + r and 1 - 2r, three components)
void func_001BDAD8() {
    for (u32 i = 0; i < 128; ++i) {
        const u32 v = 0x002B91C0 + 16 * i;
        f32 a = NextRandomFloat();
        GRef<f32>(v + 0) = fpu::Sub(1.0f, fpu::Add(a, a));
        a = NextRandomFloat();
        GRef<f32>(v + 4) = fpu::Sub(1.0f, fpu::Add(a, a));
        a = NextRandomFloat();
        GRef<f32>(v + 12) = 1.0f;
        GRef<f32>(v + 8) = fpu::Sub(1.0f, fpu::Add(a, a));
    }
    if (MissionId() == 0x12) func_001D6518();
    func_0019FF90();
    const f32 k = GRef<f32>(0x0040F970);
    if (MissionId() == 8) {
        for (s32 i = 0; i < 0x80; ++i) {
            const f32 x = fpu::IntToFloat(i) * k * 30.0f;
            GRef<f32>(0x002B8C3C + 4 * static_cast<u32>(i)) = ee::sinf(x);
            GRef<f32>(0x002B8E3C + 4 * static_cast<u32>(i)) = ee::cosf(x);
        }
    }
}

// EE: 0x00154660
// Called at mission start, on every retry and by the replay start: resets every effect pool in
// the original order, then the shared tables and the VU0 R seed (word at 0x0040E7F0).
void InitEffects(u32 block30) {
    func_00184C20(block30);
    const auto pool = [](u32 off) { return g_EffectBlock() + off; };
    thunk_func_00155BC8(pool(0));
    thunk_func_00155C50(pool(0x2010));
    thunk_func_00155CB8(pool(0x30700));
    thunk_func_00155D40(pool(0x31710));
    thunk_func_00155DC8(pool(0x38320));
    thunk_func_00155E50(pool(0x22220));
    thunk_func_00155ED8(pool(0x22C30));
    thunk_func_00155F60(pool(0x22E40));
    thunk_func_00155FE8(pool(0x24650));
    thunk_func_00156070(pool(0x247E0));
    thunk_func_001560F8(pool(0x268E0));
    thunk_func_00156180(pool(0x267F0));
    thunk_func_00156208(pool(0x274F0));
    thunk_func_00156290(pool(0x28400));
    thunk_func_00156318(pool(0x2B430));
    thunk_func_001563A0(pool(0x2B6C0));
    thunk_func_00156428(pool(0x2DCD0));
    thunk_func_001564B0(pool(0x2E2E0));
    thunk_func_00156538(pool(0x2E4F0));
    thunk_func_001565C0(pool(0x38B30));
    thunk_func_00156648(pool(0x61940));
    thunk_func_001566D0(pool(0x3FF90));
    thunk_func_00156758(pool(0x5FEE0));
    thunk_func_001567E0(pool(0x61F00));
    thunk_func_00156868(pool(0x5E490));
    thunk_func_001568F0(pool(0x62410));
    thunk_func_00156978(pool(0x5D880));
    thunk_func_00156A00(pool(0x61BE0));
    thunk_func_00156A88(pool(0x5E8A0));
    thunk_func_00156B10(pool(0x5F2C0));
    thunk_func_00156B98(pool(0x61420));
    thunk_func_00156C20(pool(0x4A830));
    thunk_func_00156CB0(pool(0x604F0));
    thunk_func_00156D38(pool(0x447D0));
    thunk_func_00156DC0(pool(0x61AD0));
    thunk_func_00156E48(pool(0x39F40));
    thunk_func_00156ED0(pool(0x5EAB0));
    thunk_func_00156F58(pool(0x5D070));
    thunk_func_00156FE0(pool(0x60D00));
    thunk_func_00157068(pool(0x5B050));
    thunk_func_001570F0(pool(0x5FAD0));
    thunk_func_00157178(pool(0x61010));
    thunk_func_00157200(pool(0x47800));
    thunk_func_00157288(pool(0x61630));
    thunk_func_00157310(pool(0x61DF0));
    func_001BDAD8();
    const u32 tail = g_EffectBlock() + 0x60000;
    GRef<u8>(tail + 0x2451) = 1;
    GRef<u32>(tail + 0x2440) = 0;
    GRef<u32>(tail + 0x2444) = 0;
    GRef<u32>(tail + 0x2448) = 0;
    GRef<f32>(g_EffectBlock() + 0x62494) = 65536.0f;
    GRef<f32>(g_EffectBlock() + 0x62498) = 1.0f;
    vu0::RInit(GRef<u32>(0x0040E7F0));
}

}  // namespace ac04::game
