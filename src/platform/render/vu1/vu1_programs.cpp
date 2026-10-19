// CPU reimplementations of the VU1 model programs B (0x00298AC0) and C (0x00299820), the glow fin
// program D (0x0029A930) and the terrain
// program T (.vudata 0x00402200), following
// their microcode pair by pair (docs/subsystems/vu-programs.md "Model programs B and C"; listings:
// tools/research/vu_disasm.py). Arithmetic uses the VU helpers (src/common/vu0_math.h); the results
// only reach the GS, so the VU rounding hypothesis affects pixels, never guest RAM.
//
// Memory is the real VU1 data memory image: inputs at TOP, the clipper's scratch polygons at the
// absolute addresses 0x20/0x48 and the backface history at absolute 0/1, as on the PS2.
// The micro helpers rely on the dynamic rounding mode (vu0::micro::ChopScope).
#ifdef _MSC_VER
#pragma fenv_access(on)
#endif
#include "platform/render/vu1/vu1_programs.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

#include "common/vu0_math.h"

namespace ac04::render::vu1 {

namespace vu0 = ac04::vu0::micro;  // VU1 micro programs: chop rounding
using vu0::Vec4;

namespace {

std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

Vec4 LoadF(const Vu1State& vu, std::uint32_t a) { return {vu.F(a, 0), vu.F(a, 1), vu.F(a, 2), vu.F(a, 3)}; }

void StoreF(Vu1State& vu, std::uint32_t a, const Vec4& v) {
    std::uint32_t* q = vu.Qword(a);
    q[0] = Bits(v.x);
    q[1] = Bits(v.y);
    q[2] = Bits(v.z);
    q[3] = Bits(v.w);
}

void StoreRaw(Vu1State& vu, std::uint32_t a, const std::uint32_t* w) {
    std::uint32_t* q = vu.Qword(a);
    for (int i = 0; i < 4; ++i) q[i] = w[i];
}

void StoreI(Vu1State& vu, std::uint32_t a, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t w) {
    std::uint32_t* q = vu.Qword(a);
    q[0] = static_cast<std::uint32_t>(x);
    q[1] = static_cast<std::uint32_t>(y);
    q[2] = static_cast<std::uint32_t>(z);
    q[3] = static_cast<std::uint32_t>(w);
}

// itofN: integer to float scaled by 2^-shift (exact for the 16-bit inputs the meshes hold).
Vec4 Itof(const Vu1State& vu, std::uint32_t a, int shift) {
    const float k = shift == 12 ? 1.0f / 4096.0f : 1.0f;
    return {vu0::IntToFloat(vu.I(a, 0)) * k, vu0::IntToFloat(vu.I(a, 1)) * k, vu0::IntToFloat(vu.I(a, 2)) * k,
            vu0::IntToFloat(vu.I(a, 3)) * k};
}

// ftoiN: v * 2^shift truncated toward zero, saturated.
std::int32_t Ftoi(float v, int shift) {
    const double d = static_cast<double>(v) * static_cast<double>(1 << shift);
    if (d >= 2147483647.0) return 0x7FFFFFFF;
    if (d <= -2147483648.0) return static_cast<std::int32_t>(0x80000000u);
    return static_cast<std::int32_t>(d);
}

void StoreFtoi(Vu1State& vu, std::uint32_t a, const Vec4& v, int shift) {
    StoreI(vu, a, Ftoi(v.x, shift), Ftoi(v.y, shift), Ftoi(v.z, shift), Ftoi(v.w, shift));
}

Vec4 MulM(const Vu1State& vu, std::uint32_t m, const Vec4& v) {
    return vu0::MulMatrix(LoadF(vu, m), LoadF(vu, m + 1), LoadF(vu, m + 2), LoadF(vu, m + 3), v);
}

// A matrix (four columns) read from VU memory once, for loops that do not write it.
struct Mat {
    Vec4 c[4];
    void Load(const Vu1State& vu, std::uint32_t m) {
        for (std::uint32_t i = 0; i < 4; ++i) c[i] = LoadF(vu, m + i);
    }
    Vec4 Mul(const Vec4& v) const { return vu0::MulMatrix(c[0], c[1], c[2], c[3], v); }
};

Vec4 Sub4(const Vec4& a, const Vec4& b) {
    return {vu0::Sub(a.x, b.x), vu0::Sub(a.y, b.y), vu0::Sub(a.z, b.z), vu0::Sub(a.w, b.w)};
}
Vec4 Add4(const Vec4& a, const Vec4& b) {
    return {vu0::Add(a.x, b.x), vu0::Add(a.y, b.y), vu0::Add(a.z, b.z), vu0::Add(a.w, b.w)};
}
Vec4 Mul4(const Vec4& a, float s) { return {vu0::Mul(a.x, s), vu0::Mul(a.y, s), vu0::Mul(a.z, s), vu0::Mul(a.w, s)}; }

// ILW: the 16-bit integer of field c.
std::int32_t I16(const Vu1State& vu, std::uint32_t a, int c) { return static_cast<std::int16_t>(vu.I(a, c) & 0xFFFF); }

// MFIR: a 16-bit integer sign-extended to 32 bits.
std::uint32_t Mfir(std::int32_t v) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(v))); }

// CLIP: +x -x +y -y +z -z against |w|.
std::uint32_t ClipFlags(const Vec4& c) {
    const float w = std::fabs(c.w);
    std::uint32_t f = 0;
    if (c.x > w) f |= 1;
    if (c.x < -w) f |= 2;
    if (c.y > w) f |= 4;
    if (c.y < -w) f |= 8;
    if (c.z > w) f |= 16;
    if (c.z < -w) f |= 32;
    return f;
}

float Comp(const Vec4& v, int c) { return c == 0 ? v.x : (c == 1 ? v.y : v.z); }

struct ModelRun {
    Vu1State& vu;
    bool c;  // program C or E (lit, two passes, backface); else B or A (one pass)
    std::uint32_t top;
    bool a = false;  // program A: B's pass structure and clipper, C's lit colour (23.y bit 1)
    bool e = false;  // program E: C with the screen Z x 16 and no +0.001 in the clipper
};

