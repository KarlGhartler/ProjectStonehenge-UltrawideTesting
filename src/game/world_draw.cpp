// Per-view world drawing helpers (block A of S5): clip matrices, the directional light, terrain cell
// lookups and the VU0 sphere test. docs/subsystems/rendering.md "Terrain", "Lights", "VU0
// visibility helpers".
#include "game/game.h"

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/main_context.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00136990
// World-to-clip matrix of camera `cam` with the near plane pushed by `near_off` (as a z offset along
// the view direction) and far distance `far`: m = Clip * inverse(R(cam angles) with translation cam
// + R * (0, 0, z)), z = d * near_off / h.
// fpucheck: 1 add (far - z)
void func_00136990(f32 near_off, f32 far, u32 m, u32 cam) {
    guest::GuestFrame frame(0x50);
    const u32 v = frame.Address(), clip = frame.Address(0x10);
    ee::memset(v, 0, 0x10);
    const f32 d = GRef<f32>(cam + 0x168), h = GRef<f32>(cam + 0x164);
    GRef<f32>(v + 0xC) = 1.0f;
    const f32 z = d * near_off / h;
    GRef<f32>(v + 8) = z;
    BuildClipMatrix(clip, GRef<f32>(cam + 0x160), h, d, GRef<f32>(cam + 0x16C), fpu::Add(-z, far));
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, cam + 0x10);
    sceVu0ApplyMatrix(v, m, v);
    sceVu0AddVector(v, v, cam);
    sceVu0TransMatrix(m, m, v);
    sceVu0InversMatrix(m, m);
    sceVu0MulMatrix(m, clip, m);
}

// EE: 0x00136B68
// Directional light n of light set ls from angles (rx, ry): the light direction R * (0, 0, 1)
// normalised at +n * 0x10, the light matrix column of R * (0, 0, -1) at +0x50 / +0x60 / +0x70 /
// +0x80 + n * 4, and the colour bytes of `rgba` / 128 at +0x90 + n * 0x10.
void func_00136B68(f32 rx, f32 ry, u32 ls, u32 rgba, s32 n) {
    guest::GuestFrame frame(0x80);
    const u32 m = frame.Address(), d2 = frame.Address(0x40), back = frame.Address(0x50), col = frame.Address(0x60),
              fwd = frame.Address(0x70);
    const u32 nn = static_cast<u32>(n);
    ee::memset(back, 0, 0x10);
    GRef<f32>(back + 8) = -1.0f;
    ee::memset(fwd, 0, 0x10);
    GRef<f32>(fwd + 8) = 1.0f;
    SetUnitMatrixVu0(m);
    sceVu0RotMatrixX(m, m, rx);
    sceVu0RotMatrixY(m, m, ry);
    sceVu0ApplyMatrix(fwd, m, fwd);
    sceVu0Normalize(ls + nn * 0x10, fwd);
    sceVu0ApplyMatrix(d2, m, back);
    for (u32 b = 0; b < 4; ++b) GRef<f32>(col + 4 * b) = fpu::IntToFloat(static_cast<s32>((rgba >> (8 * b)) & 0xFF));
    sceVu0ScaleVector(col, col, 0.0078125f);
    GRef<f32>(ls + 0x50 + nn * 4) = GRef<f32>(d2 + 0);
    GRef<f32>(ls + 0x60 + nn * 4) = GRef<f32>(d2 + 4);
    GRef<f32>(ls + 0x70 + nn * 4) = GRef<f32>(d2 + 8);
    GRef<f32>(ls + 0x80 + nn * 4) = GRef<f32>(d2 + 0xC);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(ls + 0x90 + nn * 0x10 + o) = GRef<u32>(col + o);
}

// EE: 0x00137318
// Copies the light-set block (header +0xE0, matrix and colours +0x50..+0xDF, MSCAL +0xF0) twice to
// `out` (11 qwords each, the layout of WriteLightSetPacket without the packet); returns the end.
u32 func_00137318(u32 ls, u32 out) {
    static const u32 kOrder[] = {0xE0, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xF0};
    for (u32 copy = 0; copy < 2; ++copy, out += 0xB0)
        for (u32 q = 0; q < 11; ++q)
            for (u32 o = 0; o < 16; o += 4) GRef<u32>(out + q * 0x10 + o) = GRef<u32>(ls + kOrder[q] + o);
    return out;
}

// EE: 0x0013A538
// VU0 distance test of centre c against the camera position p with the distances in r: Q = |c - p|
// (xyz); out = vf0 * Q = (0, 0, 0, Q); the MAC flags of `vsubq.xyzw r - Q` decide: r.x - Q neither
// zero nor negative (bits 0x88 clear) -> 1, else r.w - Q neither zero nor negative (0x11 clear) -> 2,
// else 4.
s32 func_0013A538(u32 c, u32 r, u32 p, u32 out) {
    const f32 dx = vu0::Sub(GRef<f32>(c), GRef<f32>(p));
    const f32 dy = vu0::Sub(GRef<f32>(c + 4), GRef<f32>(p + 4));
    const f32 dz = vu0::Sub(GRef<f32>(c + 8), GRef<f32>(p + 8));
    const f32 s = vu0::Add(vu0::Add(vu0::Mul(dx, dx), vu0::Mul(dy, dy)), vu0::Mul(dz, dz));
    const f32 q = vu0::Sqrt(s);
    const f32 rx = vu0::Sub(GRef<f32>(r + 0), q), rw = vu0::Sub(GRef<f32>(r + 12), q);
    s32 result;
    if (!(rx <= 0.0f)) result = 1;
    else if (!(rw <= 0.0f)) result = 2;
    else result = 4;
    GRef<f32>(out + 0) = vu0::Mul(0.0f, q);
    GRef<f32>(out + 4) = vu0::Mul(0.0f, q);
    GRef<f32>(out + 8) = vu0::Mul(0.0f, q);
    GRef<f32>(out + 12) = vu0::Mul(1.0f, q);
    return result;
}

// EE: 0x0013A5C8
// Top point of terrain cell (x, z): ((x - 0x80) * 2048, sample * -80, (z - 0x80) * 2048, 1.0), the
// sample from the cell's block (folded like func_0013D590).
void func_0013A5C8(u32 map, u32 out, s32 x, s32 z) {
    s32 cx = x > -1 ? x : (x & 0xF);
    s32 cz = z > -1 ? z : (z & 0xF);
    if (cx >= 0x100) cx = (cx & 0xF) + 0xF0;
    if (cz >= 0x100) cz = (cz & 0xF) + 0xF0;
    const s32 bz = (cz > -1 ? cz : cz + 0xF) >> 4, bx = (cx > -1 ? cx : cx + 0xF) >> 4;
    GRef<f32>(out) = fpu::IntToFloat((x - 0x80) * 0x800);
    const u32 slot = GRef<u8>(GRef<u32>(map + 8) + static_cast<u32>(bx + bz * 16));
    const u8 h = GRef<u8>(GRef<u32>(map + 0xC) + slot * 0x400 + static_cast<u32>((cx & 0xF) * 2 + (cz & 0xF) * 0x40));
    GRef<f32>(out + 0xC) = 1.0f;
    GRef<f32>(out + 8) = fpu::IntToFloat((z - 0x80) * 0x800);
    GRef<f32>(out + 4) = fpu::IntToFloat(static_cast<s32>(h) * -0x50);
}

// EE: 0x00138EE8
// Block index and 16 x 16 cell offset of terrain cell (x, z) (folded like func_0013D590).
void func_00138EE8(u32 out_block, u32 out_offset, s32 x, s32 z) {
    if (x < 0) x &= 0xF;
    if (z < 0) z &= 0xF;
    if (x > 0xFF) x = (x & 0xF) + 0xF0;
    if (z > 0xFF) z = (z & 0xF) + 0xF0;
    const s32 bz = (z > -1 ? z : z + 0xF) >> 4, bx = (x > -1 ? x : x + 0xF) >> 4;
    GRef<s32>(out_block) = bx + bz * 16;
    GRef<s32>(out_offset) = (x - bx * 16) + (z - bz * 16) * 16;
}

// EE: 0x00138F70
// Same as func_00138EE8 for the attribute / texture grid (offset in 16-bit entries of 32 per row).
void func_00138F70(u32 out_block, u32 out_offset, s32 x, s32 z) {
    if (x < 0) x &= 0xF;
    if (z < 0) z &= 0xF;
    if (x > 0xFF) x = (x & 0xF) + 0xF0;
    if (z > 0xFF) z = (z & 0xF) + 0xF0;
    const s32 bz = (z > -1 ? z : z + 0xF) >> 4, bx = (x > -1 ? x : x + 0xF) >> 4;
    GRef<s32>(out_block) = bx + bz * 16;
    GRef<s32>(out_offset) = (x - bx * 16) * 2 + (z - bz * 16) * 0x40;
}

// EE: 0x00139000
// World coordinate -> terrain cell (2048 units, 0x80 = origin; 0x7F base for negatives).
s32 func_00139000(f32 v) {
    const s32 i = fpu::FloatToInt(v);
    const s32 c = (i > -1 ? i : i + 0x7FF) >> 11;
    return v < 0.0f ? c + 0x7F : c + 0x80;
}

