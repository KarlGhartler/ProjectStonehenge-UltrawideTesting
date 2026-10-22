// Segment collision against the terrain and the static map objects (docs/subsystems/weapons.md
// "Terrain queries"): the terrain segment test 0x0013D698 walks the 2048-unit cells the segment
// crosses (at most 16), tests the static objects of each cell (0x00144DC8 -> 0x00143EF8) and the
// 8 triangles of the cell's 3 x 3 sample fan (0x0017C4E8). Used by the cameras, shadows,
// projectiles, landing and crashes. Triangle hits keep the smallest segment parameter t.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

vu0::Vec4 Load(u32 p) { return {GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; }

// The sign test of the three edge products (cvt.w.s, so |k| < 1 counts as 0): inside when all are
// >= 0 or all are <= 0.
bool InsideTriangle(f32 k0, f32 k1, f32 k2) {
    const s32 a = fpu::FloatToInt(k0), b = fpu::FloatToInt(k1), c = fpu::FloatToInt(k2);
    return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}

// Whether the segment [from, to] reaches the plane of point p0 with normal n, and where (shared by
// 0x0017C250 and 0x00143EF8). `w` is the segment vector, i1 = w.n, i0 = (p0 - from).n.
bool SegmentReachesPlane(f32 i1, f32 i0) {
    if (0.0f < i1) return !(i0 < 0.0f) && !(i1 < i0);
    return !(0.0f < i0) && !(i0 < i1);
}

// Point on the segment at t = i0 / i1 and the triangle test against p0, p1, p2 with normal n.
// Writes the point (w = t) to `hit` when inside.
bool HitTriangle(u32 tmp, u32 p0, u32 p1, u32 p2, u32 n, u32 from, u32 seg, f32 i0, f32 i1, u32 hit) {
    const f32 t = fpu::Div(i0, i1);
    const u32 pt = tmp, e0 = tmp + 0x10, e1 = tmp + 0x20, e2 = tmp + 0x30, c0 = tmp + 0x40, c1 = tmp + 0x50,
              c2 = tmp + 0x60;
    sceVu0ScaleVector(pt, seg, t);
    sceVu0AddVector(pt, pt, from);
    sceVu0SubVector(e0, p0, pt);
    sceVu0SubVector(e1, p1, pt);
    sceVu0SubVector(e2, p2, pt);
    sceVu0OuterProduct(c0, e0, e1);
    sceVu0OuterProduct(c1, e1, e2);
    sceVu0OuterProduct(c2, e2, e0);
    const f32 k0 = sceVu0InnerProduct(c0, n), k1 = sceVu0InnerProduct(c1, n), k2 = sceVu0InnerProduct(c2, n);
    if (!InsideTriangle(k0, k1, k2)) return false;
    sceVu0CopyVector(hit, pt);
    GRef<f32>(hit + 0xC) = t;
    return true;
}

}  // namespace

// EE: 0x0017BFD0
// Unit normal of triangle (a, b, c): (c - a) x (b - a) / |...|, w = 0 (VU0).
void func_0017BFD0(u32 out, u32 a, u32 b, u32 c) {
    const vu0::Vec4 va = Load(a), vb = Load(b), vc = Load(c);
    const vu0::Vec4 e1{vu0::Sub(vb.x, va.x), vu0::Sub(vb.y, va.y), vu0::Sub(vb.z, va.z), 0};
    const vu0::Vec4 e2{vu0::Sub(vc.x, va.x), vu0::Sub(vc.y, va.y), vu0::Sub(vc.z, va.z), 0};
    const f32 nx = vu0::Sub(vu0::Mul(e2.y, e1.z), vu0::Mul(e1.y, e2.z));
    const f32 ny = vu0::Sub(vu0::Mul(e2.z, e1.x), vu0::Mul(e1.z, e2.x));
    const f32 nz = vu0::Sub(vu0::Mul(e2.x, e1.y), vu0::Mul(e1.x, e2.y));
    const f32 s = vu0::Add(vu0::Add(vu0::Mul(nx, nx), vu0::Mul(ny, ny)), vu0::Mul(nz, nz));
    const f32 q = vu0::Div(1.0f, vu0::Add(0.0f, vu0::Sqrt(s)));
    GRef<f32>(out) = vu0::Mul(nx, q);
    GRef<f32>(out + 4) = vu0::Mul(ny, q);
    GRef<f32>(out + 8) = vu0::Mul(nz, q);
    GRef<f32>(out + 12) = 0.0f;
}