// The draw loop (B 0x151, C 0x15E): `count` vertices from `src` (4 qwords each) into the output buffer
// `out`; tag1/tag2 are the GIF tags of the two passes (22/25 for strips, 21/31 for clipped fans).
void Draw(ModelRun& r, std::int32_t count, std::uint32_t src, std::uint32_t out, std::uint32_t tag1[4],
          std::uint32_t tag2[4], std::int32_t vi10, std::int32_t vi09) {
    Vu1State& vu = r.vu;
    const std::uint32_t top = r.top;
    const Vec4 fogp = LoadF(vu, top + 30);
    if (!r.c) {
        tag1[0] = Mfir(0x8000 + count);
        StoreRaw(vu, out++, tag1);
        Mat world;  // programs B and A write only their output buffer in this loop
        world.Load(vu, top);
        // Program A: C's lit colour when 23.y bit 1 is set (docs/subsystems/vu-programs.md).
        const bool lit_on = r.a && (I16(vu, top + 23, 1) & 2) != 0;
        Mat normal, colour;
        Vec4 rows[4], ambient;
        if (lit_on) {
            normal.Load(vu, top + 8);
            for (std::uint32_t k = 0; k < 4; ++k) rows[k] = LoadF(vu, top + 12 + k);
            colour.Load(vu, top + 16);
            ambient = LoadF(vu, top + 20);
        }
        for (std::int32_t i = 0; i < count; ++i, src += 4) {
            const Vec4 st = Itof(vu, src, 12);
            Vec4 col = Itof(vu, src + 1, 0);
            const Vec4 pos = LoadF(vu, src + 2);
            if (lit_on) {
                Vec4 nrm = Itof(vu, src + 3, 12);
                nrm.w = 0.0f;
                const Vec4 n = normal.Mul(nrm);
                const float nc[4] = {n.x, n.y, n.z, n.w};
                Vec4 term[4];
                for (int k = 0; k < 4; ++k) {
                    const Vec4 row = rows[k];
                    term[k] = {std::fmax(vu0::Mul(row.x, nc[k]), 0.0f), std::fmax(vu0::Mul(row.y, nc[k]), 0.0f),
                               std::fmax(vu0::Mul(row.z, nc[k]), 0.0f), std::fmax(vu0::Mul(row.w, nc[k]), 0.0f)};
                }
                Vec4 inten = Add4(Add4(Add4(term[0], term[1]), term[2]), term[3]);
                Vec4 lit = colour.Mul(inten);
                lit = {std::fmax(lit.x, 0.0f), std::fmax(lit.y, 0.0f), std::fmax(lit.z, 0.0f), std::fmax(lit.w, 0.0f)};
                lit.x = vu0::Add(lit.x, ambient.x);
                lit.y = vu0::Add(lit.y, ambient.y);
                lit.z = vu0::Add(lit.z, ambient.z);
                lit.w = vu0::Mul(col.w, 1.0f);
                lit.x = std::fmin(std::fmax(vu0::Mul(col.x, lit.x), 0.0f), 255.0f);
                lit.y = std::fmin(std::fmax(vu0::Mul(col.y, lit.y), 0.0f), 255.0f);
                lit.z = std::fmin(std::fmax(vu0::Mul(col.z, lit.z), 0.0f), 255.0f);
                col = lit;
            }
            const Vec4 p = world.Mul(pos);
            float fog = vu0::Mul(fogp.x, 1.0f);
            fog = vu0::Sub(fog, p.w);
            fog = vu0::Mul(fog, fogp.y);
            fog = std::fmax(fog, 0.0f);
            fog = std::fmin(fog, 255.0f);
            const float q = vu0::Div(1.0f, p.w);
            const Vec4 xyzf{vu0::Mul(p.x, q), vu0::Mul(p.y, q), vu0::Mul(p.z, q), fog};
            StoreF(vu, out++, Mul4(st, q));
            StoreFtoi(vu, out++, col, 0);
            StoreFtoi(vu, out++, xyzf, 4);
        }
        return;
    }
    std::uint32_t out2 = out + 3 * static_cast<std::uint32_t>(count) + 3;
    const std::int32_t flags = I16(vu, top + 23, 1);
    if ((flags & 4) != 0) {
        tag2[0] = Mfir(0x8000 + count);
        tag1[0] = static_cast<std::uint32_t>(count);
    } else {
        tag1[0] = Mfir(0x8000 + count);
    }
    std::uint32_t q26[4], q28[4], q27[4];
    std::memcpy(q26, vu.Qword(top + 26), 16);
    std::memcpy(q28, vu.Qword(top + 28), 16);
    std::memcpy(q27, vu.Qword(top + 27), 16);
    StoreRaw(vu, out++, q26);
    StoreRaw(vu, out++, q28);
    StoreRaw(vu, out++, tag1);
    StoreRaw(vu, out2++, q26);
    StoreRaw(vu, out2++, q27);
    StoreRaw(vu, out2++, tag2);
    // Backface sense: 29.x, 23.z parity and the triangle index.
    std::int32_t vi08 = I16(vu, top + 29, 0) == 0 ? (I16(vu, top + 23, 2) & 1) : ((I16(vu, top + 23, 2) & 1) != 0 ? 0 : 1);
    if ((vi09 & 1) != 0) vi08 = vi08 != 0 ? 0 : 1;
    std::int32_t vi07 = 2;
    // The constants top+0..top+20, read once; again per vertex only when the backface history
    // (absolute 0 and 1, written in the loop) lies inside them.
    Mat world, normal, colour;
    Vec4 rows[4], ambient;
    auto load_constants = [&]() {
        world.Load(vu, top);
        normal.Load(vu, top + 8);
        for (std::uint32_t k = 0; k < 4; ++k) rows[k] = LoadF(vu, top + 12 + k);
        colour.Load(vu, top + 16);
        ambient = LoadF(vu, top + 20);
    };
    load_constants();
    const bool reload = (flags & 1) != 0 && (((0u - top) & (kVuMemQwords - 1)) <= 20 || ((1u - top) & (kVuMemQwords - 1)) <= 20);
    for (std::int32_t i = 0; i < count; ++i, src += 4) {
        if (reload && i > 0) load_constants();
        const Vec4 st = Itof(vu, src, 12);
        const Vec4 col = Itof(vu, src + 1, 0);
        const Vec4 pos = LoadF(vu, src + 2);
        Vec4 nrm = Itof(vu, src + 3, 12);
        nrm.w = 0.0f;
        const Vec4 n = normal.Mul(nrm);
        const Vec4 p = world.Mul(pos);
        // Light intensities: each row × the normal component, clamped per term, then summed.
        const float nc[4] = {n.x, n.y, n.z, n.w};
        Vec4 term[4];
        for (int k = 0; k < 4; ++k) {
            const Vec4 row = rows[k];
            term[k] = {std::fmax(vu0::Mul(row.x, nc[k]), 0.0f), std::fmax(vu0::Mul(row.y, nc[k]), 0.0f),
                       std::fmax(vu0::Mul(row.z, nc[k]), 0.0f), std::fmax(vu0::Mul(row.w, nc[k]), 0.0f)};
        }
        Vec4 inten = term[0];
        inten = Add4(inten, term[1]);
        inten = Add4(inten, term[2]);
        inten = Add4(inten, term[3]);
        const float q = vu0::Div(1.0f, p.w);
        const bool env_dark = vu0::Add(0.0f, n.y) < 0.0f;
        // Reflection pass texture coordinates and colour.
        Vec4 env{vu0::Add(vu0::Sub(0.0f, n.x), 1.0f), vu0::Add(vu0::Add(0.0f, n.z), 1.0f), 1.0f, 1.0f};
        env.x = vu0::Mul(env.x, 0.5f);
        env.y = vu0::Mul(env.y, 0.5f);
        env = Mul4(env, q);
        Vec4 env_col{128.0f, 128.0f, 128.0f, 128.0f};
        if (env_dark) env_col.x = env_col.y = env_col.z = 0.0f;
        // Lit colour.
        Vec4 lit = colour.Mul(inten);
        lit = {std::fmax(lit.x, 0.0f), std::fmax(lit.y, 0.0f), std::fmax(lit.z, 0.0f), std::fmax(lit.w, 0.0f)};
        lit.x = vu0::Add(lit.x, ambient.x);
        lit.y = vu0::Add(lit.y, ambient.y);
        lit.z = vu0::Add(lit.z, ambient.z);
        lit.w = vu0::Mul(col.w, 1.0f);
        lit.x = std::fmin(std::fmax(vu0::Mul(col.x, lit.x), 0.0f), 255.0f);
        lit.y = std::fmin(std::fmax(vu0::Mul(col.y, lit.y), 0.0f), 255.0f);
        lit.z = std::fmin(std::fmax(vu0::Mul(col.z, lit.z), 0.0f), 255.0f);
        // Screen position and fog.
        float fog = vu0::Mul(p.w, -1.0f);
        fog = vu0::Add(fog, fogp.x);
        fog = vu0::Mul(fog, fogp.y);
        fog = std::fmin(std::fmax(fog, 0.0f), 255.0f);
        Vec4 xyzf{vu0::Mul(p.x, q), vu0::Mul(p.y, q), vu0::Mul(p.z, q), fog};
        if (r.e) xyzf.z = vu0::Mul(xyzf.z, 16.0f);  // program E: muli.z 16.0 before ftoi4
        if ((flags & 1) != 0) {
            const Vec4 cur{vu0::Add(0.0f, xyzf.x), vu0::Add(0.0f, xyzf.y), 0.0f, 1.0f};
            const Vec4 prev1 = LoadF(vu, 1);
            if (vi07 > 0) {
                --vi07;
            } else {
                const Vec4 prev2 = LoadF(vu, 0);
                const Vec4 a = Sub4(prev2, prev1);
                const Vec4 b = Sub4(cur, prev1);
                // opmula/opmsub: z of a x b, or of b x a.
                const float z = vi08 == 0 ? vu0::Sub(vu0::Mul(a.x, b.y), vu0::Mul(b.x, a.y))
                                          : vu0::Sub(vu0::Mul(b.x, a.y), vu0::Mul(a.x, b.y));
                if (vi10 == 0) vi08 = vi08 == 0 ? 1 : 0;
                if (z < 0.0f) xyzf.w = vu0::Add(xyzf.w, 2048.0f);  // ADC: kicked, not drawn
            }
            StoreF(vu, 0, prev1);
            StoreF(vu, 1, cur);
        }
        StoreF(vu, out++, Mul4(st, q));
        StoreF(vu, out2++, env);
        StoreFtoi(vu, out2++, env_col, 0);
        StoreFtoi(vu, out2++, xyzf, 4);
        if ((flags & 2) != 0) {
            StoreFtoi(vu, out++, lit, 0);
        } else {
            StoreFtoi(vu, out++, col, 0);
        }
        StoreFtoi(vu, out++, xyzf, 4);
    }
}

