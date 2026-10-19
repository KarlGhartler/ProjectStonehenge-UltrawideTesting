// Replay playback (slice S5j; docs/subsystems/mission-flow.md "Replay and demo"): entering and
// leaving the playback, the per-frame record player (units, weapons, effects, sounds), the replay
// effect slots and the per-player target-lock record.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Eff() { return W(0x004106B4); }  // effect block

}  // namespace

// Replay playback (scene 2 subs 0x10-0x12). Recorder R = *0x00410AF8: +0 u16 flags (1 restart this
// frame, 2 end reached, 0x10 start marker seen), +2 u16 kind (what follows the playback, set by
// 0x0016E010), +4 ring start, +8 write pointer, +0xC read pointer, +0x10 playback start, +0x14
// frames recorded, +0x18 loops played, +0x1C ring size. Ring words: 0x01234567 frame end (the next
// frame overwrites the following 0x76544567, so 0x76544567 survives only after the last frame),
// 0x76543210 wrap to the ring start, 0x12344321 start marker; records start with a type byte (0
// unit, 1 weapon, 2 effect, 3 sound).

namespace {

u32 Rec() { return W(0x00410AF8); }

}  // namespace

// EE: 0x0016E910
// Writes a start marker (0x12344321, then 0x76544567) into the ring.
void func_0016E910() {
    u32 w = func_0016DF88(8);
    GRef<u32>(w) = 0x12344321u;
    w += 4;
    GRef<u32>(w) = 0x76544567u;
    func_0016E000(w);
}

// EE: 0x0016E950
// Playback start n frames before the end: finds the end marker 0x76544567 from the ring start;
// the oldest frame is the first word after a frame-end marker 0x01234567 following it (wrap marker
// 0x76543210 -> ring start + 4; past the ring -> ring start). With n == 0 that oldest frame; else
// walks back from the end counting frame-end markers (from the ring start it continues before the
// wrap marker at the ring's end) and returns the word after the n-th, or the oldest frame when
// there are fewer.
u32 func_0016E950(s32 n) {
    const u32 r = Rec();
    u32 a = W(r + 4);
    while (W(a) != 0x76544567u) a += 4;
    const u32 end = a;
    const u32 start = W(r + 4), limit = start + W(r + 0x1C);
    for (;;) {
        if (!(a < limit)) {
            a = start;
            break;
        }
        const u32 v = W(a);
        if (v == 0x01234567u) {
            a += 4;
            break;
        }
        if (v == 0x76543210u) a = start;
        a += 4;
    }
    const u32 oldest = a;
    if (n == 0) return a;
    s32 count = 0;
    a = end - 4;
    for (;;) {
        if (a == start) {
            if (a == oldest) return a;
            a = a + W(r + 0x1C) - 4;
            if (W(a) != 0x76543210u) {
                do {
                    a -= 4;
                } while (W(a) != 0x76543210u);
            }
        }
        if (a == end) return oldest;
        if (a == oldest) return a;
        if (W(a) == 0x01234567u) {
            ++count;
            if (n == count) return a + 4;
        }
        a -= 4;
    }
}

// EE: 0x0016EA90
// Dead (no reference): skips `frames` frames of the replay ring from `a`: follows wrap markers
// (0x76543210 -> recorder +4) and start markers (0x12344321), counts frame ends (0x01234567; the
// end marker 0x76544567 counts as one and continues at the playback start +0x10) and steps over
// the records with the record players in skip mode (types 0-3; others one word). Returns the
// position reached.
u32 func_0016EA90(u32 a, s32 frames) {
    if (frames <= 0) return a;
    s32 n = 1;
    for (;;) {
        const u32 v = W(a);
        if (v == 0x76543210u) {
            a = W(Rec() + 4);
            continue;
        }
        if (v == 0x12344321u) {
            a += 4;
            continue;
        }
        if (v == 0x01234567u) {
            a += 4;
        } else if (v == 0x76544567u) {
            a = W(Rec() + 0x10);
        } else {
            switch (GRef<u8>(a)) {
                case 0: a = func_0016ED08(a, 1); break;
                case 1: a = func_0016FF40(a, 1); break;
                case 2: a = func_00170520(a, 1); break;
                case 3: a = func_00170F98(a, 1); break;
                default: a += 4; break;
            }
            continue;
        }
        if (!(n < frames)) return a;
        ++n;
    }
}

// EE: 0x0016ECC8
void func_0016ECC8() {
    func_00135E10();
    func_00135FE0();
}

// EE: 0x0016ECE8
void func_0016ECE8() {
    func_00135EF8();
    func_00136240();
}

// EE: 0x0016FF10
// Swaps the projectile block *0x004106AC with the replay one *0x00410AFC, then resets projectiles.
void func_0016FF10() {
    const u32 a = W(0x00410AFC), b = W(0x004106AC);
    GRef<u32>(0x004106AC) = a;
    GRef<u32>(0x00410AFC) = b;
    ResetProjectiles();
}

// EE: 0x0016FF28
void func_0016FF28() {
    const u32 a = W(0x004106AC), b = W(0x00410AFC);
    GRef<u32>(0x00410AFC) = a;
    GRef<u32>(0x004106AC) = b;
}

// EE: 0x001704A0
// Swaps the effect block *0x004106B4 with the replay one *0x00410B00, then InitEffects.
void func_001704A0() {
    const u32 a = W(0x00410B00), b = Eff();
    const u32 n = W(Scene() + 0x170C);
    GRef<u32>(0x00410B00) = b;
    GRef<u32>(0x004106B4) = a;
    InitEffects(n);
}

// EE: 0x001704C0
void func_001704C0() {
    const u32 a = Eff(), b = W(0x00410B00);
    GRef<u32>(0x00410B00) = a;
    GRef<u32>(0x004106B4) = b;
}

// EE: 0x001704D8
// InitEffects, then the 16 replay effect slot flags *0x00410B04 cleared.
void func_001704D8() {
    InitEffects(W(Scene() + 0x170C));
    for (u32 k = 0; k < 0x10; ++k) GRef<u8>(W(0x00410B04) + k) = 0;
}

// EE: 0x00170D48
// Ages the 16 replay effect slot flags: a slot with bit 0 set loses bit 1 when it has it, else
// bit 0.
void func_00170D48() {
    for (u32 k = 0; k < 0x10; ++k) {
        const u32 a = W(0x00410B04) + k;
        const u8 v = GRef<u8>(a);
        if ((v & 1) == 0) continue;
        GRef<u8>(a) = (v & 2) != 0 ? static_cast<u8>(v & 0xFD) : static_cast<u8>(v & 0xFE);
    }
}

