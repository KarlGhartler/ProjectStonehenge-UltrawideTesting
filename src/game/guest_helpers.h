// Small guest-memory helpers used throughout the translated game code (they were repeated as local
// one-liners in many files). Addresses are 32-bit guest addresses (ADR-001).
#pragma once

#include "common/guest_ref.h"
#include "common/types.h"

namespace ac04::game {

// A float in guest memory (mostly the .rodata / .lit4 constants the original loads with lwc1).
inline f32 K(u32 address) { return guest::GRef<f32>(address); }

// A 32-bit value sign-extended to 64 bits, as the EE keeps it in a register.
inline u64 Sx(s32 v) { return static_cast<u64>(static_cast<s64>(v)); }

// Word i / doubleword i of a packet or record at p.
inline void Put(u32 p, u32 i, u32 v) { guest::GRef<u32>(p + 4 * i) = v; }
inline void Put64(u32 p, u32 i, u64 v) { guest::GRef<u64>(p + 8 * i) = v; }
inline u64& Q(u32 p, u32 i) { return guest::GRef<u64>(p + 8 * i); }

// lq / sq of one quadword (copied as four words).
inline void CopyQword(u32 dst, u32 src) {
    for (u32 o = 0; o < 16; o += 4) guest::GRef<u32>(dst + o) = guest::GRef<u32>(src + o);
}

// sqc2 vf0: (0, 0, 0, 1).
inline void StoreVf0(u32 dst) {
    guest::GRef<u32>(dst) = 0;
    guest::GRef<u32>(dst + 4) = 0;
    guest::GRef<u32>(dst + 8) = 0;
    guest::GRef<u32>(dst + 12) = 0x3F800000;  // 1.0f
}

}  // namespace ac04::game