// The polygon clipper (B 0x052, C 0x054): triangle at `src` (3 vertices) against the six clip
// planes; returns the vertex count of the result, left at absolute 0x20 or 0x48 in the input
// format (returned in `result`).
std::int32_t ClipTriangle(ModelRun& r, std::uint32_t src, const Vec4 clip[3], std::uint32_t& result) {
    Vu1State& vu = r.vu;
    const std::uint32_t top = r.top;
    const float eps = r.c && !r.e ? vu0::Add(0.0f, 0.001f) : 0.0f;
    std::uint32_t dst = 0x20;
    for (int k = 0; k < 3; ++k) {
        const std::uint32_t v = src + 4 * static_cast<std::uint32_t>(k);
        StoreF(vu, dst++, Itof(vu, v, 12));
        StoreF(vu, dst++, Itof(vu, v + 1, 0));
        std::memcpy(vu.Qword(dst++), vu.Qword(v + 2), 16);
        StoreF(vu, dst++, Itof(vu, v + 3, 12));
        StoreF(vu, dst++, clip[k]);
    }
    std::int32_t count = 3;
    float sign = 1.0f;
    std::uint32_t in = 0x20, outb = 0x48;
    for (std::uint32_t plane = 1; plane <= 0x20; plane <<= 1) {
        const int comp = (plane & 3) != 0 ? 0 : ((plane & 0xC) != 0 ? 1 : 2);
        std::uint32_t o = outb;
        std::int32_t out_count = count;
        Vec4 a[5], b[5];
        for (int q = 0; q < 5; ++q) a[q] = LoadF(vu, in + 5 * static_cast<std::uint32_t>(count - 1) + q);
        for (std::int32_t e = 0; e < count; ++e) {
            for (int q = 0; q < 5; ++q) b[q] = LoadF(vu, in + 5 * static_cast<std::uint32_t>(e) + q);
            const float ac = Comp(a[4], comp), bc = Comp(b[4], comp);
            const float da = vu0::Sub(vu0::Mul(sign, ac), a[4].w);
            const float db = vu0::Sub(vu0::Mul(sign, bc), b[4].w);
            const int code = (da < 0.0f ? 0 : 1) + (db < 0.0f ? 0 : 2);
            if (code == 0 || code == 2) {
                for (int q = 0; q < 5; ++q) StoreF(vu, o++, a[q]);
            }
            if (code == 2 || code == 1) {
                // Intersection, with the factor measured from the inside end.
                const float aa = vu0::Add(vu0::Mul(vu0::Mul(sign, a[4].w), -1.0f), ac);
                const float bb = vu0::Add(vu0::Mul(vu0::Mul(sign, b[4].w), -1.0f), bc);
                const Vec4* from = code == 2 ? a : b;
                const Vec4* to = code == 2 ? b : a;
                const float num = code == 2 ? aa : bb;
                const float den = code == 2 ? vu0::Sub(bb, aa) : vu0::Sub(aa, bb);
                const float qinv = vu0::Div(1.0f, den);
                float t = std::fabs(vu0::Mul(num, qinv));
                if (r.c && !r.e) t = vu0::Add(t, eps);
                Vec4 iv[5];
                for (int q = 0; q < 4; ++q) iv[q] = Add4(Mul4(Sub4(to[q], from[q]), t), from[q]);
                iv[4] = MulM(vu, top + 4, iv[2]);
                for (int q = 0; q < 5; ++q) StoreF(vu, o++, iv[q]);
                if (code == 2) ++out_count;
            }
            if (code == 3) --out_count;
            for (int q = 0; q < 5; ++q) a[q] = b[q];
        }
        count = out_count;
        std::swap(in, outb);
        sign = vu0::Mul(sign, -1.0f);
        if (count - 2 <= 0) return count;
    }
    // Back to the input format, into the other buffer.
    std::uint32_t o = outb;
    for (std::int32_t e = 0; e < count; ++e) {
        const std::uint32_t v = in + 5 * static_cast<std::uint32_t>(e);
        StoreFtoi(vu, o++, LoadF(vu, v), 12);
        StoreFtoi(vu, o++, LoadF(vu, v + 1), 0);
        std::memcpy(vu.Qword(o++), vu.Qword(v + 2), 16);
        StoreFtoi(vu, o++, LoadF(vu, v + 3), 12);
    }
    result = outb;
    return count;
}