// EE: 0x0013A110
// Per-view terrain set-up: camera cell, LOD ring radii, texture slot table reset, sun light colour
// by altitude (two keyed colours of the entry's light record), the terrain light set, the fog / LOD
// bias powers of two and the fog distances (also into the effect block).
// fpucheck: 4 ring adds; t: 2 sub; 1 - t: 1 sub; powf args: 4 sub
void func_0013A110(u32 t, u32 cam) {
    GRef<u32>(t + 0x70) = 0;
    const u32 light = GRef<u32>(GRef<u32>(t) + GRef<u32>(t + 4) * 100 + 0x54);
    GRef<u16>(t + 0x11C) = static_cast<u16>(func_00139000(GRef<f32>(cam)));
    const s32 cz = func_00139000(GRef<f32>(cam + 8));
    const f32 f = GRef<f32>(cam + 0x168);
    GRef<u32>(t + 0x120) = 0x40;
    GRef<f32>(t + 0x60) = fpu::Add(GRef<f32>(t + 0x50), f * 3.0f);
    GRef<f32>(t + 0x64) = fpu::Add(GRef<f32>(t + 0x54), f * 4.0f);
    GRef<f32>(t + 0x68) = fpu::Add(GRef<f32>(t + 0x58), f * 5.0f);
    GRef<f32>(t + 0x6C) = fpu::Add(GRef<f32>(t + 0x5C), f * 6.0f);
    const u32 rs = GRef<u32>(0x004104D4);
    GRef<u16>(t + 0x11E) = static_cast<u16>(cz);
    for (s32 k = 0x3FF; k >= 0; --k) GRef<u16>(GRef<u32>(t + 0x12C) + 2 * static_cast<u32>(k)) = 0x400;
    guest::GuestFrame frame(0x20);
    const u32 c1 = frame.Address(), c2 = frame.Address(0x10);
    const u32 w1 = GRef<u32>(light + 0x20), w2 = GRef<u32>(light + 0x24);
    for (u32 b = 0; b < 3; ++b) {
        GRef<f32>(c1 + 4 * b) = fpu::IntToFloat(static_cast<s32>((w1 >> (8 * b)) & 0xFF));
        GRef<f32>(c2 + 4 * b) = fpu::IntToFloat(static_cast<s32>((w2 >> (8 * b)) & 0xFF));
    }
    GRef<u32>(c1 + 0xC) = 0;
    GRef<u32>(c2 + 0xC) = 0;
    const f32 lo = GRef<f32>(rs + 0x10);
    f32 a = fpu::Div(fpu::Sub(-GRef<f32>(cam + 4), lo), fpu::Sub(GRef<f32>(rs + 0x14), lo));
    if (a < GRef<f32>(c2 + 0xC)) a = GRef<f32>(c2 + 0xC);
    if (1.0f < a) a = 1.0f;
    sceVu0ScaleVector(c1, c1, fpu::Sub(1.0f, a));
    sceVu0ScaleVector(c2, c2, a);
    sceVu0AddVector(c1, c1, c2);
    const u32 colour = fpu::FixUnsSfSi(GRef<f32>(c1)) | fpu::FixUnsSfSi(GRef<f32>(c1 + 4)) << 8 |
                       fpu::FixUnsSfSi(GRef<f32>(c1 + 8)) << 16;
    func_00136B68(GRef<f32>(light + 8), GRef<f32>(light + 0xC), GRef<u32>(0x00410688), colour, 0);
    func_00136B68(GRef<f32>(light + 0x10), GRef<f32>(light + 0x14), GRef<u32>(0x00410688), GRef<u32>(light + 0x28), 1);
    SetAmbient(GRef<u32>(0x00410688), GRef<u32>(light + 0x30));
    const s32 v = fpu::FloatToInt(GRef<f32>(cam + 0x168) * 0.015625f);
    const u16 near = static_cast<u16>(GRef<u16>(t + 0xEC) - v);
    GRef<u16>(t + 0xF2) = static_cast<u16>(GRef<u16>(t + 0xF0) - v);
    GRef<u16>(t + 0xEE) = near;
    s32 bias = static_cast<s16>(near);
    if (bias > 0x800) bias = static_cast<s16>(bias - 0x1000);
    const f32 b = fpu::IntToFloat(bias) * 0.0625f;
    for (s32 k = 0; k < 4; ++k) GRef<f32>(t + 0x470 + 4 * static_cast<u32>(k)) = ee::powf(2.0f, fpu::Sub(fpu::IntToFloat(k), b));
    GRef<f32>(t + 0xA8) = GRef<f32>(rs + 8);
    GRef<f32>(t + 0xA4) = GRef<f32>(rs + 0xC);
    const u32 fx = GRef<u32>(0x004106B4) + 0x60000;
    GRef<f32>(fx + 0x2498) = GRef<f32>(rs + 8);
    GRef<f32>(fx + 0x2494) = GRef<f32>(rs + 0xC);
}

// EE: 0x0013A6B8
// Terrain culling (docs/subsystems/rendering.md "Visible cells"): the 8 x 8 blocks of 8 x 8 cells
// around the camera are clip-tested at their corner columns (top point and 2048 below), then the
// cells of the visible blocks; each visible cell gets its LOD ring class, distance, block and
// offset. Lists in the scratchpad (class 0x70000000, offset 0x70001000, block 0x70002000, block x /
// z 0x70003000 / 0x70003080) and RAM (T+0x74 x, T+0x78 z, T+0x114 distance), then copied to RAM
// like the original fromSPR DMA (whole transfer lengths, stale bytes included).
// fpucheck: 1 sub per corner column (y - 2048); 3 sub per visible cell (s = C - 1024)
void func_0013A6B8(u32 t, u32 cam) {
    guest::GuestFrame frame(0x110);
    const u32 sp = frame.Address();
    const u32 m = sp, row = sp + 0x60, s = sp + 0x40, out = sp + 0x50;
    const u32 p1 = sp + 0x90, b1 = sp + 0xA0, tmp1 = sp + 0xB0, p2 = sp + 0xC0, b2 = sp + 0xD0, tmp2 = sp + 0xE0,
              blk = sp + 0xF0, idx = sp + 0xF4;
    auto clip = [&](u32 p, u32 b, u32 tmp, s32 x, s32 z) -> u32 {
        func_0013A5C8(t, p, x, z);
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(b + o) = GRef<u32>(p + o);
        GRef<f32>(b + 4) = fpu::Sub(GRef<f32>(b + 4), 2048.0f);
        return static_cast<u32>(func_00136818(p, b, tmp));
    };
    u32 xs = GRef<u32>(t + 0x74), zs = GRef<u32>(t + 0x78), dist = GRef<u32>(t + 0x114);
    u32 cls = 0x70000000, blks = 0x70002000, idxs = 0x70001000, bx = 0x70003000, bz = 0x70003080;
    // 1. Blocks.
    func_00136990(-16384.0f, GRef<f32>(t + 0xA4), m, cam);
    func_001367B0(m);
    for (s32 i = 0; i < 9; ++i)
        GRef<u32>(row + 4 * static_cast<u32>(i)) =
            clip(p1, b1, tmp1, i * 8 + GRef<s16>(t + 0x11C) - 0x20, GRef<s16>(t + 0x11E) - 0x20);
    GRef<u32>(t + 0x7C) = 0;
    for (s32 r = 0; r < 8; ++r) {
        u32 b = GRef<u32>(row);
        u32 n1 = clip(p1, b1, tmp1, GRef<s16>(t + 0x11C) - 0x20, (r + 1) * 8 + GRef<s16>(t + 0x11E) - 0x20);
        for (s32 j = 0; j < 8; ++j) {
            const u32 a = b;
            b = GRef<u32>(row + 4 * static_cast<u32>(j + 1));
            const u32 n0 = n1;
            GRef<u32>(row + 4 * static_cast<u32>(j)) = n0;
            n1 = clip(p1, b1, tmp1, (j + 1) * 8 + GRef<s16>(t + 0x11C) - 0x20, (r + 1) * 8 + GRef<s16>(t + 0x11E) - 0x20);
            if (((n0 & b) & (a & n1)) == 3) continue;
            GRef<u32>(t + 0x7C) += 1;
            GRef<u16>(bx) = static_cast<u16>(GRef<u16>(t + 0x11C) - 0x20 + j * 8);
            bx += 2;
            GRef<u16>(bz) = static_cast<u16>(GRef<u16>(t + 0x11E) - 0x20 + r * 8);
            bz += 2;
        }
        GRef<u32>(row + 0x20) = n1;
    }
    // 2. Cells of the visible blocks.
    func_00136990(-2048.0f, GRef<f32>(t + 0xA4), m, cam);
    func_001367B0(m);
    for (s32 k = 0; k < GRef<s32>(t + 0x7C); ++k) {
        const u32 kk = 2 * static_cast<u32>(k);
        const s32 z0 = GRef<s16>(0x70003080 + kk);
        for (s32 i = 0; i < 9; ++i)
            GRef<u32>(row + 4 * static_cast<u32>(i)) =
                clip(p2, b2, tmp2, static_cast<s16>(GRef<s16>(0x70003000 + kk) + i), z0);
        s32 z = static_cast<s16>(z0 + 1);
        for (s32 r = 0; r < 8; ++r) {
            const s32 x0 = GRef<s16>(0x70003000 + kk);
            u32 b = GRef<u32>(row);
            u32 n1 = clip(p2, b2, tmp2, x0, z);
            const s32 zc = z - 1;
            const s32 znext = z + 1;
            for (s32 j = 0; j < 8; ++j) {
                const s32 x = static_cast<s16>(x0 + 1 + j);
                const u32 a = b;
                b = GRef<u32>(row + 4 * static_cast<u32>(j + 1));
                const u32 n0 = n1;
                GRef<u32>(row + 4 * static_cast<u32>(j)) = n0;
                n1 = clip(p2, b2, tmp2, x, z);
                if (func_00104F10(p2, cam) < GRef<f32>(0x0040E504)) n1 = 1;
                if (((a & n1) & (n0 & b)) == 3) continue;
                const s32 cx = x - 1;
                GRef<f32>(s + 0) = fpu::Sub(GRef<f32>(p2 + 0), 1024.0f);
                GRef<f32>(s + 4) = fpu::Sub(GRef<f32>(p2 + 4), 1024.0f);
                GRef<f32>(s + 8) = fpu::Sub(GRef<f32>(p2 + 8), 1024.0f);
                GRef<u8>(cls++) = static_cast<u8>(func_0013A538(s, t + 0x60, cam, out));
                GRef<f32>(dist) = GRef<f32>(out + 0xC);
                func_00138EE8(blk, idx, cx, zc);
                dist += 4;
                GRef<u8>(blks++) = GRef<u8>(blk);
                GRef<u8>(idxs++) = GRef<u8>(idx);
                GRef<s16>(xs) = static_cast<s16>(cx);
                GRef<s16>(zs) = static_cast<s16>(zc);
                xs += 2;
                zs += 2;
                GRef<s32>(t + 0x70) += 1;
            }
            GRef<u32>(row + 0x20) = n1;
            z = static_cast<s16>(znext);
        }
    }
    // 3. fromSPR copies (channel 8 on the PS2).
    FlushCache(0);
    const u32 d_cls = GRef<u32>(t + 0x100), d_bz = GRef<u32>(t + 0x84), d_blk = GRef<u32>(t + 0x108),
              d_idx = GRef<u32>(t + 0x10C), d_bx = GRef<u32>(t + 0x80);
    auto copy = [](u32 dst, u32 src, u32 bytes) {
        for (u32 o = 0; o < bytes; o += 4) GRef<u32>(dst + o) = GRef<u32>(src + o);
    };
    copy(d_cls, 0x70000000, 0x1000);
    copy(d_blk, 0x70002000, 0x1000);
    copy(d_idx, 0x70001000, 0x1000);
    copy(d_bx, 0x70003000, 0x80);
    copy(d_bz, 0x70003080, 0x80);
}

