// Units of a mission (block A of S5): the unit list (*0x00410640), the 255 unit records
// (0x002A1F70[i], 0xA0 bytes), their placement from mission block 22 (SetupMissionUnits) and the
// per-kind state records (player 0x4C0, AI aircraft 0x110, ground 0xD0). docs/subsystems/
// mission-load.md "SetupMissionUnits" and "Callees of SetupMissionUnits"; random reads:
// docs/game/random.md (one NextRandomFloat per path-kind-4 ground unit, one per aircraft unit).
//
// `sqc2 vf0` stores VU0 vf0 = (0, 0, 0, 1.0).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;


namespace {

u8 Game(u32 i) { return GRef<u8>(g_GameState().addr + i); }
u32 Unit(u32 i) { return GRef<u32>(0x002A1F70 + 4 * i); }
u32 UnitCount() { return GRef<u32>(g_UnitList()); }

void StoreVf0(u32 p) {
    GRef<u32>(p + 0) = 0;
    GRef<u32>(p + 4) = 0;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(p + 12) = 0x3F800000;
}

void StoreZeroQword(u32 p) {
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(p + o) = 0;
}

}  // namespace

// EE: 0x00135C50
void func_00135C50() {
    GRef<u32>(0x004117CC) = 0x49000000;  // 524288.0f
    GRef<u32>(0x004117C8) = 0x49000000;
}

// EE: 0x00133A80
void func_00133A80() {
    for (u32 n = 0; n < 2; ++n) {
        GRef<u32>(GRef<u32>(0x00410674) + 4 * n) = 0;
        GRef<u8>(GRef<u32>(0x00410674) + 8 + n) = 0xFF;
    }
}

// EE: 0x00133A58
void func_00133A58() {
    GRef<u32>(0x00410674) = WorkHeapAlloc(0xC, 4);
    func_00133A80();
}

// EE: 0x00134FA8
void func_00134FA8() {
    const u32 r = WorkHeapAlloc(0x50, 0x10);
    GRef<u32>(0x00410670) = r;
    for (u32 o = 0x38; o >= 0x30; o -= 4) GRef<s32>(r + o) = -1;
    GRef<u8>(r + 0x3C) = 0;
    GRef<u32>(GRef<u32>(0x00410670) + 0x40) = 0;
}

// EE: 0x0011BA90
// Unit list header and all 255 unit records reset; the unit index at +0x55.
void func_0011BA90() {
    const u32 list = g_UnitList();
    GRef<u8>(list + 0x83) = 0;
    GRef<u32>(list + 0) = 0;
    GRef<u8>(g_UnitList() + 0x84) = 0;
    GRef<u8>(g_UnitList() + 0x85) = 0;
    GRef<u8>(g_UnitList() + 0x86) = 0;
    GRef<u8>(g_UnitList() + 0x87) = 0;
    GRef<u8>(g_UnitList() + 0x88) = 0xFF;
    GRef<u8>(g_UnitList() + 0x8A) = 0xFF;
    GRef<u8>(g_UnitList() + 0x8B) = 0;
    const u8 id = Game(0x27);
    if (id == 1) GRef<u8>(g_UnitList() + 0x8A) = 0x34;
    else if (id == 0x10) GRef<u8>(g_UnitList() + 0x8A) = 0x39;
    else if (id == 0xE || id == 0xF) GRef<u8>(g_UnitList() + 0x8A) = 0x12;
    for (u32 i = 0; i < 0xFF; ++i) {
        const u32 u = Unit(i);
        GRef<u32>(u + 0x40) = 0;
        GRef<u32>(u + 0x48) = 0;
        GRef<u32>(u + 0x4C) = 0;
        GRef<u32>(u + 0x50) = 0;
        GRef<u8>(u + 0x54) = 3;
        GRef<u16>(u + 0x56) = 0;
        GRef<u8>(u + 0x58) = 0xFF;
        GRef<u8>(u + 0x59) = 0xFF;
        GRef<u8>(u + 0x5A) = 0;
        GRef<u8>(u + 0x5B) = 0;
        GRef<u32>(u + 0x44) = 0;
        GRef<u16>(u + 0x5C) = 0;
        GRef<u16>(u + 0x60) = 0;
        GRef<u16>(u + 0x5E) = 0;
        GRef<u8>(u + 0x55) = static_cast<u8>(i);
        GRef<u8>(u + 0x62) = 0xFF;
        GRef<u8>(u + 0x63) = 0xFF;
        GRef<u8>(u + 0x68) = 0;
        GRef<u32>(u + 0x6C) = 0;
        GRef<u32>(u + 0x70) = 0;
        GRef<u32>(u + 0x74) = 0;
        GRef<u32>(u + 0x78) = 0;
        GRef<u8>(u + 0x64) = 0;
        GRef<u8>(u + 0x65) = 0;
        GRef<u8>(u + 0x66) = 0;
        GRef<u8>(u + 0x67) = 0;
        StoreVf0(u + 0x00);
        StoreZeroQword(u + 0x10);
        StoreZeroQword(u + 0x20);
        StoreZeroQword(u + 0x30);
        for (u32 o = 0x84; o >= 0x7C; o -= 4) GRef<s32>(u + o) = -1;
        GRef<u8>(u + 0x9B) = 0;
        GRef<u8>(u + 0x88) = 0;
        GRef<u16>(u + 0x8A) = 0;
        GRef<u8>(u + 0x89) = 0;
        GRef<u32>(u + 0x94) = 0;
        GRef<u32>(u + 0x90) = 0;
        GRef<u32>(u + 0x8C) = 0;
        GRef<u8>(u + 0x98) = 0;
        GRef<u8>(u + 0x99) = 0;
        GRef<u8>(u + 0x9A) = 0;
    }
    if (Game(0x27) == 10) func_00133A80();
    if (Game(0x27) == 0x12) func_00135C50();
}

