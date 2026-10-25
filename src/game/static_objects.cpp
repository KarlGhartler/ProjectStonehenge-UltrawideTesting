// Static map objects (block A of S5): the object store *0x0041069C (+0x7000.. lists of the current
// map entry, +0x7020/+0x7034/+0x703C/+0x7044/+0x7054 per-entry tables) and the collision
// triangles baked from the model packets. docs/subsystems/rendering.md "Static map objects",
// docs/subsystems/mission-flow.md (func_00144728 allocations).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00143C10
// Triangles of model instance `inst` in world space: per node, its packet chunks (up to the DMA ret
// word 0x60000000) are walked as strips, each position transformed by matrix * node matrix; every
// triangle is 4 qwords at `out` (a, b, c, or a, c, b when the chunk's strip flag is 1; then the unit
// normal). Returns the triangle count.
s32 func_00143C10(u32 inst, u32 out, u32 matrix) {
    s32 count = 0;
    const s32 nodes = GRef<s32>(GRef<u32>(inst + 0x30) + 8);
    guest::GuestFrame frame(0x70);
    const u32 m = frame.Address(), v0 = frame.Address(0x40), v1 = frame.Address(0x50), v2 = frame.Address(0x60);
    for (s32 k = 0; k < nodes; ++k) {
        u32 p = GRef<u32>(GRef<u32>(inst + 0x2C) + 4 * static_cast<u32>(k));
        MulMatrixVu0(m, matrix, GRef<u32>(inst + 0x10) + static_cast<u32>(k) * 0x40);
        u32 tag = GRef<u32>(p);
        while (tag != 0x60000000) {
            const s32 w = (static_cast<s32>(tag & 0xFFFF) - 1) * 4;
            const s32 nv = w / 9;
            u32 pos = p + static_cast<u32>(w % 9 + nv * 3 + 7) * 4;
            u32 gif = p;
            if (tag != 0x17000000) {
                s32 i = 0;
                do {
                    ++i;
                } while (GRef<u32>(p + 4 * static_cast<u32>(i)) != 0x17000000);
                gif = p + 4 * static_cast<u32>(i);
            }
            const u8 flip = GRef<u8>(gif - 0xE);
            CopyQword(v1, pos);
            CopyQword(v2, pos + 0x10);
            u32 next = pos + 0x20;
            sceVu0ApplyMatrix(v1, m, v1);
            sceVu0ApplyMatrix(v2, m, v2);
            for (s32 i = 2; i < nv; ++i, next += 0x10, out += 0x40, ++count) {
                CopyQword(v0, v1);
                CopyQword(v1, v2);
                CopyQword(v2, next);
                sceVu0ApplyMatrix(v2, m, v2);
                CopyQword(out, v0);
                if (flip == 1) {
                    CopyQword(out + 0x10, v2);
                    CopyQword(out + 0x20, v1);
                } else {
                    CopyQword(out + 0x10, v1);
                    CopyQword(out + 0x20, v2);
                }
                func_00109658(out + 0x30, out, out + 0x10, out + 0x20);
            }
            p += ((tag & 0xFFFF) + 6) * 0x10;
            tag = GRef<u32>(p);
        }
    }
    return count;
}