// EE: 0x00170518
void func_00170518() { func_00170D48(); }

// EE: 0x00170F68
// Sound reset for a replay (re)start.
void func_00170F68() {
    func_00216C58();
    func_00216CA0();
    func_00219260();
    func_0021AEE0();
}

// EE: 0x0016E010
// Enters replay playback: heap mark, unit and player snapshots, replay projectile and effect
// blocks, replay camera (0x00175D08), lock record, projectile-follow states, sounds stopped (all
// players' loop sounds, voices); in mission kinds 1 / 2 without status flags 0x10000000 / 1 the VAG
// stream 0 (0x0021E798(0, 0)). Kind R+2: 0 when scene flag 0x10000 (+0x1668), else 5 (game[0x29]
// == 4), 4 (game[0x29] == 3 with status 0x80), 3 (status 0x10000000), 1 (status 1), 2 (status
// 0x80), 6 (mission kind 2), else 0.
void func_0016E010() {
    PushWorkHeapMark();
    func_0016ECC8();
    func_0016FF10();
    func_001704A0();
    func_00175D08();
    func_00176918();
    func_00136570(0);
    func_00136570(1);
    func_00219260();
    func_0021AEE0();
    func_0021CBB8();
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) func_001279B0(p, 0);
    StopAllVoices();
    if (static_cast<u32>(Game(0x29) - 1) < 2 && (~StatusFlags() & 0x10000001u) == 0x10000001u) func_0021E798(0, 0);
    u16 kind = 0;
    if ((W(Scene() + 0x1668) & 0x10000u) == 0) {
        const u8 g = Game(0x29);
        const u32 f = StatusFlags();
        if (g == 4) kind = 5;
        else if (g == 3 && (f & 0x80u) != 0) kind = 4;
        else if ((StatusFlags() & 0x10000000u) != 0) kind = 3;
        else if ((StatusFlags() & 1u) != 0) kind = 1;
        else if ((StatusFlags() & 0x80u) != 0) kind = 2;
        else if (GRef<s8>(Scene() + 0x1653) == 2) kind = 6;
    }
    GRef<u16>(Rec() + 2) = kind;
}

// EE: 0x0016E1D0
// Leaves replay playback: replay HUD reset, snapshots restored, blocks swapped back, heap mark
// popped; by kind: 0 -> sounds, player 0 loop sounds and VAG stream back, main-context flags
// (flags & ~0x800) | 0x200; 1, 2, 4-6 -> sound and voices stopped; 3 -> sounds stopped and player 0
// loop sounds back (jump table 0x00404FA0). Mission timer +0x1660 = *0x004117D4 - 1.
void func_0016E1D0() {
    func_00176058();
    func_0016ECE8();
    func_0016FF28();
    func_001704C0();
    PopWorkHeapMark();
    switch (GRef<u16>(Rec() + 2)) {
    case 0:
        func_0021CBB8();
        func_001279B0(0, 1);
        func_0021E798(0, 1);
        Ctx()->flags = (Ctx()->flags & 0xFFFFF7FFu) | 0x200u;
        break;
    case 1: case 2: case 4: case 5: case 6:
        func_00219260();
        func_0021AEE0();
        StopAllVoices();
        break;
    case 3:
        func_00219260();
        func_0021AEE0();
        func_0021CBB8();
        func_001279B0(0, 1);
        break;
    default:
        break;
    }
    GRef<s32>(Scene() + 0x1660) = GRef<s32>(0x004117D4) - 1;
}

namespace {

// The reset done at every playback restart (start marker, loop).
void ReplayRestartReset(bool locks) {
    if (locks) func_00176940();
    func_00175FF0();
    func_00136570(0);
    func_00136570(1);
}

}  // namespace

