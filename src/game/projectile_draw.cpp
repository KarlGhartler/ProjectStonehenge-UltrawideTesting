// Projectile drawing and the effect helpers of the in-engine sequences (slices S5d / S5i):
// DrawProjectiles and its per-kind drawers, ResetEffects and the cutscene effect spawns.
#include "game/game.h"

#include "common/ee_math.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Eff() { return W(0x004106B4); }  // effect block

}  // namespace

// EE: 0x0014D798
// Missiles draw: every live missile record (pool W(0x004106AC) +0x4000 + 0x60 i, mask +0x8A28, not
// hidden by +0x5C bit 0) as model 0x0014AA78 at its position / rotation; the last argument is 0 for
// the missile the owner's camera follows (unit +0x44 -> +0x162 of players 0 / 1), else 1.
void func_0014D798(u32 cam, u32 list) {
    s32 tracked[2];
    for (u32 k = 0; k < 2; ++k) tracked[k] = GRef<s16>(W(W(0x002A1F70 + k * 4) + 0x44) + 0x162);
    for (s32 i = 0; i < 0x20; ++i) {
        const u32 pool = W(0x004106AC);
        if (((~W(pool + 0x8A28) >> i) & 1) != 0) continue;
        const u32 rec = pool + 0x4000 + static_cast<u32>(i) * 0x60;
        if ((W(rec + 0x5C) & 1) != 0) continue;
        const u8 k = GRef<u8>(rec + 0x58);
        s32 shown = 1;
        if (k < 2 && tracked[k] == i) shown = 0;
        func_0014AA78(W(0x004106A8), cam, list, rec + 0x10, rec, k, shown);
    }
}

// EE: 0x0014DBC0
// Flak draw: empty in the original.
void func_0014DBC0(u32 /*cam*/, u32 /*list*/) {}

// EE: 0x0014E068
// Shells draw: empty in the original.
void func_0014E068(u32 /*cam*/, u32 /*list*/) {}

// EE: 0x0014E9B8
// Bombs draw: empty in the original.
void func_0014E9B8(u32 /*cam*/, u32 /*list*/) {}

// EE: 0x0014EE50
// Cannon shells draw: an empty 8-pass loop in the original.
void func_0014EE50(u32 /*cam*/, u32 /*list*/) {}

// EE: 0x0014F940
// Special weapons draw: every live record (pool +0x4C00 + 0x80 i, mask +0x8A2C, not hidden by +0x70
// bit 0, kind byte +0x5B not 7 or 12) as model 0x0014ACB0 with the owner's aircraft variant
// (unit +0x44 -> +0x172); the weapon the owner's camera tracks (+0x178, only while the aircraft flag
// +0x188 bit 1 and the view flag 0x00410680[owner] +0xC bit 1 are set) gets 0, others 1.
void func_0014F940(u32 cam, u32 list) {
    for (s32 i = 0; i < 0x20; ++i) {
        const u32 pool = W(0x004106AC);
        if (((~W(pool + 0x8A2C) >> i) & 1) != 0) continue;
        const u32 rec = pool + 0x4C00 + static_cast<u32>(i) * 0x80;
        if ((W(rec + 0x70) & 1) != 0) continue;
        const u8 kind = GRef<u8>(pool + static_cast<u32>(i) * 0x80 + 0x4C5B);
        if (kind == 7 || kind == 0xC) continue;
        const u8 k = GRef<u8>(rec + 0x58);
        const u32 ac = W(W(0x002A1F70 + static_cast<u32>(k) * 4) + 0x44);
        s32 shown = 1;
        if ((W(ac + 0x188) & 2) != 0 && (W(W(0x00410680 + static_cast<u32>(k) * 4) + 0xC) & 2) != 0 && GRef<s16>(ac + 0x178) == i)
            shown = 0;
        func_0014ACB0(W(0x004106A8), cam, list, rec + 0x10, rec, k, shown, GRef<s8>(ac + 0x172));
    }
}

// EE: 0x0014BB88
// DrawProjectiles: bullets (0x0014C310), missiles, specials, flak, shells, bombs, cannon shells,
// then in bucket 9 an A+D packet (GIF tag 0x1000400000008001, data 5 to register 8) and a FLUSH
// packet.
void DrawProjectiles(u32 cam, u32 list) {
    func_0014C310(cam, list);
    func_0014D798(cam, list);
    func_0014F940(cam, list);
    func_0014DBC0(cam, list);
    func_0014E068(cam, list);
    func_0014E9B8(cam, list);
    func_0014EE50(cam, list);
    const u32 p = AllocBucketPacket(list, 9, 2, 0, 0x50000002);
    GRef<u64>(p) = 0x1000400000008001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 5;
    GRef<u64>(p + 0x18) = 8;
    const u32 q = BeginPacket();
    GRef<u32>(q + 4) = 0;
    GRef<u32>(q) = 0x10000000;
    GRef<u32>(q + 8) = 0;
    GRef<u32>(q + 0xC) = 0x11000000;
    EndPacket(list, 9, q + 0x10);
}

