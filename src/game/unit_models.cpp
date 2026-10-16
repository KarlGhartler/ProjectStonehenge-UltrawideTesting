// Unit model sets and instances (block A of S5): the unit model object *0x004106A0 (+0 per-unit
// records of 0x18, +8 model-set table, +0xC set count, +0x8024 ...), model sets of 0x14 bytes
// (+0 bound models (0x28 each), +4 extra item table, +8 header item, +0xC header word 1, +0x10
// model count) and per-unit state records of 0x20 (+0 instances (0x4C each), +4, +8, +0xC set
// index, +0x10 instance count, +0x14). docs/subsystems/rendering.md "Units".
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x001457E0
// Loads a unit model set from a container (none: all fields 0). Headers with word 0 below 0xA0
// (or 5 items) hold count - 1 models after the header; others hold 4 models (items 3-6), the
// header in item 2 and an extra item table in item 1.
void func_001457E0(u32 obj, u32 set, u32 container) {
    (void)obj;
    if (container == 0) {
        GRef<u32>(set + 4) = 0;
        GRef<u32>(set + 0x10) = 0;
        GRef<u32>(set + 0xC) = 0;
        GRef<u32>(set + 8) = 0;
        GRef<u32>(set + 0) = 0;
        return;
    }
    guest::GuestFrame frame(0x20);
    const u32 r = frame.Address(), sub = frame.Address(0x10);
    InitContainerReader(r, container);
    GRef<s32>(set + 0x10) = GRef<s32>(r);
    const u32 head = GetContainerItem(r, 0);
    u32 version = GRef<u32>(head);
    if (GRef<s32>(r) == 5) version = 0x96;
    GRef<u32>(set + 0xC) = GRef<u32>(head + 4);
    if (version < 0xA0) {
        GRef<u32>(set + 8) = GetContainerItem(r, 0);
        const u32 models = WorkHeapAlloc(GRef<s32>(r) * 0x28 - 1, 0x10);
        GRef<u32>(set + 0) = models;
        for (s32 k = 0; k < GRef<s32>(r) - 1; ++k)
            BindModelData(static_cast<u32>(k) * 0x28 + models, GetContainerItem(r, k + 1));
        GRef<s32>(set + 0x10) = GRef<s32>(set + 0x10) - 1;
        return;
    }
    GRef<u32>(set + 8) = GetContainerItem(r, 2);
    u32 m = WorkHeapAlloc(0xA0, 0x10);
    GRef<u32>(set + 0) = m;
    for (s32 k = 0; k < 4; ++k, m += 0x28) BindModelData(m, GetContainerItem(r, k + 3));
    InitContainerReader(sub, GetContainerItem(r, 1));
    if (GRef<s32>(sub) < 1) {
        GRef<u32>(set + 4) = 0;
    } else {
        u32 t = WorkHeapAlloc(GRef<s32>(sub) * 4, 0x10);
        GRef<u32>(set + 4) = t;
        for (s32 i = 0; i < GRef<s32>(sub); ++i, t += 4) GRef<u32>(t) = GetContainerItem(sub, i);
    }
    GRef<u32>(set + 0x10) = 4;
}

