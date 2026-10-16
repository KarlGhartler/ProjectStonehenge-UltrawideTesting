// Random table reader (docs/game/random.md): 512 floats at 0x00411998 filled once at boot
// (InitRandomTable, boot.cpp) and a 9-bit cursor at 0x00411780. FlightPlayFrame resets the cursor
// to the frame counter at every gameplay frame; every other read just advances it.
#include "game/game.h"

namespace ac04::game {

using guest::GRef;

AC04_GUEST_VAR(u32, g_RandomIndex, 0x00411780)

// EE: 0x00100DB8
void SetRandomIndex(u32 index) {
    g_RandomIndex() = index & 0x1FF;
}

// EE: 0x00100DC8
f32 NextRandomFloat() {
    const u32 i = g_RandomIndex() & 0x1FF;
    g_RandomIndex() = g_RandomIndex() + 1;
    return GRef<f32>(0x00411998 + 4 * i);
}

}  // namespace ac04::game