// EE: 0x0017C250
// Segment [from, to] against triangle (p0, p1, p2) with normal n (zero normal: no hit). On a hit
// the point (w = segment parameter) goes to `hit`. Returns 1 on a hit.
s32 func_0017C250(u32 p0, u32 p1, u32 p2, u32 from, u32 to, u32 hit, u32 n) {
    if (GRef<f32>(n) == 0.0f && GRef<f32>(n + 4) == 0.0f && GRef<f32>(n + 8) == 0.0f) return 0;
    guest::GuestFrame frame(0x90);
    const u32 seg = frame.Address(0x80), d0 = frame.Address(0x70);
    sceVu0SubVector(seg, to, from);
    const f32 i1 = sceVu0InnerProduct(seg, n);
    sceVu0SubVector(d0, p0, from);
    const f32 i0 = sceVu0InnerProduct(d0, n);
    if (!SegmentReachesPlane(i1, i0)) return 0;
    return HitTriangle(frame.Address(), p0, p1, p2, n, from, seg, i0, i1, hit) ? 1 : 0;
}

// EE: 0x0017C4E8
// Segment against triangle (p0, p1, p2): its unit normal (also stored to `normal`), then the
// segment test.
s32 func_0017C4E8(u32 p0, u32 p1, u32 p2, u32 from, u32 to, u32 hit, u32 normal) {
    guest::GuestFrame frame(0x10);
    const u32 n = frame.Address();
    func_0017BFD0(n, p0, p1, p2);
    sceVu0CopyVector(normal, n);
    return func_0017C250(p0, p1, p2, from, to, hit, n);
}

// EE: 0x00143EF8
// Segment against `count` baked object triangles (0x40 each: p0, p1, p2, unit normal; see
// func_00143C10). The nearest hit (smallest t) goes to `hit` (w = t, 1.0 when none) and its normal
// to `normal`. Returns 1 when something was hit.
s32 func_00143EF8(u32 tris, s32 count, u32 hit, u32 from, u32 to, u32 normal) {
    f32 best = 1.0f;
    GRef<f32>(hit + 0xC) = 1.0f;
    guest::GuestFrame frame(0xB0);
    const u32 cand = frame.Address(0xA0), seg = frame.Address(0x90), d0 = frame.Address(0x80);
    for (; count > 0; --count, tris += 0x40) {
        const u32 n = tris + 0x30;
        if (GRef<f32>(n) == 0.0f && GRef<f32>(n + 4) == 0.0f && GRef<f32>(n + 8) == 0.0f) continue;
        sceVu0SubVector(seg, to, from);
        const f32 i1 = sceVu0InnerProduct(seg, n);
        sceVu0SubVector(d0, tris, from);
        const f32 i0 = sceVu0InnerProduct(d0, n);
        if (i1 == 0.0f && i0 == 0.0f) continue;
        if (!SegmentReachesPlane(i1, i0)) continue;
        if (!HitTriangle(frame.Address(), tris, tris + 0x10, tris + 0x20, n, from, seg, i0, i1, cand)) continue;
        if (!(GRef<f32>(cand + 0xC) < GRef<f32>(hit + 0xC))) continue;
        CopyQword(hit, cand);
        CopyQword(normal, n);
        best = GRef<f32>(hit + 0xC);
    }
    return best != 1.0f ? 1 : 0;
}

