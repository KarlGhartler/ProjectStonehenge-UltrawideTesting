// World render objects created on the mission-load path (block A of S5): terrain object
// (*0x00410694), sea surface (*0x0041068C), the per-frame sequence-model list (*0x00410698) and
// their packet templates. docs/subsystems/rendering.md "Terrain", "Sea"; allocation order:
// docs/subsystems/mission-load.md (CreateFlightSystems).
#include "game/game.h"

#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x001390D0
// Terrain packet templates: 4 texture-level records at +0x2B0 (0x70 each: DMA cnt + DIRECT,
// TEX0_1 (TBP 0x2900 + level offsets, TBW 8 >> level, PSM 0x13), CLAMP_1, TEX1 and MIPTBP
// defaults), the far / near LOD GIF headers at +0x130..+0x1AC, the per-vertex attribute bytes
// +0x150..+0x24F and the strip template +0x250..+0x29C.
void func_001390D0(u32 t) {
    u32 p = t + 0x2B0;
    s32 tbw = 8, umsk = 0x100, step = 0x400, size = 0x40;
    u32 tbp = 0x2900;
    for (u32 level = 0; level < 4; ++level, p += 0x70) {
        const u32 tex0 = tbp | static_cast<u32>(tbw) << 16 | 0x13000000u;
        tbw /= 2;
        GRef<u32>(p + 0x10) = 4;
        GRef<u32>(p + 0x14) = 0x10000000;
        GRef<s32>(p + 0x30) = size;
        GRef<s32>(p + 0x34) = size;
        GRef<u32>(p + 0x24) = tex0;
        GRef<u32>(p + 0x60) = static_cast<u32>(umsk) | 0x8000;
        GRef<u32>(p + 0x00) = 0x10000006;
        GRef<u32>(p + 0x04) = 0;
        GRef<u32>(p + 0x08) = 0;
        GRef<u32>(p + 0x0C) = 0x50000006;
        GRef<u32>(p + 0x18) = 0xE;
        GRef<u32>(p + 0x20) = tex0;
        GRef<u32>(p + 0x28) = 0x50;
        GRef<u32>(p + 0x38) = 0x52;
        GRef<u32>(p + 0x40) = 0;
        GRef<u32>(p + 0x44) = 0;
        GRef<u32>(p + 0x48) = 0x51;
        GRef<u32>(p + 0x50) = 0;
        GRef<u32>(p + 0x58) = 0x53;
        GRef<u32>(p + 0x64) = 0x08000000;
        GRef<u32>(p + 0x68) = 0;
        tbp += static_cast<u32>(step);
        umsk /= 4;
        step /= 4;
        size /= 2;
    }
    GRef<u32>(t + 0x130) = 0x10000000;
    GRef<u32>(t + 0x138) = 0x6E01C017;
    GRef<u32>(t + 0x140) = 0x10000005;
    GRef<u32>(t + 0x14C) = 0x6E10C020;
    GRef<u32>(t + 0x134) = 0;
    GRef<u32>(t + 0x144) = 0;
    GRef<u32>(t + 0x148) = 0x01000404;
    // Vertex attribute bytes (four per vertex record of 0x10), in the order of the original.
    static const u8 kBytes[][2] = {
        {0x58, 0}, {0x59, 0}, {0x5A, 0}, {0x5B, 1}, {0x68, 2}, {0x69, 0}, {0x6A, 0}, {0x6B, 1},
        {0x78, 2}, {0x79, 0}, {0x7A, 2}, {0x7B, 1}, {0x88, 0}, {0x89, 0}, {0x8A, 2}, {0x8B, 1},
        {0x52, 1}, {0x62, 1}, {0x72, 1}, {0x82, 1}, {0x5C, 0}, {0x5D, 0}, {0x5E, 1}, {0x5F, 0},
        {0x6C, 2}, {0x6D, 0}, {0x6E, 1},
    };
    for (const auto& b : kBytes) GRef<u8>(t + 0x100 + b[0]) = b[1];
    GRef<u32>(t + 0x1A0) = 0x1000000B;
    GRef<u32>(t + 0x1A8) = 0x01000404;
    GRef<u32>(t + 0x1AC) = 0x6E28C020;
    static const u8 kBytes2[][2] = {
        {0x6F, 0}, {0x7C, 2}, {0x7D, 2}, {0x7E, 1}, {0x7F, 0}, {0x8C, 0}, {0x8D, 2}, {0x8E, 1}, {0x8F, 0},
    };
    for (const auto& b : kBytes2) GRef<u8>(t + 0x100 + b[0]) = b[1];
    GRef<u32>(t + 0x190) = 0x01000303;
    GRef<u32>(t + 0x194) = 0;
    GRef<u32>(t + 0x198) = 0;
    GRef<u32>(t + 0x19C) = 0x17000000;
    GRef<u32>(t + 0x1A4) = 0;
    for (u32 o = 0x1B2; o <= 0x242; o += 0x10) GRef<u8>(t + o) = 1;
    static const u16 kBytes3[][2] = {
        {0x1B8, 1}, {0x1B9, 0}, {0x1BA, 1}, {0x1BB, 1}, {0x1C8, 0}, {0x1C9, 0}, {0x1CA, 0}, {0x1CB, 1},
        {0x1D8, 1}, {0x1D9, 0}, {0x1DA, 0}, {0x1DB, 1}, {0x1E8, 2}, {0x1E9, 0}, {0x1EA, 0}, {0x1EB, 1},
        {0x1F8, 2}, {0x1F9, 0}, {0x1FA, 1}, {0x1FB, 1}, {0x208, 2}, {0x209, 0}, {0x20A, 2}, {0x20B, 1},
        {0x218, 1}, {0x219, 0}, {0x21A, 2}, {0x21B, 1}, {0x228, 0}, {0x229, 0}, {0x22A, 2}, {0x22B, 1},
        {0x238, 0}, {0x239, 0}, {0x23A, 1}, {0x23B, 1}, {0x248, 0}, {0x249, 0}, {0x24A, 0}, {0x24B, 1},
        {0x1BC, 1}, {0x22D, 2}, {0x1BD, 1}, {0x1BE, 1}, {0x1BF, 0}, {0x1CC, 0}, {0x1CD, 0}, {0x1CE, 1},
        {0x1CF, 0}, {0x1DC, 1}, {0x1DD, 0}, {0x1DE, 1}, {0x1DF, 0}, {0x1EC, 2}, {0x1ED, 0}, {0x1EE, 1},
        {0x1EF, 0}, {0x1FC, 2}, {0x1FD, 1}, {0x1FE, 1}, {0x1FF, 0}, {0x20C, 2}, {0x20D, 2}, {0x20E, 1},
        {0x20F, 0}, {0x21C, 1}, {0x21D, 2}, {0x21E, 1}, {0x21F, 0}, {0x22C, 0}, {0x22E, 1}, {0x22F, 0},
        {0x23C, 0}, {0x23D, 1}, {0x23E, 1},
    };
    for (const auto& b : kBytes3) GRef<u8>(t + b[0]) = static_cast<u8>(b[1]);
    GRef<u32>(t + 0x29C) = 0x6C01C013;
    GRef<u8>(t + 0x24E) = 1;
    GRef<u32>(t + 0x250) = 0x01000303;
    GRef<u32>(t + 0x254) = 0x17000000;
    GRef<u32>(t + 0x258) = 0x11000000;
    GRef<u32>(t + 0x260) = 0x10000002;
    GRef<u32>(t + 0x26C) = 0x6C01800C;
    GRef<f32>(t + 0x27C) = 1.0f;
    GRef<u32>(t + 0x280) = 0x6E028014;
    GRef<u32>(t + 0x290) = 0x10000001;
    GRef<u8>(t + 0x23F) = 0;
    GRef<u8>(t + 0x24C) = 0;
    GRef<u8>(t + 0x24D) = 0;
    GRef<u8>(t + 0x24F) = 0;
    for (u32 o : {0x25Cu, 0x264u, 0x268u, 0x274u, 0x28Cu, 0x294u, 0x298u}) GRef<u32>(t + o) = 0;
}

