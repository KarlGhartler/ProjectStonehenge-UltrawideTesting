// Campaign progress and unlocks (docs/subsystems/frontend.md "Unlocks"): the campaign mission
// counter, the "cleared" progress bits 0x002A0318 and the ownership words 0x002A0320 (six u64:
// colours 0-2, special weapons 0-2; 2 bits per aircraft 1-21: 0 locked, 1 shown, 2 buyable,
// 3 owned). Mission ranks 0x002A045C: u16 rank per difficulty (6 x 0x168 bytes) and campaign
// mission (18 x 0x14 bytes); 4 = S.
#include "game/game.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kProgressFlags = 0x002A0318;
constexpr u32 kOwnership = 0x002A0320;
constexpr u32 kMissionRanks = 0x002A045C;
constexpr u32 kDifficulty = 0x002A15BA;
constexpr u32 kAircraftTable = 0x0039DA80;   // 21 x 0x3C
constexpr u32 kMissionAircraft = 0x00384848; // 18 x {s16 colour-1 aircraft, s16 colour-2 aircraft}

u32& ProgressFlags() { return GRef<u32>(kProgressFlags); }
u16 Rank(u32 difficulty, u32 mission) { return GRef<u16>(kMissionRanks + difficulty * 0x168 + mission * 0x14); }

}  // namespace

// EE: 0x00226400
// Field +8 (s16) of aircraft `aircraft` (1-21) in the aircraft table; -1 out of range.
s16 GetAircraftField8(s32 aircraft) {
    const u32 i = static_cast<u32>(aircraft - 1);
    if (!(i < 0x15)) return -1;
    return GRef<s16>(kAircraftTable + i * 0x3C + 8);
}

// EE: 0x00226430
// Aircraft (1-21, s16 table 0x0039DA50) at list position `n` (0-20, else 0); GetAircraftField8 gives
// the list position of an aircraft.
s16 GetAircraftAtListPosition(u64 n) {
    if (!(n < 0x15)) n = 0;
    return GRef<s16>(0x0039DA50 + static_cast<u32>(n) * 2);
}

// EE: 0x00226500
// Next campaign mission (1-18); after mission 18 the campaign is marked cleared and the counter
// restarts at 0. Also sets the mission id of the game state.
void AdvanceCampaignMission() {
    s32 n = g_MissionNo();
    if (!(n > 0)) {
        n = 1;
    } else if (!(n < 0x13)) {
        n = 0x12;
    }
    n += 1;
    if (!(n < 0x13)) {
        MarkCampaignCleared();
        n = 0;
    }
    g_MissionNo() = static_cast<u16>(n);
    g_GameState()[0x27] = static_cast<u8>(GetMissionId(0, n));
}

// EE: 0x00226580
// Raises the 2-bit ownership state of `aircraft` (1-21) in ownership word `word` (0-2) to `level`
// (1-3); never lowers it.
void RaiseOwnership(s32 aircraft, u64 word, s64 level) {
    if (!(static_cast<u32>(level - 1) < 3)) return;
    if (!(word < 3)) return;
    const u32 i = static_cast<u32>(aircraft - 1);
    if (!(i < 0x15)) return;
    const u32 shift = i * 2;
    u64& own = GRef<u64>(kOwnership + static_cast<u32>(word) * 8);
    const s64 cur = static_cast<s32>((own >> shift) & 3);
    if (!(cur < level)) return;
    own = (own & ~(3ull << shift)) | (static_cast<u64>(level) << shift);
}

// EE: 0x002265F8
// True when every aircraft except `aircraft` has the "buyable" bit (bit 1 of its 2-bit state) in
// ownership word `word` (0-2). Aircraft out of range count as aircraft 1.
bool CheckOtherAircraftBuyable(u64 word, s32 aircraft) {
    u32 i = static_cast<u32>(aircraft - 1);
    if (!(i < 0x16)) i = 0;
    const u64 mask = ~(3ull << (i * 2)) & 0xAAAAAAAAAAull;
    if (!(word < 3)) return false;
    return (GRef<u64>(kOwnership + static_cast<u32>(word) * 8) & mask) == mask;
}

