// Small flight systems created and reset on the mission-load path (block A of S5): projectile
// pools, HUD and centre-message records, replay recorder and buffer, mission event / waypoint /
// proximity lists, trial ghost tables, replay HUD and pause menu. Order and sizes:
// docs/subsystems/mission-load.md (the work heap is 98 % full, so every allocation counts).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

u8 Game(u32 i) { return GRef<u8>(g_GameState().addr + i); }
u32 Players() { return Game(0x25); }

// Projectile pools (0x8A50 bytes, gp 0x004106AC).
u32 Proj() { return GRef<u32>(0x004106AC); }

// Replay recorder (0x20 bytes, gp 0x00410AF8).
u32 Replay() { return GRef<u32>(0x00410AF8); }

// Pause menu state (0x10 bytes, gp 0x00410B94).
u32 PauseMenu() { return GRef<u32>(0x00410B94); }

}  // namespace

// ---- Projectiles ------------------------------------------------------------------------------

// EE: 0x0014BC80
// Bullets: 256 x 0x40.
void func_0014BC80() {
    for (u32 k = 0, p = Proj(); k < 0x100; ++k, p += 0x40) {
        GRef<u32>(p + 0x30) = 0;
        GRef<u32>(p + 0x34) = 0;
    }
}

// EE: 0x0014CBD0
// Missiles: 32 x 0x60 at +0x4000 (the word at +0x30 of each, walked from the last).
void func_0014CBD0() {
    for (u32 k = 0; k < 0x20; ++k) GRef<u32>(Proj() + 0x4BF0 - k * 0x60) = 0;
}

// EE: 0x0014DA98
// Flak: 64 x 0x30 at +0x5C00.
void func_0014DA98() {
    for (u32 k = 0, p = Proj() + 0x5C00; k < 0x40; ++k, p += 0x30) {
        GRef<u32>(p + 0) = 0;
        GRef<u32>(p + 4) = 0;
    }
}

// EE: 0x0014DF40
// Shells: 64 x 0x40 at +0x6800.
void func_0014DF40() {
    for (u32 k = 0; k < 0x40; ++k) GRef<u32>(Proj() + 0x77C4 - k * 0x40) = 0;
}

// EE: 0x0014E890
// Bombs: 64 x 0x40 at +0x7800.
void func_0014E890() {
    for (u32 k = 0; k < 0x40; ++k) GRef<u32>(Proj() + 0x87C4 - k * 0x40) = 0;
}

// EE: 0x0014ECA0
// Cannon shells: 8 x 0x40 at +0x8800.
void func_0014ECA0() {
    for (u32 k = 0; k < 8; ++k) GRef<u32>(Proj() + 0x89C0 - k * 0x40) = 0;
}

// EE: 0x0014F5B0
// Special weapons: 32 x 0x80 at +0x4C00.
void func_0014F5B0() {
    for (u32 k = 0, p = Proj() + 0x4C00; k < 0x20; ++k, p += 0x80) {
        GRef<u32>(p + 0x50) = 0;
        GRef<u32>(p + 0x70) = 0;
    }
}

// EE: 0x0014BA10
// Occupancy masks and cursors (+0x8A00..+0x8A4F), then the reset of the 7 projectile kinds.
void ResetProjectiles() {
    const u32 p = Proj();
    GRef<u8>(p + 0x8A4D) = 0;
    GRef<u8>(p + 0x8A4E) = 0;
    GRef<u8>(p + 0x8A4F) = 0;
    GRef<u8>(p + 0x8A4A) = 0;
    GRef<u16>(p + 0x8A44) = 0;
    GRef<u16>(p + 0x8A46) = 0;
    GRef<u16>(p + 0x8A48) = 0;
    GRef<u8>(p + 0x8A4C) = 0;
    GRef<u8>(p + 0x8A4B) = 0;
    for (u32 o = 0x8A18; o >= 0x8A00; o -= 8) GRef<u64>(p + o) = 0;
    GRef<u32>(p + 0x8A40) = 0;
    GRef<u32>(p + 0x8A28) = 0;
    GRef<u32>(p + 0x8A2C) = 0;
    GRef<u64>(p + 0x8A20) = 0;
    GRef<u64>(p + 0x8A30) = 0;
    GRef<u64>(p + 0x8A38) = 0;
    func_0014BC80();
    func_0014CBD0();
    func_0014F5B0();
    func_0014DA98();
    func_0014DF40();
    func_0014E890();
    func_0014ECA0();
}

