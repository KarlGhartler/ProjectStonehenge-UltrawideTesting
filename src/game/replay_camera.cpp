// Replay cameras (slice S5j; docs/subsystems/mission-flow.md "Replay and demo"): the replay camera
// state and frame, the automatic style choice, the ten styles of table 0x002B77B0 and the manual
// cameras of table 0x002B77D8.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

// Replay camera state C = *0x00410B0C (0x1E0 bytes, 0x00175D08): +0..+0x3F camera parameters,
// +0x90 / +0xA0 / +0xB0 the unit's last three positions, +0xC0 their difference, +0xE0 / +0x110
// vectors (vf0 at reset), +0x130 byte, +0x160 / +0x170 vectors, +0x180 / +0x184 / +0x188 words,
// +0x190 style (0xFF none), +0x191 manual camera, +0x192 watched player, +0x193 cut timer, +0x194,
// +0x198, +0x19C (-1), +0x1A0, +0x1A4 situation flags, +0x1AD nearest path point, +0x1AE, +0x1B0
// path table (0x002B7670), +0x1B4 its distance, +0x1B8 its value, +0x1BC, +0x1C4, +0x1C8 frame
// counter, +0x1CC cut length, +0x1D0..+0x1DA style masks (u16), +0x1DC.

namespace {

u32 Cam() { return W(0x00410B0C); }

}  // namespace

// EE: 0x001712F8
// Number of obstacles on the segment from `a` to `b`: the terrain (0x0013D698 hit) and every unit
// from 2 with +0x48 bit 0x100000 (large objects; not model 0xB2) within 2048 of `a` whose model the
// segment hits (0x00146808 with the unit's rotation / translation matrix).
s32 func_001712F8(u32 a, u32 b) {
    guest::GuestFrame frame(0x70);
    const u32 sp = frame.Address(), nrm = sp + 0x10, m = sp + 0x20, attr = sp + 0x60;
    s32 n = func_0013D698(Terrain(), sp, attr, a, b, nrm) == 1 ? 1 : 0;
    for (s32 i = 2; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        if ((~W(u + 0x48) & 0x100000u) != 0) continue;
        if (GRef<u8>(u + 0x69) == 0xB2) continue;
        if (2048.0f < func_00104F10(u, a)) continue;
        sceVu0UnitMatrix(m);
        RotateMatrix(m, m, u + 0x10);
        sceVu0TransMatrix(m, m, u);
        if (func_00146808(UnitModels(), sp, a, b, m, nrm, i) != 0) ++n;
    }
    return n;
}

// EE: 0x00171460
// As 0x001712F8 counting obstacles between `a` and `b`, and moves `out` to the hit point of the
// terrain when |a - b| < K 0x0040EA20, or of a unit model when that unit is nearer than K (then
// out.w = 1.0).
s32 func_00171460(u32 out, u32 a, u32 b) {
    guest::GuestFrame frame(0x70);
    const u32 sp = frame.Address(), nrm = sp + 0x10, m = sp + 0x20, attr = sp + 0x60;
    s32 n = 0;
    const f32 k = F(0x0040EA20);
    if (func_0013D698(Terrain(), sp, attr, a, b, nrm) == 1) {
        if (func_00104F10(a, b) < k) sceVu0CopyVector(out, sp);
        n = 1;
    }
    for (s32 i = 2; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        if ((~W(u + 0x48) & 0x100000u) != 0) continue;
        if (GRef<u8>(u + 0x69) == 0xB2) continue;
        const f32 d = func_00104F10(u, a);
        if (2048.0f < d) continue;
        sceVu0UnitMatrix(m);
        RotateMatrix(m, m, u + 0x10);
        sceVu0TransMatrix(m, m, u);
        if (func_00146808(UnitModels(), sp, a, b, m, nrm, i) == 0) continue;
        if (d < k) {
            sceVu0CopyVector(out, sp);
            GRef<f32>(out + 0xC) = 1.0f;
        }
        ++n;
    }
    return n;
}

// EE: 0x00171640
// Per-frame situation of the watched unit (view V, unit V+6): while it is alive (+0x4C bit
// 0x1000000) a cut starts (C+0x1A4 bit 8) when it was dead before (bit 0x2000, cleared); when it is
// not alive bit 0x2000 is set (both reset the cut length +0x1CC); frame
// counter +0x1C8 past the cut length +0x1CC -> new cut: timer +0x193 = 120, flags |= 5, counters
// cleared, path point none, situation bits 0x1C0 from the distance to the point 0x002B75E0 in
// mission 0xF (within 32768: terrain under the unit (0x40 when not hit), or beside it +-1024 in x
// (0x80 hit, 0x100 not)); otherwise flags &= ~4. Position history +0x90/+0xA0/+0xB0 (on a cut
// three points 16 apart backwards along the unit's heading), difference +0xC0. +0x1A0 cleared when
// the unit's projectile-follow special slot is -1. Then the nearest path point of the table +0x1B0
// (count +0, points of 0x10 bytes at +0x10, within 4096; 64 or nearer sets the timer to 240),
// index -> +0x1AD (flag 0x1000 when it changed), distance -> +0x1B4, value -> +0x1B8; skipped while
// the timer +0x193 counts down.
// fpucheck: 1 sub, 1 add or sub
void func_00171640(u32 view) {
    guest::GuestFrame frame(0x80);
    const u32 sp = frame.Address(), hit = sp + 0x10, nrm = sp + 0x20, m = sp + 0x30, attr = sp + 0x70;
    const u32 u = Unit(GRef<u8>(view + 6));
    u32 c = Cam();
    if ((W(u + 0x4C) & 0x1000000u) != 0) {
        const u32 f = W(c + 0x1A4);
        if ((f & 0x2000u) != 0) {
            GRef<u32>(c + 0x1CC) = 0;
            GRef<u32>(c + 0x1A4) = (f & 0xFFFFDFFFu) | 8u;
        }
    } else {
        const u32 f = W(c + 0x1A4);
        if ((f & 0x2000u) == 0) {
            GRef<u32>(c + 0x1CC) = 0;
            GRef<u32>(c + 0x1A4) = f | 0x2000u;
        }
    }
    const s32 n = GRef<s32>(c + 0x1C8) + 1;
    GRef<s32>(c + 0x1C8) = n;
    if (GRef<s32>(c + 0x1CC) < n) {
        GRef<u8>(c + 0x193) = 0x78;
        GRef<u32>(c + 0x1C8) = 0;
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 5u;
        GRef<u32>(c + 0x194) = 0;
        GRef<u8>(Cam() + 0x1AD) = 0xFF;
        const f32 d = func_00104F10(0x002B75E0, u);
        c = Cam();
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFFE3Fu;
        if (Game(0x27) == 0xF && d < 32768.0f) {
            sceVu0CopyVector(sp, u);
            GRef<f32>(sp + 4) = fpu::Sub(F(sp + 4), 4096.0f);
            if (func_0013D698(Terrain(), hit, attr, sp, u, nrm) == 1) {
                sceVu0CopyVector(sp, u);
                if (F(u) < F(0x002B75E0)) GRef<f32>(sp) = fpu::Add(F(sp), 1024.0f);
                else GRef<f32>(sp) = fpu::Sub(F(sp), 1024.0f);
                const bool h = func_0013D698(Terrain(), hit, attr, sp, u, nrm) == 1;
                c = Cam();
                GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | (h ? 0x80u : 0x100u);
            } else {
                c = Cam();
                GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x40u;
            }
        }
    } else {
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFFFFBu;
    }
    c = Cam();
    if ((W(c + 0x1A4) & 8u) != 0) {
        ZeroQ(sp);
        GRef<f32>(sp + 8) = -16.0f;
        SetUnitMatrixVu0(m);
        func_00102D28(m, m, u + 0x10);
        sceVu0ApplyMatrix(sp, m, sp);
        sceVu0AddVector(Cam() + 0x90, u, sp);
        sceVu0AddVector(Cam() + 0xA0, Cam() + 0x90, sp);
        sceVu0AddVector(Cam() + 0xB0, Cam() + 0xA0, sp);
        sceVu0SubVector(Cam() + 0xC0, Cam() + 0x90, Cam() + 0xA0);
        GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) & 0xFFFFFFF7u;
    } else {
        sceVu0CopyVector(c + 0xB0, c + 0xA0);
        sceVu0CopyVector(Cam() + 0xA0, Cam() + 0x90);
        sceVu0CopyVector(Cam() + 0x90, u);
        sceVu0SubVector(Cam() + 0xC0, Cam() + 0x90, Cam() + 0xA0);
    }
    c = Cam();
    if (GRef<s32>(W(0x00410680 + static_cast<u32>(GRef<u8>(view + 6)) * 4) + 0x14) == -1) GRef<u32>(c + 0x1A0) = 0;
    GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFEFFFu;
    if (W(c + 0x1B0) == 0) {
        GRef<u8>(c + 0x1AD) = 0xFF;
        return;
    }
    sceVu0CopyVector(sp, u);
    c = Cam();
    if (GRef<u8>(c + 0x193) != 0) {
        GRef<u8>(c + 0x193) = static_cast<u8>(GRef<u8>(c + 0x193) - 1);
        return;
    }
    const u32 t = W(c + 0x1B0);
    u32 best = 0xFF;
    f32 bd = 4096.0f;
    const s32 count = GRef<s32>(t);
    GRef<f32>(hit + 0xC) = 1.0f;
    for (s32 i = 0; i < count; ++i) {
        const u32 p = W(t + 0x10) + static_cast<u32>(i) * 0x10;
        GRef<f32>(hit) = F(p);
        GRef<f32>(hit + 4) = F(p + 4);
        GRef<f32>(hit + 8) = F(p + 8);
        const f32 d = func_00104F10(sp, hit);
        if (d < 64.0f) {
            GRef<u8>(Cam() + 0x193) = 0xF0;
        } else if (d < bd) {
            bd = d;
            best = static_cast<u32>(i) & 0xFF;
            GRef<f32>(Cam() + 0x1B8) = F(p + 0xC);
        }
    }
    c = Cam();
    if (GRef<u8>(c + 0x1AD) != best) GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x1000u;
    GRef<u8>(c + 0x1AD) = static_cast<u8>(best);
    GRef<f32>(Cam() + 0x1B4) = bd;
}

