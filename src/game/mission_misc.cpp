// Mission support (slice S5e): carrier landing / takeoff / rearm segments of mission id 3
// (docs/subsystems/mission-flow.md), waypoint and radio trigger markers (records 0x00410B28,
// 0x00410B2C, 0x00410B30, 0x00410B34), the trial best-run track (0x002B7C20), tutorial hint
// requests, and the in-mission menus (pause / game over / rearm: menu state 0x00410B94, lists
// 0x002B8370-0x002B8450, item callbacks called indirectly from the item records).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_call.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Menu() { return W(0x00410B94); }
u32 Markers2() { return W(0x00410B2C); }
u32 Markers3() { return W(0x00410B30); }
u32 Markers4() { return W(0x00410B34); }

// Copies the per-unit flags (unit +0x4C) of units 2.. back from the save table 0x002B7820.
void RestoreUnitFlags() {
    for (s32 i = 2; i < UnitCount(); ++i) GRef<u32>(Unit(i) + 0x4C) = W(0x002B7820 + 4u * static_cast<u32>(i));
}

// The pause-menu list by mission kind and replay length (as func_0017DCC0).
u32 PauseList() {
    const s8 kind = GRef<s8>(Scene() + 0x1653);
    const bool replay = !(GRef<s32>(W(0x00410AF8) + 0x14) < 0xB5);
    if (kind == 1) return replay ? 0x002B83D0 : 0x002B83F0;
    if (kind == 0) return replay ? 0x002B8370 : 0x002B8390;
    if (kind == 2) return 0x002B8410;
    if (kind == 3) return 0x002B83B0;
    return 0x002B8370;
}

}  // namespace

// EE: 0x00176918
// Allocates the per-player target-lock record (0x20 bytes, 0x00410B10) and resets it.
void func_00176918() {
    GRef<u32>(0x00410B10) = WorkHeapAlloc(0x20, 4);
    func_00176940();
}

// EE: 0x001798D8
// Landing segment entry: player state +0x80 = unit 0 position, target cleared, state +0x188
// bits 8-15 cleared; the flags of units 2.. are saved to 0x002B7820 and the units retired except
// the carrier group (flags48 0x400: active, with 0x20000000 also lockable).
void func_001798D8() {
    const u32 u = Unit(0);
    const u32 s = W(u + 0x44);
    CopyQ(s + 0x80, u);
    GRef<u8>(u + 0x62) = 0xFF;
    GRef<u32>(s + 0x188) &= 0xFFFF00FFu;
    for (s32 i = 2; i < UnitCount(); ++i) {
        const u32 o = Unit(i);
        GRef<u32>(0x002B7820 + 4u * static_cast<u32>(i)) = W(o + 0x4C);
        if ((W(o + 0x48) & 0x400u) == 0) {
            GRef<u32>(o + 0x4C) = 0;
        } else {
            const u32 f = W(o + 0x4C);
            GRef<u32>(o + 0x4C) = f | 0x10000000u;
            if ((W(o + 0x48) & 0x20000000u) != 0) GRef<u32>(o + 0x4C) = f | 0x30000000u;
        }
    }
}

// EE: 0x00179998
// Landing segment exit: the player goes back to state +0x80 x, start-point height and the rules
// z (+0x1C), lowered to 4096 under the lowest terrain of 3 x 4 samples around (0x0013E820),
// heading 0, speed C 0x0040EBD4 along the nose; unit flags restored.
// fpucheck: 3 adds
void func_00179998() {
    const u32 u = Unit(0);
    const u32 s = W(u + 0x44);
    GRef<u32>(u) = W(s + 0x80);
    GRef<u32>(u + 4) = W(W(W(0x004104CC) + 4) + 8);
    GRef<u32>(s + 0x188) |= 0x800000u;
    GRef<f32>(u + 0xC) = 1.0f;
    GRef<u32>(u + 8) = W(Rules() + 0x1C);
    f32 low = 0.0f;
    guest::GuestFrame fr(0x50);
    const u32 p = fr.Address(0), m = fr.Address(0x10);
    for (s32 i = -1; i < 2; ++i) {
        const f32 dx = fpu::IntToFloat(i) * 2048.0f;
        for (s32 j = -1; j < 3; ++j) {  // 3 x 4 samples, as the original
            GRef<f32>(p + 4) = 0.0f;
            GRef<f32>(p) = fpu::Add(F(u), dx);
            GRef<f32>(p + 0xC) = 1.0f;
            GRef<f32>(p + 8) = fpu::Add(F(u + 8), fpu::IntToFloat(j) * 2048.0f);
            const f32 h = func_0013E820(Terrain(), p);
            if (h < low) low = h;
        }
    }
    low = fpu::Sub(low, 4096.0f);
    if (low < F(u + 4)) GRef<f32>(u + 4) = low;
    ZeroQ(u + 0x10);
    GRef<u32>(u + 0x2C) = 0;
    GRef<u32>(u + 0x28) = W(0x0040EBD4);
    GRef<u32>(u + 0x20) = 0;
    GRef<u32>(u + 0x24) = 0;
    ZeroQ(s);
    GRef<u32>(s + 0x134) = 0;
    GRef<u32>(s + 0x138) = 0;
    GRef<u32>(s + 0x13C) = 0;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, u + 0x10);
    sceVu0ApplyMatrix(u + 0x20, m, u + 0x20);
    RestoreUnitFlags();
}

