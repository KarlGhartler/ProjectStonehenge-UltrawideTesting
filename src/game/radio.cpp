// Radio / message system set-up for a mission (block A of S5): the radio database from mission
// blocks 34 and 35, voice channels and request queues, tutorial control hints, the subtitle
// timeline, the vibration slots and the periodic chatter timers. docs/subsystems/mission-load.md
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
