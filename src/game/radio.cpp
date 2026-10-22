// Radio / message system set-up for a mission (block A of S5): the radio database from mission
// blocks 34 and 35, voice channels and request queues, tutorial control hints, the subtitle
// timeline, the vibration slots and the periodic chatter timers. docs/subsystems/mission-flow.md
// "Message system"; random reads (libc rand, three per call of func_002594F0): docs/game/random.md.
#include "game/game.h"

#include "common/ee_math.h"

#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

constexpr u32 kRadioHeader = 0x004BA300;  // block 34 header copy (0x30 bytes)
constexpr u32 kRadioTable = 0x004BA570;   // 0xA80 entry pointers

}  // namespace

// EE: 0x002508E0
void func_002508E0() {
    GRef<u32>(0x00411860) = 0;
    GRef<u16>(0x00411864) = 0;
    GRef<u8>(0x00411866) = 0;
    GRef<u8>(0x00411865) = 0;
    GRef<u8>(0x00411867) = 0;
}

// EE: 0x00253038
// The 6 request queues.
void func_00253038() {
    for (u32 a = 0x00411871; a <= 0x00411876; ++a) GRef<u8>(a) = 0;
}

// EE: 0x00254E48
void func_00254E48(u32 p) {
    for (u32 k = 0; k < 3; ++k) GRef<u32>(0x004BD9A0 + 4 * k) = p + 0x10 * k;
}

// EE: 0x00254FB0
// Candidate entry: id -1, kind 8, 0x1B.
void func_00254FB0(u32 r) {
    GRef<u16>(r + 0) = 0xFFFF;
    GRef<u8>(r + 2) = 8;
    GRef<u8>(r + 3) = 0x1B;
    GRef<u32>(r + 0xC) = 0;
    GRef<u32>(r + 4) = 0;
    GRef<u32>(r + 8) = 0;
}

// EE: 0x002556A0
// The 2 voice channels, 16 candidate entries, the played-id history and the request tables.
void func_002556A0() {
    GRef<u8>(0x004BD9B0) = 0;
    GRef<u8>(0x004BD9B1) = 0;
    GRef<u8>(0x004BD9B2) = 0;
    for (u32 c = 0; c < 2; ++c) {
        const u32 o = 4 * c;
        GRef<u32>(0x004BD9B4 + o) = 1;
        GRef<u32>(0x004BD9BC + o) = 0xF;
        GRef<u32>(0x004BD9C4 + o) = 0x17;
        GRef<s32>(0x004BD9CC + o) = -1;
        GRef<u32>(0x004BD9D4 + o) = 0;
        GRef<u32>(0x004BD9DC + o) = 0;
        GRef<s32>(0x004BD9E4 + o) = -1;
        GRef<u32>(0x004BD9EC + o) = 8;
    }
    for (u32 k = 0; k < 16; ++k) func_00254FB0(0x004BDA00 + 0x10 * k);
    for (u32 a = 0x004BDEFF; a >= 0x004BDB00; --a) GRef<u8>(a) = 0;
    for (u32 a = 0x004BDF0C; a >= 0x004BDF00; a -= 4) GRef<u32>(a) = 0;
    for (u32 a = 0x004BDFCC; a >= 0x004BDF10; a -= 4) GRef<s32>(a) = -1;
}

// EE: 0x002583C0
// Tutorial control hints: request masks and the 13 icon counters.
void func_002583C0() {
    GRef<u32>(0x004BE040) = 0;
    GRef<u32>(0x004BE044) = 0;
    for (u32 a = 0x004BE078; a >= 0x004BE048; a -= 4) GRef<u32>(a) = 0;
}

// EE: 0x00259280
// Binds a scripted subtitle timeline: the 32-byte header copied to 0x004BE080, its track offsets
// (count u16 +2) made absolute.
void func_00259280(u32 t) {
    for (u32 o = 0; o < 0x20; o += 8) GRef<u64>(0x004BE080 + o) = GRef<u64>(t + o);
    for (u32 i = 0; i < GRef<u16>(0x004BE082); ++i) GRef<u32>(0x004BE08C + 4 * i) = t + GRef<u32>(t + 0xC + 4 * i);
    GRef<u32>(0x004BE0A0) = 0;
    GRef<u32>(0x004BE0AC) = GRef<u32>(0x004BE08C);
    GRef<u16>(0x004BE0A4) = 0;
    GRef<u16>(0x004BE0A6) = 0;
}