// EE: 0x001459E0
// Unit model instances: the 2 shared sets (player aircraft, S+0x175C + p * 0x40), the mission's
// sets (block S+0x16E8), then per unit (U+0x5B states, set index per state at U+0x58...) the
// instances of its set; 0xFF = no model. Two unused 4-byte-per-set tables are allocated as in the
// original.
void func_001459E0(u32 obj) {
    const u32 shared = WorkHeapAlloc(0x28, 4);
    for (u32 p = 0; p < 2; ++p) func_001457E0(obj, shared + p * 0x14, GRef<u32>(g_FlightScene() + p * 0x40 + 0x175C));
    guest::GuestFrame frame(0x10);
    const u32 r = frame.Address();
    InitContainerReader(r, GRef<u32>(g_FlightScene() + 0x16E8));
    GRef<s32>(obj + 0xC) = GRef<s32>(r);
    GRef<u32>(obj + 8) = WorkHeapAlloc(GRef<s32>(r) * 4, 0x10);
    WorkHeapAlloc(GRef<s32>(obj + 0xC) * 4, 0x10);
    WorkHeapAlloc(GRef<s32>(obj + 0xC) * 4, 0x10);
    const u32 sets = WorkHeapAlloc(GRef<s32>(obj + 0xC) * 0x14, 4);
    for (s32 i = 0; i < GRef<s32>(obj + 0xC); ++i) func_001457E0(obj, sets + static_cast<u32>(i) * 0x14, GetContainerItem(r, i));
    GRef<u32>(obj) = WorkHeapAlloc(GRef<s32>(g_UnitList()) * 0x18, 0x10);
    for (s32 n = 0; n < GRef<s32>(g_UnitList()); ++n) {
        const u32 u = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(n));
        const u32 rec = GRef<u32>(obj) + static_cast<u32>(n) * 0x18;
        GRef<u32>(rec + 4) = GRef<u8>(u + 0x5B);
        GRef<u32>(rec + 8) = WorkHeapAlloc(static_cast<s32>(GRef<u8>(u + 0x5B)) << 5, 0x10);
        for (s32 k = 0; k < GRef<s32>(rec + 4); ++k) {
            const u8 id = GRef<u8>(u + 0x58 + static_cast<u32>(k));
            const u32 st = GRef<u32>(rec + 8) + static_cast<u32>(k) * 0x20;
            if (id == 0xFF) {
                GRef<u32>(st + 0x14) = 0;
                GRef<s32>(st + 0xC) = -1;
                GRef<u32>(st + 0x10) = 0;
                GRef<u32>(st + 8) = 0;
                GRef<u32>(st + 0) = 0;
                GRef<u32>(st + 4) = 0;
                continue;
            }
            const u32 set = n < 2 ? shared + static_cast<u32>(n) * 0x14 : sets + id * 0x14u;
            GRef<u32>(st + 0xC) = id;
            GRef<u32>(st + 0x10) = GRef<u32>(set + 0x10);
            const u32 inst = WorkHeapAlloc(GRef<s32>(set + 0x10) * 0x4C, 0x10);
            const u32 models = GRef<u32>(set + 0);
            GRef<u32>(st + 8) = GRef<u32>(set + 8);
            GRef<u32>(st + 4) = GRef<u32>(set + 4);
            GRef<u32>(st + 0x14) = GRef<u32>(set + 0xC);
            GRef<u32>(st + 0) = inst;
            for (s32 i = 0; i < GRef<s32>(st + 0x10); ++i)
                CreateModelInstance(static_cast<u32>(i) * 0x4C + inst, static_cast<u32>(i) * 0x28 + models);
        }
    }
    GRef<s32>(obj + 0x8024) = -1;
}

namespace {

constexpr u32 kUnitPtrTable = 0x002A1F70;  // unit object per unit index

u32 UnitObject(s32 u) { return GRef<u32>(kUnitPtrTable + 4 * static_cast<u32>(u)); }

// Current LOD block of unit u: word (word (H[0] + 0x18u + 8) + 0x20 * byte U+0x5A).
u32 UnitLodEntry(u32 h, s32 u) {
    return GRef<u32>(GRef<u32>(h) + 0x18 * static_cast<u32>(u) + 8) +
           0x20 * static_cast<u32>(GRef<u8>(UnitObject(u) + 0x5A));
}

void DrawSpecialObjectNotYet() {
    log::WriteOnce("func_001E0A98", log::Level::Info, "game",
                   "world draw: DrawSpecialObject (0x001E0A98) not translated yet (slice S5f)");
}

}  // namespace

