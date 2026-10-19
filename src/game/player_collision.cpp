// Player collision (slice S5c): the six hull segments against the terrain and the unit meshes,
// impact strength and damage, terrain lookups, and the point/box collision core of the waypoint
// and object tests with the VU0 flag reads (docs/port/validation.md B-2).
// Behaviour notes: docs/port/s5-blockB-inventory.csv.
#include <cmath>

#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

vu0::Vec4 Load(u32 p) { return {F(p), F(p + 4), F(p + 8), F(p + 12)}; }

// One hull segment of UpdatePlayerCollision: point `pt` (body frame, rotated), stored world
// position `slot` (old -> new), terrain test and, when the player is alive, the unit meshes.
// Returns the terrain hit count; adds to *strength and *mesh_hits; sets the contact bits.
s32 HullSegment(u32 u, u32 P, u32 pt, u32 slot, u32 prox, u32 sp, u16 tbit, u16 mbit, s32* strength, s32* mesh_hits) {
    const u32 vel = u + 0x20;
    const u32 to = sp + 0x40, from = sp + 0x50, hit = sp + 0x60, normal = sp + 0x30, d = sp + 0x20, attr = sp + 0x1B0;
    sceVu0AddVector(to, pt, u);
    sceVu0AddVector(to, to, vel);
    sceVu0CopyVector(from, slot);
    sceVu0CopyVector(slot, to);
    s32 n = 0;
    if (func_0013D698(Terrain(), hit, attr, from, to, normal) == 1) {
        sceVu0SubVector(d, hit, pt);
        n = 1;
        sceVu0SubVector(d, d, vel);
        sceVu0AddVector(d, d, normal);
        sceVu0AddVector(slot, d, pt);
        sceVu0CopyVector(u, d);
        GRef<u32>(u + 0x2C) = 0;
        GRef<f32>(u + 0xC) = 1.0f;
        *strength += func_00122268(vel, normal);
        GRef<u16>(P + 0x2F4) = static_cast<u16>(GRef<u16>(P + 0x2F4) | tbit);
    }
    if ((W(u + 0x4C) & 0x1000000u) != 0) {
        const s32 m = func_00122488(prox, u, slot, pt, from, to);
        *mesh_hits += m;
        if (0 < m) GRef<u16>(P + 0x2F4) = static_cast<u16>(GRef<u16>(P + 0x2F4) | mbit);
    }
    return n;
}

}  // namespace

// EE: 0x00122268
// Impact strength: |normalize(a) . b| * 100 truncated, at least 1.
s32 func_00122268(u32 a, u32 b) {
    guest::GuestFrame f(0x10);
    sceVu0Normalize(f.Address(), a);
    f32 d = sceVu0InnerProduct(f.Address(), b);
    if (d <= 0.0f) d = -d;
    const s32 i = fpu::FloatToInt(d * 100.0f);
    return i == 0 ? 1 : i;
}

// EE: 0x001222E8
// Proximity flags out[i] for the units near u (radius +0x74 + r); units with model 0xB2 take the
// hit themselves (+0x63 = p, hit flag 0x40).
// fpucheck: 3 adds (x + y, + z, radius + r); sqrt.S through libm sqrtf (NaN path unreachable on the EE)
void func_001222E8(u32 out, u32 u, s32 r, s32 p) {
    guest::GuestFrame f(0x10);
    const u32 d = f.Address();
    for (s32 i = 0; i < UnitCount(); ++i, out += 4) {
        GRef<u32>(out) = 0;
        if (i == p) continue;
        const u32 o = Unit(i);
        const u32 nf = ~W(o + 0x4C);
        if ((nf & 0x10000000u) != 0) continue;
        if ((nf & 0x1000000u) != 0) {
            const u8 cls = GRef<u8>(o + 0x54);
            if (cls == 1 || cls == 0 || (nf & 0x20000000u) != 0) continue;
        }
        sceVu0SubVector(d, o, u);
        sceVu0MulVector(d, d, d);
        const f32 dist = fpu::Sqrt(fpu::Add(fpu::Add(F(d), F(d + 4)), F(d + 8)));
        if (dist < fpu::Add(F(o + 0x74), fpu::IntToFloat(r))) GRef<u32>(out) = 1;
        if (GRef<u8>(o + 0x69) == 0xB2) {
            if (W(out) != 1) continue;
            GRef<u8>(o + 0x63) = static_cast<u8>(p);
            GRef<u32>(o + 0x50) |= 0x40u;
            GRef<u32>(out) = 0;
        }
    }
}