// EE: 0x00175D08
// Creates the replay camera state (0x1E0 bytes from the work heap, *0x00410B0C) with its defaults
// (path table 0x002B7670, distances 4096 / 8, parameters K 0x0040EB4C-0x0040EB5C, flags 9, style
// masks 0x3F / 0x7F / 0xBF / 0x25D / 0x1C / 0x11C, +0x1DC = 50) and sets up the replay HUD
// (*0x00410B40: demo flag, mode 7, camera 0; label and element mask: demo 1 / 0x3F, missions with
// terrain set 1 or ids 0x29-0x3A / 0x3D-0x4E 1 / 0x40, else 0 / 0x1F with situation bits 0x1C0,
// else 0 / 0x3F); watched player +0x192 (game[0x2D] when below the player count) and the replay
// flag of that player in the aircraft model block.
void func_00175D08() {
    GRef<u32>(0x00410B0C) = WorkHeapAlloc(0x1E0, 0x10);
    u32 c = Cam();
    GRef<u8>(c + 0x1AE) = 0;
    GRef<u32>(c + 0x1B0) = 0x002B7670;
    GRef<u32>(Cam() + 0x1B8) = 0;
    StoreVf0(Cam() + 0xE0);
    StoreVf0(Cam() + 0x110);
    ZeroQ(Cam() + 0x100);
    GRef<u8>(Cam() + 0x130) = 0x1E;
    c = Cam();
    GRef<u8>(c + 0x191) = 0;
    const f32 k0 = F(0x0040EB4C), k1 = F(0x0040EB50), k2 = F(0x0040EB54), k3 = F(0x0040EB58), k4 = F(0x0040EB5C);
    GRef<f32>(c + 0x1BC) = 4096.0f;
    GRef<f32>(c + 0x1C4) = 8.0f;
    GRef<u32>(c + 0x1C8) = 0;
    GRef<u32>(c + 0x1CC) = 0;
    GRef<s32>(c + 0x19C) = -1;
    GRef<f32>(c + 0x14) = k0;
    GRef<f32>(c + 0x18) = k1;
    GRef<f32>(c + 0x20) = k2;
    GRef<f32>(c + 0x24) = k3;
    GRef<f32>(c + 0x28) = k4;
    GRef<f32>(c + 0x38) = 1.0f;
    GRef<u32>(c + 0x1A0) = 0;
    GRef<u32>(c + 0) = 0;
    GRef<u32>(c + 4) = 0;
    GRef<u32>(c + 8) = 0;
    GRef<u32>(c + 0xC) = 0;
    GRef<f32>(c + 0x10) = k0;
    GRef<u32>(c + 0x1C) = 0;
    GRef<u32>(c + 0x2C) = 0;
    GRef<f32>(c + 0x30) = 1.0f;
    GRef<f32>(c + 0x34) = 1.0f;
    GRef<u32>(c + 0x3C) = 0;
    ZeroQ(c + 0x170);
    ZeroQ(Cam() + 0x160);
    c = Cam();
    GRef<u8>(c + 0x190) = 0xFF;
    GRef<u32>(c + 0x180) = 0;
    GRef<u32>(c + 0x184) = 0;
    GRef<u16>(c + 0x1DC) = 0x32;
    GRef<u32>(c + 0x1A4) = 9;
    GRef<u16>(c + 0x1D0) = 0x3F;
    GRef<u16>(c + 0x1D4) = 0xBF;
    GRef<u16>(c + 0x1D2) = 0x7F;
    GRef<u16>(c + 0x1D6) = 0x25D;
    GRef<u16>(c + 0x1D8) = 0x1C;
    GRef<u16>(c + 0x1DA) = 0x11C;
    GRef<u32>(c + 0x188) = 0;
    GRef<u32>(c + 0x198) = 0;
    const u32 hud = W(0x00410B40);
    func_0017D840(hud, Game(0x29) == 4 ? 1 : 0);
    func_0017D888(W(0x00410B40), 7);
    func_0017D898(W(0x00410B40), 0);
    const u8 id = Game(0x27);
    if (Game(0x29) == 4) {
        func_0017D848(W(0x00410B40), 1, 0);
        func_0017D890(W(0x00410B40), 0x3F);
    } else if (W(Terrain() + 4) == 1 || static_cast<u32>(id - 0x29) < 0x12 || static_cast<u32>(id - 0x3D) < 0x12) {
        func_0017D848(W(0x00410B40), 1, 0);
        func_0017D890(W(0x00410B40), 0x40);
    } else if ((W(Cam() + 0x1A4) & 0x1C0u) != 0) {
        func_0017D848(W(0x00410B40), 0, 0);
        func_0017D890(W(0x00410B40), 0x1F);
    } else {
        func_0017D848(W(0x00410B40), 0, 0);
        func_0017D890(W(0x00410B40), 0x3F);
    }
    GRef<u8>(Cam() + 0x192) = Game(0x2D) < Game(0x25) ? Game(0x2D) : 0;
    const u32 blk = W(W(0x004106A4) + 0x44);
    if (GRef<s8>(Cam() + 0x192) == 1) GRef<u32>(blk + 4) = W(blk + 4) | 1u;
    else GRef<u32>(blk) = W(blk) | 1u;
}

// EE: 0x00175FF0
// Replay camera cut reset: +0xE0 = vf0, +0x19C = -1, counters, +0x1A0, +0x160 / +0x170, +0x180 /
// +0x184 / +0x188 / +0x198 cleared, style none, flags 0xD.
void func_00175FF0() {
    StoreVf0(Cam() + 0xE0);
    u32 c = Cam();
    GRef<s32>(c + 0x19C) = -1;
    GRef<u32>(c + 0x1C8) = 0;
    GRef<u32>(c + 0x1CC) = 0;
    GRef<u32>(c + 0x1A0) = 0;
    ZeroQ(c + 0x170);
    ZeroQ(Cam() + 0x160);
    c = Cam();
    GRef<u8>(c + 0x190) = 0xFF;
    GRef<u32>(c + 0x180) = 0;
    GRef<u32>(c + 0x184) = 0;
    GRef<u32>(Cam() + 0x1A4) = 0xD;
    GRef<u32>(c + 0x188) = 0;
    GRef<u32>(Cam() + 0x198) = 0;
}

// EE: 0x00176060
// Replay camera frame (replay view V = flight scene view 0): HUD element mask as 0x00175D08; one
// view watching unit C+0x192. Missions with terrain set 1 or ids 0x29-0x3A / 0x3D-0x4E use camera
// 6 and path table 0x002B7670 (ids 0x29-0x3A, or bit game[0] of 0x204114 clear) or 0x002B7708
// (ids 0x3D-0x4E, or that bit set).
// Otherwise R1 / L1 of the watched player (pad bits 8 / 4) step the camera +0x1AE through 0-5 (0-4
// in the special situations 0x1C0, where camera 5 falls back to 0), each change setting flag 0x800
// (a pending 0x800 plays menu SE 0/1 first) and, for a living unit, a new cut. HUD camera / time,
// view solve flags, situation (0x00171640); the camera of the slot (table 0x002B77D8: auto
// 0x001756C0, 0x00173670, 0x001759E8, 0x00173020, 0x00173468, 0x00173A98) or, for a dead unit,
// 0x00175BA0. Fade +0x7B = 0x80 on a cut, else 0x66 with display flag 0x2000. View solve; camera
// FOV +0x16C = +0x168 x K 0x0040EB60 (0x0040EB64 in view mode 2). With flag 2 the camera angles are
// held one frame (flag 1: first frame). Shake: every +0x184 frames (10-19, rand) a new random
// target (+0x160, +-1 x +0x188 x speed factor (0 below 1, (d - 1) / 5 below 6, else 1)), blended
// from the previous (+0x170) by the curve 0x00246D98 / 0x00246DE8 over +0x0..+0x3F and added to
// the angles; a random drift +0x100 (rand x K 0x0040EB68 x min(|+0x120|, 30) x 2, wrapped by K
// 0x0040EB6C-0x0040EB80) added too; camera matrices.
// fpucheck: 1 sub (speed), 1 add (x 2), 4 wraps
void func_00176060() {
    const u32 hud = W(0x00410B40);
    const u8 id = Game(0x27);
    const bool special_map = W(Terrain() + 4) == 1 || static_cast<u32>(id - 0x29) < 0x12 || static_cast<u32>(id - 0x3D) < 0x12;
    if (Game(0x29) == 4) func_0017D890(hud, 0x3F);
    else if (special_map) func_0017D890(hud, 0x40);
    else if ((W(Cam() + 0x1A4) & 0x1C0u) != 0) func_0017D890(hud, 0x1F);
    else func_0017D890(hud, 0x3F);
    GRef<u8>(Scene() + 0x1650) = 1;
    const u32 v = Scene();
    const u32 c0 = v + 0xB0, c1 = v + 0x270;
    const u8 p = GRef<u8>(Cam() + 0x192);
    GRef<u8>(v + 6) = p;
    const u32 u = Unit(p);
    u32 c = Cam();
    if (special_map) {
        GRef<u8>(c + 0x1AE) = 6;
        const u8 g = Game(0x27);
        u32 table = 0x002B7670;
        if (static_cast<u32>(g - 0x29) < 0x12) table = 0x002B7670;
        else if (static_cast<u8>(g - 0x3D) < 0x12) table = 0x002B7708;
        else if (((0x204114u >> Game(0)) & 1u) != 0) table = 0x002B7708;
        GRef<u32>(Cam() + 0x1B0) = table;
    } else {
        const bool sit = (W(c + 0x1A4) & 0x1C0u) != 0;
        if ((W(c + 0x1A4) & 0x800u) != 0) {
            GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFF7FFu;
            PlayMenuSe(0, 1);
            c = Cam();
        }
        if (sit && GRef<u8>(c + 0x1AE) == 5) {
            GRef<u8>(c + 0x1AE) = 0;
            c = Cam();
            GRef<u32>(c + 0x1CC) = 0;
            GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x800u;
        }
        const u32 last = sit ? 4u : 5u;
        const u32 pad = Ctx()->pad_buttons[GRef<u8>(v + 6)][1];
        bool changed = false;
        if ((pad & 8u) != 0) {
            const u8 n = GRef<u8>(c + 0x1AE);
            GRef<u8>(c + 0x1AE) = n < last ? static_cast<u8>(n + 1) : 0;
            changed = true;
        } else if ((pad & 4u) != 0) {
            const u8 n = GRef<u8>(c + 0x1AE);
            GRef<u8>(c + 0x1AE) = n == 0 ? static_cast<u8>(last) : static_cast<u8>(n - 1);
            changed = true;
        }
        if (changed) {
            c = Cam();
            GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x800u;
            if ((W(u + 0x4C) & 0x1000000u) != 0) GRef<u32>(c + 0x1CC) = 0;
        }
    }
    func_0017D898(W(0x00410B40), GRef<u8>(Cam() + 0x1AE));
    func_0017D838(W(0x00410B40), W(Scene() + 0x1660));
    func_0010D5E8(v, 0);
    func_00171640(v);
    if ((W(u + 0x4C) & 0x1000000u) != 0)
        guest::CallGuest(W(0x002B77D8 + static_cast<u32>(GRef<u8>(Cam() + 0x1AE)) * 4), v);
    else
        func_00175BA0(v);
    c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u8>(CtxAddr() + 0x7B) = 0x80;
    } else {
        GRef<u8>(CtxAddr() + 0x7B) = 0x66;
        Ctx()->disp_flags = static_cast<u16>(Ctx()->disp_flags | 0x2000);
    }
    func_0010D760(v);
    f32 k = F(0x0040EB60);
    if ((GRef<u8>(v + 7) & 3) == 2) k = F(0x0040EB64);
    c = Cam();
    GRef<f32>(c1 + 0x16C) = F(c1 + 0x168) * k;
    GRef<f32>(c0 + 0x16C) = F(c0 + 0x168) * k;
    guest::GuestFrame frame(0x40);
    const u32 sp = frame.Address(), out = sp + 0x10, shake = sp + 0x20, tmp = sp + 0x30;
    const u32 a0 = c0 + 0x10, a1 = c1 + 0x10;
    const u32 f = W(c + 0x1A4);
    if ((f & 2u) != 0) {
        if ((f & 1u) != 0) {
            sceVu0CopyVector(c + 0x50, a0);
            sceVu0CopyVector(Cam() + 0x60, a1);
            GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) & 0xFFFFFFFEu;
        } else {
            func_00104F10(c + 0x70, c + 0x80);  // result unused, as the original
            sceVu0CopyVector(shake, Cam() + 0x50);
            sceVu0CopyVector(tmp, Cam() + 0x60);
            sceVu0SubVector(sp, Cam() + 0x50, a0);
            sceVu0SubVector(out, Cam() + 0x60, a1);
            sceVu0CopyVector(Cam() + 0x50, a0);
            sceVu0CopyVector(Cam() + 0x60, a1);
            sceVu0CopyVector(a0, shake);
            sceVu0CopyVector(a1, tmp);
        }
    }
    c = Cam();
    GRef<s32>(c + 0x180) += 1;
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x184) = 0;
        ZeroQ(c + 0x160);
        ZeroQ(Cam() + 0x170);
        c = Cam();
    }
    if (GRef<s32>(c + 0x184) < GRef<s32>(c + 0x180)) {
        const f32 d = func_00104F10(c + 0x90, c + 0xA0);
        f32 s;
        if (d < 1.0f) s = 0.0f;
        else if (d < 6.0f) s = fpu::Sub(d, 1.0f) / 5.0f;
        else s = 1.0f;
        GRef<u32>(Cam() + 0x180) = 0;
        const s32 r = ee::rand();
        c = Cam();
        GRef<s32>(c + 0x184) = r % 10 + 10;
        sceVu0CopyVector(c + 0x170, c + 0x160);
        const s32 rx = ee::rand();
        GRef<f32>(Cam() + 0x160) = fpu::IntToFloat(rx % 200 - 100) / 100.0f * F(Cam() + 0x188) * s;
        const s32 ry = ee::rand();
        GRef<f32>(Cam() + 0x164) = fpu::IntToFloat(ry % 200 - 100) / 100.0f * F(Cam() + 0x188) * s;
        c = Cam();
    }
    func_00246D98(fpu::IntToFloat(GRef<s32>(c + 0x180)) / fpu::IntToFloat(GRef<s32>(c + 0x184)), sp);
    func_00246DE8(out, c, c + 0x10, c + 0x20, c + 0x30, sp);
    sceVu0SubVector(shake, Cam() + 0x160, Cam() + 0x170);
    sceVu0ScaleVector(shake, shake, F(out + 4));
    sceVu0AddVector(shake, shake, Cam() + 0x170);
    func_00102EF0(a0, a0, shake);
    func_00102EF0(a1, a1, shake);
    f32 len = GetVectorLength(Cam() + 0x120);
    if (30.0f < len) len = 30.0f;
    len = fpu::Add(len, len);
    const u32 amp = static_cast<u32>(fpu::FloatToInt(len)) & 0xFF;
    const f32 k2 = F(0x0040EB68);
    const s32 r1 = ee::rand();
    const f32 fa = fpu::IntToFloat(static_cast<s32>(amp));
    GRef<f32>(sp) = fpu::IntToFloat(r1 % 200 - 100) * k2 * fa;
    const s32 r2 = ee::rand();
    GRef<u32>(sp + 8) = 0;
    GRef<u32>(sp + 0xC) = 0;
    GRef<f32>(sp + 4) = fpu::IntToFloat(r2 % 200 - 100) * k2 * fa;
    sceVu0AddVector(Cam() + 0x100, Cam() + 0x100, sp);
    c = Cam();
    const f32 hi = F(0x0040EB6C), lo = F(0x0040EB78);
    if (hi < F(c + 0x100)) GRef<f32>(c + 0x100) = fpu::Sub(F(c + 0x100), F(0x0040EB70));
    if (hi < F(c + 0x104)) GRef<f32>(c + 0x104) = fpu::Sub(F(c + 0x104), F(0x0040EB74));
    if (F(c + 0x100) < lo) GRef<f32>(c + 0x100) = fpu::Add(F(c + 0x100), F(0x0040EB7C));
    if (F(c + 0x104) < lo) GRef<f32>(c + 0x104) = fpu::Add(F(c + 0x104), F(0x0040EB80));
    func_00102EF0(a0, a0, Cam() + 0x100);
    func_00102EF0(a1, a1, Cam() + 0x100);
    UpdateCameraMatrices(c1);
    UpdateCameraMatrices(c0);
}