// EE: 0x0016E2D0
// Replay playback, scene 2 subs 0x10-0x12. Sub 0x10: fade in (0x0010A130(0, 0.5, 0), +0x1670 =
// 1.0), sub 0x11; playback start 600 frames before the end (kind 2), 900 (kind 0) or the oldest
// frame (0x0016E950), R+0 = 1; the frames up to the end are counted into *0x004117D4 (game[0x29]
// == 4: timer +0x1660 = 0) or the timer goes back by them; *0x004117D8 = timer. Sub 0x12: what
// follows by kind (jump table 0x00404FC0): 0 scene flag 0x10000 cleared and sub 0xB, 1 / 6 scene
// 5, 2 sub 0x1C, 3 scene 5 with status 0x20000000 else sub 0x1D, 4 EnterMenu(0x1C), 5
// EnterMenu(0). Every frame: on a restart (R+0 bit 0) the timer, locks, camera cut, follow states,
// projectiles, effects, target references and sounds are reset (unit list +0x60 cleared); on a
// start marker seen (bit 0x10) the same without timer, locks and sounds. Unit flags 0x31000200
// cleared, then the frame's records applied (0x0016ED08, 0x0016FF40, 0x00170520, 0x00170F98) up to
// its frame-end marker; effect slots aged, follow states updated (0x001365A0). Wrap / start marker /
// end marker handling (end: R+0 |= 2, read pointer back to the start; next frame restarts with R+0
// |= 1, loop count +1). Demo (game[0x29] == 4): fade out after the first loop. Start of the
// watched player: fade out and the replay HUD off (0x0017CC80). Sub 0x11 with the fade at 1.0 ->
// sub 0x12.
void UpdateReplayPlayback() {
    MainContext* ctx = Ctx();
    u32 r = Rec();
    if (ctx->sub == 0x10) {
        GRef<f32>(Scene() + 0x1670) = 1.0f;
        func_0010A130(0.0f, 0.5f, 0);
        RequestSubState(0x11);
        r = Rec();
        const u16 kind = GRef<u16>(r + 2);
        GRef<u32>(r + 0x18) = 0;
        s32 back = 0;
        if (kind == 2) back = 0x258;
        else if (kind == 0) back = 0x384;
        GRef<u32>(Rec() + 0x10) = func_0016E950(back);
        r = Rec();
        const u32 start = W(r + 0x10);
        GRef<u16>(r) = 1;
        s32 frames = 0;
        u32 a = start;
        GRef<u32>(r + 0xC) = start;
        while (a != W(r + 8)) {
            const u32 v = W(a);
            if (v == 0x01234567u) {
                ++frames;
            } else if (v == 0x76543210u) {
                a = W(r + 4);
            } else if (v == 0x76544567u) {
                break;
            }
            a += 4;
        }
        const u32 S = Scene();
        if (Game(0x29) == 4) {
            GRef<s32>(0x004117D4) = frames;
            GRef<s32>(S + 0x1660) = 0;
        } else {
            const s32 t = GRef<s32>(S + 0x1660);
            GRef<s32>(0x004117D4) = t;
            GRef<s32>(S + 0x1660) = t - frames;
        }
        GRef<s32>(0x004117D8) = GRef<s32>(S + 0x1660);
    }
    if (ctx->sub == 0x12) {
        switch (GRef<u16>(r + 2)) {
        case 0:
            GRef<u32>(Scene() + 0x1668) &= 0xFFFEFFFFu;
            RequestSubState(0xB);
            break;
        case 1: RequestScene(5); break;
        case 2: RequestSubState(0x1C); break;
        case 3:
            if ((StatusFlags() & 0x20000000u) != 0) RequestScene(5);
            else RequestSubState(0x1D);
            break;
        case 4: EnterMenu(0x1C); break;
        case 5: EnterMenu(0); break;
        case 6: RequestScene(5); break;
        default: break;
        }
        r = Rec();
    }
    const u16 f = GRef<u16>(r);
    if ((f & 1) != 0) {
        GRef<s32>(Scene() + 0x1660) = GRef<s32>(0x004117D8);
        ReplayRestartReset(true);
        ResetProjectiles();
        func_001704D8();
        func_0011D888();
        func_00170F68();
        ZeroQ(UnitList() + 0x60);
    } else if ((f & 0x10) != 0) {
        GRef<u16>(r) = static_cast<u16>(f & 0xFFEF);
        ReplayRestartReset(false);
        func_001704D8();
        ResetProjectiles();
        func_0011D888();
    }
    func_001769C0();
    for (s32 i = 0; i < UnitCount(); ++i) GRef<u32>(Unit(i) + 0x4C) = W(Unit(i) + 0x4C) & 0xCEFFFDFFu;
    for (;;) {
        r = Rec();
        const u32 a = W(r + 0xC);
        const u32 v = W(a);
        if (v == 0x76543210u) {
            GRef<u32>(r + 0xC) = W(r + 4);
            continue;
        }
        if (v == 0x12344321u) {
            ReplayRestartReset(false);
            func_001704D8();
            ResetProjectiles();
            func_0011D888();
            GRef<u32>(Rec() + 0xC) = W(Rec() + 0xC) + 4;
            continue;
        }
        if (v == 0x01234567u) {
            GRef<u32>(r + 0xC) = a + 4;
            break;
        }
        if (v == 0x76544567u) break;
        switch (GRef<u8>(a)) {
        case 0: GRef<u32>(Rec() + 0xC) = func_0016ED08(a, 0); break;
        case 1: GRef<u32>(Rec() + 0xC) = func_0016FF40(a, 0); break;
        case 2: GRef<u32>(Rec() + 0xC) = func_00170520(a, 0); break;
        case 3: GRef<u32>(Rec() + 0xC) = func_00170F98(a, 0); break;
        default: GRef<u32>(r + 0xC) = W(r + 0xC) + 4; break;
        }
    }
    func_00170518();
    func_001365A0();
    r = Rec();
    if (W(W(r + 0xC)) == 0x76543210u) GRef<u32>(r + 0xC) = W(r + 4);
    const u32 a = W(r + 0xC);
    if (W(a) == 0x12344321u) {
        GRef<u32>(r + 0xC) = a + 4;
        GRef<u16>(r) = static_cast<u16>(GRef<u16>(r) | 0x10);
    }
    if (W(W(r + 0xC)) == 0x76544567u) {
        GRef<u32>(r + 0xC) = W(r + 0x10);
        GRef<u16>(r) = static_cast<u16>(GRef<u16>(r) | 2);
    }
    const u16 g = GRef<u16>(r);
    if ((g & 2) != 0) {
        GRef<u16>(r) = static_cast<u16>((g & 0xFFFD) | 1);
        GRef<s32>(r + 0x18) += 1;
    } else if ((g & 1) != 0) {
        GRef<u16>(r) = static_cast<u16>(g & 0xFFFE);
    }
    if (Game(0x29) == 4 && 0 < GRef<s32>(r + 0x18) && F(Scene() + 0x1670) == 0.0f) func_0010A130(1.0f, 0.5f, 0);
    s32 v = 0;
    if (Game(0x2D) < Game(0x25)) v = Game(0x2D);
    if ((Ctx()->pad_buttons[v][1] & 0x40) != 0 && F(Scene() + 0x1670) == 0.0f) {
        func_0010A130(1.0f, 0.5f, 0);
        func_0017CC80(W(0x00410B40));
    }
    if (Ctx()->sub == 0x11 && F(Scene() + 0x1670) == 1.0f) RequestSubState(0x12);
}