// EE: 0x00144DC8
// Segment against the static object of terrain cell (x, z) (folded like func_0013D590): skipped
// when both ends are above the object's radius (+0x40 as -y); a kind placed once uses its only
// triangle buffer, others the buffer of the placement at this cell. The object's attribute word
// (+0x38) goes to `attr`.
s32 func_00144DC8(u32 obj, u32 hit, u32 from, u32 to, s32 x, s32 z, u32 normal, u32 attr) {
    s32 cx = x > -1 ? x : (x & 0xF);
    s32 cz = z > -1 ? z : (z & 0xF);
    if (cx >= 0x100) cx = (cx & 0xF) + 0xF0;
    if (cz >= 0x100) cz = (cz & 0xF) + 0xF0;
    const s32 bz = (cz > -1 ? cz : cz + 0xF) >> 4, bx = (cx > -1 ? cx : cx + 0xF) >> 4;
    const u32 block = GRef<u8>(GRef<u32>(obj + 0x7018) + static_cast<u32>(bz * 16 + bx));
    const u32 kind = GRef<u8>(GRef<u32>(obj + 0x7014) + ((static_cast<u32>(cz) & 0xF) << 4) + (block << 8) +
                               (static_cast<u32>(cx) & 0xF));
    if (kind == 0xFF) return 0;
    const u32 inst = GRef<u32>(obj + 0x7004) + kind * 0x4C;
    const f32 top = -GRef<f32>(inst + 0x40);
    if (GRef<f32>(from + 4) < top && GRef<f32>(to + 4) < top) return 0;
    s32 r = 0;
    const u32 placed = GRef<u8>(GRef<u32>(obj + 0x7030) + kind);
    const u32 tris = GRef<u8>(GRef<u32>(obj + 0x705C) + kind);
    if (placed == 1) {
        r = func_00143EF8(GRef<u32>(GRef<u32>(GRef<u32>(obj + 0x7028) + kind * 4)), static_cast<s32>(tris), hit, from, to, normal);
    } else {
        for (u32 i = 0; i < GRef<u8>(GRef<u32>(obj + 0x7030) + kind); ++i) {
            if (static_cast<u32>(cx) != GRef<u8>(GRef<u32>(GRef<u32>(obj + 0x704C) + kind * 4) + i)) continue;
            if (static_cast<u32>(cz) != GRef<u8>(GRef<u32>(GRef<u32>(obj + 0x7050) + kind * 4) + i)) continue;
            r = func_00143EF8(GRef<u32>(GRef<u32>(GRef<u32>(obj + 0x7028) + kind * 4) + i * 4),
                              static_cast<s32>(GRef<u8>(GRef<u32>(obj + 0x705C) + kind)), hit, from, to, normal);
            break;
        }
    }
    GRef<u32>(attr) = GRef<u32>(inst + 0x38);
    return r;
}

namespace {

// Terrain cell index of a world coordinate (fmodf and a 2048 bias for negative non-multiples).
// fpucheck: 1 sub
s32 TerrainCell(f32 v) {
    const f32 r = ee::fmodf(v, 2048.0f);
    s32 i;
    if (v < 0.0f && r != 0.0f) i = fpu::FloatToInt(fpu::Sub(v, 2048.0f));
    else i = fpu::FloatToInt(v);
    return ((i > -1 ? i : i + 0x7FF) >> 11) + 0x80;
}

}  // namespace