// EE: 0x00139520
// Terrain object buffers: map entry table (200 bytes, two 0x64-byte entries with a 0x40 block at
// +0x58), LOD and cell lists, light set and texture state; render defaults; templates; the
// terrain light set (*0x00410688) with the environment texture of mission block S+0x16E0.
void func_00139520(u32 t) {
    GRef<u32>(t) = WorkHeapAlloc(200, 0x10);
    for (u32 k = 0; k < 2; ++k) GRef<u32>(GRef<u32>(t) + k * 100 + 0x58) = WorkHeapAlloc(0x40, 0x10);
    GRef<u32>(t + 0x100) = WorkHeapAlloc(0x1000, 0x10);
    GRef<u32>(t + 0x104) = WorkHeapAlloc(0x40, 0x10);
    GRef<u32>(t + 0x110) = WorkHeapAlloc(0x2000, 0x10);
    GRef<u32>(t + 0x114) = WorkHeapAlloc(0x4000, 0x10);
    GRef<u32>(t + 0x108) = WorkHeapAlloc(0x1000, 0x10);
    GRef<u32>(t + 0x10C) = WorkHeapAlloc(0x1000, 0x10);
    GRef<u32>(t + 0x74) = WorkHeapAlloc(0x2000, 0x10);
    GRef<u32>(t + 0x78) = WorkHeapAlloc(0x2000, 0x10);
    GRef<u32>(t + 0x80) = WorkHeapAlloc(0x80, 0x10);
    GRef<u32>(t + 0x84) = WorkHeapAlloc(0x80, 0x10);
    GRef<u32>(t + 0x124) = WorkHeapAlloc(0x2000, 0x10);
    GRef<u32>(t + 0x128) = WorkHeapAlloc(0x2000, 0x10);
    GRef<u32>(t + 0x12C) = WorkHeapAlloc(0x800, 0x10);
    const u32 buf = WorkHeapAlloc(0x400, 0x10);
    const u32 a = GRef<u32>(0x0040E4F0), b = GRef<u32>(0x0040E4F4);
    for (u32 o : {0x88u, 0x8Cu, 0x90u, 0x94u, 0xACu, 0xB0u, 0xF4u, 0xB8u}) GRef<u32>(t + o) = 0;
    GRef<u32>(t + 0x118) = buf;
    GRef<u32>(t + 0x4A0) = 1;
    GRef<u32>(t + 0xA8) = 0x45800000;  // 4096.0f
    GRef<u32>(t + 0xA4) = a;
    GRef<u32>(t + 0x4A4) = b;
    GRef<u16>(t + 0xF0) = 0xF30;
    GRef<u32>(t + 0xA0) = 0x80F0C080;
    GRef<u32>(t + 0x4B0) = 0x80202020;
    GRef<u32>(t + 0x9C) = 0x80000000;
    GRef<u8>(t + 0xB4) = 0x40;
    GRef<u8>(t + 0xB5) = 0x7F;
    GRef<u8>(t + 0xB6) = 0x20;
    GRef<u16>(t + 0xEC) = 0xF30;
    func_001390D0(t);
    InitLightSet(GRef<u32>(0x00410688));
    SetLightTexture(GRef<u32>(0x00410688), GRef<u32>(g_FlightScene() + 0x16E0), 0x1E00, 0x3F08);
}

