// newlib single-precision maths used by the game (fdlibm float routines as compiled into
// SLUS_201.52), translated from the disassembly so the results are bit-exact on the host: every
// float operation goes through the EE helpers (src/common/ee_math.h). Tables are read from the
// executable's .data in guest memory (never copied into src/).
//
// sinf 0x00284830, cosf 0x00284660, __ieee754_rem_pio2f 0x00286DE0, __kernel_rem_pio2f 0x00287450,
// __kernel_sinf 0x00287DA0, __kernel_cosf 0x002872F8, scalbnf 0x002886B8, atan2f 0x00284CB8
// (wrapper), __ieee754_atan2f 0x00286018, atanf 0x002883A0, fabsf 0x00288648, asinf 0x00284BB8 /
// __ieee754_asinf 0x00285C78, acosf 0x00284AB8 / __ieee754_acosf 0x00285848, __ieee754_sqrtf 0x002871C0,
// powf 0x00284F20 / __ieee754_powf 0x00286550.
#include <cstring>

#include "common/ee_math.h"
#include "common/guest_ref.h"
#include "sce/sce.h"

namespace ee {

using ac04::guest::GRef;
namespace fpu = ac04::fpu;

namespace {

u32 Bits(f32 f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}

f32 FromBits(u32 b) {
    f32 f;
    std::memcpy(&f, &b, 4);
    return f;
}

f32 K(u32 bits) { return FromBits(bits); }

// Tables in .data.
constexpr u32 kTwoOverPi = 0x0040C578;  // ipio2: s32 digits of 2/pi, 8 bits each
constexpr u32 kNpio2Hw = 0x0040C890;    // high words of n*pi/2, n = 1..32
constexpr u32 kInitJk = 0x0040C960;     // jk by precision
constexpr u32 kPio2 = 0x0040C970;       // pi/2 in 8-bit pieces
constexpr u32 kAtanHi = 0x0040CA20;
constexpr u32 kAtanLo = 0x0040CA30;
constexpr u32 kAtanT = 0x0040CA40;  // aT[0..10]

// scalbnf (0x002886B8): x * 2^n by exponent arithmetic (normal results only on the EE: underflow
// gives zero, overflow the largest magnitude).
f32 Scalbnf(f32 x, s32 n) {
    const u32 hx = Bits(x);
    const s32 k = static_cast<s32>((hx >> 23) & 0xFF);
    if (k == 0 || (hx & 0x7FFFFFFFu) == 0) return x;
    const s32 e = k + n;
    if (e >= 0xFF) return (hx >> 31) != 0 ? -FLT_MAX : FLT_MAX;
    if (e <= 0) return (hx >> 31) != 0 ? -0.0f : 0.0f;
    return FromBits((hx & 0x807FFFFFu) | static_cast<u32>(e) << 23);
}

// __kernel_sinf (0x00287DA0): sin(x + y) on [-pi/4, pi/4]; iy = 0 when y is zero.
f32 KernelSinf(f32 x, f32 y, s32 iy) {
    const u32 ix = Bits(x) & 0x7FFFFFFFu;
    if (ix < 0x32000000u && fpu::FloatToInt(x) == 0) return x;
    const f32 z = fpu::Mul(x, x);
    f32 r = fpu::Mul(z, K(0x2F2EC9D3));
    const f32 v = fpu::Mul(z, x);
    r = fpu::Add(r, K(0xB2D72F34));
    r = fpu::Mul(z, r);
    r = fpu::Add(r, K(0x3638EF1B));
    r = fpu::Mul(z, r);
    r = fpu::Add(r, K(0xB9500D01));
    r = fpu::Mul(z, r);
    r = fpu::Add(r, K(0x3C088889));
    if (iy == 0) {
        f32 t = fpu::Mul(z, r);
        t = fpu::Add(t, K(0xBE2AAAAB));
        t = fpu::Mul(v, t);
        return fpu::Add(x, t);
    }
    const f32 vr = fpu::Mul(v, r);
    f32 t = fpu::Mul(y, 0.5f);
    const f32 vs1 = fpu::Mul(v, K(0xBE2AAAAB));
    t = fpu::Sub(t, vr);
    t = fpu::Mul(z, t);
    t = fpu::Sub(t, y);
    t = fpu::Sub(t, vs1);
    return fpu::Sub(x, t);
}

// __kernel_cosf (0x002872F8): cos(x + y) on [-pi/4, pi/4].
f32 KernelCosf(f32 x, f32 y) {
    const u32 ix = Bits(x) & 0x7FFFFFFFu;
    if (ix < 0x32000000u && fpu::FloatToInt(x) == 0) return 1.0f;
    const f32 z = fpu::Mul(x, x);
    f32 p = fpu::Mul(z, K(0xAD47D74E));
    p = fpu::Add(p, K(0x310F74F6));
    p = fpu::Mul(z, p);
    p = fpu::Add(p, K(0xB493F27C));
    p = fpu::Mul(z, p);
    p = fpu::Add(p, K(0x37D00D01));
    p = fpu::Mul(z, p);
    p = fpu::Add(p, K(0xBAB60B61));
    p = fpu::Mul(z, p);
    p = fpu::Add(p, K(0x3D2AAAAB));
    const f32 r = fpu::Mul(z, p);
    if (ix < 0x3E99999Au) {
        f32 a = fpu::Mul(z, r);
        const f32 xy = fpu::Mul(x, y);
        const f32 hz = fpu::Mul(z, 0.5f);
        a = fpu::Sub(a, xy);
        return fpu::Sub(1.0f, fpu::Sub(hz, a));
    }
    const f32 qx = ix > 0x3F480000u ? K(0x3E900000) : FromBits(ix - 0x01000000u);
    f32 a = fpu::Mul(z, r);
    const f32 xy = fpu::Mul(x, y);
    f32 hz = fpu::Mul(z, 0.5f);
    a = fpu::Sub(a, xy);
    hz = fpu::Sub(hz, qx);
    const f32 one_qx = fpu::Sub(1.0f, qx);
    return fpu::Sub(one_qx, fpu::Sub(hz, a));
}

// __kernel_rem_pio2f (0x00287450): reduction of large arguments (fdlibm k_rem_pio2f, float version,
// 8-bit chunks). x: 3 chunks of the argument, e0 its exponent, prec 2 (always, from rem_pio2f).
s32 KernelRemPio2f(const f32* x, f32* y, s32 e0, s32 nx, s32 prec) {
    const f32 two8 = 256.0f, twon8 = 0.00390625f;
    const s32 jk = GRef<s32>(kInitJk + 4u * static_cast<u32>(prec));
    const s32 jp = jk;
    const s32 jx = nx - 1;
    s32 jv = (e0 - 3) / 8;
    if (jv < 0) jv = 0;
    s32 q0 = e0 - 8 * (jv + 1);
    f32 f[20], q[20], fq[20];
    s32 iq[20];
    {
        s32 j = jv - jx;
        const s32 m = jx + jk;
        for (s32 i = 0; i <= m; ++i, ++j)
            f[i] = j < 0 ? 0.0f : fpu::IntToFloat(GRef<s32>(kTwoOverPi + 4u * static_cast<u32>(j)));
    }
    for (s32 i = 0; i <= jk; ++i) {
        f32 fw = 0.0f;
        for (s32 j = 0; j <= jx; ++j) fw = fpu::Add(fw, fpu::Mul(x[j], f[jx + i - j]));
        q[i] = fw;
    }
    s32 jz = jk;
    s32 n = 0, ih = 0;
    f32 z = 0.0f;
    for (;;) {  // recompute
        s32 i = 0;
        z = q[jz];
        for (s32 j = jz; j > 0; ++i, --j) {
            const f32 fw = fpu::IntToFloat(fpu::FloatToInt(fpu::Mul(twon8, z)));
            iq[i] = fpu::FloatToInt(fpu::Sub(z, fpu::Mul(two8, fw)));
            z = fpu::Add(q[j - 1], fw);
        }
        z = Scalbnf(z, q0);
        z = fpu::Sub(z, fpu::Mul(8.0f, floorf(fpu::Mul(z, 0.125f))));
        n = fpu::FloatToInt(z);
        z = fpu::Sub(z, fpu::IntToFloat(n));
        ih = 0;
        if (q0 > 0) {
            const s32 t = iq[jz - 1] >> (8 - q0);
            n += t;
            iq[jz - 1] -= t << (8 - q0);
            ih = iq[jz - 1] >> (7 - q0);
        } else if (q0 == 0) {
            ih = iq[jz - 1] >> 8;
        } else if (z >= 0.5f) {
            ih = 2;
        }
        if (ih > 0) {
            n += 1;
            s32 carry = 0;
            for (s32 k = 0; k < jz; ++k) {
                const s32 j = iq[k];
                if (carry == 0) {
                    if (j != 0) {
                        carry = 1;
                        iq[k] = 0x100 - j;
                    }
                } else {
                    iq[k] = 0xFF - j;
                }
            }
            if (q0 == 1) iq[jz - 1] &= 0x7F;
            if (q0 == 2) iq[jz - 1] &= 0x3F;
            if (ih == 2) {
                z = fpu::Sub(1.0f, z);
                if (carry != 0) z = fpu::Sub(z, Scalbnf(1.0f, q0));
            }
        }
        if (z == 0.0f) {
            s32 j = 0;
            for (s32 k = jz - 1; k >= jk; --k) j |= iq[k];
            if (j == 0) {
                s32 k = 1;
                while (iq[jk - k] == 0) ++k;
                for (s32 m = jz + 1; m <= jz + k; ++m) {
                    f[jx + m] = fpu::IntToFloat(GRef<s32>(kTwoOverPi + 4u * static_cast<u32>(jv + m)));
                    f32 fw = 0.0f;
                    for (s32 jj = 0; jj <= jx; ++jj) fw = fpu::Add(fw, fpu::Mul(x[jj], f[jx + m - jj]));
                    q[m] = fw;
                }
                jz += k;
                continue;
            }
        }
        break;
    }
    if (z == 0.0f) {
        jz -= 1;
        q0 -= 8;
        while (iq[jz] == 0) {
            jz--;
            q0 -= 8;
        }
    } else {
        z = Scalbnf(z, -q0);
        if (z >= two8) {
            const f32 fw = fpu::IntToFloat(fpu::FloatToInt(fpu::Mul(twon8, z)));
            iq[jz] = fpu::FloatToInt(fpu::Sub(z, fpu::Mul(two8, fw)));
            jz += 1;
            q0 += 8;
            iq[jz] = fpu::FloatToInt(fw);
        } else {
            iq[jz] = fpu::FloatToInt(z);
        }
    }
    f32 fw = Scalbnf(1.0f, q0);
    for (s32 i = jz; i >= 0; --i) {
        q[i] = fpu::Mul(fw, fpu::IntToFloat(iq[i]));
        fw = fpu::Mul(fw, twon8);
    }
    for (s32 i = jz; i >= 0; --i) {
        f32 acc = 0.0f;
        for (s32 k = 0; k <= jp && k <= jz - i; ++k)
            acc = fpu::Add(acc, fpu::Mul(GRef<f32>(kPio2 + 4u * static_cast<u32>(k)), q[i + k]));
        fq[jz - i] = acc;
    }
    // prec 1 or 2 (the only one rem_pio2f uses is 2).
    f32 acc = 0.0f;
    for (s32 i = jz; i >= 0; --i) acc = fpu::Add(acc, fq[i]);
    y[0] = ih == 0 ? acc : -acc;
    acc = fpu::Sub(fq[0], acc);
    for (s32 i = 1; i <= jz; ++i) acc = fpu::Add(acc, fq[i]);
    y[1] = ih == 0 ? acc : -acc;
    return n & 7;
}

// __ieee754_rem_pio2f (0x00286DE0): x = n * pi/2 + (y0 + y1).
s32 RemPio2f(f32 x, f32* y) {
    const u32 hx = Bits(x);
    const u32 ix = hx & 0x7FFFFFFFu;
    const f32 pio2_1 = K(0x3FC90F80), pio2_1t = K(0x37354443);
    const f32 pio2_2 = K(0x37354400), pio2_2t = K(0x2E85A308);
    const f32 pio2_3 = K(0x2E85A300), pio2_3t = K(0x248D3132);
    if (ix <= 0x3F490FD8u) {
        y[0] = x;
        y[1] = 0.0f;
        return 0;
    }
    if (ix < 0x4016CBE4u) {
        if (static_cast<s32>(hx) > 0) {
            f32 z = fpu::Sub(x, pio2_1);
            if ((ix & 0xFFFFFFF0u) != 0x3FC90FD0u) {
                y[0] = fpu::Sub(z, pio2_1t);
                y[1] = fpu::Sub(fpu::Sub(z, y[0]), pio2_1t);
            } else {
                z = fpu::Sub(z, pio2_2);
                y[0] = fpu::Sub(z, pio2_2t);
                y[1] = fpu::Sub(fpu::Sub(z, y[0]), pio2_2t);
            }
            return 1;
        }
        f32 z = fpu::Add(x, pio2_1);
        if ((ix & 0xFFFFFFF0u) != 0x3FC90FD0u) {
            y[0] = fpu::Add(z, pio2_1t);
            y[1] = fpu::Add(fpu::Sub(z, y[0]), pio2_1t);
        } else {
            z = fpu::Add(z, pio2_2);
            y[0] = fpu::Add(z, pio2_2t);
            y[1] = fpu::Add(fpu::Sub(z, y[0]), pio2_2t);
        }
        return -1;
    }
    if (ix <= 0x43490F80u) {
        const f32 t = FromBits(ix);  // fabsf
        const s32 n = fpu::FloatToInt(fpu::Add(fpu::Mul(t, K(0x3F22F984)), 0.5f));
        const f32 fn = fpu::IntToFloat(n);
        f32 r = fpu::Sub(t, fpu::Mul(fn, pio2_1));
        f32 w = fpu::Mul(fn, pio2_1t);
        if (n < 32 && (ix & 0xFFFFFF00u) != GRef<u32>(kNpio2Hw + 4u * static_cast<u32>(n - 1))) {
            y[0] = fpu::Sub(r, w);
        } else {
            const s32 j = static_cast<s32>(ix >> 23);
            y[0] = fpu::Sub(r, w);
            s32 i = j - static_cast<s32>((Bits(y[0]) >> 23) & 0xFF);
            if (i > 8) {
                f32 tt = r;
                w = fpu::Mul(fn, pio2_2);
                r = fpu::Sub(tt, w);
                w = fpu::Sub(fpu::Mul(fn, pio2_2t), fpu::Sub(fpu::Sub(tt, r), w));
                y[0] = fpu::Sub(r, w);
                i = j - static_cast<s32>((Bits(y[0]) >> 23) & 0xFF);
                if (i > 25) {
                    tt = r;
                    w = fpu::Mul(fn, pio2_3);
                    r = fpu::Sub(tt, w);
                    w = fpu::Sub(fpu::Mul(fn, pio2_3t), fpu::Sub(fpu::Sub(tt, r), w));
                    y[0] = fpu::Sub(r, w);
                }
            }
        }
        y[1] = fpu::Sub(fpu::Sub(r, y[0]), w);
        if (static_cast<s32>(hx) < 0) {
            y[0] = -y[0];
            y[1] = -y[1];
            return -n;
        }
        return n;
    }
    if (ix >= 0x7F800000u) {
        y[0] = y[1] = fpu::Sub(x, x);
        return 0;
    }
    const s32 e0 = static_cast<s32>(ix >> 23) - 134;
    f32 z = FromBits(ix - (static_cast<u32>(e0) << 23));
    f32 tx[3];
    for (s32 i = 0; i < 2; ++i) {
        tx[i] = fpu::IntToFloat(fpu::FloatToInt(z));
        z = fpu::Mul(fpu::Sub(z, tx[i]), 256.0f);
    }
    tx[2] = z;
    s32 nx = 3;
    while (nx > 1 && tx[nx - 1] == 0.0f) --nx;
    const s32 n = KernelRemPio2f(tx, y, e0, nx, 2);
    if (static_cast<s32>(hx) < 0) {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}

}  // namespace

// sinf (0x00284830).
f32 sinf(f32 x) {
    const u32 ix = Bits(x) & 0x7FFFFFFFu;
    if (ix <= 0x3F490FD8u) return KernelSinf(x, 0.0f, 0);
    if (ix > 0x7F7FFFFFu) return fpu::Sub(x, x);
    f32 y[2];
    switch (RemPio2f(x, y) & 3) {
        case 0: return KernelSinf(y[0], y[1], 1);
        case 1: return KernelCosf(y[0], y[1]);
        case 2: return -KernelSinf(y[0], y[1], 1);
        default: return -KernelCosf(y[0], y[1]);
    }
}

// cosf (0x00284660).
f32 cosf(f32 x) {
    const u32 ix = Bits(x) & 0x7FFFFFFFu;
    if (ix <= 0x3F490FD8u) return KernelCosf(x, 0.0f);
    if (ix > 0x7F7FFFFFu) return fpu::Sub(x, x);
    f32 y[2];
    switch (RemPio2f(x, y) & 3) {
        case 0: return KernelCosf(y[0], y[1]);
        case 1: return -KernelSinf(y[0], y[1], 1);
        case 2: return -KernelCosf(y[0], y[1]);
        default: return KernelSinf(y[0], y[1], 1);
    }
}

// atanf (0x002883A0).
f32 atanf(f32 x) {
    const u32 hx = Bits(x);
    const u32 ix = hx & 0x7FFFFFFFu;
    if (ix > 0x507FFFFFu) {
        if (ix > 0x7F800000u) return fpu::Add(x, x);
        const f32 hi = GRef<f32>(kAtanHi + 12), lo = GRef<f32>(kAtanLo + 12);
        return static_cast<s32>(hx) > 0 ? fpu::Add(hi, lo) : fpu::Sub(-hi, lo);
    }
    s32 id;
    if (ix <= 0x3EDFFFFFu) {
        if (ix <= 0x30FFFFFFu && fpu::Add(x, K(0x7149F2CA)) > 1.0f) return x;
        id = -1;
    } else {
        x = FromBits(ix);  // fabsf
        if (ix <= 0x3F97FFFFu) {
            if (ix <= 0x3F2FFFFFu) {
                id = 0;
                x = fpu::Div(fpu::Sub(fpu::Add(x, x), 1.0f), fpu::Add(x, 2.0f));
            } else {
                id = 1;
                x = fpu::Div(fpu::Sub(x, 1.0f), fpu::Add(x, 1.0f));
            }
        } else if (ix <= 0x401BFFFFu) {
            id = 2;
            x = fpu::Div(fpu::Sub(x, 1.5f), fpu::Add(fpu::Mul(x, 1.5f), 1.0f));
        } else {
            id = 3;
            x = fpu::Div(-1.0f, x);
        }
    }
    auto at = [](u32 i) { return GRef<f32>(kAtanT + 4u * i); };
    const f32 z = fpu::Mul(x, x);
    const f32 w = fpu::Mul(z, z);
    f32 a = fpu::Mul(w, at(10));
    f32 b = fpu::Mul(w, at(9));
    a = fpu::Add(at(8), a);
    b = fpu::Add(at(7), b);
    a = fpu::Mul(w, a);
    b = fpu::Mul(w, b);
    a = fpu::Add(at(6), a);
    b = fpu::Add(at(5), b);
    a = fpu::Mul(w, a);
    b = fpu::Mul(w, b);
    a = fpu::Add(at(4), a);
    b = fpu::Add(at(3), b);
    a = fpu::Mul(w, a);
    b = fpu::Mul(w, b);
    a = fpu::Add(at(2), a);
    b = fpu::Add(at(1), b);
    a = fpu::Mul(w, a);
    const f32 s2 = fpu::Mul(w, b);
    a = fpu::Add(at(0), a);
    const f32 s1 = fpu::Mul(z, a);
    if (id < 0) return fpu::Sub(x, fpu::Mul(x, fpu::Add(s1, s2)));
    f32 r = fpu::Mul(x, fpu::Add(s1, s2));
    r = fpu::Sub(r, GRef<f32>(kAtanLo + 4u * static_cast<u32>(id)));
    r = fpu::Sub(r, x);
    r = fpu::Sub(GRef<f32>(kAtanHi + 4u * static_cast<u32>(id)), r);
    return static_cast<s32>(hx) < 0 ? -r : r;
}

namespace {

// __ieee754_atan2f (0x00286018).
f32 Ieee754Atan2f(f32 y, f32 x) {
    const u32 hx = Bits(x), hy = Bits(y);
    const u32 ix = hx & 0x7FFFFFFFu, iy = hy & 0x7FFFFFFFu;
    const f32 pi = K(0x40490FDA), pi_lo = K(0x34222168);
    if (ix > 0x7F800000u || iy > 0x7F800000u) return fpu::Add(x, y);
    if (hx == 0x3F800000u) return atanf(y);
    const s32 m = static_cast<s32>((hy >> 31) & 1) | static_cast<s32>((hx >> 30) & 2);
    if (iy == 0) {
        if (m == 2) return pi;
        if (m == 3) return K(0xC0490FDA);
        return y;
    }
    if (ix == 0) return static_cast<s32>(hy) < 0 ? K(0xBFC90FDB) : K(0x3FC90FDB);
    if (ix == 0x7F800000u) {
        if (iy == 0x7F800000u) {
            switch (m) {
                case 0: return K(0x3F490FDB);
                case 1: return K(0xBF490FDB);
                case 2: return K(0x4016CBE4);
                default: return K(0xC016CBE4);
            }
        }
        switch (m) {
            case 0: return 0.0f;
            case 1: return GRef<f32>(0x0040C4DC);
            case 2: return pi;
            default: return K(0xC0490FDA);
        }
    }
    if (iy == 0x7F800000u) return static_cast<s32>(hy) < 0 ? K(0xBFC90FDB) : K(0x3FC90FDB);
    const s32 k = static_cast<s32>(iy - ix) >> 23;
    f32 z;
    if (k > 60) {
        z = K(0x3FC90FDC);
    } else if (static_cast<s32>(hx) < 0 && k < -60) {
        z = 0.0f;
    } else {
        z = atanf(FromBits(Bits(fpu::Div(y, x)) & 0x7FFFFFFFu));
    }
    switch (m) {
        case 0: return z;
        case 1: return FromBits(Bits(z) ^ 0x80000000u);
        case 2: return fpu::Sub(pi, fpu::Sub(z, pi_lo));
        default: return fpu::Sub(fpu::Sub(z, pi_lo), pi);
    }
}


// __ieee754_sqrtf (0x002871C0): bit-by-bit square root (correctly rounded); exponent 0xFF gives
// x * x + x, negatives (x - x) / (x - x).
f32 Ieee754Sqrtf(f32 x) {
    s32 ix = static_cast<s32>(Bits(x));
    if ((ix & 0x7F800000) == 0x7F800000) return fpu::Add(x * x, x);
    if (ix <= 0) {
        if ((ix & 0x7FFFFFFF) == 0) return x;
        if (ix < 0) {
            const f32 d = fpu::Sub(x, x);
            return fpu::Div(d, d);
        }
    }
    s32 m = ix >> 23;
    if (m == 0) {
        s32 i = 0;
        while ((ix & 0x00800000) == 0) {
            ix <<= 1;
            ++i;
        }
        m = 1 - i;
    }
    m -= 127;
    ix = (ix & 0x007FFFFF) | 0x00800000;
    if ((m & 1) != 0) ix += ix;
    m >>= 1;
    ix += ix;
    s32 q = 0, s = 0;
    for (u32 r = 0x01000000; r != 0; r >>= 1) {
        const s32 t = s + static_cast<s32>(r);
        if (t <= ix) {
            s = t + static_cast<s32>(r);
            ix -= t;
            q += static_cast<s32>(r);
        }
        ix += ix;
    }
    if (ix != 0) q += q & 1;
    return FromBits(static_cast<u32>((q >> 1) + 0x3F000000 + (m << 23)));
}

// R(t) = p / q of __ieee754_asinf / __ieee754_acosf: p = t (pS0 + t (pS1 + ... t pS5)),
// q = 1 + t (qS1 + ... t qS4).
void AsinPoly(f32 t, f32* p, f32* q) {
    f32 a = fpu::Add(t * K(0x3811EF08), K(0x3A4F7F04));
    a = fpu::Add(t * a, K(0xBD241146));
    a = fpu::Add(t * a, K(0x3E4E0AA8));
    a = fpu::Add(t * a, K(0xBEA6B090));
    a = fpu::Add(t * a, K(0x3E2AAAAB));
    *p = t * a;
    f32 b = fpu::Add(t * K(0x3D9DC62E), K(0xBF303361));
    b = fpu::Add(t * b, K(0x4001572D));
    b = fpu::Add(t * b, K(0xC019D139));
    *q = fpu::Add(t * b, 1.0f);
}

// __ieee754_asinf (0x00285C78).
f32 Ieee754Asinf(f32 x) {
    const s32 hx = static_cast<s32>(Bits(x));
    const s32 ix = hx & 0x7FFFFFFF;
    const f32 pio2_hi = K(0x3FC90FDA), pio2_lo = K(0x33A22168), pio4_hi = K(0x3F490FDB);
    if (ix == 0x3F800000) return fpu::Add(x * pio2_hi, x * pio2_lo);
    if (ix > 0x3F800000) {
        const f32 d = fpu::Sub(x, x);
        return fpu::Div(d, d);
    }
    f32 p, q;
    if (ix < 0x3F000000) {
        if (ix <= 0x31FFFFFF && 1.0f < fpu::Add(x, K(0x7149F2CA))) return x;
        AsinPoly(x * x, &p, &q);
        return fpu::Add(x, x * fpu::Div(p, q));
    }
    const f32 w = fpu::Sub(1.0f, FromBits(Bits(x) & 0x7FFFFFFFu));
    const f32 t = w * 0.5f;
    AsinPoly(t, &p, &q);
    const f32 s = Ieee754Sqrtf(t);
    f32 r;
    if (ix > 0x3F799999) {
        const f32 a = fpu::Add(s, s * fpu::Div(p, q));
        r = fpu::Sub(pio2_hi, fpu::Sub(fpu::Add(a, a), pio2_lo));
    } else {
        const f32 df = FromBits(Bits(s) & 0xFFFFF000u);
        const f32 c = fpu::Div(fpu::Sub(t, df * df), fpu::Add(s, df));
        const f32 rr = fpu::Div(p, q);
        const f32 pp = fpu::Add(s, s) * rr;
        const f32 qq = fpu::Sub(pio4_hi, fpu::Add(df, df));
        const f32 a = fpu::Sub(pp, fpu::Sub(pio2_lo, fpu::Add(c, c)));
        r = fpu::Sub(pio4_hi, fpu::Sub(a, qq));
    }
    return hx > 0 ? r : -r;
}

// __ieee754_acosf (0x00285848).
f32 Ieee754Acosf(f32 x) {
    const s32 hx = static_cast<s32>(Bits(x));
    const s32 ix = hx & 0x7FFFFFFF;
    const f32 pio2_hi = K(0x3FC90FDA), pio2_lo = K(0x33A22168);
    if (ix == 0x3F800000) return hx > 0 ? 0.0f : K(0x40490FDB);
    if (ix > 0x3F800000) {
        const f32 d = fpu::Sub(x, x);
        return fpu::Div(d, d);
    }
    f32 p, q;
    if (ix < 0x3F000000) {
        if (ix <= 0x23000000) return K(0x3FC90FDB);
        AsinPoly(x * x, &p, &q);
        const f32 r = fpu::Div(p, q);
        return fpu::Sub(pio2_hi, fpu::Sub(x, fpu::Sub(pio2_lo, x * r)));
    }
    if (hx < 0) {
        const f32 z = fpu::Add(x, 1.0f) * 0.5f;
        AsinPoly(z, &p, &q);
        const f32 s = Ieee754Sqrtf(z);
        const f32 w = fpu::Sub(fpu::Div(p, q) * s, pio2_lo);
        const f32 a = fpu::Add(s, w);
        return fpu::Sub(K(0x40490FDA), fpu::Add(a, a));
    }
    const f32 z = fpu::Sub(1.0f, x) * 0.5f;
    const f32 s = Ieee754Sqrtf(z);
    const f32 df = FromBits(Bits(s) & 0xFFFFF000u);
    const f32 c = fpu::Div(fpu::Sub(z, df * df), fpu::Add(s, df));
    AsinPoly(z, &p, &q);
    const f32 w = fpu::Add(fpu::Div(p, q) * s, c);
    const f32 a = fpu::Add(df, w);
    return fpu::Add(a, a);
}

// __ieee754_powf (0x00286550), newlib e_powf.c as compiled (operation order of the disassembly).
// Tables in .data: bp 0x0040C4F0, dp_h 0x0040C4F8, dp_l 0x0040C500; huge * huge 0x0040C574.
f32 Ieee754Powf(f32 x, f32 y) {
    const s32 hx = static_cast<s32>(Bits(x)), hy = static_cast<s32>(Bits(y));
    const s32 ix = hx & 0x7FFFFFFF, iy = hy & 0x7FFFFFFF;
    const f32 huge = K(0x7149F2CA), tiny = K(0x0DA24260);
    if (iy == 0) return 1.0f;
    if (ix > 0x7F800000 || iy > 0x7F800000) return fpu::Add(x, y);
    s32 yisint = 0;
    if (hx < 0) {
        if (iy > 0x4B7FFFFF) {
            yisint = 2;
        } else if (iy > 0x3F7FFFFF) {
            const s32 sh = 0x96 - (iy >> 23);
            const s32 j = iy >> sh;
            if ((j << sh) == iy) yisint = 2 - (j & 1);
        }
    }
    if (iy == 0x7F800000) {
        if (ix == 0x3F800000) return fpu::Sub(y, y);
        if (ix > 0x3F800000) return hy >= 0 ? y : 0.0f;
        return hy < 0 ? -y : 0.0f;
    }
    if (iy == 0x3F800000) return hy >= 0 ? x : fpu::Div(1.0f, x);
    if (hy == 0x40000000) return x * x;
    if (hy == 0x3F000000 && hx >= 0) return Ieee754Sqrtf(x);
    f32 ax = FromBits(Bits(x) & 0x7FFFFFFFu);
    if (ix == 0x7F800000 || ix == 0 || ix == 0x3F800000) {
        f32 z = ax;
        if (hy < 0) z = fpu::Div(1.0f, z);
        if (hx >= 0) return z;
        if (((ix - 0x3F800000) | yisint) == 0) {
            const f32 d = fpu::Sub(z, z);
            return fpu::Div(d, d);
        }
        return yisint == 1 ? -z : z;
    }
    const s32 sign = static_cast<s32>(static_cast<u32>(hx) >> 31);
    if (((sign - 1) | yisint) == 0) {
        const f32 d = fpu::Sub(x, x);
        return fpu::Div(d, d);
    }
    u32 t1bits;
    f32 t2;
    if (iy > 0x4D000000) {
        if (ix <= 0x3F7FFFF7) return hy >= 0 ? 0.0f : GRef<f32>(0x0040C574);
        if (ix > 0x3F800007) return hy > 0 ? GRef<f32>(0x0040C574) : 0.0f;
        const f32 t = fpu::Sub(x, 1.0f);
        const f32 q = t * 0.25f;
        const f32 tt = t * t;
        const f32 vl = t * K(0x36ECA570);
        f32 a = fpu::Sub(K(0x3EAAAAAB), q);
        const f32 u = t * K(0x3FB8AA00);
        a = t * a;
        const f32 b = fpu::Sub(0.5f, a);
        const f32 w = tt * b;
        const f32 v = fpu::Sub(vl, w * K(0x3FB8AA3B));
        t1bits = Bits(fpu::Add(u, v)) & 0xFFFFF000u;
        t2 = fpu::Sub(v, fpu::Sub(FromBits(t1bits), u));
    } else {
        s32 n = 0;
        s32 jx = ix;
        if (jx <= 0x7FFFFF) {
            ax = ax * 16777216.0f;
            n = -24;
            jx = static_cast<s32>(Bits(ax));
        }
        const s32 j = jx & 0x7FFFFF;
        n = n - 0x7F + (jx >> 23);
        jx = j | 0x3F800000;
        s32 k;
        if (j <= 0x1CC471) {
            k = 0;
        } else if (j <= 0x5DB3D6) {
            k = 1;
        } else {
            k = 0;
            jx += static_cast<s32>(0xFF800000u);
            n += 1;
        }
        ax = FromBits(static_cast<u32>(jx));
        const f32 bp = GRef<f32>(0x0040C4F0 + 4 * static_cast<u32>(k));
        const f32 u = fpu::Sub(ax, bp);
        const f32 v = fpu::Div(1.0f, fpu::Add(ax, bp));
        const f32 s = u * v;
        const f32 s_h = FromBits(Bits(s) & 0xFFFFF000u);
        f32 t_h = FromBits(static_cast<u32>(((jx >> 1) | 0x20000000) + (k << 21) + 0x40000));
        const f32 t_l = fpu::Sub(ax, fpu::Sub(t_h, bp));
        f32 s2 = s * s;
        f32 r = fpu::Add(s2 * K(0x3E53F142), K(0x3E6C3255));
        const f32 s_l = v * fpu::Sub(fpu::Sub(u, s_h * t_h), s_h * t_l);
        const f32 s4 = s2 * s2;
        r = fpu::Add(s2 * r, K(0x3E8BA305));
        const f32 sl2 = s_l * fpu::Add(s_h, s);
        r = fpu::Add(s2 * r, K(0x3EAAAAAB));
        r = fpu::Add(s2 * r, K(0x3EDB6DB7));
        r = fpu::Add(s2 * r, K(0x3F19999A));
        s2 = s_h * s_h;
        r = fpu::Add(s4 * r, sl2);
        t_h = FromBits(Bits(fpu::Add(fpu::Add(s2, 3.0f), r)) & 0xFFFFF000u);
        const f32 tl2 = fpu::Sub(r, fpu::Sub(fpu::Sub(t_h, 3.0f), s2));
        const f32 uu = s_h * t_h;
        const f32 vv = fpu::Add(s_l * t_h, tl2 * s);
        const f32 p_h = FromBits(Bits(fpu::Add(uu, vv)) & 0xFFFFF000u);
        const f32 p_l = fpu::Sub(vv, fpu::Sub(p_h, uu));
        const f32 z_h = p_h * K(0x3F763800);
        const f32 dp_h = GRef<f32>(0x0040C4F8 + 4 * static_cast<u32>(k));
        const f32 dp_l = GRef<f32>(0x0040C500 + 4 * static_cast<u32>(k));
        const f32 tn = fpu::IntToFloat(n);
        const f32 z_l = fpu::Add(fpu::Add(p_h * K(0x369DC3A0), p_l * K(0x3F76384F)), dp_l);
        t1bits = Bits(fpu::Add(fpu::Add(fpu::Add(z_h, z_l), dp_h), tn)) & 0xFFFFF000u;
        t2 = fpu::Sub(z_l, fpu::Sub(fpu::Sub(fpu::Sub(FromBits(t1bits), tn), dp_h), z_h));
    }
    f32 s = 1.0f;
    if (((sign - 1) | (yisint - 1)) == 0) s = -1.0f;
    const f32 t1 = FromBits(t1bits);
    const f32 y1 = FromBits(Bits(y) & 0xFFFFF000u);
    const f32 p_l = fpu::Add(fpu::Sub(y, y1) * t1, y * t2);
    f32 p_h = y1 * t1;
    const f32 z = fpu::Add(p_l, p_h);
    const s32 j = static_cast<s32>(Bits(z));
    if (j > 0x43000000) return s * huge * huge;
    if (j == 0x43000000) {
        if (fpu::Sub(z, p_h) < fpu::Add(p_l, K(0x3338AA3C))) return s * huge * huge;
    } else if ((j & 0x7FFFFFFF) > 0x43160000) {
        return s * tiny * tiny;
    } else if (static_cast<u32>(j) == 0xC3160000u) {
        if (p_l <= fpu::Sub(z, p_h)) return s * tiny * tiny;
    }
    const s32 i = j & 0x7FFFFFFF;
    s32 n = 0;
    f32 t = z;
    if (i > 0x3F000000) {
        const s32 k1 = (i >> 23) - 0x7E;
        const s32 nn = j + (0x800000 >> k1);
        const s32 k = ((nn & 0x7FFFFFFF) >> 23) - 0x7F;
        const f32 tt = FromBits(static_cast<u32>(nn & ~(0x7FFFFF >> k)));
        p_h = fpu::Sub(p_h, tt);
        n = ((nn & 0x7FFFFF) | 0x800000) >> (23 - k);
        if (!(j > -1)) n = -n;
        t = fpu::Add(p_l, p_h);
    }
    t = FromBits(Bits(t) & 0xFFFFF000u);
    const f32 u = t * K(0x3F317200);
    const f32 v = fpu::Add(fpu::Sub(p_l, fpu::Sub(t, p_h)) * K(0x3F317218), t * K(0x35BFBE8C));
    f32 zz = fpu::Add(u, v);
    const f32 tz = zz * zz;
    const f32 w = fpu::Sub(v, fpu::Sub(zz, u));
    f32 poly = fpu::Add(tz * K(0x3331BB4C), K(0xB5DDEA0E));
    const f32 wzw = fpu::Add(w, zz * w);
    poly = fpu::Add(tz * poly, K(0x388AB355));
    poly = fpu::Add(tz * poly, K(0xBB360B61));
    poly = fpu::Add(tz * poly, K(0x3E2AAAAB));
    const f32 t1b = fpu::Sub(zz, tz * poly);
    const f32 r = fpu::Sub(fpu::Div(zz * t1b, fpu::Sub(t1b, 2.0f)), wzw);
    zz = fpu::Sub(1.0f, fpu::Sub(r, zz));
    const s32 jz = static_cast<s32>(Bits(zz) + (static_cast<u32>(n) << 23));
    if ((jz >> 23) <= 0) zz = Scalbnf(zz, n);
    else zz = FromBits(static_cast<u32>(jz));
    return s * zz;
}

// Shared tail of the asinf / acosf wrappers for |x| > 1 (SVID/XOPEN error path as in atan2f).
f32 DomainError(f32 x, f32 z) {
    if (GRef<s32>(0x0040CA0C) == -1) return z;
    if ((Bits(x) & 0x7FFFFFFFu) > 0x7F800000u) return z;
    if (!(1.0f < FromBits(Bits(x) & 0x7FFFFFFFu))) return z;
    GRef<s32>(GRef<u32>(0x00401D74)) = 33;
    return 0.0f;
}

}  // namespace

// atan2f wrapper (0x00284CB8): with _LIB_VERSION (0x0040CA0C) not _IEEE_ (-1) and x = y = 0, the
// SVID/XOPEN error path: matherr (0x00288178) returns 0, so errno (first word of the reent struct
// at *0x00401D74) becomes EDOM (33) and the result is the exception's retval, +0.0.
f32 atan2f(f32 y, f32 x) {
    const f32 z = Ieee754Atan2f(y, x);
    if (GRef<s32>(0x0040CA0C) == -1) return z;
    if ((Bits(x) & 0x7FFFFFFFu) > 0x7F800000u || (Bits(y) & 0x7FFFFFFFu) > 0x7F800000u) return z;
    if (x == 0.0f && y == 0.0f) {
        GRef<s32>(GRef<u32>(0x00401D74)) = 33;
        return 0.0f;
    }
    return z;
}

// asinf (0x00284BB8) / acosf (0x00284AB8) wrappers: |x| > 1 (not NaN, _LIB_VERSION not _IEEE_)
// sets errno EDOM and returns the exception retval +0.0.
f32 asinf(f32 x) { return DomainError(x, Ieee754Asinf(x)); }
f32 acosf(f32 x) { return DomainError(x, Ieee754Acosf(x)); }

// powf wrapper (0x00284F20): z = __ieee754_powf(x, y); with _LIB_VERSION not _IEEE_ and finite
// operands, the SVID/XOPEN error paths set errno (first word of the reent struct at *0x00401D74):
// 0 ** 0 and 0 ** negative (EDOM 33, retval 0 / -HUGE_VAL not reached by the game), an underflow
// to 0 (ERANGE 34, retval 0). NaN / infinite operands cannot occur on the EE.
f32 powf(f32 x, f32 y) {
    const f32 z = Ieee754Powf(x, y);
    if (GRef<s32>(0x0040CA0C) == -1) return z;
    const u32 ax = Bits(x) & 0x7FFFFFFFu, ay = Bits(y) & 0x7FFFFFFFu;
    if (ax > 0x7F800000u || ay > 0x7F800000u) return z;
    if (x == 0.0f) {
        if (y == 0.0f || (ay < 0x7F800000u && y < 0.0f)) GRef<s32>(GRef<u32>(0x00401D74)) = 33;
        return z;
    }
    if (z == 0.0f && ax < 0x7F800000u && ay < 0x7F800000u) {
        GRef<s32>(GRef<u32>(0x00401D74)) = 34;
        return 0.0f;
    }
    return z;
}

}  // namespace ee