// EE: 0x0014B9D8
void CreateProjectilePools() {
    GRef<u32>(0x004106AC) = WorkHeapAlloc(0x8A50, 0x10);
    GRef<u32>(0x004106B0) = WorkHeapAlloc(0x160, 0x10);
    ResetProjectiles();
}

// ---- Unit snapshots (replay) -----------------------------------------------------------------

// EE: 0x00135D60
// Snapshot buffers: 0x48 per unit (0x002A2660[]), 0x50 per player (0x00410678[]).
void func_00135D60() {
    for (s32 i = 0; i < GRef<s32>(g_UnitList()); ++i) GRef<u32>(0x002A2660 + 4 * static_cast<u32>(i)) = WorkHeapAlloc(0x48, 4);
    for (u32 p = 0; p < Players(); ++p) GRef<u32>(0x00410678 + 4 * p) = WorkHeapAlloc(0x50, 4);
}

// ---- HUD and centre messages -------------------------------------------------------------------

// EE: 0x0015E818
// HUD / lock state of every player: message ring, warning state, radar mode, map-explored rows.
void func_0015E818() {
    for (u32 p = 0; p < Players(); ++p) {
        const u32 h = GRef<u32>(0x00410AA0 + 4 * p);
        GRef<u8>(h + 0x6F) = 0xFF;
        GRef<u8>(h + 0x40) = 0;
        GRef<u8>(h + 0x41) = 0;
        GRef<u8>(h + 0x6C) = 0xFF;
        GRef<u8>(h + 0x6D) = 0xFF;
        GRef<u8>(h + 0x6E) = 0xFF;
        GRef<u8>(h + 0x70) = 0;
        GRef<u8>(h + 0x72) = 0;
        GRef<u16>(h + 0x42) = 0;
        GRef<u16>(h + 0x64) = 0;
        GRef<u16>(h + 0x66) = 0;
        GRef<u16>(h + 0x68) = 0;
        GRef<u16>(h + 0x6A) = 0;
        for (u32 o = 0x62; o >= 0x44; o -= 2) GRef<u16>(h + o) = 0xFFFF;
    }
}

// EE: 0x0015E7B0
void func_0015E7B0() {
    for (u32 p = 0; p < Players(); ++p) GRef<u32>(0x00410AA0 + 4 * p) = WorkHeapAlloc(0x74, 0x10);
    func_0015E818();
}

// EE: 0x0015E8C0
// Map-explored rows (16 halfwords, mission block 28 part 2) into every player's HUD record.
void func_0015E8C0(u32 rows) {
    for (u32 p = 0; p < Players(); ++p) {
        const u32 h = GRef<u32>(0x00410AA0 + 4 * p);
        for (u32 k = 0; k < 16; ++k) GRef<u16>(h + 0x44 + 2 * k) = GRef<u16>(rows + 2 * k);
    }
}

// EE: 0x0016DA60
void func_0016DA60() {}

// EE: 0x0016DA78
// Centre-message state: 6 bytes per player (0x00410AF0[]).
void func_0016DA78() {
    for (u32 p = 0; p < Players(); ++p) GRef<u32>(0x00410AF0 + 4 * p) = WorkHeapAlloc(6, 4);
}

// EE: 0x0016DAE0
void func_0016DAE0() {
    for (u32 p = 0; p < Players(); ++p) {
        const u32 m = GRef<u32>(0x00410AF0 + 4 * p);
        GRef<u8>(m + 4) = 0xFF;
        GRef<u16>(m + 0) = 0;
        GRef<u16>(m + 2) = 0;
        GRef<u8>(m + 5) = 0xFF;
    }
}

