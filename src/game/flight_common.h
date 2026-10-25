// Shared accessors for the scene 2 (flight) code of slice S5 block B and later: the gp globals the
// flight code reads on every line (unit table, flight scene object, mission rules/status, aircraft
// records, world objects) and small qword helpers. Internal to src/game; addresses are the
// original .sdata/.bss locations (gp = 0x004159F0).
#pragma once

#include "common/ee_math.h"
#include "common/guest_ref.h"
#include "common/guest_stack.h"
#include "common/types.h"
#include "game/guest_helpers.h"
#include "game/main_context.h"

namespace ac04::game::fl {

using guest::GRef;

// Unit table 0x002A1F70 (255 pointers to 0xA0-byte unit structs) and the unit list header
// (gp 0x00410640: +0 count, +0x83 kill counter, +0x88 carrier unit index, +0x4C camera objects).
inline u32 Unit(s32 i) { return GRef<u32>(0x002A1F70 + 4u * static_cast<u32>(i)); }
inline u32 UnitList() { return GRef<u32>(0x00410640); }
inline s32 UnitCount() { return GRef<s32>(UnitList()); }
// Per-player aircraft parameter record (gp 0x00410650[p]).
inline u32 AircraftRec(s32 p) { return GRef<u32>(0x00410650 + 4u * static_cast<u32>(p)); }

inline u32 Scene() { return GRef<u32>(0x004104C4); }    // flight scene object, 0x1810 bytes
inline u32 Rules() { return GRef<u32>(0x004104F0); }    // mission rules, 0x70 bytes
inline u32 Status() { return GRef<u32>(0x004104F4); }   // mission status, 0x90 bytes
inline u32 Events() { return GRef<u32>(0x004104F8); }   // mission event tables
inline u32 Terrain() { return GRef<u32>(0x00410694); }  // terrain object
inline u32 WorldObjs() { return GRef<u32>(0x00410698); }
inline u32 SeaObj() { return GRef<u32>(0x0041069C); }
inline u32 UnitModels() { return GRef<u32>(0x004106A0); }
inline u32 AircraftModels() { return GRef<u32>(0x004106A4); }
inline u32 Radio() { return GRef<u32>(0x00410B28); }

inline u32 StatusFlags() { return GRef<u32>(Status() + 0x20); }
inline u8 MissionKind() { return GRef<u8>(Scene() + 0x1653); }

inline u8 Game(u32 i) { return GRef<u8>(g_GameState().addr + i); }
inline void SetGame(u32 i, u8 v) { GRef<u8>(g_GameState().addr + i) = v; }
inline u8 Players() { return Game(0x25); }

inline u32 CtxAddr() { return g_pMainContext().addr; }
inline u16 Sub() { return Ctx()->sub; }
inline u32 PadExt(s32 p) { return CtxAddr() + 0x2D0 + 0x20u * static_cast<u32>(p); }
inline u32 PadState(s32 p) { return CtxAddr() + 0x210 + 0x50u * static_cast<u32>(p); }

inline f32 F(u32 a) { return GRef<f32>(a); }
inline u32 W(u32 a) { return GRef<u32>(a); }

inline void CopyQ(u32 dst, u32 src) {
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(dst + o) = GRef<u32>(src + o);
}
inline void ZeroQ(u32 dst) {
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(dst + o) = 0;
}

// The rate response shared by the flight-model channels: `rate` moves towards `target` with the
// gain k / (resp * resp) and the damping `damp` (functions 0x001282B8 … 0x00128E80, 0x00129B58).
// fpucheck: 6 adds per executed path as in the original blocks.
inline f32 RateResponse(u32 rate_addr, f32 target, f32 resp, f32 damp, f32 k_up, f32 k_down) {
    f32 v;
    if (0.0f < target) {
        v = F(rate_addr);
        if (v < target) {
            v = fpu::Add(v, target / resp / resp * k_up);
            GRef<f32>(rate_addr) = v;
        }
        if (v < 0.0f) {
            v = fpu::Sub(v, v * damp);
            GRef<f32>(rate_addr) = v;
        }
        if (target < v) {
            v = fpu::Sub(v, fpu::Sub(v, target) * damp);
            GRef<f32>(rate_addr) = v;
        }
    } else if (target < 0.0f) {
        v = F(rate_addr);
        if (target < v) {
            v = fpu::Add(v, target / resp / resp * k_down);
            GRef<f32>(rate_addr) = v;
        }
        if (0.0f < v) {
            v = fpu::Sub(v, v * damp);
            GRef<f32>(rate_addr) = v;
        }
        if (v < target) {
            v = fpu::Sub(v, fpu::Sub(v, target) * damp);
            GRef<f32>(rate_addr) = v;
        }
    } else {
        v = fpu::Sub(F(rate_addr), F(rate_addr) * damp);
        GRef<f32>(rate_addr) = v;
    }
    return v;
}

}  // namespace ac04::game::fl
