// Scene 5: mission results (slice S7; docs/subsystems/results-startup.md "Scene 5"): the sub-states
// of SceneResults, the kill tally, the step-by-step replay of the recorded unit tracks on the
// results map (ResultsUpdate), the results screen draw passes, the debriefing voice and the
// post-mission dispatch EnterPostMissionMenu.
//
// Result record R (*0x00411578, 0x0042D2C0): +0 mission time, +4 track record stream, +8 unit
// records (0xD0 each), +0xC s16 last track step, +0xE unit count, +0xF mode, +0x19 s8 mission,
// +0x1C u32 rank thresholds[4], +0x2C u32 trial time thresholds[3].
// Unit record U (0xD0): +0 model, +1 kind, +2 group, +3, +4 flags (bit 0 counts for the new-record
// flag), +5 kill count, +6 u16 score, +0x10 kills (0xC each: +0 time, +4/+6/+8 s16 position / 8,
// +0xA scorer 1 / 2).
// Result state St (*0x0041157C, 0x50 bytes): +0 map centre, +0x10 tallies[3] ({u32 entries, u16
// score}; entries of 6 bytes: +1 count, +4 u16 score; aircraft at +0 / +0x12 for player 2's own
// kills), +0x14 frame layouts[4] by mode ({rects, items, s8 rect count, s8 item count}), +0x18 unit
// displays (0x160 each), +0x1C aircraft list (8 x 6 bytes: model, count, group, -, u16 score), +0x20
// track stream position, +0x24 track step, +0x28 step x 6, +0x2C u16 track block, +0x2E u8 units
// in the block, +0x2F u8 aircraft list size, +0x30 u16 points, +0x34 reward, +0x38 credits, +0x3C
// f32 timer, +0x40 u8 flags (1 opening, 2 closing, 4 tracks running, 0x10 tracks shown, 0x20 / 0x40
// held view, 0x80 leaving), +0x41 mode, +0x42 rank, +0x43 player (0 / 1 / 2), +0x44 trial record
// slot, +0x45 new-record flag.
// Unit display D (0x160): +0 trail (256 x 4 points, +0x4000 s16 head, +0x4002 s16 tail), +4 kill
// markers (0x20 each: +0 time, +4 size, +0x10 position), +0x10 matrix, +0x50/+0x60 track points
// A/B, +0x70 position, +0x80/+0x90 control points, +0xA0/+0xB0 angles A/B, +0xD0/+0xE0 directions
// A/B, +0xF0/+0x100 up A/B, +0x110 up, +0x120/+0x130 forward A/B, +0x140 forward, +0x150 model,
// +0x151 group, +0x152 unit (0xFF none), +0x153 flags (1 curve, 2 hidden, 4 marker running, 0x10
// in the block, 0x20 ground unit from a 0x11 record), +0x154 kill marker index.
#include "game/game.h"

#include <cmath>
#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Work() { return W(0x00411580); }    // gp-0x4470: scene work (bucket list, camera +0x10)
u32 St() { return W(0x0041157C); }      // gp-0x4474: result state
u32 Rec() { return W(0x00411578); }     // gp-0x4478: result record
u32 Win() { return W(0x00411850); }     // gp-0x41A0: debriefing message window
constexpr u32 kTabLevels = 0x0042D2A0;  // menu tab levels of the results screen

u8& StB(u32 off) { return GRef<u8>(St() + off); }
u32 UnitRec(s32 i) { return W(Rec() + 8) + static_cast<u32>(i) * 0xD0; }
u32 Display(s32 i) { return W(St() + 0x18) + static_cast<u32>(i) * 0x160; }
s32 UnitRecs() { return GRef<u8>(Rec() + 0xE); }

void ClearTrail(u32 d) {
    GRef<u16>(W(d) + 0x4002) = 0;
    GRef<u16>(W(d) + 0x4000) = 0;
}

// Kill categories of the tally: 0 aircraft (model < 0x60), 1 ground (and models 0xDA, 0xDB, 0xE3),
// 2 ships (models from 0xD0).
s32 KillCategory(u8 model) {
    if (model == 0xE3 || model == 0xDA || model == 0xDB) return 1;
    if (!(model < 0xD0)) return 2;
    return model < 0x60 ? 0 : 1;
}

void AddToEntry(u32 e, u16 score) {
    GRef<u8>(e + 1) += 1;
    GRef<u16>(e + 4) = static_cast<u16>(GRef<u16>(e + 4) + score);
}

// Reward and rank from the points: reward = (points - first threshold) x 100 (0 below it); rank =
// number of thresholds reached. Trial missions timed by the record table (*0x004114E7 per mission
// = 1) rank by time instead: rank 1, then 2 + k for every time threshold k not below the time (the
// mission time, or the elapsed track time while the tracks run).
void ComputeRewardAndRank() {
    const u32 st = St();
    const u32 r = Rec();
    const u32 points = GRef<u16>(st + 0x30);
    const u32 first = W(r + 0x1C);
    if (points < first)
        GRef<u32>(st + 0x34) = 0;
    else
        GRef<u32>(st + 0x34) = (points - first) * 100;
    if (GRef<u8>(r + 0xF) == 3 && GRef<s8>(0x004114E7 + GRef<s8>(r + 0x19)) == 1) {
        const s32 time = GRef<s32>(r);
        s32 t = W(st + 0x24) * 6;
        if (time < t || (StB(0x40) & 4) == 0) t = time;
        GRef<u8>(st + 0x42) = 1;
        for (s32 k = 0; k < 3; ++k) {
            if (!(GRef<s32>(Rec() + 0x2C + 4u * static_cast<u32>(k)) < t)) GRef<u8>(St() + 0x42) = static_cast<u8>(k + 2);
        }
        return;
    }
    for (s32 k = 0; k < 4; ++k) {
        if (!(GRef<u16>(St() + 0x30) < W(Rec() + 0x1C + 4u * static_cast<u32>(k)))) GRef<u8>(St() + 0x42) = static_cast<u8>(k + 1);
    }
}

// Track angles: bytes as fractions of a turn (`turn` / 255), wrapped to (-half, half].
void TrackAngles(u32 out, u32 src, f32 turn, f32 half) {
    const f32 a = static_cast<f32>(static_cast<s32>(GRef<u8>(src + 1))) * turn / 255.0f;
    GRef<f32>(out) = a;
    GRef<f32>(out + 4) = static_cast<f32>(static_cast<s32>(GRef<u8>(src + 2))) * turn / 255.0f;
    GRef<f32>(out + 8) = static_cast<f32>(static_cast<s32>(GRef<u8>(src + 3))) * turn / 255.0f;
    if (half < a) GRef<f32>(out) = fpu::Sub(a, turn);
    if (half < GRef<f32>(out + 4)) GRef<f32>(out + 4) = fpu::Sub(GRef<f32>(out + 4), turn);
    if (half < GRef<f32>(out + 8)) GRef<f32>(out + 8) = fpu::Sub(GRef<f32>(out + 8), turn);
    GRef<u32>(out + 0xC) = 0;
}

void TrackPosition(u32 out, u32 src) {
    GRef<f32>(out) = static_cast<f32>(GRef<s16>(src + 4)) * 8.0f;
    GRef<f32>(out + 4) = static_cast<f32>(GRef<s16>(src + 6)) * 8.0f;
    GRef<f32>(out + 0xC) = 1.0f;
    GRef<f32>(out + 8) = static_cast<f32>(GRef<s16>(src + 8)) * 8.0f;
}

void TrackDirection(u32 out, u32 src) {
    GRef<f32>(out) = static_cast<f32>(GRef<s8>(src + 0xA)) / 127.0f;
    GRef<f32>(out + 4) = static_cast<f32>(GRef<s8>(src + 0xB)) / 127.0f;
    GRef<f32>(out + 0xC) = 1.0f;
    GRef<f32>(out + 8) = static_cast<f32>(GRef<s8>(src + 0xC)) / 127.0f;
}

}  // namespace

// EE: 0x00246FC8
// Copies the campaign credits (0x002A0D30) into the result state.
void func_00246FC8() { GRef<u32>(St() + 0x38) = W(0x002A0D30); }

// EE: 0x00246E50
// Frame layout table of a results page: 0x003B0090 for `a` 0, 0x003B00A0 for 1, else by `b`:
// 0x003B00B0 / 0x003B00C0 / 0x003B00D0 (2 and above).
u32 func_00246E50(s32 a, s32 b) {
    if (a == 0) return 0x003B0090;
    if (a == 1) return 0x003B00A0;
    if (b == 0) return 0x003B00B0;
    if (b == 1) return 0x003B00C0;
    return 0x003B00D0;
}

// EE: 0x00246FF0
// Writes the result summary (*0x004114D8, `results-startup.md`) from the result state: pending bit,
// reward, mission time, kills (tally 0 aircraft count, ground + ship counts), points, rank, mode,
// mission and the new-record bit (state +0x45).
void func_00246FF0() {
    const u32 st = St();
    u32 s = W(0x004114D8);
    GRef<u32>(s + 8) = W(st + 0x34);
    GRef<u16>(s) |= 1;
    GRef<u32>(s + 0xC) = W(Rec());
    const u32 t = W(W(st + 0x10));
    GRef<u32>(s + 0x10) = GRef<u8>(t + 1);
    GRef<u32>(s + 0x14) = GRef<u8>(t + 7);
    GRef<u16>(s + 6) = GRef<u16>(st + 0x30);
    GRef<u32>(s + 0x14) = static_cast<u32>(GRef<u8>(t + 7)) + GRef<u8>(t + 0xD);
    GRef<u8>(s + 4) = GRef<u8>(st + 0x42);
    GRef<u16>(W(0x004114D8) + 2) = GRef<u8>(St() + 0x41);
    GRef<u8>(W(0x004114D8) + 5) = GRef<u8>(Rec() + 0x19);
    s = W(0x004114D8);
    if (GRef<u8>(St() + 0x45) == 1)
        GRef<u16>(s) |= 2;
    else
        GRef<u16>(s) &= 0xFFFD;
}

// EE: 0x00247540
// Trial record slot (state +0x44) in mode 3: the points of player 1's kills (units 0 / 1 and units
// with a group, credited to scorer 1) against the third best score of the trial record (record
// table byte *0x004114E7 per mission = 0: slot 0 when higher), or the mission time against the third
// best time (byte = 1: slot 1 when lower); 2 otherwise. Also state +0x43 = player (game[0x2D],
// clamped to 2).
void func_00247540() {
    s32 points[2] = {0, 0};
    const u32 r = Rec();
    for (s32 i = 0; i < GRef<u8>(r + 0xE); ++i) {
        const u32 u = W(r + 8) + static_cast<u32>(i) * 0xD0;
        const u8 group = GRef<u8>(u + 2);
        for (s32 j = 0; j < GRef<u8>(u + 5); ++j) {
            const u8 scorer = GRef<u8>(u + static_cast<u32>(j) * 0xC + 0x1A);
            if (!(static_cast<u32>(i) < 2) && group == 0) continue;
            s32* dst = scorer == 1 ? &points[0] : scorer == 2 ? &points[1] : nullptr;
            if (dst != nullptr) *dst += GRef<s16>(u + 6);
        }
    }
    const u8 player = Game(0x2D);
    const u32 st = St();
    if (player == 0)
        GRef<u8>(st + 0x43) = 0;
    else if (player == 1)
        GRef<u8>(st + 0x43) = 1;
    else
        GRef<u8>(st + 0x43) = 2;
    const u32 s = St();
    if (GRef<u8>(s + 0x41) != 3) {
        GRef<u8>(s + 0x44) = 2;
        return;
    }
    const s8 mission = GRef<s8>(Rec() + 0x19);
    const u32 rec = 0x002A0318 + 0x9A0 + static_cast<u32>(mission * 0x14);
    if (GRef<s8>(0x004114E7 + mission) == 0) {
        GRef<u8>(s + 0x44) = static_cast<s32>(GRef<u16>(rec + 6)) < points[0] ? 0 : 2;
    } else {
        GRef<u8>(s + 0x44) = W(Rec()) < W(rec + 0x10) ? 1 : 2;
    }
}