// EE: 0x00122488
// Segment (from, to) against the meshes of the flagged units; pushes u out on a hit.
s32 func_00122488(u32 prox, u32 u, u32 slot, u32 pt, u32 from, u32 to) {
    s32 hits = 0;
    guest::GuestFrame f(0x80);
    const u32 m = f.Address(0), hit = f.Address(0x40), normal = f.Address(0x50), d = f.Address(0x60);
    for (s32 i = 0; i < UnitCount(); ++i, prox += 4) {
        if (W(prox) == 0) continue;
        const u32 o = Unit(i);
        if ((~W(o + 0x48) & 0x100000u) != 0) continue;
        sceVu0UnitMatrix(m);
        RotateMatrix(m, m, o + 0x10);
        sceVu0TransMatrix(m, m, o);
        if (func_00146808(UnitModels(), hit, from, to, m, normal, i) == 0) continue;
        ++hits;
        sceVu0SubVector(d, hit, pt);
        sceVu0SubVector(d, d, u + 0x20);
        sceVu0AddVector(d, d, normal);
        sceVu0AddVector(slot, d, pt);
        sceVu0CopyVector(u, d);
        GRef<u32>(u + 0x2C) = 0;
        GRef<u32>(o + 0x50) |= 0x40u;
        GRef<f32>(u + 0xC) = 1.0f;
    }
    return hits;
}