// EE: 0x00179BC0
// Carrier landing segment start (sub 0x14): saves the units once (status bit 0x10000000) and the
// player hp (0x004117DC), timer 0x2A30, approach point and flag 0x2000000 / 0x1000000 by the
// aircraft (models 2, 4, 8, 0xE, 0x15 use the short approach), carrier map entry 1, sounds,
// player reset 0x00179D70, fades.
void func_00179BC0() {
    const u32 u = Unit(0);
    if ((~StatusFlags() & 0x10000000u) != 0) {
        GRef<u32>(Status() + 0x20) = StatusFlags() | 0x10000000u;
        func_001798D8();
        GRef<s32>(0x004117DC) = GRef<s16>(Unit(0) + 0x5E);
    }
    const u32 st = Status();
    GRef<u32>(st + 0x38) = 0x2A30;
    GRef<u32>(st + 0x20) = W(st + 0x20) & 0xBFFFFFFFu;
    const u32 r = Rules();
    const u8 m = GRef<u8>(u + 0x69);
    u32 x, y, z, f;
    if (m == 2 || m == 4 || m == 8 || m == 0xE || m == 0x15) {
        f = W(st + 0x20) | 0x2000000u;
        x = 0x44800000u;  // 1024
        y = 0xC2BF0000u;  // -95.5
        z = W(0x0040EBD8);
    } else {
        f = W(st + 0x20) | 0x1000000u;
        x = 0x46620000u;  // 14464
        y = 0xC4700000u;  // -960
        z = 0x46000000u;  // 8192
    }
    GRef<u32>(st + 0x20) = f;
    GRef<f32>(r + 0xC) = 1.0f;
    GRef<u32>(r) = x;
    GRef<u32>(r + 4) = y;
    GRef<u32>(r + 8) = z;
    GRef<u32>(Terrain() + 4) = 1;
    func_001390B0(Terrain(), 1);
    func_00149E90(AircraftModels(), 1);
    func_00179D70();
    func_001279B0(0, 0);
    StopAllVoices();
    func_0021F190(5.0f, 0, 0, 0, 2);
    func_00216C58();
    func_002559C8();
    GRef<u32>(Scene() + 0x1668) |= 0x100u;
    ResetProjectiles();
    func_00136570(0);
    func_00136570(1);
    func_0016DAE0();
    func_0017D848(W(0x00410B40), 1, 1);
}

// EE: 0x00179D70
// Places the player behind the carrier for the landing approach: hp from 0x004117DC, weapon
// and lock timers cleared, position at distance C 0x0040EBDC from the rules point (angle and drop
// randomised by difficulty g_GameState[0x28]: 2 random reads, none at 0/1 or when mission 3 is in
// phase 2), speed C 0x0040EBF8, gear/animation tracks cleared, HUD mask cleared.
// fpucheck: 16 adds
void func_00179D70() {
    const u32 u = Unit(0);
    const u32 s = W(u + 0x44);
    GRef<u16>(u + 0x5E) = GRef<u16>(0x004117DC);
    GRef<u32>(s + 0x188) |= 0x800000u;
    const f32 dist = F(0x0040EBDC);
    GRef<u32>(u + 0x50) = 0;
    GRef<u32>(u + 0x4C) |= 0x31000000u;
    GRef<u32>(s + 0x2D4) = 0;
    GRef<s32>(s + 0x2C8) = -1;
    GRef<s32>(s + 0x2C4) = -1;
    func_0011EF40(s);
    const bool phase2 = Game(0x27) == 3 && (StatusFlags() & 4u) != 0;
    f32 ang, drop;
    if ((StatusFlags() & 0x2000000u) != 0) {
        ZeroQ(u + 0x10);
        const f32 h = F(0x0040EBE0);
        GRef<f32>(u + 0x14) = h;
        drop = 0.0f;
        ang = h;
        if (Game(0x28) == 2 && !phase2) {
            const f32 r = fpu::Sub(NextRandomFloat(), 0.5f);
            ang = fpu::Add(fpu::Add(r, r) * 10.0f * F(0x0040EBE4) / 180.0f, ang);
            drop = NextRandomFloat() * -1024.0f;
        } else if (!(Game(0x28) < 3)) {
            const f32 r = fpu::Sub(NextRandomFloat(), 0.5f);
            ang = fpu::Add(h, fpu::Add(r, r) * 20.0f * F(0x0040EBE8) / 180.0f);
            drop = NextRandomFloat() * -2048.0f;
        }
    } else {
        ZeroQ(u + 0x10);
        ang = 0.0f;
        drop = 0.0f;
        if (Game(0x28) == 2 && !phase2) {
            const f32 r = fpu::Sub(NextRandomFloat(), 0.5f);
            ang = fpu::Add(fpu::Add(r, r) * 15.0f * F(0x0040EBEC) / 180.0f, ang);
            drop = NextRandomFloat() * -1024.0f;
        } else if (!(Game(0x28) < 3)) {
            const f32 r = fpu::Sub(NextRandomFloat(), 0.5f);
            ang = fpu::Add(ang, fpu::Add(r, r) * 30.0f * F(0x0040EBF0) / 180.0f);
            drop = NextRandomFloat() * -2048.0f;
        }
    }
    const u32 r = Rules();
    GRef<f32>(u) = fpu::Add(F(r), dist * ee::sinf(ang));
    GRef<f32>(u + 4) = fpu::Add(fpu::Add(F(r + 4), dist * ee::sinf(F(0x0040EBF4))), drop);
    const f32 c = ee::cosf(ang);
    GRef<f32>(u + 0xC) = 1.0f;
    GRef<u32>(u + 0x28) = W(0x0040EBF8);
    GRef<u32>(u + 0x20) = 0;
    GRef<f32>(u + 8) = fpu::Add(F(r + 8), dist * c);
    GRef<u32>(u + 0x24) = 0;
    GRef<u32>(u + 0x2C) = 0;
    ZeroQ(s);
    GRef<u32>(s + 0x138) = 0;
    GRef<u32>(s + 0x13C) = 0;
    const f32 t = F(s + 0x138);
    GRef<u32>(s + 0x140) = 0;
    GRef<u32>(s + 0x144) = 0;
    GRef<u32>(s + 0x134) = 0;
    GRef<u8>(s + 0x15C) = 0;
    GRef<u8>(s + 0x164) = 0;
    GRef<u8>(s + 0x17A) = 0;
    GRef<u8>(s + 0x286) = 0;
    GRef<u8>(s + 0x287) = 0;
    GRef<u8>(s + 0x288) = 0;
    func_00149EB8(0x20, 0, t);
    func_00149EB8(0x21, 0, t);
    func_00149EB8(0x22, 0, t);
    s32 first, n;
    const s8 kind = GRef<s8>(s + 0x172);
    if (kind == 1) {
        first = 0x3A;
        n = 6;
    } else {
        first = kind == 2 ? 0x3E : 0x34;
        n = 4;
    }
    for (s32 k = 0; k < n; ++k) func_00149EB8(first + k, 0, 0.0f);
    guest::GuestFrame fr(0x40);
    const u32 m = fr.Address();
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, u + 0x10);
    sceVu0ApplyMatrix(u + 0x20, m, u + 0x20);
    func_00121318(0);
    GRef<u32>(s + 0x188) |= 0x10000000u;
    GRef<u8>(s + 0x280) = 1;
    func_0017B2B0();
    func_0016DEF8();
}