namespace {

// Random camera style from the u16 mask (bit n = style n allowed): up to 4097 tries of rand() % 10
// for an allowed style different from the current one (+0x191), else style 0.
void PickStyle(u32 mask) {
    for (s32 n = 0x1000; n >= 0; --n) {
        const s32 r = ee::rand() % 10;
        if (((mask >> (static_cast<u32>(r) & 31)) & 1u) == 0) continue;
        if (static_cast<u32>(r) == GRef<u8>(Cam() + 0x191)) continue;
        GRef<u8>(Cam() + 0x191) = static_cast<u8>(r);
        return;
    }
    GRef<u8>(Cam() + 0x191) = 0;
}

void RunStyle(u32 view) { guest::CallGuest(W(0x002B77B0 + static_cast<u32>(GRef<u8>(Cam() + 0x191)) * 4), view); }

}  // namespace

// EE: 0x001756C0
// Automatic replay camera (manual camera 0): view mode 0x43; on a cut the target unit +0x190 =
// the lock record target (*0x00410B10 + 0xC), kept only for a living (+0x4C 0x20000000) aircraft
// (+0x54 == 1) of side 1; the situation picks a style mask (+0x1D0 + 2n): n = 3 / 4 / 5 for the
// situation bits 0x40 / 0x80 / 0x100, else 1 when the unit is within 512 of the ground under the
// position history (0x0013E338), else 2 with a target, 0 without; a random style of that mask
// (0x002B77B0 table: 0x001744B8, 0x00174058, 0x00173670, 0x00172EF0, 0x00172850, 0x00172098,
// 0x00172450, 0x00171E88, 0x00171AE8, 0x00171CD0) then runs every frame.
// fpucheck: 1 sub
void func_001756C0(u32 view) {
    GRef<u8>(view + 7) = 0x43;
    u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u8>(c + 0x190) = GRef<u8>(W(0x00410B10) + 0xC);
        c = Cam();
        const u8 t = GRef<u8>(c + 0x190);
        if (t != 0xFF) {
            const u32 u = Unit(t);
            if (GRef<u8>(u + 0x54) != 1) GRef<u8>(Cam() + 0x190) = 0xFF;
            if ((W(u + 0x48) & 3u) != 1) GRef<u8>(Cam() + 0x190) = 0xFF;
            if ((W(u + 0x4C) & 0x20000000u) == 0) GRef<u8>(Cam() + 0x190) = 0xFF;
        }
        c = Cam();
        const u32 f = W(c + 0x1A4);
        u32 n;
        if ((f & 0x40u) != 0) {
            n = 3;
        } else if ((f & 0x80u) != 0) {
            n = 4;
        } else if ((f & 0x100u) != 0) {
            n = 5;
        } else {
            const f32 h = func_0013E338(Terrain(), c + 0x90);
            c = Cam();
            if (fpu::Sub(h, F(c + 0x94)) < 512.0f) n = 1;
            else n = GRef<u8>(c + 0x190) == 0xFF ? 0 : 2;
        }
        PickStyle(GRef<u16>(c + 0x1D0 + n * 2));
    }
    RunStyle(view);
}
AC04_GUEST_FUNCTION(0x001756C0, func_001756C0);

// EE: 0x00175890
// Manual camera 6 (table 0x002B77D8 entry 6, set on the special maps): near a path point (+0x1AD) view mode 3, solve flags 0x11 (0x91
// on entering, flag 0x400) and the path chase camera 0x00175420; else (leaving starts a cut) a
// random style of mask +0x1DC on each cut, run every frame.
void func_00175890(u32 view) {
    u32 c = Cam();
    if (GRef<u8>(c + 0x1AD) != 0xFF) {
        GRef<u8>(view + 7) = 3;
        GRef<u8>(view + 9) = 0x11;
        c = Cam();
        if ((W(c + 0x1A4) & 0x400u) == 0) {
            GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x400u;
            GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
        }
        func_00175420(view);
        return;
    }
    if ((W(c + 0x1A4) & 0x400u) != 0) GRef<u32>(c + 0x1A4) = (W(c + 0x1A4) & 0xFFFFFBFFu) | 4u;
    if ((W(c + 0x1A4) & 4u) != 0) PickStyle(GRef<u16>(c + 0x1DC) & 0xFFFFu);
    RunStyle(view);
}
AC04_GUEST_FUNCTION(0x00175890, func_00175890);

// Replay camera styles (table 0x002B77B0) and manual cameras (table 0x002B77D8); view V = flight
// scene view 0 (+6 watched unit, +7 view mode, +9 solve flags, cameras +0xB0 / +0x270 with their
// distance +0x168 at V+0x218 / V+0x3D8).

namespace {

void SetViewDistance(u32 view, f32 d) {
    GRef<f32>(view + 0x3D8) = d;
    GRef<f32>(view + 0x218) = d;
}

}  // namespace

// EE: 0x001744B8
// Style 0: fixed view looking at the unit from above-behind on the curve 0x00246D98 / 0x00246DE8
// over C+0x0..+0x3F at t = frame counter / 180 (eye = unit + (64, -(curve x 4096 + 64), 0), target
// angle y K 0x0040EACC); view mode 3, solve flags 0x10 (0x90 on a cut: cut length 210, +0x188
// cleared); distance 512.
// fpucheck: 2 add, 1 sub
void func_001744B8(u32 view) {
    guest::GuestFrame frame(0x40);
    const u32 sp = frame.Address(), tgt = sp + 0x10, w = sp + 0x20, out = sp + 0x30;
    const u32 u = Unit(GRef<u8>(view + 6));
    GRef<u8>(view + 7) = 3;
    GRef<u8>(view + 9) = 0x10;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x188) = 0;
        GRef<u32>(c + 0x1CC) = 0xD2;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
        c = Cam();
    }
    GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 2u;
    sceVu0CopyVector(sp, u);
    func_0010D680(view, sp);
    sceVu0CopyVector(Cam() + 0x80, sp);
    c = Cam();
    func_00246D98(fpu::IntToFloat(GRef<s32>(c + 0x1C8)) / 180.0f, w);
    func_00246DE8(out, c, c + 0x10, c + 0x20, c + 0x30, w);
    const f32 lift = fpu::Add(F(out) * 4096.0f, 64.0f);
    GRef<f32>(sp) = fpu::Add(F(sp), 64.0f);
    GRef<f32>(sp + 4) = fpu::Sub(F(sp + 4), lift);
    ZeroQ(tgt);
    GRef<f32>(tgt + 4) = F(0x0040EACC);
    func_0010D5F0(view, sp, tgt);
    sceVu0CopyVector(Cam() + 0x70, sp);
    SetViewDistance(view, 512.0f);
}
AC04_GUEST_FUNCTION(0x001744B8, func_001744B8);

// EE: 0x00174058
// Style 1: chase with drift. Look-at = unit + R(unit angles) (0, -5, 15) (also C+0x80; its motion ->
// C+0x120, last -> C+0x110). On a cut the eye C+0xE0 = unit + direction of the position difference
// C+0xC0 x |C+0xC0| x 60, plus R(Z by a random angle (rand % 360 - 180) x K 0x0040EAAC / 180, kept
// out of (-K 0x0040EAB4, K 0x0040EAB0) and within [K 0x0040EABC, K 0x0040EAB8]) R(angles) (0, 32, 0);
// drift C+0xD0 = C+0xC0 x K 0x0040EAC0; flags |= 1, C+0xDC = 1.0, shake +0x188 = K 0x0040EAC4, cut
// length 300, solve 0x91. Else the eye drifts by C+0xD0. Pushed out to 128 from the unit (x (128 -
// d) x K 0x0040EAC8), kept 256 above the ground (0x0013E2B0); eye with the unit angles (z 0);
// distance min(max(|sp - unit| x 4, 2048), C+0x1BC) where sp is the unit position (so 2048 or less).
// fpucheck: 2 sub
void func_00174058(u32 view) {
    guest::GuestFrame frame(0xC0);
    const u32 sp = frame.Address(), ang = sp + 0x10, m = sp + 0x20, dir = sp + 0x60, m2 = sp + 0x70, off = sp + 0xB0;
    GRef<u8>(view + 7) = 3;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    const u32 u = Unit(GRef<u8>(view + 6));
    const u32 rot = u + 0x10;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    GRef<f32>(sp + 4) = -5.0f;
    GRef<f32>(sp + 8) = 15.0f;
    GRef<u32>(sp) = 0;
    GRef<u32>(sp + 0xC) = 0;
    sceVu0ApplyMatrix(sp, m, sp);
    sceVu0AddVector(sp, sp, u);
    func_0010D680(view, sp);
    sceVu0CopyVector(Cam() + 0x80, sp);
    sceVu0SubVector(Cam() + 0x120, sp, Cam() + 0x110);
    sceVu0CopyVector(Cam() + 0x110, sp);
    sceVu0CopyVector(sp, u);
    GRef<u8>(view + 9) = 0x11;
    u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        const f32 s = GetVectorLength(c + 0xC0) * 60.0f;
        sceVu0Normalize(dir, Cam() + 0xC0);
        sceVu0ScaleVector(dir, dir, s);
        sceVu0AddVector(Cam() + 0xE0, sp, dir);
        GRef<f32>(Cam() + 0xEC) = 1.0f;
        ZeroQ(off);
        GRef<f32>(off + 4) = 32.0f;
        const s32 r = ee::rand() % 360;
        f32 a = fpu::IntToFloat(r - 180) * F(0x0040EAAC) / 180.0f;
        if (0.0f <= a && a < F(0x0040EAB0)) a = F(0x0040EAB0);
        if (a < 0.0f && F(0x0040EAB4) < a) a = F(0x0040EAB4);
        if (F(0x0040EAB8) < a) a = F(0x0040EAB8);
        if (a < F(0x0040EABC)) a = F(0x0040EABC);
        sceVu0UnitMatrix(m2);
        sceVu0RotMatrixZ(m2, m2, a);
        func_00102D28(m2, m2, rot);
        sceVu0ApplyMatrix(off, m2, off);
        sceVu0AddVector(Cam() + 0xE0, Cam() + 0xE0, off);
        sceVu0ScaleVector(Cam() + 0xD0, Cam() + 0xC0, F(0x0040EAC0));
        c = Cam();
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 1u;
        GRef<f32>(c + 0xDC) = 1.0f;
        GRef<f32>(c + 0x188) = F(0x0040EAC4);
        GRef<u32>(c + 0x1CC) = 0x12C;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    } else {
        sceVu0AddVector(c + 0xE0, c + 0xE0, c + 0xD0);
    }
    const f32 d = func_00104F10(Cam() + 0xE0, u);
    if (d < 128.0f) {
        sceVu0SubVector(dir, Cam() + 0xE0, u);
        sceVu0ScaleVector(dir, dir, fpu::Sub(128.0f, d) * F(0x0040EAC8));
        sceVu0AddVector(Cam() + 0xE0, Cam() + 0xE0, dir);
    }
    const f32 h = fpu::Sub(func_0013E2B0(Terrain(), Cam() + 0xE0), 256.0f);
    if (h < F(Cam() + 0xE4)) GRef<f32>(Cam() + 0xE4) = h;
    sceVu0CopyVector(ang, rot);
    GRef<u32>(ang + 8) = 0;
    func_0010D5F0(view, Cam() + 0xE0, ang);
    sceVu0CopyVector(Cam() + 0x70, Cam() + 0xE0);
    f32 dist = func_00104F10(sp, u) * 4.0f;
    if (dist < 2048.0f) dist = 2048.0f;
    if (F(Cam() + 0x1BC) < dist) dist = F(Cam() + 0x1BC);
    SetViewDistance(view, dist);
}
AC04_GUEST_FUNCTION(0x00174058, func_00174058);