// EE: 0x00259340
// Selects track n of the subtitle timeline 0x004BE080: frame counter and flags cleared, entry
// pointer = track n start, track +4 = n, step +6 = 0. Returns 1, or 0 when there is no timeline or
// n >= count.
s32 func_00259340(u32 n) {
    const u32 T = 0x004BE080, C = 0x004BE0A0;
    const u32 count = GRef<u16>(T + 2);
    if (count == 0) return 0;
    if (!(n < count)) return 0;
    const u32 e = GRef<u32>(T + static_cast<u32>(static_cast<s32>(static_cast<s16>(n))) * 4 + 0xC);
    GRef<u32>(C) = 0;
    GRef<u32>(C + 8) = 0;
    GRef<u32>(C + 0xC) = e;
    GRef<u16>(C + 4) = static_cast<u16>(n);
    GRef<u16>(C + 6) = 0;
    return 1;
}

namespace {

// Pointer past a u16 string ending in a code >= 0xFFFE.
u32 SkipString(u32 s) {
    while (GRef<u16>(s) <= 0xFFFD) s += 2;
    return s + 2;
}

}  // namespace

// EE: 0x002509F8
// Radio database from mission block 35: index cleared, font, message string pointers into the
// records at `base` (+0xC), the 77-entry name table, speaker data, subtitle timeline, tutorial
// extra block (only when the first section's first u16 is 0x13), control hints.
void func_002509F8(u32 base, u32 b) {
    const u32 w0 = GRef<u32>(b + 0), w1 = GRef<u32>(b + 4), w2 = GRef<u32>(b + 8);
    const u32 w3 = GRef<u32>(b + 0xC), w4 = GRef<u32>(b + 0x10), w5 = GRef<u32>(b + 0x14);
    for (u32 a = 0x004BD76C; a >= 0x004BCF70; a -= 4) GRef<s32>(a) = -1;
    const u32 header = b + w3;
    SelectFont(b + w1, b + w4);
    u32 s = b + w2 + 4;
    u32 r = base;
    for (u32 i = 0; i < GRef<u16>(kRadioHeader + 0x20); ++i, r += 0x10) {
        GRef<u32>(r + 0xC) = s;
        s = SkipString(s);
    }
    const u32 first = GRef<u32>(header);
    const u32 rest = header + 0x10;
    // The name strings follow the string of record count - 3 (r - 0x24 = that record's +0xC).
    s = GRef<u32>(r - 0x24);
    for (u32 i = 0; i < 0x4D; ++i) {
        GRef<u32>(0x004BCF70 + 4 * i) = s;
        s = SkipString(s);
    }
    GRef<s32>(0x004BD770) = -1;
    GRef<u32>(0x004BD78C) = first;
    GRef<u32>(0x004BD788) = header;
    GRef<u32>(0x004BD790) = rest;
    GRef<u32>(0x004BD784) = 3;
    GRef<s32>(0x004BD774) = -1;
    GRef<u32>(0x004BD778) = 0;
    GRef<u32>(0x004BD77C) = 0;
    func_00259280(b + w5);
    if (GRef<u16>(GRef<u32>(kRadioHeader)) == 0x13) GRef<u32>(0x00411868) = b + GRef<u32>(b + w0 * 4);
    else GRef<u32>(0x00411868) = 0;
    func_002583C0();
}

