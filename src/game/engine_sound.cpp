// Engine and ambient sound state (0x002155D0-0x00216790): per-player engine layers (0x00423880 +
// p * 0x318, 11 layers of 0x48), curve sets (0x00423F50 + p * 0x10E), the ambient records
// (0x0033D7C0 + p * 0xC0) and the enable flags. docs/subsystems/sound.md "Engine sound".
#include "game/game.h"

namespace ac04::game {

using guest::GRef;

namespace {

constexpr u32 kLayers = 0x00423880;   // 2 x 0x318
constexpr u32 kCurves = 0x00423F50;   // 2 x 0x10E
constexpr u32 kAmbient = 0x0033D7C0;  // 2 x 0xC0

// The eight per-player words of the engine state (0x00410C88 + 8k + 4p).
void ClearPlayerWords(u32 p) {
    for (u32 a : {0x00410C88u, 0x00410C90u, 0x00410C98u, 0x00410CA0u, 0x00410CA8u, 0x00410CC0u, 0x00410CB0u,
                  0x00410CB8u})
        GRef<u32>(a + 4 * p) = 0;
}

}  // namespace

// EE: 0x002155D0
// Copies an engine curve set (0x10E bytes, aircraft block 8) for player p.
void func_002155D0(s32 p, u32 curves) {
    const u32 dst = kCurves + static_cast<u32>(p) * 0x10E;
    for (u32 o = 0; o < 0x100; o += 8) GRef<u64>(dst + o) = GRef<u64>(curves + o);
    GRef<u64>(dst + 0x100) = GRef<u64>(curves + 0x100);
    GRef<u32>(dst + 0x108) = GRef<u32>(curves + 0x108);
    GRef<u8>(dst + 0x10C) = GRef<u8>(curves + 0x10C);
    GRef<u8>(dst + 0x10D) = GRef<u8>(curves + 0x10D);
}

// EE: 0x00215838
// Stops engine layer n of player p.
void func_00215838(s32 p, s32 n) {
    StopSound(kLayers + static_cast<u32>(p) * 0x318 + static_cast<u32>(n) * 0x48);
}

// EE: 0x00215BD0
// Engine sound disabled, mode set; per player the state words cleared and the 11 layer channels
// freed (0xFF, from layer 10 down).
void func_00215BD0(u32 mode) {
    GRef<u32>(0x00410CD8) = 0;
    GRef<u32>(0x00410CDC) = mode;
    for (u32 p = 0; p < 2; ++p) {
        ClearPlayerWords(p);
        for (s32 n = 10; n >= 0; --n) GRef<u8>(kLayers + p * 0x318 + static_cast<u32>(n) * 0x48 + 0xE) = 0xFF;
    }
}

// EE: 0x00215CD0
void func_00215CD0(s32 p, u32 curves, u32 bank, u16 v) {
    GRef<u32>(0x00410CC8 + 4 * static_cast<u32>(p)) = bank;
    GRef<u16>(0x004117F8 + 2 * static_cast<u32>(p)) = v;
    func_002155D0(p, curves);
}

// EE: 0x00215D08
// Speed / volume / pitch limits.
void func_00215D08(u16 a, u16 b, u16 c, u16 d) {
    GRef<u16>(0x00410CF0) = a;
    GRef<u16>(0x00410CF2) = b;
    GRef<u16>(0x00410CF4) = c;
    GRef<u16>(0x00410CF6) = d;
}

// EE: 0x002162F0
// Engine sound start: bank ids of both players' banks (the second only in modes 1-3), enabled,
// reverb mode 3 on core 0.
void func_002162F0() {
    GRef<u32>(0x00410CD0) = func_00218F00(GRef<s32>(0x00410CC8));
    if (GRef<u32>(0x00410CDC) - 1u < 3) GRef<u32>(0x00410CD4) = func_00218F00(GRef<s32>(0x00410CCC));
    GRef<u32>(0x00410CD8) = 1;
    func_0021CF70(0, 3);
}

// EE: 0x00216348
// Engine sound stop: state cleared, every layer with a channel stopped.
void func_00216348() {
    GRef<u32>(0x00410CD8) = 0;
    for (u32 p = 0; p < 2; ++p) {
        ClearPlayerWords(p);
        for (u32 n = 0; n < 0xB; ++n) {
            const u32 layer = kLayers + p * 0x318 + n * 0x48;
            if (GRef<s8>(layer + 0xE) != -1) {
                func_00215838(static_cast<s32>(p), static_cast<s32>(n));
                GRef<u8>(layer + 0xE) = 0xFF;
            }
        }
    }
}

// EE: 0x00216520
void func_00216520(u32 mode) {
    GRef<u32>(0x00410CDC) = mode;
}

// EE: 0x00216528
void func_00216528() {
    func_00215D08(100, 100, 0x100, 100);
}

// EE: 0x00216550
// Ambient record n: aircraft id (+0xA0) and the value at +0xA4.
void func_00216550(s32 n, u32 aircraft, u32 mask) {
    GRef<u32>(kAmbient + static_cast<u32>(n) * 0xC0 + 0xA4) = mask;
    GRef<u32>(kAmbient + static_cast<u32>(n) * 0xC0 + 0xA0) = aircraft;
}

// EE: 0x00216570
// Ambient record n: curve set (aircraft block 8) and a new sound bank from aircraft block 7.
void func_00216570(s32 n, u32 curves, u32 header) {
    const s32 bank = func_00218D58();
    GRef<u32>(kAmbient + static_cast<u32>(n) * 0xC0 + 0xB0) = curves;
    GRef<s32>(kAmbient + static_cast<u32>(n) * 0xC0 + 0xA8) = bank;
    func_00219810(bank, header);
}

// EE: 0x002165D0
// Engine / ambient sound init for 1 or 2 players (any other count: 2), then everything off.
void func_002165D0(u32 players) {
    func_00216528();
    s32 count = static_cast<s32>(players);
    if (count == 1) {
        func_00215BD0(0);
    } else {
        if (count != 2) count = 2;
        func_00215BD0(1);
    }
    for (s32 p = 0; p < count; ++p) {
        const u32 a = kAmbient + static_cast<u32>(p) * 0xC0;
        const u32 curves = GRef<u32>(a + 0xB0);
        const u32 bank = GRef<u32>(a + 0xA8);
        const u16 v = GRef<u16>(a + 0xA4);
        GRef<u32>(a + 0x9C) = 0;
        func_00215CD0(p, curves, bank, v);
        GRef<u32>(a + 0x54) = 0;
        GRef<u32>(a + 0xB4) = 0;
        GRef<u32>(a + 0x04) = 0;
        GRef<u32>(a + 0xB8) = 0;
    }
    func_00216728();
}

// EE: 0x002166D0
void func_002166D0() {
    GRef<u32>(0x0033D858) = 1;
    func_002162F0();
    for (u32 a = kAmbient; a < 0x0033D940; a += 0xC0) {
        GRef<u32>(a + 0xB4) = 0;
        GRef<u32>(a + 0xB8) = 0;
    }
}

// EE: 0x00216728
void func_00216728() {
    GRef<u32>(0x0033D858) = 0;
    func_00216348();
    for (u32 p = 0; p < 2; ++p) {
        func_00219798(kAmbient + p * 0xC0);
        func_00219798(kAmbient + 0x50 + p * 0xC0);
    }
}

// EE: 0x00216790
void func_00216790(u32 mode) {
    func_00216520(mode);
}

}  // namespace ac04::game