// EE: 0x002476F0
// Counts every kill at once (the skip of the track replay): tallies, aircraft list and points
// cleared, then each credited kill (scorer 1 or 2; units 0 / 1 and units with a group only) added to
// the scorer's tally by category (aircraft also into the aircraft list, at most 8 models; in mode 2
// player 1 / 2's own aircraft go to the +0x12 entry), unit flag bit 0 sets the new-record flag;
// then reward and rank.
void func_002476F0() {
    StB(0x2F) = 0;
    GRef<u16>(St() + 0x30) = 0;
    GRef<u32>(St() + 0x34) = 0;
    for (u32 i = 0; i < 3; ++i) {
        const u32 t = W(St() + 0x10) + i * 8;
        GRef<u16>(t + 4) = 0;
        for (u32 k = 0; k < 4; ++k) {
            GRef<u16>(W(t) + k * 6 + 4) = 0;
            GRef<u8>(W(t) + k * 6 + 1) = 0;
        }
    }
    for (u32 k = 0; k < 8; ++k) {
        const u32 e = W(St() + 0x1C) + k * 6;
        GRef<u16>(e + 4) = 0;
        GRef<u8>(e + 1) = 0;
    }
    for (s32 i = 0; i < UnitRecs(); ++i) {
        const u32 u = UnitRec(i);
        const u8 model = GRef<u8>(u);
        const u8 group = GRef<u8>(u + 2);
        const bool own = static_cast<u32>(i) < 2;
        for (s32 j = 0; j < GRef<u8>(u + 5); ++j) {
            const u8 scorer = GRef<u8>(u + static_cast<u32>(j) * 0xC + 0x1A);
            if (!own && group == 0 && (scorer == 1 || scorer == 2)) continue;
            if ((GRef<u8>(u + 4) & 1) != 0) StB(0x45) = 1;
            u32 tally = 0;
            if (scorer == 1)
                tally = W(St() + 0x10);
            else if (scorer == 2)
                tally = W(St() + 0x10) + 8;
            if (tally == 0) continue;
            const u16 score = GRef<u16>(u + 6);
            const s32 cat = KillCategory(model);
            if (cat == 2 || cat == 1) {
                AddToEntry(W(tally) + (cat == 2 ? 0xC : 6), score);
                GRef<u16>(tally + 4) = static_cast<u16>(GRef<u16>(tally + 4) + score);
                GRef<u16>(St() + 0x30) = static_cast<u16>(GRef<u16>(St() + 0x30) + score);
                continue;
            }
            bool found = false;
            for (s32 k = 0; k < StB(0x2F); ++k) {
                const u32 e = W(St() + 0x1C) + static_cast<u32>(k) * 6;
                if (GRef<u8>(e) != model) continue;
                AddToEntry(e, score);
                GRef<u16>(St() + 0x30) = static_cast<u16>(GRef<u16>(St() + 0x30) + score);
                const u32 a = GRef<u8>(Rec() + 0xF) == 2 && own ? W(tally) + 0x12 : W(tally);
                AddToEntry(a, score);
                GRef<u16>(tally + 4) = static_cast<u16>(GRef<u16>(tally + 4) + score);
                found = true;
                break;
            }
            if (found || !(StB(0x2F) < 8)) continue;
            const u32 e = W(St() + 0x1C) + StB(0x2F) * 6u;
            GRef<u8>(e) = model;
            GRef<u8>(e + 1) = 1;
            GRef<u16>(e + 4) = score;
            GRef<u8>(e + 2) = GRef<u8>(u + 3);
            StB(0x2F) += 1;
            GRef<u16>(St() + 0x30) = static_cast<u16>(GRef<u16>(St() + 0x30) + score);
            const u32 a = GRef<u8>(Rec() + 0xF) == 2 && own ? W(tally) + 0x12 : W(tally);
            AddToEntry(a, score);
            GRef<u16>(tally + 4) = static_cast<u16>(GRef<u16>(tally + 4) + score);
        }
    }
    ComputeRewardAndRank();
}

