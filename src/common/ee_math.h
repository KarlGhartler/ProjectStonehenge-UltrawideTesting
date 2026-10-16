// EE FPU behaviour (.claude/rules/game-code.md, "Floating point").
// - Rounding: round to nearest (ties to even), as PCSX2 runs AC04. Its GameDB sets
//   `eeRoundMode: 0` for SLUS-20152 ("fixes autopilot crashing the plane"), and the RAM check at
//   the first SceneStep matches nearest for cvt.s.w (512/512 random-table entries) and div.s
//   (docs/port/validation.md E-2). Decided by the owner 2026-10-13.
// - No denormals (results and operands below FLT_MIN become zero), no infinities/NaN (overflow
//   clamps to the largest magnitude).
// - add/sub mask the smaller operand first (PCSX2's EE adder model, see Add below): host `+`/`-`
//   differ from it by 1 ULP whenever the exponents differ and the masked bits matter.
// - Host code would usually give the same values, but translated code uses these helpers wherever
//   the result is stored or compared (random table, physics, AI) so flush/clamp rules are applied.
//
// Exactness: each helper computes the result in double and rounds once to float. Products are
// exact in double; for sums, differences, quotients and square roots double rounding is innocuous
// because 53 >= 2 * 24 + 2, so the float is the correctly rounded result. Overflow to the EE's
// extended range (exponent 255) is clamped to FLT_MAX (H: the EE keeps those values as normal
// numbers; refine if a function depends on it).
#pragma once

#include <cfloat>
#include <cmath>
#include <cstring>

#include "common/types.h"

namespace ac04::fpu {

// Rounds a double-precision value to the nearest float (ties to even), with EE flush/clamp rules.
inline f32 RoundToFloat(double exact) {
    if (exact != exact) return 0.0f;  // cannot occur on the EE; defensive
    if (std::fabs(exact) > static_cast<double>(FLT_MAX)) return std::signbit(exact) ? -FLT_MAX : FLT_MAX;
    const float f = static_cast<float>(exact);
    if (std::fabs(f) < FLT_MIN) return std::signbit(f) ? -0.0f : 0.0f;  // no denormals
    if (std::isinf(f)) return std::signbit(f) ? -FLT_MAX : FLT_MAX;
    return f;
}

// Operand flush: the EE treats denormal inputs as zero.
inline f32 Flush(f32 x) {
    return std::fabs(x) < FLT_MIN ? (std::signbit(x) ? -0.0f : 0.0f) : x;
}

// add.s / sub.s (also adda/suba/madd/msub): PCSX2 2.8.2 (iFPU.cpp FPU_ADD_SUB, FPU_CORRECT_ADD_SUB = 1,
// not a setting) models the EE adder's missing guard bits: before the add, the operand with the smaller
// exponent keeps only its top (25 - d) significand bits, d = exponent difference (sign only when
// d >= 25); the sum is then rounded to nearest. Measured: the menu fade timer (30 x 1/60) reads
// 0x3EFFFFF8 in PCSX2, plain rounding gives 0x3F000003 (validation.md S2a-3).
inline void MaskSmallerOperand(f32& a, f32& b) {
    u32 ua, ub;
    std::memcpy(&ua, &a, 4);
    std::memcpy(&ub, &b, 4);
    const int d = static_cast<int>((ua >> 23) & 0xFF) - static_cast<int>((ub >> 23) & 0xFF);
    if (d >= 25) {
        ub &= 0x80000000u;
    } else if (d > 0) {
        ub &= 0xFFFFFFFFu << (d - 1);
    } else if (d <= -25) {
        ua &= 0x80000000u;
    } else if (d < 0) {
        ua &= 0xFFFFFFFFu << (-d - 1);
    }
    std::memcpy(&a, &ua, 4);
    std::memcpy(&b, &ub, 4);
}
inline f32 Add(f32 a, f32 b) {
    a = Flush(a);
    b = Flush(b);
    MaskSmallerOperand(a, b);
    return RoundToFloat(static_cast<double>(a) + b);
}
inline f32 Sub(f32 a, f32 b) {
    a = Flush(a);
    b = Flush(b);
    MaskSmallerOperand(a, b);
    return RoundToFloat(static_cast<double>(a) - b);
}
inline f32 Mul(f32 a, f32 b) { return RoundToFloat(static_cast<double>(Flush(a)) * Flush(b)); }
inline f32 Div(f32 a, f32 b) {
    a = Flush(a);
    b = Flush(b);
    if (b == 0.0f) {
        // EE: division by zero gives the largest magnitude with the XOR of the signs.
        const bool neg = std::signbit(a) != std::signbit(b);
        return neg ? -FLT_MAX : FLT_MAX;
    }
    return RoundToFloat(static_cast<double>(a) / b);
}

// sqrt.s: the EE takes the square root of |x| (no NaN for negative input).
inline f32 Sqrt(f32 x) { return RoundToFloat(std::sqrt(std::fabs(static_cast<double>(Flush(x))))); }

// Accumulator ops (mula.s / madda.s / madd.s): each product and each sum is rounded separately.
inline f32 MulAdd(f32 acc, f32 a, f32 b) { return Add(acc, Mul(a, b)); }

// cvt.s.w: 32-bit integer to float, rounded to nearest.
inline f32 IntToFloat(s32 v) { return RoundToFloat(static_cast<double>(v)); }

// cvt.s.w of an unsigned value as gcc emits it: values of 2^31 and above are halved (keeping the low
// bit), converted and doubled with add.s. tools/research/fpucheck.py counts each call as one add.
inline f32 UnsignedToFloat(u32 v) {
    if (static_cast<s32>(v) >= 0) return IntToFloat(static_cast<s32>(v));
    const f32 h = IntToFloat(static_cast<s32>((v >> 1) | (v & 1)));
    return Add(h, h);
}

// cvt.w.s: float to integer, truncating (the EE clamps out-of-range values).
inline s32 FloatToInt(f32 v) {
    if (v >= 2147483648.0f) return 0x7FFFFFFF;
    if (v <= -2147483648.0f) return static_cast<s32>(0x80000000u);
    return static_cast<s32>(v);
}

// libgcc __fixunssfsi (0x0028D220), the soft-float fp-bit version (unpacks the float, no FPU
// convert): zero, negative values and values below 1 give 0; values of 2^32 and above (and the
// exponent-255 patterns fp-bit sees as Inf/NaN: Inf gives 0xFFFFFFFF, NaN 0) saturate; otherwise the
// value is truncated.
inline u32 FixUnsSfSi(f32 v) {
    u32 bits;
    std::memcpy(&bits, &v, 4);
    const u32 exp = (bits >> 23) & 0xFF;
    if (exp == 0xFF) return (bits & 0x7FFFFF) != 0 || (bits >> 31) != 0 ? 0u : 0xFFFFFFFFu;
    if ((bits >> 31) != 0 || exp < 127) return 0;
    if (exp >= 127 + 32) return 0xFFFFFFFFu;
    return static_cast<u32>(static_cast<double>(v));
}

}  // namespace ac04::fpu