// EE: 0x00144CC0
// Per frame: the static-object lists of the current map entry (terrain t+4) into +0x7000...
void func_00144CC0(u32 obj) {
    const u32 t = GRef<u32>(0x00410694);
    const u32 e = GRef<u32>(t) + GRef<u32>(t + 4) * 100;
    GRef<u32>(obj + 0x7000) = GRef<u32>(e + 0x48);
    GRef<u32>(obj + 0x7018) = GRef<u32>(e + 0x30);
    GRef<u32>(obj + 0x700C) = GRef<u32>(e + 0x4C);
    GRef<u32>(obj + 0x7014) = GRef<u32>(e + 0x34);
    const u32 v = GRef<u32>(e + 0x38);
    const u32 n = obj + GRef<u32>(t + 4) * 4;
    GRef<u32>(obj + 0x702C) = GRef<u32>(e + 0x48);
    const u32 d = GRef<u32>(n + 0x703C);
    GRef<u32>(obj + 0x705C) = GRef<u32>(n + 0x7054);
    GRef<u32>(obj + 0x704C) = d;
    const u32 a = GRef<u32>(n + 0x7020), b = GRef<u32>(n + 0x7044), c = GRef<u32>(n + 0x7034);
    const u32 lists = GRef<u32>(e + 0x3C);
    GRef<s32>(obj + 0xB064) = -1;
    GRef<u32>(obj + 0x7004) = v;
    GRef<u32>(obj + 0x7010) = lists;
    GRef<u32>(obj + 0x7028) = a;
    GRef<u32>(obj + 0x7050) = b;
    GRef<u32>(obj + 0x7030) = c;
}

// EE: 0x00144728
// Static map objects of both map entries: per object kind a histogram of its placements over the
// 16 x 16 blocks of 16 x 16 cells (E+0x30 block table, E+0x34 per-block kind bytes, 0xFF = none);
// then per kind and placement a buffer of (triangle count byte << 6) bytes with the triangles of the
// object translated to the cell ((x - 0x80) * 2048, 0, (z - 0x80) * 2048), and the cell x / z bytes.
void func_00144728(u32 obj) {
    guest::GuestFrame frame(0x50);
    const u32 v = frame.Address(), m = frame.Address(0x10);
    for (u32 n = 0; n < 2; ++n) {
        const u32 e = GRef<u32>(GRef<u32>(0x00410694)) + n * 100;
        if ((GRef<u32>(e + 0x60) & 1) == 0) continue;
        const s32 kinds = GRef<s32>(e + 0x48);
        const u32 objects = GRef<u32>(e + 0x38);
        const u32 slot = 4 * n;
        GRef<u32>(obj + 0x7020 + slot) = WorkHeapAlloc(kinds * 4, 4);
        GRef<u32>(obj + 0x7054 + slot) = WorkHeapAlloc(kinds, 1);
        GRef<u32>(obj + 0x7034 + slot) = WorkHeapAlloc(kinds, 1);
        GRef<u32>(obj + 0x703C + slot) = WorkHeapAlloc(kinds * 4, 4);
        GRef<u32>(obj + 0x7044 + slot) = WorkHeapAlloc(kinds * 4, 4);
        GRef<u32>(obj + 0x7030) = GRef<u32>(obj + 0x7034 + slot);
        const u32 tris = GRef<u32>(obj + 0x7054 + slot);
        GRef<u32>(obj + 0x704C) = GRef<u32>(obj + 0x703C + slot);
        GRef<u32>(obj + 0x7050) = GRef<u32>(obj + 0x7044 + slot);
        for (s32 i = 0; i < kinds; ++i) {
            GRef<u8>(GRef<u32>(obj + 0x7034 + slot) + static_cast<u32>(i)) = 0;
            GRef<u8>(tris + static_cast<u32>(i)) = GRef<u8>(objects + static_cast<u32>(i) * 0x4C + 0x48);
        }
        for (u32 bx = 0; bx < 16; ++bx)
            for (u32 bz = 0; bz < 16; ++bz) {
                const u32 block = GRef<u32>(e + 0x30) + bx + bz * 16;
                for (u32 cx = 0; cx < 16; ++cx)
                    for (u32 cz = 0; cz < 16; ++cz) {
                        const u8 kind = GRef<u8>(GRef<u32>(e + 0x34) + cx + cz * 16 + static_cast<u32>(GRef<u8>(block)) * 0x100);
                        if (kind != 0xFF) {
                            const u32 c = GRef<u32>(obj + 0x7030) + kind;
                            GRef<u8>(c) = static_cast<u8>(GRef<u8>(c) + 1);
                        }
                    }
            }
        GRef<u32>(v + 4) = 0;
        GRef<f32>(v + 0xC) = 1.0f;
        for (s32 i = 0; i < kinds; ++i) {
            const u32 ii = 4 * static_cast<u32>(i);
            u32 placed = 0;
            GRef<u32>(GRef<u32>(obj + 0x7020 + slot) + ii) = WorkHeapAlloc(GRef<u8>(GRef<u32>(obj + 0x7030) + static_cast<u32>(i)) * 4, 4);
            GRef<u32>(GRef<u32>(obj + 0x704C) + ii) = WorkHeapAlloc(GRef<u8>(GRef<u32>(obj + 0x7030) + static_cast<u32>(i)), 1);
            GRef<u32>(GRef<u32>(obj + 0x7050) + ii) = WorkHeapAlloc(GRef<u8>(GRef<u32>(obj + 0x7030) + static_cast<u32>(i)), 1);
            for (u32 bx = 0; bx < 16; ++bx)
                for (u32 bz = 0; bz < 16; ++bz) {
                    const u32 block = GRef<u32>(e + 0x30) + bx + bz * 16;
                    for (u32 cx = 0; cx < 16; ++cx) {
                        const u32 x = cx + bx * 16;
                        for (u32 cz = 0; cz < 16; ++cz) {
                            const u8 kind = GRef<u8>(GRef<u32>(e + 0x34) + cx + cz * 16 + static_cast<u32>(GRef<u8>(block)) * 0x100);
                            if (kind != static_cast<u32>(i)) continue;
                            const u32 z = cz + bz * 16;
                            const u32 buf = WorkHeapAlloc(static_cast<s32>(GRef<u8>(GRef<u32>(obj + 0x7054 + slot) + static_cast<u32>(i))) << 6, 0x10);
                            GRef<f32>(v + 8) = fpu::IntToFloat(static_cast<s32>(z - 0x80) << 11);
                            GRef<f32>(v + 0) = fpu::IntToFloat(static_cast<s32>(x - 0x80) << 11);
                            GRef<u32>(GRef<u32>(GRef<u32>(obj + 0x7020 + slot) + ii) + placed * 4) = buf;
                            sceVu0UnitMatrix(m);
                            sceVu0TransMatrix(m, m, v);
                            func_00143C10(objects + static_cast<u32>(i) * 0x4C, GRef<u32>(GRef<u32>(GRef<u32>(obj + 0x7020 + slot) + ii) + placed * 4), m);
                            GRef<u8>(GRef<u32>(GRef<u32>(obj + 0x704C) + ii) + placed) = static_cast<u8>(x);
                            GRef<u8>(GRef<u32>(GRef<u32>(obj + 0x7050) + ii) + placed) = static_cast<u8>(z);
                            ++placed;
                        }
                    }
                }
        }
    }
    func_00144CC0(obj);
}

