// Movie playback state reset at boot (docs/formats/SMV.md, docs/subsystems/iop-sound.md). The
// player itself is translated in the movie milestone.
#include "game/game.h"

#include "sce/ee_hw.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00212110
// HYPOTHESIS: InitMoviePlayback. Resets the two SMV audio state blocks, sets EE timer 0 to count
// H-blanks (mode 0x83: CLKS = H-BLNK, CUE) and clears 0x00410C58.
void InitMoviePlayback() {
    func_00213C50(0x002BBCB8);
    func_00213C50(0x002BBCE0);
    sce::EeHwWrite(sce::EeReg::kT0Mode, 0x83);
    GRef<u32>(0x00410C58) = 0;
}

// EE: 0x00213C50
// Mutes the sound-data input and clears one audio state block (6 words).
s32 func_00213C50(u32 block) {
    SetSoundDataInputVolume(0, 0, 0);
    GRef<u32>(block + 4) = 0;
    GRef<u32>(block) = 0;
    GRef<u32>(block + 0x14) = 0;
    GRef<u32>(block + 8) = 0;
    GRef<u32>(block + 0xC) = 0;
    GRef<u32>(block + 0x10) = 0;
    return 0;
}

}  // namespace ac04::game