// kind: 0 B, 1 C, 2 A, 3 E.
void RunModelStrip(Vu1State& vu, int kind, std::vector<std::uint8_t>& gif) {
    ModelRun r{vu, kind == 1 || kind == 3, vu.top};
    r.a = kind == 2;
    r.e = kind == 3;
    const std::uint32_t top = vu.top;
    const std::int32_t n = I16(vu, top + 23, 0);
    const std::int32_t clip = I16(vu, top + 29, 1);
    auto out_for = [&](std::int32_t vi09) { return top + 0x7E + ((vi09 & 1) != 0 ? 0x68u : 0u); };
    std::uint32_t tag1[4], tag2[4];
    if (clip == 0) {
        std::memcpy(tag1, vu.Qword(top + 22), 16);
        std::memcpy(tag2, vu.Qword(top + 25), 16);
        const std::int32_t vi09 = n - 3;
        Draw(r, n, top + 0x20, out_for(vi09), tag1, tag2, 0, vi09);
        Xgkick(vu, out_for(vi09), gif);
        return;
    }
    for (std::int32_t vi09 = 0;; ++vi09) {
        const std::uint32_t src = top + 0x20 + 4 * static_cast<std::uint32_t>(vi09);
        const std::uint32_t out = out_for(vi09);
        Vec4 cp[3];
        std::int32_t near_flag[3];
        for (int k = 0; k < 3; ++k) {
            cp[k] = MulM(vu, top + 4, LoadF(vu, src + 2 + 4 * static_cast<std::uint32_t>(k)));
            const float d = vu0::Sub(cp[k].w, 8192.0f);
            near_flag[k] = d < 0.0f ? 0x10 : (d == 0.0f ? 0x01 : 0);
        }
        std::memcpy(tag1, vu.Qword(top + 22), 16);
        std::memcpy(tag2, vu.Qword(top + 25), 16);
        bool culled = false;
        if (near_flag[0] == 0 && near_flag[1] == 0 && near_flag[2] == 0) {
            Draw(r, 3, src, out, tag1, tag2, 0, vi09);
        } else {
            const std::uint32_t f0 = ClipFlags(cp[0]), f1 = ClipFlags(cp[1]), f2 = ClipFlags(cp[2]);
            if ((f0 | f1 | f2) == 0) {
                Draw(r, 3, src, out, tag1, tag2, near_flag[0], vi09);
            } else if ((f0 & f1 & f2) != 0) {
                culled = true;
            } else {
                std::uint32_t buf = 0;
                const std::int32_t cnt = ClipTriangle(r, src, cp, buf);
                if (cnt - 2 <= 0) {
                    culled = true;
                } else {
                    std::memcpy(tag1, vu.Qword(top + 21), 16);
                    std::memcpy(tag2, vu.Qword(top + 31), 16);
                    Draw(r, cnt, buf, out, tag1, tag2, 1, vi09);
                }
            }
        }
        if (culled) StoreI(vu, out, static_cast<std::int32_t>(Mfir(0x8000)), 0, 0, 0);
        Xgkick(vu, out, gif);
        if (vi09 + 3 == n) return;
    }
}


