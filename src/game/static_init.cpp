// Static constructors of the game (.ctors list 0x0029F308, docs/game/boot.md "Static
// constructors"). The port's __main (src/sce/libc.cpp) walks the original list from guest memory
// and calls each entry through CallGuest, in the original order (last entry first).
#include "game/game.h"

#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x001E0A80
// Seeds one effect generator word: x = seed * 0xEE29 + 1 (docs/game/random.md).
void func_001E0A80(u32 state, s32 seed) {
    GRef<s32>(state) = static_cast<s32>(static_cast<u32>(seed) * 0xEE29u + 1u);
}

// EE: 0x001E0A20
void func_001E0A20() { func_001E0A80(0x00410BB0, 0x1A99); }

// EE: 0x001E94C0
void func_001E94C0() { func_001E0A80(0x00410BE0, 0x1A99); }

// EE: 0x001E94E0
// Dead (no reference): copy of func_001E0A80.
void func_001E94E0(u32 state, s32 seed) {
    GRef<s32>(state) = static_cast<s32>(static_cast<u32>(seed) * 0xEE29u + 1u);
}

// EE: 0x001FF608
void func_001FF608() {
    for (u32 i = 0; i < 6; ++i) func_001E0A80(0x00410BF8 + 8 * i, 0x1A99);
}

// EE: 0x00210D88
void func_00210D88() {
    func_001E0A80(0x00410C30, 0x1A99);
    func_001E0A80(0x00410C38, 0x1A99);
}

// EE: 0x002140A8
// Movie memory allocator header: alignment at +0xC, offsets cleared.
u32 func_002140A8(u32 alloc, u32 align) {
    GRef<u32>(alloc + 0xC) = align;
    GRef<u32>(alloc) = 0;
    GRef<u32>(alloc + 8) = 0;
    return alloc;
}

// EE: 0x002124F8
void func_002124F8() { func_002140A8(0x002BBA00, 0x40); }

// EE: 0x00213C40
u32 func_00213C40(u32 feeder) {
    GRef<u32>(feeder + 8) = 0;
    GRef<u32>(feeder + 0x14) = 0;
    return feeder;
}

// EE: 0x00214030
void func_00214030() {
    func_00213C40(0x002BBCB8);
    func_00213C40(0x002BBCE0);
}

// EE: 0x002596F8
// Radio chatter timers at 0x004BE0B0 from three rand() calls with newlib's default seed (before
// InitRandomTable reseeds): {rand % 180 + 900, rand % 16 + 16, rand % 120 + 180, 0}, built on the
// stack and copied with unaligned 64-bit stores.
void func_002596F8() {
    const s32 a = ee::rand() % 0xB4 + 0x384;
    const s32 r = ee::rand();
    const s32 b = r - ((r < 0 ? r + 0xF : r) >> 4) * 16 + 0x10;
    const s32 c = ee::rand() % 0x78 + 0xB4;
    GRef<s32>(0x004BE0B0) = a;
    GRef<s32>(0x004BE0B4) = b;
    GRef<s32>(0x004BE0B8) = c;
    GRef<s32>(0x004BE0BC) = 0;
}

// EE: 0x00100188
void func_00100188() { ee::__register_frame_info(0x0029F230, 0x00411980); }

AC04_GUEST_FUNCTION(0x001E0A20, func_001E0A20);
AC04_GUEST_FUNCTION(0x001E94C0, func_001E94C0);
AC04_GUEST_FUNCTION(0x001FF608, func_001FF608);
AC04_GUEST_FUNCTION(0x00210D88, func_00210D88);
AC04_GUEST_FUNCTION(0x002124F8, func_002124F8);
AC04_GUEST_FUNCTION(0x00214030, func_00214030);
AC04_GUEST_FUNCTION(0x002596F8, func_002596F8);
AC04_GUEST_FUNCTION(0x00100188, func_00100188);

}  // namespace ac04::game