// EE: 0x0016ED08
// Applies one recorded unit record at rec (type 0, unit rec+1) unless `skip`; returns the next
// record. Players (unit < player count, 0x30 bytes): flags 0x30000200 (+ 0x1000000 when rec+0x2F
// bit 0), angles s16 / K 0x0040E980, velocity = new position - old (0 on a restart), position,
// w 1.0; lock record bytes (*0x00410B10 + 4 / 6 / 8 / 0xA + unit) = rec+0x14..+0x17; stick and
// throttle inputs (byte / 255) and the moving parts; for aircraft models 0x0C / 0x11 / 0x13 / 0x15
// the variable-geometry channels 0x20-0x22 and 4 or 6 (+0x172) channels from 0x34 / 0x3A / 0x3E;
// gear state and channels, gear lights, unit +0x56, trails; afterburner nodes 1/0-5 by rec+0x2E bits
// 0-5 (off when +0x173 == 8), nodes 0/0 and 0/1 by bits 7 / 6. Vehicles (kinds 8 / 9, 0x30 bytes)
// and other objects (0x1C bytes): flags as recorded (bit 0x80: not visible), vehicles also the
// +0x48 bits (0x770000 visible, kind 9 0x230000), angles / K 0x0040E984 (vehicles) or 0x0040E988,
// velocity, position, +0x5A, +0x56, the group updates for +0x54 == 1 / 2 (0x0011D580 / 0x0011D738),
// the 4 animation tracks when visible (floats / bytes / 255), vehicles +0x70 / +0x74.
// fpucheck: verified 9 (the three record paths share one velocity block of 3 sub)
u32 func_0016ED08(u32 rec, s32 skip) {
    const s32 idx = GRef<u8>(rec + 1);
    const auto motion = [rec](u32 u, f32 k) {
        GRef<f32>(u + 0x10) = fpu::IntToFloat(GRef<s16>(rec + 2)) / k;
        const u16 restart = GRef<u16>(Rec()) & 1;
        GRef<f32>(u + 0x14) = fpu::IntToFloat(GRef<s16>(rec + 4)) / k;
        GRef<u32>(u + 0x1C) = 0;
        GRef<f32>(u + 0x18) = fpu::IntToFloat(GRef<s16>(rec + 6)) / k;
        if (restart != 0) {
            ZeroQ(u + 0x20);
        } else {
            GRef<f32>(u + 0x20) = fpu::Sub(F(rec + 8), F(u));
            GRef<f32>(u + 0x24) = fpu::Sub(F(rec + 0xC), F(u + 4));
            GRef<u32>(u + 0x2C) = 0;
            GRef<f32>(u + 0x28) = fpu::Sub(F(rec + 0x10), F(u + 8));
        }
        GRef<f32>(u) = F(rec + 8);
        GRef<f32>(u + 4) = F(rec + 0xC);
        GRef<f32>(u + 0xC) = 1.0f;
        GRef<f32>(u + 8) = F(rec + 0x10);
    };
    const auto byte255 = [](u32 a) { return fpu::IntToFloat(GRef<u8>(a)) / 255.0f; };
    if (static_cast<u32>(idx) < Game(0x25)) {
        if (skip != 0) return rec + 0x30;
        const u32 u = Unit(idx);
        const u32 ac = W(u + 0x44);
        const u32 f = W(u + 0x4C) | 0x30000200u;
        GRef<u32>(u + 0x4C) = f;
        if ((GRef<u8>(rec + 0x2F) & 1) != 0) GRef<u32>(u + 0x4C) = f | 0x1000000u;
        motion(u, F(0x0040E980));
        const u32 locks = W(0x00410B10) + static_cast<u32>(idx);
        GRef<u8>(locks + 4) = GRef<u8>(rec + 0x14);
        GRef<u8>(W(0x00410B10) + static_cast<u32>(idx) + 6) = GRef<u8>(rec + 0x15);
        GRef<u8>(W(0x00410B10) + static_cast<u32>(idx) + 8) = GRef<u8>(rec + 0x16);
        GRef<u8>(W(0x00410B10) + static_cast<u32>(idx) + 0xA) = GRef<u8>(rec + 0x17);
        GRef<f32>(ac + 0x25C) = F(rec + 0x18);
        GRef<f32>(ac + 0x240) = byte255(rec + 0x1C);
        GRef<f32>(ac + 0x250) = byte255(rec + 0x1D);
        GRef<f32>(ac + 0x258) = byte255(rec + 0x1E);
        GRef<f32>(ac + 0x254) = byte255(rec + 0x1F);
        GRef<f32>(ac + 0x248) = byte255(rec + 0x20);
        GRef<f32>(ac + 0x24C) = byte255(rec + 0x21);
        GRef<f32>(ac + 0x244) = byte255(rec + 0x22);
        UpdateAircraftMovingParts(idx, 0);
        const u8 model = GRef<u8>(u + 0x69);
        if (model == 0x11 || model == 0xC || model == 0x13 || model == 0x15) {
            const f32 a = byte255(rec + 0x23);
            const f32 c = byte255(rec + 0x25);
            func_00149EB8(0x20, idx, a);
            func_00149EB8(0x21, idx, byte255(rec + 0x24));
            func_00149EB8(0x22, idx, c);
            const s8 t = GRef<s8>(ac + 0x172);
            s32 first = 0x34, n = 4;
            if (t == 1) {
                first = 0x3A;
                n = 6;
            } else if (t == 2) {
                first = 0x3E;
            }
            for (s32 k = 0; k < n; ++k) func_00149EB8(first + k, idx, c);
        }
        GRef<u8>(ac + 0x280) = GRef<u8>(rec + 0x28);
        GRef<f32>(ac + 0x260) = byte255(rec + 0x29);
        GRef<f32>(ac + 0x264) = byte255(rec + 0x2A);
        GRef<f32>(ac + 0x268) = byte255(rec + 0x2B);
        UpdateGearLights(idx);
        GRef<u16>(u + 0x56) = GRef<u16>(rec + 0x2C);
        UpdatePlayerTrails(idx);
        for (s32 k = 0; k < 6; ++k) {
            const bool on = ((GRef<u8>(rec + 0x2E) >> k) & 1) != 0 && GRef<s8>(ac + 0x173) != 8;
            func_0014AA38(W(0x004106A8), idx, 1, k, on ? 1u : 0u);
        }
        func_0014AA38(W(0x004106A8), idx, 0, 0, (GRef<u8>(rec + 0x2E) & 0x80) != 0 ? 1u : 0u);
        func_0014AA38(W(0x004106A8), idx, 0, 1, (GRef<u8>(rec + 0x2E) & 0x40) != 0 ? 1u : 0u);
        return rec + 0x30;
    }
    const u32 u = Unit(idx);
    const bool vehicle = static_cast<u32>(GRef<u8>(u + 0x6A) - 8) < 2;
    const u32 size = vehicle ? 0x30u : 0x1Cu;
    if (skip != 0) return rec + size;
    const u32 f = W(u + 0x4C) | 0x30000200u;
    GRef<u32>(u + 0x4C) = f;
    if ((GRef<u8>(rec + 0x15) & 1) != 0) GRef<u32>(u + 0x4C) = f | 0x1000000u;
    if ((GRef<u8>(rec + 0x15) & 0x80) != 0) GRef<u32>(u + 0x4C) = W(u + 0x4C) & 0xDFFFFFFFu;
    if (vehicle) {
        const u32 g = W(u + 0x48) & 0xFF8FFFFFu & 0xFFF8FFFFu;
        GRef<u32>(u + 0x48) = g;
        if ((W(u + 0x4C) & 0x20000000u) != 0) GRef<u32>(u + 0x48) = g | 0x700000u | 0x70000u;
        else if (GRef<u8>(Unit(idx) + 0x6A) == 9) GRef<u32>(u + 0x48) = g | 0x200000u | 0x30000u;
    }
    motion(u, vehicle ? F(0x0040E984) : F(0x0040E988));
    GRef<u8>(u + 0x5A) = GRef<u8>(rec + 0x14);
    GRef<u16>(u + 0x56) = GRef<u16>(rec + 0x16);
    if (GRef<u8>(u + 0x54) == 1) func_0011D580(GRef<u8>(u + 0x55));
    if (GRef<u8>(u + 0x54) == 2) func_0011D738(GRef<u8>(u + 0x55));
    if ((W(u + 0x4C) & 0x20000000u) != 0) {
        for (s32 t = 0; t < 4; ++t) {
            const f32 a = vehicle ? F(rec + 0x18 + static_cast<u32>(t) * 4) : byte255(rec + 0x18 + static_cast<u32>(t));
            func_00145D08(a, UnitModels(), GRef<u8>(u + 0x55), t);
        }
    }
    if (vehicle) {
        GRef<f32>(u + 0x70) = F(rec + 0x28);
        GRef<f32>(u + 0x74) = F(rec + 0x2C);
    }
    return rec + size;
}