// EE: 0x0013B2F8
// Terrain texture cache (docs/subsystems/rendering.md "Terrain texture cache"): assigns every
// visible cell a VRAM slot (near cells: the 8 x 8 ring around the camera cell; far cells: one slot
// per texture from 0x40 up) and queues the level uploads of the slots into buckets 0-31 (bucket =
// slot / 64). Words the original leaves unwritten keep the packet buffer contents.
// fpucheck: level mask 1-6 add/sub (one per test reached); 2 add per flip-distance test
void func_0013B2F8(u32 t, u32 cam, u32 ot) {
    guest::GuestFrame frame(0x50);
    const u32 mtx = frame.Address();  // sp+0x10 matrix; the vector (sp+0x50) follows it
    u32 p = 0;
    auto w = [&](u32 off, u32 v) { GRef<u32>(p + off) = v; };
    auto close = [&](s32 bucket) { EndPacket(ot, bucket, (p + 0xF) & 0xFFFFFFF0u); };
    // 1. TEXA.
    p = BeginPacket();
    w(4, 0); w(0, 0x10000002); w(8, 0); w(0xC, 0x50000002); p += 0x10;
    w(4, 0x10000000); w(8, 0xE); w(0, 0x8001); p += 0x10;
    w(8, 0x3B); w(0, 0); w(4, 0); p += 0x10;
    close(0);
    // 2. CLUT upload.
    p = BeginPacket();
    w(4, 0); w(0, 0x10000006); w(8, 0x11000000); w(0xC, 0x50000006); p += 0x10;
    w(0, 4); w(4, 0x10000000); w(8, 0xE); p += 0x10;
    w(0, 0); w(4, 0x00013F00); w(8, 0x50); p += 0x10;
    w(0, 0x10); w(8, 0x52); w(4, 0x10); p += 0x10;
    w(8, 0x51); w(0, 0); w(4, 0); p += 0x10;
    w(0, 0); w(8, 0x53); p += 0x10;
    w(0, 0x8040); w(4, 0x08000000); w(8, 0); p += 0x10;
    w(0, 0x30000040); w(4, GRef<u32>(t + 0x34)); w(8, 0); w(0xC, 0x50000040); p += 0x10;
    close(0);
    // 3. Bucket headers.
    for (s32 b = 0; b < 0x20; ++b) {
        p = BeginPacket();
        w(0, 0x10000000); w(4, 0); w(8, 0); w(0xC, 0x11000000); p += 0x10;
        if (b == 0) {
            w(4, 0); w(0, 0x10000008); w(8, 0); w(0xC, 0x50000008); p += 0x10;
            w(0, 0x8007); w(8, 0xE); w(4, 0x10000000); p += 0x10;
            w(0, 0x000101E8); w(8, 0x4D); w(4, 0x00FFFFFF); w(0xC, 0); p += 0x10;
            w(8, 0x4F); w(0, 0x01000000); w(4, 1); w(0xC, 0); p += 0x10;
            w(8, 0x19); w(0, 0); w(4, 0); p += 0x10;
            w(8, 0x41); w(0, 0x00400000); w(4, 0x00400000); p += 0x10;
            w(0, 0x30000); w(8, 0x48); p += 0x10;
            w(0, 5); w(8, 9); w(4, 0); p += 0x10;
            w(8, 0x15); w(0, 0); w(4, 0); p += 0x10;
        }
        close(b);
    }
    // 4. Direction vector (never read; the calls are kept).
    const u32 v = mtx + 0x40;
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(v + o) = 0;  // memset
    GRef<f32>(v + 8) = GRef<f32>(cam + 0x168) * 0.000244140625f;
    sceVu0UnitMatrix(mtx);
    func_00102D28(mtx, mtx, cam + 0x10);
    sceVu0ApplyMatrix(v, mtx, v);
    // 5. Cells.
    const s32 ncx = GRef<s16>(t + 0x11C), ncz = GRef<s16>(t + 0x11E);
    s32 count[4] = {0, 0, 0, 0};  // per-level upload counters (stack; never read)
    u32 xs = GRef<u32>(t + 0x74), zs = GRef<u32>(t + 0x78), dist = GRef<u32>(t + 0x114);
    for (s32 i = 0; i < GRef<s32>(t + 0x70); ++i) {
        const u32 blk = GRef<u8>(0x70002000 + static_cast<u32>(i));
        const u32 idx = GRef<u8>(0x70001000 + static_cast<u32>(i));
        const u32 attr = GRef<u16>(GRef<u32>(t + 0x20) +
                                   2 * ((static_cast<u32>(GRef<u8>(GRef<u32>(t + 0x1C) + blk)) << 8) + idx));
        const u32 tex = attr & 0x3FF, flags = attr & 0xFC00;
        const f32 d = GRef<f32>(dist);
        u32 m;
        if (d < fpu::Sub(GRef<f32>(t + 0x470), 1296.0f)) {
            m = 1;
        } else if (d < fpu::Sub(GRef<f32>(t + 0x474), 1296.0f)) {
            m = 3;
        } else if (d < fpu::Add(GRef<f32>(t + 0x474), 6144.0f)) {
            m = 7;
        } else if (d < fpu::Sub(GRef<f32>(t + 0x478), 1024.0f)) {
            m = 6;
        } else if (d < fpu::Add(GRef<f32>(t + 0x478), 8192.0f)) {
            m = 0xE;
        } else if (d < fpu::Add(GRef<f32>(t + 0x47C), 8192.0f)) {
            m = 0xC;
        } else {
            m = 8;
        }
        const u32 oa = GRef<u32>(t + 0x124) + 2 * static_cast<u32>(i);
        const u32 ot_ = GRef<u32>(t + 0x128) + 2 * static_cast<u32>(i);
        const s32 x = GRef<s16>(xs), z = GRef<s16>(zs);
        if (x >= ncx - 4 && x < ncx + 4 && z >= ncz - 4 && z < ncz + 4) {
            GRef<u16>(oa) = static_cast<u16>(flags | static_cast<u32>((x - (ncx - 4)) + (z - (ncz - 4)) * 8));
            GRef<u16>(ot_) = static_cast<u16>(tex);
        } else {
            const u32 slotp = GRef<u32>(t + 0x12C) + 2 * tex;
            const u32 slot = GRef<u16>(slotp);
            if (slot == 0x400) {
                GRef<u16>(oa) = static_cast<u16>(flags | GRef<u16>(t + 0x120));
                GRef<u16>(ot_) = static_cast<u16>(tex);
                GRef<u16>(slotp) = GRef<u16>(t + 0x120);
                GRef<u8>(GRef<u32>(t + 0x118) + tex) = static_cast<u8>(m);
                GRef<u32>(t + 0x120) += 1;
            } else {
                const u32 done = GRef<u8>(GRef<u32>(t + 0x118) + tex);
                GRef<u16>(oa) = static_cast<u16>(flags | slot);
                GRef<u16>(ot_) = static_cast<u16>(tex);
                m &= ~done;
            }
        }
        xs += 2;
        zs += 2;
        p = BeginPacket();
        const u32 s = GRef<u16>(oa) & 0x3FF;
        const u32 sx = s % 8, sy = (s % 64) / 8;
        const u32 flip = ((attr & 0x400) != 0 ? 2u : 0u) | ((attr & 0x800) != 0 ? 1u : 0u);
        u32 n = 0x40, q = 0x100, dbw = 8, base = 0x2900, step = 0x400;
        u32 tmpl = t + 0x2B0;
        for (u32 k = 0; k < 4; ++k) {
            if ((m & (1u << k)) != 0) {
                count[k] += 1;
                const u32 src = GRef<u32>(GRef<u32>(t + 0x24 + 4 * k) + 4 * tex);
                bool flipped = false;
                if (s < 0x40 && flip != 0) {
                    const f32 c = GRef<f32>(cam + 0x168);
                    flipped = !(fpu::Add(fpu::Add(c, c), 4096.0f) <= d);
                }
                if (!flipped) {
                    GRef<u32>(tmpl + 0x40) = 0;
                    GRef<u32>(tmpl + 0x48) = 0x51;
                    GRef<u32>(tmpl + 0x44) = (sx * n) | ((sy * n) << 16);
                    for (u32 o = 0; o < 0x70; o += 4) GRef<u32>(p + o) = GRef<u32>(tmpl + o);
                    p += 0x70;
                    w(0, 0x30000000 | q); w(4, src); w(8, 0); w(0xC, 0x50000000 | q); p += 0x10;
                } else {
                    // Mirror the level into 0x3E00 (PSMT8H), then copy it into the slot.
                    w(4, 0); w(0, 0x10000006); w(8, 0); w(0xC, 0x50000006); p += 0x10;
                    w(0, 4); w(4, 0x10000000); w(8, 0xE); p += 0x10;
                    w(0, 0); w(4, 0x1B013E00); w(8, 0x50); p += 0x10;
                    w(0, 0); w(4, 0); w(8, 0x51); p += 0x10;
                    w(0, n); w(8, 0x52); w(4, n); p += 0x10;
                    w(0, 0); w(8, 0x53); p += 0x10;
                    w(0, q); w(4, 0x08000000); w(8, 0); p += 0x10;
                    w(0, 0x30000000 | q); w(4, src); w(8, 0); w(0xC, 0x50000000 | q); p += 0x10;
                    w(0, 0x10000002); w(4, 0); w(8, 0); w(0xC, 0x50000002); p += 0x10;
                    w(0, 1); w(4, 0x10000000); w(8, 0xE); p += 0x10;
                    w(8, 0x3F); p += 0x10;
                    w(0, 0x10000004); w(4, 0); w(8, 0); w(0xC, 0x50000004); p += 0x10;
                    w(4, 0x64000000); w(0, 1); w(8, 0x00434370); p += 0x10;
                    w(0, 0x316); w(4, 0); w(8, 0x98007E00); w(0xC, 0xD); p += 0x10;
                    const u32 n1 = n - 1;
                    if (flip == 1) {
                        w(0, n1 << 20); w(8, 0); w(0xC, 0); p += 0x10;
                        w(0, n << 4); w(8, (n << 4) | (n << 20)); w(0xC, 0); p += 0x10;
                    } else if (flip == 2) {
                        w(0, n1 << 4); w(8, 0); w(0xC, 0); p += 0x10;
                        w(0, n << 20); w(8, (n << 4) | (n << 20)); w(0xC, 0); p += 0x10;
                    } else {
                        w(0, (n1 << 4) | (n1 << 20)); w(8, 0); w(0xC, 0); p += 0x10;
                        w(0, 0); w(8, (n << 4) | (n << 20)); w(0xC, 0); p += 0x10;
                    }
                    w(4, 0); w(0, 0x10000007); w(8, 0); w(0xC, 0x50000007); p += 0x10;
                    w(8, 0xE); w(0, 0x8006); w(4, 0x10000000); p += 0x10;
                    w(8, 0x3F); p += 0x10;
                    w(0, 0x1B013D00); w(8, 0x50); w(4, base | (dbw << 16) | 0x13000000); p += 0x10;
                    w(4, (sx * n) | ((sy * n) << 16)); w(0, 0); w(8, 0x51); p += 0x10;
                    GRef<u16>(oa) = static_cast<u16>(GRef<u16>(oa) & 0xF3FF);
                    w(8, 0x52); w(0, n); w(4, n); p += 0x10;
                    w(8, 0x53); w(0, 2); p += 0x10;
                    w(8, 0x3F); p += 0x10;
                }
            }
            n /= 2;
            dbw /= 2;
            base += step;
            q /= 4;
            step /= 4;
            tmpl += 0x70;
        }
        EndPacket(ot, static_cast<s32>(s >> 6), (p + 0xF) & 0xFFFFFFF0u);
        dist += 4;
    }
    (void)count;
    // 6. Bucket trailers.
    for (s32 b = 0; b < 0x20; ++b) {
        p = BeginPacket();
        w(0, 0x10000002); w(4, 0); w(8, 0); w(0xC, 0x50000002); p += 0x10;
        w(0, 0x8001); w(4, 0x10000000); w(8, 0xE); p += 0x10;
        w(8, 0x3F); p += 0x10;
        w(0, 0x10000000); w(4, 0); w(8, 0); w(0xC, 0x11000000); p += 0x10;
        close(b);
    }
}