// EE: 0x00137550
// Once per boot: the 256-entry noise table 0x002A2D00 = 2 r / (2^31 - 1) - 1 from libc rand()
// (soft-float doubles, rounded to float), entry 256 = entry 0.
void func_00137550() {
    if (GRef<u32>(0x00410690) != 0) return;
    GRef<u32>(0x00410690) = 1;
    for (u32 k = 0; k < 0x100; ++k) {
        const u32 r = static_cast<u32>(ee::rand());
        double d = static_cast<double>(static_cast<s32>(r & 0x7FFFFFFF)) / 2147483647.0;
        d = d + d;
        d = d - 1.0;
        GRef<f32>(0x002A2D00 + 4 * k) = static_cast<f32>(d);
    }
    GRef<u32>(0x002A3100) = GRef<u32>(0x002A2D00);
}

// EE: 0x001381F8
// Sea surface object: two 0x40C0 grids, a 0x420 block, four 0x40000 vertex buffers, wave
// parameters, the noise table and the VIF/GIF header words.
void func_001381F8(u32 s) {
    GRef<u32>(s + 0) = WorkHeapAlloc(0x40C0, 0x10);
    GRef<u32>(s + 4) = WorkHeapAlloc(0x40C0, 0x10);
    GRef<u32>(s + 8) = WorkHeapAlloc(0x420, 0x10);
    for (u32 k = 0; k < 4; ++k) GRef<u32>(s + 0x20 + 4 * k) = WorkHeapAlloc(0x40000, 0x10);
    GRef<u32>(s + 0xC) = 2;
    GRef<u32>(s + 0x30) = 0;
    GRef<u8>(s + 0x14) = 10;
    GRef<u8>(s + 0x15) = 3;
    GRef<u8>(s + 0x17) = 2;
    GRef<u8>(s + 0x10) = 10;
    GRef<u8>(s + 0x11) = 10;
    GRef<u8>(s + 0x12) = 10;
    GRef<u8>(s + 0x13) = 10;
    GRef<u8>(s + 0x16) = 3;
    func_00137550();
    GRef<u32>(s + 0x48) = 2;
    GRef<u32>(s + 0x38) = 0x10000003;
    GRef<u32>(s + 0x44) = 0x50000003;
    GRef<u32>(s + 0x4C) = 0x10000000;
    GRef<u32>(s + 0x50) = 0xE;
    GRef<u32>(s + 0x68) = 0x58;
    GRef<u32>(s + 0x70) = 0x42;
    GRef<u32>(s + 0x78) = 0x98007C00;
    GRef<u32>(s + 0x88) = 0x98007D00;
    GRef<u32>(s + 0x8C) = 5;
    GRef<u32>(s + 0x90) = 6;
    GRef<u32>(s + 0x94) = 0;
    GRef<u32>(s + 0x3C) = 0;
    GRef<u32>(s + 0x40) = 0;
    GRef<u32>(s + 0x58) = 0x58;
    GRef<u32>(s + 0x5C) = 0;
    GRef<u32>(s + 0x60) = 0x42;
    GRef<u32>(s + 0x64) = 0;
    GRef<u32>(s + 0x6C) = 0;
    GRef<u32>(s + 0x74) = 0;
    GRef<u32>(s + 0x7C) = 5;
    GRef<u32>(s + 0x80) = 6;
    GRef<u32>(s + 0x84) = 0;
}