// ---- Terrain program T (.vudata 0x00402200; docs/subsystems/vu-programs.md "Terrain program T") ----

// Output (pairs 0x1B1-0x204): packet 1 (A+D CLAMP_1/TEX0_1, then ST, RGBAQ, XYZF2 per vertex) and
// packet 2 (context 2: second UV, (128, 128, 128, a/255 * fog), the same XYZF2) at TOP + 0xD2 or
// TOP + 0x7E (alternating), then XGKICK.
void TerrainOutput(Vu1State& vu, std::uint32_t top, std::uint32_t src, std::int32_t cnt, std::uint32_t& toggle,
                   std::vector<std::uint8_t>& gif) {
    std::uint32_t out;
    if (toggle == 0) {
        toggle = 1;
        out = top + 0xD2;
    } else {
        toggle = 0;
        out = top + 0x7E;
    }
    std::uint32_t o1 = out;
    std::uint32_t o2 = out + static_cast<std::uint32_t>(cnt * 3 + 4);
    const std::int32_t pass2 = I16(vu, top + 22, 0);
    std::uint32_t q15[4], q19[4], q16[4];
    std::memcpy(q15, vu.Qword(top + 15), 16);
    std::memcpy(q19, vu.Qword(top + 19), 16);
    std::memcpy(q16, vu.Qword(top + 16), 16);
    StoreRaw(vu, o1++, q15);
    StoreRaw(vu, o1++, q19);
    StoreRaw(vu, o1++, q16);
    std::uint32_t tag1[4], tag2[4];
    std::memcpy(tag1, vu.Qword(top + 17), 16);
    std::memcpy(tag2, vu.Qword(top + 18), 16);
    const std::uint32_t eop_n = Mfir(0x8000 + cnt);  // 16-bit integer, sign-extended by MFIR
    tag2[0] = eop_n;
    tag1[0] = pass2 == 0 ? eop_n : Mfir(cnt);
    const float far = vu.F(top + 30, 0), k = vu.F(top + 30, 1);
    StoreRaw(vu, o1++, tag1);
    StoreRaw(vu, o2++, tag2);
    Mat m;
    m.Load(vu, top);
    for (std::int32_t i = 0; i < cnt; ++i) {
        const std::uint32_t r = src + 4 * static_cast<std::uint32_t>(i);
        Vec4 uv = LoadF(vu, r);
        const Vec4 col = LoadF(vu, r + 1);
        const Vec4 pos = LoadF(vu, r + 2);
        Vec4 uv2 = LoadF(vu, r + 3);
        const Vec4 s = m.Mul(pos);
        const float q = vu0::Div(1.0f, s.w);
        float fog = vu0::Mul(vu0::Sub(far, s.w), k);
        float a2 = vu0::Mul(col.w, 0.003921568393707275f);
        if (fog < 0.0f) fog = 0.0f;  // maxz.w vf00z
        Vec4 xyzf{vu0::Mul(s.x, q), vu0::Mul(s.y, q), vu0::Mul(s.z, q), fog < 255.0f ? fog : 255.0f};
        uv = Mul4(uv, q);
        uv2 = Mul4(uv2, q);
        a2 = vu0::Mul(a2, xyzf.w);
        const float c128 = vu0::Add(0.0f, 128.0f);
        StoreF(vu, o1, uv);
        StoreFtoi(vu, o1 + 1, col, 0);
        StoreFtoi(vu, o1 + 2, xyzf, 4);
        o1 += 3;
        StoreF(vu, o2, uv2);
        StoreFtoi(vu, o2 + 1, {c128, c128, c128, a2}, 0);
        StoreFtoi(vu, o2 + 2, xyzf, 4);
        o2 += 3;
    }
    Xgkick(vu, out, gif);
}

// One 5-qword clipper vertex (q0-q3 of the record, clip position).
struct TVert {
    Vec4 q[5];
};

TVert LoadT(const Vu1State& vu, std::uint32_t a) {
    TVert v;
    for (std::uint32_t i = 0; i < 5; ++i) v.q[i] = LoadF(vu, a + i);
    return v;
}

void StoreT(Vu1State& vu, std::uint32_t a, const TVert& v) {
    for (std::uint32_t i = 0; i < 5; ++i) StoreF(vu, a + i, v.q[i]);
}