// Replay effect slots *0x00410B04 (16): +0 flags (bit 0 used, bit 1 seen this frame), +0x10 kind,
// +0x20 owner (word), +0x60 effect handle (word).

namespace {

bool Recording() { return (W(Scene() + 0x1668) & 1u) != 0; }

u32 Slots() { return W(0x00410B04); }

void CopyWords(u32 dst, u32 src, u32 n) {
    for (u32 k = 0; k < n; ++k) GRef<u32>(dst + k * 4) = W(src + k * 4);
}

}  // namespace

// EE: 0x00170DA0
// The handle of the used slot for (kind, owner), marking it seen (bit 1); -1 when none.
s32 func_00170DA0(u32 kind, u32 owner) {
    kind &= 0xFF;
    owner &= 0xFF;
    for (u32 i = 0; i < 0x10; ++i) {
        const u8 f = GRef<u8>(Slots() + i);
        if ((f & 1) == 0) continue;
        if (GRef<u8>(Slots() + 0x10 + i) != kind) continue;
        if (W(Slots() + 0x20 + i * 4) != owner) continue;
        GRef<u8>(Slots() + i) = static_cast<u8>(f | 2);
        return GRef<s32>(Slots() + 0x60 + i * 4);
    }
    return -1;
}

// EE: 0x00170E28
// Takes the first free slot for (kind, owner) with an effect handle (flags 3).
void func_00170E28(u32 kind, u32 owner, u32 handle) {
    for (u32 i = 0; i < 0x10; ++i) {
        const u8 f = GRef<u8>(Slots() + i);
        if ((f & 1) != 0) continue;
        GRef<u8>(Slots() + i) = static_cast<u8>(f | 3);
        GRef<u8>(Slots() + 0x10 + i) = static_cast<u8>(kind);
        GRef<u32>(Slots() + 0x20 + i * 4) = owner & 0xFF;
        GRef<u32>(Slots() + 0x60 + i * 4) = handle & 0xFF;
        return;
    }
}

// EE: 0x00170AA0
// Records an effect event (type 2) with one vector, when recording: {2, kind, owner, extra, a}.
void func_00170AA0(u32 kind, u32 owner, u32 extra, u32 a) {
    if (!Recording()) return;
    const u32 r = func_0016DF88(0x14);
    GRef<u8>(r) = 2;
    GRef<u8>(r + 1) = static_cast<u8>(kind);
    GRef<u8>(r + 2) = static_cast<u8>(owner);
    GRef<u8>(r + 3) = static_cast<u8>(extra);
    CopyWords(r + 4, a, 4);
    func_0016E000(r + 0x14);
}

// EE: 0x00170B60
// Records an effect event with two vectors (0x24 bytes).
void func_00170B60(u32 kind, u32 owner, u32 extra, u32 a, u32 b) {
    if (!Recording()) return;
    const u32 r = func_0016DF88(0x24);
    GRef<u8>(r) = 2;
    GRef<u8>(r + 1) = static_cast<u8>(kind);
    GRef<u8>(r + 2) = static_cast<u8>(owner);
    GRef<u8>(r + 3) = static_cast<u8>(extra);
    for (u32 k = 0; k < 4; ++k) {
        GRef<u32>(r + 4 + k * 4) = W(a + k * 4);
        GRef<u32>(r + 0x14 + k * 4) = W(b + k * 4);
    }
    func_0016E000(r + 0x24);
}

// EE: 0x00170C40
// Records an effect event with three vectors (0x34 bytes).
void func_00170C40(u32 kind, u32 owner, u32 extra, u32 a, u32 b, u32 c) {
    if (!Recording()) return;
    const u32 r = func_0016DF88(0x34);
    GRef<u8>(r) = 2;
    GRef<u8>(r + 1) = static_cast<u8>(kind);
    GRef<u8>(r + 2) = static_cast<u8>(owner);
    GRef<u8>(r + 3) = static_cast<u8>(extra);
    for (u32 k = 0; k < 4; ++k) {
        GRef<u32>(r + 4 + k * 4) = W(a + k * 4);
        GRef<u32>(r + 0x14 + k * 4) = W(b + k * 4);
        GRef<u32>(r + 0x24 + k * 4) = W(c + k * 4);
    }
    func_0016E000(r + 0x34);
}

// EE: 0x00170598
// Plays a one-vector effect event (kinds 5, 7, 8, 13, 15: 0x001D32A0, 0x00188A50, 0x001828D0,
// 0x001979F0(v, extra), 0x0018EBE8(extra, v); jump table 0x00405040); a new (kind, owner) pair
// (owner != 0xFF) gets a slot. Returns rec + 0x14.
u32 func_00170598(u32 rec, s32 skip) {
    if (skip != 0) return rec + 0x14;
    guest::GuestFrame frame(0x10);
    const u32 v = frame.Address();
    const u32 owner = GRef<u8>(rec + 2), kind = GRef<u8>(rec + 1);
    CopyWords(v, rec + 4, 4);
    s32 found = 0;
    if (owner != 0xFF) found = func_00170DA0(kind, GRef<u8>(rec + 2));
    switch (GRef<u8>(rec + 1)) {
    case 5: func_001D32A0(v); break;
    case 7: func_00188A50(v); break;
    case 8: func_001828D0(v); break;
    case 13: func_001979F0(v, GRef<u8>(rec + 3)); break;
    case 15: func_0018EBE8(GRef<u8>(rec + 3), v); break;
    default: break;
    }
    if (found == -1) func_00170E28(GRef<u8>(rec + 1), GRef<u8>(rec + 2), 0);
    return rec + 0x14;
}

