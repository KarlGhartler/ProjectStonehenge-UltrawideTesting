// Fixed-width types used by translated game code and the SDK layer (.claude/rules/game-code.md).
// Never use `long`: it is 64-bit on ee-gcc but 32-bit on MSVC.
#pragma once

#include <cstddef>
#include <cstdint>

using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using f32 = float;

// 128-bit EE register / quadword value (lq/sq, MMI). Plain storage; arithmetic is done per lane.
struct alignas(16) u128 {
    u64 lo;
    u64 hi;
};
static_assert(sizeof(u128) == 16, "u128 must be 16 bytes");

static_assert(sizeof(f32) == 4, "f32 must be IEEE single precision");