// ---- Replay recorder ---------------------------------------------------------------------------

// EE: 0x0016FEE8
// Replay copy of the projectile pools.
void func_0016FEE8() {
    GRef<u32>(0x00410AFC) = WorkHeapAlloc(0x8A50, 0x10);
}

// EE: 0x00170460
// Replay copy of the effect block: AllocEffectBlock runs with its pointer (0x004106B4) swapped
// with the replay one (0x00410B00), then the effect slot table; the pointers are swapped back.
void func_00170460() {
    GRef<u32>(0x00410B00) = GRef<u32>(0x004106B4);
    AllocEffectBlock();
    GRef<u32>(0x00410B04) = WorkHeapAlloc(0xA0, 4);
    const u32 replay = GRef<u32>(0x004106B4);
    GRef<u32>(0x004106B4) = GRef<u32>(0x00410B00);
    GRef<u32>(0x00410B00) = replay;
}

// EE: 0x0016DE20
void func_0016DE20() {
    GRef<u32>(0x00410AF8) = WorkHeapAlloc(0x20, 0x10);
    func_0016FEE8();
    func_00170460();
}

// EE: 0x0016DE48
// Replay buffer: all the remaining work heap in 256 KiB units minus one unit; demo playback uses
// the demo block, mission kind 4 a 0x400-byte buffer.
void func_0016DE48() {
    GRef<u32>(Replay() + 0x1C) = 0x200000;
    const s32 free = static_cast<s32>(GetWorkHeapFree());
    const s32 size = (free / 0x40000 - 1) * 0x40000;
    const u32 r = Replay();
    GRef<s32>(r + 0x1C) = size;
    if (Game(0x29) == 4) {
        const u32 begin = GRef<u32>(g_FlightScene() + 0x17D4);
        const u32 end = GRef<u32>(g_FlightScene() + 0x17D8);
        GRef<u32>(r + 4) = begin;
        GRef<u32>(r + 0x1C) = end - begin;
    } else if (GRef<s8>(g_FlightScene() + 0x1653) == 4) {
        const u32 b = WorkHeapAlloc(0x400, 0x10);
        GRef<u32>(Replay() + 0x1C) = 0x400;
        GRef<u32>(Replay() + 4) = b;
    } else {
        const u32 b = WorkHeapAlloc(size, 0x10);
        GRef<u32>(Replay() + 4) = b;
    }
}

// EE: 0x0016DEF8
// Replay read/write pointers back to the buffer start; the buffer is zeroed (128-bit stores)
// except in demo playback.
void func_0016DEF8() {
    const u32 r = Replay();
    u32 p = GRef<u32>(r + 4);
    GRef<u16>(r + 0) = 0;
    GRef<u32>(r + 0x10) = p;
    GRef<u32>(r + 0xC) = p;
    GRef<u32>(r + 8) = p;
    GRef<u32>(r + 0x14) = 0;
    GRef<u32>(r + 0x18) = 0;
    if (Game(0x29) == 4) return;
    for (s32 k = 0; k < GRef<s32>(Replay() + 0x1C) / 16; ++k, p += 0x10) {
        GRef<u64>(p) = 0;
        GRef<u64>(p + 8) = 0;
    }
}

// ---- Mission event lists -----------------------------------------------------------------------

// EE: 0x00176908
// Mission event queue counters.
void func_00176908() {
    const u32 q = GRef<u32>(0x00410B08);
    GRef<u32>(q + 0x18) = 0;
    GRef<u32>(q + 0x14) = 0;
}

// EE: 0x0017B2C8
// Waypoint list (4 entries of 0x20, count at +0x80).
void func_0017B2C8() {
    GRef<u32>(0x00410B28) = WorkHeapAlloc(0x90, 0x10);
}

// EE: 0x0017B2F0
void func_0017B2F0() {
    const u32 l = GRef<u32>(0x00410B28);
    GRef<u8>(l + 0x80) = 0;
    for (u32 k = 0; k < 4; ++k) {
        GRef<u32>(l + 0x20 * k + 0x18) = 0;
        GRef<u32>(l + 0x20 * k + 0x14) = 0x1E;
    }
}