// EE: 0x0017A7C8
// Carrier rearm menu entry (sub 0x1D): player reset for a retry, menu reset, cockpit view
// (state +0x149 = 1, previous view in 0x00410B24), speed from the aircraft record, fade, BGM
// volume to 1/4 (previous in 0x004117E0), the rearm voice/stream 0x3F or 0x40, fades.
void func_0017A7C8() {
    func_0017A980();
    func_0017E228();
    const u32 s = W(Unit(0) + 0x44);
    GRef<u8>(0x00410B24) = GRef<u8>(s + 0x149);
    GRef<u8>(s + 0x149) = 1;
    CopyQ(s + 0x50, AircraftRec(0) + 0x20);
    GRef<f32>(Scene() + 0x1670) = 1.0f;
    func_0010A130(0.0f, 0.5f, 0);
    GRef<f32>(0x004117E0) = func_0021CD08(6);
    func_0021CCF0(F(0x004117E0) * 0.25f, 6);
    if ((StatusFlags() & 0x1000000u) == 0)
        func_0021FD40(0, 0x3F);
    else
        func_0021FD40(0, 0x40);
    func_0021F190(0.5f, 0, 0xFF, 0xFF, 1);
    func_002559C8();
    GRef<u32>(Scene() + 0x1668) |= 0x100u;
    func_0017D848(W(0x00410B40), 0xFF, 0);
}

// EE: 0x0017A8D0
// Rearm menu exit: view and BGM volume restored, stream fade-out.
void func_0017A8D0() {
    GRef<u8>(W(Unit(0) + 0x44) + 0x149) = GRef<u8>(0x00410B24);
    func_0021CCF0(F(0x004117E0), 6);
    func_0021F190(2.0f, 0, 0, 0, 2);
}

// EE: 0x0017ABD8
// Carrier takeoff start (sub 0x19): timer 0xE10, player reset, sounds, special voice fade by the
// approach flag, HUD mask, replay HUD preset.
void func_0017ABD8() {
    GRef<u32>(Status() + 0x38) = 0xE10;
    func_0017A980();
    func_001279B0(0, 0);
    StopAllVoices();
    if ((StatusFlags() & 0x2000000u) == 0)
        func_002170C8(1);
    else
        func_002170C8(0);
    func_002559C8();
    func_0017B2B0();
    func_0016DAE0();
    func_0017D848(W(0x00410B40), 1, 1);
}

// EE: 0x0017B0B0
// Carrier segment end: back to map entry 0, player state flags/timers cleared, aircraft models
// set 0, the camera to the player, special voice stopped, mission BGM (status +0x80) restarted,
// back to the mission (0x00179998), segment flags cleared.
void func_0017B0B0() {
    const u32 s = W(Unit(0) + 0x44);
    GRef<u32>(Terrain() + 4) = 0;
    func_001390B0(Terrain(), 0);
    GRef<u32>(s + 0x188) &= 0xEFFFFFFFu;
    func_00121318(0);
    GRef<s32>(s + 0x2C4) = -1;
    GRef<s32>(s + 0x2C8) = -1;
    GRef<u32>(s + 0x24C) = 0;
    func_0011EF40(s);
    func_00149E90(AircraftModels(), 0);
    func_0010D748(Scene(), s + 0x30);
    func_002172A8();
    const s32 bgm = GRef<s32>(Status() + 0x80);
    if (bgm != -1) func_0021FD40(0, static_cast<u16>(bgm));
    func_0021F190(1.0f, 0, 0xFF, 0xFF, 1);
    func_002559C8();
    if (GRef<s16>(Status() + 0x86) != 0) GRef<u32>(Status() + 0x20) |= 0x200u;
    func_00179998();
    GRef<u32>(Status() + 0x20) &= 0xECFFFFFFu;
    GRef<u32>(Scene() + 0x1668) |= 0x100u;
    func_0016DAE0();
    func_0016DEF8();
}

