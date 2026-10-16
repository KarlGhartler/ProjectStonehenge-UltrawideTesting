// Aircraft and special weapon tables in .data (aircraft-data module; docs/subsystems/frontend.md
// "Hangar"): name and code strings, the per-aircraft special weapon lists and loadout records.
#include "game/game.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x0011B840
// Short code of special weapon `n` ("UGBS", "QAAM" ...; table 0x002A1BF0, n above 15 -> 15).
u32 GetSpecialWeaponCode(u32 n) { return GRef<u32>(0x002A1BF0 + (n < 0x10 ? n : 0xF) * 4); }

// EE: 0x0011B868
// Display name of special weapon `n` (table 0x002A1C30, n above 15 -> 15).
u32 GetSpecialWeaponName(u32 n) { return GRef<u32>(0x002A1C30 + (n < 0x10 ? n : 0xF) * 4); }

// EE: 0x0011B890
// Short code of aircraft `n` ("ZERO", "F-5E", ... "F04T"; table 0x002A1C70, n above 24 -> 24).
u32 GetAircraftCode(u32 n) { return GRef<u32>(0x002A1C70 + (n < 0x19 ? n : 0x18) * 4); }

// EE: 0x0011B8B8
// HYPOTHESIS: loadout record of aircraft `a` (0x10 bytes at 0x002A1CD8; a above 21 -> 0).
u32 GetAircraftLoadout(s64 a) {
    if (!(a < 0x16)) a = 0;
    return 0x002A1CD8 + static_cast<u32>(a) * 0x10;
}

// EE: 0x0011B8D8
// Entry `i` of aircraft `a`'s loadout record after its first word (0x002A1CDC + a * 0x10 + i * 4).
u32 GetAircraftLoadoutEntry(s64 a, s32 i) {
    if (!(a < 0x16)) a = 0;
    return 0x002A1CD8 + static_cast<u32>(a) * 0x10 + static_cast<u32>(i) * 4 + 4;
}

// EE: 0x0011B900
// Special weapons of aircraft `a`: {u32 count, u8 ids[4]} at 0x002A1E48 + a * 8 (a above 21 -> 0).
u32 GetAircraftSpecialWeapons(s64 a) {
    if (!(a < 0x16)) a = 0;
    return 0x002A1E48 + static_cast<u32>(a) * 8;
}

}  // namespace ac04::game