namespace {

// The look-at used by most styles: unit + R(unit angles) (0, dy, dz), stored as C+0x80, its motion
// as C+0x120 and itself as C+0x110 (out = the point).
void ChaseLookAt(u32 view, u32 u, u32 m, u32 out, f32 dy, f32 dz) {
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, u + 0x10);
    GRef<f32>(out + 4) = dy;
    GRef<f32>(out + 8) = dz;
    GRef<u32>(out) = 0;
    GRef<u32>(out + 0xC) = 0;
    sceVu0ApplyMatrix(out, m, out);
    sceVu0AddVector(out, out, u);
    func_0010D680(view, out);
    sceVu0CopyVector(Cam() + 0x80, out);
    sceVu0SubVector(Cam() + 0x120, out, Cam() + 0x110);
    sceVu0CopyVector(Cam() + 0x110, out);
}

// Fly-by eye at C+0x150 on a cut: unit + the velocity direction x |C+0xC0| x 60, plus R(Z a) R(unit
// angles) (0, lift, 0) with a = (rand % 360 - 180) x k / 180 clamped by the four limits (as
// 0x00172098 / 0x00172450 do it; `neg_to` is the value given to a negative angle above `neg`).
void FlyByEye(u32 u, u32 pos, u32 scratch, f32 lift, u32 k, u32 pos_min, u32 neg, u32 neg_to, u32 hi, u32 lo) {
    const u32 dir = scratch, m = scratch + 0x10, off = scratch + 0x50;
    const f32 s = GetVectorLength(Cam() + 0xC0) * 60.0f;
    sceVu0Normalize(dir, Cam() + 0xC0);
    sceVu0ScaleVector(dir, dir, s);
    sceVu0AddVector(Cam() + 0x150, pos, dir);
    GRef<f32>(Cam() + 0x15C) = 1.0f;
    ZeroQ(off);
    GRef<f32>(off + 4) = lift;
    const s32 r = ee::rand() % 360;
    f32 a = fpu::IntToFloat(r - 180) * F(k) / 180.0f;
    if (0.0f <= a && a < F(pos_min)) a = F(pos_min);
    if (a < 0.0f && F(neg) < a) a = F(neg_to);
    if (F(hi) < a) a = F(hi);
    if (a < F(lo)) a = F(lo);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixZ(m, m, a);
    func_00102D28(m, m, u + 0x10);
    sceVu0ApplyMatrix(off, m, off);
    sceVu0AddVector(Cam() + 0x150, Cam() + 0x150, off);
}

}  // namespace

// EE: 0x00173670
// Style 2 (also manual camera 1): orbit. Look-at as the chase styles; view mode 3, solve 0x12. On a
// cut a random offset C+0x140 = R(X a, Y b) (0, 0, 128) with a = (rand % 360 - 180) / 180 x 0.5 x K
// 0x0040EA94 and b = (rand % 360 - 180) / 180 x K, cut length 300, flags |= 1, shake K 0x0040EA98,
// solve |= 0x80. When the unit is hidden from unit + 1.25 x offset (0x001712F8) the offset is
// flipped, then x = -128, then (128, 0, 0), then x = -128 again while still hidden (C+0x14C = 0,
// flags |= 5). Eye = unit + R(Y unit yaw) offset, target angles 0.
void func_00173670(u32 view) {
    guest::GuestFrame frame(0xC0);
    const u32 m = frame.Address(), p = m + 0x40, zero = m + 0x50, t = m + 0x60, m2 = m + 0x60, off = m + 0xA0, ang = m + 0xB0;
    GRef<u8>(view + 7) = 3;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    const u32 u = Unit(GRef<u8>(view + 6));
    ChaseLookAt(view, u, m, p, -5.0f, 15.0f);
    sceVu0CopyVector(p, u);
    GRef<u8>(view + 9) = 0x12;
    if ((W(Cam() + 0x1A4) & 4u) != 0) {
        ZeroQ(off);
        GRef<f32>(off + 8) = 128.0f;
        ZeroQ(ang);
        const f32 k = F(0x0040EA94);
        const s32 r1 = ee::rand() % 360;
        GRef<f32>(ang) = fpu::IntToFloat(r1 - 180) / 180.0f * 0.5f * k;
        const s32 r2 = ee::rand() % 360;
        GRef<f32>(ang + 4) = fpu::IntToFloat(r2 - 180) / 180.0f * k;
        SetUnitMatrixVu0(m2);
        sceVu0RotMatrixX(m2, m2, F(ang));
        sceVu0RotMatrixY(m2, m2, F(ang + 4));
        sceVu0ApplyMatrix(Cam() + 0x140, m2, off);
        const u32 c = Cam();
        GRef<u32>(c + 0x1CC) = 0x12C;
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 1u;
        GRef<f32>(c + 0x188) = F(0x0040EA98);
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    const auto hidden = [&]() {
        sceVu0ScaleVector(t, Cam() + 0x140, 1.25f);
        sceVu0AddVector(t, t, u);
        return 0 < func_001712F8(u, t);
    };
    if (hidden()) {
        sceVu0ScaleVector(Cam() + 0x140, Cam() + 0x140, -1.0f);
        if (hidden()) {
            GRef<f32>(Cam() + 0x140) = -128.0f;
            if (hidden()) {
                const u32 c = Cam();
                GRef<u32>(c + 0x144) = 0;
                GRef<f32>(c + 0x140) = 128.0f;
                GRef<u32>(c + 0x148) = 0;
                if (hidden()) GRef<f32>(Cam() + 0x140) = -128.0f;
            }
        }
        const u32 c = Cam();
        GRef<u32>(c + 0x14C) = 0;
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 5u;
    }
    ZeroQ(p);
    sceVu0CopyVector(p, Cam() + 0x140);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixY(m, m, F(u + 0x14));
    sceVu0ApplyMatrix(p, m, p);
    sceVu0AddVector(p, u, p);
    ZeroQ(zero);
    func_0010D5F0(view, p, zero);
    sceVu0CopyVector(Cam() + 0x70, p);
}
AC04_GUEST_FUNCTION(0x00173670, func_00173670);

// EE: 0x00172EF0
// Style 3: cockpit view of the unit (aircraft view byte +0x149 = 1, view mode 0xC2, flag 2 cleared,
// solve 0x13; on a cut length 300, aircraft +0x20 and shake cleared, solve 0x93): eye the unit
// position and angles, up vector the aircraft +0x10 (cleared), attach point the aircraft record
// +0x20 (z - 1024 when the unit is dead).
// fpucheck: 1 sub
void func_00172EF0(u32 view) {
    guest::GuestFrame frame(0x10);
    const u32 sp = frame.Address();
    const u32 u = Unit(GRef<u8>(view + 6));
    const u32 ac = W(u + 0x44);
    GRef<u8>(ac + 0x149) = 1;
    GRef<u8>(view + 7) = 0xC2;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) & 0xFFFFFFFDu;
    GRef<u8>(view + 9) = 0x13;
    const u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1CC) = 0x12C;
        ZeroQ(ac + 0x20);
        GRef<u32>(Cam() + 0x188) = 0;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    const u32 rec = AircraftRec(GRef<u8>(view + 6));
    ZeroQ(ac + 0x10);
    CopyQ(sp, rec + 0x20);
    if ((W(u + 0x4C) & 0x1000000u) == 0) GRef<f32>(sp + 8) = fpu::Sub(F(sp + 8), 1024.0f);
    func_0010D5F0(view, u, u + 0x10);
    func_0010D630(view, ac + 0x10);
    func_0010D610(view, sp);
}
AC04_GUEST_FUNCTION(0x00172EF0, func_00172EF0);

// EE: 0x00172850
// Style 4: fixed mounts on the aircraft. Look-at unit + R(angles) (0, -8, 256); view mode 3, solve
// 0x14. On a cut length 300, shake K 0x0040EA5C, a random entry C+0x198 = rand % 4 of the mount
// lists 0x002B77A0 (4 s8 mount indices each), solve |= 0x80. The mount (0x002B7720 + 16 i, plus the
// look offset 0x002B7760 + 16 i, in the unit frame) is replaced by the next of the list while the
// unit is hidden from it (0x001712F8; flags |= 5). Eye = the mount position in the unit frame.
void func_00172850(u32 view) {
    guest::GuestFrame frame(0x60);
    const u32 sp = frame.Address(), m = sp + 0x10, t = sp + 0x50;
    GRef<u8>(view + 7) = 3;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    const u32 u = Unit(GRef<u8>(view + 6));
    const u32 rot = u + 0x10;
    ChaseLookAt(view, u, m, sp, -8.0f, 256.0f);
    GRef<u8>(view + 9) = 0x14;
    u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1CC) = 0x12C;
        GRef<f32>(c + 0x188) = F(0x0040EA5C);
        const s32 r = ee::rand();
        GRef<s32>(Cam() + 0x198) = r % 4;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    const u32 list = 0x002B77A0 + static_cast<u32>(GRef<s32>(Cam() + 0x198)) * 4;
    SetUnitMatrixVu0(m);
    RotateMatrix(m, m, rot);
    sceVu0TransMatrix(m, m, u);
    const auto hidden = [&](u32 k) {
        const u32 i = static_cast<u32>(static_cast<s32>(GRef<s8>(list + k))) * 0x10;
        sceVu0AddVector(t, 0x002B7720 + i, 0x002B7760 + i);
        sceVu0ApplyMatrix(t, m, t);
        return 0 < func_001712F8(u, t);
    };
    if (hidden(0)) {
        GRef<s32>(Cam() + 0x198) = GRef<s8>(list + 1);
        if (hidden(1)) {
            GRef<s32>(Cam() + 0x198) = GRef<s8>(list + 2);
            if (hidden(2)) GRef<s32>(Cam() + 0x198) = GRef<s8>(list + 3);
        }
        GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 5u;
    }
    CopyQ(sp, 0x002B7720 + static_cast<u32>(GRef<s32>(Cam() + 0x198)) * 0x10);
    sceVu0ApplyMatrix(sp, m, sp);
    func_0010D5F0(view, sp, rot);
    sceVu0CopyVector(Cam() + 0x70, sp);
}
AC04_GUEST_FUNCTION(0x00172850, func_00172850);