// EE: 0x0011B948
// Unit list (0x90) with 2 x (4 + 5) target lists of 0x14 and one of 0x1C per player, then the 255
// unit records.
void func_0011B948() {
    g_UnitList() = WorkHeapAlloc(0x90, 4);
    for (u32 p = 0; p < 2; ++p) {
        for (u32 k = 0; k < 4; ++k) GRef<u32>(g_UnitList() + p * 0x10 + 4 * k + 4) = WorkHeapAlloc(0x14, 4);
        for (u32 k = 0; k < 5; ++k) GRef<u32>(g_UnitList() + p * 0x14 + 4 * k + 0x24) = WorkHeapAlloc(0x14, 4);
        GRef<u32>(g_UnitList() + p * 4 + 0x4C) = WorkHeapAlloc(0x1C, 4);
    }
    for (u32 i = 0; i < 0xFF; ++i) GRef<u32>(0x002A1F70 + 4 * i) = WorkHeapAlloc(0xA0, 0x10);
    if (Game(0x27) == 10) func_00133A58();
    if (Game(0x27) == 0xF) func_00134FA8();
    func_0011BA90();
}

// EE: 0x0011E878
// Hit points of aircraft model `id` (signed byte table 0x002A2388), scaled by the difficulty mode.
s32 func_0011E878(u32 id) {
    const s32 hp = GRef<s8>(0x002A2388 + (id & 0xFF));
    const u8 mode = Game(0x28);
    if (mode == 4) return hp * 0x46 / 100;
    if (mode == 5) return hp * 0x1E / 100;
    return hp;
}

// EE: 0x0011B920
// Special weapon id of aircraft `a` (above 21 -> 0), slot n.
u8 func_0011B920(s64 a, s32 n) {
    if (0x15 < a) a = 0;
    return GRef<u8>(static_cast<u32>(static_cast<s32>(a)) * 0x10 + static_cast<u32>(n) * 4 + 0x002A1CDC);
}

// EE: 0x001279A0
void func_001279A0() {
    GRef<u32>(0x00410658) = 0;
    GRef<u32>(0x0041065C) = 0;
}

// EE: 0x001279B0
// Engine-sound player flags (0x00410658[p]) on/off with the matching engine sound mode, and the
// engine sounds enabled/disabled when the first flag is set / the last one cleared.
void func_001279B0(s32 p, s64 on) {
    u32& a = GRef<u32>(0x00410658);
    u32& b = GRef<u32>(0x0041065C);
    u32& f = GRef<u32>(0x00410658 + 4 * static_cast<u32>(p));
    const u32 before = a + b;
    bool changed = false;
    if (on == 1) {
        if (f == 0) {
            f = 1;
            changed = true;
        }
    } else if (on == 0 && f == 1) {
        f = 0;
        changed = true;
    }
    const u32 after = a + b;
    if (changed) {
        if (Game(0x25) == 2) {
            if (after == 2) func_00216790(1);
            else if (a == 1) func_00216790(2);
            else if (b == 1) func_00216790(3);
        } else {
            func_00216790(0);
        }
    }
    if (before == 0) {
        if (after != 0) func_002166D0();
    } else if (after == 0) {
        func_00216728();
    }
}

// EE: 0x0011EC48
// Player special-weapon slot k of the player record (loadout record at +0x110).
void func_0011EC48(u32 pl, s64 k) {
    const u32 lo = GRef<u32>(pl + 0x110);
    GRef<s8>(pl + 0x172) = static_cast<s8>(k);
    GRef<u8>(pl + 0x15A) = GRef<u8>(lo + 2);
    GRef<u16>(pl + 0x158) = GRef<u16>(lo);
    if (k < 3) {
        const u32 off = static_cast<u32>(static_cast<s32>(static_cast<s8>(k)) * 4);
        const u8 w = GRef<u8>(lo + off + 4);
        GRef<u8>(pl + 0x173) = w;
        GRef<u8>(pl + 0x175) = GRef<u8>(lo + off + 5);
        GRef<u8>(pl + 0x15B) = GRef<u8>(lo + off + 6);
        if (w <= 7) {
            GRef<u8>(pl + 0x174) = 0;
            GRef<u32>(pl + 0x14C) |= 0x80;
        } else if (w == 8) {
            GRef<u8>(pl + 0x174) = 1;
            GRef<u32>(pl + 0x14C) &= 0xFFFFFF7Fu;
        } else if (w <= 0xE) {
            GRef<u8>(pl + 0x174) = 0;
            GRef<u32>(pl + 0x14C) &= 0xFFFFFF7Fu;
        }
    } else {
        GRef<u8>(pl + 0x15B) = 0;
        GRef<u8>(pl + 0x173) = 0xF;
        GRef<u8>(pl + 0x175) = 0;
    }
}