namespace {

// Reads `count` sub-containers of `reader` into a table of item-pointer tables (dst_table + k*4 =
// table of sub-container k's items), as the map loaders do for the cell-list variants and lists.
void LoadItemTables(u32 reader, u32 sub_reader, u32 dst_table, s32 count) {
    for (s32 k = 0; k < count; ++k) {
        InitContainerReader(sub_reader, GetContainerItem(reader, k));
        GRef<u32>(dst_table + 4 * static_cast<u32>(k)) = WorkHeapAlloc(GRef<s32>(sub_reader) * 4, 4);
        for (s32 i = 0; i < GRef<s32>(sub_reader); ++i)
            GRef<u32>(GRef<u32>(dst_table + 4 * static_cast<u32>(k)) + 4 * static_cast<u32>(i)) =
                GetContainerItem(sub_reader, i);
    }
}

// Models of a map entry: count x (bound model 0x28, instance 0x4C); instances at +0x38, count +0x48.
void LoadEntryModels(u32 e, u32 reader, u32 container) {
    InitContainerReader(reader, container);
    u32 rec = WorkHeapAlloc(GRef<s32>(reader) * 0x28, 0x10);
    GRef<u32>(e + 0x38) = WorkHeapAlloc(GRef<s32>(reader) * 0x4C, 0x10);
    for (s32 i = 0; i < GRef<s32>(reader); ++i, rec += 0x28) {
        BindModelData(rec, GetContainerItem(reader, i));
        CreateModelInstance(GRef<u32>(e + 0x38) + static_cast<u32>(i) * 0x4C, rec);
    }
    GRef<s32>(e + 0x48) = GRef<s32>(reader);
}

// Per-cell lists of a map entry (+0x3C, count +0x4C).
void LoadEntryCellLists(u32 e, u32 reader, u32 sub_reader, u32 container) {
    InitContainerReader(reader, container);
    GRef<u32>(e + 0x3C) = WorkHeapAlloc(GRef<s32>(reader) * 4, 4);
    LoadItemTables(reader, sub_reader, GRef<u32>(e + 0x3C), GRef<s32>(reader));
    GRef<s32>(e + 0x4C) = GRef<s32>(reader);
}

// Terrain texture of a map entry (GIM record +0x40; TBP 0x3A00, CBP 0x3F20).
void LoadEntryTexture(u32 e, u32 gim) {
    GRef<u32>(e + 0x40) = WorkHeapAlloc(0x28, 0x10);
    InitGimTexture(GRef<u32>(e + 0x40), gim);
    const u32 clut = GRef<u32>(GRef<u32>(e + 0x40) + 8);
    GRef<u16>(GRef<u32>(GRef<u32>(e + 0x40) + 4)) = 0x3A00;
    if (clut != 0) GRef<u16>(clut) = 0x3F20;
}

// Item table of a map entry (+0x44, count +0x50).
void LoadEntryItems(u32 e, u32 reader, u32 container) {
    InitContainerReader(reader, container);
    GRef<u32>(e + 0x44) = WorkHeapAlloc(GRef<s32>(reader) * 4, 0x10);
    for (s32 i = 0; i < GRef<s32>(reader); ++i)
        GRef<u32>(GRef<u32>(e + 0x44) + 4 * static_cast<u32>(i)) = GetContainerItem(reader, i);
    GRef<s32>(e + 0x50) = GRef<s32>(reader);
}

}  // namespace

