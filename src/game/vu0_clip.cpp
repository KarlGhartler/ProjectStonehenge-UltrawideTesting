// VU0 visibility helpers of the world renderer (docs/subsystems/rendering.md "VU0 visibility
// helpers"): func_001367B0 loads a clip matrix into VU0 vf8-vf11 and the clip tests that follow use
// it; func_00108FC0 clips a triangle in homogeneous space. The registers are kept here as a host copy (vf8-vf11). Every caller loads the matrix right
// before its tests and calls no other VU0 code that writes vf8-vf11 in between (checked for
// 0x0013A6B8, SelectUnitModelLods, 0x0014AA78 and 0x0014ACB0 when they were translated).
#include "game/game.h"

#include <cmath>

#include "common/vu0_math.h"

namespace ac04::game {

using guest::GRef;

namespace {

vu0::Vec4 g_ClipMatrix[4];  // VU0 vf8-vf11

vu0::Vec4 Load(u32 p) { return {GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; }

void CopyVec(u32 dst, u32 src) {
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(dst + i) = GRef<u32>(src + i);
}

void Store(u32 p, const vu0::Vec4& v) {
    GRef<f32>(p) = v.x;
    GRef<f32>(p + 4) = v.y;
    GRef<f32>(p + 8) = v.z;
    GRef<f32>(p + 12) = v.w;
}

vu0::Vec4 Transform(const vu0::Vec4& v) {
    return vu0::MulMatrix(g_ClipMatrix[0], g_ClipMatrix[1], g_ClipMatrix[2], g_ClipMatrix[3], v);
}

// vclipw.xyz: bits 0-5 = x > |w|, x < -|w|, y > |w|, y < -|w|, z > |w|, z < -|w|.
u32 ClipBits(const vu0::Vec4& v) {
    const f32 w = std::fabs(v.w);
    u32 b = 0;
    if (v.x > w) b |= 0x01;
    if (v.x < -w) b |= 0x02;
    if (v.y > w) b |= 0x04;
    if (v.y < -w) b |= 0x08;
    if (v.z > w) b |= 0x10;
    if (v.z < -w) b |= 0x20;
    return b;
}

}  // namespace

// EE: 0x001367B0
// Loads the 4 x 4 clip matrix m (column-major) into VU0 vf8-vf11.
void func_001367B0(u32 m) {
    for (u32 i = 0; i < 4; ++i) g_ClipMatrix[i] = Load(m + 0x10 * i);
}

// EE: 0x001367C8
// v = M * p (written to out); returns 1 when any clip bit is set (outside the frustum), else 0.
s32 func_001367C8(u32 p, u32 out) {
    const vu0::Vec4 v = Transform(Load(p));
    const u32 bits = ClipBits(v);
    Store(out, v);
    return bits != 0 ? 1 : 0;
}

// EE: 0x00136818
// Two points (a box's corners): bit 0 = b outside, bit 1 = a outside; out = the transformed corner
// with the smaller w (the MAC flag of `vsub.w vf0, va, vb`: Zw or Sw -> va).
s32 func_00136818(u32 a, u32 b, u32 out) {
    const vu0::Vec4 va = Transform(Load(a));
    const vu0::Vec4 vb = Transform(Load(b));
    const u32 ca = ClipBits(va), cb = ClipBits(vb);
    s32 r = 0;
    if (cb != 0) r |= 1;
    if (ca != 0) r |= 2;
    const f32 dw = vu0::Sub(va.w, vb.w);
    Store(out, dw <= 0.0f ? va : vb);
    return r;
}

namespace {

// VU0 data memory as func_00108FC0 uses it (vsqi / vlqi through vi7 / vi8; 256 qwords, addresses
// wrap). Only that function touches it, and it reads back only what it wrote in the same call.
vu0::Vec4 g_Vu0Data[256];

vu0::Vec4 Lerp(const vu0::Vec4& from, const vu0::Vec4& to, f32 t) {
    return {vu0::Add(vu0::Mul(vu0::Sub(to.x, from.x), t), from.x), vu0::Add(vu0::Mul(vu0::Sub(to.y, from.y), t), from.y),
            vu0::Add(vu0::Mul(vu0::Sub(to.z, from.z), t), from.z), vu0::Add(vu0::Mul(vu0::Sub(to.w, from.w), t), from.w)};
}

}  // namespace

// EE: 0x00108FC0
// Clips the triangle of vertices {a[i], b[i], pos[i]} (i = 0..2; qword arrays) against the
// homogeneous frustum of the 4 x 4 matrix m (column-major) and writes the result polygon to out
// as {a, b, pos} triples (0x30 each); returns its vertex count (3 when nothing is clipped, 0 when
// the triangle lies outside one plane or fewer than 3 vertices remain). The trivial tests use the
// vclipw.xyz flags (|w|); the clipping proper (Sutherland-Hodgman, planes x, -x, y, -y, z, -z)
// keeps a vertex while s x c - w is negative (the MAC sign flag, so -0 counts as inside) and
// interpolates all three qwords at |d_in / (d_out - d_in)| with d = c - s x w, re-transforming the
// new position. Each pass runs through VU0 data memory (buffers at qword 0 and 0x20).
s32 func_00108FC0(u32 out, u32 m, u32 a, u32 b, u32 pos) {
    const vu0::Vec4 c0 = Load(m), c1 = Load(m + 0x10), c2 = Load(m + 0x20), c3 = Load(m + 0x30);
    const auto xf = [&](const vu0::Vec4& v) { return vu0::MulMatrix(c0, c1, c2, c3, v); };
    vu0::Vec4 clip[3];
    for (u32 i = 0; i < 3; ++i) clip[i] = xf(Load(pos + i * 0x10));
    const u32 flags = (ClipBits(clip[0]) << 12) | (ClipBits(clip[1]) << 6) | ClipBits(clip[2]);
    if (flags == 0) {
        for (u32 i = 0; i < 3; ++i) {
            CopyVec(out + i * 0x30, a + i * 0x10);
            CopyVec(out + i * 0x30 + 0x10, b + i * 0x10);
            CopyVec(out + i * 0x30 + 0x20, pos + i * 0x10);
        }
        return 3;
    }
    static const u32 kAllOut[6] = {0x3EFBE, 0x3DF7D, 0x3BEFB, 0x37DF7, 0x2FBEF, 0x1F7DF};
    for (u32 k : kAllOut)
        if ((k | flags) == 0x3FFFF) return 0;
    u32 wr = 0;
    const auto put = [&](const vu0::Vec4& v) { g_Vu0Data[wr++ & 0xFF] = v; };
    for (u32 i = 0; i < 3; ++i) {
        put(Load(a + i * 0x10));
        put(Load(b + i * 0x10));
        put(Load(pos + i * 0x10));
        put(clip[i]);
    }
    s32 count = 3;
    u32 rd = 0;
    wr = 0x20;
    bool second = false;
    f32 s = 1.0f;
    for (u32 plane = 1; plane != 0x40; plane <<= 1) {
        const s32 n = count;
        u32 pi = static_cast<u32>(n - 1) * 4 + rd;
        vu0::Vec4 p[4];
        for (u32 j = 0; j < 4; ++j) p[j] = g_Vu0Data[pi++ & 0xFF];
        vu0::Vec4 c[4];
        for (u32 j = 0; j < 4; ++j) c[j] = g_Vu0Data[rd++ & 0xFF];
        for (s32 left = n; left > 0; --left) {
            const auto comp = [&](const vu0::Vec4& v) { return (plane & 3) != 0 ? v.x : (plane & 0xC) != 0 ? v.y : v.z; };
            const f32 cp = comp(p[3]), cc = comp(c[3]);
            u32 out_bits = 0;
            if (!std::signbit(vu0::Sub(vu0::Mul(s, cp), p[3].w))) out_bits |= 1;
            if (!std::signbit(vu0::Sub(vu0::Mul(s, cc), c[3].w))) out_bits |= 2;
            const f32 dp = vu0::Add(vu0::Mul(vu0::Mul(s, p[3].w), -1.0f), cp);
            const f32 dc = vu0::Add(vu0::Mul(vu0::Mul(s, c[3].w), -1.0f), cc);
            if (out_bits == 0) {
                for (u32 j = 0; j < 4; ++j) put(p[j]);
            } else if (out_bits == 2) {
                const f32 t = std::fabs(vu0::Mul(dp, vu0::Div(1.0f, vu0::Sub(dc, dp))));
                for (u32 j = 0; j < 4; ++j) put(p[j]);
                const vu0::Vec4 na = Lerp(p[0], c[0], t), nb = Lerp(p[1], c[1], t), np = Lerp(p[2], c[2], t);
                ++count;
                put(na);
                put(nb);
                put(np);
                put(xf(np));
            } else if (out_bits == 1) {
                const f32 t = std::fabs(vu0::Mul(dc, vu0::Div(1.0f, vu0::Sub(dp, dc))));
                const vu0::Vec4 na = Lerp(c[0], p[0], t), nb = Lerp(c[1], p[1], t), np = Lerp(c[2], p[2], t);
                put(na);
                put(nb);
                put(np);
                put(xf(np));
            } else {
                --count;
            }
            for (u32 j = 0; j < 4; ++j) p[j] = c[j];
            for (u32 j = 0; j < 4; ++j) c[j] = g_Vu0Data[rd++ & 0xFF];
        }
        if (!second) {
            second = true;
            rd = 0x20;
            wr = 0;
        } else {
            second = false;
            rd = 0;
            wr = 0x20;
        }
        s = vu0::Mul(s, -1.0f);
        if (count - 2 <= 0) return 0;
    }
    for (s32 i = 0; i < count; ++i) {
        for (u32 j = 0; j < 3; ++j) Store(out + static_cast<u32>(i) * 0x30 + j * 0x10, g_Vu0Data[rd++ & 0xFF]);
        ++rd;
    }
    return count;
}

// EE: 0x00109500
// Projects `count` (at least one) clipped vertices {st, colour, position} (0x30 each) from `in`
// through the 4 x 4 matrix m to `out`: Q = 1 / w; st x Q; colour to integers with the fog value
// max(min((w x fog.z + fog.x) x fog.y, fog.w), 0) as alpha; position x Q to 12.4 fixed point with
// the fog value (12.4) as w. Returns the end of the output.
u32 func_00109500(u32 out, u32 in, u32 m, u32 fog, s32 count) {
    const vu0::Vec4 c0 = Load(m), c1 = Load(m + 0x10), c2 = Load(m + 0x20), c3 = Load(m + 0x30), f = Load(fog);
    do {
        const vu0::Vec4 st = Load(in), col = Load(in + 0x10);
        const vu0::Vec4 r = vu0::MulMatrix(c0, c1, c2, c3, Load(in + 0x20));
        const f32 q = vu0::Div(1.0f, r.w);
        f32 w = vu0::Mul(vu0::Add(vu0::Mul(r.w, f.z), f.x), f.y);
        if (f.w < w) w = f.w;
        Store(out, {vu0::Mul(st.x, q), vu0::Mul(st.y, q), vu0::Mul(st.z, q), vu0::Mul(st.w, q)});
        const f32 a = w < 0.0f ? 0.0f : w;
        GRef<s32>(out + 0x10) = vu0::FloatToInt0(col.x);
        GRef<s32>(out + 0x14) = vu0::FloatToInt0(col.y);
        GRef<s32>(out + 0x18) = vu0::FloatToInt0(col.z);
        GRef<s32>(out + 0x1C) = vu0::FloatToInt0(a);
        GRef<s32>(out + 0x20) = vu0::FloatToInt4(vu0::Mul(r.x, q));
        GRef<s32>(out + 0x24) = vu0::FloatToInt4(vu0::Mul(r.y, q));
        GRef<s32>(out + 0x28) = vu0::FloatToInt4(vu0::Mul(r.z, q));
        GRef<s32>(out + 0x2C) = vu0::FloatToInt4(w);
        out += 0x30;
        in += 0x30;
    } while (--count > 0);
    return out;
}

}  // namespace ac04::game