// EE: 0x0011E8D8
// Player state record of unit i (0x4C0 bytes, U+0x44).
void func_0011E8D8(s32 i) {
    const u32 u = Unit(static_cast<u32>(i));
    const u32 p = WorkHeapAlloc(0x4C0, 0x10);
    GRef<u32>(u + 0x44) = p;
    GRef<u32>(p + 0x110) = GetAircraftLoadout(GRef<u8>(u + 0x69));
    StoreZeroQword(p + 0x00);
    StoreZeroQword(p + 0x10);
    StoreZeroQword(p + 0x20);
    StoreVf0(p + 0x50);
    StoreVf0(p + 0x30);
    StoreVf0(p + 0x40);
    StoreZeroQword(p + 0x70);
    StoreVf0(p + 0x80);
    StoreZeroQword(p + 0x90);
    StoreZeroQword(p + 0xA0);
    GRef<u8>(p + 0x148) = static_cast<u8>(i);
    GRef<u8>(p + 0x151) = 0xFF;
    GRef<u8>(p + 0x152) = 1;
    for (u32 o = 0x114; o <= 0x144; o += 4) GRef<u32>(p + o) = 0;
    GRef<u8>(p + 0x149) = 0;
    GRef<u8>(p + 0x14A) = 0xFF;
    GRef<u8>(p + 0x14B) = 0;
    GRef<u32>(p + 0x14C) = 0;
    GRef<u8>(p + 0x150) = 0;
    GRef<u8>(p + 0x153) = 0;
    for (u32 k = 0; k < 4; ++k) {
        GRef<u8>(p + 0x154 + k) = 0xFF;
        GRef<u32>(p + 0x194 + 4 * k) = 0;
        GRef<u32>(p + 0x1A4 + 4 * k) = 0;
    }
    GRef<u16>(p + 0x160) = 0;
    GRef<u16>(p + 0x15E) = 0;
    for (u32 o = 0x170; o >= 0x166; o -= 2) GRef<u16>(p + o) = 0;
    const f32 k0 = GRef<f32>(0x0040DD60);
    GRef<f32>(p + 0x1B4) = 8192.0f;
    GRef<f32>(p + 0x1BC) = k0;
    GRef<u8>(p + 0x181) = 0xFF;
    GRef<u32>(p + 0x188) = 0x30000;
    for (u32 o : {0x15Cu, 0x15Du, 0x164u, 0x165u, 0x17Au}) GRef<u8>(p + o) = 0;
    GRef<u16>(p + 0x162) = 0;
    GRef<u16>(p + 0x178) = 0;
    GRef<f32>(p + 0x1B8) = k0;
    for (u32 o : {0x17Cu, 0x17Eu, 0x17Du, 0x182u, 0x183u, 0x184u, 0x185u}) GRef<u8>(p + o) = 0;
    for (u32 o : {0x18Cu, 0x190u, 0x278u, 0x27Cu}) GRef<u32>(p + o) = 0;
    GRef<u8>(p + 0x280) = 0;
    GRef<u8>(p + 0x281) = 0;
    for (u32 o = 0x23C; o >= 0x1C0; o -= 4) GRef<f32>(p + o) = 0.5f;
    GRef<f32>(p + 0x254) = 0.5f;
    GRef<u8>(p + 0x2F2) = 0xFF;
    GRef<f32>(p + 0x290) = 524288.0f;
    GRef<f32>(p + 0x240) = 0.5f;
    GRef<u32>(p + 0x244) = 0;
    GRef<u32>(p + 0x248) = 0;
    GRef<u32>(p + 0x24C) = 0;
    GRef<f32>(p + 0x250) = 0.5f;
    GRef<f32>(p + 0x258) = 0.5f;
    for (u32 o = 0x25C; o <= 0x274; o += 4) GRef<u32>(p + o) = 0;
    for (u32 o : {0x283u, 0x284u, 0x285u, 0x282u, 0x28Bu}) GRef<u8>(p + o) = 0;
    GRef<u8>(p + 0x2F0) = 0xFF;
    GRef<u8>(p + 0x2F1) = 0xFF;
    for (u32 o = 0x2B0; o >= 0x294; o -= 4) GRef<s32>(p + o) = -1;
    for (u32 k = 0; k < 2; ++k) {
        GRef<s32>(p + 0x2C4 + 4 * k) = -1;
        GRef<s32>(p + 0x2B4 + 4 * k) = -1;
        GRef<s32>(p + 0x2CC + 4 * k) = -1;
        GRef<u32>(p + 0x2BC + 4 * k) = 0;
        GRef<u8>(p + 0x289 + k) = 0;
    }
    GRef<u16>(p + 0x2F4) = 0;
    GRef<u16>(p + 0x2F6) = 0;
    GRef<u32>(p + 0x350) = 0;
    func_001279A0();
    GRef<u32>(p + 0x2D4) = 0;
}

// EE: 0x0011EFA0
// Player i's position history (six copies of the position with w = 2.0), moving parts, special
// weapon slot k, aircraft data pointer 0x00410650[i] (player aircraft block +0x176C).
void func_0011EFA0(s64 i, s64 k) {
    const u32 idx = static_cast<u32>(static_cast<s32>(i));
    const u32 u = Unit(idx);
    const u32 p = GRef<u32>(u + 0x44);
    for (u32 o = 0xB0; o <= 0x100; o += 0x10) {
        sceVu0CopyVector(p + o, u);
        GRef<f32>(p + o + 0xC) = 2.0f;
    }
    if ((GRef<u32>(u + 0x4C) & 0x10000000) != 0) UpdateAircraftMovingParts(static_cast<s32>(i), 1);
    func_0011EC48(p, k);
    const u32 data = GRef<u32>(g_FlightScene() + idx * 0x40 + 0x176C);
    GRef<u32>(0x00410650 + 4 * idx) = data;
    if (i < static_cast<s64>(Game(0x25))) func_00149E28(GRef<u32>(0x004106A4), static_cast<s32>(i), GRef<u32>(data + 0xD0));
}