// EE: 0x00250C30
// Radio system init for a mission from blocks 34 (database) and 35 (messages): pointer table,
// database, channels, queues, candidate entries; ends with a debug printf of block 35's header.
void func_00250C30(u32 db, u32 msg) {
    for (u32 a = 0x004BCF6C; a >= kRadioTable; a -= 4) GRef<s32>(a) = -1;
    const u32 h = kRadioHeader;
    for (u32 o = 0; o < 0x30; o += 8) GRef<u64>(h + o) = GRef<u64>(db + o);
    GRef<u32>(h + 0x00) = db + GRef<u32>(db + 0x00);
    GRef<u32>(h + 0x0C) = db + GRef<u32>(db + 0x0C);
    GRef<u32>(h + 0x14) = db + GRef<u32>(db + 0x14);
    GRef<u32>(h + 0x1C) = db + GRef<u32>(db + 0x1C);
    GRef<u32>(h + 0x04) = db + GRef<u32>(db + 0x04);
    u32 t = 0;
    auto put = [&](s32 off) {
        GRef<u32>(kRadioTable + 4 * t) = off == -1 ? 0xFFFFFFFFu : GRef<u32>(h + 0x1C) + static_cast<u32>(off);
        ++t;
    };
    for (u32 n = GRef<u16>(h + 8), p = GRef<u32>(h + 4); n != 0; --n, p += 8) put(GRef<s32>(p + 4));
    for (u32 n = GRef<u16>(h + 0x10), p = GRef<u32>(h + 0xC); n != 0; --n, p += 0xC) put(GRef<s32>(p + 8));
    for (u32 n = GRef<u16>(h + 0x18), p = GRef<u32>(h + 0x14); n != 0; --n, p += 8) put(GRef<s32>(p + 4));
    const u32 c1 = GRef<u16>(h + 0xA), c2 = GRef<u16>(h + 0x12), c3 = GRef<u16>(h + 0x1A);
    u32 entries = GRef<u32>(h + 0x1C) + (c1 + c2 + c3) * 0x10;
    {
        const s32 n = GRef<u16>(h + 0x24);
        const s32 lo = GRef<u16>(h + 0x28);
        u32 e = entries - static_cast<u32>(lo) * 0x10;
        for (s32 k = 0; k < n; ++k, e += 0x10, ++t) {
            const bool in = k >= lo && k < lo + GRef<u16>(h + 0x26);
            GRef<u32>(kRadioTable + 4 * t) = in ? e : 0xFFFFFFFFu;
        }
    }
    entries += GRef<u16>(h + 0x26) * 0x10u;
    if (c1 != 0 || c2 != 0 || c3 != 0) {
        const s32 lo = GRef<u16>(h + 0x2A);
        u32 e = entries - static_cast<u32>(lo) * 0x10 + t * 0x10;
        for (u32 k = 0; k < 0x28; ++k, e += 0x10, ++t) {
            const s32 i = static_cast<s32>(t);
            const bool in = i >= lo && i < lo + 0x14;
            GRef<u32>(kRadioTable + 4 * t) = in ? e : 0xFFFFFFFFu;
        }
        if (c1 != 0 && c2 != 0 && c3 != 0) func_00254E48(entries + 0x140);
    }
    func_002509F8(GRef<u32>(h + 0x1C), msg);
    func_002556A0();
    func_00253038();
    for (u32 k = 0; k < 20; ++k) func_00254FB0(0x004BA330 + 0x10 * k);
    for (u32 k = 0; k < 16; ++k) func_00254FB0(0x004BA470 + 0x10 * k);
    func_002508E0();
    GRef<u8>(0x00411870) = 0;
    GRef<u32>(0x0041186C) = 0;
    const u64 args[4] = {GRef<u32>(msg + 0), GRef<u32>(msg + 4), GRef<u32>(msg + 8), GRef<u32>(msg + 0xC)};
    ee::printf(0x00409D78 /* " %x %x %x %x\n" */, args, 4);
}

// ---- Vibration -------------------------------------------------------------------------------

// EE: 0x00257430
void func_00257430(u32 slot) {
    GRef<u32>(slot + 0) = 0xC;
    GRef<s32>(slot + 4) = -1;
    GRef<u16>(slot + 0x10) = 1;
    GRef<u32>(slot + 0x18) = 0;
    GRef<u32>(slot + 8) = 0;
    GRef<u16>(slot + 0xC) = 0;
    GRef<u16>(slot + 0xE) = 0;
    GRef<u16>(slot + 0x12) = 0;
    GRef<u32>(slot + 0x14) = 0;
}

// EE: 0x00257468
void func_00257468(s32 p, s32 n) {
    func_00257430(0x004BDFD0 + static_cast<u32>(p % 2) * 0x38 + static_cast<u32>(n % 2) * 0x1C);
}