// EE: 0x001456F8
// Direction angles of d into out: out.y = atan2(d.x, d.z), out.x = atan2(-d.y, |d.xz|), out.z =
// out.w = 0; a vertical d gives (0 or *0x0040E518, 0, same) by the sign of d.y. The sqrtf fallback
// after sqrt.s (NaN results) cannot happen on the EE.
// fpucheck: 1 add (x^2 + z^2)
void func_001456F8(u32 out, u32 d) {
    const f32 x = GRef<f32>(d), z = GRef<f32>(d + 8);
    if (x == 0.0f && z == 0.0f) {
        GRef<f32>(out + 4) = 0.0f;
        if (0.0f <= GRef<f32>(d + 4)) {
            GRef<f32>(out + 0) = 0.0f;
            GRef<f32>(out + 8) = 0.0f;
        } else {
            const f32 c = GRef<f32>(0x0040E518);
            GRef<f32>(out + 0) = c;
            GRef<f32>(out + 8) = c;
        }
        return;
    }
    GRef<f32>(out + 4) = atan2f(x, z);
    const f32 xx = GRef<f32>(d) * GRef<f32>(d), zz = GRef<f32>(d + 8) * GRef<f32>(d + 8);
    const f32 len = fpu::Sqrt(fpu::Add(xx, zz));
    GRef<f32>(out + 0) = atan2f(-GRef<f32>(d + 4), len);
    GRef<u32>(out + 0xC) = 0;
    GRef<u32>(out + 8) = 0;
}

// EE: 0x00145D08
// Part value `track` = v on the three instances of unit u's current LOD block.
void func_00145D08(f32 v, u32 h, s32 u, s32 track) {
    const u32 inst = UnitLodEntry(h, u);
    SetModelPartValue(v, inst, track);
    SetModelPartValue(v, inst + 0x4C, track);
    SetModelPartValue(v, inst + 0x98, track);
}

// EE: 0x00145DA0
// Track time `track` of unit u's current LOD instance 0.
u32 func_00145DA0(u32 h, s32 u, s32 track) { return func_00141000(UnitLodEntry(h, u), track); }

// EE: 0x00146680
// out = matrix of the first node of unit u's current instance with id `id`; unchanged when absent.
void func_00146680(u32 h, u32 out, s32 u, s32 id) {
    const u32 inst = UnitLodEntry(h, u);
    for (s32 k = 0; k < GRef<s32>(GRef<u32>(inst + 0x30) + 8); ++k) {
        if (GRef<s32>(GRef<u32>(inst + 8) + 4 * static_cast<u32>(k)) == id) {
            sceVu0CopyMatrix(out, GRef<u32>(inst + 0x10) + 0x40 * static_cast<u32>(k));
            return;
        }
    }
}

// EE: 0x00146988
// func_00144228 of unit u's current instance 2.
u32 func_00146988(u32 h, s32 u) { return func_00144228(UnitLodEntry(h, u) + 0x98); }

// EE: 0x001359D0
// Unit model track animation (docs/subsystems/rendering.md): track 1 loops every 240 frames;
// mission 0x14 zeroes tracks 2 and 3; mission 0x19 drives track 2 or 3 with S+0x1654 / 120.
// fpucheck: unsigned int -> float conversions (UnsignedToFloat)
void func_001359D0(s32 u) {
    const u32 s = GRef<u32>(0x004104C4);
    const u32 h = GRef<u32>(0x004106A0);
    func_00145D08(fpu::Div(fpu::UnsignedToFloat(GRef<u32>(s + 0x165C) % 240u), 240.0f), h, u, 1);
    if (Ctx()->sub == 0x14) {
        func_00145D08(0.0f, GRef<u32>(0x004106A0), u, 2);
        func_00145D08(0.0f, GRef<u32>(0x004106A0), u, 3);
    }
    if (Ctx()->sub != 0x19) return;
    const f32 t = fpu::Div(fpu::UnsignedToFloat(GRef<u32>(GRef<u32>(0x004104C4) + 0x1654)), 120.0f);
    if ((GRef<u32>(GRef<u32>(0x004104F4) + 0x20) & 0x4000000) == 0) {
        func_00145D08(0.0f, GRef<u32>(0x004106A0), u, 2);
        func_00145D08(t, GRef<u32>(0x004106A0), u, 3);
    } else {
        func_00145D08(t, GRef<u32>(0x004106A0), u, 2);
        func_00145D08(0.0f, GRef<u32>(0x004106A0), u, 3);
    }
}