// EE: 0x0017B330
// Waypoint markers (record 0x00410B28, up to 4 × 0x20: x, -, z, 1, radius +0x10, kind +0x14,
// flags +0x18): the nearest marker becomes the current one (+0x82; inside its radius +0x81 bit 0
// and +0x83 = its kind, else kind 30); for the nearest marker of kind < 0x1D, distance bands
// (C 0x0040EC18-0x0040EC38, measured from the radius when flag 0x10000000) give the radio
// distance call 0x00252AF0(kind, clock direction 1-36, distance code) once per band.
// fpucheck: 3 adds
void func_0017B330() {
    const u32 u = Unit(0);
    s32 near = -1, nearc = -1;
    f32 dn = 524288.0f, dc = 524288.0f;
    for (s32 k = 0; k < GRef<u8>(Radio() + 0x80); ++k) {
        const u32 e = Radio() + 0x20u * static_cast<u32>(k);
        const f32 d = GetDistanceXZ(e, u);
        if (d < dn) {
            dn = d;
            near = k;
        }
        if (GRef<s32>(e + 0x14) != 0x1D && d < dc) {
            dc = d;
            nearc = k;
        }
    }
    if (near == -1) {
        GRef<u8>(Radio() + 0x81) = static_cast<u8>(GRef<u8>(Radio() + 0x81) & 0xFE);
        GRef<u8>(Radio() + 0x83) = 0x1E;
        return;
    }
    GRef<u8>(Radio() + 0x82) = static_cast<u8>(near);
    const u32 e = Radio() + 0x20u * static_cast<u32>(near);
    if (dn < F(e + 0x10)) {
        GRef<u8>(Radio() + 0x81) = static_cast<u8>(GRef<u8>(Radio() + 0x81) | 1);
        GRef<u8>(Radio() + 0x83) = GRef<u8>(e + 0x14);
        GRef<u32>(e + 0x18) |= 0x3Fu;
    } else {
        GRef<u8>(Radio() + 0x81) = static_cast<u8>(GRef<u8>(Radio() + 0x81) & 0xFE);
        GRef<u8>(Radio() + 0x83) = 0x1E;
    }
    if (nearc == -1) return;
    const u32 c = Radio() + 0x20u * static_cast<u32>(nearc);
    if (!(GRef<s32>(c + 0x14) < 0x1D)) return;
    const u32 v = W(c + 0x18);
    s32 code = 0;
    const f32 d = (v & 0x10000000u) != 0 ? fpu::Sub(dc, F(c + 0x10)) : dc;
    if (d < F(0x0040EC18)) {
        if ((~v & 0x20u) != 0) {
            GRef<u32>(c + 0x18) = v | 0x3Fu;
            code = 2;
        }
    } else if (d < F(0x0040EC1C)) {
        if ((~v & 0x10u) != 0) {
            GRef<u32>(c + 0x18) = v | 0x1Fu;
            code = 4;
        }
    } else if (d < F(0x0040EC20)) {
        if ((~v & 8u) != 0) {
            GRef<u32>(c + 0x18) = v | 0xFu;
            code = 8;
        }
    } else if (d < F(0x0040EC24)) {
        if ((~v & 4u) != 0) {
            GRef<u32>(c + 0x18) = v | 7u;
            if (F(0x0040EC28) < d) code = 0xC;
        }
    } else if (d < F(0x0040EC2C)) {
        if ((~v & 2u) != 0) {
            GRef<u32>(c + 0x18) = v | 3u;
            if (F(0x0040EC30) < d) code = 0x14;
        }
    } else if (d < F(0x0040EC34)) {
        if ((~v & 1u) != 0) {
            GRef<u32>(c + 0x18) = v | 1u;
            if (F(0x0040EC38) < d) code = 0x1E;
        }
    }
    if (code == 0) return;
    guest::GuestFrame fr(0x10);
    const u32 dv = fr.Address();
    sceVu0SubVector(dv, c, u);
    f32 a = fpu::Sub(ee::atan2f(F(dv), F(dv + 8)), F(0x0040EC3C));
    if (a < 0.0f) a = fpu::Add(a, F(0x0040EC40));
    s32 clock = fpu::FloatToInt(a * F(0x0040EC44));
    if (clock == 0) clock = 0x24;
    func_00252AF0(GRef<s32>(c + 0x14), clock, code);
}

// EE: 0x0017B690
// Dead (no reference): 1 while inside the current waypoint marker's radius (+0x81 bit 0).
u32 func_0017B690() { return GRef<u8>(Radio() + 0x81) & 1; }

// EE: 0x0017B6A0
// Event op: adds a waypoint marker (x, z, radius, kind; flags bit 0: distances from the radius).
void func_0017B6A0(f32 x, f32 z, f32 radius, s32 kind, s32 flags) {
    const u32 r = Radio();
    if (!(GRef<u8>(r + 0x80) < 4)) return;
    const u32 e = r + static_cast<u32>(GRef<u8>(r + 0x80)) * 0x20;
    GRef<s32>(e + 0x14) = kind;
    GRef<f32>(e) = x;
    GRef<f32>(e + 8) = z;
    GRef<f32>(e + 0xC) = 1.0f;
    GRef<f32>(e + 0x10) = radius;
    GRef<u32>(e + 4) = 0;
    if ((flags & 1) != 0) GRef<u32>(e + 0x18) |= 0x10000000u;
    GRef<u8>(r + 0x80) = static_cast<u8>(GRef<u8>(r + 0x80) + 1);
}

// EE: 0x0017B740
// Radio trigger areas (record 0x00410B2C, up to 4): the player entering one plays its message
// once (bit k of +0x84).
void func_0017B740() {
    const u32 u = Unit(0);
    for (s32 k = 0; k < GRef<s32>(Markers2() + 0x80); ++k) {
        if (((GRef<s32>(Markers2() + 0x84) >> (k & 0x1F)) & 1) != 0) continue;
        const u32 e = Markers2() + 0x20u * static_cast<u32>(k);
        if (GetDistanceXZ(u, e) < F(e + 0x10)) {
            func_00252FB8(GRef<s32>(e + 0x14), 1, 0);
            GRef<u32>(Markers2() + 0x84) |= 1u << (k & 0x1F);
        }
    }
}

// EE: 0x0017B818
// Event op: adds a radio trigger area (x, z, radius, message).
void func_0017B818(f32 x, f32 z, f32 radius, s32 msg) {
    const u32 r = Markers2();
    const s32 n = GRef<s32>(r + 0x80);
    if (!(n < 4)) return;
    const u32 e = r + static_cast<u32>(n) * 0x20;
    GRef<s32>(e + 0x14) = msg;
    GRef<f32>(e + 0x10) = radius;
    GRef<f32>(e) = x;
    GRef<s32>(r + 0x80) = GRef<s32>(r + 0x80) + 1;
    GRef<f32>(e + 8) = z;
    GRef<f32>(e + 0xC) = 1.0f;
    GRef<u32>(e + 4) = 0;
}

// EE: 0x0017B8A0
// Mission id 1: ordered radio checkpoints (record 0x00410B30, up to 10 × {x, -, z, radius},
// messages +0xA0): unit i reaching the next one plays its message.
void func_0017B8A0(s32 i) {
    const u32 r = Markers3();
    if (!(GRef<s32>(r + 0xCC) < GRef<s32>(r + 0xC8))) return;
    const u32 e = Markers3() + static_cast<u32>(GRef<s32>(Markers3() + 0xCC)) * 0x10;
    if (GetDistanceXZ(Unit(i), e) < F(Markers3() + static_cast<u32>(GRef<s32>(Markers3() + 0xCC)) * 0x10 + 0xC)) {
        func_00252FB8(GRef<s32>(Markers3() + static_cast<u32>(GRef<s32>(Markers3() + 0xCC)) * 4 + 0xA0), 1, 0);
        GRef<s32>(Markers3() + 0xCC) += 1;
    }
}