// EE: 0x0017B708
// Proximity-message list A.
void func_0017B708() {
    GRef<u32>(0x00410B2C) = WorkHeapAlloc(0x90, 0x10);
}

// EE: 0x0017B730
void func_0017B730() {
    const u32 l = GRef<u32>(0x00410B2C);
    GRef<u32>(l + 0x84) = 0;
    GRef<u32>(l + 0x80) = 0;
}

// EE: 0x0017B868
// Ordered proximity-message list (mission id 1).
void func_0017B868() {
    GRef<u32>(0x00410B30) = WorkHeapAlloc(0xD0, 0x10);
}

// EE: 0x0017B890
void func_0017B890() {
    const u32 l = GRef<u32>(0x00410B30);
    GRef<u32>(l + 0xCC) = 0;
    GRef<u32>(l + 0xC8) = 0;
}

// EE: 0x0017B978
// Proximity list C (mission id 8).
void func_0017B978() {
    GRef<u32>(0x00410B34) = WorkHeapAlloc(0xB0, 0x10);
}

// EE: 0x0017B9A0
void func_0017B9A0() {
    const u32 l = GRef<u32>(0x00410B34);
    GRef<u32>(l + 0xA4) = 0;
    GRef<u32>(l + 0xA0) = 0;
}

// EE: 0x0017BB38
// Trial mode record buffers: ghost data 0x002A0D34, recording 0x002B7C38.
void func_0017BB38() {
    GRef<u32>(0x00410B38) = 0x002A0D34;
    GRef<u32>(0x00410B3C) = 0x002B7C38;
}

// EE: 0x0017BC70
// Trial mode: ghost record table by trial id 0x15-0x19 (0x200 bytes each, 0x80 records; the last
// 0x20) and the recording buffer filled with 0xFFFF.
void func_0017BC70() {
    GRef<u32>(0x002B7C2C) = GRef<u32>(0x00410B3C);
    GRef<u32>(0x002B7C20) = 0;
    GRef<u32>(0x002B7C24) = 0;
    const u8 id = Game(0x27);
    const u32 ghost = GRef<u32>(0x00410B38);
    if (id >= 0x15 && id <= 0x19) {
        GRef<u32>(0x002B7C30) = id == 0x19 ? 0x20 : 0x80;
        GRef<u32>(0x002B7C28) = ghost + (static_cast<u32>(id) - 0x15) * 0x200;
    } else {
        GRef<u32>(0x002B7C30) = 0;
        GRef<u32>(0x002B7C28) = 0;
    }
    for (u32 k = 0, p = GRef<u32>(0x00410B3C); k < 0x80; ++k, p += 4) {
        GRef<u16>(p) = 0xFFFF;
        GRef<u16>(p + 2) = 0xFFFF;
    }
}

// EE: 0x0017BF90
// Tutorial hint mask.
void func_0017BF90() {
    GRef<u32>(0x004117E4) = 0;
}

// ---- Replay / pause HUD ------------------------------------------------------------------------

// EE: 0x0017CBE0
void func_0017CBE0(u32 r) {
    const u32 a = GRef<u32>(0x0040EC48), b = GRef<u32>(0x0040EC4C), c = GRef<u32>(0x0040EC50);
    GRef<u8>(r + 0x52) = 0xFF;
    GRef<u16>(r + 0x5C) = 0xFF19;
    GRef<u16>(r + 0x5E) = 0xB7;
    GRef<u32>(r + 0x00) = a;
    GRef<u32>(r + 0x10) = b;
    GRef<u32>(r + 0x20) = c;
    GRef<u32>(r + 0x30) = 0x3F800000;
    GRef<u32>(r + 0x3C) = 0;
    GRef<u8>(r + 0x50) = 0xFF;
    GRef<u8>(r + 0x51) = 0xFF;
    GRef<u8>(r + 0x53) = 0;
    GRef<u8>(r + 0x54) = 0;
    GRef<u8>(r + 0x55) = 0;
    for (u32 o = 0x40; o <= 0x4C; o += 4) GRef<u32>(r + o) = 0;
    GRef<u8>(r + 0x58) = 0;
    GRef<u16>(r + 0x5A) = 0;
    GRef<u8>(r + 0x59) = 0;
    for (u32 o : {0x04u, 0x08u, 0x0Cu, 0x14u, 0x18u, 0x1Cu, 0x24u, 0x28u, 0x2Cu, 0x34u, 0x38u}) GRef<u32>(r + o) = 0;
}