// EE: 0x0012A6B8
// AI aircraft state of unit i (0x110 bytes; also kept in 0x004117B0).
void CreateAIAircraftState(s32 i) {
    const u32 s = WorkHeapAlloc(0x110, 0x10);
    GRef<u32>(0x004117B0) = s;
    GRef<u32>(s + 0x10) = 0;
    GRef<u32>(s + 0x14) = 0;
    GRef<u32>(Unit(static_cast<u32>(i)) + 0x44) = s;
    for (u32 o : {0xF4u, 0xF5u, 0xF6u, 0x18u}) GRef<u8>(s + o) = 0;
    GRef<u32>(s + 0x20) = 0;
    GRef<u8>(s + 0x19) = 0;
    GRef<u8>(s + 0x19) |= 0x80;
    for (u32 o : {0x1Bu, 0x1Au, 0x3Cu}) GRef<u8>(s + o) = 0;
    GRef<u32>(s + 0x38) = 0;
    for (u32 o : {0x3Du, 0x3Eu, 0x3Fu}) GRef<u8>(s + o) = 0;
    GRef<u32>(s + 0x40) = 0;
    for (u32 o : {0x44u, 0x45u, 0x46u, 0x47u, 0x48u}) GRef<u8>(s + o) = 0;
    GRef<u32>(s + 0x50) = 0;
    GRef<u16>(s + 0x4A) = 0;
    const u32 z = GRef<u32>(s + 0x50);
    GRef<u32>(s + 0x4C) = 0;
    GRef<u32>(s + 0x54) = 0;
    GRef<u32>(s + 0x58) = 0;
    GRef<u32>(s + 0x5C) = 0;
    for (u32 o = 0x60; o <= 0x7C; o += 4) GRef<u32>(s + o) = z;
    GRef<f32>(s + 0x80) = 1.0f;
    GRef<f32>(s + 0x84) = 1.0f;
    GRef<f32>(s + 0x88) = 1.0f;
    for (u32 o : {0x8Cu, 0x90u, 0x94u, 0x98u}) GRef<u32>(s + o) = z;
    GRef<f32>(s + 0x9C) = 1.0f;
    GRef<u32>(s + 0xA0) = 0;
    GRef<f32>(s + 0xA4) = 524288.0f;
    GRef<u8>(s + 0xE0) = 0;
    GRef<u32>(s + 0xCC) = 0xFF;
    GRef<f32>(s + 0xA8) = -1.0f;
    GRef<f32>(s + 0xBC) = 1.0f;
    GRef<f32>(s + 0xC0) = 262144.0f;
    GRef<u32>(s + 0xC4) = GRef<u32>(0x0040E020);
    GRef<f32>(s + 0xC8) = -1800.0f;
    for (u32 o : {0xB0u, 0xB4u, 0xB8u, 0xE8u, 0xE4u}) GRef<u32>(s + o) = z;
    for (u32 o : {0x100u, 0xF0u, 0xFCu, 0xF8u, 0x104u}) GRef<u32>(s + o) = 0;
}

// EE: 0x0012FC08
// Ground / naval unit state of unit i (0xD0 bytes; also kept in 0x004117C0).
void CreateGroundUnitState(s32 i) {
    const u32 s = WorkHeapAlloc(0xD0, 0x10);
    GRef<u32>(0x004117C0) = s;
    GRef<u32>(s + 8) = 0;
    GRef<u32>(Unit(static_cast<u32>(i)) + 0x44) = s;
    GRef<u32>(s + 0xC) = 0;
    StoreVf0(s + 0x10);
    StoreZeroQword(s + 0x20);
    GRef<u8>(s + 0x40) = 0;
    GRef<u32>(s + 0x8C) = 0;
    GRef<u32>(s + 0x88) = 0;
    GRef<u8>(s + 0x41) = 0x80;
    GRef<u8>(s + 0x42) = 0;
    GRef<u8>(s + 0x43) = 0;
    GRef<u32>(s + 0x48) = 0;
    StoreZeroQword(s + 0x60);
    StoreZeroQword(s + 0x70);
    GRef<u8>(s + 0x47) = 0xFF;
    GRef<u32>(s + 0xC0) = 0;
    GRef<u32>(s + 0x84) = 0;
    StoreZeroQword(s + 0x90);
    StoreZeroQword(s + 0xA0);
    StoreZeroQword(s + 0xB0);
    GRef<u32>(s + 0xC4) = 0;
}

// EE: 0x0010E3F8
// Placement header (mission block 22): 0x68-byte record of 26 pointers (header, then the
// sub-tables at s16 word offsets +6, +0xA ... +0x66).
u32 func_0010E3F8(u32 block) {
    const u32 h = WorkHeapAlloc(0x68, 4);
    GRef<u32>(h) = block;
    for (u32 k = 1; k < 26; ++k) GRef<u32>(h + 4 * k) = block + static_cast<u32>(static_cast<s32>(GRef<s16>(block + 4 * k + 2)) * 4);
    return h;
}