// EE: 0x0017B930
// Event op (mission id 1): adds a radio checkpoint.
void func_0017B930(f32 x, f32 z, f32 radius, s32 msg) {
    const u32 r = Markers3();
    const s32 n = GRef<s32>(r + 0xC8);
    if (!(n < 10)) return;
    GRef<s32>(r + static_cast<u32>(n) * 4 + 0xA0) = msg;
    const u32 e = r + static_cast<u32>(n) * 0x10;
    GRef<f32>(e + 0xC) = radius;
    GRef<f32>(e) = x;
    GRef<s32>(r + 0xC8) = GRef<s32>(r + 0xC8) + 1;
    GRef<f32>(e + 8) = z;
    GRef<u32>(e + 4) = 0;
}

// EE: 0x0017B9B0
// Mission id 8: radio areas reached by units of model 0x60 (record 0x00410B34, up to 8); one area
// per 32 frames ((frame >> 5) mod count) against every 32nd unit from frame & 31.
void func_0017B9B0() {
    const s32 n = GRef<s32>(Markers4() + 0xA0);
    if (n == 0) return;
    const u32 fc = W(Scene() + 0x165C);
    const u32 k = (fc >> 5) % static_cast<u32>(n);  // divu
    if (((GRef<s32>(Markers4() + 0xA4) >> (k & 0x1F)) & 1) != 0) return;
    for (u32 j = fc & 0x1F; static_cast<s32>(j) < UnitCount(); j += 0x20) {
        const u32 o = Unit(static_cast<s32>(j));
        if ((~W(o + 0x4C) & 0x10000000u) != 0 || GRef<u8>(o + 0x69) != 0x60) continue;
        if (GetDistanceXZ(o, Markers4() + k * 0x10) < F(Markers4() + k * 0x10 + 0xC)) {
            func_00252FB8(GRef<s32>(Markers4() + k * 4 + 0x80), 1, 0);
            GRef<u32>(Markers4() + 0xA4) |= 1u << (k & 0x1F);
        }
    }
}

// EE: 0x0017BAF0
// Event op (mission id 8): adds a radio area.
void func_0017BAF0(f32 x, f32 z, f32 radius, s32 msg) {
    const u32 r = Markers4();
    const s32 n = GRef<s32>(r + 0xA0);
    if (!(n < 8)) return;
    GRef<s32>(r + static_cast<u32>(n) * 4 + 0x80) = msg;
    const u32 e = r + static_cast<u32>(n) * 0x10;
    GRef<f32>(e + 0xC) = radius;
    GRef<f32>(e) = x;
    GRef<s32>(r + 0xA0) = GRef<s32>(r + 0xA0) + 1;
    GRef<f32>(e + 8) = z;
    GRef<u32>(e + 4) = 0;
}

// EE: 0x0017BD68
// Trial track: appends {time / 2, score} and terminates the track with {0xFFFF, 0xFFFF}.
void func_0017BD68(s32 time, u16 score) {
    const s32 n = GRef<s32>(0x002B7C24);
    const s32 lim = GRef<s32>(0x002B7C30);
    if (!(n < lim)) return;
    const u32 e = W(0x002B7C2C) + static_cast<u32>(n) * 4;
    GRef<s32>(0x002B7C24) = n + 1;
    GRef<s16>(e) = static_cast<s16>(time / 2);
    GRef<u16>(e + 2) = score;
    if (n + 1 < lim) {
        GRef<u16>(e + 6) = 0xFFFF;
        GRef<u16>(e + 4) = 0xFFFF;
    }
}

// EE: 0x0017BDD0
// Trial best track at mission time t: the score of the last sample at or before t / 2 (the
// cursor 0x002B7C20 advances one sample per call).
u16 func_0017BDD0(s32 t) {
    const s32 i = GRef<s32>(0x002B7C20);
    const u32 e = W(0x002B7C28) + static_cast<u32>(i) * 4;
    if (!(t / 2 < static_cast<s32>(GRef<u16>(e)))) {
        if (i < GRef<s32>(0x002B7C30) - 1) GRef<s32>(0x002B7C20) = i + 1;
        return GRef<u16>(e + 2);
    }
    if (i != 0) return GRef<u16>(e - 2);
    return 0;
}

// EE: 0x0017BF98
// Event op 0x43: requests tutorial control hint `bit`.
void func_0017BF98(u32 bit) { GRef<u32>(0x004117E4) |= 1u << (bit & 0x1F); }

// EE: 0x0017BFB0
// Per frame: passes the requested tutorial hints to the hint panel.
void func_0017BFB0() {
    if (W(0x004117E4) != 0) func_00258400(W(0x004117E4));
}

// ---- In-mission menus ---------------------------------------------------------------------------

// EE: 0x0017D938
// Pause item RESUME: pause request flag 0x400, stream slot 0 resumed.
void func_0017D938() {
    Ctx()->flags |= 0x400;
    func_0021E798(0, 0);
}

// EE: 0x0017D958
// Pause item REPLAY: action 2 after the fade.
void func_0017D958() {
    GRef<u32>(Menu() + 0xC) = 2;
    func_0010A130(1.0f, 1.0f, 0);
    func_0010A130(1.0f, 1.0f, 1);
}

// EE: 0x0017D9A8
// Menu item RETRY (option 0 = yes): action 3 after the fade.
void func_0017D9A8(s32 opt) {
    if (opt != 0) return;
    GRef<u32>(Menu() + 0xC) = 3;
    func_0010A130(1.0f, 1.0f, 0);
    func_0010A130(1.0f, 1.0f, 1);
    func_0021F190(0.5f, 0, 0, 0, 2);
}

// EE: 0x0017DA28
// Menu item EXIT (option 0 = yes): action 1 after the fade.
void func_0017DA28(s32 opt) {
    if (opt != 0) return;
    GRef<u32>(Menu() + 0xC) = 1;
    func_0010A130(1.0f, 1.0f, 0);
    func_0010A130(1.0f, 1.0f, 1);
    func_0021F190(0.5f, 0, 0, 0, 2);
}