// EE: 0x001706D0
// Plays a two-vector effect event (a = rec+4, b = rec+0x14; jump table 0x00405070): 2 0x001E94F8(a,
// b, extra), 3 0x001F08E8(a, b, explosion id of unit `extra`), 6 0x001BFEF0, 10 0x001CF2E8, 11
// 0x001FF6C0(slot handle, a, b, extra) -> new handle, 12 0x001CD360, 14 0x00181BB0(slot handle,
// a, b, extra & 7, extra >> 4) -> new handle, 17 0x0020C2A0(b.w, a, b with w = 0, extra). A new
// (kind, owner) pair gets a slot with the new handle. Returns rec + 0x24.
u32 func_001706D0(u32 rec, s32 skip) {
    if (skip != 0) return rec + 0x24;
    guest::GuestFrame frame(0x20);
    const u32 a = frame.Address(), b = a + 0x10;
    const u32 owner = GRef<u8>(rec + 2);
    for (u32 k = 0; k < 4; ++k) {
        GRef<u32>(a + k * 4) = W(rec + 4 + k * 4);
        GRef<u32>(b + k * 4) = W(rec + 0x14 + k * 4);
    }
    s32 found = 0;
    u32 handle = 0;
    if (owner != 0xFF) found = func_00170DA0(GRef<u8>(rec + 1), GRef<u8>(rec + 2));
    const u32 extra = GRef<u8>(rec + 3);
    switch (GRef<u8>(rec + 1)) {
    case 2: func_001E94F8(a, b, static_cast<s32>(extra)); break;
    case 3: {
        const u32 u = Unit(static_cast<s32>(extra));
        func_001F08E8(a, b, static_cast<s32>(func_0011E5A8(GRef<u8>(u + 0x9A), GRef<u8>(u + 0x54))));
        break;
    }
    case 6: func_001BFEF0(a, b); break;
    case 10: func_001CF2E8(a, b); break;
    case 11: handle = func_001FF6C0(static_cast<u32>(found), a, b, static_cast<s32>(extra)); break;
    case 12: func_001CD360(a, b); break;
    case 14: handle = func_00181BB0(static_cast<u32>(found), a, b, static_cast<s32>(extra & 7), static_cast<s32>(extra >> 4)); break;
    case 17: {
        const f32 k = F(b + 0xC);
        GRef<u32>(b + 0xC) = 0;
        func_0020C2A0(k, a, b, static_cast<s32>(extra));
        break;
    }
    default: break;
    }
    if (found == -1) func_00170E28(GRef<u8>(rec + 1), GRef<u8>(rec + 2), handle & 0xFF);
    return rec + 0x24;
}

// EE: 0x001708D0
// Plays a three-vector effect event (a, b, c at rec+4 / +0x14 / +0x24): 1 0x001E9518(a, b, c,
// extra), 4 0x001F0910(a, b, c, explosion id of unit `extra`), 9 0x0019F230, 16 SpawnImpactEffect
// (0x00154558). A new (kind, owner) pair gets a slot. Returns rec + 0x34.
u32 func_001708D0(u32 rec, s32 skip) {
    if (skip != 0) return rec + 0x34;
    guest::GuestFrame frame(0x30);
    const u32 a = frame.Address(), b = a + 0x10, c = a + 0x20;
    for (u32 k = 0; k < 4; ++k) {
        GRef<u32>(a + k * 4) = W(rec + 4 + k * 4);
        GRef<u32>(b + k * 4) = W(rec + 0x14 + k * 4);
        GRef<u32>(c + k * 4) = W(rec + 0x24 + k * 4);
    }
    s32 found = 0;
    if (GRef<u8>(rec + 2) != 0xFF) found = func_00170DA0(GRef<u8>(rec + 1), GRef<u8>(rec + 2));
    const u32 extra = GRef<u8>(rec + 3);
    switch (GRef<u8>(rec + 1)) {
    case 1: func_001E9518(a, b, c, static_cast<s32>(extra)); break;
    case 4: {
        const u32 u = Unit(static_cast<s32>(extra));
        func_001F0910(a, b, c, func_0011E5A8(GRef<u8>(u + 0x9A), GRef<u8>(u + 0x54)));
        break;
    }
    case 9: func_0019F230(a, b, c, static_cast<s32>(extra)); break;
    case 16: SpawnImpactEffect(a, b, c, extra); break;
    default: break;
    }
    if (found == -1) func_00170E28(GRef<u8>(rec + 1), GRef<u8>(rec + 2), 0);
    return rec + 0x34;
}

// EE: 0x00170520
// Plays an effect event record by kind (jump table 0x00404FF0): 5, 7, 8, 13, 15 one vector; 2, 3,
// 6, 10-12, 14, 17 two; 1, 4, 9, 16 three. Unknown kinds: rec + 0x34.
u32 func_00170520(u32 rec, s32 skip) {
    switch (GRef<u8>(rec + 1)) {
    case 5: case 7: case 8: case 13: case 15: return func_00170598(rec, skip);
    case 2: case 3: case 6: case 10: case 11: case 12: case 14: case 17: return func_001706D0(rec, skip);
    case 1: case 4: case 9: case 16: return func_001708D0(rec, skip);
    default: return rec + 0x34;
    }
}

// EE: 0x00170240
// Records a weapon event (type 1, 0x28 bytes) when recording: +1 kind (0xFF missile, 0xFE special
// weapon, 0xFD-0xFA other launchers, else a weapon id), +2 target, +3 owner, +4 param, +5 slot,
// rotation wrapped once in place (K 0x0040E9FC / 0x0040EA04 bounds) and stored as s16 x K
// 0x0040EA1C (+6 x, +8 y, +0xA z), position +0xC, velocity +0x18, +0x24 = f.
// fpucheck: verified 6 (one wrap helper for the three angles: 3 sub, 3 add)
void func_00170240(f32 f, u32 slot, u32 kind, u32 owner, u32 target, u32 pos, u32 rot, u32 vel, u32 param) {
    if (!Recording()) return;
    const u32 r = func_0016DF88(0x28);
    const f32 hi = F(0x0040E9FC), lo = F(0x0040EA04);
    GRef<u8>(r + 5) = static_cast<u8>(slot);
    GRef<u8>(r) = 1;
    GRef<u8>(r + 1) = static_cast<u8>(kind);
    GRef<u8>(r + 4) = static_cast<u8>(param);
    const auto wrap = [&](u32 a, u32 sub, u32 add) {
        f32 v = F(a);
        if (hi < v) {
            v = fpu::Sub(v, F(sub));
            GRef<f32>(a) = v;
        }
        if (v < lo) {
            v = fpu::Add(v, F(add));
            GRef<f32>(a) = v;
        }
        return v;
    };
    const f32 x = wrap(rot, 0x0040EA00, 0x0040EA08);
    const f32 y = wrap(rot + 4, 0x0040EA0C, 0x0040EA10);
    const f32 z = wrap(rot + 8, 0x0040EA14, 0x0040EA18);
    const f32 k = F(0x0040EA1C);
    GRef<f32>(r + 0xC) = F(pos);
    GRef<f32>(r + 0x10) = F(pos + 4);
    GRef<f32>(r + 0x14) = F(pos + 8);
    GRef<s16>(r + 0xA) = static_cast<s16>(fpu::FloatToInt(z * k));
    GRef<s16>(r + 6) = static_cast<s16>(fpu::FloatToInt(x * k));
    GRef<s16>(r + 8) = static_cast<s16>(fpu::FloatToInt(y * k));
    GRef<f32>(r + 0x18) = F(vel);
    GRef<f32>(r + 0x1C) = F(vel + 4);
    GRef<f32>(r + 0x20) = F(vel + 8);
    GRef<u8>(r + 3) = static_cast<u8>(owner);
    GRef<u8>(r + 2) = static_cast<u8>(target);
    GRef<f32>(r + 0x24) = f;
    func_0016E000(r + 0x28);
}