// EE: 0x0017CCA8
// Replay / pause HUD textures: the three GIM sets of mission block 1 (items 1, 2, 0), TBP 0x2500,
// CBP 0x3F3C. Each pointer is allocated even for an empty set.
void func_0017CCA8(u32 hud, u32 data) {
    if (data == 0) return;
    guest::GuestFrame frame(0x20);
    const u32 top = frame.Address(), sub = frame.Address(0x10);
    InitContainerReader(top, data);
    if (GRef<s32>(top) != 3) return;
    static const struct { s32 item; u32 count; u32 ptr; } kSets[] = {{1, 0x53, 0x40}, {2, 0x54, 0x44}, {0, 0x55, 0x48}};
    for (const auto& s : kSets) {
        InitContainerReader(sub, GetContainerItem(top, s.item));
        const u8 n = GRef<u8>(sub);
        GRef<u8>(hud + s.count) = n;
        GRef<u32>(hud + s.ptr) = WorkHeapAlloc(n * 0x28, 0x10);
        for (u32 k = 0; k < GRef<u8>(hud + s.count); ++k) {
            const u32 tex = GRef<u32>(hud + s.ptr) + k * 0x28;
            InitGimTexture(tex, GetContainerItem(sub, static_cast<s32>(k)));
            SetTextureVramAddress(GRef<u32>(hud + s.ptr) + k * 0x28, 0x2500, 0x3F3C);
        }
    }
}

// EE: 0x0017D8B0
void func_0017D8B0() {
    GRef<u32>(0x00410B40) = WorkHeapAlloc(0x60, 0x10);
    func_0017CBE0(GRef<u32>(0x00410B40));
    func_0017CCA8(GRef<u32>(0x00410B40), GRef<u32>(g_FlightScene() + 0x1698));
}

// ---- Pause menu ------------------------------------------------------------------------------

// EE: 0x0017E8B0
// Animation targets of a pause-menu item and (when the menu is open on it) of its sub-items.
void func_0017E8B0(u32 item) {
    const u32 m = PauseMenu();
    if (GRef<s8>(m + 4) == 0) {
        const u32 e = GRef<u32>(item);
        GRef<u32>(e + 4) = GRef<u32>(item + 0x1C);
        GRef<u32>(e + 8) = GRef<u32>(item + 0x20);
        GRef<u32>(e + 0x10) = GRef<u32>(item + 0x24);
        GRef<u32>(e + 0x14) = GRef<u32>(item + 0x28);
        GRef<s32>(e + 0x24) = (GRef<s32>(item + 0x28) - GRef<s32>(item + 0x20)) / 6;
        const u32 v = GRef<u32>(item + 0x18) == 0 ? 8 : 0;
        GRef<u32>(e + 0x18) = v;
        GRef<u32>(e + 0xC) = v;
    }
    if (GRef<s8>(m + 4) != 1) return;
    const u32 e = GRef<u32>(item);
    const bool wide = (Ctx()->flags & 0x10000) != 0;
    const s32 x = GRef<s32>(e + 0x10) + GRef<s32>(e + (wide ? 0x20 : 0x1C)) + 0x40;
    for (s32 j = 0; j < GRef<s32>(item + 0x14); ++j) {
        const u32 s = GRef<u32>(item + 4 + 4 * static_cast<u32>(j));
        GRef<s32>(s + 4) = x;
        const s32 y0 = GRef<s32>(e + 0x14);
        GRef<s32>(s + 0x10) = x;
        GRef<s32>(s + 8) = y0;
        const s32 y1 = GRef<s32>(e + 0x14) + j * 0x180;
        GRef<s32>(s + 0x14) = y1;
        GRef<s32>(s + 0x24) = (y1 - y0) / 6;
        const u32 v = j == GRef<s8>(m + 6) ? 0 : 8;
        GRef<u32>(s + 0x18) = v;
        GRef<u32>(s + 0xC) = v;
    }
}

