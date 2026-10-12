// Memory card save system initialisation (docs/formats/folder-memcard.md). The save/load state
// machines are translated with the save milestone; the boot path only sets up the two port blocks.
// Save system block: 0x003F7940 (func_0025F298); port blocks at +0x40 (port 0) and +0x580 (port 1).
#include "game/game.h"

#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x0025E310
void func_0025E310() { sceMcInit(); }

// EE: 0x0025E330
// Port block: port, slot, last result -11, current file -1, directory name = 2-byte prefix (from
// 0x004116F8) + `name`; then clears the 0x480-byte directory buffer. Returns the block.
u32 func_0025E330(u32 block, u32 port, u32 slot, u32 name) {
    GRef<u32>(block + 0x480) = port;
    GRef<u32>(block + 0x484) = slot;
    GRef<s32>(block + 0x494) = -11;
    GRef<s32>(block + 0x488) = -1;
    GRef<u32>(block + 0x498) = 0;
    GRef<u32>(block + 0x49C) = 0;
    GRef<u32>(block + 0x4A0) = 0;
    const u8 second = GRef<u8>(0x004116F9);
    GRef<u8>(block + 0x4A8) = GRef<u8>(0x004116F8);
    GRef<u8>(block + 0x4A9) = second;
    ee::strcat(block + 0x4A8, name);
    return ee::memset(block, 0, 0x480);
}

// EE: 0x0025F298
u32 func_0025F298() { return 0x003F7940; }

// EE: 0x0025F2A8
// HYPOTHESIS: InitSaveSystem. Directory "BASLUS-20152AC04" on both memory card ports.
void InitSaveSystem(u32 save) {
    guest::GuestFrame name(0x20);
    GRef<u64>(name.Address(0)) = 0x322D53554C534142ull;  // "BASLUS-2"
    GRef<u64>(name.Address(8)) = 0x3430434132353130ull;  // "0152AC04"
    GRef<u8>(name.Address(0x10)) = 0;
    func_0025E310();
    func_0025E330(save + 0x40, 0, 0, name.Address());
    func_0025E330(save + 0x580, 1, 0, name.Address());
    func_0025F320(save);
}

// EE: 0x0025F320
void func_0025F320(u32 save) {
    GRef<u32>(save + 0xA24) = 0;
    GRef<u32>(save) = 0;
    GRef<u32>(save + 4) = 0;
    GRef<u32>(save + 0x4E0) = 0;
    GRef<u32>(save + 0x4E4) = 0;
    GRef<u32>(save + 0xA20) = 0;
}

}  // namespace ac04::game