// EE: 0x00247BD8
// Per frame while the results screen runs (sub 4). Pad of the shown player (state +0x43 == 1: pad
// 1): R2 held runs 5 steps per frame, L2 held (0x2000) holds the view (+0x40 bit 0x40, SE 0xD on
// press). Each step: the opening timer (bit 0: +K 0x00410404 up to 5, then the tracks start and the
// debriefing message is shown: page by player in mode 2, by record slot in mode 3), the closing timer
// (bit 1: -K 0x00410408, SE 0xE at 3.00); while the tracks are shown (bit 0x10) the kill markers grow
// (K 0x0041040C per step, size from the curve 0x00246D98 and the weights 0x00409C50), every kill whose
// time falls in this step starts a marker (SE 7) and is counted when the tracks run, the track
// stream is read every 10 steps (records 0x10: unit placement and next placement, 0x11: ground unit)
// and every unit is placed between its two track points (Bezier when both were read) with a trail
// of 256 frames for aircraft. Then: square advances the message, reward and rank; cross (or square
// on a finished page) skips the tracks (counting every kill at once) or starts closing (window
// hidden, voice stopped, BGM fade 1.0).
// fpucheck: verified 13 (the 6 angle-wrap subs of the two track-point reads share TrackAngles)
void ResultsUpdate() {
    guest::GuestFrame frame(0x120);
    const u32 sp = frame.Address();
    const u32 v_up = sp + 0x20;
    const u32 v_fwd = sp + 0x30;
    const u32 m = sp + 0x40;
    const u32 tmp = sp + 0x80;
    s32 loops = 1;
    const u32 pad = StB(0x43) == 1 ? 1 : 0;
    const u32 held = Ctx()->pad_buttons[pad][0];
    const u32 pressed = Ctx()->pad_buttons[pad][1];
    if ((held & 0x200) != 0) loops = 5;
    if ((held & 0x2000) != 0)
        StB(0x40) |= 0x40;
    else
        StB(0x40) &= 0xBF;
    if ((pressed & 0x2000) != 0) PlayMenuSe(0, 0xD);
    const u32 cross = pressed & 0x40;
    const u32 square = pressed & 0x10;
    s32 i = 0;
    for (;;) {
        u32 st = St();
        if ((GRef<u8>(st + 0x40) & 1) != 0) {
            const u8 flags = GRef<u8>(st + 0x40);
            const f32 t = fpu::Add(GRef<f32>(st + 0x3C), F(0x00410404));
            GRef<f32>(st + 0x3C) = t;
            if (5.0f < t) {
                GRef<u8>(st + 0x40) = flags | 0x30;
                StB(0x40) &= 0xFE;
                func_0024F0D8(Win());
                const u8 mode = GRef<u8>(Rec() + 0xF);
                if (mode == 2)
                    ShowMessagePage(Win(), StB(0x43));
                else if (mode == 3)
                    ShowMessagePage(Win(), StB(0x44));
            }
            st = St();
        }
        if ((GRef<u8>(st + 0x40) & 2) != 0) {
            const f32 t = fpu::Sub(GRef<f32>(st + 0x3C), F(0x00410408));
            GRef<f32>(st + 0x3C) = t;
            if (fpu::FloatToInt(t * 100.0f) == 300) PlayMenuSe(0, 0xE);
        }
        if ((StB(0x40) & 0x10) == 0) break;
        ++i;
        // Kill markers.
        for (u32 k = 0; k < 4; ++k) GRef<u32>(sp + 0x10 + 4 * k) = W(0x00409C50 + 4 * k);
        for (s32 k = 0; k < UnitRecs(); ++k) {
            const u32 d = Display(k);
            const u8 df = GRef<u8>(d + 0x153);
            if ((df & 4) == 0) continue;
            const s32 idx = GRef<s32>(d + 0x154);
            const u32 mk = W(d + 4) + static_cast<u32>(idx) * 0x20;
            const u32 u = UnitRec(k);
            if (1.0f <= GRef<f32>(mk)) {
                const u8 v = df & 0xFB;
                GRef<u8>(d + 0x153) = v;
                if (GRef<u8>(u + 5) != 0) {
                    GRef<u8>(d + 0x153) = v | 2;
                    if (idx < GRef<u8>(u + 5)) GRef<s32>(d + 0x154) = idx + 1;
                }
                ClearTrail(d);
            } else {
                const f32 t = fpu::Add(GRef<f32>(mk), F(0x0041040C));
                GRef<f32>(mk) = t;
                if (1.0f <= t) GRef<f32>(mk) = 1.0f;
            }
            func_00246D98(GRef<f32>(mk), sp);
            const f32 a = GRef<f32>(sp) * GRef<f32>(sp + 0x10);
            const f32 b = GRef<f32>(sp + 4) * GRef<f32>(sp + 0x14);
            const f32 c = GRef<f32>(sp + 8) * GRef<f32>(sp + 0x18);
            const f32 e = GRef<f32>(sp + 0xC) * GRef<f32>(sp + 0x1C);
            GRef<f32>(mk + 4) = fpu::Add(fpu::Add(fpu::Add(a, b), c), e);
        }
        // Kills of this step.
        for (s32 k = 0; k < UnitRecs(); ++k) {
            const u32 u = UnitRec(k);
            if (GRef<u8>(u + 5) == 0) continue;
            const u32 d = Display(k);
            const s32 step = GRef<s32>(St() + 0x24);
            s32 idx = GRef<s32>(d + 0x154);
            if (idx == GRef<u8>(u + 5)) idx -= 1;
            const u32 kill = u + 0x10 + static_cast<u32>(idx) * 0xC;
            const s32 time = GRef<s32>(kill);
            if (time < (step - 1) * 6) continue;
            if (!(time < step * 6)) continue;
            const u8 scorer = GRef<u8>(kill + 0xA);
            const u8 model = GRef<u8>(u);
            const u8 group = GRef<u8>(u + 2);
            u32 tally = 0;
            if (scorer == 1)
                tally = W(St() + 0x10);
            else if (scorer == 2)
                tally = W(St() + 0x10) + 8;
            PlayMenuSe(0, 7);
            const u32 mk = W(d + 4) + W(d + 0x154) * 0x20;
            GRef<u32>(mk) = 0;
            GRef<u32>(mk + 4) = 0;
            GRef<f32>(mk + 0x10) = static_cast<f32>(GRef<s16>(kill + 4)) * 8.0f;
            GRef<f32>(mk + 0x14) = static_cast<f32>(GRef<s16>(kill + 6)) * 8.0f;
            GRef<f32>(mk + 0x1C) = 1.0f;
            GRef<f32>(mk + 0x18) = static_cast<f32>(GRef<s16>(kill + 8)) * 8.0f;
            GRef<u8>(d + 0x153) |= 4;
            if (static_cast<u8>(GRef<u8>(u + 1) - 2) < 2 || k == 0 || k == 1) GRef<u8>(d + 0x153) |= 2;
            if (tally == 0) continue;
            if ((StB(0x40) & 4) == 0) continue;
            const bool own = static_cast<u32>(k) < 2;
            if (!own && group == 0 && (scorer == 1 || scorer == 2)) continue;
            if ((GRef<u8>(u + 4) & 1) != 0) StB(0x45) = 1;
            const u16 score = GRef<u16>(u + 6);
            const s32 cat = KillCategory(model);
            if (cat == 2 || cat == 1) {
                AddToEntry(W(tally) + (cat == 2 ? 0xC : 6), score);
                GRef<u16>(tally + 4) = static_cast<u16>(GRef<u16>(tally + 4) + score);
                GRef<u16>(St() + 0x30) = static_cast<u16>(GRef<u16>(St() + 0x30) + score);
                continue;
            }
            bool found = false;
            for (s32 n = 0; n < StB(0x2F); ++n) {
                const u32 e = W(St() + 0x1C) + static_cast<u32>(n) * 6;
                if (GRef<u8>(e) != model) continue;
                AddToEntry(e, score);
                GRef<u16>(St() + 0x30) = static_cast<u16>(GRef<u16>(St() + 0x30) + score);
                AddToEntry(own ? W(tally) + 0x12 : W(tally), score);
                GRef<u16>(tally + 4) = static_cast<u16>(GRef<u16>(tally + 4) + score);
                found = true;
                break;
            }
            if (found || !(StB(0x2F) < 8)) continue;
            const u32 e = W(St() + 0x1C) + StB(0x2F) * 6u;
            GRef<u8>(e) = model;
            GRef<u8>(e + 1) = 1;
            GRef<u16>(e + 4) = score;
            GRef<u8>(e + 2) = GRef<u8>(u + 3);
            StB(0x2F) += 1;
            GRef<u16>(St() + 0x30) = static_cast<u16>(GRef<u16>(St() + 0x30) + score);
            AddToEntry(own ? W(tally) + 0x12 : W(tally), score);
            GRef<u16>(tally + 4) = static_cast<u16>(GRef<u16>(tally + 4) + score);
        }
        // End of the track record: start over.
        st = St();
        if (static_cast<s32>(GRef<s16>(Rec() + 0xC)) < static_cast<s32>(GRef<u16>(st + 0x2C))) {
            GRef<u32>(st + 0x24) = 0;
            GRef<u16>(st + 0x2C) = 0;
            GRef<u8>(st + 0x40) &= 0xFB;
            GRef<u32>(st + 0x28) = 0;
            StB(0x2E) = 0;
            GRef<u32>(St() + 0x20) = W(Rec() + 4);
            for (s32 k = 0; k < UnitRecs(); ++k) {
                const u32 d = Display(k);
                GRef<u32>(d + 0x154) = 0;
                ClearTrail(d);
                GRef<u8>(d + 0x153) = 0;
            }
        }
        std::memset(&GRef<u8>(v_up), 0, 0x10);
        GRef<f32>(v_up + 4) = 1.0f;
        std::memset(&GRef<u8>(v_fwd), 0, 0x10);
        GRef<f32>(v_fwd + 8) = 1.0f;
        st = St();
        if (GRef<s32>(st + 0x24) % 10 == 0) {
            StB(0x2E) = 0;
            GRef<u16>(st + 0x2C) = static_cast<u16>(GRef<s32>(st + 0x24) / 10);
            for (s32 k = 0; k < UnitRecs(); ++k) {
                const u32 d = Display(k);
                GRef<u8>(d + 0x152) = 0xFF;
                GRef<u8>(d + 0x153) &= 0xEF;
            }
            // Track block (written by func_0024CDD0): a 4-byte header, then records up to 0 (block end,
            // 2 bytes) or 0xFF; the recorder writes only codes 0x10 and 0x11 (with another code the
            // original would not advance).
            u32 s = W(St() + 0x20);
            u32 t0 = s + 4;
            if (GRef<u8>(s) != 0xFF) {
                s = t0;
                const u8 first = GRef<u8>(s);
                if (first == 0) {
                    t0 = s + 2;
                } else if (first != 0xFF) {
                    for (;;) {
                        const u8 op = GRef<u8>(t0);
                        if (op == 0x10) {
                            const u32 ev = t0 + 2;
                            const u8 unit = GRef<u8>(ev);
                            const u32 d = Display(unit);
                            const u32 u = UnitRec(unit);
                            const u8 v = GRef<u8>(d + 0x153) | 0x10;
                            const u8 a1 = v & 0xDF;
                            GRef<u8>(d + 0x153) = a1;
                            if ((GRef<u8>(ev + 0xD) & 1) != 0) {
                                if ((v & 6) != 0) {
                                    ClearTrail(d);
                                    if ((a1 & 4) != 0 && GRef<s32>(d + 0x154) < GRef<u8>(u + 5)) GRef<s32>(d + 0x154) += 1;
                                }
                                GRef<u8>(d + 0x153) = a1 & 0xF9;
                            }
                            if ((GRef<u8>(d + 0x153) & 2) == 0) {
                                GRef<u8>(d + 0x152) = GRef<u8>(ev);
                                const u32 u2 = UnitRec(GRef<u8>(ev));
                                GRef<u8>(d + 0x150) = GRef<u8>(u2);
                                GRef<u8>(d + 0x151) = GRef<u8>(u2 + 2);
                                TrackPosition(d + 0x50, ev);
                                TrackAngles(d + 0xA0, ev, F(0x00410410), F(0x00410414));
                                TrackDirection(d + 0xD0, ev);
                                sceVu0UnitMatrix(m);
                                func_00102D28(m, m, d + 0xA0);
                                sceVu0ApplyMatrix(d + 0xF0, m, v_up);
                                sceVu0ApplyMatrix(d + 0x120, m, v_fwd);
                                sceVu0CopyVector(d + 0x80, d + 0x50);
                                sceVu0CopyVector(d + 0x90, d + 0x50);
                                sceVu0CopyVector(d + 0x60, d + 0x50);
                                sceVu0CopyVector(d + 0xB0, d + 0xA0);
                                sceVu0CopyVector(d + 0xE0, d + 0xD0);
                                sceVu0CopyVector(d + 0x100, d + 0xF0);
                                sceVu0CopyVector(d + 0x130, d + 0x120);
                                StB(0x2E) += 1;
                            }
                            t0 = ev + 0xE;
                        } else if (op == 0x11) {
                            const u32 d = Display(GRef<u8>(t0 + 2));
                            t0 += 4;
                            GRef<u8>(d + 0x153) |= 0x30;
                        }
                        s = t0;
                        const u8 next = GRef<u8>(s);
                        if (next == 0xFF) break;
                        if (next == 0) {
                            t0 = s + 2;
                            break;
                        }
                    }
                }
            }
            GRef<u32>(St() + 0x20) = t0;
            for (s32 k = 0; k < UnitRecs(); ++k) {
                const u32 d = Display(k);
                if ((GRef<u8>(d + 0x153) & 0x10) == 0) ClearTrail(d);
            }
            // Second track point of each shown unit from the next block.
            if (static_cast<s32>(GRef<u16>(St() + 0x2C)) < static_cast<s32>(GRef<s16>(Rec() + 0xC))) {
                for (s32 k = 0; k < UnitRecs(); ++k) {
                    const u32 d = Display(k);
                    if ((GRef<u8>(d + 0x153) & 2) != 0) continue;
                    u32 p = W(St() + 0x20) + 4;
                    while (GRef<u8>(p) != 0) {
                        const u8 op = GRef<u8>(p);
                        if (op != 0x10) {
                            if (op == 0x11) p += 4;
                            continue;
                        }
                        const u32 ev = p + 2;
                        if (GRef<u8>(d + 0x152) == GRef<u8>(ev)) {
                            GRef<u8>(d + 0x153) &= 0xDF;
                            TrackPosition(d + 0x60, ev);
                            TrackAngles(d + 0xB0, ev, F(0x00410418), F(0x0041041C));
                            TrackDirection(d + 0xE0, ev);
                            std::memset(&GRef<u8>(tmp), 0, 0x10);
                            GRef<f32>(tmp + 0xC) = 1.0f;
                            const f32 dist = func_00104F10(d + 0x50, d + 0x60);
                            sceVu0ScaleVector(tmp, d + 0xD0, dist * F(0x00410420));
                            sceVu0AddVector(d + 0x80, d + 0x50, tmp);
                            GRef<f32>(d + 0x8C) = 1.0f;
                            sceVu0ScaleVector(tmp, d + 0xE0, dist * F(0x00410424));
                            sceVu0AddVector(d + 0x90, d + 0x60, tmp);
                            GRef<f32>(d + 0x9C) = 1.0f;
                            GRef<u8>(d + 0x153) |= 1;
                            sceVu0UnitMatrix(m);
                            func_00102D28(m, m, d + 0xB0);
                            sceVu0ApplyMatrix(d + 0x100, m, v_up);
                            sceVu0ApplyMatrix(d + 0x130, m, v_fwd);
                        }
                        p = ev + 0xE;
                    }
                }
            }
        }
        // Place every unit of the block between its track points.
        const f32 frac = static_cast<f32>(GRef<s32>(St() + 0x24) % 10) / 10.0f;
        func_00246D98(frac, tmp);
        for (s32 k = 0; k < UnitRecs(); ++k) {
            const u32 d = Display(k);
            if (GRef<u8>(d + 0x152) == 0xFF) continue;
            const u32 pos = d + 0x70;
            if ((GRef<u8>(d + 0x153) & 0x14) == 4)
                sceVu0CopyVector(pos, d + 0x60);
            else if ((GRef<u8>(d + 0x153) & 1) != 0)
                func_00246DE8(pos, d + 0x50, d + 0x80, d + 0x90, d + 0x60, tmp);
            else
                sceVu0CopyVector(pos, d + 0x50);
            const f32 g = fpu::Sub(1.0f, frac);
            sceVu0ScaleVector(sp + 0x90, d + 0xF0, g);
            sceVu0ScaleVector(sp + 0xA0, d + 0x100, frac);
            sceVu0AddVector(d + 0x110, sp + 0x90, sp + 0xA0);
            sceVu0Normalize(d + 0x110, d + 0x110);
            sceVu0ScaleVector(sp + 0xB0, d + 0x120, g);
            sceVu0ScaleVector(sp + 0xC0, d + 0x130, frac);
            sceVu0AddVector(d + 0x140, sp + 0xB0, sp + 0xC0);
            sceVu0Normalize(d + 0x140, d + 0x140);
            std::memset(&GRef<u8>(sp + 0xD0), 0, 0x10);
            GRef<f32>(sp + 0xDC) = 1.0f;
            sceVu0CameraMatrix(sp + 0xE0, sp + 0xD0, d + 0x140, d + 0x110);
            sceVu0InversMatrix(d + 0x10, sp + 0xE0);
            sceVu0TransMatrix(d + 0x10, d + 0x10, pos);
            if (!(GRef<u8>(d + 0x150) < 0x60)) continue;
            const u32 trail = W(d);
            GRef<u16>(trail + 0x4000) += 1;
            if (!(GRef<s16>(trail + 0x4000) < 0x100)) GRef<u16>(trail + 0x4000) = 0;
            for (u32 n = 0; n < 4; ++n) {
                sceVu0ApplyMatrix(trail + static_cast<u32>(GRef<s16>(trail + 0x4000)) * 0x40 + n * 0x10, d + 0x10,
                                  0x003B0150 + n * 0x10);
            }
            s16 tail = GRef<s16>(trail + 0x4002);
            if (GRef<s16>(trail + 0x4000) == tail) {
                tail = static_cast<s16>(tail + 1);
                GRef<s16>(trail + 0x4002) = tail;
            }
            if (!(tail < 0x100)) GRef<u16>(trail + 0x4002) = 0;
        }
        st = St();
        const s32 next = GRef<s32>(st + 0x24) + 1;
        GRef<s32>(st + 0x24) = next;
        GRef<s32>(st + 0x28) = next * 6;
        if (!(i < loops)) break;
    }
    if (square != 0) {
        func_0024F158(Win());
        if ((GRef<u8>(Win()) & 1) == 0 && (StB(0x40) & 2) == 0 && (StB(0x40) & 1) == 0) PlayMenuSe(0, 0xD);
    }
    UpdateMessageWindow(Win());
    ComputeRewardAndRank();
    if (cross == 0 && (square == 0 || (GRef<u8>(Win()) & 1) == 0)) return;
    if ((StB(0x40) & 4) != 0) {
        func_002476F0();
        StB(0x40) &= 0xFB;
        PlayMenuSe(0, 0xD);
        return;
    }
    if ((StB(0x40) & 2) != 0) return;
    HideMessageWindow(Win());
    StB(0x40) |= 2;
    StB(0x40) &= 0xCF;
    GRef<f32>(St() + 0x3C) = 5.0f;
    StB(0x40) &= 0xFE;
    func_002507D8(Win());
    func_0021F190(1.0f, 0, 0, 0, 1);
    PlayMenuSe(0, 0xD);
}

