// Mission results into the campaign records (docs/subsystems/results-startup.md): the result
// record `*0x004114D8` is applied once after a flight (bit 0 of +0 = pending).
// Result record: +0 u16 flags (bit 0 pending, bit 1 unlock colour 2 of the mission's aircraft),
// +2 u16 kind (0 campaign, 1 HYPOTHESIS: free mission, 3 trial; others are only cleared),
// +4 u8 rank, +5 u8 mission (1-based), +6 u16 score, +8 u32 credits earned, +0xC u32 time,
// +0x10 u32, +0x14 u32 (totals).
// Records block 0x002A0318: +0x38/+0x3C/+0x40 totals of result +0xC/+0x10/+0x14, +0x44 sortie
// count, +0x48 u32 sorties per aircraft (21) and colour (3), +0x144 mission records per difficulty
// (6 x 18 x 0x14), +0x9A0 trial records (6 x 0x14), +0xA18 credits.
// Mission record (0x14 bytes): +0 u16 best rank, +2 u16 score[3] (highest first), +8 u32 time[3]
// (lowest first).
#include "game/game.h"

#include "common/ee_math.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u32, g_pMissionResult, 0x004114D8)  // gp-0x4518

constexpr u32 kRecords = 0x002A0318;
constexpr u32 kRecordTables = 0x002B7C20;  // +8 stored table, +0xC new table (4-byte entries)

// Totals and the per-aircraft/colour sortie count, shared by every result kind.
void AddResultTotals(u32 r, s32 aircraft_index) {
    GRef<u32>(kRecords + 0x38) += GRef<u32>(r + 0xC);
    GRef<u32>(kRecords + 0x3C) += GRef<u32>(r + 0x10);
    GRef<u32>(kRecords + 0x44) += 1;
    GRef<u32>(kRecords + 0x40) += GRef<u32>(r + 0x14);
    GRef<u32>(kRecords + 0x48 + static_cast<u32>(aircraft_index) * 0xC + g_GameState()[1] * 4u) += 1;
}

}  // namespace

// EE: 0x0017BE38
// HYPOTHESIS: keeps the better of two recordings (trial replay data). Tables at *0x002B7C28
// (stored) and *0x002B7C2C (new), 0x80 entries of {u16, u16} (0x20 for mission id 0x19): for
// mission ids below 0x18 the last valid (not 0xFFFF) second halves are compared and the new table
// is copied when its value is higher; otherwise the first halves, and it is copied when lower.
void func_0017BE38() {
    const u8 id = g_GameState()[0x27];
    s32 n = 0x80;
    if (id != 0x15 && id != 0x16 && id != 0x17 && id != 0x18 && id == 0x19) n = 0x20;
    const u32 stored = GRef<u32>(kRecordTables + 8);
    const u32 fresh = GRef<u32>(kRecordTables + 0xC);
    bool copy;
    if (g_GameState()[0x27] < 0x18) {
        u32 a = 0;
        u32 b = 0;
        for (s32 i = 0; i < n; ++i) {
            const u16 v = GRef<u16>(stored + static_cast<u32>(i) * 4 + 2);
            if (v != 0xFFFF) a = v;
            const u16 w = GRef<u16>(fresh + static_cast<u32>(i) * 4 + 2);
            if (w != 0xFFFF) b = w;
        }
        copy = static_cast<s32>(a) < static_cast<s32>(b);
    } else {
        u32 a = 0xFFFF;
        u32 b = 0xFFFF;
        for (s32 i = 0; i < n; ++i) {
            const u16 v = GRef<u16>(stored + static_cast<u32>(i) * 4);
            if (v != 0xFFFF) a = v;
            const u16 w = GRef<u16>(fresh + static_cast<u32>(i) * 4);
            if (w != 0xFFFF) b = w;
        }
        copy = static_cast<s32>(b) < static_cast<s32>(a);
    }
    if (!copy) return;
    for (s32 i = 0; i < n; ++i) {
        const u32 src = GRef<u32>(kRecordTables + 0xC) + static_cast<u32>(i) * 4;
        const u32 dst = GRef<u32>(kRecordTables + 8) + static_cast<u32>(i) * 4;
        GRef<u16>(dst) = GRef<u16>(src);
        GRef<u16>(dst + 2) = GRef<u16>(src + 2);
    }
}