// EE: 0x0016FF40
// Plays a weapon event record (see 0x00170240) unless `skip`: position (w 1.0), rotation (s16 / K
// 0x0040E9F8, w 0), velocity (w 0); by kind: 0xFF 0x0014CAE0, 0xFE 0x0014D990, 0xFD 0x0014DEB8,
// 0xFC 0x0014E310(+0x24), 0xFB 0x0014F398, 0xFA 0x0014EC28, else 0x0014FB88 (weapon id). Special
// weapon (0xFE): a player owner gets the primary lock (0x00176AA8), the camera follow
// (FollowProjectile(owner, 0, slot)) and launch sound 0xF (0x0021AE30; the original's remap of
// ids 9 / 0xB-0xD to 0x10 never applies); a player target the secondary lock (0x00176B30). Other
// weapons (kind < 0xFA, not 0x10) from a player: FollowProjectile(owner, 1, slot) and the launch
// sound of the weapon id. Returns rec + 0x28.
u32 func_0016FF40(u32 rec, s32 skip) {
    if (skip != 0) return rec + 0x28;
    guest::GuestFrame frame(0x50);
    const u32 pos = frame.Address(), rot = pos + 0x10, vel = pos + 0x20, ev = pos + 0x30;
    const f32 k = F(0x0040E9F8);
    GRef<f32>(rot) = fpu::IntToFloat(GRef<s16>(rec + 6)) / k;
    GRef<f32>(rot + 4) = fpu::IntToFloat(GRef<s16>(rec + 8)) / k;
    GRef<f32>(rot + 8) = fpu::IntToFloat(GRef<s16>(rec + 0xA)) / k;
    GRef<f32>(pos) = F(rec + 0xC);
    GRef<f32>(pos + 4) = F(rec + 0x10);
    GRef<f32>(pos + 8) = F(rec + 0x14);
    GRef<f32>(pos + 0xC) = 1.0f;
    GRef<f32>(vel) = F(rec + 0x18);
    GRef<f32>(vel + 4) = F(rec + 0x1C);
    GRef<f32>(vel + 8) = F(rec + 0x20);
    GRef<u32>(rot + 0xC) = 0;
    GRef<u32>(vel + 0xC) = 0;
    const u8 kind = GRef<u8>(rec + 1);
    const u8 slot = GRef<u8>(rec + 5), owner = GRef<u8>(rec + 3), target = GRef<u8>(rec + 2), param = GRef<u8>(rec + 4);
    switch (kind) {
    case 0xFF: func_0014CAE0(slot, owner, pos, rot, vel); break;
    case 0xFE: func_0014D990(slot, owner, target, pos, rot, vel); break;
    case 0xFD: func_0014DEB8(slot, owner, pos); break;
    case 0xFC: func_0014E310(F(rec + 0x24), slot, owner, pos, vel, param); break;
    case 0xFB: func_0014F398(slot, owner, target, pos, rot, param); break;
    case 0xFA: func_0014EC28(slot, owner, pos, vel); break;
    default: func_0014FB88(slot, kind, owner, target, pos, rot, vel, param); break;
    }
    const auto launch_sound = [&](s32 id) {
        GRef<s32>(ev + 8) = 2;
        GRef<u32>(ev) = GRef<u8>(rec + 3);
        GRef<s32>(ev + 4) = id;
        CopyQ(ev + 0x10, pos);
        func_0021AE30(ev);
    };
    if (kind == 0xFE) {
        if (GRef<u8>(rec + 3) < 2) {
            func_00176AA8(GRef<u8>(rec + 3), GRef<u8>(rec + 5));
            FollowProjectile(GRef<u8>(rec + 3), 0, GRef<u8>(rec + 5));
            launch_sound(0xF);  // the original then remaps ids 9 / 0xB-0xD to 0x10: never for 0xF
        }
        if (GRef<u8>(rec + 2) < 2) func_00176B30(GRef<u8>(rec + 2), GRef<u8>(rec + 5));
        return rec + 0x28;
    }
    if (kind < 0xFA && kind != 0x10 && GRef<u8>(rec + 3) < 2) {
        FollowProjectile(GRef<u8>(rec + 3), 1, GRef<u8>(rec + 5));
        launch_sound(GRef<u8>(rec + 1));
    }
    return rec + 0x28;
}

// EE: 0x00170F98
// Plays a sound event record (type 3) unless `skip`: kind 1 (4 bytes) resets the positional sound
// table (0x00216CA0); kind 2 (0x24 bytes) fills positional slot rec+2 (0x004206A0 + 0x30 slot: +0
// sound rec+3, +0x20 position rec+4, +0x10 vector rec+0x14) and starts it (0x00216CE0); kind 3
// (0x14 bytes) PlayEventSe(rec+3, position); kind 4 (0x14 bytes) the fixed sound rec+3
// (0x00219668), except in mission kind 2 where it plays only for the watched player (rec+2), else
// sounds 0 / 1 become PlayEventSe 0 / 1. Other kinds: 4 bytes.
u32 func_00170F98(u32 rec, s32 skip) {
    const u8 kind = GRef<u8>(rec + 1);
    if (kind == 2) {
        if (skip != 0) return rec + 0x24;
        const u32 e = 0x004206A0 + static_cast<u32>(GRef<u8>(rec + 2)) * 0x30;
        GRef<f32>(e + 0x20) = F(rec + 4);
        GRef<u32>(e) = GRef<u8>(rec + 3);
        GRef<f32>(e + 0x24) = F(rec + 8);
        GRef<f32>(e + 0x28) = F(rec + 0xC);
        GRef<f32>(e + 0x2C) = F(rec + 0x10);
        GRef<f32>(e + 0x10) = F(rec + 0x14);
        GRef<f32>(e + 0x14) = F(rec + 0x18);
        GRef<f32>(e + 0x18) = F(rec + 0x1C);
        GRef<f32>(e + 0x1C) = F(rec + 0x20);
        func_00216CE0(e);
        return rec + 0x24;
    }
    if (kind == 1) {
        if (skip == 0) func_00216CA0();
        return rec + 4;
    }
    if (kind != 3 && kind != 4) return rec + 4;
    if (skip != 0) return rec + 0x14;
    guest::GuestFrame frame(0x10);
    const u32 v = frame.Address();
    GRef<f32>(v) = F(rec + 4);
    GRef<f32>(v + 4) = F(rec + 8);
    GRef<f32>(v + 8) = F(rec + 0xC);
    GRef<f32>(v + 0xC) = F(rec + 0x10);
    const u8 snd = GRef<u8>(rec + 3);
    if (kind == 3) {
        PlayEventSe(snd, v);
        return rec + 0x14;
    }
    if (GRef<s8>(Scene() + 0x1653) != 2) {
        func_00219668(snd);
        return rec + 0x14;
    }
    u32 p = 0;
    if (Game(0x2D) < Game(0x25)) p = Game(0x2D);
    if (GRef<u8>(rec + 2) == p) {
        func_00219668(GRef<u8>(rec + 3));
    } else if (snd == 0) {
        PlayEventSe(0, v);
    } else if (snd == 1) {
        PlayEventSe(1, v);
    }
    return rec + 0x14;
}