namespace {

void Pw(u32 p, u32 v) { GRef<u32>(p) = v; }
u64 Arg(s64 v) { return static_cast<u64>(v); }

// XYZF2 value from an ftoi4 vector: x | y << 16, z >> 4 (arithmetic) in the upper word.
void PutXyz(u32 p, u32 v) {
    Pw(p, W(v) | W(v + 4) << 16);
    Pw(p + 4, static_cast<u32>(GRef<s32>(v + 8) >> 4));
}

// DMA cnt tag + VIF NOP + DIRECT of `qwc` quadwords.
void PutDirect(u32 p, u32 qwc) {
    Pw(p, 0x10000000 | qwc);
    Pw(p + 4, 0);
    Pw(p + 8, 0);
    Pw(p + 0xC, 0x50000000 | qwc);
}

// GIF tag words 0-2 (word 3, the upper half of the register list, is left as it is).
void PutGifTag(u32 p, u32 w0, u32 w1, u32 w2) {
    Pw(p, w0);
    Pw(p + 4, w1);
    Pw(p + 8, w2);
}

// Map draw header: TEST_1 0x50000 (depth test GEQUAL) and the context's ZBUF_1 entry (ctx+0xE0);
// the upper words of the TEST_1 entry are left as they are.
u32 PutMapDrawHeader(u32 p) {
    PutDirect(p, 3);
    PutGifTag(p + 0x10, 0x8002, 0x10000000, 0xE);
    Pw(p + 0x20, 0x50000);
    Pw(p + 0x28, 0x47);
    CopyQ(p + 0x30, CtxAddr() + 0xE0);
    return p + 0x40;
}

// Map draw trailer: TEST_1 0x30000 and ZBUF_1 with ZMSK set; closes the packet.
void PutMapDrawTrailer(u32 list, u32 p) {
    PutDirect(p, 3);
    PutGifTag(p + 0x10, 0x8002, 0x10000000, 0xE);
    Pw(p + 0x20, 0x30000);
    Pw(p + 0x28, 0x47);
    CopyQ(p + 0x30, CtxAddr() + 0xE0);
    Pw(p + 0x34, 1);
    EndPacket(list, 0, p + 0x40);
}

vu0::Vec4 LoadV(u32 p) { return {GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; }

vu0::Vec4 Transform(u32 m, const vu0::Vec4& v) {
    return vu0::MulMatrix(LoadV(m), LoadV(m + 0x10), LoadV(m + 0x20), LoadV(m + 0x30), v);
}

// vclipw.xyz bits (x > |w|, x < -|w|, y > |w|, y < -|w|, z > |w|, z < -|w|).
u32 ClipBits(const vu0::Vec4& v) {
    const f32 w = std::fabs(v.w);
    u32 b = 0;
    if (v.x > w) b |= 0x01;
    if (v.x < -w) b |= 0x02;
    if (v.y > w) b |= 0x04;
    if (v.y < -w) b |= 0x08;
    if (v.z > w) b |= 0x10;
    if (v.z < -w) b |= 0x20;
    return b;
}

// Line packet of the map grid (PRIM 0x49, two vertices with RGBAQ = alpha << 24 | 0x181818).
u32 PutGridLine(u32 p, u32 a0, u32 v0, u32 a1, u32 v1) {
    PutDirect(p, 4);
    PutGifTag(p + 0x10, 0x8001, 0x64000000, 0x00F41410);
    GRef<u64>(p + 0x20) = 0x49;
    Pw(p + 0x28, a0 << 24 | 0x181818);
    GRef<f32>(p + 0x2C) = 1.0f;
    PutXyz(p + 0x30, v0);
    Pw(p + 0x38, a1 << 24 | 0x181818);
    GRef<f32>(p + 0x3C) = 1.0f;
    PutXyz(p + 0x40, v1);
    return p + 0x50;
}

}  // namespace

// EE: 0x00246A90
// Frame item markers of the results layout: for each of `count` items (0x18 bytes: x0, y0, x1, y1,
// RGBA) whose time in `times` is not negative, a sprite (XYZ3 / XYZ2) on a frame draw environment
// with ALPHA 0x68 / FIX 0x80, into bucket `index` of `list`.
void func_00246A90(u32 list, s32 index, s32 count, u32 items, u32 times) {
    if (!(0 < count)) return;
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    GRef<u64>(p) = 0x1121400000000001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0x8000000068ull;
    GRef<u64>(p + 0x18) = 0x43;
    p += 0x20;
    for (s32 n = count; n != 0; --n, items += 0x18, times += 4) {
        if (GRef<f32>(times) < 0.0f) continue;
        GRef<u64>(p) = 0x4400000000008001ull;
        GRef<u64>(p + 8) = 0xF5D1;
        GRef<u64>(p + 0x10) = GRef<u32>(items + 0x10);
        GRef<u64>(p + 0x18) = static_cast<u64>(static_cast<s64>(GRef<s32>(items))) |
                              static_cast<u64>(static_cast<s64>(GRef<s32>(items + 4))) << 16;
        GRef<u64>(p + 0x20) = static_cast<u64>(static_cast<s64>(GRef<s32>(items + 8))) |
                              static_cast<u64>(static_cast<s64>(GRef<s32>(items + 0xC))) << 16;
        p += 0x30;
    }
    LinkPacket(list, index, start, CloseDmaPacket(start, p));
}

// EE: 0x00246C08
// Results map camera matrices: as UpdateCameraMatrices with the far value scaled by K 0x00410400
// instead of offset.
void func_00246C08(u32 cam) {
    const u32 view = cam + 0x20;
    sceVu0UnitMatrix(view);
    func_00102D28(view, view, cam + 0x10);
    sceVu0TransMatrix(view, view, cam);
    sceVu0InversMatrix(view, view);
    if ((GRef<u32>(cam + 0x198) & 1) != 0) {
        const f32 sx = static_cast<f32>(GRef<s16>(cam + 0x18C)) / GRef<f32>(cam + 0x160);
        const f32 sy = static_cast<f32>(GRef<s16>(cam + 0x18E)) / GRef<f32>(cam + 0x164);
        GRef<f32>(cam + 0x180) = sx;
        GRef<f32>(cam + 0x184) = sy;
    }
    const u32 screen = cam + 0x60;
    const u32 clip = cam + 0xA0;
    sceVu0UnitMatrix(screen);
    const f32 farz = GRef<f32>(cam + 0x170) * F(0x00410400);
    sceVu0ViewScreenMatrix(screen, GRef<f32>(cam + 0x168), GRef<f32>(cam + 0x180), GRef<f32>(cam + 0x184),
                           GRef<f32>(cam + 0x178), GRef<f32>(cam + 0x17C), GRef<f32>(cam + 0x190),
                           GRef<f32>(cam + 0x194), GRef<f32>(cam + 0x16C), farz);
    BuildClipMatrix(clip, fpu::Add(GRef<f32>(cam + 0x160), GRef<f32>(cam + 0x160)),
                    fpu::Add(GRef<f32>(cam + 0x164), GRef<f32>(cam + 0x164)), GRef<f32>(cam + 0x168),
                    GRef<f32>(cam + 0x16C), GRef<f32>(cam + 0x170));
    sceVu0MulMatrix(cam + 0xE0, screen, view);
    sceVu0MulMatrix(cam + 0x120, clip, view);
    const s64 x = GRef<s16>(cam + 0x188);
    const s64 y = GRef<s16>(cam + 0x18A);
    const s64 x1 = static_cast<s32>(x + GRef<s16>(cam + 0x18C) - 1);
    const s64 y1 = static_cast<s32>(y + GRef<s16>(cam + 0x18E) - 1);
    const u64 scissor = static_cast<u64>(x) | static_cast<u64>(y1) << 48 | static_cast<u64>(y) << 32 |
                        static_cast<u64>(x1) << 16;
    GRef<u64>(cam + 0x1A0) = scissor;
    GRef<u64>(cam + 0x1B0) = scissor;
}

// EE: 0x00246EB8
// Results map camera: 2D camera; the map centre = position of the shown player's display (game[0x2D]
// == 1: display 1, else 0) into state +0; rotation 0x003B01B0; position = centre + rotation * (0, 0,
// -*0x004114E4); then the matrices (0x00246C08).
void func_00246EB8(u32 cam) {
    guest::GuestFrame frame(0x60);
    const u32 centre = frame.Address();
    const u32 m = frame.Address(0x10);
    const u32 off = frame.Address(0x50);
    std::memset(&GRef<u8>(centre), 0, 0x10);
    GRef<f32>(centre + 0xC) = 1.0f;
    Init2dCamera(cam);
    sceVu0CopyVector(centre, W(St() + 0x18) + (Game(0x2D) == 1 ? 0x160u : 0u) + 0x70);
    sceVu0CopyVector(St(), centre);
    sceVu0CopyVector(cam + 0x10, 0x003B01B0);
    GRef<f32>(off + 0xC) = 1.0f;
    GRef<u32>(off) = 0;
    GRef<u32>(off + 4) = 0;
    GRef<f32>(off + 8) = -F(0x004114E4);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, cam + 0x10);
    sceVu0ApplyMatrix(off, m, off);
    sceVu0AddVector(cam, centre, off);
    func_00246C08(cam);
}

// EE: 0x00249118
// Results background pass into bucket 0: the context's draw environment entries (ctx+0xD0, +0xE0,
// +0x100, +0xF0), TEST_1 0x30000, a black sprite with alpha 0 over the viewport (sets the depth), then
// ZBUF_1 again with ZMSK set.
void func_00249118(u32 cam, u32 list) {
    const u32 p = BeginPacket();
    const u32 ctx = CtxAddr();
    PutDirect(p, 6);
    PutGifTag(p + 0x10, 0x8005, 0x10000000, 0xE);
    CopyQ(p + 0x20, ctx + 0xD0);
    CopyQ(p + 0x30, ctx + 0xE0);
    CopyQ(p + 0x40, ctx + 0x100);
    CopyQ(p + 0x50, ctx + 0xF0);
    Pw(p + 0x60, 0x30000);
    Pw(p + 0x68, 0x47);
    PutDirect(p + 0x70, 3);
    PutGifTag(p + 0x80, 0x8001, 0x44000000, 0x4410);
    GRef<u64>(p + 0x90) = 6;
    const s32 w = GRef<s16>(cam + 0x18C);
    const s32 h = GRef<s16>(cam + 0x18E);
    const f32 hw = static_cast<f32>(w / 2);
    const f32 hh = static_cast<f32>(h / 2);
    GRef<f32>(p + 0x9C) = 1.0f;
    Pw(p + 0x98, 0);
    const f32 cx = GRef<f32>(cam + 0x178);
    const f32 cy = GRef<f32>(cam + 0x17C);
    const s32 x0 = fpu::FloatToInt(fpu::Sub(cx, hw));
    const s32 y0 = fpu::FloatToInt(fpu::Sub(cy, hh));
    const s32 x1 = fpu::FloatToInt(fpu::Add(cx, hw));
    const s32 y1 = fpu::FloatToInt(fpu::Add(cy, hh));
    Pw(p + 0xA4, 0);
    Pw(p + 0xA0, static_cast<u32>(x0) << 4 | static_cast<u32>(y0) << 20);
    Pw(p + 0xA8, static_cast<u32>(x1) << 4 | static_cast<u32>(y1) << 20);
    Pw(p + 0xAC, 0);
    PutDirect(p + 0xB0, 2);
    PutGifTag(p + 0xC0, 0x8001, 0x10000000, 0xE);
    CopyQ(p + 0xD0, ctx + 0xE0);
    Pw(p + 0xD4, 1);
    EndPacket(list, 0, p + 0xE0);
}