// EE: 0x0017DAA8
// Pause item HUD (option 0 = on): bit 1 of 0x002A15CC.
void func_0017DAA8(s32 opt) {
    if (opt == 0)
        GRef<u8>(0x002A15CC) = static_cast<u8>(GRef<u8>(0x002A15CC) & 0xFD);
    else
        GRef<u8>(0x002A15CC) = static_cast<u8>(GRef<u8>(0x002A15CC) | 2);
}

// EE: 0x0017DAD8
s32 func_0017DAD8() { return (GRef<u8>(0x002A15CC) & 2) != 0 ? 1 : 0; }

// EE: 0x0017DAF0
// Pause item VIBRATION / view option: context flag 0x20000.
void func_0017DAF0(s32 opt) {
    if (opt == 0)
        Ctx()->flags &= 0xFFFDFFFFu;
    else
        Ctx()->flags |= 0x20000;
}

// EE: 0x0017DB28
s32 func_0017DB28() { return (Ctx()->flags & 0x20000) != 0 ? 1 : 0; }

// EE: 0x0017DB40
// Game-over item (option 0): action 4 (back to the title/menu) after the fade.
void func_0017DB40(s32 opt) {
    if (opt != 0) return;
    GRef<u32>(Menu() + 0xC) = 4;
    func_0010A130(1.0f, 1.0f, 0);
    func_0010A130(1.0f, 1.0f, 1);
    func_0021F190(0.5f, 0, 0, 0, 2);
}

// EE: 0x0017DBC0
// Rearm item TAKEOFF.
void func_0017DBC0(s32 opt) {
    if (opt == 0) RequestSubState(0x19);
}

// EE: 0x0017DBD8
// Rearm item CHANGE WEAPON: the chosen special weapon, weapon timers, sound, lock state.
void func_0017DBD8() {
    const u32 s = W(Unit(0) + 0x44);
    func_0011EC48(s, GRef<s32>(0x002B84E0 + 4u * W(0x00410B90)));
    func_0011ED28(s);
    PlayMenuSe(10, 0x2C);
    GRef<u32>(s + 0x274) = 0;
    GRef<u32>(s + 0x26C) = W(0x0040EC54);
    GRef<u32>(s + 0x270) = 0;
}

// EE: 0x0017DC48
// Rearm CHANGE WEAPON current option: the position of the player's special weapon in the list.
s32 func_0017DC48() {
    const s8 cur = GRef<s8>(W(Unit(0) + 0x44) + 0x172);
    for (GRef<s32>(0x00410B90) = 0; GRef<s32>(0x00410B90) < 3; GRef<s32>(0x00410B90) += 1)
        if (GRef<s32>(0x002B84E0 + 4u * W(0x00410B90)) == cur) return GRef<s32>(0x00410B90);
    GRef<s32>(0x00410B90) = 0;
    return 0;
}

// EE: 0x0017DE68
// Runs the confirmed menu action once the fade is complete (1 exit to the menu page by mission
// kind / result, 2 replay, 3 retry, 4 title), then stops sounds and vibration.
void func_0017DE68() {
    const s32 a = GRef<s32>(Menu() + 0xC);
    if (a == 1) {
        const s8 kind = GRef<s8>(Scene() + 0x1653);
        if (kind == 1) {
            EnterMenu(0x19);
        } else if (kind < 2) {
            if (kind == 0) {
                const u8 r = Game(0x29);
                if (r == 1)
                    EnterMenu(5);
                else if (r == 2)
                    EnterMenu(0x18);
                else if (r == 3)
                    EnterMenu(0x1C);
            }
        } else if (kind == 2) {
            EnterMenu(0x1B);
        } else if (kind == 3) {
            EnterMenu(0x1A);
        }
    } else if (a == 2) {
        GRef<u32>(Scene() + 0x1668) |= 0x10000u;
        RequestSubState(0x10);
    } else if (a == 3) {
        RequestSubState(8);
    } else if (a == 4) {
        EnterMenu(0);
    }
    func_00219260();
    func_0021AEE0();
    for (s32 p = 0; p < Players(); ++p) func_001279B0(p, 0);
    StopAllVoices();
    GRef<u32>(Menu() + 0xC) = 0;
}

// EE: 0x0017DFF8
// Pause menu per frame: L1+L2+R1+R2+SELECT... (pad bits 0xFA held with 0x20 pressed) sets the
// pad-ext 0x100 flag of the controlling pad, then the menu input.
void func_0017DFF8() {
    const u32 list = PauseList();
    MainContext* ctx = Ctx();
    const u32 c = CtxAddr();
    if ((ctx->flags & 0x2000) == 0) {
        if ((W(c + 0x214) & 0xFA) == 0xFA && (W(c + 0x218) & 0x20) != 0)
            GRef<u16>(c + 0x2DE) = static_cast<u16>(GRef<u16>(c + 0x2DE) | 0x100);
    } else if ((W(c + 0x264) & 0xFA) == 0xFA && (W(c + 0x268) & 0x20) != 0) {
        GRef<u16>(c + 0x2FE) = static_cast<u16>(GRef<u16>(c + 0x2FE) | 0x100);
    }
    func_0017E4E8(list);
}

// EE: 0x0017E118
// Pause menu draw.
void func_0017E118(u32 pkt) {
    GRef<u32>(Menu()) = pkt;
    func_0017EA08(PauseList());
}

// EE: 0x0017E1D0
// Game-over menu reset.
void func_0017E1D0() {
    GRef<u8>(Menu() + 4) = 0;
    GRef<u8>(Menu() + 5) = 0;
    GRef<u8>(Menu() + 6) = 0;
    GRef<u32>(Menu() + 0xC) = 0;
    GRef<u32>(Menu() + 8) = 0;
    func_0017E7D0(0x002B8430);
}

// EE: 0x0017E210
// Game-over menu draw.
void func_0017E210(u32 pkt) {
    GRef<u32>(Menu()) = pkt;
    func_0017EA08(0x002B8430);
}