// EE: 0x00154A48
// ResetEffects: resets every effect pool of the effect state W(0x004106B4) (the same per-pool
// reset functions as at mission start), in this order. Called by the in-engine sequence set-up.
void ResetEffects() {
    func_00155BC8(Eff());
    func_00155C50(Eff() + 0x2010);
    func_00155CB8(Eff() + 0x30700);
    func_00155D40(Eff() + 0x31710);
    func_00155DC8(Eff() + 0x38320);
    func_00155E50(Eff() + 0x22220);
    func_00155ED8(Eff() + 0x22C30);
    func_00155F60(Eff() + 0x22E40);
    func_00155FE8(Eff() + 0x24650);
    func_00156070(Eff() + 0x247E0);
    func_001560F8(Eff() + 0x268E0);
    func_00156180(Eff() + 0x267F0);
    func_00156208(Eff() + 0x274F0);
    func_00156290(Eff() + 0x28400);
    func_00156318(Eff() + 0x2B430);
    func_001563A0(Eff() + 0x2B6C0);
    func_00156428(Eff() + 0x2DCD0);
    func_001564B0(Eff() + 0x2E2E0);
    func_00156538(Eff() + 0x2E4F0);
    func_001565C0(Eff() + 0x38B30);
    func_00156648(Eff() + 0x61940);
    func_001566D0(Eff() + 0x3FF90);
    func_00156758(Eff() + 0x5FEE0);
    func_001567E0(Eff() + 0x61F00);
    func_00156868(Eff() + 0x5E490);
    func_001568F0(Eff() + 0x62410);
    func_00156978(Eff() + 0x5D880);
    func_00156A00(Eff() + 0x61BE0);
    func_00156A88(Eff() + 0x5E8A0);
    func_00156B10(Eff() + 0x5F2C0);
    func_00156B98(Eff() + 0x61420);
    func_00156C20(Eff() + 0x4A830);
    func_00156CB0(Eff() + 0x604F0);
    func_00156D38(Eff() + 0x447D0);
    func_00156DC0(Eff() + 0x61AD0);
    func_00156E48(Eff() + 0x39F40);
    func_00156ED0(Eff() + 0x5EAB0);
    func_00156F58(Eff() + 0x5D070);
    func_00156FE0(Eff() + 0x60D00);
    func_00157068(Eff() + 0x5B050);
    func_001570F0(Eff() + 0x5FAD0);
    func_00157178(Eff() + 0x61010);
    func_00157200(Eff() + 0x47800);
    func_00157288(Eff() + 0x61630);
    func_00157310(Eff() + 0x61DF0);
}

// EE: 0x001D8098
// Allocates the lowest free record of an 8-record effect pool (occupancy mask pool+0x200, count
// +0x204): sets the bit, counts it and returns the index; -1 (0xFFFFFFFF) when all 8 are used.
s32 func_001D8098(u32 pool) {
    u32 mask = W(pool + 0x200);
    if (mask != 0xFFFFFFFFu) {
        for (s32 i = 0; i < 8; ++i, mask >>= 1) {
            if ((mask & 1) == 0) {
                GRef<u32>(pool + 0x200) = W(pool + 0x200) | (1u << i);
                GRef<s32>(pool + 0x204) = GRef<s32>(pool + 0x204) + 1;
                return i;
            }
        }
    }
    return -1;
}

// EE: 0x001C9768
// Cutscene effect for the sequence after mission 18 (mission id 0x24 only): allocates a record of
// the pool Eff()+0x61BE0 (0x40 bytes; an index of 8 or more would use record 7), sets +0x2C = a,
// resets it (0x001C0010), then position (w 1.0), velocity and +0x28 = b.
void func_001C9768(u32 pos, u32 vel, f32 a, f32 b) {
    if (GRef<u8>(g_GameState().addr + 0x27) != 0x24) return;
    const s32 h = func_001D8098(Eff() + 0x61BE0);
    if (h == -1) return;
    const u32 pool = Eff() + 0x61BE0;
    const u32 rec = static_cast<u32>(h) < 8 ? pool + (static_cast<u32>(h) << 6) : pool + 0x1C0;
    GRef<f32>(rec + 0x2C) = a;
    func_001C0010(rec);
    CopyQ(rec, pos);
    GRef<f32>(rec + 0xC) = 1.0f;
    CopyQ(rec + 0x10, vel);
    GRef<f32>(rec + 0x28) = b;
}

// EE: 0x001E9540
// Cutscene explosion `n`: the effect kind from table 0x002BA2C0 [n] at `pos` / `vel` (0x001F08E8).
void func_001E9540(u32 pos, u32 vel, s32 n) {
    func_001F08E8(pos, vel, GRef<s32>(0x002BA2C0 + static_cast<u32>(n) * 4));
}

}  // namespace ac04::game