// EE: 0x0013D698
// Segment [from, to] against the terrain map and its static objects. Returns 1 on a hit with the
// point in `hit` (w = 1.0), the surface normal in `normal` and the material (attribute >> 12, or
// the object's attribute word) in `attr`. Writes the 3 x 3 sample heights of the tested cell into
// the terrain object's fan template (+0x1B9 ...), as the original does.
// fpucheck: 4 cell subs, slope 3 (from.z - to.z, from.x - to.x, z0 - k * x0), 1 add per column
s32 func_0013D698(u32 map, u32 hit_out, u32 attr_out, u32 from, u32 to, u32 normal_out) {
    const s32 x0 = TerrainCell(GRef<f32>(from + 0));
    const s32 z0 = TerrainCell(GRef<f32>(from + 8));
    const s32 x1 = TerrainCell(GRef<f32>(to + 0));
    const s32 z1c = TerrainCell(GRef<f32>(to + 8));
    const bool backwards = x1 < x0;
    const s32 xmin = backwards ? x1 : x0, xmax = backwards ? x0 : x1;
    const s32 zmin = z1c < z0 ? z1c : z0, zmax = z1c < z0 ? z0 : z1c;
    s32 span = xmax - xmin;
    const f32 slope = fpu::Div(fpu::Sub(GRef<f32>(from + 8), GRef<f32>(to + 8)), fpu::Sub(GRef<f32>(from), GRef<f32>(to)));
    const f32 icept = fpu::Sub(GRef<f32>(from + 8), slope * GRef<f32>(from));
    const u32 col = map + 0x4F8;  // z cell at each column boundary
    if (span == 0) {
        GRef<s32>(map + 0x4F8) = z0;
        GRef<s32>(map + 0x4FC) = z1c;
    } else if (span >= 0x11) {
        span = 0x10;
    } else {
        const s32 last = span + 1;
        for (s32 k = 0; k <= last; ++k) {
            const f32 z = fpu::Add(fpu::IntToFloat((k + xmin - 0x80) << 11) * slope, icept);
            const s32 iz = fpu::FloatToInt(z);
            const s32 c = (iz > -1 ? iz : iz + 0x7FF) >> 11;
            s32 v = c + 0x80;
            GRef<s32>(col + 4 * static_cast<u32>(k)) = v;
            if (z < 0.0f) {
                v = c + 0x7F;
                GRef<s32>(col + 4 * static_cast<u32>(k)) = v;
            }
            if (k == 0 || k == last) {
                if (v < zmin) {
                    GRef<s32>(col + 4 * static_cast<u32>(k)) = zmin;
                    v = zmin;
                }
                if (zmax < v) GRef<s32>(col + 4 * static_cast<u32>(k)) = zmax;
            }
        }
    }
    // Cells crossed, column by column in the direction of the segment (at most 16).
    s32 cells = 0;
    auto push = [&](s32 x, s32 z) {
        GRef<s16>(map + 0x4B8 + 2 * static_cast<u32>(cells)) = static_cast<s16>(x);
        GRef<s16>(map + 0x4D8 + 2 * static_cast<u32>(cells)) = static_cast<s16>(z);
        ++cells;
        return cells < 16;
    };
    const bool zup = z0 < z1c;
    auto column = [&](s32 k, s32 a, s32 b) {  // z range a -> b (inclusive), returns false when full
        if (zup ? b < a : a < b) return true;
        if (!push(k + xmin, a)) return false;
        if (zup) {
            for (s32 z = a + 1; z <= b; ++z)
                if (!push(k + xmin, z)) return false;
        } else {
            for (s32 z = a - 1; z >= b; --z)
                if (!push(k + xmin, z)) return false;
        }
        return true;
    };
    if (!backwards) {
        for (s32 k = 0; k <= span; ++k)
            if (!column(k, GRef<s32>(col + 4 * static_cast<u32>(k)), GRef<s32>(col + 4 * static_cast<u32>(k) + 4))) break;
    } else {
        for (s32 k = span; k >= 0; --k)
            if (!column(k, GRef<s32>(col + 4 * static_cast<u32>(k) + 4), GRef<s32>(col + 4 * static_cast<u32>(k)))) break;
    }
    guest::GuestFrame frame(0xC0);
    const u32 sp = frame.Address();
    const u32 va = sp, vb = sp + 0x10, vc = sp + 0x20, best = sp + 0x30, cand = sp + 0x40, cn = sp + 0x50,
              cattr = sp + 0x90, pos = sp + 0x94;
    GRef<f32>(best + 0xC) = 1.0f;
    for (s32 i = 0; i < cells; ++i) {
        const s32 x = GRef<s16>(map + 0x4B8 + 2 * static_cast<u32>(i));
        const s32 z = GRef<s16>(map + 0x4D8 + 2 * static_cast<u32>(i));
        if (func_00144DC8(GRef<u32>(0x0041069C), cand, from, to, x, z, cn, cattr) == 1 &&
            GRef<f32>(cand + 0xC) < GRef<f32>(best + 0xC)) {
            sceVu0CopyVector(best, cand);
            sceVu0CopyVector(normal_out, cn);
            GRef<u32>(attr_out) = GRef<u32>(cattr);
        }
        if (GRef<f32>(best + 0xC) != 1.0f) break;
        func_0013D590(pos + 0x00, pos + 0x04, x, z);
        func_0013D590(pos + 0x08, pos + 0x0C, x + 1, z);
        func_0013D590(pos + 0x10, pos + 0x14, x, z + 1);
        func_0013D590(pos + 0x18, pos + 0x1C, x + 1, z + 1);
        auto sample = [&](u32 k, u32 d) -> u8 {
            const u32 slot = GRef<u8>(GRef<u32>(map + 8) + GRef<u32>(pos + 8 * k));
            return GRef<u8>(GRef<u32>(map + 0xC) + (slot << 10) + GRef<u32>(pos + 8 * k + 4) + d);
        };
        GRef<u8>(map + 0x1B9) = sample(0, 0x21);
        const u8 h0 = sample(0, 0);
        GRef<u8>(map + 0x1C9) = h0;
        GRef<u8>(map + 0x1D9) = sample(0, 1);
        GRef<u8>(map + 0x1E9) = sample(1, 0);
        GRef<u8>(map + 0x1F9) = sample(1, 0x20);
        GRef<u8>(map + 0x209) = sample(3, 0);
        GRef<u8>(map + 0x219) = sample(2, 1);
        GRef<u8>(map + 0x229) = sample(2, 0);
        const s32 ox = (x - 0x80) * 2, oz = (z - 0x80) * 2;
        auto vertex = [&](u32 out, u32 rec) {
            GRef<f32>(out + 0) = fpu::IntToFloat((GRef<u8>(rec + 0) + ox) << 10);
            GRef<f32>(out + 4) = fpu::IntToFloat(GRef<u8>(rec + 1) * -0x50);
            GRef<f32>(out + 8) = fpu::IntToFloat((GRef<u8>(rec + 2) + oz) << 10);
        };
        GRef<u8>(map + 0x249) = h0;
        GRef<u8>(map + 0x239) = sample(0, 0x20);
        vertex(va, map + 0x1B8);
        GRef<f32>(va + 0xC) = 1.0f;
        vertex(vb, map + 0x1C8);
        GRef<f32>(vb + 0xC) = 1.0f;
        vertex(vc, map + 0x1D8);
        GRef<f32>(vc + 0xC) = 1.0f;
        for (s32 t = 0; t < 8; ++t) {
            GRef<f32>(cand + 0xC) = 1.0f;
            if (func_0017C4E8(vb, va, vc, from, to, cand, cn) == 1 && GRef<f32>(cand + 0xC) < GRef<f32>(best + 0xC)) {
                s32 ax = x > -1 ? x : (x & 0xF);
                s32 az = z > -1 ? z : (z & 0xF);
                if (ax >= 0x100) ax = (ax & 0xF) + 0xF0;
                if (az >= 0x100) az = (az & 0xF) + 0xF0;
                const s32 bz = (az > -1 ? az : az + 0xF) >> 4, bx = (ax > -1 ? ax : ax + 0xF) >> 4;
                const u32 blk = GRef<u8>(GRef<u32>(map + 0x1C) + static_cast<u32>(bz * 16 + bx));
                const u32 word = ((blk << 8) + static_cast<u32>((az - bz * 16) * 16 + (ax - bx * 16))) << 1;
                GRef<u32>(attr_out) = GRef<u16>(GRef<u32>(map + 0x20) + word) >> 12;
                sceVu0CopyVector(best, cand);
                sceVu0CopyVector(normal_out, cn);
            }
            sceVu0CopyVector(vb, vc);
            const u32 k = static_cast<u32>((t + 2) % 8 + 1);
            const u32 rec = map + 0x1B8 + k * 0x10;
            GRef<f32>(vc + 0) = fpu::IntToFloat((GRef<u8>(rec + 0) + ox) << 10);
            GRef<f32>(vc + 8) = fpu::IntToFloat((GRef<u8>(rec + 2) + oz) << 10);
            GRef<f32>(vc + 4) = fpu::IntToFloat(GRef<u8>(rec + 1) * -0x50);
        }
        if (GRef<f32>(best + 0xC) != 1.0f) break;
    }
    if (GRef<f32>(best + 0xC) == 1.0f) return 0;
    GRef<f32>(best + 0xC) = 1.0f;
    sceVu0CopyVector(hit_out, best);
    return 1;
}

}  // namespace ac04::game