// EE: 0x00172098
// Style 5: fly-by. Look-at as the chase styles, view mode 3, solve 0x11. On a cut the eye C+0x150 =
// fly-by point with lift 2048 and the angle limits K 0x0040EA30-0x0040EA40, cut length 300, flags |=
// 1, shake cleared, solve |= 0x80. Kept 256 above the ground; eye angles the unit angles (z 0);
// distance min(max(0 x 4, 2048), C+0x1BC) as style 1.
// fpucheck: 1 sub
void func_00172098(u32 view) {
    guest::GuestFrame frame(0xC0);
    const u32 sp = frame.Address(), ang = sp + 0x10, m = sp + 0x20;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    const u32 u = Unit(GRef<u8>(view + 6));
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u8>(view + 7) = 3;
    ChaseLookAt(view, u, m, sp, -5.0f, 15.0f);
    sceVu0CopyVector(sp, u);
    GRef<u8>(view + 9) = 0x11;
    if ((W(Cam() + 0x1A4) & 4u) != 0) {
        FlyByEye(u, sp, sp + 0x60, 2048.0f, 0x0040EA30, 0x0040EA34, 0x0040EA38, 0x0040EA38, 0x0040EA3C, 0x0040EA40);
        const u32 c = Cam();
        GRef<u32>(c + 0x1CC) = 0x12C;
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 1u;
        GRef<u32>(c + 0x188) = 0;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    const f32 h = fpu::Sub(func_0013E2B0(Terrain(), Cam() + 0x150), 256.0f);
    if (h < F(Cam() + 0x154)) GRef<f32>(Cam() + 0x154) = h;
    sceVu0CopyVector(ang, u + 0x10);
    GRef<u32>(ang + 8) = 0;
    func_0010D5F0(view, Cam() + 0x150, ang);
    sceVu0CopyVector(Cam() + 0x70, Cam() + 0x150);
    f32 dist = func_00104F10(sp, u) * 4.0f;
    if (dist < 2048.0f) dist = 2048.0f;
    if (F(Cam() + 0x1BC) < dist) dist = F(Cam() + 0x1BC);
    SetViewDistance(view, dist);
}
AC04_GUEST_FUNCTION(0x00172098, func_00172098);

// EE: 0x00172450
// Style 6: low fly-by, view mode 0x43. As style 5 with lift 64 and the limits K 0x0040EA44-0x0040EA58
// (a negative angle above -K 0x0040EA4C becomes K 0x0040EA50); the eye is moved to an obstacle
// between the eye at the unit height and itself (0x00171460), its y then 64 above that point,
// else 64 above the ground. Distance |eye - unit| x 4 within [512, 4096].
// fpucheck: 1 sub
void func_00172450(u32 view) {
    guest::GuestFrame frame(0xE0);
    const u32 m = frame.Address(), p = m + 0x40, ang = m + 0x50, hit = m + 0xC0, q = m + 0xD0;
    GRef<u8>(view + 7) = 0x43;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    const u32 u = Unit(GRef<u8>(view + 6));
    ChaseLookAt(view, u, m, p, -5.0f, 15.0f);
    sceVu0CopyVector(p, u);
    GRef<u8>(view + 9) = 0x11;
    if ((W(Cam() + 0x1A4) & 4u) != 0) {
        FlyByEye(u, p, m + 0x60, 64.0f, 0x0040EA44, 0x0040EA48, 0x0040EA4C, 0x0040EA50, 0x0040EA54, 0x0040EA58);
        sceVu0CopyVector(q, Cam() + 0x150);
        GRef<f32>(q + 4) = F(u + 4);
        f32 h;
        if (0 < func_00171460(hit, q, Cam() + 0x150)) {
            sceVu0CopyVector(Cam() + 0x150, hit);
            h = F(Cam() + 0x154);
        } else {
            h = func_0013E2B0(Terrain(), Cam() + 0x150);
        }
        const u32 c = Cam();
        GRef<f32>(c + 0x154) = fpu::Sub(h, 64.0f);
        GRef<u32>(c + 0x1CC) = 0x12C;
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 1u;
        GRef<u32>(c + 0x188) = 0;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    sceVu0CopyVector(ang, u + 0x10);
    GRef<u32>(ang + 8) = 0;
    func_0010D5F0(view, Cam() + 0x150, ang);
    sceVu0CopyVector(Cam() + 0x70, Cam() + 0x150);
    f32 dist = func_00104F10(Cam() + 0x150, u) * 4.0f;
    if (dist < 512.0f) dist = 512.0f;
    if (4096.0f < dist) dist = 4096.0f;
    SetViewDistance(view, dist);
}
AC04_GUEST_FUNCTION(0x00172450, func_00172450);

// EE: 0x00171E88
// Style 7: two-unit view. Look-at the midpoint of the unit and the camera target unit (+0x190);
// view mode 1, flag 2 cleared, solve 0x15 (0x95 on a cut: flags |= 1, shake K 0x0040EA2C, length
// 420). Eye = midpoint + (4096, -4096, 4096) (w as left on the stack by the original), target
// angles 0; distance (1 - (clamp(2 |u - t|, 128, 8192) - 128) / 8064) x 3584 + 512.
// fpucheck: 7
void func_00171E88(u32 view) {
    guest::GuestFrame frame(0x30);
    const u32 sp = frame.Address(), eye = sp + 0x10, zero = sp + 0x20;
    GRef<u8>(view + 7) = 1;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    u32 c = Cam();
    GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFFFFDu;
    const u32 t = Unit(GRef<u8>(c + 0x190));
    const u32 u = Unit(GRef<u8>(view + 6));
    sceVu0AddVector(sp, u, t);
    sceVu0ScaleVector(sp, sp, 0.5f);
    func_0010D680(view, sp);
    sceVu0CopyVector(Cam() + 0x80, sp);
    sceVu0SubVector(Cam() + 0x120, sp, Cam() + 0x110);
    sceVu0CopyVector(Cam() + 0x110, sp);
    GRef<u8>(view + 9) = 0x15;
    c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 1u;
        GRef<f32>(c + 0x188) = F(0x0040EA2C);
        GRef<u32>(c + 0x1CC) = 0x1A4;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    GRef<f32>(eye + 4) = fpu::Sub(F(sp + 4), 4096.0f);
    GRef<f32>(eye) = fpu::Add(F(sp), 4096.0f);
    GRef<f32>(eye + 8) = fpu::Add(F(sp + 8), 4096.0f);
    ZeroQ(zero);
    func_0010D5F0(view, eye, zero);
    sceVu0CopyVector(Cam() + 0x70, eye);
    f32 d = func_00104F10(u, t);
    d = fpu::Add(d, d);
    if (d < 128.0f) d = 128.0f;
    if (8192.0f < d) d = 8192.0f;
    const f32 f = fpu::Sub(d, 128.0f) / 8064.0f;
    SetViewDistance(view, fpu::Add(fpu::Sub(1.0f, f) * 3584.0f, 512.0f));
}
AC04_GUEST_FUNCTION(0x00171E88, func_00171E88);

namespace {

// Styles 8 / 9: look-at as the chase styles, fixed eye offset from the unit, distance 2048.
void SideCamera(u32 view, u8 solve, u32 shake, bool side) {
    guest::GuestFrame frame(0x50);
    const u32 sp = frame.Address(), m = sp + 0x10;
    GRef<u8>(view + 7) = 3;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    const u32 u = Unit(GRef<u8>(view + 6));
    ChaseLookAt(view, u, m, sp, -5.0f, 15.0f);
    GRef<u8>(view + 9) = solve;
    const u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1CC) = 0xF0;
        GRef<f32>(c + 0x188) = F(shake);
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    ZeroQ(sp);
    if (side) {
        GRef<f32>(sp + 4) = -32.0f;
        GRef<f32>(sp + 8) = -512.0f;
        GRef<u32>(sp) = 0;
        GRef<u32>(sp + 0xC) = 0;
        GRef<f32>(sp) = F(u) < F(0x002B75E0) ? 1024.0f : -1024.0f;
    } else {
        GRef<f32>(sp + 4) = -1024.0f;
        GRef<f32>(sp + 8) = -512.0f;
        GRef<u32>(sp) = 0;
        GRef<u32>(sp + 0xC) = 0;
    }
    sceVu0AddVector(sp, u, sp);
    func_0010D5F0(view, sp, u + 0x10);
    sceVu0CopyVector(Cam() + 0x70, sp);
    SetViewDistance(view, 2048.0f);
}

}  // namespace

// EE: 0x00171AE8
// Style 8: eye = unit + (+-1024 toward the point 0x002B75E0 in x, -32, -512), solve 0x16, cut
// length 240, shake K 0x0040EA24.
void func_00171AE8(u32 view) { SideCamera(view, 0x16, 0x0040EA24, true); }
AC04_GUEST_FUNCTION(0x00171AE8, func_00171AE8);

// EE: 0x00171CD0
// Style 9: eye = unit + (0, -1024, -512), solve 0x17, cut length 240, shake K 0x0040EA28.
// fpucheck: verified 0 (the counted sub belongs to GroundedEye below)
void func_00171CD0(u32 view) { SideCamera(view, 0x17, 0x0040EA28, false); }
AC04_GUEST_FUNCTION(0x00171CD0, func_00171CD0);

namespace {

u32 FollowState(u32 view) { return W(0x00410680 + static_cast<u32>(GRef<u8>(view + 6)) * 4); }

// Eye kept 256 above the ground under it (0x0013E2B0), then set with target angles 0.
// fpucheck: 1 sub
void GroundedEye(u32 view, u32 eye, u32 zero) {
    const f32 h = fpu::Sub(func_0013E2B0(Terrain(), eye), 256.0f);
    if (h < F(eye + 4)) GRef<f32>(eye + 4) = h;
    ZeroQ(zero);
    func_0010D5F0(view, eye, zero);
}

}  // namespace

// EE: 0x00175BA0
// Camera for a dead watched unit: view mode 1, solve 0x1A (0x9A on a cut: length 1800, shake K
// 0x0040EB40); look-at the unit; eye = unit + (K 0x0040EB44, -1, K 0x0040EB48) x (frames / 60 x 256 +
// 3072), target angles 0; distance 512.
// fpucheck: 1 add
void func_00175BA0(u32 view) {
    guest::GuestFrame frame(0x30);
    const u32 sp = frame.Address(), zero = sp + 0x10, off = sp + 0x20;
    const u32 u = Unit(GRef<u8>(view + 6));
    GRef<u8>(view + 7) = 1;
    GRef<u8>(view + 9) = 0x1A;
    const u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1CC) = 0x708;
        GRef<f32>(c + 0x188) = F(0x0040EB40);
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
    sceVu0CopyVector(sp, u);
    func_0010D680(view, sp);
    sceVu0CopyVector(Cam() + 0x80, sp);
    ZeroQ(zero);
    sceVu0CopyVector(sp, u);
    ZeroQ(off);
    const f32 s = fpu::Add(fpu::IntToFloat(GRef<s32>(Cam() + 0x1C8)) / 60.0f * 256.0f, 3072.0f);
    GRef<f32>(off) = F(0x0040EB44);
    GRef<f32>(off + 4) = -1.0f;
    GRef<f32>(off + 8) = F(0x0040EB48);
    sceVu0ScaleVector(off, off, s);
    sceVu0AddVector(sp, sp, off);
    func_0010D5F0(view, sp, zero);
    sceVu0CopyVector(Cam() + 0x70, sp);
    SetViewDistance(view, 512.0f);
}

// EE: 0x00172B78
// Default external replay view (style 12 without a followed projectile): view mode 0x43. Eye mount
// 0x002B7720 (x - 60, y + 24 tested) or 0x002B7750 (x + 60, y - 24 tested) in the unit frame,
// switched with hysteresis C+0x194 (60 frames on mount 0 while it is hidden, a cut when switching:
// flags |= 5); eye = unit + R(angles) mount; the original also builds the offset 0x004050B0 in the
// unit frame and adds it to a vector it does not use. Look-at unit + R(angles) (0, -8, 256); shake K
// 0x0040EA60, solve 0x14 (0x94 on a cut: flags |= 1, length 0xFFFF).
// fpucheck: 2 sub, 2 add
void func_00172B78(u32 view) {
    guest::GuestFrame frame(0x60);
    const u32 sp = frame.Address(), m = sp + 0x10, t = sp + 0x50;
    GRef<u8>(view + 7) = 0x43;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    const u32 u = Unit(GRef<u8>(view + 6));
    const u32 rot = u + 0x10;
    SetUnitMatrixVu0(m);
    RotateMatrix(m, m, rot);
    sceVu0TransMatrix(m, m, u);
    sceVu0CopyVector(t, 0x002B7720);
    GRef<f32>(t) = fpu::Sub(F(t), 60.0f);
    GRef<f32>(t + 4) = fpu::Add(F(t + 4), 24.0f);
    sceVu0ApplyMatrix(t, m, t);
    u32 c = Cam();
    if (0 < func_001712F8(u, t)) {
        c = Cam();
        if (GRef<s32>(c + 0x194) == 0) GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 5u;
        GRef<s32>(c + 0x194) = 0x3C;
    } else {
        c = Cam();
        const s32 n = GRef<s32>(c + 0x194);
        if (0 < n) {
            GRef<s32>(c + 0x194) = n - 1;
            if (n - 1 == 0) GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 5u;
        }
    }
    sceVu0CopyVector(t, 0x002B7750);
    GRef<f32>(t) = fpu::Add(F(t), 60.0f);
    GRef<f32>(t + 4) = fpu::Sub(F(t + 4), 24.0f);
    sceVu0ApplyMatrix(t, m, t);
    if (0 < func_001712F8(u, t)) {
        c = Cam();
        if (0 < GRef<s32>(c + 0x194)) GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 5u;
        GRef<u32>(c + 0x194) = 0;
    }
    c = Cam();
    CopyQ(sp, W(c + 0x194) != 0 ? 0x002B7750u : 0x002B7720u);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    sceVu0ApplyMatrix(sp, m, sp);
    sceVu0AddVector(sp, u, sp);
    GRef<u64>(t) = GRef<u64>(0x004050B0);
    GRef<u64>(t + 8) = GRef<u64>(0x004050B8);
    SetUnitMatrixVu0(m);
    RotateMatrix(m, m, rot);
    sceVu0ApplyMatrix(t, m, t);
    sceVu0AddVector(t, t, sp);
    func_0010D5F0(view, sp, rot);
    sceVu0CopyVector(Cam() + 0x70, sp);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    GRef<f32>(sp + 4) = -8.0f;
    GRef<f32>(sp + 8) = 256.0f;
    GRef<u32>(sp) = 0;
    GRef<u32>(sp + 0xC) = 0;
    sceVu0ApplyMatrix(sp, m, sp);
    sceVu0AddVector(sp, sp, u);
    func_0010D680(view, sp);
    sceVu0CopyVector(Cam() + 0x80, sp);
    sceVu0SubVector(Cam() + 0x120, sp, Cam() + 0x110);
    sceVu0CopyVector(Cam() + 0x110, sp);
    c = Cam();
    GRef<f32>(c + 0x188) = F(0x0040EA60);
    GRef<u8>(view + 9) = 0x14;
    c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 1u;
        GRef<u32>(c + 0x1CC) = 0xFFFF;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
}