// EE: 0x00257630
// All vibration state reset; pad state pointers of player 1 and (2 players) player 2.
void func_00257630(u32 players) {
    for (u32 k = 0; k < 2; ++k) {
        GRef<u32>(0x00411880 + 4 * k) = 0;
        GRef<u32>(0x00411888 + 4 * k) = 0;
    }
    const u32 pads = g_pMainContext().addr + 0x210;
    GRef<u32>(0x00411878) = pads;
    GRef<u32>(0x0041187C) = static_cast<s64>(players) < 2 ? 0 : pads + 0x50;
    for (s32 n = 0; n < 2; ++n) {
        func_00257468(0, n);
        func_00257468(1, n);
    }
}

// EE: 0x00257360
// Camera-shake factor of player p from the rumble state: (D_00411888[p] - 0x48) / 183 while the
// strong motor runs, *0x0041045C while the weak one runs, else 0; clamped to 0..1.
f32 func_00257360(s32 p) {
    const u32 pp = 4 * static_cast<u32>(p);
    f32 k = 0.0f;
    if (GRef<s32>(0x00411888 + pp) != 0) k = fpu::IntToFloat(GRef<s32>(0x00411888 + pp) - 0x48) / 183.0f;
    else if (GRef<u32>(0x00411880 + pp) != 0) k = GRef<f32>(0x0041045C);
    if (1.0f <= k) return 1.0f;
    if (k < 0.0f) k = 0.0f;
    return k;
}

// Vibration slots: 2 per player at 0x004BDFD0 + p * 0x38, 0x1C bytes each: +0 kind (flags in
// table 0x003F5DD8[kind]: bit 0 pattern, bit 1 looped), +4 pattern step (from 0x003F61A8[kind]),
// +8 frame in step, +0xC/+0xE/+0x10 step values, +0x12 bit 0 requested this frame, +0x14 / +0x18
// parameters (float).

// EE: 0x002573F0
// The slot of player p running kind `kind`, or 0.
u32 func_002573F0(s32 p, s32 kind) {
    u32 s = 0x004BDFD0 + static_cast<u32>(p) * 0x38;
    for (s32 i = 0; i < 2; ++i, s += 0x1C)
        if (GRef<s32>(s) == kind) return s;
    return 0;
}

// EE: 0x002574B8
// Starts a pattern vibration of kind `kind` in slot s with parameter v.
void func_002574B8(u32 s, s32 kind, f32 v) {
    GRef<s32>(s) = kind;
    const u32 step = func_00257CF0(kind);
    const u16 a = GRef<u16>(step + 4);
    GRef<f32>(s + 0x18) = v;
    GRef<u16>(s + 0xC) = a;
    GRef<u32>(s + 4) = step;
    const s16 b = GRef<s16>(step + 6), c = GRef<s16>(step + 0x16);
    GRef<u32>(s + 8) = 0;
    GRef<u32>(s + 0x14) = 0;
    GRef<s16>(s + 0xE) = static_cast<s16>(c - b);
    GRef<u16>(s + 0x12) = 0;
    GRef<u16>(s + 0x10) = GRef<u16>(step + 8);
}

// EE: 0x00257528
// Starts a parameter-driven vibration of kind `kind` in slot s (+0x14 = a, +0x18 = b).
void func_00257528(u32 s, s32 kind, f32 a, f32 b) {
    GRef<s32>(s) = kind;
    const u32 step = func_00257CF0(kind);
    GRef<u16>(s + 0xC) = 0;
    GRef<u16>(s + 0xE) = 0;
    GRef<f32>(s + 0x18) = b;
    GRef<f32>(s + 0x14) = a;
    GRef<u16>(s + 0x10) = GRef<u16>(step + 8);
    GRef<u32>(s + 4) = step;
    GRef<u32>(s + 8) = 0;
    GRef<u16>(s + 0x12) = 0;
}

