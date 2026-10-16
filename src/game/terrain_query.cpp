// Terrain height queries (docs/subsystems/weapons.md "Terrain queries"). The height map: map+8 is
// the 256-entry cell slot table (bytes), map+0xC the sample blocks (0x400 bytes per slot: 32
// rows of 32 byte samples, two samples per sub-cell). World cells are 0x800 units; cell index =
// coordinate / 0x800 + 0x80. Height y = sample * -80.0 (y grows downwards).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x0013D590
// Sample block slot and byte offset of world cell (x, z): negative coordinates keep their low 4
// bits, coordinates >= 0x100 are folded into 0xF0..0xFF.
void func_0013D590(u32 out_cell, u32 out_offset, s32 x, s32 z) {
    s32 cx = x > -1 ? x : (x & 0xF);
    s32 cz = z > -1 ? z : (z & 0xF);
    if (cx >= 0x100) cx = (cx & 0xF) + 0xF0;
    if (cz >= 0x100) cz = (cz & 0xF) + 0xF0;
    GRef<s32>(out_cell) = (cx >> 4) + ((cz >> 4) << 4);
    GRef<s32>(out_offset) = (cx & 0xF) * 2 + (cz & 0xF) * 64;
}

namespace {

// Cell coordinate of a world coordinate (as the compiler rounds it: (v - 1) >> 11 for negatives).
s32 CellOf(s32 v) {
    if (v >= 0) return v >> 11;
    const s32 a = static_cast<s32>(static_cast<u32>(v) - 0x800u);
    const s32 b = a > -1 ? a : static_cast<s32>(static_cast<u32>(v) - 1u);
    return b >> 11;
}

}  // namespace

// EE: 0x0013E338
// Ground height at (pos.x, pos.z): bilinear on one of the two triangles of the sub-cell, from the
// four neighbouring cells' samples. A/B/C/D are the corner samples the original picks per case.
// fpucheck: 4 adds (D + dD, A + dB, g - h, h + ...)
f32 func_0013E338(u32 map, u32 pos) {
    const s32 iz = fpu::FloatToInt(GRef<f32>(pos + 8));
    const s32 ix = fpu::FloatToInt(GRef<f32>(pos + 0));
    const s32 fz = iz & 0x7FF;
    const s32 fx = ix & 0x7FF;
    const s32 cx = CellOf(ix) + 0x80;
    const s32 cz = CellOf(iz) + 0x80;
    guest::GuestFrame frame(0x20);
    const u32 sp = frame.Address();
    func_0013D590(sp + 0x00, sp + 0x04, cx, cz);
    func_0013D590(sp + 0x08, sp + 0x0C, cx + 1, cz);
    func_0013D590(sp + 0x10, sp + 0x14, cx, cz + 1);
    func_0013D590(sp + 0x18, sp + 0x1C, cx + 1, cz + 1);
    // Sample of corner k (0 = (cx, cz), 1 = (cx+1, cz), 2 = (cx, cz+1), 3 = (cx+1, cz+1)) at byte d.
    auto sample = [&](u32 k, u32 d) -> s32 {
        const u32 slot = GRef<u8>(GRef<u32>(map + 8) + GRef<u32>(sp + 8 * k));
        return GRef<u8>(GRef<u32>(map + 0xC) + (slot << 10) + GRef<u32>(sp + 8 * k + 4) + d);
    };
    constexpr f32 k = 0.0009765625f;
    f32 u, v;  // f5, f6
    s32 a = 0, b = 0, c = 0, d = 0;
    enum { kFromB, kFromD } path;
    if (fx < 0x400) {
        u = fpu::IntToFloat(fx) * k;
        if (fz < 0x400) {
            v = fpu::IntToFloat(fz) * k;
            if (fz < fx) {
                a = sample(0, 0); b = sample(0, 1); c = sample(0, 0x21); path = kFromB;
            } else {
                c = sample(0, 0x21); d = sample(0, 0x20); a = sample(0, 0); path = kFromD;
            }
        } else {
            v = fpu::IntToFloat(0x800 - fz) * k;
            if (0x800 - fx < fz) {
                b = sample(2, 1); a = sample(2, 0); c = sample(0, 0x21); path = kFromB;
            } else {
                c = sample(0, 0x21); d = sample(2, 0x20); a = sample(2, 0); path = kFromD;
            }
        }
    } else if (fz < 0x400) {
        v = fpu::IntToFloat(fz) * k;
        u = fpu::IntToFloat(0x800 - fx) * k;
        if (fz < 0x800 - fx) {
            b = sample(0, 1); c = sample(0, 0x21); a = sample(1, 0); path = kFromB;
        } else {
            c = sample(0, 0x21); d = sample(1, 0x20); a = sample(1, 0); path = kFromD;
        }
    } else {
        v = fpu::IntToFloat(0x800 - fz) * k;
        u = fpu::IntToFloat(0x800 - fx) * k;
        if (fz < fx) {
            d = sample(1, 0x20); c = sample(0, 0x21); a = sample(3, 0); path = kFromD;
        } else {
            a = sample(3, 0); b = sample(2, 1); c = sample(0, 0x21); path = kFromB;
        }
    }
    if (path == kFromB) d = c + (a - b);
    else b = a + (c - d);
    const f32 dd = fpu::IntToFloat(c - d) * u;
    const f32 db = fpu::IntToFloat(b - a) * u;
    const f32 g = fpu::Add(fpu::IntToFloat(d), dd);
    const f32 h = fpu::Add(fpu::IntToFloat(a), db);
    const f32 t = fpu::Sub(g, h) * v;
    return fpu::Add(h, t) * -80.0f;
}

// ---- Unit model sets ---------------------------------------------------------------------------

// EE: 0x00146710
// Bounding radius of unit i's model: players from the aircraft model set (*0x004106A4 +8, 10.0
// when it has none), others from model set `set` (variant U+0x5A).
f32 func_00146710(u32 set, s32 unit) {
    const u32 u = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(unit));
    if (unit < 2) {
        const u32 m = GRef<u32>(GRef<u32>(GRef<u32>(0x004106A4) + 8) + 4 * static_cast<u32>(unit));
        if (m == 0) return 10.0f;
        return GRef<f32>(m + 0x40);
    }
    const u32 rec = GRef<u32>(set) + static_cast<u32>(unit) * 0x18;
    return GRef<f32>(GRef<u32>(GRef<u32>(rec + 8) + GRef<u8>(u + 0x5A) * 0x20u) + 0x40);
}

// EE: 0x00146788
// Ground offset of unit i's model (variant U+0x5A).
f32 func_00146788(u32 set, s32 unit) {
    const u32 u = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(unit));
    const u32 rec = GRef<u32>(set) + static_cast<u32>(unit) * 0x18;
    return GRef<f32>(GRef<u32>(GRef<u32>(rec + 8) + GRef<u8>(u + 0x5A) * 0x20u) + 0x44);
}

// EE: 0x00149E28
void func_00149E28(u32 set, s32 p, u32 data) {
    GRef<u32>(set + 0x78 + 4 * static_cast<u32>(p)) = data;
}

// EE: 0x00149E90
// Bit 0 of the aircraft model set flags: set for n == 0, cleared otherwise.
void func_00149E90(u32 set, s64 n) {
    if (n == 0) GRef<u32>(set + 0x40) |= 1;
    else GRef<u32>(set + 0x40) &= 0xFFFFFFFEu;
}

}  // namespace ac04::game