// EE: 0x00145DF8
// UpdateUnitModelHierarchies: the four instances of every visible unit u >= 2 (U+0x4C has
// 0x20000000, 0x10000000 and 0x200), then the aircraft models; H+0x8024 = -1.
void UpdateUnitModelHierarchies(u32 h) {
    guest::GuestFrame frame(0x90);
    const u32 m1 = frame.Address(), m2 = m1 + 0x40, z = m1 + 0x80;
    sceVu0UnitMatrix(m1);
    sceVu0UnitMatrix(m2);
    for (s32 u = 2; u < GRef<s32>(GRef<u32>(0x00410640)); ++u) {
        const u32 f = GRef<u32>(UnitObject(u) + 0x4C);
        if ((f & 0x20000000) == 0 || (f & 0x10000000) == 0 || (f & 0x200) == 0) continue;
        const u32 inst = UnitLodEntry(h, u);
        for (u32 k = 0; k < 4; ++k) {
            GRef<u32>(z) = 0;
            UpdateModelHierarchy(inst + 0x4C * k, z, m1, m2, 1);
        }
    }
    func_001481B8(GRef<u32>(0x004106A4));
    GRef<s32>(h + 0x8024) = -1;
}

// EE: 0x00145F80
// SelectUnitModelLods (docs/subsystems/rendering.md): per unit, visibility against the clip
// matrix (rebuilt when the model's radius group L+0xC changes), then the LOD from the projected
// size. The clip matrix is loaded into VU0 right before each test (func_001367B0).
// fpucheck: 1 add (-2r), 1 sub (e = w - r)
void SelectUnitModelLods(u32 h, u32 v) {
    guest::GuestFrame frame(0x50);
    const u32 m = frame.Address(), out = m + 0x40;
    const u32 cam = v + 0xB0;
    s32 c = -2;
    f32 r = 0.0f;
    for (s32 u = 0; u < GRef<s32>(GRef<u32>(0x00410640)); ++u) {
        const u32 rec = GRef<u32>(h) + 0x18 * static_cast<u32>(u);
        const u32 unit = UnitObject(u);
        GRef<u32>(rec + 0) = 0;
        const u32 l = GRef<u32>(rec + 8) + 0x20 * static_cast<u32>(GRef<u8>(unit + 0x5A));
        GRef<u32>(rec + 0xC) = 0;
        if ((GRef<u32>(unit + 0x4C) & 0x20000000) == 0) continue;
        const bool player = u < 2;
        u32 inst;
        if (player) {
            if (static_cast<s32>(GRef<u8>(v + 6)) == u && (GRef<u8>(v + 7) & 3) != 1) continue;
            const u32 a = GRef<u32>(0x004106A4);
            GRef<u32>(rec + 0x10) = GRef<u32>(GRef<u32>(a + 0x10) + 4 * static_cast<u32>(u));
            inst = GRef<u32>(GRef<u32>(a + 8) + 4 * static_cast<u32>(u));
            GRef<u32>(rec + 0) = 2;
        } else {
            inst = GRef<u32>(l + 0);
            GRef<u32>(rec + 0x10) = GRef<u32>(l + 8);
        }
        if (GRef<s32>(l + 0xC) != c) {
            r = GRef<f32>(inst + 0x40);
            GRef<u32>(rec + 0) |= 2;
            const f32 nr = -r;
            func_00136990(fpu::Add(nr, nr), GRef<f32>(GRef<u32>(0x00410694) + 0xA4), m, cam);
        }
        func_001367B0(m);
        if (func_001367C8(unit, out) != 0) continue;
        GRef<u32>(rec + 0) |= 1;
        if (!player) c = GRef<s32>(l + 0xC);
        const f32 e = fpu::Sub(GRef<f32>(out + 0xC), r);
        f32 size;
        if (e < GRef<f32>(cam + 0x168))
            size = 320.0f;
        else
            size = fpu::Div(r, e) * GRef<f32>(cam + 0x168);
        const s32 kind = GRef<s32>(l + 0x14);
        f32 t0, t1, t2;
        if (kind == 2) {
            t0 = 100.0f; t1 = 64.0f; t2 = 10.0f;
        } else if (kind == 1) {
            t0 = 64.0f; t1 = 32.0f; t2 = 4.0f;
        } else {
            t0 = 32.0f; t1 = 8.0f; t2 = 2.0f;
        }
        u32 lod;
        if (t0 < size)
            lod = 0;
        else if (t1 < size)
            lod = 1;
        else if (t2 < size)
            lod = 2;
        else
            lod = 3;
        GRef<u32>(rec + 0x14) = lod;
        GRef<u32>(rec + 0xC) = inst + 0x4C * lod;
    }
}