// EE: 0x00145010
// Static map objects per view (docs/subsystems/rendering.md "Static map objects"): view scissor and
// context-1 state with the fog colour, texture uploads (4 levels per texture, one bucket per
// texture), then one model instance per visible cell that holds an object.
void func_00145010(u32 o, u32 cam, u32 ot) {
    guest::GuestFrame frame(0xE0);
    const u32 sp = frame.Address();
    const u32 levels = sp, g = sp + 0x10, v = sp + 0x40, m0 = sp + 0x60, m1 = sp + 0xA0;
    u32 p = 0;
    auto wr = [&](u32 off, u32 val) { GRef<u32>(p + off) = val; };
    auto cp16 = [&](u32 src) {
        for (u32 k = 0; k < 16; k += 4) GRef<u32>(p + k) = GRef<u32>(src + k);
    };
    const u32 t = GRef<u32>(0x00410694);
    // 1. View scissor (cam+0x1A0).
    p = BeginPacket();
    wr(0, 0x10000002); wr(4, 0); wr(8, 0x11000000); wr(0xC, 0x50000002); p += 0x10;
    wr(0, 0x8001); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    cp16(cam + 0x1A0);
    EndPacket(ot, 0, p + 0x10);
    // 2. Context 1 and fog colour.
    p = BeginPacket();
    wr(4, 0); wr(0, 0x10000005); wr(8, 0x11000000); wr(0xC, 0x50000005); p += 0x10;
    wr(0, 0x8004); wr(8, 0xE); wr(4, 0x10000000); p += 0x10;
    wr(0, 0x7040D); wr(8, 0x47); p += 0x10;
    wr(0, 0x36C); wr(8, 0x14); wr(4, static_cast<u32>(static_cast<s32>(GRef<s16>(t + 0xF2)))); p += 0x10;
    wr(8, 0x3B); wr(0, 0); wr(4, 0); p += 0x10;
    cp16(g_pMainContext().addr + 0xD0);
    wr(4, 0xFF000000);
    const u32 fog = static_cast<u32>(func_001100A0(cam + 0x10, GRef<f32>(GRef<u32>(0x00410694) + 0x8C),
                                                   GRef<f32>(cam + 4)));
    p += 0x10;
    wr(0, 0x10000002); wr(4, 0); wr(8, 0); wr(0xC, 0x50000002); p += 0x10;
    wr(0, 0x8001); wr(8, 0xE); wr(4, 0x10000000); p += 0x10;
    wr(0, fog); wr(8, 0x3D);
    EndPacket(ot, 0, p + 0x10);
    // 3. Textures.
    GRef<u32>(levels + 4) = 0x400;
    GRef<u32>(levels + 8) = 0x500;
    GRef<u32>(levels + 0xC) = 0x540;
    GRef<u32>(levels + 0) = 0;
    for (s32 e = 0; e < GRef<s32>(o + 0x700C); ++e) {
        p = BeginPacket();
        for (u32 k = 0; k < 4; ++k) {
            InitGimTexture(g, GRef<u32>(GRef<u32>(GRef<u32>(o + 0x7010) + 4 * static_cast<u32>(e)) + 4 * k));
            GRef<u16>(GRef<u32>(g + 4)) = static_cast<u16>(GRef<u16>(levels + 4 * k) + 0x2500);
            if (GRef<u32>(g + 8) != 0) GRef<u16>(GRef<u32>(g + 8)) = 0x3F10;
            p = BuildTextureUpload(g, p, 1, 0);
        }
        InitGimTexture(g, GRef<u32>(GRef<u32>(GRef<u32>(o + 0x7010) + 4 * static_cast<u32>(e))));
        GRef<u16>(GRef<u32>(g + 4)) = 0x2500;
        if (GRef<u32>(g + 8) != 0) GRef<u16>(GRef<u32>(g + 8)) = 0x3F10;
        p = WriteTex0Tex1(g, p);
        EndPacket(ot, e, (p + 0xF) & 0xFFFFFFF0u);
    }
    // 4. Objects.
    GRef<u32>(v + 4) = 0;
    GRef<f32>(v + 0x1C) = 1.0f;
    GRef<f32>(v + 0xC) = 1.0f;
    GRef<u32>(v + 0x10) = 0;
    GRef<u32>(v + 0x14) = 0;
    GRef<u32>(v + 0x18) = 0;
    sceVu0UnitMatrix(m1);
    u32 cls = GRef<u32>(t + 0x100), xs = GRef<u32>(t + 0x74), zs = GRef<u32>(t + 0x78);
    u32 blks = 0x70002000, idxs = 0x70001000;
    for (s32 i = 0; i < GRef<s32>(GRef<u32>(0x00410694) + 0x70); ++i) {
        const u32 obj = GRef<u8>(GRef<u32>(o + 0x7014) +
                                 (static_cast<u32>(GRef<u8>(GRef<u32>(o + 0x7018) + GRef<u8>(blks))) << 8) +
                                 GRef<u8>(idxs));
        if (obj != 0xFF) {
            const u32 c = GRef<u8>(cls);
            const u32 f = ((c & 1) != 0 || (c & 2) != 0) ? 4 : 0;
            GRef<f32>(v + 0) = fpu::IntToFloat(GRef<s16>(xs) - 0x80) * 2048.0f;
            GRef<f32>(v + 8) = fpu::IntToFloat(GRef<s16>(zs) - 0x80) * 2048.0f;
            sceVu0TransMatrix(m0, m1, v);
            p = BeginPacket();
            const u32 inst = GRef<u32>(o + 0x7004) + 0x4C * obj;
            p = EmitModelInstance(inst, p, cam, m0, m1, f | 8);
            const u32 rec = GRef<u32>(o + 0x7004) + 0x4C * obj;
            EndPacket(ot, GRef<s32>(GRef<u32>(GRef<u32>(rec + 0x30) + 0x1C) + 0x1C), p);
        }
        ++cls;
        xs += 2;
        zs += 2;
        ++blks;
        ++idxs;
    }
    // 5. Bucket 0x1F: restore SCISSOR/FRAME (MC+0x110, MC+0xD0).
    p = BeginPacket();
    wr(4, 0); wr(0, 0x10000003); wr(8, 0x11000000); wr(0xC, 0x50000003); p += 0x10;
    wr(0, 0x8002); wr(8, 0xE); wr(4, 0x10000000); p += 0x10;
    cp16(g_pMainContext().addr + 0x110);
    p += 0x10;
    cp16(g_pMainContext().addr + 0xD0);
    p += 0x10;
    wr(0, 0x10000000); wr(4, 0); wr(8, 0); wr(0xC, 0x11000000);
    EndPacket(ot, 0x1F, p + 0x10);
}