// EE: 0x002470A0
// Merges the pending result into mission record `rec`: best rank, top-3 scores (highest first)
// and top-3 times (lowest first), all unsigned.
void UpdateMissionRecord(u32 rec) {
    const u32 r = g_pMissionResult();
    const u16 rank = GRef<u8>(r + 4);
    if (GRef<u16>(rec) < rank) GRef<u16>(rec) = rank;
    const u16 score = GRef<u16>(r + 6);
    s32 at = 3;
    if (GRef<u16>(rec + 2) < score) {
        at = 0;
    } else {
        for (s32 i = 1; i < 3; ++i) {
            if (GRef<u16>(rec + 2 + static_cast<u32>(i) * 2) < score) {
                at = i;
                break;
            }
        }
    }
    for (s32 i = 1; !(i < at); --i) GRef<u16>(rec + 2 + static_cast<u32>(i + 1) * 2) = GRef<u16>(rec + 2 + static_cast<u32>(i) * 2);
    if (at != 3) GRef<u16>(rec + 2 + static_cast<u32>(at) * 2) = score;
    const u32 time = GRef<u32>(r + 0xC);
    at = 3;
    if (time < GRef<u32>(rec + 8)) {
        at = 0;
    } else {
        for (s32 i = 1; i < 3; ++i) {
            if (time < GRef<u32>(rec + 8 + static_cast<u32>(i) * 4)) {
                at = i;
                break;
            }
        }
    }
    for (s32 i = 1; !(i < at); --i) GRef<u32>(rec + 8 + static_cast<u32>(i + 1) * 4) = GRef<u32>(rec + 8 + static_cast<u32>(i) * 4);
    if (at != 3) GRef<u32>(rec + 8 + static_cast<u32>(at) * 4) = time;
}

// EE: 0x00247210
// Applies a pending result once: kind 0 (campaign) adds the credits plus the rank bonus (s16 table
// 0x003AFFE0 per difficulty and rank, x100; capped at 999,999,999), kinds 0/1 add the totals,
// update the mission record of the current difficulty and run the unlocks; kind 3 (trial) adds the
// totals, marks the trial as passed (0x002A031C bit `mission` for rank >= 2) and updates the trial
// record and the stored recording. The pending flags are cleared afterwards (not when bit 0 was
// clear).
void ApplyMissionResults() {
    u32 r = g_pMissionResult();
    if (((GRef<u16>(r) ^ 1) & 1) != 0) return;
    s32 aircraft_index = static_cast<s32>(g_GameState()[0]) - 1;
    if (!(-1 < aircraft_index)) aircraft_index = 1;
    if (!(aircraft_index < 0x15)) aircraft_index = 1;
    const u16 kind = GRef<u16>(r + 2);
    if (kind == 1 || kind == 0) {
        if (kind == 0) {
            const u8 rank = GRef<u8>(r + 4);
            s32 bonus = 0;
            if (rank != 0) {
                bonus = GRef<s16>(0x003AFFE0 + (static_cast<u32>(rank) - 1) * 2 + g_GameState()[0x28] * 8u) * 100;
            }
            s32& credits = GRef<s32>(kRecords + 0xA18);
            credits = credits + static_cast<s32>(GRef<u32>(r + 8) + static_cast<u32>(bonus));
            if (999999999 < credits) credits = 999999999;
        }
        AddResultTotals(r, aircraft_index);
        UpdateMissionRecord(kRecords + 0x144 + g_GameState()[0x28] * 0x168u + GRef<u8>(r + 5) * 0x14u - 0x14);
        r = g_pMissionResult();
        if ((GRef<u16>(r) & 2) != 0) {
            UnlockMissionColor2(GRef<u8>(r + 5));
            r = g_pMissionResult();
        }
        UnlockAircraftUpToMission(GRef<u8>(r + 5));
    } else if (kind == 3) {
        AddResultTotals(r, aircraft_index);
        if (!(GRef<u8>(r + 4) < 2) && GRef<u8>(r + 5) < 6) {
            GRef<u32>(kRecords + 4) |= 1u << (GRef<u8>(r + 5) & 0x1F);
        }
        UpdateMissionRecord(kRecords + 0x144 + 0x85C + GRef<u8>(r + 5) * 0x14u);
        func_0017BE38();
    } else if (kind != 2) {
        GRef<u16>(g_pMissionResult()) = 0;
        return;
    }
    GRef<u16>(g_pMissionResult()) = 0;
}