namespace {

// Cell sample lookup shared by the strip builders: func_00138F70 into the stack pair (b, off),
// then H = height block + off, C = colour-index block + off. Returns off.
s32 CellSample(u32 t, u32 sp, s32 x, s32 z, u32& h, u32& c) {
    func_00138F70(sp, sp + 4, x, z);
    const u32 b = GRef<u32>(sp);
    const s32 off = GRef<s32>(sp + 4);
    h = GRef<u32>(t + 0xC) + (static_cast<u32>(GRef<u8>(GRef<u32>(t + 8) + b)) << 10) + static_cast<u32>(off);
    c = GRef<u32>(t + 0x14) + (static_cast<u32>(GRef<u8>(GRef<u32>(t + 0x10) + b)) << 10) + static_cast<u32>(off);
    return off;
}

// Vertex record rec of the holder gets height H[o] (byte +9) and colour palette[C[o]] (word +4).
void SetCellVertex(u32 t, u32 rec, u32 h, u32 c, u32 o) {
    GRef<u8>(t + rec + 9) = GRef<u8>(h + o);
    GRef<u32>(t + rec + 4) = GRef<u32>(GRef<u32>(t + 0x18) + 4 * static_cast<u32>(GRef<u8>(c + o)));
}

// Offsets on the last column / row of a 16 x 16 block (the asm's signed tests, off >= 0 here).
bool LastColumn(s32 off) { return (off / 32) * 32 == off - 0x1E; }
bool LastRow(s32 off) { return off / 64 == 0xF; }

void CopyQwords(u32& p, u32 src, u32 n) {
    for (u32 o = 0; o < n * 16; o += 4) GRef<u32>(p + o) = GRef<u32>(src + o);
    p += n * 16;
}

}  // namespace