// EE: 0x00226668
// True when each of the 18 campaign missions has an S rank (4) on difficulty `difficulty` or a
// higher one.
bool CheckAllMissionsRankedS(u64 difficulty) {
    if (!(difficulty < 6)) return false;
    const s32 d = static_cast<s32>(difficulty);
    for (u32 m = 0; m < 0x12; ++m) {
        s32 k = d;
        while (Rank(static_cast<u32>(k), m) != 4) {
            ++k;
            if (!(k < 6)) return false;
        }
    }
    return true;
}

// EE: 0x00226710
// Campaign mission `mission` (1-18) completed: colour 2 of its listed aircraft becomes buyable.
void UnlockMissionColor2(s32 mission) {
    const u32 i = static_cast<u32>(mission - 1);
    if (!(i < 0x12)) return;
    RaiseOwnership(GRef<s16>(kMissionAircraft + i * 4 + 2), 2, 2);
}

// EE: 0x00226748
// Unlocks from ranks and clears: after the first clear, on difficulty >= 2, colour 1 of each
// mission's aircraft for ranks above B (>= 3, i.e. A or S); aircraft 18 in colours 1/2 when all
// other aircraft are buyable in that colour; after the second clear (bit 8), the colours of
// aircraft 21 for S ranks on every mission at difficulty 2 / 3 / 4.
void ApplyRankUnlocks() {
    const u32 progress = ProgressFlags();
    if ((progress & 1) != 0) {
        const u32 difficulty = g_GameState()[0x28];
        if (!(static_cast<s32>(difficulty) < 2)) {
            u32 entry = kMissionAircraft;
            for (s32 m = 0x11; m >= 0; --m, entry += 4) {
                const u32 mission = static_cast<u32>(0x11 - m);
                if (!(Rank(difficulty, mission) < 3)) RaiseOwnership(GRef<s16>(entry), 1, 2);
            }
        }
    }
    if (CheckOtherAircraftBuyable(1, 0x12)) RaiseOwnership(0x12, 1, 2);
    if (CheckOtherAircraftBuyable(2, 0x12)) RaiseOwnership(0x12, 2, 2);
    if ((progress & 8) == 0) return;
    if (CheckAllMissionsRankedS(2)) RaiseOwnership(0x15, 0, 2);
    if (CheckAllMissionsRankedS(3)) RaiseOwnership(0x15, 1, 2);
    if (CheckAllMissionsRankedS(4)) RaiseOwnership(0x15, 2, 2);
}

// EE: 0x002268B0
// Aircraft 1-20 whose first mission (table +0xA) is at most `mission` become buyable in colour 0,
// then the rank unlocks.
void UnlockAircraftUpToMission(s64 mission) {
    if (!(static_cast<u32>(mission - 1) < 0x12)) return;
    for (s32 a = 0; a < 0x14; ++a) {
        if (!(mission < GRef<s16>(kAircraftTable + static_cast<u32>(a) * 0x3C + 0xA))) RaiseOwnership(a + 1, 0, 2);
    }
    ApplyRankUnlocks();
}

// EE: 0x00226968
// Unlocks of a campaign clear: aircraft 2 colours 1/2 buyable on difficulty >= 2; aircraft 21
// colour 0 and colours 1/2 of every aircraft shown.
void ApplyClearUnlocks() {
    if (!(GRef<u16>(kDifficulty) < 2)) {
        RaiseOwnership(2, 1, 2);
        RaiseOwnership(2, 2, 2);
    }
    RaiseOwnership(0x15, 0, 1);
    for (s32 a = 1; a < 0x16; ++a) {
        RaiseOwnership(a, 1, 1);
        RaiseOwnership(a, 2, 1);
    }
}

// EE: 0x00226B20
// Campaign cleared: bit 1 (bit 8 when it was already set: cleared twice), bit 2 on difficulty
// >= 3, bit 4 on difficulty >= 4; then the clear and rank unlocks.
void MarkCampaignCleared() {
    u32 v = ProgressFlags();
    if ((v & 1) != 0) v |= 8;
    const u16 difficulty = GRef<u16>(kDifficulty);
    v |= 1;
    if (!(difficulty < 3)) v |= 2;
    if (!(difficulty < 4)) v |= 4;
    ProgressFlags() = v;
    ApplyClearUnlocks();
    ApplyRankUnlocks();
}

}  // namespace ac04::game