// EE: 0x0017E228
// Rearm menu reset: the CHANGE WEAPON options are the special weapons owned for this aircraft
// (2-bit slots of the u64 masks 0x002A0338 + 8i), written as "NAME / nn" (name: 4 bytes of the
// weapon code, a NUL byte stored as TAB; nn: the stock) into the label buffers of 0x002B8090;
// option -> slot in 0x002B84E0; the list shows CHANGE WEAPON only when one is owned.
void func_0017E228() {
    GRef<u8>(Menu() + 4) = 0;
    GRef<u8>(Menu() + 5) = 0;
    GRef<u8>(Menu() + 6) = 0;
    GRef<u32>(Menu() + 0xC) = 0;
    GRef<u32>(Menu() + 8) = 0;
    const u8 model = GRef<u8>(Unit(0) + 0x69);
    const u32 lo = GetAircraftLoadout(model);
    const u32 s = W(Unit(0) + 0x44);
    const u64 mask = 3ull << ((static_cast<u32>(model - 1) * 2) & 0x3F);
    s32 n = 0;
    for (s32 i = 0; i < 3; ++i) {
        if ((GRef<u64>(0x002A0320 + static_cast<u32>(i + 3) * 8) & mask) != mask) continue;
        const u32 buf = W(0x002B8090 + static_cast<u32>(n) * 0x28);
        u32 name = GetSpecialWeaponCode(static_cast<u32>(static_cast<s32>(GRef<s8>(lo + 4 + 4u * static_cast<u32>(i)))));
        for (u32 d = buf; static_cast<s32>(d) < static_cast<s32>(buf + 4); ++d, ++name) {
            GRef<u8>(d) = GRef<u8>(name);
            if (GRef<s8>(name) == 0) GRef<u8>(d) = 9;
        }
        GRef<u8>(buf + 4) = 0x20;
        GRef<u8>(buf + 5) = 0x2F;
        GRef<u8>(buf + 6) = 0x20;
        const s16 stock = GRef<s16>(lo + 6 + 4u * static_cast<u32>(i));
        GRef<u8>(buf + 7) = static_cast<u8>(stock / 10 + 0x30);
        GRef<u8>(buf + 9) = 0;
        GRef<u8>(buf + 8) = static_cast<u8>(GRef<s16>(lo + 6 + 4u * static_cast<u32>(i)) % 10 + 0x30);
        GRef<s32>(0x002B84E0 + 4u * static_cast<u32>(n)) = i;
        if (GRef<s8>(s + 0x172) == n) GRef<s32>(0x00410B90) = i;
        ++n;
    }
    GRef<s32>(0x002B8338 + 0x14) = n;
    const u32 label = W(W(0x002B8450 + 8));
    if (n == 0) {
        GRef<u32>(0x002B8450 + 4) = 1;
        GRef<u32>(label + 0x20) = 0x590;
        GRef<u32>(label + 0x1C) = 0x760;
    } else {
        GRef<u32>(0x002B8450 + 4) = 2;
        GRef<u32>(label + 0x20) = 0x880;
        GRef<u32>(label + 0x1C) = 0xB10;
    }
    func_0017E7D0(0x002B8450);
}

// EE: 0x0017E460
// Rearm menu draw, with the aircraft's weapon panel for the selected option.
void func_0017E460(u32 pkt) {
    GRef<u32>(Menu()) = pkt;
    func_0017EA08(0x002B8450);
    if (GRef<s16>(Menu() + 4) == 0x101)
        GRef<s32>(0x00410B90) = GRef<s8>(Menu() + 6);
    else
        func_0017DC48();
    func_0017EC88(pkt, GRef<u8>(Unit(0) + 0x69), GRef<s32>(0x002B84E0 + 4u * W(0x00410B90)));
}

// EE: 0x0017E4E8
// Menu input (list: {flags, count, items}; item: label +0, options +4.., option count +0x14,
// state +0x18, current-option getter +0x2C, action +0x30): runs a finished action, then up/down
// (repeat bits 2/1) through the items or the options, confirm (0x50) calls the action or opens
// the options, cancel (0x20) closes them; no input while the fade is on. 2 players: the pad of
// the player who paused.
void func_0017E4E8(u32 list) {
    if (GRef<s32>(Menu() + 0xC) != 0 && F(Scene() + 0x1670) == 1.0f) func_0017DE68();
    const u32 c = CtxAddr();
    u32 pad;
    if (Players() == 2) {
        u32 a, b;
        s32 st;
        if ((Ctx()->flags & 0x2000) == 0) {
            st = GRef<s32>(c + 0x210);
            a = c + 0x2B0;
            b = c + 0x2C0;
        } else {
            st = GRef<s32>(c + 0x260);
            a = c + 0x2C0;
            b = c + 0x2B0;
        }
        pad = st == 0x80 ? b : a;
    } else {
        pad = c + 0x2B0;
    }
    u32 pressed = 0, rep = 0;
    if (F(Scene() + 0x1670) == 0.0f) {
        pressed = W(pad + 4);
        rep = W(pad + 8);
    }
    const u32 m = Menu();
    if (GRef<s8>(m + 4) == 0) {
        if ((rep & 2) != 0) {
            GRef<u8>(m + 5) = static_cast<u8>(GRef<u8>(m + 5) + 1);
            PlayMenuSe(0, 1);
        }
        if ((rep & 1) != 0) {
            GRef<u8>(Menu() + 5) = static_cast<u8>(GRef<u8>(Menu() + 5) - 1);
            PlayMenuSe(0, 1);
        }
        const s32 n = GRef<s32>(list + 4);
        if (GRef<s8>(Menu() + 5) < 0) GRef<s8>(Menu() + 5) = static_cast<s8>(n - 1);
        if (n - 1 < GRef<s8>(Menu() + 5)) GRef<u8>(Menu() + 5) = 0;
        if ((pressed & 0x50) == 0) return;
        const u32 item = W(list + 8 + 4u * static_cast<u32>(static_cast<s32>(GRef<s8>(Menu() + 5))));
        if (GRef<s32>(item + 0x14) == 0) {
            guest::CallGuest(W(item + 0x30), 0);
        } else {
            GRef<u8>(Menu() + 4) = 1;
            if (W(item + 0x2C) != 0)
                GRef<u8>(Menu() + 6) = static_cast<u8>(guest::CallGuest<s32>(W(item + 0x2C)));
            else
                GRef<u8>(Menu() + 6) = 0;
            func_0017E8B0(item);
        }
        PlayMenuSe(0, 2);
        return;
    }
    if (GRef<s8>(m + 4) != 1) return;
    const u32 item = W(list + 8 + 4u * static_cast<u32>(static_cast<s32>(GRef<s8>(m + 5))));
    if ((rep & 2) != 0) {
        GRef<u8>(m + 6) = static_cast<u8>(GRef<u8>(m + 6) + 1);
        PlayMenuSe(0, 1);
    }
    if ((rep & 1) != 0) {
        GRef<u8>(Menu() + 6) = static_cast<u8>(GRef<u8>(Menu() + 6) - 1);
        PlayMenuSe(0, 1);
    }
    const s32 n = GRef<s32>(item + 0x14);
    if (GRef<s8>(Menu() + 6) < 0) GRef<s8>(Menu() + 6) = static_cast<s8>(n - 1);
    if (n - 1 < GRef<s8>(Menu() + 6)) GRef<u8>(Menu() + 6) = 0;
    if ((pressed & 0x20) != 0) {
        GRef<u8>(Menu() + 4) = 0;
        PlayMenuSe(0, 3);
    } else if ((pressed & 0x50) != 0) {
        guest::CallGuest(W(item + 0x30), static_cast<s32>(GRef<s8>(Menu() + 6)));
        GRef<u8>(Menu() + 4) = 0;
        PlayMenuSe(0, 2);
    }
}