// EE: 0x00249368
// Map grid: 64 x 64 crossing points every 2048 units around the map centre (cell of state +0 / +8,
// rounded down); each point inside the frustum (world->clip) gets lines to the previous point of its
// row and of its column when those were inside; alpha (K 0x00410428 - w) x K 0x0041042C clamped to
// 0-128, colour 0x181818, ALPHA_1 0x48.
void func_00249368(u32 cam, u32 list) {
    if ((StB(0x40) & 0x20) == 0) return;
    guest::GuestFrame frame(0x4D0);
    const u32 sp = frame.Address();
    const u32 world = sp + 0x10;
    const u32 screen = sp + 0x20;
    const u32 fixed = sp + 0x30;
    const u32 prev = sp + 0x40;      // 64 ftoi4 points of the previous row
    const u32 outside = sp + 0x440;  // 64 flags
    const u32 alpha = sp + 0x480;    // 64 alphas
    const f32 fx = GRef<f32>(St()) * 0.00048828125f;
    const f32 fz = GRef<f32>(St() + 8) * 0.00048828125f;
    GRef<f32>(sp) = F(0x00410428);
    GRef<f32>(sp + 4) = F(0x0041042C);
    u32 p = BeginPacket();
    s32 cx;
    if (fx < 0.0f)
        cx = static_cast<s16>(fpu::FloatToInt(fx)) - 1;
    else
        cx = static_cast<s16>(fpu::FloatToInt(fx));
    s32 cz = static_cast<s16>(fpu::FloatToInt(fz));
    if (fz < 0.0f) cz -= 1;
    const s32 z_end = cz + 0x20;
    for (s32 k = 0x3F; k >= 0; --k) GRef<u8>(outside + static_cast<u32>(k)) = 1;
    PutDirect(p, 2);
    PutGifTag(p + 0x10, 0x8001, 0x10000000, 0xE);
    Pw(p + 0x20, 0x48);
    Pw(p + 0x28, 0x42);
    Pw(p + 0x24, 0);
    Pw(p + 0x2C, 0);
    GRef<f32>(world + 0xC) = 1.0f;
    p += 0x30;
    GRef<u32>(world + 4) = 0;
    for (s32 z = cz - 0x20; z < z_end; ++z) {
        const s32 x_end = cx + 0x20;
        const f32 zf = static_cast<f32>(z);
        u32 col = 0;
        for (s32 x = cx - 0x20; x < x_end; ++x, ++col) {
            GRef<f32>(world + 8) = zf * 2048.0f;
            GRef<f32>(world) = static_cast<f32>(x) * 2048.0f;
            if (ClipBits(Transform(cam + 0x120, LoadV(world))) != 0) {
                GRef<u8>(outside + col) = 1;
                continue;
            }
            sceVu0ApplyMatrix(screen, cam + 0xE0, world);
            const f32 q = 1.0f / GRef<f32>(screen + 0xC);
            s32 a = fpu::FloatToInt(fpu::Sub(GRef<f32>(sp), GRef<f32>(screen + 0xC)) * GRef<f32>(sp + 4));
            if (!(-1 < a)) a = 0;
            if (!(a < 0x81)) a = 0x80;
            const u32 a8 = static_cast<u32>(a) & 0xFF;
            sceVu0ScaleVector(screen, screen, q);
            sceVu0FTOI4Vector(fixed, screen);
            if (0 < static_cast<s32>(col) && GRef<u8>(outside + col - 1) == 0)
                p = PutGridLine(p, a8, fixed, GRef<u8>(alpha + col - 1), prev + (col - 1) * 0x10);
            if (GRef<u8>(outside + col) == 0) p = PutGridLine(p, a8, fixed, GRef<u8>(alpha + col), prev + col * 0x10);
            GRef<u8>(outside + col) = 0;
            GRef<u8>(alpha + col) = static_cast<u8>(a8);
            CopyQ(prev + col * 0x10, fixed);
        }
    }
    EndPacket(list, 0, p);
}

// EE: 0x00249908
// Kill markers on the map: for every display with a running marker (+0x153 bit 2) whose position is
// inside the frustum, an X of two lines (points 0x00409C60-0x00409C90 around the marker, RGBAQ
// 0x003B00E0).
void func_00249908(u32 cam, u32 list) {
    if ((StB(0x40) & 0x20) == 0) return;
    guest::GuestFrame frame(0xC0);
    const u32 sp = frame.Address();
    const u32 m = sp + 0x80;
    u32 p = PutMapDrawHeader(BeginPacket());
    PutDirect(p, 2);
    PutGifTag(p + 0x10, 0x8001, 0x10000000, 0xE);
    Pw(p + 0x20, 0x48);
    Pw(p + 0x28, 0x42);
    Pw(p + 0x24, 0);
    Pw(p + 0x2C, 0);
    p += 0x30;
    for (s32 k = 0; k < UnitRecs(); ++k) {
        const u32 d = Display(k);
        if ((GRef<u8>(d + 0x153) & 4) == 0) continue;
        const u32 pos = W(d + 4) + W(d + 0x154) * 0x20 + 0x10;
        if (ClipBits(Transform(cam + 0x120, LoadV(pos))) != 0) continue;
        for (u32 n = 0; n < 0x40; n += 4) GRef<u32>(sp + n) = W(0x00409C60 + n);
        sceVu0UnitMatrix(m);
        sceVu0TransMatrix(m, m, pos);
        sceVu0MulMatrix(m, cam + 0xE0, m);
        for (u32 n = 0; n < 4; ++n) sceVu0ApplyMatrix(sp + n * 0x10, m, sp + n * 0x10);
        for (u32 n = 0; n < 4; ++n) sceVu0ScaleVector(sp + n * 0x10, sp + n * 0x10, 1.0f / GRef<f32>(sp + n * 0x10 + 0xC));
        for (u32 n = 0; n < 4; ++n) sceVu0FTOI4Vector(sp + 0x40 + n * 0x10, sp + n * 0x10);
        for (u32 half = 0; half < 2; ++half) {
            PutDirect(p, 3);
            PutGifTag(p + 0x10, 0x8001, 0x44000000, 0x4410);
            GRef<u64>(p + 0x20) = 1;
            GRef<u64>(p + 0x28) = GRef<u64>(0x003B00E0);
            PutXyz(p + 0x30, sp + 0x40 + half * 0x10);
            PutXyz(p + 0x38, sp + 0x70 - half * 0x10);
            p += 0x40;
        }
    }
    PutMapDrawTrailer(list, p);
}

// EE: 0x00249E60
// Ground units (+0x153 bits 0x20 and 0x10, not hidden; track record 0x11): a box (corners
// 0x003B0010-0x003B0080 at the unit's position from its record +8, colours of func_00246E50 by unit and group): top quad
// and a strip around the sides; skipped when the position is outside the frustum.
void func_00249E60(u32 cam, u32 list) {
    if ((StB(0x40) & 0x20) == 0) return;
    guest::GuestFrame frame(0x1C0);
    const u32 sp = frame.Address();
    const u32 unit_m = sp + 0x100;
    const u32 clip_m = sp + 0x140;
    const u32 screen_m = sp + 0x180;
    u32 p = PutMapDrawHeader(BeginPacket());
    SetUnitMatrixVu0(unit_m);
    for (s32 k = 0; k < UnitRecs(); ++k) {
        const u8 df = GRef<u8>(Display(k) + 0x153);
        const u32 u = UnitRec(k);
        if ((df & 0x20) == 0 || (df & 0x10) == 0 || (df & 2) != 0) continue;
        GRef<f32>(unit_m + 0x30) = static_cast<f32>(GRef<s16>(u + 8)) * 8.0f;
        GRef<f32>(unit_m + 0x34) = static_cast<f32>(GRef<s16>(u + 0xA)) * 8.0f;
        GRef<f32>(unit_m + 0x38) = static_cast<f32>(GRef<s16>(u + 0xC)) * 8.0f;
        sceVu0MulMatrix(clip_m, cam + 0x120, unit_m);
        if (ClipBits(Transform(clip_m, {0.0f, 0.0f, 0.0f, 1.0f})) != 0) continue;
        const u32 colors = func_00246E50(k, GRef<u8>(u + 2));
        sceVu0MulMatrix(screen_m, cam + 0xE0, unit_m);
        for (u32 n = 0; n < 8; ++n) {
            const u32 v = sp + n * 0x10;
            sceVu0ApplyMatrix(v, screen_m, 0x003B0010 + n * 0x10);
            sceVu0ScaleVector(v, v, 1.0f / GRef<f32>(v + 0xC));
            sceVu0FTOI4Vector(sp + 0x80 + n * 0x10, v);
        }
        const auto pt = [&](u32 n) { return sp + 0x80 + n * 0x10; };
        PutDirect(p, 4);
        PutGifTag(p + 0x10, 1, 0x64000000, 0x00444410);
        GRef<u64>(p + 0x20) = 4;
        GRef<u64>(p + 0x28) = GRef<u64>(colors);
        for (u32 n = 0; n < 4; ++n) PutXyz(p + 0x30 + n * 8, pt(n));
        PutDirect(p + 0x50, 0xD);
        PutGifTag(p + 0x60, 1, 0x24000000, 0xF0);
        GRef<u64>(p + 0x70) = 0xC;
        PutGifTag(p + 0x80, 0x800A, 0x24000000, 0x41);
        static constexpr u32 kSides[10] = {0, 4, 1, 5, 3, 7, 2, 6, 0, 4};
        for (u32 n = 0; n < 10; ++n) {
            GRef<u64>(p + 0x90 + n * 0x10) = GRef<u64>(colors + (n & 1) * 8);
            PutXyz(p + 0x98 + n * 0x10, pt(kSides[n]));
        }
        p += 0x130;
    }
    PutMapDrawTrailer(list, p);
}

