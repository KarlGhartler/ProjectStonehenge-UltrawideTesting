// VU0 macro-mode (COP2) arithmetic for translated game code (CLAUDE.md "Repo layout"), and the
// micro-mode variant used by the VU1 program HLE.
//
// Macro mode (namespace ac04::vu0): PCSX2 2.8.2 compiles COP2 macro instructions into the EE block
// (microVU_Macro.inl) with plain SSE add/sub/mul and no MXCSR switch, so they run under the EE's
// rounding mode, which the AC04 GameDB entry sets to nearest (eeRoundMode 0). Measured: the map
// camera's _sceVu0ecossin (angle -1.57060099) gives 0x394CE000 in PCSX2, which round to nearest
// reproduces and chop does not (0x394CDFFE); validation.md S4-3. Unlike the EE FPU add.s, there is
// no smaller-operand masking (PCSX2 uses SSE_ADDPS/ADDSS directly for VU add/sub).
// Micro mode (namespace ac04::vu0::micro): VU micro programs run with the VU rounding setting,
// chop (round toward zero) in the owner's PCSX2 configuration and PCSX2's default.
// Both: a multiply-add (vmadd*, vmadda*) is a multiply and an add, each rounded separately;
// denormals are flushed to zero and results are clamped to +-FLT_MAX.
//
// Exactness: macro helpers compute in double and round once (double rounding is innocuous for sums,
// products, quotients and roots: 53 >= 2 * 24 + 2). Micro helpers are single-precision operations in
// the host's round-toward-zero mode (ChopScope), which is exactly the chopped result; checked against
// the former double-precision emulation over whole sessions (validation.md S4-6).
#pragma once

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

#include "common/types.h"