// EE: 0x00146268
// Unit models (docs/subsystems/rendering.md): context-1 state and the light set in bucket 6, then
// per visible unit its texture and model instance (bucket 6) and its attached objects
// (DrawSpecialObject, slice S5f: positions and angles are still updated here).
void func_00146268(u32 h, u32 cam, u32 ot, u32 ot2) {
    (void)ot2;
    guest::GuestFrame frame(0x140);
    const u32 sp = frame.Address();
    const u32 m0 = sp, m1 = sp + 0x40, g = sp + 0x80, d = sp + 0xB0, m3 = sp + 0xC0, m2 = sp + 0x100;
    u32 p = BeginPacket();
    auto wr = [&](u32 off, u32 val) { GRef<u32>(p + off) = val; };
    wr(4, 0); wr(0, 0x10000004); wr(8, 0); wr(0xC, 0x50000004); p += 0x10;
    wr(8, 0xE); wr(0, 0x8003); wr(4, 0x10000000); p += 0x10;
    wr(0, 0x60); wr(8, 0x14); wr(4, 0); p += 0x10;
    wr(0, 0x5040D); wr(8, 0x47); p += 0x10;
    wr(0, 0); wr(4, 0); wr(8, 0x3B);
    EndPacket(ot, 6, p + 0x10);
    WriteLightSetPacket(GRef<u32>(0x00410688), cam, ot, 6, 0);
    for (s32 u = 0; u < GRef<s32>(GRef<u32>(0x00410640)); ++u) {
        const u32 unit = UnitObject(u);
        const u32 rec = GRef<u32>(h) + 0x18 * static_cast<u32>(u);
        const u32 l = GRef<u32>(rec + 8) + 0x20 * static_cast<u32>(GRef<u8>(unit + 0x5A));
        if ((GRef<u32>(rec + 0) & 1) == 0) continue;
        p = BeginPacket();
        sceVu0UnitMatrix(m1);
        func_00102D28(m1, m1, unit + 0x10);
        sceVu0TransMatrix(m0, m1, unit);
        InitGimTexture(g, GRef<u32>(rec + 0x10));
        GRef<u16>(GRef<u32>(g + 4)) = 0x2500;
        if (GRef<u32>(g + 8) != 0) GRef<u16>(GRef<u32>(g + 8)) = 0x3F28;
        if ((GRef<u32>(rec + 0) & 2) != 0)
            p = BuildTextureUpload(g, p, 1, 0);
        else
            p = WriteTex0Tex1(g, p);
        const u32 mode = (GRef<u32>(rec + 0x14) > 1 ? 0u : 4u) | 8u;
        p = EmitModelInstance(GRef<u32>(rec + 0xC), p, cam, m0, m1, mode);
        EndPacket(ot, 6, (p + 0xF) & 0xFFFFFFF0u);
        if (GRef<u32>(l + 4) == 0) continue;
        const u32 i0 = GRef<u32>(l + 0);
        sceVu0UnitMatrix(m2);
        func_00102D28(m2, m2, unit + 0x10);
        sceVu0TransMatrix(m3, m2, unit);
        const u32 pos = GRef<u32>(i0 + 0x18), dir = GRef<u32>(i0 + 0x1C);
        const u32 ids = GRef<u32>(i0 + 0xC);
        for (s32 k = 0; k < GRef<s32>(GRef<u32>(i0 + 0x30) + 8); ++k) {
            const u32 b = GRef<u8>(ids + static_cast<u32>(k));
            if (b == 0xFF) continue;
            const u32 o = GRef<u32>(GRef<u32>(l + 4) + 4 * b);
            sceVu0ApplyMatrix(o, m3, pos + 0x10 * static_cast<u32>(k));
            if (GRef<u8>(o + 0x3D) >= 3) {
                sceVu0ApplyMatrix(d, m2, dir + 0x10 * static_cast<u32>(k));
                func_001456F8(o + 0x20, d);
            }
            DrawSpecialObjectNotYet();
        }
    }
    p = BeginPacket();
    wr(4, 0); wr(0, 0x10000000); wr(8, 0); wr(0xC, 0x11000000);
    EndPacket(ot, 6, p + 0x10);
}

}  // namespace ac04::game