// EE: 0x0010E5C0
// Unit position and angles from a 0x18-byte placement record (angles in degrees * k / 180), and the
// flags from the record's flag bytes 2 and 3 (0x80 in byte 3: the list's +0x88 unit).
void func_0010E5C0(u32 u, u32 r) {
    GRef<f32>(u + 0) = GRef<f32>(r + 4);
    const f32 k = GRef<f32>(0x0040DBC0);
    GRef<f32>(u + 4) = GRef<f32>(r + 8);
    u32 flags = GRef<u32>(u + 0x48);
    GRef<f32>(u + 0xC) = 1.0f;
    GRef<f32>(u + 8) = GRef<f32>(r + 0xC);
    GRef<f32>(u + 0x10) = fpu::IntToFloat(GRef<s16>(r + 0x10)) * k / 180.0f;
    GRef<f32>(u + 0x14) = fpu::IntToFloat(GRef<s16>(r + 0x12)) * k / 180.0f;
    GRef<u32>(u + 0x1C) = 0;
    GRef<f32>(u + 0x18) = fpu::IntToFloat(GRef<s16>(r + 0x14)) * k / 180.0f;
    flags |= GRef<u8>(r + 2) & 3;
    GRef<u32>(u + 0x48) = flags;
    if ((GRef<u8>(r + 2) & 0x80) != 0) GRef<u32>(u + 0x48) = flags | 0x80000000u;
    if ((GRef<u8>(r + 2) & 0x40) != 0) GRef<u32>(u + 0x48) |= 0x08000000;
    if ((GRef<u8>(r + 2) & 0x20) != 0) GRef<u32>(u + 0x48) |= 0x04000000;
    flags = GRef<u32>(u + 0x48) | (static_cast<u32>(GRef<u8>(r + 3) & 3) << 12);
    GRef<u32>(u + 0x48) = flags;
    const u8 b3 = GRef<u8>(r + 3);
    if (b3 == 0x80) {
        GRef<u32>(u + 0x48) = flags | 0x400;
        if (GRef<u8>(g_UnitList() + 0x88) == 0xFF) GRef<u8>(g_UnitList() + 0x88) = GRef<u8>(u + 0x55);
    } else {
        if ((b3 & 4) != 0) GRef<u32>(u + 0x48) = flags | 0x100;
        if ((GRef<u8>(r + 3) & 8) != 0) GRef<u32>(u + 0x48) |= 0x200;
        GRef<u8>(u + 0x88) = static_cast<u8>((GRef<u8>(r + 3) & 0x70) >> 4);
    }
    GRef<u8>(u + 0x6A) = GRef<u8>(r + 0);
}

namespace {

// Radar class U+0x9B of model id (units without flag 0x400).
u8 RadarClass(u8 id) {
    if (id == 0) return 0;
    if (id < 0x17) return 1;
    switch (id) {
        case 0x2F: return 2;
        case 0x30: return 1;
        case 0x32: return 3;
        case 0x33: return 2;
        case 0x34: return 4;
        case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: return 2;
        case 0x3B: return 6;
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: return 5;
        case 0x41: return 4;
        case 0x42: return 6;
        case 0x44: return 1;
        case 0x45: return 4;
        case 0x46: return 2;
        case 0x47: case 0x48: return 5;
        default: return 0;
    }
}

// Type index U+0x99 of model id; returns false for id 0xFF (left unchanged).
bool TypeIndex(u8 id, u8* out) {
    if (id >= 1 && id <= 0xF) { *out = static_cast<u8>(id - 1); return true; }
    switch (id) {
        case 0x11: *out = 0xF; return true;
        case 0x10: *out = 0x10; return true;
        case 0x12: *out = 0x10; return true;
        case 0x13: *out = 0x11; return true;
        case 0x14: *out = 0x12; return true;
        case 0x44: *out = 0x13; return true;
        case 0x30: *out = 0x14; return true;
        case 0x33: *out = 0x15; return true;
        case 0x34: *out = 0x16; return true;
        case 0x35: *out = 0x17; return true;
        case 0x36: *out = 0x18; return true;
        case 0x37: case 0x2F: *out = 0x19; return true;
        case 0x38: *out = 0x1A; return true;
        case 0x39: *out = 0x1B; return true;
        case 0x45: *out = 0x1D; return true;
        case 0x46: *out = 0x1E; return true;
        case 0x47: *out = 0x1F; return true;
        case 0x3B: *out = 0x20; return true;
        case 0x41: *out = 0x21; return true;
        default: break;
    }
    if (static_cast<u8>(id - 0x3C) < 5 || static_cast<u8>(id - 0x47) < 2) { *out = 0x1C; return true; }
    if (id == 0x49 || id == 0x2E || static_cast<u8>(id - 0x4E) < 0x12) { *out = 0x2E; return true; }
    if (id < 0x60) { *out = 0x30; return true; }
    if (static_cast<u8>(id - 0x60) < 6) { *out = 0x22; return true; }
    if (static_cast<u8>(id - 0x66) < 2) { *out = 0x24; return true; }
    if (static_cast<u8>(id - 0x70) < 0x42) { *out = 0x23; return true; }
    if (id == 0xC5) { *out = 0x25; return true; }
    if (id == 0xC2) { *out = 0x27; return true; }
    if (static_cast<u8>(id + 0x3D) < 2) { *out = 0x26; return true; }
    if (id < 0xD0) { *out = 0x2E; return true; }
    switch (id) {
        case 0xD2: *out = 0x28; return true;
        case 0xD3: *out = 0x29; return true;
        case 0xD4: *out = 0x2A; return true;
        case 0xD5: *out = 0x2B; return true;
        case 0xE0: *out = 0x2C; return true;
        case 0xE1: *out = 0x2D; return true;
        case 0xDB: *out = 0x25; return true;
        case 0xDA: *out = 0x26; return true;
        case 0xE3: *out = 0x31; return true;
        default: break;
    }
    if (id < 0xFF) { *out = 0x2F; return true; }
    return false;
}

// A path reference of an AI or ground state: kind 1 sub-table 0xF x 0x14, 2 sub-table 0x10 x 16,
// 5 sub-table 0x13 x 32 (others none).
u32 PathRef(u32 h, u8 kind, u8 index, bool* known) {
    *known = true;
    switch (kind) {
        case 1: return GRef<u32>(h + 0x3C) + index * 0x14u;
        case 2: return GRef<u32>(h + 0x40) + (static_cast<u32>(index) << 4);
        case 5: return GRef<u32>(h + 0x4C) + (static_cast<u32>(index) << 5);
        default: *known = false; return 0;
    }
}

}  // namespace