// EE: 0x00257590
// Next step of a pattern (looped kinds with the request bit stay on the loop step); 1 at the end.
s32 func_00257590(u32 s) {
    const u32 step = GRef<u32>(GRef<u32>(s + 4) + 0xC);
    const u16 kf = GRef<u16>(0x003F5DD8 + 2 * GRef<u32>(s));
    GRef<u32>(s + 4) = step;
    u32 next;
    if ((kf & 2) == 0)
        next = step + 0x10;
    else if ((GRef<u16>(step + 10) & 1) == 0)
        next = GRef<u32>(step + 0xC);
    else
        next = (((GRef<u16>(s + 0x12) ^ 1u) & 1u) == 0) ? GRef<u32>(step + 0xC) : step + 0x10;
    if (GRef<s32>(next) == -1) return 1;
    GRef<u32>(s + 8) = 0;
    GRef<u16>(s + 0xC) = GRef<u16>(step + 4);
    GRef<s16>(s + 0xE) = static_cast<s16>(GRef<s16>(next + 6) - GRef<s16>(step + 6));
    GRef<u16>(s + 0x10) = GRef<u16>(step + 8);
    return 0;
}

// EE: 0x002576C8
// Vibration request of kind `kind` for player p with parameters a, b: refreshes a running slot
// (kind 3: b capped at 16; kind 7: distance falloff 1 - (b * C - 17.5) / 50 between 17.5 and
// 67.5), else starts it in a free slot. Returns 1, or 0 when both slots are busy.
// fpucheck: 2 adds (b * C - 17.5, 1 - ..)
s32 func_002576C8(s32 p, s32 kind, f32 a, f32 b) {
    const u32 s = func_002573F0(p, kind);
    if (s == 0) {
        u32 f = 0x004BDFD0 + static_cast<u32>(p) * 0x38;
        s32 i = 0;
        for (; i < 2; ++i, f += 0x1C)
            if (GRef<s32>(f + 4) == -1) break;
        if (i >= 2) return 0;
        if ((GRef<u16>(0x003F5DD8 + 2u * static_cast<u32>(kind)) & 1) != 0)
            func_002574B8(f, kind, b);
        else
            func_00257528(f, kind, a, b);
        return 1;
    }
    GRef<u16>(s + 0x12) = static_cast<u16>(GRef<u16>(s + 0x12) | 1);
    const s32 k = GRef<s32>(s);
    if (k == 3) {
        if (16.0f <= b) b = 16.0f;
        GRef<f32>(s + 0x18) = b;
    } else if (k == 7) {
        b = b * GRef<f32>(0x00410460);
        if (b <= 17.5f)
            GRef<f32>(s + 0x18) = 1.0f;
        else if (67.5f <= b)
            GRef<u32>(s + 0x18) = 0;
        else {
            const f32 v = fpu::Sub(1.0f, fpu::Sub(b, 17.5f) / 50.0f);
            GRef<f32>(s + 0x18) = v;
            if (v < 0.0f) GRef<f32>(s + 0x18) = 0.0f;
        }
    } else {
        GRef<f32>(s + 0x18) = b;
    }
    return 1;
}

