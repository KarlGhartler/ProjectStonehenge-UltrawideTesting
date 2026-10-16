// Model library (render module; docs/formats/SM-model.md, docs/port/s4-inventory.md §3): binding of
// SM model data, model instances (hierarchy, part poses, morph meshes) and the VIF packets that
// feed the VU1 model programs (EmitModelInstance). Also the VU0 matrix helpers these functions use.
//
// Bind record (0x28 bytes): +0 model, +4 part channel count, +8 node count, +0xC part index count,
// +0x10 node area, +0x14 root node, +0x18 current mesh header (scratch), +0x1C model, +0x20 node
// records (0x50 bytes each), +0x24 part index table (s16 per part).
// Node record (0x50): +0 node, +4 keyframe table, +8 mesh header, +0xC morph table, +0x14 morph
// targets base, +0x18 flags (1 translucent, 2 keyframes, 4 morph), +0x1C mesh strip count, +0x20
// key positions, +0x24 key values, +0x28 VIF packet, +0x2C mesh DMA list, +0x30 morph table, +0x34
// morph key table, +0x38 morph target pointers, +0x3C morph target count, +0x40 child count, +0x44
// child node pointers.
// Instance (0x4C): +0 node flags (1/2 hidden, 4 pose dirty, 8 no geometry), +4 part values, +8 node
// channel, +0xC node part index (byte), +0x10 node matrices, +0x14 node rotation matrices, +0x18
// node origins, +0x1C node axes, +0x20 visited bytes, +0x24/+0x28 morph buffers (two, by visited),
// +0x2C current mesh lists, +0x30 bind record, +0x34 model + 0x20, +0x38 model +0xC, +0x3C version,
// +0x40 bounding radius, +0x44 model +0x18, +0x48 triangle count.
#include "game/game.h"

#include <cmath>
#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "common/guest_ref.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

vu0::Vec4 LoadV(u32 a) { return {GRef<f32>(a), GRef<f32>(a + 4), GRef<f32>(a + 8), GRef<f32>(a + 12)}; }

void StoreV(u32 a, const vu0::Vec4& v) {
    GRef<f32>(a) = v.x;
    GRef<f32>(a + 4) = v.y;
    GRef<f32>(a + 8) = v.z;
    GRef<f32>(a + 12) = v.w;
}

// The four columns of matrix m times v (vmulax/vmadday/vmaddaz/vmaddw).
vu0::Vec4 MulCols(const vu0::Vec4 m[4], const vu0::Vec4& v) { return vu0::MulMatrix(m[0], m[1], m[2], m[3], v); }

// m = m * R, R given by its four columns (each column multiplied through m, in order).
void ApplyRotation(vu0::Vec4 m[4], const vu0::Vec4 r[4]) {
    vu0::Vec4 out[4];
    for (int i = 0; i < 4; ++i) out[i] = MulCols(m, r[i]);
    for (int i = 0; i < 4; ++i) m[i] = out[i];
}

// VU0 polynomials: cos = 1 + sum C_k a^(2k+2), sin = a + sum S_k a^(2k+3), k = 0..7 (coefficients
// at 0x002A1600 and 0x002A15E0), per component x, y, z, in the code's order.
void PolySinCos(const vu0::Vec4& a, vu0::Vec4& s, vu0::Vec4& c) {
    const f32 av[3] = {a.x, a.y, a.z};
    f32 sv[3], cv[3];
    for (int i = 0; i < 3; ++i) {
        f32 cs = vu0::Add(0.0f, 1.0f);
        f32 sn = vu0::Add(0.0f, av[i]);
        const f32 a2 = vu0::Mul(av[i], av[i]);
        f32 p = vu0::Add(0.0f, a2);
        f32 p3 = vu0::Mul(a2, av[i]);
        for (u32 k = 0; k < 8; ++k) {
            if (k != 0) {
                p = vu0::Mul(p, a2);
                p3 = vu0::Mul(p3, a2);
            }
            const f32 ct = vu0::Mul(p, GRef<f32>(0x002A1600 + 4 * k));
            const f32 st = vu0::Mul(p3, GRef<f32>(0x002A15E0 + 4 * k));
            cs = vu0::Add(cs, ct);
            sn = vu0::Add(sn, st);
        }
        sv[i] = sn;
        cv[i] = cs;
    }
    s = {sv[0], sv[1], sv[2], 0.0f};
    c = {cv[0], cv[1], cv[2], 0.0f};
}