// EE: 0x0024A510
// Units on the map: for every display in the block (+0x152 set, +0x153 bit 0x10, not a ground unit) the
// unit shape (6 points 0x003B00F0-0x003B0140 by its matrix: two triangles and a side strip, colours of
// func_00246E50 by unit and group) when all points are inside the frustum, then its trail (from the
// newest frame back to the tail) as strips between consecutive frames whose 4 points are inside.
void func_0024A510(u32 cam, u32 list) {
    if ((StB(0x40) & 0x20) == 0) return;
    guest::GuestFrame frame(0x1A0);
    const u32 sp = frame.Address();
    const u32 m = sp + 0x160;
    u32 p = PutMapDrawHeader(BeginPacket());
    const u32 screen_m = cam + 0xE0;
    const u32 clip_m = cam + 0x120;
    for (s32 k = 0; k < UnitRecs(); ++k) {
        const u32 d = Display(k);
        if (GRef<u8>(d + 0x152) == 0xFF) continue;
        const u8 df = GRef<u8>(d + 0x153);
        if ((df & 0x20) != 0 || (df & 0x10) == 0) continue;
        const u32 colors = func_00246E50(k, GRef<u8>(d + 0x151));
        sceVu0MulMatrix(m, screen_m, d + 0x10);
        for (u32 n = 0; n < 6; ++n) sceVu0ApplyMatrix(sp + n * 0x10, m, 0x003B00F0 + n * 0x10);
        sceVu0MulMatrix(m, clip_m, d + 0x10);
        for (u32 n = 0; n < 6; ++n) sceVu0ApplyMatrix(sp + 0x60 + n * 0x10, m, 0x003B00F0 + n * 0x10);
        u32 clip = 0;
        for (u32 n = 0; n < 6; ++n) clip |= ClipBits(LoadV(sp + 0x60 + n * 0x10));
        if (clip == 0) {
            for (u32 n = 0; n < 6; ++n) sceVu0ScaleVector(sp + n * 0x10, sp + n * 0x10, 1.0f / GRef<f32>(sp + n * 0x10 + 0xC));
            for (u32 n = 0; n < 6; ++n) sceVu0FTOI4Vector(sp + 0xC0 + n * 0x10, sp + n * 0x10);
            const auto pt = [&](u32 n) { return sp + 0xC0 + n * 0x10; };
            PutDirect(p, 4);
            PutGifTag(p + 0x10, 1, 0x64000000, 0x00F44410);
            GRef<u64>(p + 0x20) = 3;
            GRef<u64>(p + 0x28) = GRef<u64>(colors);
            for (u32 n = 0; n < 3; ++n) PutXyz(p + 0x30 + n * 8, pt(n));
            PutDirect(p + 0x50, 4);
            PutGifTag(p + 0x60, 1, 0x54000000, 0x00044410);
            GRef<u64>(p + 0x70) = 3;
            GRef<u64>(p + 0x78) = GRef<u64>(colors + 8);
            for (u32 n = 0; n < 3; ++n) PutXyz(p + 0x80 + n * 8, pt(3 + n));
            PutDirect(p + 0xA0, 9);
            PutGifTag(p + 0xB0, 1, 0x14000000, 0);
            GRef<u64>(p + 0xC0) = 0xC;
            PutGifTag(p + 0xD0, 0x8006, 0x24000000, 0x41);
            static constexpr u32 kSides[6] = {0, 3, 1, 4, 2, 5};
            for (u32 n = 0; n < 6; ++n) {
                GRef<u64>(p + 0xE0 + n * 0x10) = GRef<u64>(colors + (n & 1) * 8);
                PutXyz(p + 0xE8 + n * 0x10, pt(kSides[n]));
            }
            p += 0x140;
        }
        // Trail: VU0 macro transform, vdiv / vmulq by w and vftoi4.
        const u32 trail = W(d);
        s32 head = GRef<s16>(trail + 0x4000);
        if (head == GRef<s16>(trail + 0x4002)) continue;
        u32 prev_clip = 0;
        bool first = true;
        do {
            const u32 f = trail + static_cast<u32>(head) * 0x40;
            u32 cur_clip = 0;
            for (u32 n = 0; n < 4; ++n) cur_clip |= ClipBits(Transform(clip_m, LoadV(f + n * 0x10)));
            const u32 any = prev_clip | cur_clip;
            prev_clip = cur_clip;
            if (any != 0) {
                first = true;
            } else {
                const u32 cur = sp + 0xC0;
                const u32 last = sp + 0x120;
                for (u32 n = 0; n < 4; ++n) {
                    const vu0::Vec4 v = Transform(screen_m, LoadV(f + n * 0x10));
                    const f32 q = vu0::Div(1.0f, v.w);
                    GRef<s32>(cur + n * 0x10) = vu0::FloatToInt4(vu0::Mul(v.x, q));
                    GRef<s32>(cur + n * 0x10 + 4) = vu0::FloatToInt4(vu0::Mul(v.y, q));
                    GRef<s32>(cur + n * 0x10 + 8) = vu0::FloatToInt4(vu0::Mul(v.z, q));
                    GRef<s32>(cur + n * 0x10 + 0xC) = vu0::FloatToInt4(vu0::Mul(v.w, q));
                }
                if (!first) {
                    PutDirect(p, 0xB);
                    PutGifTag(p + 0x10, 1, 0x14000000, 0);
                    GRef<u64>(p + 0x20) = 0xC;
                    PutGifTag(p + 0x30, 0x8005, 0x34000000, 0x441);
                    static constexpr u32 kStrip[5] = {0, 1, 2, 3, 0};
                    for (u32 n = 0; n < 5; ++n) {
                        GRef<u64>(p + 0x40 + n * 0x18) = GRef<u64>(colors + (n == 2 || n == 3 ? 8u : 0u));
                        PutXyz(p + 0x48 + n * 0x18, cur + kStrip[n] * 0x10);
                        PutXyz(p + 0x50 + n * 0x18, last + kStrip[n] * 0x10);
                    }
                    p += 0xC0;
                }
                for (u32 n = 0; n < 4; ++n) CopyQ(last + n * 0x10, cur + n * 0x10);
                first = false;
            }
            head = static_cast<s16>(head - 1);
            if (!(-1 < head)) head = 0xFF;
        } while (head != GRef<s16>(trail + 0x4002));
    }
    PutMapDrawTrailer(list, p);
}

// EE: 0x0024B1E8
// Formats a frame count as minutes'seconds"hundredths ("%02d'%02d\"%02d", 0x00409CA0); `add` is
// added to the hundredths.
void func_0024B1E8(u32 buf, s32 frames, s32 add) {
    const u64 args[3] = {Arg(frames / 3600), Arg((frames % 3600) / 60), Arg((frames % 60) * 100 / 60 + add)};
    ee::sprintf(buf, 0x00409CA0, args, 3);
}

// EE: 0x0022A130
// DrawMenuText with every character shown (count 1024), only while a text layer is open.
void func_0022A130(s32 x, s32 y, s64 style, u32 str) {
    if (W(0x00410E8C) != 0) DrawMenuText(1024.0f, x, y, style, str);
}

namespace {

// One tally line: label, "X%03d" count at x 0x1980 and "%4d" score at x 0x2220.
void DrawTallyLine(f32 t, s32 y, u32 label, u32 count, u32 score, u32 buf) {
    DrawMenuText(t, 0x12B0, y, 3, label);
    const u64 c[1] = {Arg(count)};
    ee::sprintf(buf, 0x00411500, c, 1);
    DrawMenuText(t, 0x1980, y, 3, buf);
    const u64 s[1] = {Arg(score)};
    ee::sprintf(buf, 0x00411508, s, 1);
    DrawMenuText(t, 0x2220, y, 6, buf);
}

// Versus tally of one player (tally `tl`): air, ground + ships, the other player's aircraft, total.
void DrawVersusTally(f32 t, u32 tl, s32 y0, u32 other_label, u32 buf) {
    u32 e = W(tl);
    if (GRef<u16>(e + 4) != 0) DrawTallyLine(t, y0 + 0x140, 0x004114F8, GRef<u8>(e + 1), GRef<u16>(e + 4), buf);
    e = W(tl);
    const u32 g = e + 6;
    const u32 s = e + 0xC;
    if (0 < static_cast<s32>(GRef<u16>(g + 4) + GRef<u16>(s + 4))) {
        DrawTallyLine(t, y0 + 0x280, 0x00409CC0, static_cast<u32>(GRef<u8>(g + 1)) + GRef<u8>(s + 1),
                      static_cast<u32>(GRef<u16>(g + 4)) + GRef<u16>(s + 4), buf);
    }
    e = W(tl);
    const u32 o = e + 0x12;
    if (GRef<u16>(o + 4) != 0) DrawTallyLine(t, y0 + 0x3C0, other_label, GRef<u8>(o + 1), GRef<u16>(o + 4), buf);
    DrawMenuText(t, 0x12B0, y0 + 0x540, 3, 0x00409CD0);
    const u64 a[1] = {Arg(GRef<u16>(tl + 4))};
    ee::sprintf(buf, 0x00411508, a, 1);
    DrawMenuText(t, 0x2220, y0 + 0x540, 6, buf);
}

// "~" placed left of a right-aligned amount: x = (`right` - strlen("%d") x 12) x 16.
void DrawAmountMark(f32 t, s32 right, s32 y, s32 amount, u32 buf) {
    const u64 a[1] = {Arg(amount)};
    ee::sprintf(buf, 0x00411540, a, 1);
    const s32 len = static_cast<s32>(std::strlen(reinterpret_cast<const char*>(&GRef<u8>(buf))));
    DrawMenuText(t, (right - len * 0xC) << 4, y, 3, 0x00411548);
}

}  // namespace