// EE: 0x00145578
// Draws the current entry's model O+0x7004 into the frame alpha only (FBMSK 0x00FFFFFF, ZMSK),
// then restores FRAME_1 / ZBUF_1; bucket 8. HYPOTHESIS: an alpha mask pass (rendering.md).
void func_00145578(u32 o, u32 cam, u32 ot) {
    guest::GuestFrame frame(0x40);
    const u32 m = frame.Address();
    sceVu0UnitMatrix(m);
    u32 p = BeginPacket();
    auto header = [&]() {
        GRef<u32>(p + 0) = 0x10000003;
        GRef<u32>(p + 4) = 0;
        GRef<u32>(p + 8) = 0x11000000;
        GRef<u32>(p + 0xC) = 0x50000003;
        p += 0x10;
        GRef<u32>(p + 0) = 0x8002;
        GRef<u32>(p + 4) = 0x10000000;
        GRef<u32>(p + 8) = 0xE;
        p += 0x10;
    };
    auto cp16 = [&](u32 src) {
        for (u32 k = 0; k < 16; k += 4) GRef<u32>(p + k) = GRef<u32>(src + k);
    };
    header();
    cp16(g_pMainContext().addr + 0xD0);
    GRef<u32>(p + 4) = 0x00FFFFFF;
    p += 0x10;
    cp16(g_pMainContext().addr + 0xE0);
    GRef<u32>(p + 4) = 1;
    p = EmitModelInstance(GRef<u32>(o + 0x7004), p + 0x10, cam, m, m, 0x4C);
    header();
    cp16(g_pMainContext().addr + 0xD0);
    p += 0x10;
    cp16(g_pMainContext().addr + 0xE0);
    p += 0x10;
    EndPacket(ot, 8, p);
}

// EE: 0x0013EB20
// DrawMapObjects: the static map objects (*0x0041069C) for this view.
void DrawMapObjects(u32 cam, u32 ot) { func_00145010(GRef<u32>(0x0041069C), cam, ot); }

}  // namespace ac04::game