// Sutherland-Hodgman over +x, -x, +y, -y, +z, -z in the absolute buffers 0x20 / 0x48 (pairs
// 0x0AD-0x19F), reads and writes in the microcode's order (the buffers overlap with 9 vertices).
// Returns the vertex count (< 3: rejected) and the address of the compacted stride-4 result.
std::int32_t TerrainClip(Vu1State& vu, std::uint32_t top, std::uint32_t& result) {
    std::int32_t cnt = 3;
    std::uint32_t in = 0x20, out = 0x48;
    bool swapped = false;
    float s = 1.0f;
    for (std::uint32_t bit = 1;; bit <<= 1) {
        const int comp = (bit & 3) != 0 ? 0 : ((bit & 0xC) != 0 ? 1 : 2);
        const std::int32_t edges = cnt;
        std::uint32_t wr = out;
        TVert p = LoadT(vu, in + 5 * static_cast<std::uint32_t>(cnt - 1));
        TVert q = LoadT(vu, in);
        std::uint32_t rd = in + 5;
        for (std::int32_t e = 0; e < edges; ++e) {
            const float pc = vu0::Add(0.0f, Comp(p.q[4], comp));
            const float qc = vu0::Add(0.0f, Comp(q.q[4], comp));
            const float dp = vu0::Sub(vu0::Mul(s, pc), p.q[4].w);
            const float dq = vu0::Sub(vu0::Mul(s, qc), q.q[4].w);
            // MAC Sx clear = outside (d >= 0, the sign bit decides).
            const int code = (std::signbit(dp) ? 0 : 1) | (std::signbit(dq) ? 0 : 2);
            if (code == 0) {
                StoreT(vu, wr, p);
                wr += 5;
            } else if (code == 2 || code == 1) {
                const float a = vu0::Add(vu0::Mul(vu0::Mul(s, p.q[4].w), -1.0f), pc);
                const float b = vu0::Add(vu0::Mul(vu0::Mul(s, q.q[4].w), -1.0f), qc);
                TVert x;
                if (code == 2) {
                    StoreT(vu, wr, p);
                    wr += 5;
                    const float t = std::fabs(vu0::Mul(a, vu0::Div(1.0f, vu0::Sub(b, a))));
                    for (int i = 0; i < 4; ++i) x.q[i] = Add4(Mul4(Sub4(q.q[i], p.q[i]), t), p.q[i]);
                    ++cnt;
                } else {
                    const float t = std::fabs(vu0::Mul(b, vu0::Div(1.0f, vu0::Sub(a, b))));
                    for (int i = 0; i < 4; ++i) x.q[i] = Add4(Mul4(Sub4(p.q[i], q.q[i]), t), q.q[i]);
                }
                x.q[4] = MulM(vu, top + 4, x.q[2]);
                StoreT(vu, wr, x);
                wr += 5;
            } else {
                --cnt;
            }
            p = q;
            q = LoadT(vu, rd);
            rd += 5;
        }
        if (!swapped) {
            swapped = true;
            in = 0x48;
            out = 0x20;
        } else {
            swapped = false;
            in = 0x20;
            out = 0x48;
        }
        s = vu0::Mul(s, -1.0f);
        if (cnt - 2 <= 0) return cnt;
        if (bit == 0x20) break;
    }
    // Compaction to stride 4 into the other buffer (four reads, then four writes per vertex).
    std::uint32_t rd = in, wr = out;
    for (std::int32_t i = 0; i < cnt; ++i) {
        std::uint32_t tmp[4][4];
        for (std::uint32_t j = 0; j < 4; ++j) std::memcpy(tmp[j], vu.Qword(rd + j), 16);
        rd += 5;
        for (std::uint32_t j = 0; j < 4; ++j) StoreRaw(vu, wr + j, tmp[j]);
        wr += 4;
    }
    result = out;
    return cnt;
}

// One terrain cell (MSCNT from pair 2 to the E bit at 0x208).
void RunTerrainCell(Vu1State& vu, std::vector<std::uint8_t>& gif) {
    const std::uint32_t top = vu.top;
    const std::int32_t n = I16(vu, top + 23, 0);
    std::uint32_t toggle = 0;  // vi07
    // 1. Unpack the n records in place (the loop body runs at least once).
    {
        Vec4 off = Itof(vu, top + 20, 0);
        off.x = vu0::Mul(off.x, 0.015625f);
        off.y = vu0::Mul(off.y, 0.015625f);
        const Vec4 org = LoadF(vu, top + 12);
        const std::int32_t iters = n - 1 > 0 ? n : 1;
        for (std::int32_t k = 0; k < iters; ++k) {
            const std::uint32_t r = top + 0x20 + 4 * static_cast<std::uint32_t>(k);
            Vec4 q0 = Itof(vu, r, 0), q1 = Itof(vu, r + 1, 0), q2 = Itof(vu, r + 2, 0), q3 = Itof(vu, r + 3, 0);
            q0.x = vu0::Mul(q0.x, 0.0625f);
            q0.y = vu0::Mul(q0.y, 0.0625f);
            q2.x = vu0::Mul(q2.x, 1024.0f);
            q2.z = vu0::Mul(q2.z, 1024.0f);
            q2.y = vu0::Mul(q2.y, -80.0f);
            q2.x = vu0::Add(q2.x, org.x);
            q2.y = vu0::Add(q2.y, org.y);
            q2.z = vu0::Add(q2.z, org.z);
            q3.x = vu0::Add(vu0::Mul(q3.x, 0.0078125f), off.x);
            q3.y = vu0::Add(vu0::Mul(q3.y, 0.0078125f), off.y);
            q3.z = vu0::Add(0.0f, 1.0f);
            StoreF(vu, r, q0);
            StoreF(vu, r + 1, q1);
            StoreF(vu, r + 2, q2);
            StoreF(vu, r + 3, q3);
        }
    }
    const std::int32_t mode = I16(vu, top + 23, 1);
    const std::uint32_t recs = top + 0x20;
    if (mode == 4) {
        TerrainOutput(vu, top, recs, n, toggle, gif);
        return;
    }
    if (mode != 1) {
        // 3. Morph the centre and the edge midpoints toward the coarse quad by distance.
        const Vec4 p0 = LoadF(vu, recs + 2), p1 = LoadF(vu, recs + 6), p2 = LoadF(vu, recs + 10);
        const Vec4 p3 = LoadF(vu, recs + 14), p4 = LoadF(vu, recs + 18), p5 = LoadF(vu, recs + 22);
        const Vec4 p6 = LoadF(vu, recs + 26), p7 = LoadF(vu, recs + 30), p8 = LoadF(vu, recs + 34);
        auto half = [](const Vec4& a, const Vec4& b) { return Mul4(Add4(a, b), 0.5f); };
        const Vec4 mid[5] = {half(p1, p5), half(p1, p3), half(p3, p5), half(p5, p7), half(p7, p1)};
        const Vec4 v[5] = {p0, p2, p4, p6, p8};
        const Vec4 cam = LoadF(vu, top + 14), rr = LoadF(vu, top + 13);
        static const std::uint32_t kAt[5] = {2, 10, 18, 26, 34};
        Vec4 res[5];
        for (int j = 0; j < 5; ++j) {
            const Vec4 d = Sub4(v[j], cam);
            float t = vu0::Add(vu0::Mul(d.x, d.x), vu0::Mul(d.y, d.y));
            t = vu0::Add(t, vu0::Mul(d.z, d.z));
            t = vu0::Mul(vu0::Sub(t, rr.y), rr.x);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            res[j] = Add4(Mul4(Sub4(mid[j], v[j]), t), v[j]);
        }
        for (int j = 0; j < 5; ++j) StoreF(vu, recs + kAt[j], res[j]);
        TerrainOutput(vu, top, recs, n, toggle, gif);
        return;
    }
    // 4. Clip each fan triangle (record 0, t + 1, t + 2).
    for (std::int32_t left = n - 3;; --left) {
        const std::int32_t idx = (n - 2) - left;
        const std::uint32_t b = recs + 4 * static_cast<std::uint32_t>(idx);
        TVert va, vb, vc;
        for (std::uint32_t i = 0; i < 4; ++i) {
            va.q[i] = LoadF(vu, recs + i);
            vb.q[i] = LoadF(vu, b + i);
            vc.q[i] = LoadF(vu, b + 4 + i);
        }
        Mat mc;
        mc.Load(vu, top + 4);
        va.q[4] = mc.Mul(va.q[2]);
        vb.q[4] = mc.Mul(vb.q[2]);
        vc.q[4] = mc.Mul(vc.q[2]);
        const std::uint32_t fa = ClipFlags(va.q[4]), fb = ClipFlags(vb.q[4]), fc = ClipFlags(vc.q[4]);
        if ((fa | fb | fc) == 0) {
            for (std::uint32_t i = 0; i < 4; ++i) {
                StoreF(vu, i, va.q[i]);
                StoreF(vu, 4 + i, vb.q[i]);
                StoreF(vu, 8 + i, vc.q[i]);
            }
            TerrainOutput(vu, top, 0, 3, toggle, gif);
        } else if ((fa & fb & fc) == 0) {
            StoreT(vu, 0x20, va);
            StoreT(vu, 0x25, vb);
            StoreT(vu, 0x2A, vc);
            std::uint32_t result = 0;
            const std::int32_t cnt = TerrainClip(vu, top, result);
            if (cnt - 2 > 0) TerrainOutput(vu, top, result, cnt, toggle, gif);
        }
        if (left <= 0) break;
    }
}

}  // namespace