// EE: 0x0024B248
// Results text page (bucket of `list` 0, hidden while closing or with the view held), three timed
// columns ((timer - 2), (timer - K 0x00410430), (timer - K 0x00410434)) x 10 characters:
// - mode 2 (versus): VERSUS, PLAYER 01 / PLAYER 02 tallies;
// - otherwise the aircraft list (names 0x002B7008 by group), GRND TGT, SHIP and TOTAL POINTS; then
//   mode 0: MISSION nn, POINTS points / first threshold, REWARD, rank name (0x003AFFC8), RANK BONUS
//   (0x003AFFE0 by difficulty and rank x 100), TOTAL REWARD, TOTAL CREDITS (credits + reward + bonus,
//   capped at 999,999,999); mode 3: TRIAL MISSION nn and POINTS against the target (mission 4:
//   2400, 5: 750, 1-3: the first threshold, else 0); mode 1: MISSION nn and POINTS; modes 1 / 3
//   show "----" for the rewards;
// - every mode: TIME and OBJECTIVE 1 / 2 (frames from the record +0 / +0x10 / +0x14, limited to the
//   elapsed track time while the tracks run); OBJECTIVE 2 only with record +0x18 bit 0.
void func_0024B248(u32 cam, u32 list) {
    (void)cam;
    const u8 flags = StB(0x40);
    if ((flags & 2) != 0 || (flags & 0x40) != 0) return;
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const f32 timer = GRef<f32>(St() + 0x3C);
    const f32 a = fpu::Sub(timer, 2.0f);
    const f32 b = fpu::Sub(timer, F(0x00410430));
    const f32 c = fpu::Sub(timer, F(0x00410434));
    const f32 left = a * 10.0f;
    const f32 right = b * 10.0f;
    const f32 right2 = c * 10.0f;
    BeginMenuTextLayer(list, 0);
    if (StB(0x41) == 2) {
        DrawMenuText(left, 0x300, 0x490, 3, 0x004114F0);
        DrawMenuText(right, 0x12B0, 0x490, 3, 0x00409CB0);
        DrawVersusTally(right, W(St() + 0x10), 0x490, 0x00411510, buf);
        DrawMenuText(right2, 0x12B0, 0xC10, 3, 0x00409CE0);
        DrawVersusTally(right2, W(St() + 0x10) + 8, 0xC10, 0x00411518, buf);
    } else {
        for (s32 n = 0; n < StB(0x2F); ++n) {
            const u32 e = W(St() + 0x1C) + static_cast<u32>(n) * 6;
            const s32 y = (n * 0x14 + 0x49) << 4;
            func_0022A130(0x12B0, y, 3, W(0x002B7008 + GRef<u8>(e + 2) * 4u));
            const u64 cnt[1] = {Arg(GRef<u8>(e + 1))};
            ee::sprintf(buf, 0x00411500, cnt, 1);
            func_0022A130(0x1980, y, 3, buf);
            const u64 sc[1] = {Arg(GRef<u16>(e + 4))};
            ee::sprintf(buf, 0x00411508, sc, 1);
            func_0022A130(0x2220, y, 6, buf);
        }
        const u32 tl = W(St() + 0x10);
        u32 e = W(tl);
        if (GRef<u16>(e + 0xA) != 0) DrawTallyLine(right, 0xE90, 0x00409CC0, GRef<u8>(e + 7), GRef<u16>(e + 0xA), buf);
        e = W(tl);
        if (GRef<u16>(e + 0x10) != 0) DrawTallyLine(right, 0xFD0, 0x00411520, GRef<u8>(e + 0xD), GRef<u16>(e + 0x10), buf);
        DrawMenuText(right, 0x12B0, 0x1180, 3, 0x00409CD0);
        const u64 pts[1] = {Arg(GRef<u16>(St() + 0x30))};
        ee::sprintf(buf, 0x00411528, pts, 1);
        DrawMenuText(right, 0x2160, 0x1180, 6, buf);
        const u8 mode = StB(0x41);
        if (mode == 0) {
            const u64 mis[1] = {Arg(GRef<s8>(Rec() + 0x19))};
            ee::sprintf(buf, 0x00409CF0, mis, 1);
            DrawMenuText(left, 0x300, 0x490, 3, buf);
            DrawMenuText(left, 0x300, 0x5D0, 3, 0x00411530);
            const u64 pp[2] = {Arg(GRef<u16>(St() + 0x30)), Arg(GRef<s32>(Rec() + 0x1C))};
            ee::sprintf(buf, 0x00409D00, pp, 2);
            DrawMenuText(left, 0xAB0, 0x5D0, 6, buf);
            DrawMenuText(left, 0x300, 0x710, 3, 0x00411538);
            DrawAmountMark(left, 0x109, 0x710, GRef<s32>(St() + 0x34), buf);
            const u64 rw[1] = {Arg(GRef<s32>(St() + 0x34))};
            ee::sprintf(buf, 0x00411550, rw, 1);
            DrawMenuText(left, 0xB70, 0x710, 6, buf);
            DrawMenuText(left, 0x300, 0x850, 3, W(0x003AFFC8 + StB(0x42) * 4u));
            s32 bonus = 0;
            const u8 rank = StB(0x42);
            if (rank != 0) {
                bonus = GRef<s16>(0x003AFFE0 + (static_cast<u32>(rank) - 1) * 2 + Game(0x28) * 8u) * 100;
                DrawMenuText(left, 0x300, 0x990, 3, 0x00409D10);
                DrawAmountMark(left, 0x109, 0x990, bonus, buf);
                const u64 bn[1] = {Arg(bonus)};
                ee::sprintf(buf, 0x00411550, bn, 1);
                DrawMenuText(left, 0xB70, 0x990, 6, buf);
            }
            DrawMenuText(right, 0x12B0, 0x12C0, 3, 0x00409D20);
            DrawAmountMark(right, 0x244, 0x12C0, GRef<s32>(St() + 0x34) + bonus, buf);
            const u64 tr[1] = {Arg(GRef<s32>(St() + 0x34) + bonus)};
            ee::sprintf(buf, 0x00411550, tr, 1);
            DrawMenuText(right, 0x1F20, 0x12C0, 6, buf);
            s32 total = GRef<s32>(St() + 0x38) + GRef<s32>(St() + 0x34) + bonus;
            if (999999999 < total) total = 999999999;
            DrawMenuText(right, 0x12B0, 0x1400, 3, 0x00409D30);
            DrawAmountMark(right, 0x244, 0x1400, total, buf);
            const u64 tc[1] = {Arg(total)};
            ee::sprintf(buf, 0x00411558, tc, 1);
            DrawMenuText(right, 0x1E60, 0x1400, 6, buf);
        } else {
            const s8 mission = GRef<s8>(Rec() + 0x19);
            if (mode == 3) {
                const u64 mis[1] = {Arg(mission)};
                ee::sprintf(buf, 0x00409D40, mis, 1);
                DrawMenuText(left, 0x300, 0x490, 3, buf);
                s32 target = 0;
                if (mission == 4)
                    target = 0x960;
                else if (mission < 5)
                    target = 0 < mission ? GRef<s32>(Rec() + 0x1C) : 0;
                else if (mission == 5)
                    target = 0x2EE;
                DrawMenuText(left, 0x300, 0x5D0, 3, 0x00411530);
                const u64 pp[2] = {Arg(GRef<u16>(St() + 0x30)), Arg(target)};
                ee::sprintf(buf, 0x00409D00, pp, 2);
                DrawMenuText(left, 0xAB0, 0x5D0, 6, buf);
            } else {
                const u64 mis[1] = {Arg(mission)};
                ee::sprintf(buf, 0x00409CF0, mis, 1);
                DrawMenuText(left, 0x300, 0x490, 3, buf);
                DrawMenuText(left, 0x300, 0x5D0, 3, 0x00411530);
                const u64 pp[2] = {Arg(GRef<u16>(St() + 0x30)), Arg(GRef<s32>(Rec() + 0x1C))};
                ee::sprintf(buf, 0x00409D00, pp, 2);
                DrawMenuText(left, 0xAB0, 0x5D0, 6, buf);
            }
            DrawMenuText(left, 0x300, 0x710, 3, 0x00411538);
            DrawMenuText(left, 0xEF0, 0x710, 6, 0x00411560);
            DrawMenuText(left, 0x300, 0x850, 3, W(0x003AFFC8 + StB(0x42) * 4u));
            DrawMenuText(right, 0x12B0, 0x12C0, 3, 0x00411538);
            DrawMenuText(right, 0x22A0, 0x12C0, 6, 0x00411560);
            DrawMenuText(right, 0x12B0, 0x1400, 3, 0x00409D30);
            DrawMenuText(right, 0x22A0, 0x1400, 6, 0x00411560);
        }
    }
    // Times.
    const bool running = (StB(0x40) & 4) != 0;
    u32 t = W(Rec());
    if (running) {
        t = W(St() + 0x24) * 6;
        if (W(Rec()) < t) t = W(Rec());
    }
    func_0024B1E8(buf, static_cast<s32>(t), 0);
    DrawMenuText(left, 0x300, 0x1400, 3, 0x00411568);
    DrawMenuText(left, 0xC10, 0x1400, 3, buf);
    if (StB(0x40) & 4) {
        t = W(St() + 0x24) * 6;
        if (W(Rec()) < t) t = W(Rec());
        if (W(Rec() + 0x10) < t) t = W(Rec() + 0x10);
    } else {
        t = W(Rec() + 0x10);
    }
    func_0024B1E8(buf, static_cast<s32>(t), 0);
    DrawMenuText(left, 0x300, 0x1180, 3, 0x00409D58);
    DrawMenuText(left, 0xC10, 0x1180, 3, buf);
    if (StB(0x40) & 4) {
        t = W(St() + 0x24) * 6;
        if (W(Rec()) < t)
            t = W(Rec() + 0x14);
        else
            t = W(Rec() + 0x10) < t ? t - W(Rec() + 0x10) : 0;
    } else {
        t = W(Rec() + 0x14);
    }
    DrawMenuText(left, 0x300, 0x12C0, 3, 0x00409D68);
    const u32 r = Rec();
    if ((GRef<u8>(r + 0x18) & 1) != 0) {
        // The hundredths of objective 2 move by one when the objective times' hundredths do not add
        // up to the mission time's last digit.
        const u32 h1 = (W(r + 0x10) % 60) * 100 / 60;
        const u32 h2 = (W(r + 0x14) % 60) * 100 / 60;
        const u32 h0 = (W(r) % 60) * 100 / 60;
        const s32 fix = static_cast<s32>(h1 + h2) % 10 != static_cast<s32>(h0) % 10 ? 1 : 0;
        func_0024B1E8(buf, static_cast<s32>(t), fix);
        DrawMenuText(left, 0xC10, 0x12C0, 3, buf);
    } else {
        DrawMenuText(left, 0xEF0, 0x12C0, 3, 0x00411560);
    }
    EndMenuTextLayer();
}

// EE: 0x0024C178
// Frame outlines and item markers of the results layout of the current mode (state +0x14 + mode x
// 0xC), each at (timer - its start time); not while the view is held.
void func_0024C178(u32 cam, u32 list) {
    (void)cam;
    const u32 st = St();
    if ((GRef<u8>(st + 0x40) & 0x40) != 0) return;
    guest::GuestFrame frame(0x80);
    const u32 times = frame.Address();
    const u32 layout = W(st + 0x14) + GRef<u8>(st + 0x41) * 0xCu;
    const s32 rects = GRef<s8>(layout + 8);
    const s32 items = GRef<s8>(layout + 9);
    const u32 rect_tab = W(layout);
    const u32 item_tab = W(layout + 4);
    for (s32 n = 0; n < rects; ++n) {
        GRef<f32>(times + 4u * static_cast<u32>(n)) =
            fpu::Sub(GRef<f32>(st + 0x3C), GRef<f32>(rect_tab + 0xC + 0x10u * static_cast<u32>(n)));
    }
    DrawAllFrameOutlines(list, 0, rects, rect_tab, times);
    for (s32 n = 0; n < items; ++n) {
        GRef<f32>(times + 4u * static_cast<u32>(n)) =
            fpu::Sub(GRef<f32>(St() + 0x3C), GRef<f32>(item_tab + 0x14 + 0x18u * static_cast<u32>(n)));
    }
    func_00246A90(list, 0, items, item_tab, times);
}

// EE: 0x0024C278
// Results screen draw: background, frame filter, map grid, ground units, units and trails, kill
// markers, text page, layout frames, tabs, debriefing window.
void func_0024C278(u32 cam, u32 list) {
    func_00249118(cam, list);
    func_00227F28(1.0f, list, 0);
    func_00249368(cam, list);
    func_00249E60(cam, list);
    func_0024A510(cam, list);
    func_00249908(cam, list);
    func_0024B248(cam, list);
    func_0024C178(cam, list);
    DrawMenuTabs(list, 0, kTabLevels);
    DrawMessageWindow(Win(), cam, list);
}

// EE: 0x00227F28
// DrawMenuFrameFilter in mode 0.
void func_00227F28(f32 level, u32 list, s32 index) { DrawMenuFrameFilter(level, list, index, 0); }

// EE: 0x0024C328
// Sub 0: 640 x 224 display (PSMZ16S depth), scene work (0x1F0) and packet buffers, menu tab levels,
// VAG fade, the result state (+0x41 mode; displays with kill markers (0x200) and trails (0x4000 +
// head / tail), tallies, aircraft list, frame layouts 0x003AF9E0 / 0x003AFA20 (modes 0, 1, 3) and
// 0x003AFCD8 / 0x003AFD28 (mode 2)), flags 1 | 4 (opening, tracks running), credits and trial slot.
// Campaign mission 18 skips the screen: every kill counted, the summary written, then the
// post-mission menu; otherwise sub 1.
void InitResultsScene() {
    MainContext* ctx = Ctx();
    ctx->frame_psm = 0;
    ctx->disp_w = 0x280;
    ctx->draw_w = 0x280;
    ctx->disp_h = 0xE0;
    for (u8& c : ctx->clear_rgba) c = 0;
    Ctx()->zbuf_psm = 0x3A;
    Ctx()->ztest = 3;
    Ctx()->draw_mode = 0;
    Ctx()->disp_flags |= 0x8000;
    BuildDrawEnvPackets();
    GRef<u32>(0x00411580) = WorkHeapAlloc(0x1F0, 0x10);
    AllocDoubleBuffer(CtxAddr() + 0x40, 0x40000);
    const u32 buf = SwapDoubleBuffer(CtxAddr() + 0x40);
    Ctx()->cur_buf.addr = buf;
    ClearMenuTabLevels(kTabLevels);
    func_0021FB70(0.0f);
    StopAllVoices();
    func_0021F190(0.0f, 0, 0xFF, 0xFF, 1);
    GRef<u32>(0x0041157C) = WorkHeapAlloc(0x50, 0x10);
    const u32 st = St();
    GRef<u8>(st + 0x41) = GRef<u8>(Rec() + 0xF);
    GRef<u32>(st + 0x24) = 0;
    GRef<u32>(st + 0x20) = W(Rec() + 4);
    GRef<u32>(st + 0x28) = 0;
    GRef<u16>(st + 0x2C) = 0;
    GRef<u32>(St() + 0x18) = WorkHeapAlloc(GRef<u8>(Rec() + 0xE) * 0x160, 0x10);
    GRef<u8>(St() + 0x2E) = 0;
    GRef<u8>(St() + 0x40) = 0;
    GRef<f32>(St() + 0x3C) = F(0x00410438);
    GRef<u16>(St() + 0x30) = 0;
    GRef<u32>(St() + 0x38) = 0;
    GRef<u32>(St() + 0x34) = 0;
    GRef<u8>(St() + 0x42) = 0;
    GRef<u8>(St() + 0x45) = 0;
    for (s32 i = 0; i < UnitRecs(); ++i) {
        const u32 d = W(St() + 0x18) + static_cast<u32>(i) * 0x160;
        GRef<u8>(d + 0x153) = 0;
        GRef<u32>(d + 0x154) = 0;
        const u32 trail = WorkHeapAlloc(0x4010, 0x10);
        GRef<u16>(trail + 0x4002) = 0;
        GRef<u32>(d) = trail;
        GRef<u16>(trail + 0x4000) = 0;
        GRef<u32>(d + 4) = WorkHeapAlloc(0x200, 0x10);
    }
    GRef<u32>(St() + 0x1C) = WorkHeapAlloc(0x30, 4);
    GRef<u8>(St() + 0x2F) = 0;
    GRef<u32>(St() + 0x10) = WorkHeapAlloc(0x18, 4);
    for (u32 i = 0; i < 3; ++i) {
        const u32 t = W(St() + 0x10) + i * 8;
        GRef<u32>(t) = WorkHeapAlloc(0x18, 2);
        GRef<u16>(t + 4) = 0;
        for (u32 k = 0; k < 4; ++k) {
            GRef<u16>(W(t) + k * 6 + 4) = 0;
            GRef<u8>(W(t) + k * 6 + 1) = 0;
        }
    }
    const u32 layouts = WorkHeapAlloc(0x30, 4);
    GRef<u32>(St() + 0x14) = layouts;
    const auto set = [](u32 e, u32 rects, u32 items, u8 nr, u8 ni) {
        GRef<u32>(e) = rects;
        GRef<u32>(e + 4) = items;
        GRef<u8>(e + 8) = nr;
        GRef<u8>(e + 9) = ni;
    };
    set(layouts, 0x003AF9E0, 0x003AFA20, 4, 0x1D);
    set(W(St() + 0x14) + 0xC, 0x003AF9E0, 0x003AFA20, 4, 0x1D);
    set(W(St() + 0x14) + 0x18, 0x003AFCD8, 0x003AFD28, 5, 0x1C);
    set(W(St() + 0x14) + 0x24, 0x003AF9E0, 0x003AFA20, 4, 0x1D);
    GRef<u8>(St() + 0x40) |= 1;
    GRef<u8>(St() + 0x40) |= 4;
    func_00246FC8();
    func_00247540();
    if (GRef<u8>(Rec() + 0xF) == 0 && GRef<s8>(Rec() + 0x19) == 0x12) {
        func_002476F0();
        func_00246FF0();
        EnterPostMissionMenu();
        return;
    }
    RequestSubState(1);
}