// EE: 0x0013C1D0
// Cell quad (class bit 2): 4 records T+0x150..0x180 = half-cell points (0,0), (2,0), (2,2), (0,2)
// (docs/subsystems/rendering.md "Cell strips"); blk and idx are not read (their stack slots take the
// lookup results).
void func_0013C1D0(u32 t, u32 ot, u32 blk, u32 idx, s32 x, s32 z, u32 a, u32 cls) {
    (void)blk;
    (void)idx;
    guest::GuestFrame frame(0x10);
    const u32 sp = frame.Address();
    a &= 0xFFFF;
    cls &= 0xFF;
    const u32 s = a & 0x3FF, fl = a >> 10;
    const u32 v = (s >> 2) & 0xE, u = (s & 7) << 1;
    u32 p = BeginPacket();
    if ((fl & 1) != 0) {
        GRef<u8>(t + 0x170) = static_cast<u8>(u);
        GRef<u8>(t + 0x180) = static_cast<u8>(u + 2);
        GRef<u8>(t + 0x150) = static_cast<u8>(u + 2);
        GRef<u8>(t + 0x160) = static_cast<u8>(u);
    } else {
        GRef<u8>(t + 0x180) = static_cast<u8>(u);
        GRef<u8>(t + 0x170) = static_cast<u8>(u + 2);
        GRef<u8>(t + 0x150) = static_cast<u8>(u);
        GRef<u8>(t + 0x160) = static_cast<u8>(u + 2);
    }
    if ((fl & 2) != 0) {
        GRef<u8>(t + 0x181) = static_cast<u8>(v);
        GRef<u8>(t + 0x161) = static_cast<u8>(v + 2);
        GRef<u8>(t + 0x151) = static_cast<u8>(v + 2);
        GRef<u8>(t + 0x171) = static_cast<u8>(v);
    } else {
        GRef<u8>(t + 0x151) = static_cast<u8>(v);
        GRef<u8>(t + 0x181) = static_cast<u8>(v + 2);
        GRef<u8>(t + 0x161) = static_cast<u8>(v);
        GRef<u8>(t + 0x171) = static_cast<u8>(v + 2);
    }
    u32 h, c, h2, c2;
    const s32 off = CellSample(t, sp, x, z, h, c);
    SetCellVertex(t, 0x150, h, c, 0);
    if (LastColumn(off)) {
        if (LastRow(off)) {
            CellSample(t, sp, x, z + 1, h2, c2);
            SetCellVertex(t, 0x180, h2, c2, 0);
        } else {
            SetCellVertex(t, 0x180, h, c, 0x40);
        }
        CellSample(t, sp, x + 1, z + 1, h2, c2);
        SetCellVertex(t, 0x170, h2, c2, 0);
        CellSample(t, sp, x + 1, z, h2, c2);
        SetCellVertex(t, 0x160, h2, c2, 0);
    } else {
        SetCellVertex(t, 0x160, h, c, 2);
        if (LastRow(off)) {
            CellSample(t, sp, x, z + 1, h2, c2);
            SetCellVertex(t, 0x180, h2, c2, 0);
            CellSample(t, sp, x + 1, z + 1, h2, c2);
            SetCellVertex(t, 0x170, h2, c2, 0);
        } else {
            SetCellVertex(t, 0x180, h, c, 0x40);
            SetCellVertex(t, 0x170, h, c, 0x42);
        }
    }
    CopyQwords(p, t + 0x260, 3);
    GRef<u32>(t + 0x13C) = (cls << 8) | 4;
    CopyQwords(p, t + 0x130, 1);
    CopyQwords(p, t + 0x290, 2);
    CopyQwords(p, t + 0x140, 6);
    EndPacket(ot, static_cast<s32>(s >> 6), (p + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x0013C740
// Cell fan (class without bit 2): 10 records T+0x1B0..0x240 = centre (1,1), then (0,0), (1,0),
// (2,0), (2,1), (2,2), (1,2), (0,2), (0,1), (0,0); blk and idx are not read.
void func_0013C740(u32 t, u32 ot, u32 blk, u32 idx, s32 x, s32 z, u32 a, u32 cls) {
    (void)blk;
    (void)idx;
    guest::GuestFrame frame(0x10);
    const u32 sp = frame.Address();
    a &= 0xFFFF;
    cls &= 0xFF;
    const u32 s = a & 0x3FF, fl = a >> 10;
    const u32 v = (s >> 2) & 0xE, u = (s & 7) << 1;
    u32 p = BeginPacket();
    auto b8 = [&](u32 off, u32 val) { GRef<u8>(t + off) = static_cast<u8>(val); };
    b8(0x210, u + 1);
    b8(0x1B0, u + 1);
    b8(0x1D0, u + 1);
    if ((fl & 1) != 0) {
        b8(0x200, u); b8(0x240, u + 2); b8(0x1C0, u + 2); b8(0x1E0, u);
        b8(0x1F0, u); b8(0x220, u + 2); b8(0x230, u + 2);
    } else {
        b8(0x240, u); b8(0x200, u + 2); b8(0x1C0, u); b8(0x1E0, u + 2);
        b8(0x1F0, u + 2); b8(0x220, u); b8(0x230, u);
    }
    b8(0x231, v + 1);
    b8(0x1B1, v + 1);
    b8(0x1F1, v + 1);
    if ((fl & 2) != 0) {
        b8(0x221, v); b8(0x241, v + 2); b8(0x1C1, v + 2); b8(0x1D1, v + 2);
        b8(0x1E1, v + 2); b8(0x201, v); b8(0x211, v);
    } else {
        b8(0x241, v); b8(0x221, v + 2); b8(0x1C1, v); b8(0x1D1, v);
        b8(0x1E1, v); b8(0x201, v + 2); b8(0x211, v + 2);
    }
    u32 h, c, h2, c2;
    const s32 off = CellSample(t, sp, x, z, h, c);
    SetCellVertex(t, 0x1B0, h, c, 0x21);
    SetCellVertex(t, 0x1C0, h, c, 0);
    SetCellVertex(t, 0x1D0, h, c, 1);
    SetCellVertex(t, 0x230, h, c, 0x20);
    SetCellVertex(t, 0x240, h, c, 0);
    if (LastColumn(off)) {
        if (LastRow(off)) {
            CellSample(t, sp, x, z + 1, h2, c2);
            SetCellVertex(t, 0x210, h2, c2, 1);
            SetCellVertex(t, 0x220, h2, c2, 0);
        } else {
            SetCellVertex(t, 0x210, h, c, 0x41);
            SetCellVertex(t, 0x220, h, c, 0x40);
        }
        CellSample(t, sp, x + 1, z + 1, h2, c2);
        SetCellVertex(t, 0x200, h2, c2, 0);
        CellSample(t, sp, x + 1, z, h2, c2);
        SetCellVertex(t, 0x1E0, h2, c2, 0);
        SetCellVertex(t, 0x1F0, h2, c2, 0x20);
    } else {
        SetCellVertex(t, 0x1E0, h, c, 2);
        SetCellVertex(t, 0x1F0, h, c, 0x22);
        if (LastRow(off)) {
            CellSample(t, sp, x, z + 1, h2, c2);
            SetCellVertex(t, 0x210, h2, c2, 1);
            SetCellVertex(t, 0x220, h2, c2, 0);
            SetCellVertex(t, 0x200, h2, c2, 2);
        } else {
            SetCellVertex(t, 0x210, h, c, 0x41);
            SetCellVertex(t, 0x220, h, c, 0x40);
            SetCellVertex(t, 0x200, h, c, 0x42);
        }
    }
    CopyQwords(p, t + 0x260, 3);
    GRef<u32>(t + 0x13C) = (cls << 8) | 0xA;
    CopyQwords(p, t + 0x130, 1);
    CopyQwords(p, t + 0x290, 2);
    CopyQwords(p, t + 0x1A0, 12);
    EndPacket(ot, static_cast<s32>(s >> 6), (p + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x0013CDC0
// Terrain cell draw (docs/subsystems/rendering.md "Cell draw"): VU1 constants (program A input
// buffers), context-2 and context-1 GS state, then one strip packet per visible cell.
// fpucheck: 2 sub (far range, ring radii); 2 add per near-slot distance test
void func_0013CDC0(u32 t, u32 cam, u32 ot) {
    guest::GuestFrame frame(0x20);
    const u32 f = frame.Address(), r = f + 0x10;  // sp+0, sp+0x10
    u32 cls = 0x70000000;
    GRef<u32>(t + 0xA0) =
        static_cast<u32>(func_001100A0(cam + 0x10, GRef<f32>(t + 0x8C), GRef<f32>(cam + 4)));
    const f32 farz = GRef<f32>(t + 0xA4);
    const f32 range = fpu::Sub(farz, GRef<f32>(t + 0xA8));
    GRef<f32>(f) = farz;
    GRef<f32>(f + 4) = fpu::Div(255.0f, range);  // f+8, f+0xC: whatever the stack held
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(r + o) = GRef<u32>(t + 0x60 + o);
    sceVu0MulVector(r, r, r);
    GRef<f32>(r) = fpu::Div(1.0f, fpu::Sub(GRef<f32>(r + 8), GRef<f32>(r + 4)));
    u32 p = 0;
    auto w = [&](u32 off, u32 val) { GRef<u32>(p + off) = val; };
    // 1. VU1 constants, once per input buffer.
    p = BeginPacket();
    for (s32 n = 1; n >= 0; --n) {
        w(4, 0); w(0, 0x10000004); w(8, 0); w(0xC, 0x6C04C00F); p += 0x10;
        w(0, 2); w(8, 0xE); w(4, 0x10000000); p += 0x10;
        w(0, 0x65322900); w(4, 0x2007E006); w(8, 6); w(0xC, 0); p += 0x10;
        w(0, 0); w(8, 0x412); w(4, (GRef<u32>(t + 0xF4) & 1) != 0 ? 0x303EC000 : 0x301EC000); p += 0x10;
        w(0, 0); w(8, 0x412); w(4, 0x313EC000); p += 0x10;
        w(0, 0x10000001); w(4, 0); w(8, 0); w(0xC, 0x6C01801E); p += 0x10;
        CopyQwords(p, f, 1);
        w(4, 0); w(0, 0x10000002); w(8, 0); w(0xC, 0x6C02800D); p += 0x10;
        CopyQwords(p, r, 1);
        CopyQwords(p, cam, 1);
        w(0, 0x10000000); w(4, 0); w(8, 0x6E018016);
        w(0xC, (GRef<u32>(GRef<u32>(0x004104D4) + 0xC0) & 4) != 0 ? 1 : 0);
        p += 0x10;
        w(4, 0); w(0, 0x10000009); w(8, 0); w(0xC, 0x6C088000); p += 0x10;
        sceVu0CopyMatrix(p, cam + 0xE0);
        sceVu0CopyMatrix(p + 0x40, cam + 0x120);
        p += 0x80;
        w(0, 0); w(4, 0); w(8, 0); w(0xC, 0x14000000); p += 0x10;
    }
    EndPacket(ot, 0, (p + 0xF) & 0xFFFFFFF0u);
    // 2. Context 2.
    p = BeginPacket();
    w(4, 0); w(0, 0x1000000A); w(8, 0); w(0xC, 0x5000000A); p += 0x10;
    w(0, 0x8009); w(4, 0x10000000); w(8, 0xE); p += 0x10;
    const u32 mc = g_pMainContext().addr;
    CopyQwords(p, mc + 0x150, 1);
    GRef<u32>(p - 0x10 + 4) = 0xFF000000;
    CopyQwords(p, g_pMainContext().addr + 0x160, 1);
    GRef<u32>(p - 0x10 + 4) = 1;
    CopyQwords(p, g_pMainContext().addr + 0x170, 1);
    CopyQwords(p, cam + 0x1B0, 1);
    w(0, 0x50000); w(8, 0x48); p += 0x10;
    w(0, 0x44); w(8, 0x43); w(4, 0); w(0xC, 0); p += 0x10;
    w(0, 0xFF000FFF); w(8, 9); w(4, 0); p += 0x10;
    w(4, 6); w(0, 0x20012500); w(8, 7); w(0xC, 0); p += 0x10;
    w(4, 0); w(0, 0x60); w(8, 0x15); p += 0x10;
    EndPacket(ot, 0, (p + 0xF) & 0xFFFFFFF0u);
    // 3. Context 1.
    p = BeginPacket();
    w(4, 0); w(0, 0x10000007); w(8, 0x01000101); w(0xC, 0x50000007); p += 0x10;
    w(0, 0x8006); w(8, 0xE); w(4, 0x10000000); p += 0x10;
    w(8, 0x3D); w(0, GRef<u32>(t + 0xA0)); p += 0x10;
    w(0, (GRef<u32>(t + 0xF4) & 4) != 0 ? 0x36C : 0x32C); w(8, 0x14);
    w(4, static_cast<u32>(static_cast<s32>(GRef<s16>(t + 0xEE)))); p += 0x10;
    w(0, 0x44); w(8, 0x42); w(4, 0); w(0xC, 0); p += 0x10;
    w(0, 0x70000); w(8, 0x47); p += 0x10;
    w(8, 0x3B); w(0, 0); w(4, 0); p += 0x10;
    w(0, 0); w(8, 0x49); p += 0x10;
    EndPacket(ot, 0, (p + 0xF) & 0xFFFFFFF0u);
    // 4. Cells.
    u32 blks = 0x70002000, idxs = 0x70001000;
    u32 xs = GRef<u32>(t + 0x74), zs = GRef<u32>(t + 0x78), attrs = GRef<u32>(t + 0x124);
    for (s32 i = 0; i < GRef<s32>(t + 0x70); ++i) {
        const u32 s = GRef<u16>(attrs) & 0x3FF;
        bool nearClamp = false;
        if (s < 0x40) {
            const f32 c = GRef<f32>(cam + 0x168);
            const f32 lim = fpu::Add(fpu::Add(c, c), 4096.0f);
            nearClamp = GRef<f32>(GRef<u32>(t + 0x114) + 4 * static_cast<u32>(i)) < lim;
        }
        const u32 col = s & 7, row = (s & 0x3F) >> 3;
        if (nearClamp) {
            GRef<u32>(t + 0x2A0) = 5;
            GRef<u32>(t + 0x2A4) = 0;
            GRef<u32>(t + 0x2A8) = 8;
        } else {
            GRef<u32>(t + 0x2A0) = (col << 10) | (row << 30) | (((col << 6) | 0x3F) << 14) | 0xA;
            GRef<u32>(t + 0x2A4) = (row >> 2) | ((row << 8) | 0xFC);
            GRef<u32>(t + 0x2A8) = 8;
        }
        const s32 x = GRef<s16>(xs);
        GRef<f32>(t + 0x270) = fpu::IntToFloat(x - 0x80) * 2048.0f;
        const s32 z = GRef<s16>(zs);
        GRef<u32>(t + 0x288) = (col << 1) | (row << 9);
        GRef<u32>(t + 0x284) = static_cast<u32>((x - GRef<s16>(t + 0x11C) + 0x20) |
                                                ((z - GRef<s16>(t + 0x11E) + 0x20) << 8)) | 0x10000;
        GRef<f32>(t + 0x278) = fpu::IntToFloat(z - 0x80) * 2048.0f;
        const u32 c = GRef<u8>(cls);
        if ((c & 4) != 0)
            func_0013C1D0(t, ot, GRef<u8>(blks), GRef<u8>(idxs), x, z, GRef<u16>(attrs), c);
        else
            func_0013C740(t, ot, GRef<u8>(blks), GRef<u8>(idxs), x, z, GRef<u16>(attrs), c);
        ++cls;
        ++idxs;
        ++blks;
        xs += 2;
        zs += 2;
        attrs += 2;
    }
    // 5. Bucket 0x1D: CLAMP_1, PABE reset.
    p = BeginPacket();
    w(4, 0); w(0, 0x10000003); w(8, 0x11000000); w(0xC, 0x50000003); p += 0x10;
    w(0, 0x8002); w(8, 0xE); w(4, 0x10000000); p += 0x10;
    w(8, 8); w(0, 0); w(4, 0); p += 0x10;
    w(0, 0); w(8, 0x49); p += 0x10;
    EndPacket(ot, 0x1D, (p + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x00137930
// CPU triangle clipper (docs/subsystems/rendering.md "Sea draw"): buf holds 3 vertices of 4 qwords
// (+0 UV, +0x10 colour, +0x20 world position, +0x30 clip position). Returns 0 when the triangle is
// trivially rejected or clipped away (n not written); else the clipped polygon packed to stride
// 0x30 (UV, colour, position), *n = vertex count, and returns its address (buf or buf + 0x200).
// fpucheck: 2 sub per reject test reached; 2 sub per edge; 3 sub per intersection
u32 func_00137930(u32 buf, u32 n_out, u32 mc) {
    guest::GuestFrame frame(0xF0);
    const u32 sp = frame.Address();
    const u32 c0 = sp, c1 = sp + 0x10, c2 = sp + 0x20;
    const u32 prev = sp + 0x30, cur = sp + 0x70;  // 4 qwords each (P and B)
    const u32 xq0 = sp + 0xB0, xq1 = sp + 0xC0, xq2 = sp + 0xD0, xclip = sp + 0xE0;
    sceVu0ApplyMatrix(c0, mc, buf + 0x20);
    sceVu0ApplyMatrix(c1, mc, buf + 0x60);
    sceVu0ApplyMatrix(c2, mc, buf + 0xA0);
    sceVu0CopyVector(buf + 0x30, c0);
    sceVu0CopyVector(buf + 0x70, c1);
    sceVu0CopyVector(buf + 0xB0, c2);
    auto F = [](u32 a) { return GRef<f32>(a); };
    // Trivial reject: all three outside one plane (x - w, -x - w, y - w, -y - w, z - w, -z - w).
    for (u32 axis = 0; axis < 3; ++axis) {
        const u32 o = 4 * axis;
        if (fpu::Sub(F(c0 + o), F(c0 + 0xC)) > 0.0f && fpu::Sub(F(c1 + o), F(c1 + 0xC)) > 0.0f &&
            fpu::Sub(F(c2 + o), F(c2 + 0xC)) > 0.0f)
            return 0;
        if (fpu::Sub(-F(c0 + o), F(c0 + 0xC)) > 0.0f && fpu::Sub(-F(c1 + o), F(c1 + 0xC)) > 0.0f &&
            fpu::Sub(-F(c2 + o), F(c2 + 0xC)) > 0.0f)
            return 0;
    }
    auto copy4 = [](u32 dst, u32 src) {
        for (u32 o = 0; o < 0x40; o += 4) GRef<u32>(dst + o) = GRef<u32>(src + o);
    };
    s32 n = 3;
    u32 f = 0;
    for (s32 i = 0; i < 4; ++i) {
        f ^= 1;
        u32 src = buf + ((1 - f) << 9);
        u32 dst = buf + (f << 9);
        copy4(cur, src + static_cast<u32>(n) * 0x40 - 0x40);
        s32 count = n;
        const u32 axis = static_cast<u32>(i / 2);
        const f32 sg = fpu::IntToFloat((i - static_cast<s32>(axis) * 2) != 0 ? -1 : 1);
        for (s32 j = 0; j < n; ++j) {
            copy4(prev, cur);
            copy4(cur, src);
            src += 0x40;
            const u32 pc = prev + 0x30 + 4 * axis, bc = cur + 0x30 + 4 * axis;
            const f32 pw = F(prev + 0x3C), bw = F(cur + 0x3C);
            const bool pOut = fpu::Sub(sg * F(pc), pw) > 0.0f;
            const bool bOut = fpu::Sub(sg * F(bc), bw) > 0.0f;
            if (!pOut) {
                copy4(dst, prev);
                dst += 0x40;
                if (!bOut) continue;
                // Inside -> outside: X = P + (B - P) * t.
                const f32 db = fpu::Sub(F(bc), sg * bw);
                const f32 dp = fpu::Sub(F(pc), sg * pw);
                f32 t = fpu::Div(dp, fpu::Sub(db, dp));
                if (t < 0.0f) t = -t;  // via double: exact fabs, -0.0 kept
                sceVu0SubVector(xq0, cur, prev);
                sceVu0SubVector(xq1, cur + 0x10, prev + 0x10);
                sceVu0SubVector(xq2, cur + 0x20, prev + 0x20);
                sceVu0ScaleVector(xq0, xq0, t);
                sceVu0ScaleVector(xq1, xq1, t);
                sceVu0ScaleVector(xq2, xq2, t);
                sceVu0AddVector(xq0, xq0, prev);
                sceVu0AddVector(xq1, xq1, prev + 0x10);
                sceVu0AddVector(xq2, xq2, prev + 0x20);
                sceVu0ApplyMatrix(xclip, mc, xq2);
                copy4(dst, xq0);
                dst += 0x40;
                ++count;
            } else if (bOut) {
                --count;
            } else {
                // Outside -> inside: X = B + (P - B) * t.
                const f32 dp = fpu::Sub(F(pc), sg * pw);
                const f32 db = fpu::Sub(F(bc), sg * bw);
                f32 t = fpu::Div(db, fpu::Sub(dp, db));
                if (t < 0.0f) t = -t;
                sceVu0SubVector(xq0, prev, cur);
                sceVu0SubVector(xq1, prev + 0x10, cur + 0x10);
                sceVu0SubVector(xq2, prev + 0x20, cur + 0x20);
                sceVu0ScaleVector(xq0, xq0, t);
                sceVu0ScaleVector(xq1, xq1, t);
                sceVu0ScaleVector(xq2, xq2, t);
                sceVu0AddVector(xq0, xq0, cur);
                sceVu0AddVector(xq1, xq1, cur + 0x10);
                sceVu0AddVector(xq2, xq2, cur + 0x20);
                sceVu0ApplyMatrix(xclip, mc, xq2);
                copy4(dst, xq0);
                dst += 0x40;
            }
        }
        n = count;
        if (n < 3) return 0;
    }
    // Pack in place to stride 0x30 (qword order as the original: no overlap hazard).
    const u32 out = buf + (f << 9);
    for (s32 k = 0; k < n; ++k) {
        const u32 d = out + 0x30 * static_cast<u32>(k), s = out + 0x40 * static_cast<u32>(k);
        for (u32 q = 0; q < 3; ++q)
            for (u32 o = 0; o < 16; o += 4) GRef<u32>(d + 0x10 * q + o) = GRef<u32>(s + 0x10 * q + o);
    }
    GRef<s32>(n_out) = n;
    return out;
}

// EE: 0x00138350
// Sea draw (docs/subsystems/rendering.md "Sea draw"): W = sea object, one textured quad per visible
// block (two triangles through the CPU clipper), alpha fading with distance, UV scrolling with the
// frame counter MC+0x2C; built into one of four rotating packet buffers called from bucket 0x1E.
// fpucheck: 1 sub (k), 1 sub per vertex (far - w), 1 add + 1 sub per vertex (UV scroll)
void func_00138350(u32 w, u32 cam, u32 ot) {
    guest::GuestFrame frame(0xF0);
    const u32 sp = frame.Address();
    const u32 mv = sp, mcl = sp + 0x40, s = sp + 0x90, col = sp + 0xA0, uv = sp + 0xB0, xyz = sp + 0xC0,
              rgba = sp + 0xD0, nv = sp + 0xE0;
    func_00112CA8(ot, 0x1E, 0x2500, cam);
    u32 p = BeginPacket();
    auto cp16 = [&](u32 src) {
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(p + o) = GRef<u32>(src + o);
        p += 0x10;
    };
    cp16(w + 0x38);
    cp16(w + 0x48);
    cp16(w + 0x58);
    cp16(w + 0x78);
    EndPacket(ot, 0x1E, p);
    sceVu0CopyMatrix(mv, cam + 0xE0);
    func_00136990(1.0f, GRef<f32>(GRef<u32>(0x00410694) + 0xA4), mcl, cam);
    p = BeginPacket();
    auto wr = [&](u32 off, u32 v) { GRef<u32>(p + off) = v; };
    wr(4, 0); wr(0, 0x10000006); wr(8, 0); wr(0xC, 0x50000006); p += 0x10;
    wr(0, 0x8005); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    cp16(g_pMainContext().addr + 0xD0);
    wr(0, 0x34000); wr(8, 0x47); p += 0x10;
    wr(0, 0x3F0003FF); wr(8, 8); wr(4, 0); p += 0x10;  // CLAMP_1
    wr(0, 0x60); wr(8, 0x14); wr(4, 0); p += 0x10;
    cp16(g_pMainContext().addr + 0xE0);
    GRef<u32>(p - 0x10 + 4) = 1;
    if ((GRef<u32>(GRef<u32>(0x00410694) + 0xF4) & 0x80) == 0) {
        p = BuildTextureUpload(GRef<u32>(GRef<u32>(0x00410694) + 0xF8), p, 1, 0);
        p = WriteTex0Tex1(GRef<u32>(GRef<u32>(0x00410694) + 0xF8), p);
    }
    EndPacket(ot, 0x1E, (p + 0xF) & 0xFFFFFFF0u);
    // Quad template in the scratchpad (vertex stride 0x30: q0 UV, q1 unwritten, q2 position).
    const u32 q = 0x70000400;
    const s32 frameNo = GRef<s32>(g_pMainContext().addr + 0x2C);
    GRef<f32>(q + 0x94) = 8.0f;
    const s32 r = frameNo % 0x2800;
    GRef<f32>(q + 0x30) = 8.0f;
    GRef<f32>(q + 0x64) = 8.0f;
    GRef<f32>(q + 0x90) = 8.0f;
    GRef<f32>(q + 0xBC) = 1.0f;
    GRef<u32>(q + 0x00) = 0;
    GRef<u32>(q + 0x04) = 0;
    GRef<f32>(q + 0x08) = 1.0f;
    GRef<u32>(q + 0x0C) = 0;
    GRef<u32>(q + 0x34) = 0;
    GRef<f32>(q + 0x38) = 1.0f;
    GRef<u32>(q + 0x3C) = 0;
    GRef<u32>(q + 0x60) = 0;
    GRef<f32>(q + 0x68) = 1.0f;
    GRef<u32>(q + 0x6C) = 0;
    GRef<f32>(q + 0x98) = 1.0f;
    GRef<u32>(q + 0x9C) = 0;
    GRef<u32>(q + 0x24) = 0;
    GRef<f32>(q + 0x2C) = 1.0f;
    GRef<u32>(q + 0x54) = 0;
    GRef<f32>(q + 0x5C) = 1.0f;
    GRef<u32>(q + 0x84) = 0;
    GRef<f32>(q + 0x8C) = 1.0f;
    GRef<u32>(q + 0xB4) = 0;
    const u32 t = GRef<u32>(0x00410694);
    const f32 farz = GRef<f32>(t + 0xA4);
    u32 xs = GRef<u32>(t + 0x80), zs = GRef<u32>(t + 0x84);
    const f32 k = fpu::Div(255.0f, fpu::Sub(GRef<f32>(t + 0xA4), GRef<f32>(t + 0xA8)));
    const f32 o = fpu::Div(fpu::IntToFloat(r), 10240.0f);
    if (GRef<s32>(w + 0x30) < 3)
        GRef<s32>(w + 0x30) += 1;
    else
        GRef<s32>(w + 0x30) = 0;
    const u32 pbuf = GRef<u32>(w + 0x20 + 4 * GRef<u32>(w + 0x30));
    p = pbuf;
    for (s32 c = 0; c < GRef<s32>(t + 0x7C); ++c) {
        const s32 x = GRef<s16>(xs);
        xs += 2;
        const f32 x0 = fpu::IntToFloat((x - 0x80) << 11), x1 = fpu::IntToFloat((x - 0x78) << 11);
        GRef<f32>(0x70000420) = x0;
        const s32 z = GRef<s16>(zs);
        GRef<f32>(0x70000480) = x0;
        zs += 2;
        GRef<f32>(0x700004B0) = x1;
        const f32 z1 = fpu::IntToFloat((z - 0x78) << 11), z0 = fpu::IntToFloat((z - 0x80) << 11);
        GRef<f32>(0x70000450) = x1;
        GRef<f32>(0x700004B8) = z1;
        GRef<f32>(0x70000458) = z0;
        GRef<f32>(0x70000428) = z0;
        GRef<f32>(0x70000488) = z1;
        for (u32 tri = 0; tri < 2; ++tri) {
            for (u32 v = 0; v < 3; ++v)
                for (u32 o2 = 0; o2 < 0x30; o2 += 4)
                    GRef<u32>(0x70000000 + 0x40 * v + o2) = GRef<u32>(q + 0x30 * (tri + v) + o2);
            u32 vert = func_00137930(0x70000000, nv, mcl);
            if (vert == 0) continue;
            const u32 n = GRef<u32>(nv);
            wr(4, 0); wr(0, (n * 3 + 1) | 0x10000000); wr(8, 0); wr(0xC, (n * 3 + 1) | 0x50000000); p += 0x10;
            wr(8, 0x412); wr(4, 0x302EC000); wr(0, n | 0x8000); p += 0x10;
            for (s32 i = 0; i < GRef<s32>(nv); ++i) {
                sceVu0ApplyMatrix(s, mv, vert + 0x20);
                const f32 sw = GRef<f32>(s + 0xC);
                f32 a = fpu::Sub(farz, sw);
                const f32 iw = fpu::Div(1.0f, sw);
                a = a * k;
                f32 av;
                if (a < 0.0f)
                    av = 0.0f;
                else if (a < 255.0f)
                    av = fpu::Div(a * 127.0f, 255.0f);
                else
                    av = 127.0f;
                GRef<f32>(col + 0xC) = av;
                GRef<f32>(col + 8) = av;
                GRef<f32>(col + 4) = av;
                GRef<f32>(col + 0) = av;
                sceVu0ScaleVector(s, s, iw);
                sceVu0CopyVector(uv, vert);
                GRef<f32>(uv + 0) = fpu::Add(GRef<f32>(uv + 0), o);
                GRef<f32>(uv + 4) = fpu::Sub(GRef<f32>(uv + 4), o);
                sceVu0ScaleVector(uv, uv, iw);
                sceVu0FTOI4Vector(xyz, s);
                sceVu0FTOI0Vector(rgba, col);
                cp16(uv);
                cp16(rgba);
                cp16(xyz);
                vert += 0x30;
            }
        }
    }
    wr(4, 0); wr(0, 0x60000000); wr(0xC, 0); wr(8, 0);
    p = BeginPacket();
    wr(0, 0x50000000); wr(4, pbuf); wr(8, 0); wr(0xC, 0);
    EndPacket(ot, 0x1E, (p + 0x8 + 0x17) & 0xFFFFFFF0u);
    p = BeginPacket();
    EndPacket(ot, 0x1E, (p + 0xF) & 0xFFFFFFF0u);
    p = BeginPacket();
    wr(4, 0); wr(0, 0x10000005); wr(8, 0); wr(0xC, 0x50000005); p += 0x10;
    wr(8, 0xE); wr(0, 0x8004); wr(4, 0x10000000); p += 0x10;
    wr(8, 0x47); wr(0, 0x70000); p += 0x10;
    wr(8, 0x4E); GRef<u64>(p) = GRef<u64>(g_pMainContext().addr + 0xE0); p += 0x10;
    wr(8, 8); wr(0, 0); wr(4, 0); p += 0x10;  // CLAMP_1
    cp16(cam + 0x1A0);
    EndPacket(ot, 0x1F, p);
}

// ---- View packet helpers (docs/subsystems/rendering.md "View packet helpers") ----------------

// EE: 0x00138B50
// Bucket 0: A+D CLAMP_1 = S+0x17E8 (S = flight scene *0x004104C4).
void func_00138B50(u32 ot) {
    const u32 p = AllocBucketPacket(ot, 0, 2, 0, 0x50000002);
    GRef<u64>(p + 0) = 0x1000000000008001ull;
    GRef<u64>(p + 8) = 0xE;
    const u64 v = GRef<u64>(GRef<u32>(0x004104C4) + 0x17E8);
    GRef<u64>(p + 0x18) = 8;
    GRef<u64>(p + 0x10) = v;
}

// EE: 0x00138BB0
// VU1 program A (.vutext 0x00297C10, 0xEA qwords) into buckets 6 and 9.
void func_00138BB0(u32 ot) {
    AddBucketRef(ot, 6, 0x00297C10, 0xEA);
    AddBucketRef(ot, 9, 0x00297C10, 0xEA);
}

// EE: 0x00138C10
// The .vudata block (0x00402200; qword count = the halfword at 0x004021F0, the low half of the
// block's own DMA tag) into bucket 0.
void func_00138C10(u32 ot) { AddBucketRef(ot, 0, 0x00402200, GRef<u16>(0x004021F0)); }

// EE: 0x00138C28
// VU1 program B (.vutext 0x00298AD0, 0xD4 qwords) into bucket 0.
void func_00138C28(u32 ot) { AddBucketRef(ot, 0, 0x00298AD0, 0xD4); }

// EE: 0x00138C58
// SCISSOR of the view rectangle (cam+0x188..0x18E) into S+0x1800, then SCISSOR_1 and SCISSOR_2 in
// bucket 0. The fields are combined as the code does (sign-extended halfwords ORed in 64 bits).
void func_00138C58(u32 cam, u32 ot) {
    const u32 s = GRef<u32>(0x004104C4);
    const s64 x0 = GRef<s16>(cam + 0x188), y0 = GRef<s16>(cam + 0x18A);
    const s64 y1 = static_cast<s32>(static_cast<s32>(y0) + GRef<s16>(cam + 0x18E) - 1);
    const s64 x1 = static_cast<s32>(static_cast<s32>(x0) + GRef<s16>(cam + 0x18C) - 1);
    GRef<u64>(s + 0x1800) = static_cast<u64>(x0) | (static_cast<u64>(y1) << 48) | (static_cast<u64>(y0) << 32) |
                            (static_cast<u64>(x1) << 16);
    const u32 p = AllocBucketPacket(ot, 0, 3, 0, 0x50000003);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0) = 0x1000000000008002ull;
    GRef<u64>(p + 0x18) = 0x40;
    GRef<u64>(p + 0x10) = GRef<u64>(GRef<u32>(0x004104C4) + 0x1800);
    GRef<u64>(p + 0x28) = 0x41;
    GRef<u64>(p + 0x20) = GRef<u64>(GRef<u32>(0x004104C4) + 0x1800);
}

// EE: 0x00138D18
// Last bucket: SCISSOR_1 = SCISSOR_2 = full draw area as the code writes it (x1 = draw_w,
// y1 = draw_h, no -1).
void func_00138D18(u32 ot) {
    const u32 p = AllocBucketPacket(ot, GRef<s32>(ot + 8) - 1, 3, 0, 0x50000003);
    const MainContext* mc = Ctx();
    GRef<u64>(p + 0) = 0x1000000000008002ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x40;
    GRef<u64>(p + 0x10) = (static_cast<u64>(mc->draw_w) << 16) | (static_cast<u64>(mc->draw_h) << 48);
    GRef<u64>(p + 0x28) = 0x41;
    GRef<u64>(p + 0x20) = (static_cast<u64>(mc->draw_w) << 16) | (static_cast<u64>(mc->draw_h) << 48);
}

// EE: 0x00138DB0
// Z clear of the view rectangle: a sprite with Z = 0 and colour 0 drawn with ZTST always, the GIF
// tag read from .data 0x00404670.
void func_00138DB0(u32 cam, u32 ot) {
    const u64 tag0 = GRef<u64>(0x00404670), tag1 = GRef<u64>(0x00404678);
    u32 p = AllocBucketPacket(ot, 0, 6, 0, 0x50000006);
    const MainContext* mc = Ctx();
    GRef<u64>(p + 0) = tag0;
    GRef<u64>(p + 8) = tag1;
    p += 0x10;
    GRef<u64>(p + 0) = 0x30000;
    GRef<u64>(p + 8) = 0x47;
    p += 0x10;
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(p + o) = 0;
    p += 0x10;
    const s32 hw = mc->draw_w >> 1, hh = mc->draw_h >> 1;
    GRef<u32>(p + 0) = static_cast<u32>(GRef<s16>(cam + 0x188) + 0x800 - hw) << 4;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(p + 0xC) = 0;
    GRef<u32>(p + 4) = static_cast<u32>(GRef<s16>(cam + 0x18A) + 0x800 - hh) << 4;
    p += 0x10;
    GRef<u32>(p + 0) = static_cast<u32>(GRef<s16>(cam + 0x188) + 0x800 - hw + GRef<s16>(cam + 0x18C)) << 4;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(p + 0xC) = 0;
    GRef<u32>(p + 4) = static_cast<u32>(GRef<s16>(cam + 0x18A) + 0x800 - hh + GRef<s16>(cam + 0x18E)) << 4;
    p += 0x10;
    GRef<u64>(p + 8) = 0x47;
    GRef<u64>(p + 0) = 0x70000;
}

// EE: 0x0013ED58
// Sets the frame alpha of the view rectangle to a (sprites in 64-pixel columns with ALPHA_1 =
// Cd + 0 and ZMSK), then restores ZBUF_1 from the main context; one packet into bucket b.
void func_0013ED58(u32 cam, u32 ot, s32 b, u32 a) {
    const s32 w = GRef<s16>(cam + 0x18C);
    const s32 h2 = GRef<s16>(cam + 0x18E) >> 1;
    const s32 n = (w + 63) / 64;
    const s32 cy = fpu::FloatToInt(GRef<f32>(cam + 0x17C));
    const s32 cx = fpu::FloatToInt(GRef<f32>(cam + 0x178));
    u32 p = BeginPacket();
    auto wr = [&](u32 off, u32 v) { GRef<u32>(p + off) = v; };
    wr(4, 0); wr(0, 0x10000005); wr(8, 0); wr(0xC, 0x50000005); p += 0x10;
    wr(0, 4); wr(8, 0xE); wr(4, 0x10000000); p += 0x10;
    wr(0, 0x4A); wr(8, 0x42); wr(4, 0); wr(0xC, 0); p += 0x10;
    wr(0, 0x4010); wr(8, 8); wr(4, 0); p += 0x10;
    wr(0, 0x30000); wr(8, 0x47); p += 0x10;
    wr(0, 0x01000000); wr(4, 1); wr(8, 0x4E); wr(0xC, 0); p += 0x10;
    wr(0, 0x10000000 | static_cast<u32>(n + 3)); wr(4, 0); wr(8, 0); wr(0xC, 0x50000000 | static_cast<u32>(n + 3));
    p += 0x10;
    wr(0, 1); wr(8, 0x10); wr(4, 0x24000000); p += 0x10;
    GRef<u64>(p) = 0x46;
    wr(8, (a << 24) | 0x808080); wr(0xC, 0x3F800000); p += 0x10;
    wr(0, static_cast<u32>(n) | 0x8000); wr(4, 0x24000000); wr(8, 0x4C); p += 0x10;
    u32 x = static_cast<u32>(cx - (w >> 1)) << 4;
    for (s32 j = 0; j < n; ++j) {
        wr(4, 0);
        wr(0, x | (static_cast<u32>(cy - h2) << 20));
        wr(8, (x + 0x400) | (static_cast<u32>(cy + h2) << 20));
        wr(0xC, 0);
        p += 0x10;
        x += 0x400;
    }
    wr(4, 0); wr(0, 0x10000002); wr(8, 0); wr(0xC, 0x50000002); p += 0x10;
    wr(0, 0x8001); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(p + o) = GRef<u32>(g_pMainContext().addr + 0xE0 + o);
    EndPacket(ot, b, p + 0x10);
}

// EE: 0x0013EA78
// DrawTerrain: per-view setup, culling, texture cache, cells; the sea when T+0xF4 bit 0.
void DrawTerrain(u32 cam, u32 ot, u32 ot2) {
    (void)ot2;  // passed on to func_0013A6B8, which does not read it
    func_0013A110(GRef<u32>(0x00410694), cam);
    func_0013A6B8(GRef<u32>(0x00410694), cam);
    func_0013B2F8(GRef<u32>(0x00410694), cam, ot);
    func_0013CDC0(GRef<u32>(0x00410694), cam, ot);
    if ((GRef<u32>(GRef<u32>(0x00410694) + 0xF4) & 1) != 0) func_00138350(GRef<u32>(0x0041068C), cam, ot);
}

// EE: 0x0013EC60
// Sequence-model lists: DrawSpecialObject (0x001E0A98, slice S5f) for every model of every entry.
void func_0013EC60(u32 list, u32 cam, u32 ot) {
    (void)cam;
    (void)ot;
    for (s32 k = 0; k < GRef<s32>(list + 0x118); ++k) {
        for (s32 j = 0; j < GRef<s32>(list + 0x98 + 4 * static_cast<u32>(k)); ++j) {
            const u32 model = GRef<u32>(GRef<u32>(list + 0x18 + 4 * static_cast<u32>(k)) + 4 * static_cast<u32>(j));
            (void)model;
            log::WriteOnce("func_001E0A98", log::Level::Info, "game",
                           "world draw: DrawSpecialObject (0x001E0A98) not translated yet (slice S5f)");
        }
    }
}

// ---- Flight frame draw lists (docs/subsystems/rendering.md "Flight frame") -------------------

namespace {

// Next ordering-table head from the flight scene's bucket pool (+0x1540, 16 bytes each, counter
// +0x1651 read as a signed byte).
u32 NextFlightTable() {
    const u32 s = g_FlightScene();
    const s32 i = GRef<s8>(s + 0x1651);
    GRef<u8>(s + 0x1651) = static_cast<u8>(GRef<u8>(s + 0x1651) + 1);
    return s + 0x1540 + static_cast<u32>(i) * 16;
}

void DrawListNotYet(const char* name, const char* what) {
    log::WriteOnce(name, log::Level::Info, "game", "flight draw lists: %s (%s) not translated yet", name, what);
}

}  // namespace

// EE: 0x00155AF0
// Empty hook called after the views.
void func_00155AF0(u32 ot) { (void)ot; }

// EE: 0x0010BD68
// Builds the frame's ordering tables: per active view the 3D tables (main, terrain, ground
// objects, effects, optional sequence) and the 2D overlay with fade, HUD and menus; then the
// replay HUD and the shared table +0x1640. Calls into blocks B/C (effects, projectiles, HUD,
// radio, menus, sequences) are logged once and skipped until translated; the packets the skipped
// radio/subtitle drawers would open with BeginPacket/EndPacket are skipped with them.
void BuildFlightDrawLists() {
    debug::OnGuestCheckpoint(0x0010BD68);
    MainContext* ctx = Ctx();
    const u32 vif1 = g_pMainContext().addr + 0x60;
    GRef<u8>(g_FlightScene() + 0x1651) = 0;
    u32 ov = NextFlightTable();
    InitBucketList(ov, 4);
    SetPacketListBuckets(vif1, ov);
    InitBucketList(g_FlightScene() + 0x1640, 4);
    for (s32 v = 0; v < GRef<s8>(g_FlightScene() + 0x1650); ++v) {
        const u32 view = g_FlightScene() + 0x440 * static_cast<u32>(v);
        const u32 cam = view + 0xB0;
        if (GRef<s32>(view) >= 0) continue;
        if ((GRef<u32>(view) & 1) != 0) {
            const u32 main = NextFlightTable();
            const u32 terrain = NextFlightTable();
            const u32 ground = NextFlightTable();
            const u32 effects = NextFlightTable();
            InitBucketList(main, 0x10);
            InitBucketList(terrain, 0x20);
            InitBucketList(ground, 0x20);
            InitBucketList(effects, 0x400);
            func_00104090(main, 4, terrain);
            func_00104090(main, 2, ground);
            func_00104090(main, 0xC, effects);
            func_001043E8(vif1, main);
            func_00138B50(main);
            func_00138BB0(main);
            func_00138C10(terrain);
            func_00138C28(ground);
            func_00138C58(cam, main);
            if ((GRef<u32>(view) & 0x40000000) != 0) func_00138DB0(cam, main);
            DrawListNotYet("func_001558C8", "effect sub-table setup, slice S5f");
            func_00111B28(cam, main);
            DrawListNotYet("func_001D3318", "per-view effect pass, slice S5f");
            DrawTerrain(cam, terrain, view);
            DrawMapObjects(cam, ground);
            func_00118330(cam, main, 5);
            func_0011E0D8(view, main, effects);
            DrawListNotYet("DrawProjectiles", "0x0014BB88, slice S5d");
            DrawListNotYet("DrawEffects", "0x00155330, slice S5f");
            if (ctx->sub == 0x13 || GRef<s32>(g_FlightScene() + 0x1688) == 1) {
                const u32 seq = NextFlightTable();
                InitBucketList(seq, 1);
                func_00104090(main, 9, seq);
                DrawListNotYet("func_00179008", "sequence characters, slice S5i");
                DrawListNotYet("func_00179298", "sequence overlays, slice S5i");
            }
            func_00138D18(main);
        }
        ov = NextFlightTable();
        InitBucketList(ov, 4);
        func_001043E8(vif1, ov);
        const f32 fade = GRef<f32>(g_FlightScene() + 0x1670 + 4 * static_cast<u32>(v));
        if (0.0f < fade) DrawScreenFade(fade, ov, 2, 0, cam);
        if ((GRef<u32>(view) & 0xE) != 0) {
            const u32 hud = NextFlightTable();
            InitBucketList(hud, 8);
            func_00104090(ov, 1, hud);
            func_00138C58(cam, hud);
            DrawListNotYet("func_0015F560", "full-screen mission image, slice S5g");
            if ((GRef<u32>(view) & 2) != 0) DrawListNotYet("DrawHud", "0x0015EAE0, slice S5g");
            if ((GRef<u32>(view) & 4) != 0) {
                const s8 type = GRef<s8>(g_FlightScene() + 0x1653);
                if (type == 0 || type == 3) {
                    if (GRef<u8>(g_GameState().addr + 0x29) != 3)
                        DrawListNotYet("func_002512B8", "radio window, slice S5e");
                    if (GRef<s8>(g_FlightScene() + 0x1653) == 3)
                        DrawListNotYet("func_00258690", "tutorial control hints, slice S5e");
                }
            }
            if (ctx->sub == 0x1C)
                DrawListNotYet("func_0017E210", "game-over menu, slice S5j");
            else if (ctx->sub == 0x1D)
                DrawListNotYet("func_0017E460", "carrier rearm menu, slice S5j");
            else if ((ctx->flags & 0x100) != 0)
                DrawListNotYet("func_0017E118", "pause menu, slice S5e");
            else if ((GRef<u32>(view) & 8) != 0)
                DrawListNotYet("func_0016DC08", "centre messages, slice S5g");
            func_00138D18(hud);
        }
        if (ctx->sub == 0xE) {
            const u32 sub = NextFlightTable();
            InitBucketList(sub, 1);
            func_00104090(ov, 1, sub);
            DrawListNotYet("func_002593A0", "subtitle timeline, slice S5e");
        }
    }
    DrawListNotYet("func_0017D8F0", "replay HUD, slice S5j");
    func_00155AF0(ov);
    func_001043E8(vif1, g_FlightScene() + 0x1640);
}

}  // namespace ac04::game