namespace ac04::vu0 {

namespace detail {

inline std::uint32_t Bits(f32 f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

inline f32 FromBits(std::uint32_t b) {
    f32 f;
    std::memcpy(&f, &b, 4);
    return f;
}

// Denormals become zero of the same sign (branchless: these helpers run per vertex).
inline std::uint32_t FlushBits(std::uint32_t b) { return (b & 0x7F800000u) != 0 ? b : (b & 0x80000000u); }
inline f32 FlushDenormal(f32 v) { return FromBits(FlushBits(Bits(v))); }

// NaN and values beyond the float range: clamped (NaN to +FLT_MAX).
inline f32 ClampOutOfRange(double d) {
    if (d != d) return FLT_MAX;
    return d > 0.0 ? FLT_MAX : -FLT_MAX;
}

}  // namespace detail

struct Vec4 {
    f32 x = 0, y = 0, z = 0, w = 0;
};

// vftoi4: v * 16 truncated toward zero, saturated to the 32-bit range.
inline s32 FloatToInt4(f32 v) {
    const double d = static_cast<double>(v) * 16.0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return static_cast<s32>(d);
}

// vftoi12: v * 4096 truncated toward zero, saturated to the 32-bit range.
inline s32 FloatToInt12(f32 v) {
    const double d = static_cast<double>(v) * 4096.0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return static_cast<s32>(d);
}

// vftoi0: v truncated toward zero, saturated to the 32-bit range.
inline s32 FloatToInt0(f32 v) {
    const double d = static_cast<double>(v);
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return static_cast<s32>(d);
}

namespace detail {

// Rounds `d` to the nearest float (ties to even), clamped to +-FLT_MAX, denormal results flushed.
inline f32 NearestDouble(double d) {
    const double ad = std::fabs(d);
    if (!(ad <= static_cast<double>(FLT_MAX))) return ClampOutOfRange(d);  // clamped
    return FromBits(FlushBits(Bits(static_cast<f32>(d))));
}

}  // namespace detail

inline f32 Add(f32 a, f32 b) {
    return detail::NearestDouble(static_cast<double>(detail::FlushDenormal(a)) + detail::FlushDenormal(b));
}

inline f32 Sub(f32 a, f32 b) { return Add(a, -b); }

inline f32 Mul(f32 a, f32 b) {
    return detail::NearestDouble(static_cast<double>(detail::FlushDenormal(a)) * detail::FlushDenormal(b));
}

inline f32 Div(f32 a, f32 b) {
    a = detail::FlushDenormal(a);
    b = detail::FlushDenormal(b);
    if (b == 0.0f) return std::copysign(FLT_MAX, a) * (std::signbit(b) ? -1.0f : 1.0f);
    return detail::NearestDouble(static_cast<double>(a) / static_cast<double>(b));
}

// vsqrt: square root of |a| (the VU ignores the sign).
inline f32 Sqrt(f32 a) {
    return detail::NearestDouble(std::sqrt(std::fabs(static_cast<double>(detail::FlushDenormal(a)))));
}

// a + b * c as a VU multiply-add: product rounded, then the sum rounded.
inline f32 MulAdd(f32 a, f32 b, f32 c) { return Add(a, Mul(b, c)); }

// vitof0: integers to floats (exact for |v| < 2^24; larger values are rounded).
inline f32 IntToFloat(s32 v) { return detail::NearestDouble(static_cast<double>(v)); }

// vmulax/vmadday/vmaddaz/vmaddw with the matrix columns c0..c3: c0*v.x + c1*v.y + c2*v.z + c3*v.w,
// accumulated in that order per component.
inline Vec4 MulMatrix(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3, const Vec4& v) {
    Vec4 r;
    r.x = MulAdd(MulAdd(MulAdd(Mul(c0.x, v.x), c1.x, v.y), c2.x, v.z), c3.x, v.w);
    r.y = MulAdd(MulAdd(MulAdd(Mul(c0.y, v.x), c1.y, v.y), c2.y, v.z), c3.y, v.w);
    r.z = MulAdd(MulAdd(MulAdd(Mul(c0.z, v.x), c1.z, v.y), c2.z, v.z), c3.z, v.w);
    r.w = MulAdd(MulAdd(MulAdd(Mul(c0.w, v.x), c1.w, v.y), c2.w, v.z), c3.w, v.w);
    return r;
}

// VU0 R register (random source, docs/game/random.md): vrinit sets R = 0x3F800000 | (23 low
// bits of the source field); vrnext advances it with PCSX2's LFSR (VUops.cpp AdvanceLFSR: bit 4
// xor bit 22 shifted in at bit 0, exponent forced back to 1.0). Not guest memory: it lives in VU0.
inline std::uint32_t& RRegister() {
    static std::uint32_t r = 0x3F800000u;
    return r;
}

inline void RInit(std::uint32_t bits) { RRegister() = 0x3F800000u | (bits & 0x007FFFFFu); }

inline void RNext() {
    std::uint32_t& r = RRegister();
    const std::uint32_t x = (r >> 4) & 1u;
    const std::uint32_t y = (r >> 22) & 1u;
    r <<= 1;
    r ^= x ^ y;
    r = (r & 0x007FFFFFu) | 0x3F800000u;
}

namespace micro {

using vu0::Vec4;

// Micro-mode helpers are plain single-precision operations: they must run inside a ChopScope, which
// puts the host's SSE unit into the VU's mode (round toward zero, denormal inputs and results
// flushed to zero), as PCSX2 runs VU micro programs. In that mode an overflow gives +-FLT_MAX, the
// VU's clamp. Translation units using them enable `#pragma fenv_access(on)` (MSVC) so the compiler
// neither folds nor reorders these operations under the default rounding mode.
class ChopScope {
public:
    ChopScope() : saved_(_mm_getcsr()) { _mm_setcsr((saved_ & ~0x6000u) | 0x6000u | 0x8040u); }
    ~ChopScope() { _mm_setcsr(saved_); }
    ChopScope(const ChopScope&) = delete;
    ChopScope& operator=(const ChopScope&) = delete;

private:
    unsigned saved_;
};

inline f32 Add(f32 a, f32 b) { return a + b; }

inline f32 Sub(f32 a, f32 b) { return a - b; }

inline f32 Mul(f32 a, f32 b) { return a * b; }

inline f32 Div(f32 a, f32 b) {
    if (b == 0.0f) return std::copysign(FLT_MAX, a) * (std::signbit(b) ? -1.0f : 1.0f);
    return a / b;
}

// vsqrt: square root of |a| (the VU ignores the sign).
inline f32 Sqrt(f32 a) { return std::sqrt(std::fabs(a)); }

// a + b * c as a VU multiply-add: product chopped, then the sum chopped.
inline f32 MulAdd(f32 a, f32 b, f32 c) { return Add(a, Mul(b, c)); }

// vitof0: integers to floats (exact for |v| < 2^24; larger values are chopped).
inline f32 IntToFloat(s32 v) { return static_cast<f32>(v); }

// vmulax/vmadday/vmaddaz/vmaddw with the matrix columns c0..c3: c0*v.x + c1*v.y + c2*v.z + c3*v.w,
// accumulated in that order per component.
inline Vec4 MulMatrix(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3, const Vec4& v) {
    Vec4 r;
    r.x = MulAdd(MulAdd(MulAdd(Mul(c0.x, v.x), c1.x, v.y), c2.x, v.z), c3.x, v.w);
    r.y = MulAdd(MulAdd(MulAdd(Mul(c0.y, v.x), c1.y, v.y), c2.y, v.z), c3.y, v.w);
    r.z = MulAdd(MulAdd(MulAdd(Mul(c0.z, v.x), c1.z, v.y), c2.z, v.z), c3.z, v.w);
    r.w = MulAdd(MulAdd(MulAdd(Mul(c0.w, v.x), c1.w, v.y), c2.w, v.z), c3.w, v.w);
    return r;
}

}  // namespace micro

}  // namespace ac04::vu0