// EE: 0x00174640
// Missile-follow view (follow state slot +0x10; nothing when -1): missile record p =
// *0x004106AC + 0x4000 + 0x60 slot (view mode 0x41 when p+0x5C bit 0); look-at p - (0, 8, 0); eye =
// p + R(X K 0x0040EAD0, Y K 0x0040EAD4, then p angles with z 0) (0, 0, 256), target angles 0;
// distance 512, or while the follow age +8 < 60 the curve 0x00246D98 / 0x00246DE8 through (0, 0, 0),
// (K 0x0040EAD8 x2, K 0x0040EADC), (K 0x0040EAE0, K 0x0040EAE4, K 0x0040EAE8), (1, 1, 1) at (60 -
// age) / 60, x 3584 + 512.
// fpucheck: 1 sub, 1 add
void func_00174640(u32 view) {
    const u32 st = FollowState(view);
    const s32 slot = GRef<s32>(st + 0x10);
    if (slot == -1) return;
    guest::GuestFrame frame(0xD0);
    const u32 sp = frame.Address(), off = sp + 0x10, m = sp + 0x20, ang = sp + 0x60, w = sp + 0x70, out = sp + 0x80,
              p0 = sp + 0x90;
    const u32 p = W(0x004106AC) + 0x4000 + static_cast<u32>(slot) * 0x60;
    if ((W(p + 0x5C) & 1u) != 0) GRef<u8>(view + 7) = 0x41;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    ZeroQ(off);
    GRef<f32>(off + 8) = 512.0f;
    sceVu0CopyVector(sp, p);
    GRef<f32>(sp + 4) = fpu::Sub(F(sp + 4), 8.0f);
    func_0010D680(view, sp);
    GRef<f32>(off + 8) = 256.0f;
    sceVu0CopyVector(ang, p + 0x10);
    GRef<u32>(ang + 8) = 0;
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, F(0x0040EAD0));
    sceVu0RotMatrixY(m, m, F(0x0040EAD4));
    func_00102D28(m, m, ang);
    sceVu0ApplyMatrix(off, m, off);
    sceVu0AddVector(sp, p, off);
    ZeroQ(ang);
    func_0010D5F0(view, sp, ang);
    f32 d = 512.0f;
    const s32 age = GRef<s32>(W(0x00410680 + static_cast<u32>(GRef<u8>(view + 6)) * 4) + 8);
    if (age < 0x3C) {
        const u32 p1 = p0 + 0x10, p2 = p0 + 0x20, p3 = p0 + 0x30;
        const f32 t = fpu::IntToFloat(0x3C - age) / 60.0f;
        ZeroQ(p0);
        GRef<f32>(p1) = F(0x0040EAD8);
        GRef<f32>(p1 + 4) = F(0x0040EAD8);
        GRef<f32>(p1 + 8) = F(0x0040EADC);
        GRef<u32>(p1 + 0xC) = 0;
        GRef<f32>(p2) = F(0x0040EAE0);
        GRef<f32>(p2 + 4) = F(0x0040EAE4);
        GRef<f32>(p2 + 8) = F(0x0040EAE8);
        GRef<u32>(p2 + 0xC) = 0;
        GRef<f32>(p3) = 1.0f;
        GRef<f32>(p3 + 4) = 1.0f;
        GRef<f32>(p3 + 8) = 1.0f;
        GRef<u32>(p3 + 0xC) = 0;
        func_00246D98(t, w);
        func_00246DE8(out, p0, p1, p2, p3, w);
        d = fpu::Add(F(out) * 3584.0f, 512.0f);
    }
    SetViewDistance(view, d);
}

// EE: 0x001748A8
// Special-weapon-follow view (follow state slot +0x14; nothing when -1): weapon record p = state +4,
// age a = state +8, camera state W = *0x00410B08 (+0 position, +0x10 frame counter, +0x14 / +0x18
// flags). Look-at p - (0, 8, 0); W flags: +0x14 bit 0 moves to +0x18 bit 0; at a == 1 and at a == 31
// (when +0x18 bit 0 is clear) +0x14 |= 1 and +0x18 &= ~2; both bit 0 cleared when p+0x5E or a >= 32.
// By weapon kind p+0x5A (jump table 0x004050C0): 8 - eye R(X K 0x0040EB00, Y K 0x0040EB04, p angles)
// (0, 0, 1024 + curve(a / 60) x 1024, or 2048 from a 60), distance curve((60 - a) / 60) x 3584 +
// 512 (512 from 60); 12-14 - eye with K 0x0040EB08 / 0x0040EB0C at 512, distance curve((300 - a) /
// 300); 5 - counter n (0 until a 60, then +1 up to 60; view 0x41 with p+0x5E), eye with X = (n / 60 x
// K 0x0040EB10 + K 0x0040EB10) x K 0x0040EB14, Y = min((a / 1024 x 0.25 + 0.75) x K 0x0040EB14, K
// 0x0040EB14) and p angles with x 0, at a / 300 x 3584 + 512 (4096 when n or a >= 300), distance
// curve((60 - n) / 60); 6 - eye = watched unit + W (W set, when the follow state is replay-locked
// +0xC bit 0x100, to R(Y yaw) (196, 0, 0) with yaw = atan2 of the unit's forward axis, minus K
// 0x0040EB1C above K 0x0040EB18), distance 512; 9 - view 0x41 with p+0x5E; with p+0x70 bit 0x20
// W+0x18 |= 2 (counter reset) and then the look-at is W (counter +1 up to 0x78), else W = p; eye at
// 2048 with X = a / 1024 x 0.5 x K 0x0040EB20 (K 0x0040EB24 from 1024) and Y = -a / 1024 x 0.5 x K
// 0x0040EB28 (K 0x0040EB2C); others - as 9 without W, constants K 0x0040EB30-0x0040EB3C. Kinds 9 /
// other: distance (512 - a) / 512 x 1536 + 512 below 512, else 512. Curve points (0, 0, 0), (K
// 0x0040EAEC x2, K 0x0040EAF0), (K 0x0040EAF4, K 0x0040EAF8, K 0x0040EAFC), (1, 1, 1). Eyes kept
// 256 above the ground. W+0x10 = counter.
// fpucheck: verified 15 (the 6 ground clamps share GroundedEye)
void func_001748A8(u32 view) {
    const u32 W2 = W(0x00410B08);
    s32 n = GRef<s32>(W2 + 0x10);
    const u32 st = FollowState(view);
    if (GRef<s32>(st + 0x14) == -1) return;
    guest::GuestFrame frame(0xF0);
    const u32 sp = frame.Address(), p0 = sp + 0x10, p1 = sp + 0x20, p2 = sp + 0x30, p3 = sp + 0x40, w = sp + 0x50,
              out = sp + 0x60, off = sp + 0x70, m = sp + 0x80, ang = sp + 0xC0, d = sp + 0xD0, e = sp + 0xE0;
    const u32 p = W(st + 4);
    const s32 age = GRef<s32>(st + 8);
    sceVu0CopyVector(sp, p);
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<f32>(sp + 4) = fpu::Sub(F(sp + 4), 8.0f);
    {
        const u32 cw = W(0x00410B08);
        if ((W(cw + 0x14) & 1u) != 0) {
            GRef<u32>(cw + 0x14) = W(cw + 0x14) & 0xFFFFFFFEu;
            GRef<u32>(cw + 0x18) = W(cw + 0x18) | 1u;
        }
        if (age == 1) {
            GRef<u32>(cw + 0x14) = W(cw + 0x14) | 1u;
            GRef<u32>(cw + 0x18) = W(cw + 0x18) & 0xFFFFFFFDu;
        }
        if (age == 0x1F && (W(cw + 0x18) & 1u) == 0) {
            GRef<u32>(cw + 0x18) = W(cw + 0x18) & 0xFFFFFFFDu;
            GRef<u32>(cw + 0x14) = W(cw + 0x14) | 1u;
        }
        if (GRef<u8>(p + 0x5E) != 0 || !(age < 0x20)) {
            GRef<u32>(cw + 0x14) = W(cw + 0x14) & 0xFFFFFFFEu;
            GRef<u32>(cw + 0x18) = W(cw + 0x18) & 0xFFFFFFFEu;
        }
    }
    func_0010D680(view, sp);
    ZeroQ(off);
    f32 dist = 512.0f;
    const f32 k0 = F(0x0040EAEC), k1 = F(0x0040EAF0);
    GRef<f32>(p1 + 4) = k0;
    GRef<f32>(p1 + 8) = k1;
    GRef<f32>(p2) = F(0x0040EAF4);
    GRef<f32>(p2 + 4) = F(0x0040EAF8);
    GRef<f32>(p2 + 8) = F(0x0040EAFC);
    GRef<f32>(p3 + 8) = 1.0f;
    GRef<u32>(p3 + 0xC) = 0;
    GRef<f32>(off + 8) = 512.0f;
    ZeroQ(p0);
    GRef<f32>(p1) = k0;
    GRef<u32>(p1 + 0xC) = 0;
    GRef<u32>(p2 + 0xC) = 0;
    GRef<f32>(p3) = 1.0f;
    GRef<f32>(p3 + 4) = 1.0f;
    sceVu0UnitMatrix(m);
    const auto curve = [&](f32 t) {
        func_00246D98(t, w);
        func_00246DE8(out, p0, p1, p2, p3, w);
        return F(out);
    };
    // Eye = p + R(m, with the given angles) off; kept above the ground.
    const auto place = [&](u32 rot) {
        func_00102D28(m, m, rot);
        sceVu0ApplyMatrix(off, m, off);
        sceVu0AddVector(sp, p, off);
        GroundedEye(view, sp, ang);
    };
    const auto ramp = [&](u32 kx_lin, u32 kx_max, u32 ky_lin, u32 ky_max) {
        f32 ax, ay;
        if (age < 0x400) {
            ax = fpu::IntToFloat(age) * 0.0009765625f * 0.5f * F(kx_lin);
            ay = -fpu::IntToFloat(age) * 0.0009765625f * 0.5f * F(ky_lin);
        } else {
            ax = F(kx_max);
            ay = F(ky_max);
        }
        sceVu0RotMatrixX(m, m, ax);
        sceVu0RotMatrixY(m, m, ay);
        GRef<f32>(off + 8) = 2048.0f;
        sceVu0CopyVector(ang, p + 0x10);
        GRef<u32>(ang) = 0;
        place(ang);
        if (age < 0x200) dist = fpu::Add(fpu::IntToFloat(0x200 - age) * 0.001953125f * 1536.0f, 512.0f);
        else dist = 512.0f;
    };
    switch (GRef<u8>(p + 0x5A)) {
    case 8: {
        f32 z = 2048.0f;
        GRef<f32>(off + 8) = 1024.0f;
        const s32 a = GRef<s32>(st + 8);
        if (a < 0x3C) z = fpu::Add(curve(fpu::IntToFloat(a) / 60.0f) * 1024.0f, 1024.0f);
        GRef<f32>(off + 8) = z;
        sceVu0UnitMatrix(m);
        sceVu0RotMatrixX(m, m, F(0x0040EB00));
        sceVu0RotMatrixY(m, m, F(0x0040EB04));
        place(p + 0x10);
        const s32 b = GRef<s32>(FollowState(view) + 8);
        if (b < 0x3C) dist = fpu::Add(curve(fpu::IntToFloat(0x3C - b) / 60.0f) * 3584.0f, 512.0f);
        else dist = 512.0f;
        break;
    }
    case 12: case 13: case 14: {
        GRef<f32>(off + 8) = 512.0f;
        SetUnitMatrixVu0(m);
        sceVu0RotMatrixX(m, m, F(0x0040EB08));
        sceVu0RotMatrixY(m, m, F(0x0040EB0C));
        place(p + 0x10);
        const s32 b = GRef<s32>(FollowState(view) + 8);
        if (b < 0x12C) dist = fpu::Add(curve(fpu::IntToFloat(0x12C - b) / 300.0f) * 3584.0f, 512.0f);
        else dist = 512.0f;
        break;
    }
    case 5: {
        const s32 a = GRef<s32>(FollowState(view) + 8);
        if (!(0x3B < a)) n = 0;
        if (GRef<u8>(p + 0x5E) != 0) {
            GRef<u8>(view + 7) = 0x41;
            if (!(0x3B < n)) n = n + 1;
        }
        f32 z = 4096.0f;
        if (n == 0 && GRef<s32>(FollowState(view) + 8) < 0x12C)
            z = fpu::Add(fpu::IntToFloat(GRef<s32>(FollowState(view) + 8)) / 300.0f * 3584.0f, 512.0f);
        GRef<f32>(off + 8) = z;
        const f32 kx = F(0x0040EB10), ky = F(0x0040EB14);
        const f32 y0 = fpu::Add(fpu::IntToFloat(age) * 0.0009765625f * 0.25f, 0.75f);
        const f32 x0 = fpu::Add(fpu::IntToFloat(n) / 60.0f * kx, kx);
        f32 ay = y0 * ky;
        if (ky < ay) ay = ky;
        sceVu0RotMatrixX(m, m, x0 * ky);
        sceVu0RotMatrixY(m, m, ay);
        sceVu0CopyVector(ang, p + 0x10);
        GRef<u32>(ang) = 0;
        place(ang);
        dist = fpu::Add(curve(fpu::IntToFloat(0x3C - n) / 60.0f) * 3584.0f, 512.0f);
        break;
    }
    case 6: {
        if ((W(FollowState(view) + 0xC) & 0x100u) != 0) {
            StoreVf0(d);
            GRef<f32>(d + 8) = 1.0f;
            SetUnitMatrixVu0(m);
            RotateMatrix(m, m, Unit(GRef<u8>(view + 6)) + 0x10);
            sceVu0ApplyMatrix(d, m, d);
            f32 yaw = ee::atan2f(F(d), F(d + 8));
            if (F(0x0040EB18) < yaw) yaw = fpu::Sub(yaw, F(0x0040EB1C));
            StoreVf0(e);
            GRef<f32>(e) = 196.0f;
            SetUnitMatrixVu0(m);
            sceVu0RotMatrixY(m, m, yaw);
            sceVu0ApplyMatrix(e, m, e);
            GRef<u32>(e + 0xC) = 0;
            sceVu0CopyVector(W(0x00410B08), e);
        }
        sceVu0AddVector(sp, Unit(GRef<u8>(view + 6)), W(0x00410B08));
        GroundedEye(view, sp, ang);
        GRef<f32>(view + 0x218) = dist;
        GRef<s32>(W(0x00410B08) + 0x10) = n;
        GRef<f32>(view + 0x3D8) = dist;
        return;
    }
    case 9: {
        if (GRef<u8>(p + 0x5E) != 0) GRef<u8>(view + 7) = 0x41;
        u32 cw = W(0x00410B08);
        if ((GRef<u32>(p + 0x70) & 0x20u) != 0 && (W(cw + 0x18) & 2u) == 0) {
            GRef<u32>(cw + 0x18) = W(cw + 0x18) | 2u;
            n = 0;
        }
        cw = W(0x00410B08);
        if ((W(cw + 0x18) & 2u) != 0) {
            if (!(0x77 < n)) n = n + 1;
            func_0010D680(view, cw);
        } else {
            sceVu0CopyVector(cw, p);
        }
        ramp(0x0040EB20, 0x0040EB24, 0x0040EB28, 0x0040EB2C);
        break;
    }
    default:
        if (GRef<u8>(p + 0x5E) != 0) GRef<u8>(view + 7) = 0x41;
        ramp(0x0040EB30, 0x0040EB34, 0x0040EB38, 0x0040EB3C);
        break;
    }
    GRef<f32>(view + 0x218) = dist;
    GRef<s32>(W(0x00410B08) + 0x10) = n;
    GRef<f32>(view + 0x3D8) = dist;
}