// EE: 0x00122638
// Player hull collision, damage and contact bits (inventory note).
void UpdatePlayerCollision(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    guest::GuestFrame f(0x620);
    const u32 base = f.Address();
    const u32 prox = base;          // 255 words
    const u32 sp = base + 0x400;    // scratch vectors
    const u32 m = sp + 0x100, pts = sp + 0x140;
    s32 strength = 0, hits = 0, mesh = 0;
    func_001222E8(prox, u, fpu::FloatToInt(F(u + 0x70)), p);
    GRef<u16>(P + 0x2F4) = 0;
    GRef<u16>(P + 0x2F6) = 0;
    const u32 hull = W(W(W(AircraftModels()) + 4u * static_cast<u32>(p)) + 0x34);
    const f32 h0 = F(hull), h1 = F(hull + 4), h2 = F(hull + 8), h3 = F(hull + 0xC), h4 = F(hull + 0x10), h5 = F(hull + 0x14);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, u + 0x10);
    const f32 pv[6][3] = {{0, 0, h5}, {0, 0, h2}, {h3, 0, 0}, {h0, 0, 0}, {0, h1, 0}, {0, h4, 0}};
    for (u32 k = 0; k < 6; ++k) {
        const u32 q = pts + 0x10 * k;
        GRef<f32>(q) = pv[k][0];
        GRef<f32>(q + 4) = pv[k][1];
        GRef<f32>(q + 8) = pv[k][2];
        GRef<f32>(q + 12) = 1.0f;
        sceVu0ApplyMatrix(q, m, q);
    }
    if ((W(P + 0x188) & 0x800000u) != 0) {
        GRef<u32>(P + 0x188) &= 0xFF7FFFFFu;
        const u32 t = sp;
        for (u32 k = 0; k < 6; ++k) {
            sceVu0AddVector(t, pts + 0x10 * k, u);
            sceVu0AddVector(P + 0xB0 + 0x10 * k, t, u + 0x20);
        }
        return;
    }
    static const u16 kTerrainBit[6] = {0x10, 0x20, 4, 8, 1, 2};
    static const u16 kMeshBit[6] = {0x1000, 0x2000, 0x400, 0x800, 0x100, 0x200};
    // Scratch layout inside `sp`: to 0x40, from 0x50, hit 0x60, normal 0x30, d 0x20, attr 0x1B0.
    for (u32 k = 0; k < 6; ++k)
        hits += HullSegment(u, P, pts + 0x10 * k, P + 0xB0 + 0x10 * k, prox, sp, kTerrainBit[k], kMeshBit[k],
                            &strength, &mesh);
    const u32 attr = sp + 0x1B0;
    if (p < 2) {
        // Segment between hull slots 0 and 1; the push-out uses hull point 5 and slot 5 (as the
        // original, which reuses those registers).
        const u32 to = sp + 0x40, from = sp + 0x50, hit = sp + 0x60, normal = sp + 0x30, d = sp + 0x20;
        const u32 pt = pts + 0x50, slot = P + 0x100;
        sceVu0CopyVector(from, P + 0xB0);
        sceVu0CopyVector(to, P + 0xC0);
        if (func_0013D698(Terrain(), hit, attr, from, to, normal) == 1) {
            sceVu0SubVector(d, hit, pt);
            ++hits;
            sceVu0SubVector(d, d, u + 0x20);
            sceVu0AddVector(d, d, normal);
            sceVu0AddVector(slot, d, pt);
            sceVu0CopyVector(u, d);
            GRef<u32>(u + 0x2C) = 0;
            GRef<f32>(u + 0xC) = 1.0f;
            strength += func_00122268(u + 0x20, normal);
            GRef<u16>(P + 0x2F4) = static_cast<u16>(GRef<u16>(P + 0x2F4) | 2);
        }
        if ((W(u + 0x4C) & 0x1000000u) != 0) {
            const s32 n = func_00122488(prox, u, slot, pt, from, to);
            mesh += n;
            if (0 < n) GRef<u16>(P + 0x2F4) = static_cast<u16>(GRef<u16>(P + 0x2F4) | 0x200);
        }
    }
    bool flags = true;
    if (strength < 0x1F) {
        const u8 diff = Game(0x28);
        if (diff == 0) {
            GRef<s32>(P + 0x2D4) = GRef<s32>(P + 0x2D4) / 4;
        } else if (diff == 1) {
            GRef<s32>(P + 0x2D4) = GRef<s32>(P + 0x2D4) / 2;
        } else if (diff > 2) {
            if (strength < 1)
                flags = false;
            else
                GRef<s32>(P + 0x2D4) = 100;
        } else {
            GRef<s32>(P + 0x2D4) = strength;
        }
    } else {
        GRef<s32>(P + 0x2D4) = 100;
    }
    if (flags && 0 < strength) {
        GRef<u32>(u + 0x50) |= 0x80u;
        GRef<u32>(P + 0x2D8) = W(attr);
    }
    if (1 < hits && 1 < Game(0x28)) GRef<s32>(P + 0x2D4) = 1000;
    if (0 < mesh) GRef<u32>(u + 0x50) |= 0x40u;
    GRef<s16>(P + 0x2F6) = static_cast<s16>(hits + mesh);
}

// EE: 0x0013D618
// Terrain map cell (16 x 16 blocks of 16 x 16 sub-cells) of cell coordinates (x, z), clamped.
void func_0013D618(u32 out_block, u32 out_sub, s32 x, s32 z) {
    u32 ux = x < 0 ? (static_cast<u32>(x) & 0xF) : static_cast<u32>(x);
    u32 uz = z < 0 ? (static_cast<u32>(z) & 0xF) : static_cast<u32>(z);
    if (0xFF < static_cast<s32>(ux)) ux = (ux & 0xF) + 0xF0;
    if (0xFF < static_cast<s32>(uz)) uz = (uz & 0xF) + 0xF0;
    GRef<s32>(out_block) = (static_cast<s32>(ux) >> 4) + (static_cast<s32>(uz) >> 4) * 0x10;
    GRef<s32>(out_sub) = static_cast<s32>((ux & 0xF) + (uz & 0xF) * 0x10);
}