// EE: 0x0017E7D0
// Pause-menu layout: column x (16:9 aware), rows from y, the selected item flagged.
void func_0017E7D0(u32 list) {
    const s32 x = (Ctx()->flags & 0x10000) != 0 ? 0xAA0 : 0x8E0;
    const s32 y = (GRef<u32>(list) & 1) != 0 ? 0x300 : 0x140;
    for (s32 i = 0; i < GRef<s32>(list + 4); ++i) {
        const u32 item = GRef<u32>(list + 8 + 4 * static_cast<u32>(i));
        GRef<s32>(item + 0x1C) = x;
        GRef<s32>(item + 0x20) = y + 0xC00;
        GRef<s32>(item + 0x28) = i * 0x180 + y + 0xC00;
        GRef<s32>(item + 0x24) = x;
        if (i == GRef<s8>(PauseMenu() + 5)) GRef<u32>(item + 0x18) = 1;
        func_0017E8B0(item);
    }
}

// EE: 0x0017DCC0
// Pause-menu reset: fades off, menu state cleared, item list by mission kind (S+0x1653), replay
// length (> 180 frames: REPLAY item) and the game-cleared bit (HUD item).
void func_0017DCC0() {
    const u32 s = g_FlightScene();
    if (GRef<f32>(s + 0x1670) != 0.0f) func_0010A130(0.0f, 0.5f, 0);
    if (GRef<f32>(g_FlightScene() + 0x1674) != 0.0f) func_0010A130(0.0f, 0.5f, 1);
    const u32 m = PauseMenu();
    GRef<u8>(m + 4) = 0;
    GRef<u8>(m + 5) = 0;
    GRef<u8>(m + 6) = 0;
    GRef<u32>(m + 0xC) = 0;
    GRef<u32>(m + 8) = 0;
    const s8 kind = GRef<s8>(g_FlightScene() + 0x1653);
    const bool replay = GRef<s32>(Replay() + 0x14) >= 0xB5;
    const bool cleared = (GRef<u32>(0x002A0318) & 1) != 0;
    u32 list;
    if (kind == 0) {
        list = replay ? 0x002B8370 : 0x002B8390;
        GRef<u32>(list + 4) = (replay ? 5 : 4) + (cleared ? 1 : 0);
    } else if (kind == 1) {
        list = replay ? 0x002B83D0 : 0x002B83F0;
        GRef<u32>(list + 4) = (replay ? 4 : 3) + (cleared ? 1 : 0);
    } else if (kind == 2) {
        list = 0x002B8410;
    } else if (kind == 3) {
        list = 0x002B83B0;
    } else {
        list = 0x002B8370;
    }
    func_0017E7D0(list);
}

// EE: 0x0017DC98
void func_0017DC98() {
    GRef<u32>(0x00410B94) = WorkHeapAlloc(0x10, 4);
    func_0017DCC0();
}