// EE: 0x0024CBE0
// Creates the in-flight result record 0x0042D2C0 (gp 0x00411578): one 0xD0-byte record per unit
// (0x0042D300: model id, placement type, flags, group, score, position / 8 as s16), time limit
// 36000 frames, mode (0 normal, 1 HYPOTHESIS: free mission, 2 two players, 3 trial) and the rank
// table byte +0x19.
void func_0024CBE0() {
    constexpr u32 r = 0x0042D2C0;
    GRef<u32>(r + 8) = 0x0042D300;
    GRef<u32>(0x00411578) = r;
    GRef<u32>(r + 4) = 0x0043A300;
    GRef<u16>(r + 0xC) = 0xFFFF;
    GRef<u8>(r + 0xE) = GRef<u8>(g_UnitList());
    GRef<u8>(r + 0x18) = 0;
    const u32 pair = WorkHeapAlloc(8, 4);
    GRef<u32>(0x00411584) = pair;
    GRef<u32>(pair + 0) = GRef<u32>(r + 4);
    GRef<u32>(pair + 4) = GRef<u32>(r + 4);
    for (u32 i = 0; i < GRef<u8>(r + 0xE); ++i) {
        const u32 u = GRef<u32>(0x002A1F70 + 4 * i);
        const u32 e = GRef<u32>(r + 8) + i * 0xD0;
        GRef<u8>(e + 0) = GRef<u8>(u + 0x69);
        GRef<u8>(e + 2) = GRef<u8>(u + 0x48) & 3;
        GRef<u8>(e + 3) = GRef<u8>(u + 0x65);
        GRef<u16>(e + 6) = GRef<u16>(u + 0x60);
        GRef<u8>(e + 4) = 0;
        GRef<u8>(e + 1) = GRef<u8>(u + 0x6A);
        GRef<u8>(e + 5) = 0;
        if ((GRef<u32>(u + 0x48) & 0x8000000) != 0) GRef<u8>(e + 4) = 1;
        const s32 z = fpu::FloatToInt(GRef<f32>(u + 8) * 0.125f);
        const s32 x = fpu::FloatToInt(GRef<f32>(u + 0) * 0.125f);
        const s32 y = fpu::FloatToInt(GRef<f32>(u + 4) * 0.125f);
        GRef<s16>(e + 0xC) = static_cast<s16>(z);
        GRef<s16>(e + 8) = static_cast<s16>(x);
        GRef<s16>(e + 0xA) = static_cast<s16>(y);
    }
    GRef<u32>(r + 0) = 36000;
    const u8* game = &GRef<u8>(g_GameState().addr);
    const u8 id = game[0x27];
    u8 mode;
    if (game[0x25] == 2) mode = 2;
    else if (static_cast<u8>(id - 0x15) < 5) mode = 3;
    else if (game[0x29] == 2) mode = 1;
    else mode = 0;
    GRef<u8>(r + 0xF) = mode;
    if (mode == 3) {
        u32 k = static_cast<u32>(id) - 0x15;
        if (k >= 5) k = 0;
        GRef<u8>(r + 0x19) = GRef<u8>(0x00411570 + k);
    } else {
        GRef<u8>(r + 0x19) = GRef<u8>(0x003B01C0 + (id < 0x13 ? id : 0u));
    }
}

}  // namespace ac04::game