// EE: 0x0013E2B0
// Height of the first terrain/object hit straight below (from y -20480 to y 64) at pos.
f32 func_0013E2B0(u32 map, u32 pos) {
    guest::GuestFrame f(0x90);
    const u32 from = f.Address(0), to = f.Address(0x10), hit = f.Address(0x20), normal = f.Address(0x30),
              attr = f.Address(0x40);
    sceVu0CopyVector(from, pos);
    sceVu0CopyVector(to, pos);
    GRef<f32>(from + 4) = -20480.0f;
    GRef<f32>(to + 4) = 64.0f;
    func_0013D698(map, hit, attr, from, to, normal);
    // Without a hit the original returns the stale stack word (the segment always crosses the
    // terrain in the shipped maps).
    return F(hit + 4);
}

// EE: 0x0013E820
// Coarse terrain height (height byte * -80) of the last of the 2 x 2 cells at pos (only the last
// lookup is used).
f32 func_0013E820(u32 map, u32 pos) {
    const s32 ix = fpu::FloatToInt(F(pos)), iz = fpu::FloatToInt(F(pos + 8));
    const u32 t0 = W(W(map)), t1 = W(W(map) + 4);
    const s32 cx = ((ix < 0 ? ix - 1 : ix) >> 11) + 0x80;
    const s32 cz = ((iz < 0 ? iz - 1 : iz) >> 11) + 0x80;
    guest::GuestFrame f(0x10);
    u32 v = 0;
    for (s32 j = 0; j < 2; ++j)
        for (s32 i = 0; i < 2; ++i) {
            func_0013D590(f.Address(0), f.Address(4), cx + i, cz + j);
            v = GRef<u8>(t1 + static_cast<u32>(GRef<u8>(t0 + W(f.Address(0)))) * 0x400 + W(f.Address(4)));
        }
    return fpu::IntToFloat(static_cast<s32>(v)) * -80.0f;
}

// EE: 0x0013E978
// Terrain surface type at (pos.x, pos.z): top 4 bits of the cell word.
u32 func_0013E978(u32 map, u32 pos) {
    const s32 x = func_00139000(F(pos));
    const s32 z = func_00139000(F(pos + 8));
    guest::GuestFrame f(0x10);
    func_0013D618(f.Address(0), f.Address(4), x, z);
    const u32 idx = static_cast<u32>(GRef<u8>(W(map + 0x1C) + W(f.Address(0)))) * 0x100 + W(f.Address(4));
    return static_cast<u32>(GRef<u16>(idx * 2 + W(map + 0x20)) >> 12);
}

// EE: 0x00146808
// Segment (from, to) against unit u's collision mesh with matrix m (u < 2: the player aircraft
// meshes); the transformed triangle list is cached per unit in the collision buffer h.
s32 func_00146808(u32 h, u32 hit, u32 from, u32 to, u32 m, u32 normal, s32 u) {
    if (F(from) == F(to) && F(from + 4) == F(to + 4) && F(from + 8) == F(to + 8)) return 0;
    u32 mesh;
    if (u < 2) {
        const u32 a = W(W(AircraftModels() + 8) + 4u * static_cast<u32>(u));
        if (a == 0) return 0;
        mesh = a + 0x98;
        u -= 2;
    } else {
        const u32 o = Unit(u);
        const u32 lod = W(W(h) + static_cast<u32>(u) * 0x18 + 8) + static_cast<u32>(GRef<u8>(o + 0x5A)) * 0x20;
        const u8 model = GRef<u8>(o + 0x69);
        // Models 0x8D and 0x8E use the instance word itself (no +0x98 offset).
        mesh = (model < 0x8F && 0x8C < model) ? W(lod) : W(lod) + 0x98;
    }
    if (GRef<s32>(h + 0x8024) != u) {
        GRef<s32>(h + 0x8024) = u;
        GRef<s32>(h + 0x8020) = func_00143C10(mesh, h + 0x20, m);
    }
    return func_00143EF8(h + 0x20, GRef<s32>(h + 0x8020), hit, from, to, normal);
}