// EE: 0x00139718
// Loads map entry n (0x64 bytes) from a container: items 0-6 and 8 (blocks), the 4 cell-list
// variants (item 7), 9, 10, models (11), per-cell lists (12), texture (13), items (14). Flag +0x60
// bit 0 = present (an empty container leaves the entry absent).
void func_00139718(u32 t, u32 container, s32 n) {
    const u32 e = GRef<u32>(t) + static_cast<u32>(n) * 100;
    GRef<u32>(e + 0x60) = 0;
    guest::GuestFrame frame(0x30);
    const u32 c = frame.Address(), r1 = frame.Address(0x10), r2 = frame.Address(0x20);
    InitContainerReader(c, container);
    if (GRef<s32>(c) == 0) return;
    GRef<u32>(e + 0x60) |= 1;
    for (s32 k = 0; k < 7; ++k) GRef<u32>(e + 4 * static_cast<u32>(k)) = GetContainerItem(c, k);
    GRef<u32>(e + 0x1C) = GetContainerItem(c, 8);
    if (GRef<s32>(GetContainerItem(c, 7)) == 4) {
        InitContainerReader(r1, GetContainerItem(c, 7));
        LoadItemTables(r1, r2, e + 0x20, 4);
    } else {
        const u32 v = GetContainerItem(c, 7);
        GRef<u32>(e + 0x20) = v;
        GRef<u32>(e + 0x2C) = v;
        GRef<u32>(e + 0x28) = v;
        GRef<u32>(e + 0x24) = v;
    }
    GRef<u32>(e + 0x30) = GetContainerItem(c, 9);
    GRef<u32>(e + 0x34) = GetContainerItem(c, 10);
    LoadEntryModels(e, r1, GetContainerItem(c, 0xB));
    LoadEntryCellLists(e, r1, r2, GetContainerItem(c, 0xC));
    GRef<u32>(e + 0x40) = WorkHeapAlloc(0x28, 0x10);
    InitGimTexture(GRef<u32>(e + 0x40), GetContainerItem(c, 0xD));
    const u32 clut = GRef<u32>(GRef<u32>(e + 0x40) + 8);
    GRef<u16>(GRef<u32>(GRef<u32>(e + 0x40) + 4)) = 0x3A00;
    if (clut != 0) GRef<u16>(clut) = 0x3F20;
    LoadEntryItems(e, r1, GetContainerItem(c, 0xE));
}

// EE: 0x00139038
// Terrain render settings from a map entry's settings block (fog, sky, light, flags +0xF4).
void func_00139038(u32 t, u32 rs) {
    GRef<u32>(t + 0x88) = GRef<u32>(rs + 8);
    GRef<u32>(t + 0x9C) = GRef<u32>(rs + 0x28);
    GRef<u32>(t + 0x8C) = GRef<u32>(rs + 0xC);
    GRef<u16>(t + 0xEC) = GRef<u16>(rs + 4);
    GRef<u32>(t + 0x90) = GRef<u32>(rs + 0x10);
    GRef<u32>(t + 0x4B0) = GRef<u32>(rs + 0x30);
    GRef<u32>(t + 0x94) = GRef<u32>(rs + 0x14);
    GRef<u16>(t + 0xF0) = GRef<u16>(rs + 6);
    GRef<u32>(t + 0xAC) = GRef<u32>(rs + 0x18);
    GRef<u32>(t + 0xF4) = GRef<u32>(rs + 0x34);
    GRef<u32>(t + 0xB0) = GRef<u32>(rs + 0x1C);
    GRef<u8>(t + 0xB4) = GRef<u8>(rs + 0x38);
    GRef<u8>(t + 0xB5) = GRef<u8>(rs + 0x39);
    GRef<u8>(t + 0xB6) = GRef<u8>(rs + 0x3A);
}