void Xgkick(const Vu1State& vu, std::uint32_t addr, std::vector<std::uint8_t>& gif) {
    for (int guard = 0; guard < 64; ++guard) {
        const std::uint32_t* t = const_cast<Vu1State&>(vu).Qword(addr);
        const std::uint64_t lo = static_cast<std::uint64_t>(t[0]) | static_cast<std::uint64_t>(t[1]) << 32;
        const std::uint32_t nloop = static_cast<std::uint32_t>(lo & 0x7FFF);
        const bool eop = ((lo >> 15) & 1) != 0;
        const std::uint32_t flg = static_cast<std::uint32_t>((lo >> 58) & 3);
        std::uint32_t nreg = static_cast<std::uint32_t>((lo >> 60) & 0xF);
        if (nreg == 0) nreg = 16;
        std::uint32_t qwords = 0;
        if (flg == 0) qwords = nloop * nreg;
        else if (flg == 1) qwords = (nloop * nreg + 1) / 2;
        else qwords = nloop;
        // The tag and its data, in pieces that do not cross the end of VU memory (addresses wrap).
        for (std::uint32_t i = 0, n = qwords + 1; n > 0;) {
            const std::uint32_t a = (addr + i) & (kVuMemQwords - 1);
            const std::uint32_t run = std::min(n, kVuMemQwords - a);
            const std::uint8_t* q = reinterpret_cast<const std::uint8_t*>(&vu.mem[a * 4]);
            gif.insert(gif.end(), q, q + run * 16);
            i += run;
            n -= run;
        }
        addr += qwords + 1;
        if (eop) return;
    }
}

// ---- Glow fin program D (0x0029A930; docs/subsystems/vu-programs.md "Glow fin program D") ----