// EE: 0x0017C020
// Distance of point c from the line through a and b (VU0), or the distance to the nearer end
// point when the foot of the perpendicular falls outside. The two `cfc2` MAC-flag reads return the
// sign flags of `vf9.x - vf10.x` and `vf12.x - vf11.x` on hardware (validation.md B-2).
f32 func_0017C020(u32 /*tmp*/, u32 pa, u32 pb, u32 pc) {
    const vu0::Vec4 a = Load(pa), b = Load(pb), c = Load(pc);
    // vf7 = c - a, vf8 = b - a; vf7 = vf7 x vf8; vf7 = vf8 x vf7
    f32 e1x = vu0::Sub(c.x, a.x), e1y = vu0::Sub(c.y, a.y), e1z = vu0::Sub(c.z, a.z), e1w = vu0::Sub(c.w, a.w);
    const f32 e2x = vu0::Sub(b.x, a.x), e2y = vu0::Sub(b.y, a.y), e2z = vu0::Sub(b.z, a.z);
    const f32 nx = vu0::Sub(vu0::Mul(e1y, e2z), vu0::Mul(e2y, e1z));
    const f32 ny = vu0::Sub(vu0::Mul(e1z, e2x), vu0::Mul(e2z, e1x));
    const f32 nz = vu0::Sub(vu0::Mul(e1x, e2y), vu0::Mul(e2x, e1y));
    f32 vx = vu0::Sub(vu0::Mul(e2y, nz), vu0::Mul(ny, e2z));
    f32 vy = vu0::Sub(vu0::Mul(e2z, nx), vu0::Mul(nz, e2x));
    f32 vz = vu0::Sub(vu0::Mul(e2x, ny), vu0::Mul(nx, e2y));
    const f32 l2 = vu0::Add(vu0::Add(vu0::Mul(vx, vx), vu0::Mul(vy, vy)), vu0::Mul(vz, vz));
    const f32 q = vu0::Sqrt(l2);
    const f32 inv = vu0::Div(1.0f, vu0::Mul(1.0f, q));
    vx = vu0::Mul(vx, inv);
    vy = vu0::Mul(vy, inv);
    vz = vu0::Mul(vz, inv);
    (void)e1w;
    const f32 dx = vu0::Sub(a.x, c.x), dy = vu0::Sub(a.y, c.y), dz = vu0::Sub(a.z, c.z);
    const f32 dist = vu0::Add(vu0::Add(vu0::Mul(dx, vx), vu0::Mul(dy, vy)), vu0::Mul(dz, vz));
    const f32 px = vu0::Mul(vx, dist), py = vu0::Mul(vy, dist), pz = vu0::Mul(vz, dist);
    auto len2 = [](f32 x, f32 y, f32 z) { return vu0::Add(vu0::Add(vu0::Mul(x, x), vu0::Mul(y, y)), vu0::Mul(z, z)); };
    const f32 d9 = len2(vu0::Sub(a.x, b.x), vu0::Sub(a.y, b.y), vu0::Sub(a.z, b.z));
    const f32 d10 = len2(vu0::Sub(a.x, px), vu0::Sub(a.y, py), vu0::Sub(a.z, pz));
    const f32 d11 = len2(vu0::Sub(a.x, c.x), vu0::Sub(a.y, c.y), vu0::Sub(a.z, c.z));
    const f32 d12 = len2(vu0::Sub(b.x, c.x), vu0::Sub(b.y, c.y), vu0::Sub(b.z, c.z));
    f32 r = std::fabs(dist);
    if (std::signbit(vu0::Sub(d9, d10))) {
        if (!std::signbit(vu0::Sub(d12, d11)))
            r = vu0::Add(0.0f, vu0::Sqrt(d11));
        else
            r = vu0::Add(0.0f, vu0::Sqrt(d12));
    }
    return r;
}

// EE: 0x0017C130
// Dead (no reference): spheres (a, r1) and (b, r2) touch: 0 when r1 + r2 < |a - b|, else 1.
// fpucheck: verified 1
s32 func_0017C130(u32 unused, u32 a, u32 b, f32 r1, f32 r2) {
    (void)unused;
    const f32 d = func_00104F10(a, b);
    return fpu::Add(r1, r2) < d ? 0 : 1;
}