// EE: 0x001390B0
// Selects map entry n's render settings (gp 0x004104D4) and applies them to the sky.
void func_001390B0(u32 t, s32 n) {
    GRef<u32>(0x004104D4) = GRef<u32>(GRef<u32>(t) + static_cast<u32>(n) * 100 + 0x5C);
    func_00111950();
}

// EE: 0x00139FE8
// Current map entry (t+4) -> terrain object: block pointers, the 4 cell-list variants, texture;
// sun direction (0, 0, 1) turned by the angles at +0x88 (x) and +0x8C (y), read before this entry's
// settings are applied, into +0x40; then the entry's settings.
void func_00139FE8(u32 t) {
    const u32 e = GRef<u32>(t) + GRef<u32>(t + 4) * 100;
    GRef<u32>(t + 0x08) = GRef<u32>(e + 0x00);
    GRef<u32>(t + 0x18) = GRef<u32>(e + 0x10);
    GRef<u32>(t + 0x0C) = GRef<u32>(e + 0x04);
    GRef<u32>(t + 0x20) = GRef<u32>(e + 0x18);
    GRef<u32>(t + 0x10) = GRef<u32>(e + 0x08);
    GRef<u32>(t + 0x34) = GRef<u32>(e + 0x1C);
    GRef<u32>(t + 0x14) = GRef<u32>(e + 0x0C);
    GRef<u32>(t + 0x1C) = GRef<u32>(e + 0x14);
    for (u32 k = 0; k < 4; ++k) GRef<u32>(t + 0x24 + 4 * k) = GRef<u32>(e + 0x20 + 4 * k);
    GRef<u32>(t + 0xF8) = GRef<u32>(e + 0x40);
    guest::GuestFrame frame(0x50);
    const u32 m = frame.Address(), v = frame.Address(0x40);
    ee::memset(v, 0, 0x10);
    GRef<f32>(v + 8) = 1.0f;
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, GRef<f32>(t + 0x88));
    sceVu0RotMatrixY(m, m, GRef<f32>(t + 0x8C));
    sceVu0ApplyMatrix(t + 0x40, m, v);
    func_00139038(t, GRef<u32>(e + 0x54));
}