// EE: 0x0014C310
// Bullet tracers (docs/subsystems/rendering.md): ALPHA_1 = Cs * As + Cd, then per live bullet of
// the projectile block *0x004106AC a two-segment line from the head to the tail (head colour, a
// fainter middle, transparent tail), bucket 0xC.
// fpucheck: 2 add (G, B); 1 sub per bullet via sceVu0SubVector
void func_0014C310(u32 cam, u32 ot) {
    guest::GuestFrame frame(0x30);
    const u32 t = frame.Address(), h = t + 0x10, c = t + 0x20;
    u32 p = AllocBucketPacket(ot, 0xC, 2, 0, 0x50000002);
    GRef<u64>(p + 0) = 0x1000400000008001ull;
    GRef<u64>(p + 8) = 0xE;
    p += 0x10;
    GRef<u64>(p + 8) = 0x42;
    GRef<u64>(p + 0) = 0x48;
    for (u32 i = 0; i < 0x100; ++i) {
        const u32 b = GRef<u32>(0x004106AC);
        const u64 occ = GRef<u64>(b + 0x8A00 + 8 * (i >> 6));
        if (((~occ >> (i & 63)) & 1) != 0) continue;
        const u32 e = b + 0x40 * i;
        sceVu0RotTransPers(h, cam + 0xE0, e, 1);
        if (GRef<s32>(h + 8) < 0 || GRef<s32>(h + 0xC) < 0x10) continue;
        sceVu0ScaleVector(t, e + 0x20, GRef<f32>(0x0040E528));
        sceVu0SubVector(t, e, t);
        GRef<f32>(t + 0xC) = 1.0f;
        sceVu0RotTransPers(c, cam + 0xE0, t, 1);
        if (GRef<s32>(c + 8) < 0 || GRef<s32>(c + 0xC) < 0x10) continue;
        if (GRef<u32>(h + 0) > 0x10000 || GRef<s32>(h + 4) > 0x10000 || GRef<s32>(h + 4) < 0) continue;
        if (GRef<u32>(c + 0) > 0x10000 || GRef<s32>(c + 4) > 0x10000 || GRef<s32>(c + 4) < 0) continue;
        const f32 f = fpu::Div(fpu::IntToFloat(GRef<s32>(e + 0x34)) * GRef<f32>(0x0040E52C), GRef<f32>(0x0040E530));
        s32 a = 0x90 - fpu::FloatToInt(f * 144.0f);
        if (a < 4) continue;
        a -= static_cast<s32>(static_cast<u32>(a) * GRef<u32>(h + 0xC)) / 0x2800;  // mult (low word), div
        if (a < 4) continue;
        s32 g = fpu::FloatToInt(fpu::Add(f * 200.0f, 192.0f));
        s32 bc = fpu::FloatToInt(fpu::Add(f * 128.0f, 140.0f));
        if (g >= 0x100) g = 0xFF;
        if (bc > 0xC0) bc = 0xC0;
        const s32 a2 = a / 32;
        auto mid = [&](u32 o) { return static_cast<u32>(static_cast<s32>(GRef<u32>(h + o) + GRef<u32>(c + o)) / 2); };
        const u32 mx = mid(0), my = mid(4), mz = mid(8);
        auto put = [](u32 q, u32 w0, u32 w1, u32 w2, u32 w3) {
            GRef<u32>(q + 0) = w0;
            GRef<u32>(q + 4) = w1;
            GRef<u32>(q + 8) = w2;
            GRef<u32>(q + 0xC) = w3;
        };
        p = AllocBucketPacket(ot, 0xC, 5, 0, 0x50000005);
        GRef<u64>(p + 0) = 0x4024C00000008001ull;
        GRef<u64>(p + 8) = 0x5151;
        put(p + 0x10, 0xFF, static_cast<u32>(g), static_cast<u32>(bc), static_cast<u32>(a));
        put(p + 0x20, GRef<u32>(h + 0), GRef<u32>(h + 4), GRef<u32>(h + 8), 0);
        put(p + 0x30, 0xFF, static_cast<u32>(g), static_cast<u32>(bc), static_cast<u32>(a2));
        put(p + 0x40, mx, my, mz, 0);
        p = AllocBucketPacket(ot, 0xC, 5, 0, 0x50000005);
        GRef<u64>(p + 0) = 0x4024C00000008001ull;
        GRef<u64>(p + 8) = 0x5151;
        put(p + 0x10, 0xFF, static_cast<u32>(g), static_cast<u32>(bc), static_cast<u32>(a2));
        put(p + 0x20, mx, my, mz, 0);
        put(p + 0x30, 0, 0, 0, 0);
        put(p + 0x40, GRef<u32>(c + 0), GRef<u32>(c + 4), GRef<u32>(c + 8), 0);
    }
}

}  // namespace ac04::game