// EE: 0x00257890
// Vibration tick: per player and slot, the motor values (weak D_00411880[p], strong
// D_00411888[p]) from the pattern step or the parameter curve (kind 0 by *0x00410464/*0x00410468,
// kind 3 between 60 and 300), scaled by +0x18 for kinds 3, 7, 8, 10, modulo 255, sent to the pad
// (strong only from 0x30). Returns the mask of players with an active slot.
// fpucheck: 2 adds (hi - lo, v - lo)
u32 func_00257890() {
    u32 mask = 0;
    for (s32 p = 0; p < 2; ++p) {
        const u32 pp = 4u * static_cast<u32>(p);
        const u32 weak = 0x00411880 + pp, strong = 0x00411888 + pp, pad = 0x00411878 + pp;
        GRef<u32>(weak) = 0;
        GRef<u32>(strong) = 0;
        u32 s = 0x004BDFD0 + static_cast<u32>(p) * 0x38;
        for (s32 n = 0; n < 2; ++n, s += 0x1C) {
            const u32 step = GRef<u32>(s + 4);
            if (step == 0xFFFFFFFFu) continue;
            mask |= 1u << p;
            const s32 kind = GRef<s32>(s);
            if ((GRef<u16>(0x003F5DD8 + 2u * static_cast<u32>(kind)) & 1) != 0) {
                GRef<u32>(weak) = GRef<u16>(s + 0xC);
                if (GRef<u16>(s + 0x10) == 1)
                    GRef<s32>(strong) =
                        fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(s + 0xE)) / fpu::IntToFloat(GRef<s32>(step)) *
                                        fpu::IntToFloat(GRef<s32>(s + 8))) +
                        GRef<u16>(step + 6);
                else
                    GRef<u32>(strong) = GRef<u16>(step + 6);
                s32 done = 0;
                if (GRef<s32>(step) <= GRef<s32>(s + 8)) done = func_00257590(s);
                if (done != 0) {
                    func_001025C8(GRef<u32>(pad), 0, 0);
                    func_00257468(p, n);
                } else {
                    GRef<s32>(s + 8) += 1;
                    GRef<u16>(s + 0x12) &= 0xFFFEu;
                }
            } else {
                f32 lo, hi;
                s32 vlo, vhi;
                if (kind == 0) {
                    lo = GRef<f32>(0x00410464);
                    hi = GRef<f32>(0x00410468);
                    vlo = 0x28;
                    vhi = 0x78;
                } else if (kind == 3) {
                    lo = 60.0f;
                    hi = 300.0f;
                    vlo = 0x28;
                    vhi = 0x78;
                } else {
                    lo = 0.0f;
                    hi = 1.0f;
                    vlo = 0;
                    vhi = 1;
                }
                const f32 v = GRef<f32>(s + 0x14);
                GRef<u32>(weak) = 0;
                if (v < 0.0f)
                    GRef<u32>(strong) = 0;
                else if (v < lo)
                    GRef<s32>(strong) = vlo;
                else if (hi <= v)
                    GRef<s32>(strong) = vhi;
                else
                    GRef<s32>(strong) =
                        fpu::FloatToInt(fpu::IntToFloat(vhi - vlo) / fpu::Sub(hi, lo) * fpu::Sub(v, lo)) + vlo;
                func_00257468(p, n);
            }
            const s32 k = GRef<s32>(s);
            s32 val;
            if ((k < 9 && (k >= 7 || k == 3)) || k == 10)
                val = fpu::FloatToInt(fpu::IntToFloat(GRef<s32>(strong)) * GRef<f32>(s + 0x18));
            else
                val = GRef<s32>(strong);
            GRef<s32>(strong) = val % 0xFF;
            if (GRef<s32>(strong) < 0x30)
                func_001025C8(GRef<u32>(pad), GRef<u8>(weak), 0);
            else
                func_001025C8(GRef<u32>(pad), GRef<u8>(weak), GRef<u8>(strong));
        }
    }
    return mask;
}

// EE: 0x00257C28
// Stops every vibration slot and both motors.
void func_00257C28() {
    const s32 pads = GRef<u32>(0x0041187C) != 0 ? 2 : 1;
    for (s32 n = 0; n < 2; ++n) {
        func_00257468(0, n);
        func_00257468(1, n);
    }
    for (s32 i = 0; i < pads; ++i) {
        func_001025C8(GRef<u32>(0x00411878 + 4u * static_cast<u32>(i)), 0, 0);
        GRef<u32>(0x00411880 + 4u * static_cast<u32>(i)) = 0;
        GRef<u32>(0x00411888 + 4u * static_cast<u32>(i)) = 0;
    }
}

// EE: 0x00257CF0
u32 func_00257CF0(s32 kind) { return GRef<u32>(0x003F61A8 + 4u * static_cast<u32>(kind)); }

// ---- Chatter timers ----------------------------------------------------------------------------

// EE: 0x002594F0
// Periodic chatter: next event in 900-1079 frames, hold 16-31, radio at 180-299 (three rand()).
void func_002594F0() {
    GRef<s32>(0x004BE0B0) = ee::rand() % 0xB4 + 900;
    GRef<s32>(0x004BE0B4) = ee::rand() % 16 + 16;
    const s32 r = ee::rand();
    GRef<u32>(0x004BE0BC) = 0;
    GRef<s32>(0x004BE0B8) = r % 0x78 + 0xB4;
}

}  // namespace ac04::game