// EE: 0x001759E8
// Style 12 / manual camera 2: follows the projectile of the follow state: a special weapon (+0x14)
// -> 0x001748A8 (view 0x43, solve 0x18, shake 0, flag 2 cleared; on entering flag 0x200, solve
// 0x98, flags |= 5), a missile (+0x10) -> 0x00174640 (same, entering flags |= 0x205), else the
// default external view 0x00172B78 (view 0x43, solve 0x14; leaving 0x200 flags |= 5).
void func_001759E8(u32 view) {
    const u32 st = FollowState(view);
    u32 c;
    if (GRef<s32>(st + 0x14) != -1) {
        GRef<u8>(view + 7) = 0x43;
        c = Cam();
        GRef<u32>(c + 0x188) = 0;
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFFFFDu;
        GRef<u8>(view + 9) = 0x18;
        c = Cam();
        if ((W(c + 0x1A4) & 0x200u) == 0) {
            GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x200u;
            GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
            GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 5u;
        }
        func_001748A8(view);
        return;
    }
    if (GRef<s32>(st + 0x10) != -1) {
        GRef<u8>(view + 7) = 0x43;
        c = Cam();
        GRef<u32>(c + 0x188) = 0;
        GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFFFFDu;
        GRef<u8>(view + 9) = 0x18;
        c = Cam();
        if ((W(c + 0x1A4) & 0x200u) == 0) GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x205u;
        func_00174640(view);
        return;
    }
    GRef<u8>(view + 7) = 0x43;
    GRef<u8>(view + 9) = 0x14;
    c = Cam();
    if ((W(c + 0x1A4) & 0x200u) != 0) GRef<u32>(c + 0x1A4) = (W(c + 0x1A4) & 0xFFFFFDFFu) | 5u;
    func_00172B78(view);
}
AC04_GUEST_FUNCTION(0x001759E8, func_001759E8);

// EE: 0x00175420
// Path chase camera: look-at = the extrapolated unit position (2 C+0xA0 - C+0xB0) + R(unit angles)
// (0, -5, 15) (C+0x80 / +0x120 / +0x110 as usual), shake 0, flag 2; eye = the nearest path point
// (table C+0x1B0 entry C+0x1AD, w 1.0), target angles 0. The original then scales C+0x80 - C+0x70
// to the height C+0x74 and takes a length it never uses. Distance |eye - unit| x C+0x1C4 within
// [512, C+0x1BC]. A cut (flag 4) or a path point change (0x1000): length 0xFFFF, flags |= 5, solve
// |= 0x80.
void func_00175420(u32 view) {
    guest::GuestFrame frame(0x90);
    const u32 sp = frame.Address(), m = sp + 0x10, q = sp + 0x50, zero = sp + 0x60, eye = sp + 0x70, r = sp + 0x80;
    u32 u = Unit(GRef<u8>(view + 6));
    sceVu0SubVector(q, Cam() + 0xA0, Cam() + 0xB0);
    sceVu0AddVector(q, q, Cam() + 0xA0);
    GRef<u32>(Cam() + 0x188) = 0;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, u + 0x10);
    GRef<f32>(sp + 4) = -5.0f;
    GRef<f32>(sp + 8) = 15.0f;
    GRef<u32>(sp) = 0;
    GRef<u32>(sp + 0xC) = 0;
    sceVu0ApplyMatrix(sp, m, sp);
    sceVu0AddVector(sp, sp, q);
    func_0010D680(view, sp);
    sceVu0CopyVector(Cam() + 0x80, sp);
    sceVu0SubVector(Cam() + 0x120, sp, Cam() + 0x110);
    sceVu0CopyVector(Cam() + 0x110, sp);
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) | 2u;
    u = Unit(GRef<u8>(view + 6));
    sceVu0CopyVector(sp, u);
    u32 c = Cam();
    const u32 pt = W(W(c + 0x1B0) + 0x10) + static_cast<u32>(GRef<u8>(c + 0x1AD)) * 0x10;
    GRef<f32>(eye) = F(pt);
    GRef<f32>(eye + 4) = F(pt + 4);
    GRef<f32>(eye + 0xC) = 1.0f;
    GRef<f32>(eye + 8) = F(pt + 8);
    ZeroQ(zero);
    func_0010D5F0(view, eye, zero);
    sceVu0CopyVector(Cam() + 0x70, eye);
    sceVu0SubVector(r, Cam() + 0x80, Cam() + 0x70);
    sceVu0ScaleVector(r, r, F(Cam() + 0x74) / F(r + 4));
    sceVu0SubVector(r, r, Cam() + 0x70);
    GetVectorLength(r);  // result unused, as the original
    f32 d = func_00104F10(eye, u) * F(Cam() + 0x1C4);
    c = Cam();
    if (d < 512.0f) d = 512.0f;
    if (F(c + 0x1BC) < d) d = F(c + 0x1BC);
    const u32 f = W(c + 0x1A4);
    SetViewDistance(view, d);
    if ((f & 4u) != 0) GRef<u32>(c + 0x1A4) = f | 0x1000u;
    const u32 g = W(c + 0x1A4);
    if ((g & 0x1000u) != 0) {
        GRef<u32>(c + 0x1CC) = 0xFFFF;
        GRef<u32>(c + 0x1A4) = g | 5u;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    }
}

// EE: 0x00173468
// Manual camera 4: the player view of the watched unit (aircraft view byte +0x149 = 0, view mode 0,
// flag 0x100, shake 0, flag 2 cleared, solve 0x19; a cut: length 0xFFFF, solve |= 0x80, flag 0x100
// cleared); aircraft look angles +0x10 cleared then, while alive, steered by the pad extension
// bytes (+0x18 - +0x19 and +0x1B - +0x1A, x K 0x0040EA84 x 0.5 / 255; pad bit 0x20 adds K to the
// yaw), the yaw wrapped (K 0x0040EA84 / 0x0040EA88 / 0x0040EA8C / 0x0040EA90); attach point
// aircraft +0x50 (z - 1024 when dead).
// fpucheck: 2 add + up to 3
void func_00173468(u32 view) {
    guest::GuestFrame frame(0x10);
    const u32 sp = frame.Address();
    const u32 pe = PadExt(GRef<u8>(view + 6));
    const u32 u = Unit(GRef<u8>(view + 6));
    const u32 ac = W(u + 0x44);
    GRef<u8>(ac + 0x149) = 0;
    GRef<u8>(view + 7) = 0;
    GRef<u32>(view) = W(view) | 0x100u;
    u32 c = Cam();
    GRef<u32>(c + 0x188) = 0;
    GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFFFFDu;
    ZeroQ(ac + 0x10);
    GRef<u8>(view + 9) = 0x19;
    c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1CC) = 0xFFFF;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
        GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    }
    ZeroQ(sp);
    if ((W(u + 0x4C) & 0x1000000u) != 0) {
        const f32 k = F(0x0040EA84);
        const s32 dx = static_cast<s32>(GRef<u8>(pe + 0x18)) - static_cast<s32>(GRef<u8>(pe + 0x19));
        GRef<f32>(ac + 0x10) = fpu::Add(F(ac + 0x10), fpu::IntToFloat(dx) * k * 0.5f / 255.0f);
        const s32 dy = static_cast<s32>(GRef<u8>(pe + 0x1B)) - static_cast<s32>(GRef<u8>(pe + 0x1A));
        f32 y = fpu::Add(F(ac + 0x14), fpu::IntToFloat(dy) * k * 0.5f / 255.0f);
        GRef<f32>(ac + 0x14) = y;
        if ((W(pe) & 0x20u) != 0) {
            y = fpu::Add(y, k);
            GRef<f32>(ac + 0x14) = y;
        }
        y = F(ac + 0x14);
        if (k < y) {
            y = fpu::Sub(y, F(0x0040EA88));
            GRef<f32>(ac + 0x14) = y;
        }
        if (y < F(0x0040EA8C)) GRef<f32>(ac + 0x14) = fpu::Add(y, F(0x0040EA90));
    } else {
        GRef<f32>(sp + 8) = -1024.0f;
    }
    sceVu0AddVector(sp, sp, ac + 0x50);
    func_0010D5F0(view, u, u + 0x10);
    func_0010D630(view, ac + 0x10);
    func_0010D610(view, sp);
}
AC04_GUEST_FUNCTION(0x00173468, func_00173468);