// EE: 0x00139B70
// Builds the map object from the mission blocks: entry 0 from blocks 5-18 (S+0x16A8..+0x16DC),
// entry 1 from the container S+0x1710 (settings S+0x1714 / S+0x1718), entry 0 selected, view
// constants; an MTXT header (block 20) older than version 0x6F prints a warning and gets its +0x24
// and the top byte of +0x2C patched.
void func_00139B70(u32 t) {
    const u32 s = g_FlightScene();
    const u32 e = GRef<u32>(t);
    const u32 lod = GRef<u32>(s + 0x16B0);
    GRef<u32>(e + 0x60) = 1;
    GRef<u32>(e + 0x00) = GRef<u32>(s + 0x16BC);
    GRef<u32>(e + 0x04) = GRef<u32>(s + 0x16B8);
    GRef<u32>(e + 0x08) = GRef<u32>(s + 0x16C4);
    GRef<u32>(e + 0x0C) = GRef<u32>(s + 0x16C0);
    GRef<u32>(e + 0x10) = GRef<u32>(s + 0x16C8);
    GRef<u32>(e + 0x14) = GRef<u32>(s + 0x16AC);
    GRef<u32>(e + 0x18) = GRef<u32>(s + 0x16A8);
    GRef<u32>(e + 0x1C) = GRef<u32>(s + 0x16B4);
    guest::GuestFrame frame(0x20);
    const u32 r1 = frame.Address(), r2 = frame.Address(0x10);
    if (GRef<s32>(lod) == 4) {
        InitContainerReader(r1, lod);
        LoadItemTables(r1, r2, e + 0x20, 4);
    } else {
        GRef<u32>(e + 0x20) = lod;
        GRef<u32>(e + 0x2C) = lod;
        GRef<u32>(e + 0x28) = lod;
        GRef<u32>(e + 0x24) = lod;
    }
    GRef<u32>(e + 0x30) = GRef<u32>(g_FlightScene() + 0x16D0);
    GRef<u32>(e + 0x34) = GRef<u32>(g_FlightScene() + 0x16CC);
    LoadEntryModels(e, r1, GRef<u32>(g_FlightScene() + 0x16D4));
    LoadEntryCellLists(e, r1, r2, GRef<u32>(g_FlightScene() + 0x16D8));
    LoadEntryTexture(e, GRef<u32>(g_FlightScene() + 0x1700));
    LoadEntryItems(e, r1, GRef<u32>(g_FlightScene() + 0x16DC));
    const u32 e1 = GRef<u32>(t) + 0x64;
    GRef<u32>(e + 0x5C) = GRef<u32>(g_FlightScene() + 0x16F4);
    GRef<u32>(e + 0x54) = GRef<u32>(g_FlightScene() + 0x16E4);
    func_00139718(t, GRef<u32>(g_FlightScene() + 0x1710), 1);
    GRef<u32>(t + 4) = 0;
    GRef<u32>(e1 + 0x54) = GRef<u32>(g_FlightScene() + 0x1714);
    GRef<u32>(e1 + 0x5C) = GRef<u32>(g_FlightScene() + 0x1718);
    func_00139FE8(t);
    const u32 mtxt = GRef<u32>(GRef<u32>(t) + 0x54);
    GRef<f32>(t + 0x50) = 8192.0f;
    GRef<u32>(t + 0x54) = GRef<u32>(0x0040E4F8);
    GRef<u32>(t + 0x58) = GRef<u32>(0x0040E4FC);
    GRef<f32>(t + 0x5C) = 32768.0f;
    if (GRef<s32>(mtxt) < 0x6F) {
        // Debug text only; the double is passed as its bit pattern (EE soft-float ABI).
        const double d = static_cast<double>(fpu::IntToFloat(GRef<s32>(t + 0xB8)) * GRef<f32>(0x0040E500));
        u64 bits;
        std::memcpy(&bits, &d, 8);
        const u64 args[2] = {bits, GRef<u64>(0x004046C0)};
        ee::printf(0x00404690 /* "WARNING : MTXT version old! : %3.2f(%3.2f)\n" */, args, 2);
        const u32 hi = static_cast<u32>(GRef<u16>(GRef<u32>(0x004104D4) + 0x12E)) << 24;
        const u32 w = (GRef<u32>(mtxt + 0x2C) & 0x00FFFFFFu) | hi;
        GRef<u32>(mtxt + 0x24) = GRef<u32>(mtxt + 0x20);
        GRef<u32>(mtxt + 0x2C) = w;
    }
}

// EE: 0x0013EA28
void CreateWorldRenderObjects() {
    func_00139520(GRef<u32>(0x00410694));
    func_00139B70(GRef<u32>(0x00410694));
    func_00144728(GRef<u32>(0x0041069C));
    func_00147BF0(GRef<u32>(0x004106A4));
    func_001381F8(GRef<u32>(0x0041068C));
    GRef<u32>(0x00410698) = WorkHeapAlloc(0x120, 0x10);
}

// ---- Per-frame sequence-model list (*0x00410698) ------------------------------------------------

// EE: 0x0013EC20
void func_0013EC20(u32 l) {
    GRef<u32>(l + 0xC) = 0;
    GRef<u32>(l + 0x118) = 0;
    GRef<u32>(l + 0) = 0;
    GRef<u32>(l + 4) = 0;
    GRef<u32>(l + 8) = 0;
}

// EE: 0x0013EC38
// Appends a model list (pointer +0x18[n], count +0x98[n]).
void func_0013EC38(u32 l, u32 models, u32 count) {
    const u32 e = l + GRef<u32>(l + 0x118) * 4;
    GRef<u32>(e + 0x98) = count;
    GRef<u32>(e + 0x18) = models;
    GRef<s32>(l + 0x118) = GRef<s32>(l + 0x118) + 1;
}

// EE: 0x00144228
// Model data: texture / packet base (header +0x34 -> +0xC).
u32 func_00144228(u32 model) {
    return GRef<u32>(GRef<u32>(model + 0x34) + 0xC);
}

}  // namespace ac04::game