// EE: 0x0024C700
// Sub 1: loads the results background (DATA 0x161 in mode 2, 0x162 in mode 3, else 0x14E + mission)
// into scene work +0x1D0.
void LoadResultsData() {
    const u8 mode = GRef<u8>(Rec() + 0xF);
    s32 entry;
    if (mode == 2)
        entry = 0x161;
    else if (mode == 3)
        entry = 0x162;
    else
        entry = GRef<s8>(Rec() + 0x19) + 0x14E;
    GRef<u32>(Work() + 0x1D0) = AllocAndLoadDataEntry(entry);
    RequestSubState(2);
}

// EE: 0x0024C758
// Sub 2: when loaded, the container's item addresses into scene work +0x1D4 (font, text, window
// textures).
void SetupResultsData() {
    if (IsDataLoadDone() == 0) return;
    const u32 w = Work();
    const u32 c = W(w + 0x1D0);
    const s32 n = GRef<s32>(c);
    for (s32 k = 0; k < n; ++k) GRef<u32>(w + 0x1D4 + 4u * static_cast<u32>(k)) = c + W(c + 4 + 4u * static_cast<u32>(k));
    RequestSubState(3);
}

// EE: 0x0024C7C8
// Sub 3: the debriefing message window (0x58 bytes, gp 0x00411850) from the loaded font, text and
// window textures; results BGM 0x26 on slot 0, SE 0xE.
void InitResultsWindow() {
    const u32 w = Work();
    const u32 text = W(w + 0x1D8);
    const u32 gfx = W(w + 0x1DC);
    const u32 font = W(w + 0x1D4);
    const u32 win = WorkHeapAlloc(0x58, 0x10);
    GRef<u32>(0x00411850) = win;
    InitMessageWindow(win, 0);
    LoadMessageWindow(Win(), font, text, gfx);
    func_0021FD40(0, 0x26);
    PlayMenuSe(0, 0xE);
    RequestSubState(5);
}

// EE: 0x0024C850
// Sub 4: the results screen; when closing (bit 1) and the timer is below 0: leaving (bit 0x80), timer
// K 0x0041043C, sub 6.
void RunResultsScene() {
    const u32 w = Work();
    InitBucketList(w, 0x10);
    SetPacketListBuckets(CtxAddr() + 0x60, w);
    UpdateMenuTabLevels(kTabLevels, 1, 0x10);
    ResultsUpdate();
    func_00246EB8(w + 0x10);
    func_0024C278(w + 0x10, w);
    const u32 st = St();
    if ((GRef<u8>(st + 0x40) & 2) == 0) return;
    if (!(GRef<f32>(st + 0x3C) < 0.0f)) return;
    GRef<u8>(st + 0x40) |= 0x80;
    GRef<f32>(St() + 0x3C) = F(0x0041043C);
    RequestSubState(6);
}

// EE: 0x0024C918
// Sub 5: fade in from black (timer - K 0x00410440 per frame, fade = timer / K 0x00410444), then sub 4.
void FadeInResultsScene() {
    const u32 w = Work();
    const u32 cam = w + 0x10;
    InitBucketList(w, 0x10);
    SetPacketListBuckets(CtxAddr() + 0x60, w);
    func_00246EB8(cam);
    UpdateMenuTabLevels(kTabLevels, 1, 0x10);
    func_00249118(cam, w);
    func_00227F28(1.0f, w, 0);
    DrawMenuTabs(w, 0, kTabLevels);
    const f32 t = fpu::Sub(GRef<f32>(St() + 0x3C), F(0x00410440));
    GRef<f32>(St() + 0x3C) = t;
    DrawScreenFade(t / F(0x00410444), w, 0, 0, cam);
    if (!(GRef<f32>(St() + 0x3C) < 0.0f)) return;
    GRef<f32>(St() + 0x3C) = 0.0f;
    RequestSubState(4);
}

// EE: 0x0024CA28
// Sub 6 (ResultsExit): fade out (timer - K 0x00410448, fade (K 0x0041044C - timer) / K 0x0041044C);
// at the end the summary, then by mode: 0 the post-mission menu, 1 menu 0x18, 2 menu 0x1B, 3 menu
// 0x19 (the last three after ApplyMissionResults); the debriefing voice slot (mask 1) stopped.
void ResultsExit() {
    const u32 w = Work();
    const u32 cam = w + 0x10;
    InitBucketList(w, 0x10);
    SetPacketListBuckets(CtxAddr() + 0x60, w);
    UpdateMenuTabLevels(kTabLevels, 1, 0x10);
    func_00249118(cam, w);
    func_00227F28(1.0f, w, 0);
    DrawMenuTabs(w, 0, kTabLevels);
    if ((StB(0x40) & 0x80) == 0) return;
    const f32 t = fpu::Sub(GRef<f32>(St() + 0x3C), F(0x00410448));
    const f32 k = F(0x0041044C);
    GRef<f32>(St() + 0x3C) = t;
    DrawScreenFade(fpu::Sub(k, t) / k, w, 0, 0, cam);
    if (!(GRef<f32>(St() + 0x3C) < 0.0f)) return;
    func_00246FF0();
    switch (StB(0x41)) {
    case 0:
        EnterPostMissionMenu();
        break;
    case 1:
        ApplyMissionResults();
        EnterMenu(0x18);
        break;
    case 2:
        ApplyMissionResults();
        EnterMenu(0x1B);
        break;
    case 3:
        ApplyMissionResults();
        EnterMenu(0x19);
        break;
    default:
        break;
    }
    func_0021FDF8(1);
}

// EE: 0x00246E18
// SceneResults (scene 5): sub-state table 0x003B0190.
void SceneResults() { guest::CallGuest(W(0x003B0190 + 4u * Ctx()->sub)); }

AC04_GUEST_FUNCTION(0x00246E18, SceneResults);
AC04_GUEST_FUNCTION(0x0024C328, InitResultsScene);
AC04_GUEST_FUNCTION(0x0024C700, LoadResultsData);
AC04_GUEST_FUNCTION(0x0024C758, SetupResultsData);
AC04_GUEST_FUNCTION(0x0024C7C8, InitResultsWindow);
AC04_GUEST_FUNCTION(0x0024C850, RunResultsScene);
AC04_GUEST_FUNCTION(0x0024C918, FadeInResultsScene);
AC04_GUEST_FUNCTION(0x0024CA28, ResultsExit);

// EE: 0x00226B78
// After the results of a campaign mission: missions with a story movie (1, 2, 5, 7, 8, 12, 14, 15,
// 17, 18) open the movie page (menu 0x20) with return value 1-5, 7-0xB in ctx+0x14; the others go
// to the post-mission menu (0xB). The mission number is the u16 at 0x002A15B8.
void EnterPostMissionMenu() {
    u16 ret = 0;
    switch (GRef<u16>(0x002A15B8)) {
    case 1: ret = 1; break;
    case 2: ret = 2; break;
    case 5: ret = 3; break;
    case 7: ret = 4; break;
    case 8: ret = 5; break;
    case 12: ret = 7; break;
    case 14: ret = 8; break;
    case 15: ret = 9; break;
    case 17: ret = 0xA; break;
    case 18: ret = 0xB; break;
    default:
        EnterMenu(0xB);
        return;
    }
    Ctx()->ret_sub = ret;
    EnterMenu(0x20);
}

// EE: 0x0024F0D8
// Debriefing window: shows the text (state bit 1, counters cleared) once a multi-page text is
// loaded (+0xB messages, +0xC bit 2), and plays voice part 1 of message +0x3B (when set) if VAG
// slot 4 is idle.
void func_0024F0D8(u32 win) {
    if (GRef<s8>(win + 0xB) == 0) return;
    const u8 state = GRef<u8>(win + 0xC);
    if ((state & 4) == 0) return;
    const u8 id = GRef<u8>(win + 0x3B);
    GRef<u8>(win + 0xC) = state | 2;
    GRef<u16>(win + 0x3C) = 0;
    GRef<u16>(win + 0x3E) = 0;
    GRef<u16>(win + 0x14) = 0;
    GRef<u16>(win + 0xE) = 0;
    GRef<u16>(win + 0x10) = 0;
    if (id == 0xFF) return;
    if (func_00257290() != 0) return;
    func_002572A8(GRef<u8>(win + 0x3B), 1);
}

// EE: 0x0024F158
// Square on the debriefing window: shows the rest of the page at once (typed count = page length + 4)
// unless already shown or on the last page; otherwise advances the typed count by the limit +0x12.
void func_0024F158(u32 win) {
    const u32 text = W(win + 0x18);
    const s8 page = GRef<s8>(win + 0xD);
    const s32 total = GRef<s16>(W(text) + static_cast<u32>(page) * 8 + 6) + 4;
    const u16 typed = GRef<u16>(win + 0x3C);
    if (GRef<s16>(win + 0x3C) < total && page < GRef<s16>(text + 4) - 1) {
        GRef<u16>(win + 0x3C) = static_cast<u16>(total);
        return;
    }
    GRef<u16>(win + 0x3C) = static_cast<u16>(typed + GRef<u16>(win + 0x12));
}

// EE: 0x002507D8
// Stops the debriefing voice (window argument unused).
void func_002507D8(u32 win) {
    (void)win;
    func_00257270();
}

// EE: 0x00257270
// Stops VAG slot 4 (mask 0x10), the debriefing voice.
void func_00257270() { func_0021FDF8(0x10); }

// EE: 0x00257290
// 1 while VAG slot 4 is not idle (slot state 0x00353E7C != 1).
s32 func_00257290() { return W(0x00353E7C) != 1 ? 1 : 0; }

// EE: 0x002572A8
// Debriefing voice `part` of message `id`: when slot 4 is idle (and not part 1 of message 0x12),
// volume 0xFF on slot 4, then stream 1 with clip 0xAAE + id x 2 + part; returns 1 when queued, else
// 0.
s32 func_002572A8(s32 id, s32 part) {
    if (func_00257290() != 0) return 0;
    if (id == 0x12 && part == 1) return 0;
    func_0021F190(0.0f, 4, 0xFF, 0xFF, 1);
    return RequestStreamPlayArg(4, 1, static_cast<u16>(id * 2 + part + 0xAAE)) != 0 ? 1 : 0;
}

}  // namespace ac04::game