// EE: 0x0017C180
// Dead (no reference): point c lies at most `r` above the plane of triangle (p, a, b): the signed
// distance along the unit normal (a - p) x (b - p) is compared with r (1 when not above it).
s32 func_0017C180(u32 unused, u32 p, u32 a, u32 b, u32 c, f32 r) {
    (void)unused;
    guest::GuestFrame frame(0x40);
    const u32 n = frame.Address(), e1 = n + 0x10, e2 = n + 0x20, d = n + 0x30;
    sceVu0SubVector(e1, a, p);
    sceVu0SubVector(e2, b, p);
    sceVu0OuterProduct(n, e1, e2);
    sceVu0Normalize(n, n);
    sceVu0SubVector(d, c, p);
    return r < sceVu0InnerProduct(d, n) ? 0 : 1;
}

// EE: 0x0017C598
// Box of half size r around the origin extended by the velocity v (min +0x00, max +0x10).
// fpucheck: 12 static adds (6 per path)
void func_0017C598(f32 r, u32 box, u32 v) {
    const f32 n = -r;
    GRef<f32>(box + 0x18) = r;
    GRef<f32>(box + 0x14) = r;
    GRef<f32>(box + 0x10) = r;
    GRef<f32>(box + 8) = n;
    GRef<f32>(box + 4) = n;
    GRef<f32>(box) = n;
    for (u32 k = 0; k < 3; ++k) {
        const f32 c = F(v + 4 * k);
        if (c < 0.0f) {
            GRef<f32>(box + 4 * k) = fpu::Add(F(box + 4 * k), c);
            GRef<f32>(box + 0x10 + 4 * k) = fpu::Add(F(box + 0x10 + 4 * k), 0.0f);
        } else {
            GRef<f32>(box + 4 * k) = fpu::Add(F(box + 4 * k), 0.0f);
            GRef<f32>(box + 0x10 + 4 * k) = fpu::Add(F(box + 0x10 + 4 * k), c);
        }
    }
}

// EE: 0x0017C688
// Whether point p - unit position o is inside the box (min - r, max + r) on x, y and z (strict).
// The two cfc2 reads see the MAC flags of the two subtractions in order (validation.md B-2).
s32 func_0017C688(f32 r, u32 box, u32 p, u32 o) {
    const vu0::Vec4 a = Load(p), b = Load(o), lo = Load(box), hi = Load(box + 0x10);
    const f32 x = vu0::Sub(a.x, b.x), y = vu0::Sub(a.y, b.y), z = vu0::Sub(a.z, b.z);
    const bool below_max = std::signbit(vu0::Sub(x, vu0::Add(hi.x, r))) && std::signbit(vu0::Sub(y, vu0::Add(hi.y, r))) &&
                           std::signbit(vu0::Sub(z, vu0::Add(hi.z, r)));
    const bool above_min = std::signbit(vu0::Sub(vu0::Sub(lo.x, r), x)) && std::signbit(vu0::Sub(vu0::Sub(lo.y, r), y)) &&
                           std::signbit(vu0::Sub(vu0::Sub(lo.z, r), z));
    return below_max && above_min ? 1 : 0;
}

// EE: 0x0017C708
s32 func_0017C708(f32 r, u32 a, u32 b, u32 c) {
    guest::GuestFrame f(0x40);
    return func_0017C020(f.Address(), a, b, c) < r ? 1 : 0;
}

// EE: 0x0017C758
// Moving-point collider: point pointer +0x154, box of radius 5 around its velocity.
void func_0017C758(u32 col, u32 pt) {
    GRef<u32>(col + 0x154) = pt;
    func_0017C598(5.0f, col + 0x130, pt + 0x20);
}