// EE: 0x0017EA08
// Menu draw: the frame opening over 8 frames (+8), the title bar (list flag 1), the items with
// state 1 (selected) / 2 (options open).
void func_0017EA08(u32 list) {
    const u32 m = Menu();
    const s32 o = GRef<s32>(m + 8);
    const s32 k = o < 9 ? o : 8;
    const s32 x = (Ctx()->flags & 0x10000) != 0 ? 0xAA0 : 0x8E0;
    if (o < 8) GRef<s32>(m + 8) = o + 1;
    func_0017F110(W(m), x + 0x7000, 0x7E00, k);
    if ((W(list) & 1) != 0) func_0017F408(W(Menu()), x + 0x7000, 0x7F40, 0x620, 8 - k, 0x00410B88);
    for (s32 i = 0; i < GRef<s32>(list + 4); ++i) {
        const u32 item = W(list + 8 + 4u * static_cast<u32>(i));
        if (i == GRef<s8>(Menu() + 5))
            GRef<u32>(item + 0x18) = GRef<s8>(Menu() + 4) == 1 ? 2 : 1;
        else
            GRef<u32>(item + 0x18) = 0;
        func_0017EB30(item);
    }
}

// EE: 0x0017EB30
// Draws a menu item: its label (fade target 8 when not selected) and, when open (state 2), the
// options (the current one bright).
void func_0017EB30(u32 item) {
    const u32 label = W(item);
    GRef<u32>(label + 0x18) = GRef<s32>(item + 0x18) == 0 ? 8u : 0u;
    func_0017EBE8(label);
    if (GRef<s32>(item + 0x18) != 2) return;
    for (s32 i = 0; i < GRef<s32>(item + 0x14); ++i) {
        const u32 opt = W(item + 4 + 4u * static_cast<u32>(i));
        GRef<u32>(opt + 0x18) = i == GRef<s8>(Menu() + 6) ? 0u : 8u;
        func_0017EBE8(opt);
    }
}

// EE: 0x0017EBE8
// Draws a menu text object: fade +0xC steps toward +0x18, y +8 slides by +0x24 up to +0x14,
// width by aspect (+0x1C / +0x20).
void func_0017EBE8(u32 t) {
    const s32 cur = GRef<s32>(t + 0xC), tgt = GRef<s32>(t + 0x18);
    if (cur < tgt)
        GRef<s32>(t + 0xC) = cur + 1;
    else if (tgt < cur)
        GRef<s32>(t + 0xC) = cur - 1;
    const s32 lim = GRef<s32>(t + 0x14);
    s32 y = GRef<s32>(t + 8);
    if (y < lim) {
        y += GRef<s32>(t + 0x24);
        GRef<s32>(t + 8) = y;
    }
    if (lim < y) {
        GRef<s32>(t + 8) = lim;
        y = lim;
    }
    const s32 w = (Ctx()->flags & 0x10000) == 0 ? GRef<s32>(t + 0x1C) : GRef<s32>(t + 0x20);
    func_0017F408(W(Menu()), GRef<s32>(t + 4) + 0x7000, y + 0x7200, w, GRef<s32>(t + 0xC), W(t));
}

// ---- Sub-state thunks (one `j target` each; the sub-state enter/exit dispatchers call these) --

// EE: 0x0010A380
void thunk_func_00118C88() { func_00118C88(); }
// EE: 0x0010A438
void thunk_func_0017ABD8() { func_0017ABD8(); }
// EE: 0x0010A470
void thunk_func_0017A7C8() { func_0017A7C8(); }
// EE: 0x0010A4D0
void thunk_func_0017A8D0() { func_0017A8D0(); }
// EE: 0x0010A4D8
void thunk_func_0016E1D0() { func_0016E1D0(); }

// Menu item callbacks (called through the item records, func_0017E4E8).
AC04_GUEST_FUNCTION(0x0017D938, func_0017D938);
AC04_GUEST_FUNCTION(0x0017D958, func_0017D958);
AC04_GUEST_FUNCTION(0x0017D9A8, func_0017D9A8);
AC04_GUEST_FUNCTION(0x0017DA28, func_0017DA28);
AC04_GUEST_FUNCTION(0x0017DAA8, func_0017DAA8);
AC04_GUEST_FUNCTION(0x0017DAD8, func_0017DAD8);
AC04_GUEST_FUNCTION(0x0017DAF0, func_0017DAF0);
AC04_GUEST_FUNCTION(0x0017DB28, func_0017DB28);
AC04_GUEST_FUNCTION(0x0017DB40, func_0017DB40);
AC04_GUEST_FUNCTION(0x0017DBC0, func_0017DBC0);
AC04_GUEST_FUNCTION(0x0017DBD8, func_0017DBD8);
AC04_GUEST_FUNCTION(0x0017DC48, func_0017DC48);

}  // namespace ac04::game