// EE: 0x00171158
// Records a sound event (type 3) when recording: {3, kind, a, b} plus, for kind 2, the vectors v
// and w (0x24 bytes); kinds 3 / 4 the vector v (0x14 bytes); others none (4 bytes).
void func_00171158(u32 kind, u32 a, u32 b, u32 v, u32 w) {
    if (!Recording()) return;
    kind &= 0xFF;
    u32 size = 4;
    if (kind == 2) size = 0x24;
    else if (kind == 3 || kind == 4) size = 0x14;
    const u32 r = func_0016DF88(static_cast<s32>(size));
    GRef<u8>(r + 1) = static_cast<u8>(kind);
    GRef<u8>(r + 2) = static_cast<u8>(a);
    GRef<u8>(r + 3) = static_cast<u8>(b);
    GRef<u8>(r) = 3;
    if (size >= 0x14) CopyWords(r + 4, v, 4);
    if (size == 0x24) CopyWords(r + 0x14, w, 4);
    func_0016E000(r + size);
}

// Per-player target-lock record L = *0x00410B10 (0x20 bytes, 0x00176918): +0 flags (1 / 2 primary of
// player 0 / 1, 4 / 8 secondary), bytes per player p: +4 / +6 / +8 / +0xA (recorded lock bytes,
// 0x0016ED08), +0xC primary slot, +0xE secondary slot (0xFF none); words +0x10 + 4p primary record,
// +0x18 + 4p secondary record (projectile block *0x004106AC + 0x4000 + 0x60 slot).

namespace {

u32 Locks() { return W(0x00410B10); }

}  // namespace

// EE: 0x00176940
// Resets the lock record: flags 0, every byte 0xFF, records 0.
void func_00176940() {
    GRef<u32>(Locks()) = 0;
    for (u32 p = 0; p < 2; ++p) {
        GRef<u8>(Locks() + 4 + p) = 0xFF;
        GRef<u8>(Locks() + 6 + p) = 0xFF;
        GRef<u8>(Locks() + 8 + p) = 0xFF;
        GRef<u8>(Locks() + 0xA + p) = 0xFF;
        GRef<u8>(Locks() + 0xC + p) = 0xFF;
        GRef<u32>(Locks() + 0x10 + p * 4) = 0;
        GRef<u8>(Locks() + 0xE + p) = 0xFF;
        GRef<u32>(Locks() + 0x18 + p * 4) = 0;
    }
}

// EE: 0x001769C0
// Per frame: flags cleared; each player's primary / secondary slot is dropped when its bit in the
// projectile block's live mask (+0x8A28) is clear.
void func_001769C0() {
    GRef<u32>(Locks()) = 0;
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
        const u8 a = GRef<u8>(Locks() + 0xC + static_cast<u32>(p));
        if (a != 0xFF && ((~W(W(0x004106AC) + 0x8A28) >> a) & 1u) != 0) func_00176B18(p);
        const u8 b = GRef<u8>(Locks() + 0xE + static_cast<u32>(p));
        if (b != 0xFF && ((~W(W(0x004106AC) + 0x8A28) >> b) & 1u) != 0) func_00176BA0(p);
    }
}

// EE: 0x00176AA8
// Player p locks projectile slot `slot` as primary when it has none; flag 1 (2 for player 1).
// Returns the primary slot.
u8 func_00176AA8(s32 p, u32 slot) {
    const u32 L = Locks();
    slot &= 0xFF;
    if (GRef<u8>(L + 0xC + static_cast<u32>(p)) == 0xFF) {
        GRef<u8>(L + 0xC + static_cast<u32>(p)) = static_cast<u8>(slot);
        GRef<u32>(Locks() + static_cast<u32>(p) * 4 + 0x10) = W(0x004106AC) + 0x4000 + slot * 0x60;
    }
    GRef<u32>(L) = W(L) | (p == 1 ? 2u : 1u);
    return GRef<u8>(L + 0xC + static_cast<u32>(p));
}

// EE: 0x00176B18
void func_00176B18(s32 p) { GRef<u8>(Locks() + 0xC + static_cast<u32>(p)) = 0xFF; }

// EE: 0x00176B30
// Secondary lock (flags 4 / 8 for player 1), as 0x00176AA8.
u8 func_00176B30(s32 p, u32 slot) {
    const u32 L = Locks();
    slot &= 0xFF;
    if (GRef<u8>(L + 0xE + static_cast<u32>(p)) == 0xFF) {
        GRef<u8>(L + 0xE + static_cast<u32>(p)) = static_cast<u8>(slot);
        GRef<u32>(Locks() + static_cast<u32>(p) * 4 + 0x18) = W(0x004106AC) + 0x4000 + slot * 0x60;
    }
    GRef<u32>(L) = W(L) | (p == 1 ? 8u : 4u);
    return GRef<u8>(L + 0xE + static_cast<u32>(p));
}

// EE: 0x00176BA0
void func_00176BA0(s32 p) { GRef<u8>(Locks() + 0xE + static_cast<u32>(p)) = 0xFF; }


// EE: 0x00176058
// Resets the replay HUD (jump to 0x0017D8E8).
void func_00176058() { func_0017D8E8(); }

}  // namespace ac04::game