// EE: 0x0017C778
// Moving point (collider +0x154) against unit o by its collision kinds (+0x48: 0x10000 box,
// 0x20000 radius around the path, 0x40000 mesh); a mesh hit moves the point to the hit position.
s32 func_0017C778(u32 col, u32 o) {
    const u32 k = W(o + 0x48);
    if ((k & 0x70000u) == 0) return 0;
    if ((k & 0x10000u) != 0) {
        if (func_0017C688(F(o + 0x70), col + 0x130, W(col + 0x154), o) == 0) return 0;
        if ((k & 0x60000u) == 0) return 1;
    }
    guest::GuestFrame f(0xE0);
    const u32 end = f.Address(0), tmp = f.Address(0x10), hit = f.Address(0x20), normal = f.Address(0x30), m = f.Address(0x40);
    const u32 pt = W(col + 0x154);
    sceVu0AddVector(end, pt, pt + 0x20);
    if ((k & 0x20000u) != 0) {
        sceVu0CopyVector(tmp, o);
        if (func_0017C708(F(o + 0x70), W(col + 0x154), end, o) == 0) return 0;
        if ((k & 0x40000u) == 0) return 1;
    } else if ((k & 0x40000u) == 0) {
        return 0;
    }
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, o + 0x10);
    sceVu0TransMatrix(m, m, o);
    if (func_00146808(UnitModels(), hit, W(col + 0x154), end, m, normal, GRef<u8>(o + 0x55)) == 0) return 0;
    sceVu0CopyVector(W(col + 0x154), hit);
    return 1;
}

// EE: 0x0017C8D0
// Moving point (collider +0x154) against the terrain; 1 on a hit.
s32 func_0017C8D0(u32 col, u32 hit, u32 attr, u32 normal) {
    guest::GuestFrame f(0x10);
    const u32 pt = W(col + 0x154);
    sceVu0AddVector(f.Address(), pt, pt + 0x20);
    return func_0013D698(Terrain(), hit, attr, pt, f.Address(), normal);
}

// EE: 0x0017C940
// Segment collider: start +0x110, step +0x120 (copies), owner +0x150, box around the step.
void func_0017C940(u32 col, u32 a, u32 start, u32 step, u32 owner) {
    sceVu0CopyVector(col + 0x100, a);
    sceVu0CopyVector(col + 0x110, start);
    sceVu0CopyVector(col + 0x120, step);
    GRef<u32>(col + 0x150) = owner;
    func_0017C598(0.0f, col + 0x130, step);
}

// EE: 0x0017C9B8
// Segment collider against unit o (index i, skipped when it is the owner): box, path radius, mesh;
// writes the hit point (w = 1) or o's position.
s32 func_0017C9B8(u32 col, u32 o, u32 out, u32 normal, s32 i) {
    const u32 k = W(o + 0x48);
    if (GRef<s32>(col + 0x150) == i || (~k & 0x10000000u) != 0) return 0;
    if ((~k & 0x200000u) != 0) return 0;
    if ((k & 0x70000u) == 0) return 0;
    const u32 start = col + 0x110;
    if ((k & 0x10000u) != 0) {
        if (func_0017C688(F(o + 0x70), col + 0x130, start, o) == 0) return 0;
        if ((k & 0x60000u) == 0) {
            CopyQ(out, o);
            return 1;
        }
    }
    guest::GuestFrame f(0x60);
    const u32 end = f.Address(0), hit = f.Address(0x10), m = f.Address(0x20);
    sceVu0AddVector(end, start, col + 0x120);
    if ((k & 0x20000u) != 0) {
        if (func_0017C708(F(o + 0x70), start, end, o) == 0) return 0;
        if ((k & 0x40000u) == 0) {
            CopyQ(out, o);
            return 1;
        }
    } else if ((k & 0x40000u) == 0) {
        return 1;
    }
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, o + 0x10);
    sceVu0TransMatrix(m, m, o);
    if (func_00146808(UnitModels(), hit, start, end, m, normal, GRef<u8>(o + 0x55)) == 0) return 0;
    sceVu0CopyVector(out, hit);
    GRef<f32>(out + 0xC) = 1.0f;
    return 1;
}

// EE: 0x0017CB70
// Segment collider against the terrain; 1 on a hit.
s32 func_0017CB70(u32 col, u32 hit, u32 normal, u32 attr) {
    guest::GuestFrame f(0x10);
    sceVu0AddVector(f.Address(), col + 0x110, col + 0x120);
    return func_0013D698(Terrain(), hit, attr, col + 0x110, f.Address(), normal);
}

}  // namespace ac04::game