// Rotation columns (as the code builds them from 0/1 with vaddw/vsubw and the sin/cos products).
void RotZ(vu0::Vec4 r[4], f32 s, f32 c) {
    r[0] = {vu0::Mul(1.0f, c), vu0::Mul(1.0f, s), 0.0f, 0.0f};
    r[1] = {vu0::Mul(-1.0f, s), vu0::Mul(1.0f, c), 0.0f, 0.0f};
    r[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    r[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}
void RotY(vu0::Vec4 r[4], f32 s, f32 c) {
    r[0] = {vu0::Mul(1.0f, c), 0.0f, vu0::Mul(-1.0f, s), 0.0f};
    r[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    r[2] = {vu0::Mul(1.0f, s), 0.0f, vu0::Mul(1.0f, c), 0.0f};
    r[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}
void RotX(vu0::Vec4 r[4], f32 s, f32 c) {
    r[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    r[1] = {0.0f, vu0::Mul(1.0f, c), vu0::Mul(1.0f, s), 0.0f};
    r[2] = {0.0f, vu0::Mul(-1.0f, s), vu0::Mul(1.0f, c), 0.0f};
    r[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}

}  // namespace

// EE: 0x00104F50
// Distance of a and b in the x/z plane: sqrt((ax - bx)^2 + (az - bz)^2) (mula/madd, sqrt.s).
f32 GetDistanceXZ(u32 a, u32 b) {
    const f32 dz = fpu::Sub(GRef<f32>(a + 8), GRef<f32>(b + 8));
    const f32 dx = fpu::Sub(GRef<f32>(a), GRef<f32>(b));
    return fpu::Sqrt(fpu::MulAdd(fpu::Mul(dx, dx), dz, dz));
}

// EE: 0x00105178
// Scale matrix (MMI: mfc1 sign-extends each float's bits to 64 bits; pextlw/pcpyld put the
// sign-extension word into a slot of the column). Columns: (sx, 0,
// ext(sx), 0), (0, sy, 0, ext(sy)), (0, 0, sz, ext(sz)), (0, 0, 0, 1), where ext is 0 or
// 0xFFFFFFFF by the float's sign.
void BuildScaleMatrix(f32 sx, f32 sy, f32 sz, u32 m) {
    auto bits = [](f32 f) {
        u32 b;
        std::memcpy(&b, &f, 4);
        return b;
    };
    const u32 bx = bits(sx), by = bits(sy), bz = bits(sz);
    const u32 ex = static_cast<u32>(static_cast<s32>(bx) >> 31), ey = static_cast<u32>(static_cast<s32>(by) >> 31);
    const u32 ez = static_cast<u32>(static_cast<s32>(bz) >> 31);
    GRef<u32>(m) = bx;
    GRef<u32>(m + 4) = 0;
    GRef<u32>(m + 8) = ex;
    GRef<u32>(m + 12) = 0;
    GRef<u32>(m + 0x10) = 0;
    GRef<u32>(m + 0x14) = by;
    GRef<u32>(m + 0x18) = 0;
    GRef<u32>(m + 0x1C) = ey;
    GRef<u32>(m + 0x20) = 0;
    GRef<u32>(m + 0x24) = 0;
    GRef<u32>(m + 0x28) = bz;
    GRef<u32>(m + 0x2C) = ez;
    StoreV(m + 0x30, {0.0f, 0.0f, 0.0f, 1.0f});
}

// EE: 0x001051A8
// (VU0) t^3 + t (1 - t)^2 a + t^2 (1 - t) b.
f32 EvalHermite(f32 t, f32 a, f32 b) {
    const f32 u = vu0::Sub(1.0f, t);
    const f32 t2 = vu0::Mul(t, t);
    const f32 u2 = vu0::Mul(u, u);
    const f32 t3 = vu0::Mul(t2, t);
    f32 u2t = vu0::Mul(u2, t);
    f32 t2u = vu0::Mul(t2, u);
    u2t = vu0::Mul(u2t, a);
    t2u = vu0::Mul(t2u, b);
    f32 r = vu0::Add(t3, u2t);
    return vu0::Add(r, t2u);
}

// EE: 0x001051F0
// v = min(max(v, lo), hi) per component (VU0).
void ClampVector(u32 v, u32 lo, u32 hi) {
    const vu0::Vec4 a = LoadV(v), l = LoadV(lo), h = LoadV(hi);
    StoreV(v, {std::fmin(std::fmax(a.x, l.x), h.x), std::fmin(std::fmax(a.y, l.y), h.y),
               std::fmin(std::fmax(a.z, l.z), h.z), std::fmin(std::fmax(a.w, l.w), h.w)});
}

// EE: 0x001089B0
// Node matrices from rotation `rot` (radians x, y, z) and translation `trans`: rotation = I * Rz *
// Ry * Rx (VU0 polynomial sin/cos); `full` = the same with the translation added to column 3.
void BuildRotationMatrix(u32 full, u32 rotation, u32 rot, u32 trans) {
    vu0::Vec4 s, c;
    PolySinCos(LoadV(rot), s, c);
    vu0::Vec4 m[4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    vu0::Vec4 r[4];
    RotZ(r, s.z, c.z);
    ApplyRotation(m, r);
    RotY(r, s.y, c.y);
    ApplyRotation(m, r);
    RotX(r, s.x, c.x);
    ApplyRotation(m, r);
    for (u32 i = 0; i < 4; ++i) StoreV(rotation + 16 * i, m[i]);
    const vu0::Vec4 t = LoadV(trans);
    m[3].x = vu0::Add(m[3].x, t.x);
    m[3].y = vu0::Add(m[3].y, t.y);
    m[3].z = vu0::Add(m[3].z, t.z);
    for (u32 i = 0; i < 4; ++i) StoreV(full + 16 * i, m[i]);
}

// EE: 0x00108C68
// out = m * Ry * Rx * Rz of the angles at `rot` (VU0 polynomial sin/cos).
void RotateMatrix(u32 out, u32 m_in, u32 rot) {
    vu0::Vec4 s, c;
    PolySinCos(LoadV(rot), s, c);
    vu0::Vec4 m[4] = {LoadV(m_in), LoadV(m_in + 0x10), LoadV(m_in + 0x20), LoadV(m_in + 0x30)};
    vu0::Vec4 r[4];
    RotY(r, s.y, c.y);
    ApplyRotation(m, r);
    RotX(r, s.x, c.x);
    ApplyRotation(m, r);
    RotZ(r, s.z, c.z);
    ApplyRotation(m, r);
    for (u32 i = 0; i < 4; ++i) StoreV(out + 16 * i, m[i]);
}

// EE: 0x00108F00
// out = a * b (VU0; column j = a * b_j). `out` may alias either input.
void MulMatrixVu0(u32 out, u32 a, u32 b) {
    const vu0::Vec4 m[4] = {LoadV(a), LoadV(a + 0x10), LoadV(a + 0x20), LoadV(a + 0x30)};
    const vu0::Vec4 bc[4] = {LoadV(b), LoadV(b + 0x10), LoadV(b + 0x20), LoadV(b + 0x30)};
    for (u32 j = 0; j < 4; ++j) StoreV(out + 16 * j, MulCols(m, bc[j]));
}

// EE: 0x001096A8
void SetUnitMatrixVu0(u32 m) {
    StoreV(m + 0x20, {0.0f, 0.0f, 1.0f, 0.0f});
    StoreV(m + 0x30, {0.0f, 0.0f, 0.0f, 1.0f});
    StoreV(m, {1.0f, 0.0f, 0.0f, 0.0f});
    StoreV(m + 0x10, {0.0f, 1.0f, 0.0f, 0.0f});
}

// EE: 0x00144238
// Counts the nodes under `node` (preorder) and the largest part channel and part index.
void CountModelNodes(u32 rec, u32 node, u32 count, u32 max_channel, u32 max_part) {
    GRef<s32>(count) += 1;
    if (GRef<s32>(max_channel) < GRef<s32>(node + 0x2C)) GRef<s32>(max_channel) = GRef<s32>(node + 0x2C);
    const s32 part = GRef<s32>(node + 0x3C);
    if (GRef<s32>(max_part) < part && part != 0xFF) GRef<s32>(max_part) = part;
    const u32 base = GRef<u32>(rec);
    u32 list = GRef<s32>(GRef<u32>(rec + 0x1C) + 4) < 0xB4 ? node + 0x34 : GRef<u32>(node + 0x34) + base;
    for (s32 i = 0; i < GRef<s32>(node + 0x30); ++i, list += 4) CountModelNodes(rec, GRef<u32>(list) + base, count, max_channel, max_part);
}

// EE: 0x00144340
// Fills the node record of `node` (index *index) and those of its children (preorder): part index
// map, keyframes, mesh packets and their strip count, morph table.
void BindModelNodes(u32 rec, u32 node, u32 index) {
    const u32 r = GRef<u32>(rec + 0x20) + static_cast<u32>(GRef<s32>(index)) * 0x50;
    GRef<u32>(r + 0x18) = 0;
    GRef<u32>(r) = node;
    if (GRef<u32>(rec + 0xC) != 0 && GRef<s32>(node + 0x3C) != 0xFF)
        GRef<s16>(GRef<u32>(rec + 0x24) + static_cast<u32>(GRef<s32>(node + 0x3C)) * 2) = static_cast<s16>(GRef<s32>(index));
    const u32 base = GRef<u32>(rec);
    GRef<u32>(rec + 0x18) = GRef<u32>(node + 0x24) + base;
    GRef<u32>(r + 0x24) = GRef<u32>(node + 0x20) == 0 ? 0 : GRef<u32>(node + 0x20) + base;
    const u32 list = GRef<s32>(GRef<u32>(rec + 0x1C) + 4) > 0xB3 ? GRef<u32>(node + 0x34) + base : node + 0x34;
    const s32 children = GRef<s32>(node + 0x30);
    GRef<s32>(r + 0x40) = children;
    GRef<u32>(r + 0x44) = children == 0 ? 0 : WorkHeapAlloc(children << 2, 4);
    for (s32 i = 0; i < GRef<s32>(node + 0x30); ++i)
        GRef<u32>(GRef<u32>(r + 0x44) + 4 * static_cast<u32>(i)) = GRef<u32>(list + 4 * static_cast<u32>(i)) + base;
    const u32 keys = GRef<u32>(r + 0x24);
    if (keys == 0) {
        GRef<u32>(r + 4) = 0;
    } else {
        GRef<u32>(r + 4) = keys;
        GRef<u32>(r + 0x18) |= 2;
        GRef<u32>(r + 0x24) = GRef<u32>(keys + 8) + base;
        GRef<u32>(r + 0x20) = GRef<u32>(keys + 4) + base;
    }
    const u32 mh = GRef<u32>(rec + 0x18);
    GRef<u32>(r + 0x28) = GRef<u32>(mh) + base;
    GRef<u32>(r + 0x2C) = GRef<u32>(mh + 4) + base;
    GRef<u32>(r + 8) = mh;
    if ((GRef<u32>(mh + 0xC) & 1) != 0) GRef<u32>(r + 0x18) |= 1;
    s32 tags = 1;
    for (u32 p = GRef<u32>(r + 0x2C); GRef<u32>(p) != 0x60000000u; p += 16) ++tags;
    GRef<s32>(r + 0x1C) = tags;
    const u32 morph = GRef<u32>(mh + 8);
    if (morph == 0) {
        GRef<u32>(r + 0x3C) = 0;
        GRef<u32>(r + 0xC) = 0;
        GRef<u32>(r + 0x38) = 0;
    } else {
        GRef<u32>(r + 0x18) |= 4;
        const u32 m = morph + base;
        GRef<u32>(r + 0x30) = m;
        GRef<u32>(r + 0xC) = m;
        GRef<u32>(r + 0x14) = m + 0x10;
        GRef<u32>(r + 0x34) = GRef<u32>(m + 4) + base;
        const s32 n = GRef<s32>(m + 0xC);
        GRef<s32>(r + 0x3C) = n;
        const u32 targets = WorkHeapAlloc(n << 2, 4);
        GRef<u32>(r + 0x38) = targets;
        for (s32 i = 0; i < GRef<s32>(m + 0xC); ++i)
            GRef<u32>(targets + 4 * static_cast<u32>(i)) = GRef<u32>(m + 0x10 + 4 * static_cast<u32>(i)) + GRef<u32>(rec);
    }
    GRef<s32>(index) += 1;
    for (s32 i = 0; i < GRef<s32>(node + 0x30); ++i) BindModelNodes(rec, GRef<u32>(GRef<u32>(r + 0x44) + 4 * static_cast<u32>(i)), index);
}

// EE: 0x00144618
// Binds model data `mdl` to the record `rec` (0x28): counts, part index table, node records.
void BindModelData(u32 rec, u32 mdl) {
    GRef<u32>(rec) = mdl;
    guest::GuestFrame frame(0x10);
    const u32 counts = frame.Address();
    GRef<s32>(counts + 8) = -1;
    GRef<u32>(rec + 0x1C) = mdl;
    GRef<s32>(counts) = 0;
    GRef<u32>(rec + 0x10) = GRef<u32>(mdl + 0x10) + mdl;
    GRef<s32>(counts + 4) = 0;
    const u32 root = GRef<u32>(mdl + 0x14) + mdl;
    GRef<u32>(rec + 0x14) = root;
    CountModelNodes(rec, root, counts, counts + 4, counts + 8);
    GRef<s32>(rec + 8) = GRef<s32>(counts);
    GRef<s32>(rec + 4) = GRef<s32>(counts + 4) + 1;
    GRef<s32>(rec + 0xC) = GRef<s32>(GRef<u32>(rec + 0x1C) + 4) < 0xBF ? 0 : GRef<s32>(counts + 8) + 1;
    GRef<u32>(rec + 0x24) = WorkHeapAlloc(GRef<s32>(rec + 0xC) << 1, 0x10);
    GRef<u32>(rec + 0x20) = WorkHeapAlloc(GRef<s32>(rec + 8) * 0x50, 0x10);
    GRef<s32>(counts) = 0;
    BindModelNodes(rec, GRef<u32>(rec + 0x14), counts);
}

// EE: 0x0013F018
// Creates instance `inst` (0x4C) of bound model `rec`: per-node arrays, morph buffers, part values
// 0.5, the first hierarchy pass, part indices and the triangle count of the mesh lists.
void CreateModelInstance(u32 inst, u32 rec) {
    GRef<u32>(inst + 0x30) = rec;
    const u32 mdl = GRef<u32>(rec + 0x1C);
    GRef<s32>(inst + 0x3C) = GRef<s32>(mdl + 4);
    GRef<u32>(inst + 0x38) = GRef<u32>(mdl + 0xC);
    auto nodes = [&]() { return GRef<s32>(GRef<u32>(inst + 0x30) + 8); };
    GRef<u32>(inst + 0x10) = WorkHeapAlloc(GRef<s32>(rec + 8) << 6, 0x10);
    GRef<u32>(inst + 0x14) = WorkHeapAlloc(nodes() << 6, 0x10);
    GRef<u32>(inst + 0x18) = WorkHeapAlloc(nodes() << 4, 0x10);
    GRef<u32>(inst + 0x1C) = WorkHeapAlloc(nodes() << 4, 0x10);
    GRef<u32>(inst) = WorkHeapAlloc(nodes() << 2, 0x10);
    GRef<u32>(inst + 0x20) = WorkHeapAlloc(nodes(), 1);
    GRef<u32>(inst + 0xC) = WorkHeapAlloc(nodes(), 1);
    if (GRef<s32>(rec + 4) < 2) {
        GRef<u32>(inst + 4) = 0;
    } else {
        GRef<u32>(inst + 4) = WorkHeapAlloc(GRef<s32>(GRef<u32>(inst + 0x30) + 4) << 2, 0x10);
    }
    GRef<u32>(inst + 8) = WorkHeapAlloc(nodes() << 2, 0x10);
    GRef<u32>(inst + 0x24) = WorkHeapAlloc(nodes() << 2, 0x10);
    GRef<u32>(inst + 0x28) = WorkHeapAlloc(nodes() << 2, 0x10);
    GRef<u32>(inst + 0x2C) = WorkHeapAlloc(nodes() << 2, 0x10);
    for (s32 i = 0; i < nodes(); ++i) {
        const u32 o = static_cast<u32>(i) * 4;
        sceVu0UnitMatrix(GRef<u32>(inst + 0x10) + static_cast<u32>(i) * 0x40);
        sceVu0UnitMatrix(GRef<u32>(inst + 0x14) + static_cast<u32>(i) * 0x40);
        GRef<u8>(GRef<u32>(inst + 0x20) + static_cast<u32>(i)) = 0;
        GRef<u32>(GRef<u32>(inst) + o) = 0;
        const u32 r = GRef<u32>(rec + 0x20) + static_cast<u32>(i) * 0x50;
        if ((GRef<u32>(r + 0x18) & 4) == 0) {
            GRef<u32>(GRef<u32>(inst + 0x24) + o) = GRef<u32>(r + 0x2C);
            GRef<u32>(GRef<u32>(inst + 0x28) + o) = GRef<u32>(r + 0x2C);
        } else {
            const u32 a = WorkHeapAlloc(GRef<s32>(r + 0x1C) << 6, 0x10);
            GRef<u32>(GRef<u32>(inst + 0x24) + o) = a;
            GRef<u32>(GRef<u32>(inst + 0x28) + o) = WorkHeapAlloc(GRef<s32>(r + 0x1C) << 6, 0x10);
        }
        GRef<u32>(GRef<u32>(inst + 0x2C) + o) = GRef<u32>(GRef<u32>(inst + 0x24) + o);
    }
    const u32 rec2 = GRef<u32>(inst + 0x30);
    if (GRef<u32>(inst + 4) != 0) {
        for (s32 i = 0; i < GRef<s32>(rec2 + 4); ++i) GRef<f32>(GRef<u32>(inst + 4) + 4 * static_cast<u32>(i)) = 0.5f;
    }
    for (s32 i = 0; i < GRef<s32>(rec2 + 8); ++i) {
        const u32 r = GRef<u32>(rec2 + 0x20) + static_cast<u32>(i) * 0x50;
        GRef<u32>(GRef<u32>(inst + 8) + 4 * static_cast<u32>(i)) = GRef<u32>(GRef<u32>(r) + 0x2C);
    }
    GRef<u32>(inst + 0x34) = GRef<u32>(rec2 + 0x1C) + 0x20;
    GRef<u32>(inst + 0x40) = GRef<s32>(inst + 0x3C) < 0xB4 ? GRef<u32>(GRef<u32>(GRef<u32>(rec2 + 0x20)) + 0x28)
                                                          : GRef<u32>(GRef<u32>(rec2 + 0x1C) + 0x38);
    GRef<u32>(inst + 0x44) = GRef<s32>(inst + 0x3C) < 0xBE ? 0 : GRef<u32>(GRef<u32>(rec2 + 0x1C) + 0x18);
    {
        guest::GuestFrame frame(0x90);
        const u32 m1 = frame.Address(), m2 = m1 + 0x40, idx = m1 + 0x80;
        GRef<u32>(idx) = 0;
        sceVu0UnitMatrix(m1);
        sceVu0UnitMatrix(m2);
        UpdateModelHierarchy(inst, idx, m1, m2, 0);
    }
    const u32 rec3 = GRef<u32>(inst + 0x30);
    for (s32 i = 0; i < GRef<s32>(rec3 + 8); ++i) {
        const u32 r = GRef<u32>(GRef<u32>(rec3 + 0x20) + static_cast<u32>(i) * 0x50);
        GRef<u8>(GRef<u32>(inst + 0xC) + static_cast<u32>(i)) = GRef<u8>(r + 0x3C);
    }
    GRef<s32>(inst + 0x48) = 0;
    const u32 nodes_base = GRef<u32>(rec + 0x20);
    for (s32 i = 0; i < GRef<s32>(rec3 + 8); ++i) {
        u32 p = GRef<u32>(nodes_base + static_cast<u32>(i) * 0x50 + 0x2C);
        s32 tris = 0;
        for (u32 tag = GRef<u32>(p); tag != 0x60000000u; tag = GRef<u32>(p)) {
            p += ((tag & 0xFFFF) + 6) * 16;
            tris += -2 + static_cast<s32>(((tag & 0xFFFF) - 1) * 4) / 9;
        }
        if (tris == 0) GRef<u32>(GRef<u32>(inst) + 4 * static_cast<u32>(i)) |= 8;
        GRef<s32>(inst + 0x48) += tris;
    }
}

// EE: 0x0013F4D0
// Pose of node `idx` from its keyframes: the part value x 100 (clamped 0..100) between the last key
// at or below it and the next one; translation and rotation lerped on VU0 (w = 1).
void InterpolatePartPose(u32 inst, s32 idx, u32 rot_out, u32 trans_out) {
    const u32 r = GRef<u32>(GRef<u32>(inst + 0x30) + 0x20) + static_cast<u32>(idx) * 0x50;
    const u32 keys = GRef<u32>(r + 4);
    if (keys == 0) return;
    const u32 values = GRef<u32>(inst + 4);
    if (values == 0) return;
    const s32 chan = GRef<s32>(GRef<u32>(inst + 8) + static_cast<u32>(idx) * 4);
    f32 f = fpu::Mul(GRef<f32>(values + static_cast<u32>(chan) * 4), 100.0f);
    const u32 pos = GRef<u32>(r + 0x20);
    const u32 val = GRef<u32>(r + 0x24);
    if (f < 0.0f) f = 0.0f;
    if (100.0f < f) f = 100.0f;
    const s32 n = GRef<s32>(keys);
    s32 k = -1;
    for (s32 i = 0; i < n; ++i) {
        if (fpu::IntToFloat(GRef<s8>(pos + static_cast<u32>(i))) <= f) k = i;
    }
    const s32 p0 = GRef<s8>(pos + static_cast<u32>(k));
    const s32 p1 = GRef<s8>(pos + static_cast<u32>(k) + 1);
    const f32 t = fpu::Div(fpu::Sub(f, fpu::IntToFloat(p0)), fpu::IntToFloat(p1 - p0));
    const u32 a = val + static_cast<u32>(k) * 0x24;
    const u32 b = val + static_cast<u32>(k) * 0x24 + 0x24;
    const vu0::Vec4 ta{GRef<f32>(a), GRef<f32>(a + 4), GRef<f32>(a + 8), 1.0f};
    const vu0::Vec4 ra{GRef<f32>(a + 0xC), GRef<f32>(a + 0x10), GRef<f32>(a + 0x14), 1.0f};
    const vu0::Vec4 tb{GRef<f32>(b), GRef<f32>(b + 4), GRef<f32>(b + 8), 1.0f};
    const vu0::Vec4 rb{GRef<f32>(b + 0xC), GRef<f32>(b + 0x10), GRef<f32>(b + 0x14), 1.0f};
    auto lerp = [t](const vu0::Vec4& x, const vu0::Vec4& y) {
        return vu0::Vec4{vu0::Add(vu0::Mul(vu0::Sub(y.x, x.x), t), x.x), vu0::Add(vu0::Mul(vu0::Sub(y.y, x.y), t), x.y),
                         vu0::Add(vu0::Mul(vu0::Sub(y.z, x.z), t), x.z), vu0::Add(vu0::Mul(vu0::Sub(y.w, x.w), t), x.w)};
    };
    StoreV(rot_out, lerp(ra, rb));
    StoreV(trans_out, lerp(ta, tb));
}

// EE: 0x0013F6C0
// Morph node `idx`: the node's mesh list rebuilt into the instance buffer of this pass (by the
// visited byte) from morph targets A and B around the part value (key table +0x34: {u32 percent, u32
// target}), interpolating ST (s16), RGBA (u8), position (f32 xyz) and normal (s16 xyz) per vertex
// with the EE FPU. With only one target, its strips are copied.
// fpucheck: verified 31 (5 unsigned-convert adds, 1 sub duplicated in two paths, 4 + 8 + 6 + 6 in
// the unrolled ST, RGBA, position and normal loops)
void BuildMorphMesh(u32 inst, s32 idx) {
    const u32 r = GRef<u32>(GRef<u32>(inst + 0x30) + 0x20) + static_cast<u32>(idx) * 0x50;
    if ((GRef<u32>(r + 0x18) & 4) == 0) return;
    const u32 o4 = static_cast<u32>(idx) * 4;
    const s32 chan = GRef<s32>(GRef<u32>(inst + 8) + o4);
    f32 f = fpu::Mul(GRef<f32>(GRef<u32>(inst + 4) + static_cast<u32>(chan) * 4), 100.0f);
    s32 ka = 0, kb = -1;
    const u32 ktab = GRef<u32>(r + 0x34);
    const u32 buf_arr = GRef<u32>(inst + 0x24 + 4u * GRef<s8>(GRef<u32>(inst + 0x20) + static_cast<u32>(idx)));
    const u32 out_buf = GRef<u32>(buf_arr + o4);
    GRef<u32>(GRef<u32>(inst + 0x2C) + o4) = out_buf;
    const u32 targets = GRef<u32>(r + 0x38);
    const s32 nkeys = GRef<s32>(GRef<u32>(r + 0xC));
    f32 t = 0.0f;
    if (f <= fpu::UnsignedToFloat(GRef<u32>(ktab))) {
        ka = GRef<s32>(ktab + 4);
    } else if (fpu::UnsignedToFloat(GRef<u32>(ktab + static_cast<u32>(nkeys) * 8 - 8)) <= f) {
        ka = GRef<s32>(ktab + static_cast<u32>(nkeys) * 8 - 4);
    } else {
        for (s32 i = 0; i < nkeys; ++i) {
            const u32 e = ktab + static_cast<u32>(i) * 8;
            if (f < fpu::UnsignedToFloat(GRef<u32>(e))) {
                const u32 prev = GRef<u32>(e - 8);
                ka = GRef<s32>(e - 4);
                kb = GRef<s32>(e + 4);
                f = fpu::Sub(f, fpu::UnsignedToFloat(prev));
                t = fpu::Div(f, fpu::UnsignedToFloat(GRef<u32>(e) - prev));
                break;
            }
        }
    }
    u32 a = GRef<u32>(targets + static_cast<u32>(ka) * 4);
    u32 b = kb == -1 ? 0 : GRef<u32>(targets + static_cast<u32>(kb) * 4);
    const s32 blocks = GRef<s32>(a);
    if (b != 0) b += 0x10;
    a += 0x10;
    FlushCache(0);
    u32 base = GRef<u32>(r + 0x2C);
    u32 out = out_buf;
    for (s32 blk = 0; blk < blocks; ++blk) {
        const u32 qwc = GRef<u32>(a + 4);
        if (b != 0) b += 0x10;
        base += 0x10;
        a += 0x10;
        GRef<u32>(out) = qwc | 0x10000000u;
        GRef<u32>(out + 4) = 0;
        GRef<u32>(out + 8) = 0;
        GRef<u32>(out + 12) = 0x01000104;  // STCYCL CL=4 WL=1
        out += 16;
        if (b == 0) {
            for (u32 q = 0; q < qwc; ++q, out += 16, a += 16)
                for (u32 w = 0; w < 16; w += 4) GRef<u32>(out + w) = GRef<u32>(a + w);
            base += qwc * 16;
        } else {
            const u32 pad = static_cast<u32>(static_cast<s32>((qwc - 1) * 4) % 9);
            u32 pa = a + pad * 4, pb = b + pad * 4;
            const u32 nverts = GRef<u8>(pa + 2);
            u32 v = out;
            for (u32 i = 0; i < pad; ++i, v += 4) GRef<u32>(v) = 0;
            auto lerp_int = [t](s32 x, s32 y) {
                const f32 fx = fpu::IntToFloat(x), fy = fpu::IntToFloat(y);
                return fpu::FloatToInt(fpu::Add(fpu::Mul(fpu::Sub(fy, fx), t), fx));
            };
            // ST (V4-16): s, t interpolated; the second word (z, w) copied.
            GRef<u32>(v) = GRef<u32>(pa);
            pa += 4;
            pb += 4;
            v += 4;
            for (u32 i = 0; i < nverts; ++i, v += 8, pa += 8, pb += 8) {
                GRef<u32>(v + 4) = GRef<u32>(pa + 4);
                GRef<s16>(v) = static_cast<s16>(lerp_int(GRef<s16>(pa), GRef<s16>(pb)));
                GRef<s16>(v + 2) = static_cast<s16>(lerp_int(GRef<s16>(pa + 2), GRef<s16>(pb + 2)));
            }
            // RGBA (V4-8, unsigned).
            GRef<u32>(v) = GRef<u32>(pa);
            pa += 4;
            pb += 4;
            v += 4;
            for (u32 i = 0; i < nverts; ++i, v += 4, pa += 4, pb += 4)
                for (u32 c = 0; c < 4; ++c) GRef<u8>(v + c) = static_cast<u8>(lerp_int(GRef<u8>(pa + c), GRef<u8>(pb + c)));
            // Position (V4-32): x, y, z interpolated; w copied.
            GRef<u32>(v) = GRef<u32>(pa);
            pa += 4;
            pb += 4;
            v += 4;
            for (u32 i = 0; i < nverts; ++i, v += 16, pa += 16, pb += 16) {
                GRef<u32>(v + 12) = GRef<u32>(pa + 12);
                for (u32 c = 0; c < 12; c += 4) {
                    const f32 x = GRef<f32>(pa + c), y = GRef<f32>(pb + c);
                    GRef<f32>(v + c) = fpu::Add(fpu::Mul(fpu::Sub(y, x), t), x);
                }
            }
            // Normal (V4-16): x, y, z interpolated; w copied.
            GRef<u32>(v) = GRef<u32>(pa);
            pa += 4;
            pb += 4;
            v += 4;
            for (u32 i = 0; i < nverts; ++i, v += 8, pa += 8, pb += 8) {
                GRef<s16>(v + 6) = GRef<s16>(pa + 6);
                for (u32 c = 0; c < 6; c += 2) GRef<s16>(v + c) = static_cast<s16>(lerp_int(GRef<s16>(pa + c), GRef<s16>(pb + c)));
            }
            b += qwc * 16;
            a += qwc * 16;
            out += qwc * 16;
            base += qwc * 16;
        }
        // The strip's tail (header unpack and MSCNT): 5 qwords from the base list.
        for (u32 q = 0; q < 5; ++q, out += 16, base += 16)
            for (u32 w = 0; w < 16; w += 4) GRef<u32>(out + w) = GRef<u32>(base + w);
        if (b != 0) b += 0x50;
        a += 0x50;
    }
    GRef<u32>(out) = 0x60000000;
    GRef<u32>(out + 4) = 0;
    GRef<u32>(out + 8) = 0;
    GRef<u32>(out + 12) = 0;
}

// EE: 0x0013FE58
// Node matrices of node *index and its subtree (preorder): toggles the visited byte; unless skipped
// (version >= 0xB4 with flag 1: node +0x38 bit 0 clear, or a clean node under a clean parent), the
// pose (node T/R, keyframes), local matrices, parent products, morph mesh, origin (M * (0,0,0,1)) and
// axis (N * (0,0,1,0)).
void UpdateModelHierarchy(u32 inst, u32 index, u32 parent_m, u32 parent_n, u32 flags) {
    guest::GuestFrame frame(0xE0);
    const u32 rot = frame.Address(), trans = rot + 0x10, full = rot + 0x40, rotm = rot + 0x80;
    const u32 zero_w = rot + 0x20, axis = rot + 0x30;
    StoreV(zero_w, {0.0f, 0.0f, 0.0f, 1.0f});
    StoreV(axis, {0.0f, 0.0f, 1.0f, 0.0f});
    const s32 idx = GRef<s32>(index);
    const u32 node = GRef<u32>(GRef<u32>(GRef<u32>(inst + 0x30) + 0x20) + static_cast<u32>(idx) * 0x50);
    GRef<u8>(GRef<u32>(inst + 0x20) + static_cast<u32>(idx)) ^= 1;
    const u32 m = GRef<u32>(inst + 0x10) + static_cast<u32>(idx) * 0x40;
    const u32 n = GRef<u32>(inst + 0x14) + static_cast<u32>(idx) * 0x40;
    const u32 origin = GRef<u32>(inst + 0x18) + static_cast<u32>(idx) * 0x10;
    const u32 axis_out = GRef<u32>(inst + 0x1C) + static_cast<u32>(idx) * 0x10;
    bool skip = false;
    if (GRef<s32>(inst + 0x3C) >= 0xB4 && (flags & 1) != 0) {
        const u32 st = GRef<u32>(inst) + static_cast<u32>(idx) * 4;
        skip = ((GRef<u32>(node + 0x38) ^ 1) & 1) != 0;
        if ((GRef<u32>(st) & 4) != 0) {
            flags |= 0x10000;
            GRef<u32>(st) &= ~4u;
        } else if ((flags & 0x10000) == 0) {
            skip = true;
        }
    }
    if (!skip) {
        sceVu0CopyVector(rot, node + 0x10);
        sceVu0CopyVector(trans, node);
        InterpolatePartPose(inst, GRef<s32>(index), rot, trans);
        BuildRotationMatrix(full, rotm, rot, trans);
        MulMatrixVu0(n, parent_n, rotm);
        MulMatrixVu0(m, parent_m, full);
        BuildMorphMesh(inst, GRef<s32>(index));
        sceVu0ApplyMatrix(origin, m, zero_w);
        sceVu0ApplyMatrix(axis_out, n, axis);
    }
    for (s32 i = 0; i < GRef<s32>(node + 0x30); ++i) {
        GRef<s32>(index) += 1;
        UpdateModelHierarchy(inst, index, m, n, flags);
    }
}

// EE: 0x00140F68
// Part value `channel` = v; when it changes, the nodes on that channel are marked dirty (flag 4).
void SetModelPartValue(f32 v, u32 inst, s32 channel) {
    if (GRef<u32>(inst + 4) == 0) return;
    const u32 rec = GRef<u32>(inst + 0x30);
    if (rec == 0 || channel >= GRef<s32>(rec + 4)) return;
    const u32 p = GRef<u32>(inst + 4) + static_cast<u32>(channel) * 4;
    if (GRef<f32>(p) == v) return;
    GRef<f32>(p) = v;
    for (s32 i = 0; i < GRef<s32>(rec + 8); ++i) {
        if (GRef<s32>(GRef<u32>(inst + 8) + static_cast<u32>(i) * 4) == channel)
            GRef<u32>(GRef<u32>(inst) + static_cast<u32>(i) * 4) |= 4;
    }
}

// EE: 0x001404A8
// VIF packets of the visible nodes of `inst` at `p`: per node, twice (both VU1 input buffers), the
// strip header word at VU 0x1D (29) and an MSCAL 0; the node's packet call; twice the matrices at VU
// 0 (screen x node, clip x node, light x node rotation) and the four GIF tags at 0x15/0x1F/0x16/0x19
// (PRIM by pass and part flags); then a DMA call to the node's mesh list. Mode bits 0-1: pass (0
// opaque parts, 1/2 translucent parts with backface sense 0/1), 4 clipping, 8 no common packet,
// 0x20 no texture/fog/Gouraud defaults, 0x40 untextured. Returns the end of the packet.
u32 EmitModelInstance(u32 inst, u32 p, u32 cam, u32 world, u32 light, u32 mode) {
    const s32 dist = fpu::FloatToInt(GRef<f32>(cam + 0x168));
    const s32 zbyte = ((dist < 0 ? dist + 0x3F : dist) >> 6) + 0x11;
    u32 header = 0;
    const u32 pass = mode & 3;
    guest::GuestFrame frame(0x80);
    const u32 ws = frame.Address(), wc = ws + 0x40;
    MulMatrixVu0(ws, cam + 0xE0, world);
    MulMatrixVu0(wc, cam + 0x120, world);
    const u32 rec = GRef<u32>(inst + 0x30);
    if (pass == 0 && (mode & 8) == 0) {
        GRef<u32>(p) = 0x50000000;
        GRef<u32>(p + 4) = GRef<u32>(rec + 0x10);
        GRef<u32>(p + 8) = 0;
        GRef<u32>(p + 12) = 0;
        p += 16;
    }
    for (s32 i = 0; i < GRef<s32>(rec + 8); ++i) {
        const u32 r = GRef<u32>(rec + 0x20) + static_cast<u32>(i) * 0x50;
        const u32 st = GRef<u32>(GRef<u32>(inst) + static_cast<u32>(i) * 4);
        if ((st & 1) != 0 || (st & 2) != 0) continue;
        const bool translucent = (GRef<u32>(r + 0x18) & 1) != 0;
        if (pass == 0) {
            if (translucent) continue;
        } else {
            if (!translucent) continue;
            if (pass == 1) header = 0;
            else if (pass == 2) header = 1;
        }
        const u32 mh = GRef<u32>(r + 8);
        header |= static_cast<u32>(zbyte) << 16;
        const u32 vif_call = GRef<u32>(r + 0x28);
        if ((mode & 4) != 0) header |= 0x100;
        const u32 no_defaults = mode & 0x20, untextured = mode & 0x40;
        for (int k = 0; k < 2; ++k, p += 0x20) {
            GRef<u32>(p) = 0x10000001;  // cnt 1
            GRef<u32>(p + 4) = 0;
            GRef<u32>(p + 8) = 0x6E01C01D;  // UNPACK V4-8 1 -> 0x1D
            GRef<u32>(p + 12) = header;
            GRef<u32>(p + 16) = 0;
            GRef<u32>(p + 20) = 0;
            GRef<u32>(p + 24) = 0;
            GRef<u32>(p + 28) = 0x14000000;  // MSCAL 0
        }
        GRef<u32>(p) = 0x50000000;  // call the node packet
        GRef<u32>(p + 4) = vif_call;
        GRef<u32>(p + 8) = 0;
        GRef<u32>(p + 12) = 0;
        p += 16;
        const u32 mflags = GRef<u32>(mh + 0xC);
        const u32 alpha_part = mflags & 1;
        const u32 fan_flag = mflags & 8;
        for (int k = 0; k < 2; ++k) {
            GRef<u32>(p) = 0x1000000D;  // cnt 13
            GRef<u32>(p + 4) = 0;
            GRef<u32>(p + 8) = 0;
            GRef<u32>(p + 12) = 0x6C0C8000;  // UNPACK V4-32 12 -> 0
            MulMatrixVu0(p + 0x10, ws, GRef<u32>(inst + 0x10) + static_cast<u32>(i) * 0x40);
            MulMatrixVu0(p + 0x50, wc, GRef<u32>(inst + 0x10) + static_cast<u32>(i) * 0x40);
            MulMatrixVu0(p + 0x90, light, GRef<u32>(inst + 0x14) + static_cast<u32>(i) * 0x40);
            GRef<u32>(p + 0xD0) = 0;
            GRef<u32>(p + 0xD4) = 0;
            GRef<u32>(p + 0xD8) = 0;
            GRef<u32>(p + 0xDC) = 0x14000000;  // MSCAL 0
            u32 prim = alpha_part != 0 ? 0x45 : 5;
            if (no_defaults == 0) prim |= 0x38;
            if (untextured != 0) prim &= 0x6D;
            const u32 q = p + 0xE0;
            auto tag = [](u32 at, u32 unpack, u32 prim_bits) {
                GRef<u32>(at) = 0x10000001;
                GRef<u32>(at + 4) = 0;
                GRef<u32>(at + 8) = 0;
                GRef<u32>(at + 12) = unpack;
                GRef<u32>(at + 16) = 0;
                GRef<u32>(at + 20) = prim_bits << 15 | 0x30004000;
                GRef<u32>(at + 24) = 0x412;
            };
            tag(q, 0x6C01C015, prim);
            u32 strip = prim | 0x40;
            tag(q + 0x20, 0x6C01C01F, prim | 0x240);
            if (alpha_part == 0) strip = prim & 0xFFFFFDBFu;
            strip = (strip & ~7u) | (fan_flag != 0 ? 5u : 4u);
            tag(q + 0x40, 0x6C01C016, strip);
            tag(q + 0x60, 0x6C01C019, strip | 0x240);
            p = q + 0x80;
        }
        GRef<u32>(p) = 0x50000000;  // call the mesh list
        GRef<u32>(p + 4) = GRef<u32>(GRef<u32>(inst + 0x2C) + static_cast<u32>(i) * 4);
        GRef<u32>(p + 8) = 0;
        GRef<u32>(p + 12) = 0;
        p += 16;
    }
    return p;
}

// EE: 0x00140408
// Hides (flag 2) the nodes of class 0x10 when mask bit 0 is set and of class 0x20 when bit 1 is
// set (node flags +0x38 of the model's node records); shows the others.
void func_00140408(u32 inst, u64 mask) {
    const s32 nodes = GRef<s32>(GRef<u32>(inst + 0x30) + 8);
    const u32 recs = GRef<u32>(GRef<u32>(inst + 0x30) + 0x20);
    for (s32 k = 0; k < nodes; ++k) {
        const u32 cls = GRef<u32>(GRef<u32>(recs + static_cast<u32>(k) * 0x50) + 0x38);
        const u32 f = GRef<u32>(inst) + 4 * static_cast<u32>(k);
        const bool hide = ((cls & 0x10) != 0 && (mask & 1) != 0) || ((cls & 0x20) != 0 && (mask & 2) != 0);
        GRef<u32>(f) = hide ? (GRef<u32>(f) | 2) : (GRef<u32>(f) & 0xFFFFFFFDu);
    }
}

// EE: 0x00141000
// Animation track n time of a model instance (0 without tracks or out of range).
u32 func_00141000(u32 inst, s32 n) {
    if (GRef<u32>(inst + 4) != 0 && GRef<u32>(inst + 0x30) != 0 && n < GRef<s32>(GRef<u32>(inst + 0x30) + 4))
        return GRef<u32>(GRef<u32>(inst + 4) + 4 * static_cast<u32>(n));
    return 0;
}

// EE: 0x00141040
// Shows (on 1) / hides (on 0) every node of node id `id` (flag 1); other values change nothing.
void func_00141040(u32 inst, s32 id, s32 on) {
    const u32 model = GRef<u32>(inst + 0x30);
    for (s32 k = 0; k < GRef<s32>(model + 8); ++k) {
        const u32 o = 4 * static_cast<u32>(k);
        if (GRef<s32>(GRef<u32>(inst + 8) + o) != id) continue;
        if (on == 0) GRef<u32>(GRef<u32>(inst) + o) &= 0xFFFFFFFEu;
        else if (on == 1) GRef<u32>(GRef<u32>(inst) + o) |= 1;
    }
}

}  // namespace ac04::game