namespace {

// One strip (pairs 0x002-0x0DA): pass 1 transforms every vertex (q2 := node x q2, q3 := q2 + offset
// with w 1, q0 / q1 := both projected and divided by their w); pass 2 walks the strip's triangles
// and emits the silhouette quads (edges whose facing differs from mode 49.x) and, in mode 0, the cap.
void RunGlowFins(Vu1State& vu, std::vector<std::uint8_t>& gif) {
    const std::uint32_t top = vu.top;
    const std::uint16_t n = static_cast<std::uint16_t>(vu.I(top + 23, 0));
    Mat proj, node;
    proj.Load(vu, 32);
    node.Load(vu, top);
    const Vec4 offset = LoadF(vu, 45);
    // Pass 1 (0x011-0x034).
    {
        Vec4 ext{0.0f, 0.0f, 0.0f, 1.0f};  // vf19 = vf00 x vf00: only xyz are rewritten below
        std::uint32_t v = top + 0x20;
        Vec4 pos = LoadF(vu, v + 2);
        std::uint16_t i = 0;
        do {
            const Vec4 p = node.Mul(pos);
            pos = LoadF(vu, v + 6);  // the next vertex's q2, read before this vertex is stored
            ++i;
            ext.x = vu0::Add(p.x, offset.x);
            ext.y = vu0::Add(p.y, offset.y);
            ext.z = vu0::Add(p.z, offset.z);
            StoreF(vu, v + 2, p);
            Vec4 s0 = proj.Mul(p);
            StoreF(vu, v + 3, ext);
            Vec4 s1 = proj.Mul(ext);
            s0 = Mul4(s0, vu0::Div(1.0f, s0.w));
            const float q1 = vu0::Div(1.0f, s1.w);
            StoreF(vu, v, s0);
            s1 = Mul4(s1, q1);
            StoreF(vu, v + 1, s1);
            v += 4;
        } while (i != n);
    }
    // Pass 2 (0x035-0x0D6).
    std::uint32_t t46[4], t47[4], t48[4];
    std::memcpy(t46, vu.Qword(46), 16);
    std::memcpy(t47, vu.Qword(47), 16);
    std::memcpy(t48, vu.Qword(48), 16);
    const Vec4 light = LoadF(vu, 44);
    const std::uint16_t mode = static_cast<std::uint16_t>(vu.I(49, 0));
    std::uint16_t winding = static_cast<std::uint16_t>(vu.I(top + 23, 2));
    const std::uint32_t colour[4] = {1, 1, 1, 128};  // ftoi0 of (1, 1, 1, 128)
    struct V {
        Vec4 q[4];
    };
    const auto load = [&](std::uint32_t a) {
        V r;
        for (std::uint32_t k = 0; k < 4; ++k) r.q[k] = LoadF(vu, a + k);
        return r;
    };
    // add.xyzw with vf17 = 0: -0 becomes +0.
    const auto plus0 = [](const V& x) {
        V r;
        for (int k = 0; k < 4; ++k) r.q[k] = Add4(x.q[k], Vec4{0.0f, 0.0f, 0.0f, 0.0f});
        return r;
    };
    std::uint32_t src = top + 0x20;
    std::uint32_t out = top + 0x7E;
    V va = load(src), vb = load(src + 4);
    src += 8;
    std::uint16_t i = 2;
    do {
        ++i;
        const V a = plus0(va), b = plus0(vb);
        const V c = load(src);
        src += 4;
        float s;
        if (winding == 0) {
            winding = 1;
            s = 1.0f;
        } else {
            winding = 0;
            s = -1.0f;
        }
        // Face normal from the world points (opmula / opmsub), normalised, signed, dotted with the light.
        const Vec4 e1 = Sub4(a.q[2], b.q[2]), e2 = Sub4(c.q[2], b.q[2]);
        Vec4 nrm{vu0::Sub(vu0::Mul(e1.y, e2.z), vu0::Mul(e2.y, e1.z)), vu0::Sub(vu0::Mul(e1.z, e2.x), vu0::Mul(e2.z, e1.x)),
                 vu0::Sub(vu0::Mul(e1.x, e2.y), vu0::Mul(e2.x, e1.y)), 0.0f};
        const float len2 = vu0::Add(vu0::Add(vu0::Mul(nrm.x, nrm.x), vu0::Mul(nrm.y, nrm.y)), vu0::Mul(nrm.z, nrm.z));
        const float inv = vu0::Div(1.0f, vu0::Mul(1.0f, vu0::Sqrt(len2)));
        nrm.x = vu0::Mul(vu0::Mul(vu0::Mul(nrm.x, s), inv), light.x);
        nrm.y = vu0::Mul(vu0::Mul(vu0::Mul(nrm.y, s), inv), light.y);
        nrm.z = vu0::Mul(vu0::Mul(vu0::Mul(nrm.z, s), inv), light.z);
        const float d = vu0::Add(0.0f, vu0::Add(vu0::Add(nrm.x, nrm.y), nrm.z));
        // Edge facing: z of (p - p') x (q - p') for the projected points, x s x d; the flag is the
        // MAC test "fmeq 0x20" (sign set, zero, underflow and overflow clear).
        const auto facing = [&](const Vec4& p, const Vec4& pe, const Vec4& qp) {
            const Vec4 u = Sub4(p, pe), w = Sub4(qp, pe);
            float z = vu0::Sub(vu0::Mul(u.x, w.y), vu0::Mul(w.x, u.y));
            z = vu0::Mul(vu0::Mul(z, s), d);
            return static_cast<std::uint16_t>(z < 0.0f && z > -FLT_MAX ? 1 : 0);
        };
        const std::uint16_t f1 = facing(a.q[0], a.q[1], b.q[0]);
        const std::uint16_t f2 = facing(b.q[0], b.q[1], c.q[0]);
        const std::uint16_t f3 = facing(c.q[0], c.q[1], a.q[0]);
        const auto put = [&](const Vec4& p) { StoreFtoi(vu, out++, p, 4); };
        const auto quad = [&](const Vec4& p, const Vec4& pe, const Vec4& q, const Vec4& qe) {
            StoreRaw(vu, out++, t46);
            StoreRaw(vu, out++, colour);
            put(p);
            put(pe);
            put(q);
            put(qe);
        };
        if (f1 != mode) quad(a.q[0], a.q[1], b.q[0], b.q[1]);
        if (f2 != mode) quad(b.q[0], b.q[1], c.q[0], c.q[1]);
        if (f3 != mode) quad(c.q[0], c.q[1], a.q[0], a.q[1]);
        if (mode == 0) {
            StoreRaw(vu, out++, t47);
            StoreRaw(vu, out++, colour);
            put(a.q[1]);
            put(b.q[1]);
            put(c.q[1]);
        }
        va = b;
        vb = c;
    } while (i != n);
    StoreRaw(vu, out, t48);
    Xgkick(vu, top + 0x7E, gif);
}

}  // namespace

bool RunVu1Program(Vu1State& vu, std::uint32_t source, std::uint32_t pc, std::vector<std::uint8_t>& gif,
                   std::uint32_t& next_pc) {
    if (source == kProgramD) {
        // Pair 0 holds an E bit; a strip runs from pair 2 (or from 0xDE, which branches to 2) to the
        // E bit at 0xDC. Entry 0xE0 is not reached by the game's packets (vu-programs.md).
        if (pc == 0) {
            next_pc = 2;
            return true;
        }
        if (pc != 2 && pc != 0xDE) return false;
        const vu0::ChopScope chop;
        RunGlowFins(vu, gif);
        next_pc = 0xDE;
        return true;
    }
    if (source == kProgramT) {
        // Pair 0 holds an E bit; every cell's MSCNT runs from pair 2 to the E bit at 0x208.
        if (pc != 0) {
            const vu0::ChopScope chop;
            RunTerrainCell(vu, gif);
        }
        next_pc = 2;
        return true;
    }
    if (source != kProgramB && source != kProgramC && source != kProgramA && source != kProgramE) return false;
    // Pair 0 holds an E bit: an MSCAL at 0 ends at once; every MSCNT runs one strip from pair 2.
    if (pc == 0) {
        next_pc = 2;
        return true;
    }
    const vu0::ChopScope chop;
    RunModelStrip(vu, source == kProgramB ? 0 : source == kProgramC ? 1 : source == kProgramA ? 2 : 3, gif);
    next_pc = 2;
    return true;
}

}  // namespace ac04::render::vu1