// EE: 0x00173020
// Manual camera 3: cockpit look-around (view byte +0x149 = 1, view mode 0xC2, flag 2 cleared,
// solve 0x13; a cut: length 0xFFFF, aircraft +0x20 cleared, solve |= 0x80; shake 0). While alive:
// the look angles aircraft +0x20 (previous kept) are rebuilt from the pad extension bytes: yaw =
// +0x1B x lim / 255 + +0x1A x -lim / 255 clamped to +-lim, pitch = +0x18 x K 0x0040EA64 x 0.5 / 255 -
// +0x19 x ... clamped to [K 0x0040EA68, K 0x0040EA6C], each smoothed (new x 0.125 + old x 0.875) and
// copied to +0x10; the camera pitch is limited by a curve of the yaw (head limits from the aircraft
// record +0x30..+0x40, +0xD4..+0xE4 in 16:9) and by K 0x0040EA70; +0x18 / +0x1C cleared; the attach
// point (record +0x20) shifted in x by the yaw share of the side offsets; yaw wrapped (K 0x0040EA74
// - 0x0040EA80). Dead: angles vf0, attach z - 1024.
// fpucheck: verified 17 (the original shares the top - base sub and the final add of the two curve branches)
void func_00173020(u32 view) {
    guest::GuestFrame frame(0x20);
    const u32 sp = frame.Address(), prev = sp + 0x10;
    const s32 p = GRef<u8>(view + 6);
    const u32 pe = PadExt(p);
    const u32 u = Unit(p);
    const u32 ac = W(u + 0x44);
    GRef<u8>(ac + 0x149) = 1;
    GRef<u8>(view + 7) = 0xC2;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    GRef<u32>(Cam() + 0x1A4) = W(Cam() + 0x1A4) & 0xFFFFFFFDu;
    GRef<u8>(view + 9) = 0x13;
    u32 c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        GRef<u32>(c + 0x1CC) = 0xFFFF;
        ZeroQ(ac + 0x20);
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
        c = Cam();
    }
    GRef<u32>(c + 0x188) = 0;
    const u32 rec = AircraftRec(GRef<u8>(view + 6));
    if ((W(u + 0x4C) & 0x1000000u) == 0) {
        CopyQ(sp, rec + 0x20);
        StoreVf0(ac + 0x10);
        GRef<f32>(sp + 8) = fpu::Sub(F(sp + 8), 1024.0f);
    } else {
        CopyQ(prev, ac + 0x20);
        ZeroQ(ac + 0x20);
        f32 side_l, side_r, lim, top, base;
        if ((Ctx()->flags & 0x10000u) != 0) {
            side_l = F(rec + 0xD4);
            side_r = F(rec + 0xD8);
            lim = F(rec + 0xDC);
            top = F(rec + 0xE0);
            base = F(rec + 0xE4);
        } else {
            side_l = F(rec + 0x30);
            side_r = F(rec + 0x34);
            lim = F(rec + 0x38);
            top = F(rec + 0x3C);
            base = F(rec + 0x40);
        }
        const f32 nlim = -lim;
        f32 y = fpu::Add(F(ac + 0x24), fpu::IntToFloat(GRef<u8>(pe + 0x1B)) * lim / 255.0f);
        GRef<f32>(ac + 0x24) = y;
        y = fpu::Add(y, fpu::IntToFloat(GRef<u8>(pe + 0x1A)) * nlim / 255.0f);
        GRef<f32>(ac + 0x24) = y;
        if (lim < y) GRef<f32>(ac + 0x24) = lim;
        if (F(ac + 0x24) < nlim) GRef<f32>(ac + 0x24) = nlim;
        GRef<f32>(ac + 0x24) = fpu::Add(F(ac + 0x24) * 0.125f, F(prev + 4) * 0.875f);
        const f32 kp = F(0x0040EA64), pmin = F(0x0040EA68);
        f32 x = fpu::Add(F(ac + 0x20), fpu::IntToFloat(GRef<u8>(pe + 0x18)) * kp * 0.5f / 255.0f);
        GRef<f32>(ac + 0x20) = x;
        x = fpu::Sub(x, fpu::IntToFloat(GRef<u8>(pe + 0x19)) * kp * 0.5f / 255.0f);
        GRef<f32>(ac + 0x20) = x;
        if (x < pmin) GRef<f32>(ac + 0x20) = pmin;
        const f32 pmax = F(0x0040EA6C);
        if (pmax < F(ac + 0x20)) GRef<f32>(ac + 0x20) = pmax;
        GRef<f32>(ac + 0x20) = fpu::Add(F(ac + 0x20) * 0.125f, F(prev) * 0.875f);
        CopyQ(ac + 0x10, ac + 0x20);
        const f32 a = F(ac + 0x14);
        f32 cap;
        if (pmax < a) {
            const f32 num = fpu::Sub(a, pmax);
            const f32 den = fpu::Sub(lim, pmax);
            const f32 span = fpu::Sub(top, base);
            const f32 t = num / den;
            cap = fpu::Add(t * t * span, base);
        } else if (a < pmin) {
            const f32 num = fpu::Add(a, pmax);
            const f32 den = fpu::Add(nlim, pmax);
            const f32 span = fpu::Sub(top, base);
            const f32 t = num / den;
            cap = fpu::Add(t * t * span, base);
        } else {
            f32 t = a / pmax;
            if (t < 0.0f) t = -t;
            cap = t * base;
        }
        f32 v = F(ac + 0x10);
        if (v < cap) {
            GRef<f32>(ac + 0x10) = cap;
            v = cap;
        }
        if (F(0x0040EA70) < v) GRef<f32>(ac + 0x10) = F(0x0040EA70);
        GRef<u32>(ac + 0x18) = 0;
        GRef<u32>(ac + 0x1C) = 0;
        CopyQ(sp, rec + 0x20);
        if (0.0f < F(ac + 0x24)) GRef<f32>(sp) = fpu::Add(F(sp), side_l * F(ac + 0x24) / lim);
        if (F(ac + 0x24) < 0.0f) GRef<f32>(sp) = fpu::Add(F(sp), side_r * F(ac + 0x24) / nlim);
        f32 yaw = F(ac + 0x14);
        if (F(0x0040EA74) < yaw) {
            yaw = fpu::Sub(yaw, F(0x0040EA78));
            GRef<f32>(ac + 0x14) = yaw;
        }
        if (yaw < F(0x0040EA7C)) GRef<f32>(ac + 0x14) = fpu::Add(yaw, F(0x0040EA80));
    }
    func_0010D5F0(view, u, u + 0x10);
    func_0010D630(view, ac + 0x10);
    func_0010D610(view, sp);
}
AC04_GUEST_FUNCTION(0x00173020, func_00173020);

// EE: 0x00173A98
// Manual camera 5 (and style 15): follows the player's missile and then its target. View mode 0x43,
// flag 2 cleared. Without a target being followed (flag 0x20): a missile in flight (follow slot
// +0x10) sets flag 0x10 (counter reset), a missile that hit (record +0x5C bit 0) switches to its
// target +0x59 (C+0x1AC, flag 0x20, length 180); no missile clears 0x10. No target (0xFF) clears
// 0x20. Look-at: the target (solve 0x18, entering 0x200 with solve |= 0x80), the missile (same) or
// the chase point (solve 0x11; leaving 0x200: flags |= 5, solve |= 0x80). Eye C+0xE0 as style 1 (a
// cut: fly-by from the unit with lift 64 and an unclamped random roll (an extra rand() is drawn
// first, as the original), drift K 0x0040EAA0, shake K 0x0040EAA4, flags |= 1 and &= ~0x30, length
// 300), pushed out to 128 (K 0x0040EAA8), 256 above the ground; distance from the look-at (the
// unit on a cut) as style 1.
// fpucheck: 2 sub
void func_00173A98(u32 view) {
    guest::GuestFrame frame(0xC0);
    const u32 sp = frame.Address(), ang = sp + 0x10, m = sp + 0x20, dir = sp + 0x60, m2 = sp + 0x70, off = sp + 0xB0;
    GRef<u8>(view + 7) = 0x43;
    GRef<u32>(view) = W(view) & 0xFFFFFEFFu;
    u32 c = Cam();
    const u32 f0 = W(c + 0x1A4);
    const u32 a3 = f0 & 0xFFFFFFFDu;
    GRef<u32>(c + 0x1A4) = a3;
    const u32 u = Unit(GRef<u8>(view + 6));
    const u32 rot = u + 0x10;
    if ((f0 & 0x20u) == 0) {
        const u32 st = FollowState(view);
        if (GRef<s32>(st + 0x10) != -1) {
            const u32 mr = W(st);
            if ((W(mr + 0x5C) & 1u) != 0) {
                GRef<u32>(c + 0x1CC) = 0xB4;
                GRef<u32>(c + 0x1A4) = (a3 | 0x20u) & 0xFFFFFFEFu;
                GRef<u8>(c + 0x1AC) = GRef<u8>(mr + 0x59);
            } else {
                GRef<u32>(c + 0x1C8) = 0;
                GRef<u32>(c + 0x1A4) = a3 | 0x10u;
            }
        } else {
            GRef<u32>(c + 0x1A4) = a3 & 0xFFFFFFEFu;
        }
        c = Cam();
    }
    if (GRef<u8>(c + 0x1AC) == 0xFF) GRef<u32>(c + 0x1A4) = W(c + 0x1A4) & 0xFFFFFFDFu;
    const u32 f = W(c + 0x1A4);
    if ((f & 0x20u) != 0) {
        sceVu0CopyVector(sp, Unit(GRef<u8>(c + 0x1AC)));
        GRef<u8>(view + 9) = 0x18;
        c = Cam();
        if ((W(c + 0x1A4) & 0x200u) == 0) {
            GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x200u;
            GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
        }
    } else if ((f & 0x10u) != 0) {
        GRef<u8>(view + 9) = 0x18;
        c = Cam();
        if ((W(c + 0x1A4) & 0x200u) == 0) {
            GRef<u32>(c + 0x1A4) = W(c + 0x1A4) | 0x200u;
            GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
        }
        sceVu0CopyVector(sp, W(FollowState(view)));
    } else {
        GRef<u8>(view + 9) = 0x11;
        c = Cam();
        if ((W(c + 0x1A4) & 0x200u) != 0) {
            GRef<u32>(c + 0x1A4) = (W(c + 0x1A4) & 0xFFFFFDFFu) | 5u;
            GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
        }
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, rot);
        GRef<f32>(sp + 4) = -5.0f;
        GRef<f32>(sp + 8) = 15.0f;
        GRef<u32>(sp) = 0;
        GRef<u32>(sp + 0xC) = 0;
        sceVu0ApplyMatrix(sp, m, sp);
        sceVu0AddVector(sp, sp, u);
    }
    func_0010D680(view, sp);
    sceVu0CopyVector(Cam() + 0x80, sp);
    sceVu0SubVector(Cam() + 0x120, sp, Cam() + 0x110);
    sceVu0CopyVector(Cam() + 0x110, sp);
    c = Cam();
    if ((W(c + 0x1A4) & 4u) != 0) {
        sceVu0CopyVector(sp, u);
        const f32 k = F(0x0040EA9C);
        const f32 s = GetVectorLength(Cam() + 0xC0) * 60.0f;
        sceVu0Normalize(dir, Cam() + 0xC0);
        sceVu0ScaleVector(dir, dir, s);
        sceVu0AddVector(Cam() + 0xE0, sp, dir);
        GRef<f32>(Cam() + 0xEC) = 1.0f;
        ZeroQ(off);
        GRef<f32>(off + 4) = 64.0f;
        ee::rand();  // drawn and discarded, as the original
        sceVu0UnitMatrix(m2);
        const s32 r = ee::rand() % 360;
        sceVu0RotMatrixZ(m2, m2, fpu::IntToFloat(r - 180) * k / 180.0f);
        func_00102D28(m2, m2, rot);
        sceVu0ApplyMatrix(off, m2, off);
        sceVu0AddVector(Cam() + 0xE0, Cam() + 0xE0, off);
        sceVu0ScaleVector(Cam() + 0xD0, Cam() + 0xC0, F(0x0040EAA0));
        c = Cam();
        GRef<f32>(c + 0x188) = F(0x0040EAA4);
        GRef<f32>(c + 0xDC) = 1.0f;
        GRef<u32>(c + 0x1CC) = 0x12C;
        GRef<u32>(c + 0x1A4) = (W(c + 0x1A4) | 1u) & 0xFFFFFFCFu;
        GRef<u8>(view + 9) = static_cast<u8>(GRef<u8>(view + 9) | 0x80);
    } else {
        sceVu0AddVector(c + 0xE0, c + 0xE0, c + 0xD0);
    }
    const f32 d = func_00104F10(Cam() + 0xE0, u);
    if (d < 128.0f) {
        sceVu0SubVector(dir, Cam() + 0xE0, u);
        sceVu0ScaleVector(dir, dir, fpu::Sub(128.0f, d) * F(0x0040EAA8));
        sceVu0AddVector(Cam() + 0xE0, Cam() + 0xE0, dir);
    }
    const f32 h = fpu::Sub(func_0013E2B0(Terrain(), Cam() + 0xE0), 256.0f);
    if (h < F(Cam() + 0xE4)) GRef<f32>(Cam() + 0xE4) = h;
    sceVu0CopyVector(ang, rot);
    GRef<u32>(ang + 8) = 0;
    func_0010D5F0(view, Cam() + 0xE0, ang);
    sceVu0CopyVector(Cam() + 0x70, Cam() + 0xE0);
    f32 dist = func_00104F10(sp, u) * 4.0f;
    if (dist < 2048.0f) dist = 2048.0f;
    if (F(Cam() + 0x1BC) < dist) dist = F(Cam() + 0x1BC);
    SetViewDistance(view, dist);
}
AC04_GUEST_FUNCTION(0x00173A98, func_00173A98);

}  // namespace ac04::game