// EE: 0x0010E758
// Places the mission's units from block 22 (difficulty variant when it holds three) and builds
// their state records, models, sounds and target lists. Two random-table reads kinds: path-kind-4
// ground units and every aircraft unit (last pass).
// fpucheck: 3 adds (r + r, - 1, height - offset)
void SetupMissionUnits() {
    u32 block = GRef<u32>(g_FlightScene() + 0x16EC);
    if (GRef<s32>(block) == 3) {
        const u8 d = Game(0x28);
        block += GRef<u32>(block + (d < 2 ? 4 : d < 3 ? 8 : 0xC));
    }
    GRef<u32>(0x004104CC) = func_0010E3F8(block);
    for (s32 i = 0; i < GRef<s16>(GRef<u32>(GRef<u32>(0x004104CC)) + 4); ++i) {
        const u32 h = GRef<u32>(0x004104CC);
        const u32 u = Unit(static_cast<u32>(i));
        const u32 rec = GRef<u32>(h + 4) + static_cast<u32>(i) * 0x18;
        GRef<u32>(u + 0x4C) = 0;
        GRef<u32>(u + 0x50) = 0;
        GRef<u16>(u + 0x56) = 0;
        GRef<u16>(u + 0x5C) = 0;
        GRef<u32>(u + 0x48) = 0x10000000;
        GRef<u8>(u + 0x63) = 0xFF;
        GRef<u8>(u + 0x62) = 0xFF;
        func_0010E5C0(u, rec);
        const u8 type = GRef<u8>(rec);
        const u32 index = GRef<u8>(rec + 1);
        const u32 hh = GRef<u32>(0x004104CC);
        switch (type) {
            case 1: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x08) + index * 0x24; break;
            case 2: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x0C) + index * 0x14; break;
            case 3: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x10) + index * 0x20; break;
            case 4: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x14) + index * 0x24; break;
            case 5: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x18) + index * 0x20; break;
            case 6: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x1C) + index * 0x28; break;
            case 7: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x20) + index * 0x18; break;
            case 8: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x24) + index * 0x20; break;
            case 9: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x28) + index * 0x18; break;
            case 10: GRef<u32>(u + 0x40) = GRef<u32>(hh + 0x2C) + index * 0x14; break;
            default: GRef<u32>(u + 0x40) = 0; break;
        }
        const u32 t = GRef<u32>(u + 0x40);
        if (t == 0) {
            if (GRef<u8>(u + 0x55) < 2) {
                const u8 id = Game(static_cast<u32>(i) * 0x10);
                GRef<u8>(u + 0x69) = id;
                GRef<u16>(u + 0x5E) = static_cast<u16>(func_0011E878(id));
                GRef<u8>(u + 0x59) = 0xFF;
                GRef<f32>(u + 0x70) = 50.0f;
                GRef<u16>(u + 0x60) = 100;
                GRef<u8>(u + 0x64) = 1;
                GRef<u8>(u + 0x58) = 0;
                GRef<u8>(u + 0x5A) = 0;
                GRef<u8>(u + 0x5B) = 1;
                GRef<u8>(u + 0x65) = 0;
            }
        } else {
            const u8 group = GRef<u8>(t + 1);
            GRef<u8>(u + 0x58) = group;
            GRef<u8>(u + 0x5A) = 0;
            GRef<u8>(u + 0x59) = GRef<u8>(t + 2);
            if (group != 0xFF) {
                GRef<u8>(u + 0x5B) = 1;
                GRef<u32>(u + 0x48) |= 0x20000000;
            }
            if (static_cast<u8>(GRef<u8>(u + 0x59) + 2) >= 2) GRef<u8>(u + 0x5B) = 2;
            GRef<u8>(u + 0x9A) = GRef<u8>(t + 3);
            u32 fl = GRef<u32>(u + 0x48) | (static_cast<u32>(GRef<u8>(t + 0xA)) << 16);
            GRef<u32>(u + 0x48) = fl;
            GRef<f32>(u + 0x70) = fpu::IntToFloat(GRef<s16>(t + 8));
            GRef<u8>(u + 0x69) = GRef<u8>(t + 0);
            const u8 b = GRef<u8>(t + 0x10);
            GRef<u8>(u + 0x64) = b;
            if ((b & 1) != 0) GRef<u32>(u + 0x48) = fl | 0x1000000;
            else if ((b & 2) != 0) GRef<u32>(u + 0x48) = fl | 0x2000000;
            GRef<u8>(u + 0x65) = GRef<u8>(t + 0x11);
            GRef<u16>(u + 0x60) = GRef<u16>(t + 4);
            GRef<u16>(u + 0x5E) = GRef<u16>(t + 6);
        }
        u8 id = GRef<u8>(u + 0x69);
        if (id == 0x48 || id == 0x45 || id == 0x34 || (id >= 0x3C && id <= 0x40) || id == 0x47 || id == 0x41)
            GRef<u32>(u + 0x48) |= 0x10;
        if (id == 0x39 || id == 0x8A) GRef<u32>(u + 0x48) |= 0x80;
        if (id == 0xB1) GRef<u32>(u + 0x48) |= 0x40;
        if (Game(0x27) == 0x10 && id == 0x39) {
            const u32 list = g_UnitList();
            GRef<u8>(list + GRef<u8>(list + 0x83) + 0x80) = static_cast<u8>(i);
            GRef<u8>(list + 0x83) = static_cast<u8>(GRef<u8>(list + 0x83) + 1);
            id = GRef<u8>(u + 0x69);
        }
        if (Game(0x27) == 0xB && id == 0x41) {
            GRef<u8>(g_UnitList() + 0x83) = static_cast<u8>(GRef<u8>(g_UnitList() + 0x83) + 1);
            id = GRef<u8>(u + 0x69);
        }
        if (static_cast<u8>(id + 0x48) < 3 || id == 0x4A) GRef<u32>(u + 0x48) |= 0x800000;
        const u32 fl = GRef<u32>(u + 0x48);
        if ((fl & 0x8000700) == 0) {
            const u32 v = GRef<u32>(u + 0x4C);
            GRef<u32>(u + 0x4C) = v | 0x10000000;
            if ((fl & 0x20000000) != 0) GRef<u32>(u + 0x4C) = v | 0x30000000;
        }
        if ((fl & 0x8000000) != 0 && (fl & 0x100) == 0 && 1 < Game(0x28) && (GRef<u32>(0x002A0318) & 1) != 0) {
            const u32 v = GRef<u32>(u + 0x4C);
            GRef<u32>(u + 0x4C) = v | 0x10000000;
            if ((fl & 0x20000000) != 0) GRef<u32>(u + 0x4C) = v | 0x30000000;
        }
        if (type == 0) {
            if (i < static_cast<s32>(Game(0x25))) {
                GRef<u32>(u + 0x4C) |= 0x30000000;
                GRef<u32>(u + 0x48) = fl | 0x1330000;
                GRef<u8>(u + 0x54) = 0;
            } else {
                GRef<u32>(u + 0x4C) &= 0xCFFFFFFFu;
            }
            guest::GuestFrame frame(0x50);
            const u32 v = frame.Address(), m = frame.Address(0x10);
            ee::memset(v, 0, 0x10);
            GRef<f32>(v + 8) = GRef<f32>(0x0040DBC4);
            sceVu0UnitMatrix(m);
            func_00102D28(m, m, u + 0x10);
            sceVu0ApplyMatrix(u + 0x20, m, v);
            const u8 pid = Game(static_cast<u32>(i) * 0x10);
            GRef<u8>(u + 0x69) = pid;
            const u16 hp = static_cast<u16>(func_0011E878(pid));
            GRef<u8>(u + 0x58) = 0;
            GRef<u8>(u + 0x5A) = 0;
            GRef<u8>(u + 0x65) = 0;
            GRef<u16>(u + 0x56) |= 0x100;
            GRef<u16>(u + 0x5E) = hp;
            GRef<u8>(u + 0x59) = 0xFF;
            GRef<f32>(u + 0x70) = 50.0f;
            GRef<u16>(u + 0x60) = 100;
            GRef<u8>(u + 0x64) = 1;
            GRef<u8>(u + 0x5B) = 1;
            func_0011E8D8(i);
            func_0011EFA0(i, Game(static_cast<u32>(i) * 0x10 + 3));
        } else if (type == 1) {
            GRef<u8>(u + 0x54) = 1;
            GRef<u16>(u + 0x56) |= 0x100;
            CreateAIAircraftState(i);
            const u32 tr = GRef<u32>(u + 0x40);
            const u32 st = GRef<u32>(u + 0x44);
            const u32 h2 = GRef<u32>(0x004104CC);
            GRef<u32>(st) = GRef<u32>(h2 + 0x38) + GRef<u8>(tr + 0x1D) * 0x34u;
            bool known;
            u8 kind = GRef<u8>(tr + 0x1E);
            GRef<u8>(st + 4) = kind;
            u32 ref = PathRef(h2, kind, GRef<u8>(tr + 0x1F), &known);
            if (kind == 0) GRef<u32>(st + 8) = 0;
            else if (known) GRef<u32>(st + 8) = ref;
            kind = GRef<u8>(tr + 0x20);
            GRef<u8>(st + 5) = kind;
            ref = PathRef(h2, kind, GRef<u8>(tr + 0x21), &known);
            if (kind == 0) GRef<u32>(st + 0xC) = 0;
            else if (known) GRef<u32>(st + 0xC) = ref;
            guest::GuestFrame frame(0x50);
            const u32 v = frame.Address(), m = frame.Address(0x10);
            GRef<u32>(v + 0) = 0;
            GRef<u32>(v + 4) = 0;
            GRef<u32>(v + 0xC) = 0;
            GRef<f32>(v + 8) = GRef<f32>(GRef<u32>(st) + 0xC) * GRef<f32>(0x0040DBC8);
            sceVu0UnitMatrix(m);
            func_00102D28(m, m, u + 0x10);
            sceVu0ApplyMatrix(u + 0x20, m, v);
        } else {
            GRef<u8>(u + 0x54) = 2;
            CreateGroundUnitState(i);
            const u32 tr = GRef<u32>(u + 0x40);
            const u32 st = GRef<u32>(u + 0x44);
            const u8 kind = GRef<u8>(tr + 0x12);
            GRef<u8>(st + 1) = kind;
            const u32 h2 = GRef<u32>(0x004104CC);
            const u32 index2 = GRef<u8>(tr + 0x13);
            switch (kind) {
                case 0: GRef<u32>(st + 0x80) = 0; break;
                case 1: GRef<u32>(st + 0x80) = GRef<u32>(h2 + 0x3C) + index2 * 0x14; break;
                case 2: GRef<u32>(st + 0x80) = GRef<u32>(h2 + 0x40) + (index2 << 4); break;
                case 3: GRef<u32>(st + 0x80) = GRef<u32>(h2 + 0x44) + (index2 << 4); break;
                case 4: {
                    const u32 ref = GRef<u32>(h2 + 0x48) + index2 * 0xC;
                    GRef<u32>(st + 0x80) = ref;
                    const f32 x = NextRandomFloat();
                    const f32 s = fpu::Sub(fpu::Add(x, x), 1.0f);
                    GRef<s32>(st + 0xC) = GRef<s16>(ref + 8) + fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(ref + 0xA)) * s);
                    break;
                }
                case 5: GRef<u32>(st + 0x80) = GRef<u32>(h2 + 0x4C) + (index2 << 5); break;
                default: break;
            }
        }
    }
    const u32 h = GRef<u32>(0x004104CC);
    GRef<u32>(g_UnitList()) = static_cast<u32>(static_cast<s32>(GRef<s16>(GRef<u32>(h) + 4)));
    func_001459E0(GRef<u32>(0x004106A0));
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) func_001279B0(p, 0);
    func_00216550(0, Game(0), 0xFFFF);
    if (Game(0x25) == 2) func_00216550(1, Game(0x10), 0xFFFF);
    func_002165D0(Game(0x25));
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) func_001279B0(p, 1);
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
        const u32 flags = GRef<u32>(GRef<u32>(0x004106A4) + 0x18) + 4 * static_cast<u32>(p);
        GRef<u32>(flags) |= 2;
        GRef<f32>(Unit(static_cast<u32>(p)) + 0x74) = func_00146710(GRef<u32>(0x004106A0), p);
    }
    func_00149E90(GRef<u32>(0x004106A4), 0);
    for (u32 i = 2; static_cast<s32>(i) < static_cast<s32>(UnitCount()); ++i) {
        const u32 u = Unit(i);
        if (GRef<u8>(u + 0x58) == 0xFF) {
            GRef<f32>(u + 0x74) = GRef<f32>(u + 0x70);
        } else {
            const f32 r = func_00146710(GRef<u32>(0x004106A0), static_cast<s32>(i));
            GRef<f32>(u + 0x74) = r;
            if (GRef<f32>(u + 0x70) <= 0.0f) GRef<f32>(u + 0x70) = r;
        }
        if (GRef<u8>(u + 0x58) == 0xFF) GRef<u32>(u + 0x78) = 0;
        else GRef<f32>(u + 0x78) = func_00146788(GRef<u32>(0x004106A0), static_cast<s32>(i));
        if (GRef<f32>(u + 4) == 0.0f && i != GRef<u8>(g_UnitList() + 0x88)) {
            const f32 y = func_0013E338(GRef<u32>(0x00410694), u);
            GRef<f32>(u + 4) = fpu::Sub(y, GRef<f32>(u + 0x78));
        }
    }
    for (u32 p = 0; p < Game(0x25); ++p) {
        const u32 t = WorkHeapAlloc(static_cast<s32>(UnitCount()) * 0xC, 4);
        GRef<u32>(0x00410648 + 4 * p) = t;
        for (u32 k = 0; static_cast<s32>(k) < static_cast<s32>(UnitCount()); ++k) {
            const u32 e = GRef<u32>(0x00410648 + 4 * p) + 0xC * k;
            GRef<u32>(e + 0) = 0;
            GRef<f32>(e + 8) = -1.0f;
            GRef<f32>(e + 4) = 262144.0f;
        }
    }
    for (u32 i = 0; static_cast<s32>(i) < static_cast<s32>(UnitCount()); ++i) {
        const u32 u = Unit(i);
        if ((GRef<u32>(u + 0x48) & 0x400) == 0) GRef<u8>(u + 0x9B) = RadarClass(GRef<u8>(u + 0x69));
    }
    for (u32 i = 0; static_cast<s32>(i) < static_cast<s32>(UnitCount()); ++i) {
        const u32 u = Unit(i);
        u8 v;
        if (TypeIndex(GRef<u8>(u + 0x69), &v)) GRef<u8>(u + 0x99) = v;
    }
    for (u32 i = 0; static_cast<s32>(i) < static_cast<s32>(UnitCount()); ++i) {
        const u32 u = Unit(i);
        if (GRef<u8>(u + 0x54) != 1) continue;
        const u32 st = GRef<u32>(u + 0x44);
        if ((GRef<u32>(u + 0x48) & 3) == 0) {
            const s32 v = fpu::FloatToInt(NextRandomFloat() * GRef<f32>(0x0040DBCC));
            GRef<u8>(st + 0xF5) = static_cast<u8>(v);
            if ((v & 0xFF) == 1) GRef<u8>(st + 0xF6) = 0;
        } else {
            GRef<u8>(st + 0xF5) = static_cast<u8>(fpu::FloatToInt(NextRandomFloat() * GRef<f32>(0x0040DBD0)));
        }
    }
}

}  // namespace ac04::game
