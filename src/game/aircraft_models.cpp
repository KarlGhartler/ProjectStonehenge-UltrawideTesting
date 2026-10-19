// Aircraft models (block A of S5): the aircraft model object *0x004106A4 (per player p: +0[p]
// LOD instances, +4[p] bound models, +8/+0xC/+0x10 cockpit set, +0x14 extra item table, +0x1C LOD
// count, +0x30/+0x34 skin GIMs, +0x44 cockpit flag) and the mission aircraft model sets
// *0x004106A8 (other aircraft, weapons). docs/subsystems/rendering.md "Aircraft models",
// docs/formats/AIRCRAFT.md (blocks 0-2 of the aircraft container).
#include "game/game.h"
#include "game/game_flight.h"

#include <cmath>

#include "common/ee_math.h"

#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/main_context.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

u32 Slot(u32 obj, u32 field, u32 p) { return GRef<u32>(obj + field) + 4 * p; }

}  // namespace

// EE: 0x00149FB8
// Mission aircraft model sets (container S+0x16A0): per set two models (headers below 0xA0 or 2
// items: items 0-1, else 2-3) with instances; the shared texture (S+0x16A4, TBP 0x1F00, CBP 0x3F0C);
// per player the weapon model sets of its aircraft container (S+0x1754 + p * 0x40: items from 6, or
// 7 for headers from 0xB4, two models each). Flag +0x48 bit 1 = loaded.
void func_00149FB8(u32 obj) {
    guest::GuestFrame frame(0x40);
    const u32 rp = frame.Address(), rs = frame.Address(0x10), r = frame.Address(0x20), r2 = frame.Address(0x30);
    GRef<u32>(obj + 0x48) = 0;
    InitContainerReader(r, GRef<u32>(g_FlightScene() + 0x16A0));
    GRef<s32>(obj + 0x38) = GRef<s32>(r);
    GRef<u32>(obj + 4) = WorkHeapAlloc(GRef<s32>(r) * 4, 0x10);
    GRef<u32>(obj + 0) = WorkHeapAlloc(GRef<s32>(obj + 0x38) * 4, 0x10);
    for (s32 i = 0; i < GRef<s32>(obj + 0x38); ++i) {
        const u32 ii = 4 * static_cast<u32>(i);
        InitContainerReader(r2, GetContainerItem(r, i));
        u32 version = GRef<u32>(GetContainerItem(r2, 0));
        if (GRef<s32>(r2) == 2) version = 0x96;
        const bool old = version < 0xA0;
        GRef<u32>(GRef<u32>(obj + 4) + ii) = WorkHeapAlloc(0x98, 0x10);
        GRef<u32>(GRef<u32>(obj + 0) + ii) = WorkHeapAlloc(0x50, 0x10);
        for (s32 k = 0; k < 2; ++k) {
            const u32 rec = GRef<u32>(GRef<u32>(obj + 0) + ii) + static_cast<u32>(k) * 0x28;
            BindModelData(rec, GetContainerItem(r2, old ? k : k + 2));
            CreateModelInstance(GRef<u32>(GRef<u32>(obj + 4) + ii) + static_cast<u32>(k) * 0x4C,
                                GRef<u32>(GRef<u32>(obj + 0) + ii) + static_cast<u32>(k) * 0x28);
        }
    }
    InitGimTexture(obj + 0x10, GRef<u32>(g_FlightScene() + 0x16A4));
    const u32 clut = GRef<u32>(obj + 0x18);
    GRef<u16>(GRef<u32>(obj + 0x14)) = 0x1F00;
    if (clut != 0) GRef<u16>(clut) = 0x3F0C;
    GRef<u32>(obj + 0xC) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 8) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x3C) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x40) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x44) = WorkHeapAlloc(8, 0x10);
    for (u32 p = 0; p < 2; ++p) {
        const u32 a = WorkHeapAlloc(8, 0x10);
        GRef<u32>(Slot(obj, 0x40, p)) = a;
        GRef<u32>(a + 4) = 0;
        GRef<u32>(a + 0) = 0;
        const u32 b = WorkHeapAlloc(0x18, 0x10);
        GRef<u32>(Slot(obj, 0x44, p)) = b;
        for (u32 o = 0x14;; o -= 4) {
            GRef<u32>(b + o) = 0;
            if (o == 0) break;
        }
        const u32 container = GRef<u32>(g_FlightScene() + p * 0x40 + 0x1754);
        if (container == 0) {
            GRef<u32>(Slot(obj, 0xC, p)) = 0;
            GRef<u32>(Slot(obj, 8, p)) = 0;
            GRef<u32>(Slot(obj, 0x3C, p)) = 0;
            continue;
        }
        InitContainerReader(rp, container);
        const bool old = GRef<u32>(GetContainerItem(rp, 0)) < 0xB4;
        GRef<s32>(Slot(obj, 0x3C, p)) = GRef<s32>(rp) - (old ? 6 : 7);
        if (GRef<s32>(Slot(obj, 0x3C, p)) <= 0) continue;
        GRef<u32>(Slot(obj, 0xC, p)) = WorkHeapAlloc(GRef<s32>(Slot(obj, 0x3C, p)) * 4, 0x10);
        GRef<u32>(Slot(obj, 8, p)) = WorkHeapAlloc(GRef<s32>(Slot(obj, 0x3C, p)) * 4, 0x10);
        for (s32 j = 0; j < GRef<s32>(Slot(obj, 0x3C, p)); ++j) {
            const u32 jj = 4 * static_cast<u32>(j);
            GRef<u32>(GRef<u32>(Slot(obj, 0xC, p)) + jj) = WorkHeapAlloc(0x98, 0x10);
            GRef<u32>(GRef<u32>(Slot(obj, 8, p)) + jj) = WorkHeapAlloc(0x50, 0x10);
            InitContainerReader(rs, GetContainerItem(rp, j + (old ? 6 : 7)));
            GetContainerItem(rs, 0);
            for (s32 k = 0; k < 2; ++k) {
                const u32 rec = GRef<u32>(GRef<u32>(Slot(obj, 8, p)) + jj) + static_cast<u32>(k) * 0x28;
                BindModelData(rec, GetContainerItem(rs, k + 2));
                CreateModelInstance(GRef<u32>(GRef<u32>(Slot(obj, 0xC, p)) + jj) + static_cast<u32>(k) * 0x4C,
                                    GRef<u32>(GRef<u32>(Slot(obj, 8, p)) + jj) + static_cast<u32>(k) * 0x28);
            }
        }
    }
    GRef<u32>(obj + 0x48) |= 2;
}

// EE: 0x00147BF0
// Player aircraft models: per player (aircraft container S+0x1754 + p * 0x40) 4 LOD models (5 for
// headers from 0xB4) with instances, the extra item table (item 1), the skin GIMs of S+0x1758
// (skins 0 / 1 re-tagged to PSMT8H at TBP 0x0E00 / 0x2300, CBP 0x3F04 / 0x3F14, TBW from the
// width) and, unless S+0x175C is a 5-item set, the 4 cockpit models (items 3-6, item 2 kept);
// then the mission model sets and the per-player animation defaults.
void func_00147BF0(u32 obj) {
    GRef<u32>(obj + 0x00) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x04) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x30) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x34) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x14) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x44) = WorkHeapAlloc(8, 4);
    GRef<u32>(obj + 0x1C) = WorkHeapAlloc(2, 1);
    GRef<u32>(obj + 0x08) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x0C) = WorkHeapAlloc(8, 0x10);
    GRef<u32>(obj + 0x10) = WorkHeapAlloc(8, 0x10);
    guest::GuestFrame frame(0x20);
    const u32 r = frame.Address(), r2 = frame.Address(0x10);
    for (u32 p = 0; p < 2; ++p) {
        const u32 s = g_FlightScene() + p * 0x40;
        GRef<u32>(Slot(obj, 0x00, p)) = 0;
        GRef<u32>(Slot(obj, 0x04, p)) = 0;
        GRef<u32>(Slot(obj, 0x0C, p)) = 0;
        const u32 container = GRef<u32>(s + 0x1754);
        GRef<u32>(Slot(obj, 0x08, p)) = 0;
        GRef<u8>(GRef<u32>(obj + 0x1C) + p) = 0;
        GRef<u32>(Slot(obj, 0x34, p)) = 0;
        if (container == 0) continue;
        InitContainerReader(r, container);
        const s32 lods = GRef<u32>(GetContainerItem(r, 0)) < 0xB4 ? 4 : 5;
        GRef<u8>(GRef<u32>(obj + 0x1C) + p) = static_cast<u8>(lods);
        GRef<u32>(Slot(obj, 0x00, p)) = WorkHeapAlloc(lods * 0x4C, 0x10);
        const u32 recs = WorkHeapAlloc(lods * 0x28, 0x10);
        const u32 inst = GRef<u32>(Slot(obj, 0x00, p));
        GRef<u32>(Slot(obj, 0x04, p)) = recs;
        for (s32 i = 0; i < lods; ++i) {
            const u32 rec = static_cast<u32>(i) * 0x28 + recs;
            BindModelData(rec, GetContainerItem(r, i + 2));
            CreateModelInstance(static_cast<u32>(i) * 0x4C + inst, rec);
        }
        InitContainerReader(r2, GetContainerItem(r, 1));
        u32 t = WorkHeapAlloc(GRef<s32>(r2) * 4, 0x10);
        GRef<u32>(Slot(obj, 0x14, p)) = t;
        for (s32 i = 0; i < GRef<s32>(r2); ++i, t += 4) GRef<u32>(t) = GetContainerItem(r2, i);
        const u32 skins = GRef<u32>(g_FlightScene() + p * 0x40 + 0x1758);
        InitContainerReader(r, skins);
        if (GRef<s32>(skins) == 0) {
            GRef<u32>(Slot(obj, 0x30, p)) = 0;
            GRef<u32>(Slot(obj, 0x34, p)) = 0;
        } else {
            GRef<u32>(Slot(obj, 0x30, p)) = WorkHeapAlloc(GRef<s32>(skins) * 4, 0x10);
            GRef<u32>(Slot(obj, 0x34, p)) = WorkHeapAlloc(GRef<s32>(skins) * 0x28, 0x10);
            for (s32 i = 0; i < GRef<s32>(r); ++i) {
                const u32 tex = GRef<u32>(Slot(obj, 0x34, p)) + static_cast<u32>(i) * 0x28;
                GRef<u32>(GRef<u32>(Slot(obj, 0x30, p)) + 4 * static_cast<u32>(i)) = GetContainerItem(r, i);
                InitGimTexture(tex, GetContainerItem(r, i));
                if (i > 1) continue;
                const u32 hdr = GRef<u32>(tex + 4);
                GRef<u8>(hdr + 3) = 0x1B;
                GRef<u16>(hdr + 0) = i == 0 ? 0x0E00 : 0x2300;
                const u32 h2 = GRef<u32>(tex + 4);
                GRef<u8>(h2 + 2) = static_cast<u8>((static_cast<s32>(GRef<u16>(h2 + 0xC)) + 0x3F) >> 6);
                GRef<u16>(GRef<u32>(tex + 8)) = i == 0 ? 0x3F04 : 0x3F14;
            }
        }
        InitContainerReader(r, GRef<u32>(g_FlightScene() + p * 0x40 + 0x175C));
        GetContainerItem(r, 0);
        if (GRef<s32>(r) == 5) continue;
        GRef<u32>(Slot(obj, 0x08, p)) = WorkHeapAlloc(0x130, 0x10);
        GRef<u32>(Slot(obj, 0x0C, p)) = WorkHeapAlloc(0xA0, 0x10);
        for (s32 k = 0; k < 4; ++k) {
            const u32 off = static_cast<u32>(k) * 0x28;
            BindModelData(GRef<u32>(Slot(obj, 0x0C, p)) + off, GetContainerItem(r, k + 3));
            CreateModelInstance(GRef<u32>(Slot(obj, 0x08, p)) + static_cast<u32>(k) * 0x4C, GRef<u32>(Slot(obj, 0x0C, p)) + off);
        }
        GRef<u32>(Slot(obj, 0x10, p)) = GetContainerItem(r, 2);
        GRef<u32>(Slot(obj, 0x44, p)) = 1;
    }
    func_00149FB8(GRef<u32>(0x004106A8));
    GRef<u32>(obj + 0x3C) = 3;
    const u32 f = WorkHeapAlloc(8, 4);
    GRef<u32>(obj + 0x18) = f;
    GRef<u32>(f + 4) = 0;
    GRef<u32>(f + 0) = 0;
    for (u32 k = 0; k < 2; ++k) {
        GRef<u32>(obj + 0x70 + 4 * k) = 0x80000000;
        GRef<u32>(obj + 0x50 + 0x10 * k) = 0;
        GRef<u32>(obj + 0x54 + 0x10 * k) = 0;
        GRef<f32>(obj + 0x58 + 0x10 * k) = 1.0f;
        GRef<f32>(obj + 0x5C + 0x10 * k) = 1.0f;
        GRef<u32>(obj + 0x78 + 4 * k) = 0x80000000;
    }
    GRef<u32>(obj + 0x40) = 0;
}

// ---- Aircraft model animation -------------------------------------------------------------------

// EE: 0x00149C40
// Part value `channel` = v on every LOD instance of aircraft model n (none when it has no
// instances).
void func_00149C40(f32 v, u32 models, s32 n, s32 channel) {
    const u32 nn = 4 * static_cast<u32>(n);
    if (GRef<u32>(GRef<u32>(models) + nn) == 0) return;
    if (GRef<u8>(GRef<u32>(models + 0x1C) + static_cast<u32>(n)) == 0) return;
    for (u32 k = 0; k < GRef<u8>(GRef<u32>(models + 0x1C) + static_cast<u32>(n)); ++k)
        SetModelPartValue(v, GRef<u32>(GRef<u32>(models) + nn) + k * 0x4C, channel);
}

// EE: 0x00149D60
// Shows / hides node id `node` on every LOD instance of aircraft model n.
void func_00149D60(u32 models, s32 n, s32 node, s32 on) {
    for (u32 k = 0; k < GRef<u8>(GRef<u32>(models + 0x1C) + static_cast<u32>(n)); ++k)
        func_00141040(GRef<u32>(GRef<u32>(models) + 4 * static_cast<u32>(n)) + k * 0x4C, node, on);
}

// EE: 0x00149E38
// Aircraft model point n (+0x50 + n * 0x10) and value (+0x70 + n * 4).
void func_00149E38(u32 models, s32 n, u32 point, u32 value) {
    sceVu0CopyVector(models + static_cast<u32>(n) * 0x10 + 0x50, point);
    GRef<u32>(models + static_cast<u32>(n) * 4 + 0x70) = value;
}

// EE: 0x00149EB8
// Part value on the player aircraft models (*0x004106A4).
void func_00149EB8(s32 channel, s32 n, f32 v) {
    func_00149C40(v, GRef<u32>(0x004106A4), n, channel);
}

// EE: 0x0014AA38
// Value idx of table `which` (0: +0x40, 1: +0x44) of aircraft model n.
void func_0014AA38(u32 models, s32 n, s32 which, s32 idx, u32 value) {
    u32 table;
    if (which == 0) table = GRef<u32>(models + 0x40);
    else if (which == 1) table = GRef<u32>(models + 0x44);
    else return;
    GRef<u32>(GRef<u32>(table + 4 * static_cast<u32>(n)) + 4 * static_cast<u32>(idx)) = value;
}

// EE: 0x001206E0
// Moving parts of aircraft unit i (aircraft block 3, S+0x1760 + i * 0x40): per 0x60-byte record an
// initial value (+8) driven by up to 4 drivers (kind: flight value of the player state +0x240..
// +0x25C; input range +4..+0xC, output range +8..+0x10; op 0/1 set, 2 max, 3 min, 4 farthest from
// 0.5, 5 add, 6 scale), then each listed node (bytes +1.., count +0) moves towards it at rate +0xC
// (kind 7 of the last driver: continuous rotation; 9: heading; 8: altitude; reset: set). Node 15
// is left alone in mission kind 2; nodes 9 and 10 also drive channels 0x42 / 0x43.
// fpucheck: 1 add (yaw + k), 2 wrap, per driver 4, op 4: 2, op 5: 2, op 6: 2, kind 7: 1 + loop,
// rate: 1 sub + 1 add/sub.
void UpdateAircraftMovingParts(s32 i, s32 reset) {
    const u32 u = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(i));
    const u32 st = GRef<u32>(u + 0x44);
    u32 done = 0;
    const u32 block = GRef<u32>(g_FlightScene() + static_cast<u32>(i) * 0x40 + 0x1760);
    f32 alt = GRef<f32>(u + 4) * -1.5258789e-05f;
    if (1.0f < alt) alt = 1.0f;
    if (alt < 0.0f) alt = 0.0f;
    f32 heading = fpu::Div(fpu::Add(GRef<f32>(u + 0x14), GRef<f32>(0x0040DD9C)), GRef<f32>(0x0040DDA0));
    if (1.0f < heading) heading = fpu::Sub(heading, 1.0f);
    if (heading < 0.0f) heading = fpu::Add(heading, 1.0f);
    const s32 records = GRef<s32>(block + 4);
    if (records <= 0) return;
    u32 r = block + 8;
    s32 index = 0;
    auto stops = [](s16 k) { return k == 0 || k == 7 || k == 9 || k == 8; };
    while (GRef<s8>(r) != 0) {
        f32 value = GRef<f32>(r + 8);
        const f32 base = value;
        u32 d = r + 0x10;
        ++index;
        if (!stops(GRef<s16>(d))) {
            for (s32 k = 0;;) {
                f32 x;
                switch (GRef<s16>(d)) {
                    case 1: x = GRef<f32>(st + 0x250); break;
                    case 2: x = GRef<f32>(st + 0x258); break;
                    case 3: x = GRef<f32>(st + 0x254); break;
                    case 4: x = GRef<f32>(st + 0x248); break;
                    case 5: x = GRef<f32>(st + 0x24C); break;
                    case 6: x = GRef<f32>(st + 0x25C); break;
                    case 10: x = GRef<f32>(st + 0x240); break;
                    default: x = base; break;
                }
                const f32 lo = GRef<f32>(d + 4), hi = GRef<f32>(d + 0xC);
                if (lo != hi) {
                    const f32 range = fpu::Sub(hi, lo);
                    x = fpu::Div(fpu::Sub(x, lo), range);
                }
                if (1.0f < x) x = 1.0f;
                if (x < 0.0f) x = 0.0f;
                const f32 o0 = GRef<f32>(d + 8);
                x = fpu::Add(x * fpu::Sub(GRef<f32>(d + 0x10), o0), o0);
                if (1.0f < x) x = 1.0f;
                if (x < 0.0f) x = 0.0f;
                switch (static_cast<u16>(GRef<s16>(d + 2))) {
                    case 0: case 1: value = x; break;
                    case 2: if (value < x) value = x; break;
                    case 3: if (x < value) value = x; break;
                    case 4: {
                        // Soft-float double compare of |x - 0.5| and |value - 0.5| (exact).
                        const f32 a = fpu::Sub(x, 0.5f), b = fpu::Sub(value, 0.5f);
                        if (std::fabs(static_cast<double>(a)) > std::fabs(static_cast<double>(b))) value = x;
                        break;
                    }
                    case 5:
                        value = fpu::Sub(fpu::Add(value, x), base);
                        if (1.0f < value) value = 1.0f;
                        break;
                    case 6: value = fpu::Add(fpu::Sub(value, base) * x, base); break;
                    default: break;
                }
                if (1.0f < value) value = 1.0f;
                if (value < 0.0f) value = 0.0f;
                if (++k >= 4) break;
                d = r + 0x10 + static_cast<u32>(k) * 0x14;
                if (stops(GRef<s16>(d))) break;
            }
        }
        const s16 kind = GRef<s16>(d);
        for (s32 j = 0; j < GRef<s8>(r); ++j) {
            const s8 node = GRef<s8>(r + 1 + static_cast<u32>(j));
            if (((done >> (node & 0x1F)) & 1) != 0) continue;
            done |= 1u << (node & 0x1F);
            const u32 p = st + 0x1C0 + static_cast<u32>(static_cast<s32>(node)) * 4;
            if (kind == 7) {
                f32 v = fpu::Add(GRef<f32>(p), GRef<f32>(r + 0xC));
                GRef<f32>(p) = v;
                while (1.0f < v) {
                    v = fpu::Sub(GRef<f32>(p), 1.0f);
                    GRef<f32>(p) = v;
                }
            } else if (kind == 9) {
                GRef<f32>(p) = heading;
            } else if (kind == 8) {
                GRef<f32>(p) = alt;
            } else if (reset == 1) {
                GRef<f32>(p) = value;
            } else {
                const f32 rate = GRef<f32>(r + 0xC), cur = GRef<f32>(p);
                const f32 diff = fpu::Sub(value, cur);
                f32 v;
                if (rate < diff) v = fpu::Add(cur, rate);
                else if (diff < -rate) v = fpu::Sub(cur, rate);
                else v = fpu::Add(cur, diff);
                GRef<f32>(p) = v;
            }
            if (GRef<s8>(g_FlightScene() + 0x1653) == 2 && node == 0xF) continue;
            func_00149EB8(node, i, GRef<f32>(p));
            if (node == 9) func_00149EB8(0x42, i, GRef<f32>(st + 0x1E4));
            if (node == 10) func_00149EB8(0x43, i, GRef<f32>(st + 0x1E8));
        }
        if (index >= GRef<s32>(block + 4)) return;
        r += 0x60;
    }
}

// EE: 0x001481B8
// Aircraft model hierarchies: for the two player models n with an instance array, every instance
// A[0][n] + 0x4Ck (k < byte (A+0x1C)[n]) and the three instances (A+8)[n] + 0, 0x4C, 0x98.
void func_001481B8(u32 a) {
    guest::GuestFrame frame(0x90);
    const u32 m1 = frame.Address(), m2 = m1 + 0x40, z = m1 + 0x80;
    SetUnitMatrixVu0(m1);
    SetUnitMatrixVu0(m2);
    for (u32 n = 0; n < 2; ++n) {
        if (GRef<u32>(GRef<u32>(a) + 4 * n) == 0) continue;
        if (GRef<u8>(GRef<u32>(a + 0x1C) + n) != 0) {
            for (u32 k = 0;;) {
                GRef<u32>(z) = 0;
                const u32 inst = GRef<u32>(GRef<u32>(a) + 4 * n) + 0x4C * k;
                ++k;
                UpdateModelHierarchy(inst, z, m1, m2, 1);
                if (GRef<u8>(GRef<u32>(a + 0x1C) + n) <= k) break;
            }
        }
        for (u32 k = 0; k < 3; ++k) {
            GRef<u32>(z) = 0;
            UpdateModelHierarchy(GRef<u32>(GRef<u32>(a + 8) + 4 * n) + 0x4C * k, z, m1, m2, 1);
        }
    }
}

// EE: 0x00148308
// The 5th instance (A[0][n] + 0x130) of model n, when it exists (count > 4).
void func_00148308(u32 a, s32 n) {
    guest::GuestFrame frame(0x90);
    const u32 m1 = frame.Address(), m2 = m1 + 0x40, z = m1 + 0x80;
    SetUnitMatrixVu0(m1);
    SetUnitMatrixVu0(m2);
    const u32 inst = GRef<u32>(GRef<u32>(a) + 4 * static_cast<u32>(n));
    if (inst != 0 && GRef<u8>(GRef<u32>(a + 0x1C) + static_cast<u32>(n)) > 4) {
        GRef<u32>(z) = 0;
        UpdateModelHierarchy(inst + 0x130, z, m1, m2, 1);
    }
}

// ---- Aircraft shadow passes (docs/subsystems/rendering.md "Shadow-pass primitives") ----------

namespace {

// Packet writer for the shadow passes: w(off, v) writes a word at p + off.
struct PassWriter {
    u32 p;
    void W(u32 off, u32 v) const { GRef<u32>(p + off) = v; }
    void D(u32 off, u64 v) const { GRef<u64>(p + off) = v; }
    void Next(u32 n = 0x10) { p += n; }
    // DMA cnt k, 0, VIF FLUSH, DIRECT k; GIF 0x8000 | (k - 1), 0x10000000, 0xE (4th word unwritten).
    void AdList(u32 k) {
        W(4, 0); W(0, 0x10000000 | k); W(8, 0x11000000); W(0xC, 0x50000000 | k); Next();
        W(0, 0x8000 | (k - 1)); W(8, 0xE); W(4, 0x10000000); Next();
    }
};

s32 Columns(s32 w) { return (w + 63) / 64; }
u32 FramePage(s32 fbp) { return static_cast<u32>(fbp / 32); }

}  // namespace

// EE: 0x00146AB0
// Half-size copy of the page at tbp (w x h) into fbp (alpha channel only), nearest sampling.
void func_00146AB0(u32 ot, s32 b, u32 tbp, s32 w, s32 h, s32 fbp) {
    const s32 n = Columns(w), w2 = (w + 1) / 2, n2 = (n + 1) / 2, h2 = (h + 1) / 2;
    PassWriter q{BeginPacket()};
    q.AdList(11);
    q.W(8, 0x3F); q.Next();                                                    // TEXFLUSH
    q.W(0, 0x4010); q.W(8, 9); q.W(4, 0); q.Next();                            // CLAMP_2
    q.W(8, 0x4B); q.W(0, 0); q.Next();                                         // FBA_2
    q.W(0, FramePage(fbp) | static_cast<u32>(n2 << 16)); q.W(8, 0x4D);        // FRAME_2
    q.W(4, 0x00FFFFFF); q.W(0xC, 0); q.Next();
    q.W(8, 0x49); q.W(0, 0); q.Next();                                         // PABE
    q.W(0, static_cast<u32>((w2 - 1) << 16)); q.W(4, static_cast<u32>((h2 - 1) << 16)); q.W(8, 0x41); q.Next();
    q.W(0, 0x30003); q.W(8, 0x48); q.Next();                                   // TEST_2
    q.W(8, 0x19); q.W(0, 0); q.W(4, 0); q.Next();                              // XYOFFSET_2
    q.W(0, 0x01000000); q.W(4, 1); q.W(8, 0x4F); q.W(0xC, 0); q.Next();        // ZBUF_2
    q.W(0, 0); q.W(4, 0); q.W(8, 0x15); q.Next();                              // TEX1_2
    q.W(0, 0x10000003); q.W(4, 0); q.W(8, 0x11000000); q.W(0xC, 0x50000003); q.Next();
    q.W(0, 0x8001); q.W(4, 0x44000000); q.W(8, 0xF710); q.Next();
    q.D(0, 0x316); q.W(8, 0x80808080); q.W(0xC, 0x3F800000); q.Next();
    q.W(0, tbp | static_cast<u32>(n << 14) | 0xA8000000u); q.W(4, 6); q.Next();
    const u32 cnt = static_cast<u32>(2 * n + 1);
    q.W(0, cnt | 0x10000000); q.W(4, 0); q.W(8, 0); q.W(0xC, cnt | 0x50000000); q.Next();
    q.W(0, static_cast<u32>(n) | 0x8000); q.W(4, 0x44000000); q.W(8, 0x4343); q.Next();
    for (s32 j = 0; j < n; ++j) {
        const u32 u = 0x400 * static_cast<u32>(j), x = 0x200 * static_cast<u32>(j);
        q.W(0, (u + 8) | 0x80000);
        q.W(8, x); q.W(0xC, 0);
        q.Next();
        q.W(0, (u + 0x408) | (static_cast<u32>(h * 16 + 8) << 16));
        q.W(8, (x + 0x200) | (static_cast<u32>(h2) << 20)); q.W(0xC, 0);
        q.Next();
    }
    EndPacket(ot, b, (q.p + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x00146E28
// Blur step: the half-size page at tbp (w x h) scaled back up 2x into fbp, bilinear, shifted by
// (du, dv) sixteenths and added with FIX = fix.
void func_00146E28(u32 ot, s32 b, u32 tbp, s32 w, s32 h, s32 fbp, u32 fix, s32 du, s32 dv) {
    const s32 n = Columns(w), n2 = 2 * n;
    PassWriter q{BeginPacket()};
    q.AdList(12);
    q.W(8, 0x3F); q.Next();
    q.W(8, 9); q.W(0, static_cast<u32>(((w - 1) << 14) | 0xA)); q.W(4, static_cast<u32>((h - 1) << 2)); q.Next();
    q.W(8, 0x4B); q.W(0, 0); q.Next();
    q.W(0, FramePage(fbp) | static_cast<u32>(n << 17)); q.W(8, 0x4D); q.W(4, 0); q.W(0xC, 0); q.Next();
    q.W(8, 0x49); q.W(0, 0); q.Next();
    q.W(0, static_cast<u32>((2 * w - 1) << 16)); q.W(4, static_cast<u32>((2 * h - 1) << 16)); q.W(8, 0x41); q.Next();
    q.W(0, 0x3000D); q.W(8, 0x48); q.Next();
    q.W(8, 0x19); q.W(0, 0); q.W(4, 0); q.Next();
    q.W(0, 0x01000000); q.W(4, 1); q.W(8, 0x4F); q.W(0xC, 0); q.Next();
    q.W(0, 0x60); q.W(4, 0); q.W(8, 0x15); q.Next();
    q.W(0, 0x68); q.W(8, 0x43); q.W(4, fix); q.W(0xC, 0); q.Next();
    q.W(0, 0x10000003); q.W(4, 0); q.W(8, 0x11000000); q.W(0xC, 0x50000003); q.Next();
    q.W(0, 0x8001); q.W(4, 0x44000000); q.W(8, 0xF710); q.Next();
    q.D(0, 0x356); q.W(8, 0x80808080); q.W(0xC, 0x3F800000); q.Next();
    q.W(0, tbp | static_cast<u32>(n << 14) | 0xA8000000u); q.W(4, 6); q.Next();
    const u32 cnt = static_cast<u32>(4 * n + 1);
    q.W(0, cnt | 0x10000000); q.W(4, 0); q.W(8, 0); q.W(0xC, cnt | 0x50000000); q.Next();
    q.W(0, static_cast<u32>(n2) | 0x8000); q.W(4, 0x44000000); q.W(8, 0x4343); q.Next();
    for (s32 j = 0; j < n2; ++j) {
        const u32 u = 0x200 * static_cast<u32>(j), x = 0x400 * static_cast<u32>(j);
        q.W(0, (u + static_cast<u32>(du) + 8) | (static_cast<u32>(dv + 8) << 16));
        q.W(8, x); q.W(0xC, 0);
        q.Next();
        q.W(0, (u + static_cast<u32>(du) + 0x208) | (static_cast<u32>(h * 16 + dv + 8) << 16));
        q.W(8, (x + 0x400) | (static_cast<u32>(h) << 21)); q.W(0xC, 0);
        q.Next();
    }
    EndPacket(ot, b, (q.p + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x001471C8
// Darkens fbp by the texture at tbp (ALPHA_2 = (0 - Cd) * As + Cd); mode 1 adds the destination
// alpha test (DATE, DATM 1).
void func_001471C8(u32 ot, s32 b, s32 fbp, s32 w, s32 h, u32 tbp, u32 colour, s32 mode) {
    const s32 n = Columns(w);
    PassWriter q{BeginPacket()};
    q.AdList(11);
    q.W(8, 0x3F); q.Next();
    q.W(8, 9); q.W(0, 0x4010); q.W(4, 0); q.Next();
    q.W(8, 0x4B); q.W(0, 0); q.Next();
    q.W(0, FramePage(fbp) | static_cast<u32>(n << 16)); q.W(8, 0x4D); q.W(4, 0); q.W(0xC, 0); q.Next();
    q.W(8, 0x49); q.W(0, 0); q.Next();
    q.W(0, static_cast<u32>((w - 1) << 16)); q.W(4, static_cast<u32>((h - 1) << 16)); q.W(8, 0x41); q.Next();
    q.W(8, 0x48); q.W(0, mode != 0 ? 0x3C003 : 0x30003); q.Next();
    q.W(0, 0); q.W(4, 0); q.W(8, 0x19); q.Next();
    q.W(0, 0x01000000); q.W(4, 1); q.W(8, 0x4F); q.W(0xC, 0); q.Next();
    q.W(0, 0x46); q.W(4, 0); q.W(8, 0x43); q.W(0xC, 0); q.Next();
    q.W(0, 0x10000003); q.W(4, 0); q.W(8, 0x11000000); q.W(0xC, 0x50000003); q.Next();
    q.W(0, 0x8001); q.W(4, 0x44000000); q.W(8, 0xF710); q.Next();
    q.D(0, 0x356); q.W(8, colour); q.W(0xC, 0x3F800000); q.Next();
    q.W(0, tbp | static_cast<u32>(n << 14) | 0xA8000000u); q.W(4, 6); q.Next();
    const u32 cnt = static_cast<u32>(2 * n + 1);
    q.W(0, cnt | 0x10000000); q.W(4, 0); q.W(8, 0); q.W(0xC, cnt | 0x50000000); q.Next();
    q.W(0, static_cast<u32>(n) | 0x8000); q.W(4, 0x44000000); q.W(8, 0x4343); q.Next();
    for (s32 j = 0; j < n; ++j) {
        const u32 x = 0x400 * static_cast<u32>(j);
        q.W(0, (x + 8) | 0x80000);
        q.W(8, x); q.W(0xC, 0);
        q.Next();
        q.W(0, (x + 0x408) | (static_cast<u32>(h) << 20));
        q.W(8, (x + 0x400) | (static_cast<u32>(h) << 20)); q.W(0xC, 0);
        q.Next();
    }
    EndPacket(ot, b, (q.p + 0xF) & 0xFFFFFFF0u);
}

namespace {

// Shared body of func_00147540 / func_00147828: untextured sprites over fbp (w x h) with
// ALPHA_2 = 0x98 (Cs * Ad).
void ShadowFill(u32 ot, s32 b, s32 fbp, s32 w, s32 h, u32 mask, u32 prim, u32 colour) {
    const s32 n = Columns(w);
    PassWriter q{BeginPacket()};
    q.AdList(11);
    q.W(8, 0x3F); q.Next();
    q.W(0, 0x4010); q.W(8, 9); q.W(4, 0); q.Next();
    q.W(8, 0x4B); q.W(0, 0); q.Next();
    q.W(0, FramePage(fbp) | static_cast<u32>(n << 16)); q.W(8, 0x4D); q.W(4, mask); q.W(0xC, 0); q.Next();
    q.W(8, 0x49); q.W(0, 0); q.Next();
    q.W(0, static_cast<u32>((w - 1) << 16)); q.W(4, static_cast<u32>((h - 1) << 16)); q.W(8, 0x41); q.Next();
    q.W(0, 0x30003); q.W(8, 0x48); q.Next();
    q.W(8, 0x19); q.W(0, 0); q.W(4, 0); q.Next();
    q.W(0, 0x01000000); q.W(4, 1); q.W(8, 0x4F); q.W(0xC, 0); q.Next();
    q.W(0, 0x98); q.W(4, 0); q.W(8, 0x43); q.W(0xC, 0); q.Next();
    q.W(0, 0x10000002); q.W(4, 0); q.W(8, 0x11000000); q.W(0xC, 0x50000002); q.Next();
    q.W(0, 0x8001); q.W(4, 0x24000000); q.W(8, 0x10); q.Next();
    q.D(0, prim); q.W(8, colour); q.W(0xC, 0x3F800000); q.Next();
    const u32 cnt = static_cast<u32>(n + 1);
    q.W(0, cnt | 0x10000000); q.W(4, 0); q.W(8, 0); q.W(0xC, cnt | 0x50000000); q.Next();
    q.W(0, static_cast<u32>(n) | 0x8000); q.W(4, 0x24000000); q.W(8, 0x44); q.Next();
    for (s32 j = 0; j < n; ++j) {
        const u32 x = 0x400 * static_cast<u32>(j);
        q.W(0, x); q.W(4, 0);
        q.W(8, (x + 0x400) | (static_cast<u32>(h) << 20)); q.W(0xC, 0);
        q.Next();
    }
    EndPacket(ot, b, (q.p + 0xF) & 0xFFFFFFF0u);
}

}  // namespace

// EE: 0x00147540
// Blended white sprites over fbp (w x h): colour = Cs * Ad.
void func_00147540(u32 ot, s32 b, s32 fbp, s32 w, s32 h) { ShadowFill(ot, b, fbp, w, h, 0, 0x246, 0x80808080); }

// EE: 0x00147828
// Fills fbp (w x h) with colour (unblended), frame mask `mask`.
void func_00147828(u32 ot, s32 b, s32 fbp, s32 w, s32 h, u32 colour, u32 mask) {
    ShadowFill(ot, b, fbp, w, h, mask, 0x206, colour);
}

// EE: 0x00147B30
// Copies the work page back (func_00113B50) in one DIRECT packet into bucket b.
void func_00147B30(u32 ot, s32 b, u32 page, s32 w, s32 h) {
    const u32 p = BeginPacket();
    const u32 q = func_00113B50(p + 0x10, page, page, w, h);
    const u32 n = ((q - p + 0xF) >> 4) - 1;
    GRef<u32>(p + 4) = 0;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(p + 0xC) = n | 0x50000000;
    GRef<u32>(p + 0) = n | 0x10000000;
    EndPacket(ot, b, (q + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x0014A500
// Aircraft texture upload into bucket 9, once (A+0x48 bit 1).
void func_0014A500(u32 a, u32 ot) {
    if ((GRef<u32>(a + 0x48) & 2) == 0) return;
    const u32 p = BuildTextureUpload(a + 0x10, BeginPacket(), 1, 0);
    EndPacket(ot, 9, (p + 0xF) & 0xFFFFFFF0u);
    GRef<u32>(a + 0x48) &= ~2u;
}

// EE: 0x001418C8
// DrawAircraftShadow (docs/subsystems/rendering.md): finds the terrain under pos along the light
// direction l, builds a drop-shadow matrix there and draws instance I with VU1 program B into the
// frame alpha (FBMSK 0x00FFFFFF, ZMSK); TEST_1 Z GEQUAL when mode != 0. Returns 1 when drawn.
// fpucheck: 2 add (discarded distance)
s32 DrawAircraftShadow(u32 inst, u32 cam, u32 ot, s32 b, u32 pos, u32 ang, u32 l, s32 mode) {
    guest::GuestFrame frame(0x1A0);
    const u32 sp = frame.Address();
    const u32 pm = sp, m1 = sp + 0x40, m2 = sp + 0x80, hit = sp + 0xC0, e = sp + 0xD0, nrm = sp + 0xE0,
              d = sp + 0xF0, w1 = sp + 0x100, w2 = sp + 0x140, attr = sp + 0x180;
    sceVu0DropShadowMatrix(pm, l, 0.0f, GRef<f32>(0x0040E510), 0.0f, 0);
    sceVu0ScaleVector(e, l, -8192.0f);
    sceVu0AddVector(e, e, pos);
    if (func_0013D698(GRef<u32>(0x00410694), hit, attr, pos, e, nrm) == 0) return 0;
    GRef<f32>(hit + 0xC) = 1.0f;
    sceVu0SubVector(d, hit, pos);
    sceVu0MulVector(d, d, d);
    (void)fpu::Sqrt(fpu::Add(fpu::Add(GRef<f32>(d), GRef<f32>(d + 4)), GRef<f32>(d + 8)));  // unused
    const s32 kk = fpu::FloatToInt(GRef<f32>(cam + 0x168)) / 64 + 0x11;
    u32 s = 0;
    SetUnitMatrixVu0(m1);
    SetUnitMatrixVu0(m2);
    RotateMatrix(m2, m2, ang);
    sceVu0TransMatrix(m1, m1, hit);
    sceVu0MulMatrix(pm, pm, m2);
    sceVu0MulMatrix(pm, m1, pm);
    sceVu0MulMatrix(w1, cam + 0xE0, pm);
    sceVu0MulMatrix(w2, cam + 0x120, pm);
    AddBucketRef(ot, b, 0x00298AD0, GRef<u16>(0x00298AC0));
    u32 p = BeginPacket();
    auto wr = [&](u32 off, u32 v) { GRef<u32>(p + off) = v; };
    auto cp16 = [&](u32 src) {
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(p + o) = GRef<u32>(src + o);
    };
    wr(4, 0); wr(0, 0x10000005); wr(8, 0x11000000); wr(0xC, 0x50000005); p += 0x10;
    wr(4, 0x10000000); wr(8, 0xE); wr(0, 0x8004); p += 0x10;
    wr(8, 0x47); wr(0, mode != 0 ? 0x54003 : 0x34003); p += 0x10;
    wr(8, 0x42); wr(0, 0); wr(4, 0); wr(0xC, 0); p += 0x10;
    cp16(g_pMainContext().addr + 0xD0);
    wr(4, 0x00FFFFFF);
    p += 0x10;
    cp16(g_pMainContext().addr + 0xE0);
    wr(4, 1);
    p += 0x10;
    for (s32 j = 0; j < GRef<s32>(GRef<u32>(inst + 0x30) + 8); ++j) {
        const u32 flag = GRef<u32>(GRef<u32>(inst) + 4 * static_cast<u32>(j));
        if ((flag & 1) != 0 || (flag & 2) != 0) continue;
        const u32 desc = GRef<u32>(GRef<u32>(inst + 0x30) + 0x20) + 0x50 * static_cast<u32>(j);
        s |= (static_cast<u32>(kk) << 16) | 0x100;
        const u32 dtex = GRef<u32>(desc + 8);
        const u32 dcall = GRef<u32>(desc + 0x28);
        for (u32 n = 0; n < 2; ++n) {
            wr(4, 0); wr(0, 0x10000001); wr(8, 0x6E01C01D); wr(0xC, s); p += 0x10;
            wr(0, 0); wr(4, 0); wr(8, 0); wr(0xC, 0x14000000); p += 0x10;
        }
        wr(0, 0x50000000); wr(4, dcall); wr(8, 0); wr(0xC, 0); p += 0x10;
        const u32 f8 = GRef<u32>(dtex + 0xC) & 8;
        for (u32 n = 0; n < 2; ++n) {
            wr(4, 0); wr(0, 0x10000009); wr(8, 0); wr(0xC, 0x6C088000); p += 0x10;
            MulMatrixVu0(p, w1, GRef<u32>(inst + 0x10) + 0x40 * static_cast<u32>(j));
            p += 0x40;
            MulMatrixVu0(p, w2, GRef<u32>(inst + 0x10) + 0x40 * static_cast<u32>(j));
            p += 0x40;
            wr(0, 0); wr(4, 0); wr(8, 0); wr(0xC, 0x14000000); p += 0x10;
            wr(0, 0x10000001); wr(4, 0); wr(8, 0); wr(0xC, 0x6C01C015); p += 0x10;
            wr(0, 0); wr(4, 0x3042C000); wr(8, 0x412); p += 0x10;
            wr(0, 0x10000001); wr(4, 0); wr(8, 0); wr(0xC, 0x6C01C016); p += 0x10;
            wr(4, ((f8 != 0 ? 0x85u : 0x84u) << 15) | 0x30004000u); wr(8, 0x412); wr(0, 0); p += 0x10;
        }
        wr(0, 0x50000000);
        wr(4, GRef<u32>(GRef<u32>(inst + 0x2C) + 4 * static_cast<u32>(j)));
        wr(8, 0); wr(0xC, 0);
        p += 0x10;
    }
    wr(4, 0); wr(0, 0x10000003); wr(8, 0x11000000); wr(0xC, 0x50000003); p += 0x10;
    wr(0, 0x8002); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    cp16(g_pMainContext().addr + 0xD0);
    p += 0x10;
    cp16(g_pMainContext().addr + 0xE0);
    EndPacket(ot, b, p + 0x10);
    return 1;
}

// EE: 0x00149AC8
// Node matrix by id (docs/subsystems/rendering.md): searches instance 0 of model n (cached in
// A+0x28+4n), then the 5th instance (A[0][n] + 0x130) when there are at least 5; m unchanged when
// the id is absent.
void func_00149AC8(u32 a, u32 m, s32 id, s32 n) {
    const u32 cache = a + 0x28 + 4 * static_cast<u32>(n);
    auto search = [&](u32 inst) {
        GRef<u32>(cache) = inst;
        for (s32 k = 0; k < GRef<s32>(GRef<u32>(GRef<u32>(cache) + 0x30) + 8); ++k) {
            const u32 c = GRef<u32>(cache);
            if (GRef<s32>(GRef<u32>(c + 8) + 4 * static_cast<u32>(k)) == id) {
                sceVu0CopyMatrix(m, GRef<u32>(c + 0x10) + 0x40 * static_cast<u32>(k));
                return true;
            }
        }
        return false;
    };
    if (search(GRef<u32>(GRef<u32>(a) + 4 * static_cast<u32>(n)))) return;
    if (GRef<u8>(GRef<u32>(a + 0x1C) + static_cast<u32>(n)) > 4)
        search(GRef<u32>(GRef<u32>(a) + 4 * static_cast<u32>(n)) + 0x130);
}

// EE: 0x00149EC8
// func_00149AC8 on the aircraft models *0x004106A4 (the original also passes s8 P+0x149 == 1 in t0,
// which func_00149AC8 does not read).
void func_00149EC8(u32 m, s32 n, s32 id, u32 player) {
    (void)player;
    func_00149AC8(GRef<u32>(0x004106A4), m, id, n);
}

// EE: 0x0014A588
// Weapons on the view owner's aircraft (docs/subsystems/rendering.md "Weapon models"): the two
// pylon-node items (ids 0x32 / 0x33) and the k-th weapon set's items, bucket 9.
void func_0014A588(u32 mdl, u32 v, u32 ot, s32 k, s32 lod0) {
    guest::GuestFrame frame(0xE0);
    const u32 sp = frame.Address();
    const u32 nm = sp, m0 = sp + 0x40, m1 = sp + 0x80, z = sp + 0xC0, q = sp + 0xD0;
    const u32 pl = GRef<u8>(v + 6);
    const u32 cam = (GRef<u8>(v + 7) & 0x80) != 0 ? v + 0x270 : v + 0xB0;
    WriteLightSetPacket(GRef<u32>(0x00410688), cam, ot, 9, 0);
    u32 p = BeginPacket();
    auto wr = [&](u32 off, u32 val) { GRef<u32>(p + off) = val; };
    wr(4, 0); wr(0, 0x10000003); wr(8, 0); wr(0xC, 0x50000003); p += 0x10;
    wr(4, 0x10000000); wr(0, 0x8002); wr(8, 0xE); p += 0x10;
    wr(8, 0x42); wr(0, 0); wr(4, 0); wr(0xC, 0); p += 0x10;
    const u32 unit = GRef<u32>(0x002A1F70 + 4 * pl);
    wr(0, 0); wr(4, 0); wr(8, 8); p += 0x10;
    sceVu0UnitMatrix(m1);
    func_00102D28(m1, m1, unit + 0x10);
    if ((GRef<u8>(v + 7) & 0x80) != 0)
        sceVu0CopyMatrix(m0, m1);
    else
        sceVu0TransMatrix(m0, m1, unit);
    const s32 c = (GRef<u8>(v + 7) & 3) == 2 ? 1 : 0;
    (void)c;  // passed to func_00149AC8 in t0, which does not read it
    p = WriteTex0Tex1(mdl + 0x10, p);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(z + o) = 0;  // memset
    GRef<f32>(z + 0xC) = 1.0f;
    s32 lod = 0;
    auto pylon = [&](u32 slot, s32 id) {
        if (GRef<s32>(GRef<u32>(GRef<u32>(mdl + 0x40) + 4 * pl) + 4 * slot) != 1) return;
        lod = lod0;
        func_00149AC8(GRef<u32>(0x004106A4), nm, id, static_cast<s32>(pl));
        sceVu0MulMatrix(nm, m0, nm);
        sceVu0ApplyMatrix(q, nm, z);
        if (2048.0f < func_00104F10(z, q)) lod = 1;
        const u32 inst = GRef<u32>(GRef<u32>(GRef<u32>(GRef<u32>(0x004106A8) + 0xC) + 4 * pl));
        p = EmitModelInstance(inst + 0x4C * static_cast<u32>(lod), p, cam, nm, nm, 0xC);
    };
    pylon(0, 0x32);
    pylon(1, 0x33);
    const s32 sets = GRef<s32>(GRef<u32>(mdl + 0x3C) + 4 * pl);
    if (k < sets - 1 && sets >= 2) {
        const u32 b = GRef<u32>(GRef<u32>(unit + 0x44) + 0x110) + 4;
        s32 count = 0, first = 0x34;
        if (k == 0) {
            count = GRef<s8>(b + 1);
            first = 0x34;
        } else if (k == 1) {
            count = GRef<s8>(b + 5);
            first = 0x3A;
        } else if (k == 2) {
            count = GRef<s8>(b + 9);
            first = 0x3E;
        }
        const u32 off = 0x4C * static_cast<u32>(lod);
        for (s32 j = 0; j < count; ++j) {
            if (GRef<u32>(GRef<u32>(GRef<u32>(mdl + 0x44) + 4 * pl) + 4 * static_cast<u32>(j)) == 0) continue;
            func_00149AC8(GRef<u32>(0x004106A4), nm, first + j, static_cast<s32>(pl));
            sceVu0MulMatrix(nm, m0, nm);
            const u32 set = GRef<u32>(GRef<u32>(GRef<u32>(0x004106A8) + 0xC) + 4 * pl);
            p = EmitModelInstance(GRef<u32>(set + 4 * static_cast<u32>(k) + 4) + off, p, cam, nm, nm, 0xC);
        }
    }
    EndPacket(ot, 9, (p + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x0014AA78
// Missile model (bucket 9): clip test of pos, LOD 1 beyond 512, near flag below 1024. The clip
// matrix is loaded into VU0 right before its test.
void func_0014AA78(u32 mdl, u32 cam, u32 ot, u32 angles, u32 pos, s32 slot, s32 lod) {
    guest::GuestFrame frame(0xD0);
    const u32 sp = frame.Address();
    const u32 cm = sp, m0 = sp + 0x40, m1 = sp + 0x80, out = sp + 0xC0;
    func_00136990(-32.0f, GRef<f32>(GRef<u32>(0x00410694) + 0xA4), cm, cam);
    func_001367B0(cm);
    if (func_001367C8(pos, out) == 1) return;
    const f32 w = GRef<f32>(out + 0xC);
    const u32 f = w < 1024.0f ? 4u : 0u;
    if (512.0f < w) lod = 1;
    WriteLightSetPacket(GRef<u32>(0x00410688), cam, ot, 9, 0);
    u32 p = BeginPacket();
    GRef<u32>(p + 4) = 0;
    GRef<u32>(p + 0) = 0x10000002;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(p + 0xC) = 0x50000002;
    GRef<u32>(p + 0x10) = 0x8001;
    GRef<u32>(p + 0x14) = 0x10000000;
    GRef<u32>(p + 0x18) = 0xE;
    GRef<u32>(p + 0x20) = 0;
    GRef<u32>(p + 0x24) = 0;
    GRef<u32>(p + 0x28) = 0x3B;
    p = WriteTex0Tex1(mdl + 0x10, p + 0x30);
    sceVu0UnitMatrix(m1);
    func_00102D28(m1, m1, angles);
    sceVu0TransMatrix(m0, m1, pos);
    u32 inst;
    if (slot < 2)
        inst = GRef<u32>(GRef<u32>(GRef<u32>(mdl + 0xC) + 4 * static_cast<u32>(slot)));
    else
        inst = GRef<u32>(GRef<u32>(mdl + 4));
    p = EmitModelInstance(inst + 0x4C * static_cast<u32>(lod), p, cam, m0, m1, f | 8);
    EndPacket(ot, 9, (p + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x0014ACB0
// Special-weapon model idx of slot (bucket 9); nothing when idx >= M[0xF][slot] - 1 or clipped.
void func_0014ACB0(u32 mdl, u32 cam, u32 ot, u32 angles, u32 pos, s32 slot, s32 lod, s32 idx) {
    if (!(idx < GRef<s32>(GRef<u32>(mdl + 0x3C) + 4 * static_cast<u32>(slot)) - 1)) return;
    guest::GuestFrame frame(0xD0);
    const u32 sp = frame.Address();
    const u32 cm = sp, m0 = sp + 0x40, m1 = sp + 0x80, out = sp + 0xC0;
    func_00136990(-32.0f, GRef<f32>(GRef<u32>(0x00410694) + 0xA4), cm, cam);
    func_001367B0(cm);
    if (func_001367C8(pos, out) == 1) return;
    const u32 f = GRef<f32>(out + 0xC) < 1024.0f ? 4u : 0u;
    WriteLightSetPacket(GRef<u32>(0x00410688), cam, ot, 9, 0);
    u32 p = WriteTex0Tex1(mdl + 0x10, BeginPacket());
    sceVu0UnitMatrix(m1);
    func_00102D28(m1, m1, angles);
    sceVu0TransMatrix(m0, m1, pos);
    const u32 set = GRef<u32>(GRef<u32>(mdl + 0xC) + 4 * static_cast<u32>(slot));
    const u32 inst = GRef<u32>(set + 4 * static_cast<u32>(idx) + 4);
    p = EmitModelInstance(inst + 0x4C * static_cast<u32>(lod), p, cam, m0, m1, f | 8);
    EndPacket(ot, 9, (p + 0xF) & 0xFFFFFFF0u);
}

namespace {

// "TEX2 pair (t)": twice [DMA cnt 1, UNPACK 0x6C01C01C] [TEX2_1 = CLUT of GIM record t] [DMA cnt 0,
// MSCAL 0]; the TEX2 qword's 4th word is unwritten.
u32 Tex2Pair(u32 p, u32 tex) {
    const u32 clut = GRef<u32>(tex + 8), img = GRef<u32>(tex + 4);
    const u32 cbp = static_cast<u32>(GRef<u16>(clut)) << 5;
    for (u32 n = 0; n < 2; ++n) {
        GRef<u32>(p + 0x00) = 0x10000001;
        GRef<u32>(p + 0x04) = 0;
        GRef<u32>(p + 0x08) = 0;
        GRef<u32>(p + 0x0C) = 0x6C01C01C;
        GRef<u32>(p + 0x10) = static_cast<u32>(GRef<u8>(img + 3)) << 20;
        GRef<u32>(p + 0x18) = 0x16;
        GRef<u32>(p + 0x14) = cbp | (static_cast<u32>(GRef<u8>(clut + 3)) << 19) | 0x20000000;
        GRef<u32>(p + 0x28) = 0;
        GRef<u32>(p + 0x20) = 0x10000000;
        GRef<u32>(p + 0x24) = 0;
        GRef<u32>(p + 0x2C) = 0x14000000;
        p += 0x30;
    }
    return p;
}

// Byte +0x27 / +0 test shared by the shadow pass: the destination-alpha variant.
bool ShadowVariant() {
    const u32 gs = g_GameState().addr;
    const u32 scene = GRef<u8>(gs + 0x27);
    if (scene < 0x13 && ((0x00204114u >> (GRef<u8>(gs) & 31)) & 1) != 0) return true;
    return scene - 0x3D < 0x12;
}

}  // namespace

// EE: 0x001483A0
// Player aircraft of a view (docs/subsystems/rendering.md "func_001483A0 in full"): texture upload,
// cockpit clip test, ground shadow and its passes, light set, the external model or cockpit (VU1
// program E or C by the view mode), node switches, weapons, effects (S5f) and the glow pass.
void func_001483A0(u32 a, u32 v, u32 ot, s32 k, u32 ot2) {
    guest::GuestFrame frame(0x130);
    const u32 sp = frame.Address();
    const u32 m0 = sp + 0x10, mw = sp + 0x50, vec = sp + 0x90, ma = sp + 0xA0, mb = sp + 0xE0, z = sp + 0x120;
    const u32 pl = GRef<u8>(v + 6);
    const u32 p4 = 4 * pl;
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(vec + o) = GRef<u32>(0x00404730 + o);
    const u32 unit = GRef<u32>(0x002A1F70 + p4);
    const u32 m = GRef<u8>(v + 7) & 3;
    const u32 tex = GRef<u32>(GRef<u32>(a + 0x34) + p4);
    u32 g = 0;
    const u32 sset = GRef<u32>(0x00410688);
    // 1. Texture upload (once).
    if ((GRef<u32>(GRef<u32>(a + 0x44) + p4) & 1) != 0) {
        u32 p = BuildTextureUpload(tex, BeginPacket(), 1, 0);
        p = BuildTextureUpload(tex + 0x28, p, 1, 0);
        EndPacket(ot, 8, p);
        GRef<u32>(GRef<u32>(a + 0x44) + p4) &= 0xFFFFFFFEu;
    }
    // 2. Cockpit clip test of the aircraft position.
    const u32 cam = v + 0xB0;
    if (m == 3) {
        BuildClipMatrix(ma, fpu::Add(GRef<f32>(cam + 0x160), GRef<f32>(cam + 0x160)),
                        fpu::Add(GRef<f32>(cam + 0x164), GRef<f32>(cam + 0x164)), GRef<f32>(cam + 0x168), 40.0f,
                        GRef<f32>(cam + 0x170));
        sceVu0MulMatrix(mb, ma, v + 0xD0);
        // vmula/vmadd chain into vf8, clip flags cleared, vclipw.xyz: g = no clip bit set.
        auto col = [&](u32 c) {
            return vu0::Vec4{GRef<f32>(c), GRef<f32>(c + 4), GRef<f32>(c + 8), GRef<f32>(c + 12)};
        };
        const vu0::Vec4 q = vu0::MulMatrix(col(mb), col(mb + 0x10), col(mb + 0x20), col(mb + 0x30), col(unit));
        const f32 w = std::fabs(q.w);
        const bool clipped = q.x > w || q.x < -w || q.y > w || q.y < -w || q.z > w || q.z < -w;
        g = clipped ? 0u : 1u;
    }
    // 3. Ground shadow.
    if ((GRef<u32>(GRef<u32>(a + 0x18) + p4) & 2) == 0) {
        const u32 l = mb;
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(l + o) = 0;  // memset
        GRef<f32>(l + 8) = 1.0f;
        sceVu0UnitMatrix(ma);
        sceVu0RotMatrixX(ma, ma, GRef<f32>(GRef<u32>(0x00410694) + 0x88));
        sceVu0RotMatrixY(ma, ma, GRef<f32>(GRef<u32>(0x00410694) + 0x8C));
        sceVu0ApplyMatrix(l, ma, l);
        sceVu0Normalize(l, l);
        func_001132E0(ot, 8, 0);
        const s32 s = ShadowVariant() ? 1 : 0;
        s32 r = DrawAircraftShadow(GRef<u32>(GRef<u32>(a + 8) + p4), cam, ot, 8, unit, unit + 0x10, l, s);
        r |= DrawAircraftShadow(GRef<u32>(GRef<u32>(a) + p4) + 0x130, cam, ot, 8, unit, unit + 0x10, l, s);
        if (r != 0) {
            func_00146AB0(ot, 8, 0, 0x200, 0x1C0, 0x2500);
            func_00147540(ot, 8, 0x2500, 0x100, 0xE0);
            func_00147828(ot, 8, 0x2880, 0x200, 0x1C0, 0, 0);
            func_00146E28(ot, 8, 0x2500, 0x100, 0xE0, 0x2880, 0x2A, 0x10, 0);
            func_00146E28(ot, 8, 0x2500, 0x100, 0xE0, 0x2880, 0x2B, 0, 0x18);
            func_00146E28(ot, 8, 0x2500, 0x100, 0xE0, 0x2880, 0x2B, 0x20, 0x18);
            func_00147B30(ot, 8, 0x2880, 0x200, 0x1C0);
            if (ShadowVariant()) {
                func_00147828(ot, 8, 0, 0x200, 0x1C0, 0, 0x00FFFFFF);
                if (GRef<f32>(unit + 4) < -90.0f) func_00145578(GRef<u32>(0x0041069C), cam, ot);
                func_001471C8(ot, 8, 0, 0x200, 0x1C0, 0x2880, 0x28808080, 1);
            } else {
                func_001471C8(ot, 8, 0, 0x200, 0x1C0, 0x2880, 0x28808080, 0);
            }
        }
    }
    // 4. Light set; the model needs a cockpit-type view and both instance tables.
    WriteLightSetPacket(sset, cam, ot, 8, 1);
    if ((GRef<u32>(unit + 0x4C) & 0x20000000) == 0 || m == 0 || m == 1) return;
    const u32 i0 = GRef<u32>(GRef<u32>(a) + p4);
    if (i0 == 0 || GRef<u32>(GRef<u32>(a + 0x30) + p4) == 0) return;
    const u32 c = (GRef<u8>(v + 7) & 0x80) != 0 ? v + 0x270 : cam;
    u32 inst, j;
    s32 b;
    u32 prog;
    if (m == 2) {
        j = i0 + 0x4C;
        inst = i0 + 0x98;
        b = 0xE;
        prog = 0x0029B0F0;
    } else {
        b = 8;
        inst = i0;
        j = GRef<u8>(GRef<u32>(a + 0x1C) + pl) == 5 ? i0 + 0x130 : 0;
        prog = 0x00299820;
    }
    AddBucketRef(ot, b, prog + 0x10, GRef<u16>(prog));
    WriteLightSetPacket(GRef<u32>(0x00410688), c, ot, b, 1);
    func_0013ED58(c, ot, b, 0);
    u32 p = BeginPacket();
    auto wr = [&](u32 off, u32 val) { GRef<u32>(p + off) = val; };
    auto cp16 = [&](u32 src) {
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(p + o) = GRef<u32>(src + o);
    };
    wr(4, 0); wr(0, 0x10000003); wr(8, 0); wr(0xC, 0x50000003); p += 0x10;
    wr(0, 0x8002); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    wr(0, 0xFF001FFF); wr(8, 8); wr(4, 1); p += 0x10;
    wr(0, 0); wr(4, 0); wr(8, 0x3B);
    EndPacket(ot, b, p + 0x10);
    // 5. Node switches.
    if (m == 2) {
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(mb + o) = 0;  // memset
        GRef<f32>(mb + 8) = 1.0f;
        sceVu0UnitMatrix(ma);
        func_00102D28(ma, ma, v + 0x40);
        sceVu0ApplyMatrix(mb, ma, mb);
        const f32 ez = GRef<f32>(mb + 8);
        u64 f = 0;
        if (ez < -0.5f) f = 1;
        if (0.5f < ez) f |= 2;
        func_00140408(inst, f);
        func_00140408(j, f);
    }
    const bool lit = (GRef<u32>(GRef<u32>(0x00410694) + 0xF4) & 0x20) != 0;
    func_00141040(inst, 0x1E, lit ? 0 : 1);
    func_00141040(inst, 0x25, lit ? 1 : 0);
    if (j != 0) {
        func_00141040(j, 0x1E, lit ? 0 : 1);
        func_00141040(j, 0x25, lit ? 1 : 0);
    }
    auto ids = [&](s32 first, s32 count, s32 on) {
        for (s32 n = 0; n < count; ++n) {
            func_00141040(inst, first + n, on);
            if (j != 0) func_00141040(j, first + n, on);
        }
    };
    ids(0x34, 6, k != 0 ? 1 : 0);
    ids(0x3A, 4, k != 1 ? 1 : 0);
    ids(0x3E, 4, k != 2 ? 1 : 0);
    func_0014A588(GRef<u32>(0x004106A8), v, ot, k, m != 2 ? 1 : 0);
    // 6. The model.
    const u32 f0 = m == 2 ? 4u : ((static_cast<u32>(GRef<u8>(v + 7)) >> 4) & 4u);
    p = BeginPacket();
    wr(4, 0); wr(0, 0x10000009); wr(8, 0); wr(0xC, 0x50000009); p += 0x10;
    wr(0, 0x8008); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    cp16(g_pMainContext().addr + 0x150); p += 0x10;
    cp16(g_pMainContext().addr + 0x170); p += 0x10;
    cp16(c + 0x1B0); p += 0x10;
    cp16(g_pMainContext().addr + 0x160); p += 0x10;
    wr(0, 0x60); wr(8, 0x15); wr(4, 0); p += 0x10;
    wr(0, 0x58); wr(8, 0x43); wr(4, 0); wr(0xC, 0); p += 0x10;
    wr(0, 0); wr(8, 9); wr(4, 0); p += 0x10;
    wr(0, 0x50000); wr(8, 0x48); p += 0x10;
    wr(0, 0x10000003); wr(4, 0); wr(8, 0); wr(0xC, 0x50000003); p += 0x10;
    wr(0, 0x8002); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    wr(0, 0x44); wr(8, 0x42); wr(4, 0); wr(0xC, 0); p += 0x10;
    wr(0, 0x60); wr(8, 0x14); wr(4, 0); p += 0x10;
    wr(0, 0x10000003); wr(4, 0); wr(8, 0); wr(0xC, 0x50000003); p += 0x10;
    wr(0, 0x8002); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    wr(0, 0x5000D); wr(8, 0x47); p += 0x10;
    wr(0, 0); wr(4, 0); wr(8, 8); p += 0x10;
    sceVu0UnitMatrix(m0);
    if (m == 2) {
        sceVu0ScaleVector(ma, v + 0x30, -15.0f);
        sceVu0TransMatrix(m0, m0, ma);
    }
    func_00102D28(m0, m0, unit + 0x10);
    if ((GRef<u8>(v + 7) & 0x80) != 0)
        sceVu0CopyMatrix(mw, m0);
    else
        sceVu0TransMatrix(mw, m0, unit);
    sceVu0UnitMatrix(ma);
    func_00102D28(ma, ma, unit + 0x10);
    sceVu0ApplyMatrix(vec, ma, vec);
    auto light2 = [&]() {
        if (m == 3) {
            SetLight(sset, a + 0x50 + 0x10 * pl, GRef<u32>(GRef<u32>(a + 0x70) + p4), 2);
        } else {
            func_00136B68(0.0f, 0.0f, sset, 0, 2);
        }
        p = func_00137318(sset, p);
    };
    p = Tex2Pair(p, tex);
    wr(4, 0); wr(0, 0x10000002); wr(8, 0); wr(0xC, 0x50000002); p += 0x10;
    wr(0, 0x8001); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    wr(4, 0); wr(0xC, 0); wr(0, 0x58); wr(8, 0x43);
    p = WriteTex0Tex1(tex, p + 0x10);
    light2();
    p = EmitModelInstance(inst, p, c, mw, m0, f0 | 8);
    auto jlight = [&]() {
        if ((GRef<u32>(GRef<u32>(0x00410694) + 0xF4) & 0x20) != 0) {
            SetLight(sset, vec, GRef<u32>(GRef<u32>(a + 0x78) + p4), 2);
            p = func_00137318(sset, p);
        }
    };
    if (j != 0) {
        if (m == 2) {
            p = Tex2Pair(p, tex + 0x28);
            p = WriteTex0Tex1(tex + 0x28, p);
            jlight();
        }
        p = EmitModelInstance(j, p, c, mw, m0, f0 | 8);
    }
    func_00136B68(0.0f, 0.0f, sset, 0, 2);
    p = func_00137318(sset, p);
    EndPacket(ot, b, (p + 0xF) & 0xFFFFFFF0u);
    // 7. Effects on the model's nodes (slice S5f).
    if ((GRef<u32>(a + 0x40) & 1) == 0) {
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(z + o) = 0;  // memset
        GRef<f32>(z + 0xC) = 1.0f;
        func_00155900(c);
        auto effects = [&](u32 ins) {
            const u32 table = GRef<u32>(ins + 0xC);
            const s32 count = GRef<s32>(GRef<u32>(ins + 0x30) + 8);
            u32 node = GRef<u32>(ins + 0x10);
            for (s32 n = 0; n < count; ++n, node += 0x40) {
                const u32 bt = GRef<u8>(table + static_cast<u32>(n));
                if (bt == 0xFF) continue;
                const u32 o = GRef<u32>(GRef<u32>(GRef<u32>(a + 0x14) + p4) + 4 * bt);
                sceVu0UnitMatrix(mb);
                func_00102D28(mb, mb, unit + 0x10);
                if ((GRef<u8>(v + 7) & 0x80) == 0) sceVu0TransMatrix(mb, mb, unit);
                MulMatrixVu0(mb, mb, node);
                sceVu0ApplyMatrix(o, mb, z);
                func_001E0A98(c, ot2, o);
            }
        };
        effects(GRef<u32>(GRef<u32>(a) + p4));
        if (GRef<u8>(GRef<u32>(a + 0x1C) + pl) == 5) effects(GRef<u32>(GRef<u32>(a) + p4) + 0x130);
        func_00155900(cam);
    }
    // 8. Second and third passes (ALPHA_2 FIX from T+0xB5 / T+0xB6).
    const u32 t = GRef<u32>(0x00410694);
    p = BeginPacket();
    p = Tex2Pair(p, tex);
    wr(4, 0); wr(0, 0x10000002); wr(8, 0x11000000); wr(0xC, 0x50000002); p += 0x10;
    wr(0, 0x8001); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    wr(0, 0x68); wr(8, 0x43); wr(4, GRef<u8>(t + 0xB5)); wr(0xC, 0); p += 0x10;
    if (m == 3) {
        light2();
    } else {
        light2();
        wr(4, 0); wr(0, 0x10000003); wr(8, 0); wr(0xC, 0x50000003); p += 0x10;
        wr(0, 0x8002); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
        cp16(g_pMainContext().addr + 0xE0); wr(4, 1); p += 0x10;
        cp16(g_pMainContext().addr + 0x160); wr(4, 1); p += 0x10;
    }
    p = EmitModelInstance(inst, p, c, mw, m0, f0 | 0xA);
    p = EmitModelInstance(inst, p, c, mw, m0, f0 | 9);
    wr(4, 0); wr(0, 0x10000002); wr(8, 0); wr(0xC, 0x50000002); p += 0x10;
    wr(0, 0x8001); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
    wr(0, 0x68); wr(8, 0x43); wr(4, GRef<u8>(GRef<u32>(0x00410694) + 0xB6)); wr(0xC, 0); p += 0x10;
    if (j != 0) {
        if (m == 2) {
            p = Tex2Pair(p, tex + 0x28);
            jlight();
        }
        p = EmitModelInstance(j, p, c, mw, m0, f0 | 0xA);
        p = EmitModelInstance(j, p, c, mw, m0, f0 | 9);
    }
    func_00136B68(0.0f, 0.0f, sset, 0, 2);
    p = func_00137318(sset, p);
    if (m == 2) {
        wr(4, 0); wr(0, 0x10000003); wr(8, 0); wr(0xC, 0x50000003); p += 0x10;
        wr(0, 0x8002); wr(4, 0x10000000); wr(8, 0xE); p += 0x10;
        cp16(g_pMainContext().addr + 0xE0); p += 0x10;
        cp16(g_pMainContext().addr + 0x160); p += 0x10;
    }
    EndPacket(ot, b, (p + 0xF) & 0xFFFFFFF0u);
    // 9. Glow pass (cockpit clip test passed, T+0xF4 bit 1, not disabled by A+0x18 bit 0).
    if ((GRef<u32>(GRef<u32>(0x00410694) + 0xF4) & 2) == 0) g = 0;
    if ((GRef<u32>(GRef<u32>(a + 0x18) + p4) & 1) != 0) g = 0;
    if (g != 0) {
        sceVu0UnitMatrix(m0);
        func_00102D28(m0, m0, unit + 0x10);
        if ((GRef<u8>(v + 7) & 0x80) != 0)
            sceVu0CopyMatrix(mw, m0);
        else
            sceVu0TransMatrix(mw, m0, unit);
        func_00142460(GRef<u32>(GRef<u32>(a) + p4) + 0xE4, c, ot, b, mw, 0x2500, 0x2A00, m0,
                      GRef<u8>(GRef<u32>(0x00410694) + 0xB4));
    }
    p = BeginPacket();
    wr(4, 0); wr(0, 0x10000000); wr(8, 0); wr(0xC, 0x11000000);
    EndPacket(ot, b, p + 0x10);
    func_00136B68(0.0f, 0.0f, sset, 0, 2);
}

// EE: 0x0011E0D8
// Units and aircraft of one view (docs/subsystems/rendering.md): shared-model texture, unit LODs and
// models, the view owner's aircraft; mission 10/11 scene 0x19 extra effect per matching unit
// (0x001BCBD8, slice S5f).
void func_0011E0D8(u32 v, u32 ot, u32 ot2) {
    const s32 k = GRef<u8>(v + 6) < 2
                      ? GRef<s8>(GRef<u32>(GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(GRef<u8>(v + 6))) + 0x44) + 0x172)
                      : 3;
    func_0014A500(GRef<u32>(0x004106A8), ot);
    SelectUnitModelLods(GRef<u32>(0x004106A0), v);
    func_00146268(GRef<u32>(0x004106A0), v + 0xB0, ot, ot2);
    func_001483A0(GRef<u32>(0x004106A4), v, ot, k, ot2);
    if (GRef<u8>(g_GameState().addr + 0x27) != 0x19) return;
    if (static_cast<u32>(Ctx()->sub) - 10 >= 2) return;
    for (s32 u = 0; u < GRef<s32>(GRef<u32>(0x00410640)); ++u) {
        const u32 unit = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(u));
        const u32 f = GRef<u32>(unit + 0x4C);
        if ((f & 0x10000000) == 0 || (f & 0x1000000) == 0) continue;
        if (GRef<u8>(unit + 0x69) != 0xB2) continue;
        func_001BCBD8(v + 0xB0, ot2, unit, 500.0f);
    }
}

namespace {

// One quadword (lq / sq).
void GlowCopyQword(u32 dst, u32 src) {
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(dst + o) = GRef<u32>(src + o);
}

}  // namespace
// EE: 0x00141EE0
// Composite packet of the half-resolution glow pass (docs/subsystems/rendering.md): DMA cnt + DIRECT of
// 2 x ceil(w / 64) + 12 qwords: GIF tag (NLOOP 7), ZBUF from the main context with ZMSK, XYOFFSET_1 0,
// TEST_1 0x30000, TEXFLUSH, FRAME_1 (fbp / 32, width `fbw`, PSM 0x0A), TEX1_1 0, SCISSOR_1 (0..w - 1,
// 0..2 h - 1); a REGLIST tag (PRIM 0x116 sprite, TEX0 base `fbp`, CLAMP_1, a NOP) and the strips:
// per 64-pixel column from x0, UV (u + 8, 8) / XY (u, 0) and UV (u + 0x408, h2 x 32 + 8) / XY
// (u + 0x400, h2 x 32). Words the original leaves unwritten are kept. Returns the end.
u32 func_00141EE0(u32 p, s32 fbp, s32 fbw, s32 w, s32 h, s32 x0, s32 width, s32 h2) {
    const s32 n = (width + 0x3F >= 0 ? width + 0x3F : width + 0x7E) >> 6;
    const auto put = [](u32 a, u32 v) { GRef<u32>(a) = v; };
    put(p, static_cast<u32>(n * 2 + 0xC) | 0x10000000u);
    put(p + 4, 0);
    put(p + 8, 0);
    put(p + 0xC, static_cast<u32>(n * 2 + 0xC) | 0x50000000u);
    p += 0x10;
    put(p, 7);
    put(p + 4, 0x10000000);
    put(p + 8, 0xE);
    p += 0x10;
    GlowCopyQword(p, GRef<u32>(0x004104A8) + 0xE0);
    put(p + 4, 1);
    p += 0x10;
    put(p, 0);
    put(p + 4, 0);
    put(p + 8, 0x18);
    p += 0x10;
    put(p, 0x30000);
    put(p + 8, 0x47);
    p += 0x10;
    put(p + 8, 0x3F);
    p += 0x10;
    put(p, static_cast<u32>(fbp >> 5) | (static_cast<u32>(fbw) << 16) | 0x0A000000u);
    put(p + 4, 0);
    put(p + 8, 0x4C);
    put(p + 0xC, 0);
    p += 0x10;
    put(p, 0);
    put(p + 4, 0);
    put(p + 8, 0x14);
    p += 0x10;
    put(p, static_cast<u32>(w - 1) << 16);
    put(p + 4, static_cast<u32>(h * 2 - 1) << 16);
    put(p + 8, 0x40);
    p += 0x10;
    put(p, 1);
    put(p + 4, 0x44000000);
    put(p + 8, 0xF860);
    p += 0x10;
    GRef<u64>(p) = 0x116;
    p += 8;
    put(p, static_cast<u32>(fbp) | (static_cast<u32>(fbw) << 14) | 0xA8A00000u);
    put(p + 4, 0xE);
    p += 8;
    put(p, 0xFF003F7Fu);
    put(p + 4, 3);
    p += 0x10;  // the NOP register's data is left as it is
    put(p, static_cast<u32>(n) | 0x8000u);
    put(p + 4, 0x44000000);
    put(p + 8, 0x4343);
    p += 0x10;
    u32 u = static_cast<u32>(x0) << 4;
    for (s32 k = 0; k < n; ++k) {
        put(p, (u + 8) | 0x80000u);
        p += 8;
        put(p, u);
        put(p + 4, 0);
        p += 8;
        put(p, (u + 0x408) | ((static_cast<u32>(h2) * 32 + 8) << 16));
        p += 8;
        put(p, (u + 0x400) | (static_cast<u32>(h2) << 21));
        put(p + 4, 0);
        p += 8;
        u += 0x400;
    }
    return p;
}

// EE: 0x001420F0
// BuildScaledCameraMatrices: UpdateCameraMatrices (0x001029E0) for a render target scaled by `scale`:
// the viewport scale and centre ((c - 2048) x scale + 2048) scaled, far = +0x170 x C 0x0040E514, the
// scissor scaled ((origin + size) x scale - 1); SCISSOR_1 / _2 register ids at +0x1A8 / +0x1B8.
void BuildScaledCameraMatrices(u32 cam, f32 scale) {
    const u32 view = cam + 0x20;
    sceVu0UnitMatrix(view);
    func_00102D28(view, view, cam + 0x10);
    sceVu0TransMatrix(view, view, cam);
    sceVu0InversMatrix(view, view);
    if ((GRef<u32>(cam + 0x198) & 1) != 0) {
        const f32 sx = fpu::IntToFloat(GRef<s16>(cam + 0x18C)) / GRef<f32>(cam + 0x160);
        const f32 sy = fpu::IntToFloat(GRef<s16>(cam + 0x18E)) / GRef<f32>(cam + 0x164);
        GRef<f32>(cam + 0x180) = sx;
        GRef<f32>(cam + 0x184) = sy;
    }
    const u32 screen = cam + 0x60;
    const u32 clip = cam + 0xA0;
    sceVu0UnitMatrix(screen);
    {
        const f32 cx = fpu::Add(fpu::Sub(GRef<f32>(cam + 0x178), 2048.0f) * scale, 2048.0f);
        const f32 cy = fpu::Add(fpu::Sub(GRef<f32>(cam + 0x17C), 2048.0f) * scale, 2048.0f);
        const f32 farz = GRef<f32>(cam + 0x170) * GRef<f32>(0x0040E514);
        sceVu0ViewScreenMatrix(screen, GRef<f32>(cam + 0x168), GRef<f32>(cam + 0x180) * scale,
                               GRef<f32>(cam + 0x184) * scale, cx, cy, GRef<f32>(cam + 0x190),
                               GRef<f32>(cam + 0x194), GRef<f32>(cam + 0x16C), farz);
    }
    BuildClipMatrix(clip, GRef<f32>(cam + 0x160), GRef<f32>(cam + 0x164), GRef<f32>(cam + 0x168),
                    GRef<f32>(cam + 0x16C), GRef<f32>(cam + 0x170));
    MulMatrixVu0(cam + 0xE0, screen, view);
    MulMatrixVu0(cam + 0x120, clip, view);
    const s64 x = GRef<s16>(cam + 0x188);
    const s64 y = GRef<s16>(cam + 0x18A);
    const s64 y1 = static_cast<s32>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(cam + 0x18A) + GRef<s16>(cam + 0x18E)) * scale) - 1);
    const s64 x1 = static_cast<s32>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(cam + 0x188) + GRef<s16>(cam + 0x18C)) * scale) - 1);
    GRef<u64>(cam + 0x1B8) = 0x41;
    GRef<u64>(cam + 0x1A8) = 0x40;
    const u64 scissor = static_cast<u64>(x) | static_cast<u64>(y1) << 48 | (static_cast<u64>(y) << 32 | static_cast<u64>(x1) << 16);
    GRef<u64>(cam + 0x1A0) = scissor;
    GRef<u64>(cam + 0x1B0) = scissor;
}

// EE: 0x00142460
// DrawGlowPass (docs/subsystems/rendering.md "Half-resolution glow pass"; VU1 program D,
// docs/subsystems/vu-programs.md): with a non-zero `level`, links program D (MPG from .vutext
// 0x0029A940) into bucket `bucket`, renders the glow model `inst` (node flags +0, chains +0x2C, node
// matrices +0x10, header +0x30 with the node count at +8) at half resolution into the buffer `fbp_a`
// (Z buffer cleared with a frame copy, then two passes: silhouette fins with ALPHA 0x68 / 0x62, light
// from the direction 0x00410688 rotated by the camera, offset -32 x the light), composites it via
// `fbp_b` with the composite packet 0x00141EE0, blurs it with sprite strips of colour `level`, and
// adds it to the frame; the camera matrices are rebuilt at scale 0.5 and restored at 1.0.
void func_00142460(u32 inst, u32 cam, u32 list, s32 bucket, u32 mw, s32 fbp_a, s32 fbp_b, u32 /*m0*/, u32 level) {
    if (level == 0) return;
    guest::GuestFrame frame(0x100);
    const u32 sp = frame.Address();
    const u32 ctx = GRef<u32>(0x004104A8);
    const u32 hgt = GRef<u16>(ctx + 0x72);
    const s32 off_y = static_cast<s32>((0x800 - (hgt >> 2)) * 16);
    const u32 wid = GRef<u16>(ctx + 0x70);
    const s32 vx = GRef<s16>(cam + 0x188), vy = GRef<s16>(cam + 0x18A), vw = GRef<s16>(cam + 0x18C), vh = GRef<s16>(cam + 0x18E);
    const s32 n = (vw + 0x3F >= 0 ? vw + 0x3F : vw + 0x7E) >> 6;
    const s32 hwb = static_cast<s32>((wid >> 1) + 0x3F) >> 6;
    const s32 wb = static_cast<s32>(wid + 0x3F) >> 6;
    const s32 hx = vx >> 1, hy = vy >> 1, hw = vw >> 1, hh = vh >> 1;
    const s32 off_x = static_cast<s32>((0x800 - (wid >> 2)) * 16);
    const s32 cx = (fpu::FloatToInt(GRef<f32>(cam + 0x178)) - 0x800) / 2 + 0x800;
    const s32 cy = (fpu::FloatToInt(GRef<f32>(cam + 0x17C)) - 0x800) / 2 + 0x800;
    const s32 x1h = hx + hw, y1h = hy + hh;
    const s32 n2 = n * 2;
    const u32 n_eop = static_cast<u32>(n) | 0x8000u;
    const u32 fbp_a32 = static_cast<u32>((fbp_a >= 0 ? fbp_a : fbp_a + 0x1F) >> 5);
    const u32 fbp_b32 = static_cast<u32>((fbp_b >= 0 ? fbp_b : fbp_b + 0x1F) >> 5);
    const u32 hwb16 = static_cast<u32>(hwb) << 16;
    const u32 u0 = static_cast<u32>(vx) << 4;
    const s32 q = hw >> 1, hh2 = hh >> 1;
    const u32 tbw_full = static_cast<u32>(wb) << 14;
    AddBucketRef(list, bucket, 0x0029A940, GRef<u16>(0x0029A930));
    u32 p = BeginPacket();
    BuildScaledCameraMatrices(cam, 0.5f);
    const auto put = [](u32 a, u32 v) { GRef<u32>(a) = v; };
    const auto at = [](u32 base, u32 i) { return base + i * 4; };
    // Draw environment of the half-resolution target, Z cleared by a copy of the frame.
    put(at(p, 1), 0);
    put(at(p, 0), 0x10000002);
    put(at(p, 2), 0);
    put(at(p, 3), 0x50000002);
    put(at(p, 4), 0x8001);
    put(at(p, 5), 0x10000000);
    put(at(p, 6), 0xE);
    put(at(p, 8), 0);
    put(at(p, 9), 0);
    put(at(p, 10), 0x3B);
    put(at(p, 0xC), 0x10000006);
    put(at(p, 0xD), 0);
    put(at(p, 0xE), 0);
    put(at(p, 0xF), 0x50000006);
    put(at(p, 0x10), 0x8005);
    put(at(p, 0x11), 0x10000000);
    put(at(p, 0x12), 0xE);
    GlowCopyQword(at(p, 0x14), ctx + 0xE0);
    put(at(p, 0x15), 1);
    put(at(p, 0x18), fbp_b32 | hwb16 | 0x31000000u);
    put(at(p, 0x1A), 0x4C);
    put(at(p, 0x19), 0);
    put(at(p, 0x1B), 0);
    put(at(p, 0x1E), 0x18);
    put(at(p, 0x1C), static_cast<u32>(off_x));
    put(at(p, 0x1D), static_cast<u32>(off_y));
    put(at(p, 0x20), static_cast<u32>(hx) | (static_cast<u32>(x1h - 1) << 16));
    put(at(p, 0x21), static_cast<u32>(hy) | (static_cast<u32>(y1h - 1) << 16));
    put(at(p, 0x22), 0x40);
    put(at(p, 0x24), 0x30000);
    put(at(p, 0x26), 0x47);
    put(at(p, 0x28), 0x10000003);
    put(at(p, 0x29), 0);
    put(at(p, 0x2A), 0);
    put(at(p, 0x2B), 0x50000003);
    put(at(p, 0x2C), 2);
    put(at(p, 0x2D), 0x10000000);
    put(at(p, 0x2E), 0xE);
    put(at(p, 0x30), 0);
    put(at(p, 0x31), 0);
    put(at(p, 0x32), 0x14);
    put(at(p, 0x34), 0x4010);
    put(at(p, 0x35), 0);
    put(at(p, 0x36), 8);
    put(at(p, 0x38), static_cast<u32>(n2 + 3) | 0x10000000u);
    put(at(p, 0x39), 0);
    put(at(p, 0x3A), 0);
    put(at(p, 0x3B), static_cast<u32>(n2 + 3) | 0x50000000u);
    put(at(p, 0x3C), 1);
    put(at(p, 0x3D), 0x24000000);
    put(at(p, 0x3E), 0x60);
    GRef<u64>(at(p, 0x40)) = 0x116;
    put(at(p, 0x43), 10);
    put(at(p, 0x42), (static_cast<u32>(GRef<u16>(ctx + 0x8E)) << 5) | 0x80000000u | (static_cast<u32>(GRef<u8>(ctx + 0x7D)) << 20) |
                         tbw_full | 0x28000000u);
    put(at(p, 0x45), 0x44000000);
    put(at(p, 0x46), 0x43C3);
    put(at(p, 0x44), n_eop);
    u32 r = at(p, 0x48);
    {
        u32 u = u0;
        u32 x = static_cast<u32>(cx - q) * 16;
        for (s32 k = 0; k < n; ++k) {
            put(at(r, 0), u);
            put(at(r, 3), 0);
            u += 0x400;
            put(at(r, 2), x | (static_cast<u32>(cy - hh2) << 20));
            put(at(r, 4), u | (static_cast<u32>(vh) << 20));
            put(at(r, 7), 0);
            put(at(r, 6), (x + 0x200) | (static_cast<u32>(cy + hh2) << 20));
            r = at(r, 8);
            x += 0x200;
        }
    }
    // Copy the half-resolution frame back into buffer A.
    put(at(r, 1), 0);
    put(at(r, 0), 0x10000003);
    put(at(r, 2), 0);
    put(at(r, 3), 0x50000003);
    put(at(r, 4), 2);
    put(at(r, 5), 0x10000000);
    put(at(r, 6), 0xE);
    put(at(r, 10), 0x4C);
    put(at(r, 8), fbp_a32 | hwb16);
    put(at(r, 9), 0);
    put(at(r, 0xB), 0);
    put(at(r, 0xC), fbp_b32 | 0x1000000u);
    put(at(r, 0xE), 0x4E);
    put(at(r, 0xD), 1);
    put(at(r, 0xF), 0);
    put(at(r, 0x10), 0x10000003);
    put(at(r, 0x11), 0);
    put(at(r, 0x12), 0);
    put(at(r, 0x13), 0x50000003);
    put(at(r, 0x14), 2);
    put(at(r, 0x15), 0x10000000);
    put(at(r, 0x16), 0xE);
    put(at(r, 0x18), 0x30000);
    put(at(r, 0x1A), 0x47);
    put(at(r, 0x1C), 0x60);
    put(at(r, 0x1E), 0x14);
    put(at(r, 0x1D), 0);
    put(at(r, 0x20), static_cast<u32>(n2 + 4) | 0x10000000u);
    put(at(r, 0x21), 0);
    put(at(r, 0x22), 0);
    put(at(r, 0x23), static_cast<u32>(n2 + 4) | 0x50000000u);
    put(at(r, 0x24), 1);
    put(at(r, 0x25), 0x44000000);
    put(at(r, 0x26), 0xF160);
    put(at(r, 0x28), 0x116);
    put(at(r, 0x29), 0);
    put(at(r, 0x2A), tbw_full | 0xA8000000u);
    put(at(r, 0x2B), 6);
    put(at(r, 0x2C), 0x80000000u);
    put(at(r, 0x2D), 0x3F800000u);
    put(at(r, 0x31), 0x44000000);
    put(at(r, 0x32), 0x43C3);
    put(at(r, 0x30), n_eop);
    r = at(r, 0x34);
    {
        u32 u = u0;
        u32 x = static_cast<u32>(cx - q) * 16;
        for (s32 k = 0; k < n; ++k) {
            put(at(r, 0), u);
            put(at(r, 3), 0);
            put(at(r, 2), x | (static_cast<u32>(cy - (hh >> 1)) << 20));
            put(at(r, 4), (u + 0x400) | (static_cast<u32>(vh) << 20));
            x += 0x200;
            put(at(r, 7), 0);
            put(at(r, 6), x | (static_cast<u32>(cy + (hh >> 1)) << 20));
            r = at(r, 8);
            u += 0x400;
        }
    }
    put(at(r, 1), 0);
    put(at(r, 0), 0x10000002);
    put(at(r, 2), 0);
    put(at(r, 3), 0x50000002);
    put(at(r, 4), 0x8001);
    put(at(r, 5), 0x10000000);
    put(at(r, 6), 0xE);
    put(at(r, 8), fbp_b32 | 0x1000000u);
    put(at(r, 9), 1);
    put(at(r, 10), 0x4E);
    put(at(r, 0xB), 0);
    r = at(r, 0xC);
    // The two fin passes through program D.
    const u32 light = sp + 0x80, offset = sp + 0x90, axis = sp + 0xA0, rot = sp + 0xB0, node_m = sp + 0x40, screen_m = sp;
    for (u32 pass = 0; pass < 2; ++pass) {
        if (pass != 2) {
            put(at(r, 0), 0x10000002);
            put(at(r, 1), 0);
            put(at(r, 2), 0);
            put(at(r, 3), 0x50000002);
            put(at(r, 6), 0xE);
            put(at(r, 4), 0x8001);
            put(at(r, 5), 0x10000000);
            put(at(r, 8), 0x7C000);
            put(at(r, 10), 0x47);
            r = at(r, 0xC);
        }
        sceVu0CopyVector(light, GRef<u32>(0x00410688));
        for (u32 o = 0; o < 16; o += 4) GRef<u32>(axis + o) = 0;  // memset
        GRef<f32>(sp + 0xA8) = 1.0f;
        sceVu0UnitMatrix(rot);
        RotateMatrix(rot, rot, cam + 0x10);
        sceVu0ApplyMatrix(axis, rot, axis);
        sceVu0ScaleVector(offset, light, -32.0f);
        put(at(r, 0), 0x10000002);
        put(at(r, 1), 0);
        GRef<u32>(sp + 0x9C) = 0;
        put(at(r, 2), 0x11000000);
        put(at(r, 3), 0x50000002);
        put(at(r, 6), 0xE);
        put(at(r, 4), 0x8001);
        put(at(r, 5), 0x10000000);
        put(at(r, 9), 0x80);
        put(at(r, 8), pass == 0 ? 0x68u : pass == 1 ? 0x62u : 0x28u);
        put(at(r, 10), 0x42);
        put(at(r, 0xB), 0);
        put(at(r, 0xC), 0x10000002);
        put(at(r, 0xD), 0);
        put(at(r, 0xE), 0);
        put(at(r, 0xF), 0x50000002);
        put(at(r, 0x10), 0x8001);
        put(at(r, 0x11), 0x10000000);
        put(at(r, 0x12), 1);
        put(at(r, 0x14), 1);
        put(at(r, 0x15), 1);
        put(at(r, 0x16), 1);
        put(at(r, 0x17), 0x80);
        put(at(r, 0x18), 0x1000000C);
        put(at(r, 0x19), 0);
        put(at(r, 0x1A), 0x01000404);
        put(at(r, 0x1B), 0x6C0C0020);
        sceVu0CopyMatrix(at(r, 0x1C), cam + 0xE0);
        sceVu0CopyMatrix(at(r, 0x2C), cam + 0x120);
        sceVu0CopyMatrix(at(r, 0x3C), cam + 0x20);
        put(at(r, 0x4C), 0x10000002);
        put(at(r, 0x4D), 0);
        put(at(r, 0x4E), 0x01000202);
        put(at(r, 0x4F), 0x6C02002C);
        GlowCopyQword(at(r, 0x50), light);
        GlowCopyQword(at(r, 0x54), offset);
        put(at(r, 0x59), 0);
        put(at(r, 0x58), 0x10000003);
        put(at(r, 0x5A), 0);
        put(at(r, 0x5B), 0x6C03402E);
        put(at(r, 0x5D), 0x50224000);
        put(at(r, 0x5C), 1);
        put(at(r, 0x5E), 0x44441);
        put(at(r, 0x60), 1);
        put(at(r, 0x61), 0x4021C000);
        put(at(r, 0x62), 0x4441);
        put(at(r, 0x64), 0x8000);
        put(at(r, 0x65), 0);
        put(at(r, 0x66), 0);
        put(at(r, 0x68), 0x10000000);
        put(at(r, 0x69), 0);
        put(at(r, 0x6A), 0x6E014031);
        put(at(r, 0x6B), pass);
        put(at(r, 0x6C), 0x10000000);
        put(at(r, 0x6D), 0);
        put(at(r, 0x6E), 0);
        put(at(r, 0x6F), 0x14000000);
        // Per drawable node: two MSCNT uploads of {node matrix, world -> screen x node} and a DMA call
        // of the node's strip chain; the DMA tag of each piece is written into the slot left before it.
        u32 tag = at(r, 0x70);
        r = at(r, 0x72);
        for (s32 i = 0; i < GRef<s32>(GRef<u32>(inst + 0x30) + 8); ++i) {
            const u32 f = GRef<u32>(GRef<u32>(inst) + static_cast<u32>(i) * 4);
            if ((f & 1) != 0 || (f & 2) != 0) continue;
            const u32 chain = GRef<u32>(GRef<u32>(inst + 0x2C) + static_cast<u32>(i) * 4);
            MulMatrixVu0(node_m, mw, GRef<u32>(inst + 0x10) + static_cast<u32>(i) * 0x40);
            MulMatrixVu0(screen_m, cam + 0xE0, node_m);
            for (s32 k = 0;;) {
                put(at(tag, 1), 0);
                put(at(tag, 0), 0x10000009);
                put(at(r, 0), 0x01000404);
                ++k;
                put(at(r, 1), 0x6C088000);
                sceVu0CopyMatrix(at(r, 2), node_m);
                sceVu0CopyMatrix(at(r, 0x12), screen_m);
                put(at(r, 0x22), 0);
                put(at(r, 0x23), 0);
                put(at(r, 0x24), 0);
                put(at(r, 0x25), 0x14000000);
                tag = at(r, 0x26);
                if (1 < k) break;
                r = at(r, 0x28);
            }
            put(at(r, 0x27), chain);
            put(tag, 0x50000000);
            put(at(r, 0x28), 0);
            put(at(r, 0x29), 0);
            tag = at(r, 0x2A);
            r = at(r, 0x2C);
        }
        put(at(tag, 1), 0);
        put(at(tag, 0), 0x10000000);
        put(at(r, 0), 0x11000000);
        put(at(r, 1), 0);
        r = at(r, 2);
    }
    u32 q9 = func_00141EE0(r, fbp_a, hwb, static_cast<s32>(wid) >> 1, static_cast<s32>(hgt) >> 1, hx, hw, hh);
    sceVu0CopyVector(node_m, GRef<u32>(0x00410688));
    // Blur buffer A into B (colour `level`, additive).
    put(at(q9, 1), 0);
    put(at(q9, 0), 0x10000007);
    put(at(q9, 2), 0);
    put(at(q9, 3), 0x50000007);
    put(at(q9, 6), 0xE);
    put(at(q9, 4), 6);
    put(at(q9, 5), 0x10000000);
    put(at(q9, 10), 0x3F);
    put(at(q9, 0xC), fbp_b32 | hwb16);
    put(at(q9, 0xD), 0);
    put(at(q9, 0xE), 0x4C);
    put(at(q9, 0xF), 0);
    put(at(q9, 0x10), static_cast<u32>(off_x));
    put(at(q9, 0x12), 0x18);
    put(at(q9, 0x11), static_cast<u32>(off_y));
    put(at(q9, 0x14), static_cast<u32>(hx) | (static_cast<u32>(x1h) << 16));
    put(at(q9, 0x15), static_cast<u32>(hy) | (static_cast<u32>(y1h - 1) << 16));
    put(at(q9, 0x16), 0x40);
    GlowCopyQword(at(q9, 0x18), ctx + 0xE0);
    put(at(q9, 0x19), 1);
    put(at(q9, 0x1C), 0x30000);
    put(at(q9, 0x1E), 0x47);
    put(at(q9, 0x20), static_cast<u32>(n + 3) | 0x10000000u);
    put(at(q9, 0x21), 0);
    put(at(q9, 0x22), 0);
    put(at(q9, 0x23), static_cast<u32>(n + 3) | 0x50000000u);
    put(at(q9, 0x24), 1);
    put(at(q9, 0x26), 0x10);
    put(at(q9, 0x25), 0x24000000);
    GRef<u64>(at(q9, 0x28)) = 6;
    put(at(q9, 0x2A), level | 0x80000000u | (level << 16) | (level << 8));
    put(at(q9, 0x2B), 0x3F800000u);
    put(at(q9, 0x2D), 0x24000000);
    put(at(q9, 0x2E), 0x4C);
    put(at(q9, 0x2C), n_eop);
    r = at(q9, 0x30);
    {
        u32 x = static_cast<u32>(cx - q) * 16;
        const s32 hy2 = hh >> 1;
        for (s32 k = 0; k < n; ++k) {
            put(at(r, 1), 0);
            put(at(r, 0), x | (static_cast<u32>(cy - hy2) << 20));
            put(at(r, 2), (x + 0x200) | (static_cast<u32>(cy + hy2) << 20));
            put(at(r, 3), 0);
            r = at(r, 4);
            x += 0x200;
        }
    }
    put(at(r, 1), 0);
    put(at(r, 0), 0x10000004);
    put(at(r, 2), 0);
    put(at(r, 3), 0x50000004);
    put(at(r, 4), 3);
    put(at(r, 6), 0xE);
    put(at(r, 5), 0x10000000);
    put(at(r, 8), 0x30007);
    put(at(r, 10), 0x47);
    put(at(r, 0xE), 0x3F);
    put(at(r, 0x10), 0);
    put(at(r, 0x11), 0);
    put(at(r, 0x12), 8);
    put(at(r, 0x14), static_cast<u32>(n2 + 4) | 0x10000000u);
    put(at(r, 0x15), 0);
    put(at(r, 0x16), 0);
    put(at(r, 0x17), static_cast<u32>(n2 + 4) | 0x50000000u);
    put(at(r, 0x18), 1);
    put(at(r, 0x19), 0x44000000);
    put(at(r, 0x1A), 0xF160);
    put(at(r, 0x1C), 0x116);
    put(at(r, 0x1D), 0);
    put(at(r, 0x1E), static_cast<u32>(fbp_a) | (static_cast<u32>(hwb) << 14) | 0xA8000000u);
    put(at(r, 0x1F), 6);
    put(at(r, 0x20), 0x80000000u);
    put(at(r, 0x21), 0x3F800000u);
    put(at(r, 0x25), 0x44000000);
    put(at(r, 0x26), 0x43C3);
    put(at(r, 0x24), n_eop);
    r = at(r, 0x28);
    {
        u32 x = static_cast<u32>(cx - q) * 16;
        u32 u = static_cast<u32>(hx) << 4;
        const s32 hy2 = hh >> 1;
        for (s32 k = 0; k < n; ++k) {
            put(at(r, 0), (u + 8) | 0x80000u);
            put(at(r, 2), x | (static_cast<u32>(cy - hy2) << 20));
            put(at(r, 3), 0);
            put(at(r, 4), (u + 0x208) | ((static_cast<u32>(hh) * 16 + 8) << 16));
            x += 0x200;
            put(at(r, 7), 0);
            put(at(r, 6), x | (static_cast<u32>(cy + hy2) << 20));
            u += 0x200;
            r = at(r, 8);
        }
    }
    q9 = func_00141EE0(r, fbp_b, hwb, static_cast<s32>(wid) >> 1, static_cast<s32>(hgt) >> 1, hx, hw, hh);
    BuildScaledCameraMatrices(cam, 1.0f);
    // Add buffer B to the frame (full resolution, the main context's draw environment restored).
    put(at(q9, 1), 0);
    put(at(q9, 0), 0x10000008);
    put(at(q9, 2), 0x11000000);
    put(at(q9, 3), 0x50000008);
    put(at(q9, 4), 0x8007);
    put(at(q9, 6), 0xE);
    put(at(q9, 5), 0x10000000);
    GlowCopyQword(at(q9, 8), ctx + 0xD0);
    put(at(q9, 9), 0xFFFFFF);
    put(at(q9, 0xC), 0x30000);
    put(at(q9, 0xE), 0x47);
    put(at(q9, 0x10), 0x4010);
    put(at(q9, 0x12), 8);
    put(at(q9, 0x11), 0);
    GlowCopyQword(at(q9, 0x14), ctx + 0xF0);
    GlowCopyQword(at(q9, 0x18), cam + 0x1A0);
    put(at(q9, 0x1E), 0x3F);
    put(at(q9, 0x20), 0x60);
    put(at(q9, 0x21), 0);
    put(at(q9, 0x22), 0x14);
    put(at(q9, 0x24), static_cast<u32>(n2 + 4) | 0x10000000u);
    put(at(q9, 0x25), 0);
    put(at(q9, 0x26), 0);
    put(at(q9, 0x27), static_cast<u32>(n2 + 4) | 0x50000000u);
    put(at(q9, 0x28), 1);
    put(at(q9, 0x2A), 0xF160);
    put(at(q9, 0x29), 0x44000000);
    GRef<u64>(at(q9, 0x2C)) = 0x116;
    put(at(q9, 0x2E), static_cast<u32>(fbp_b) | (static_cast<u32>(hwb) << 14) | 0xA8000000u);
    put(at(q9, 0x2F), 6);
    put(at(q9, 0x30), 0x80808080u);
    put(at(q9, 0x31), 0x3F800000u);
    put(at(q9, 0x35), 0x44000000);
    put(at(q9, 0x36), 0x43C3);
    put(at(q9, 0x34), n_eop);
    r = at(q9, 0x38);
    {
        u32 x = static_cast<u32>(fpu::FloatToInt(GRef<f32>(cam + 0x178)) - hw) * 16;
        u32 u = static_cast<u32>(hx) << 4;
        const s32 hy2 = hh;
        for (s32 k = 0; k < n; ++k) {
            put(at(r, 0), (u + 8) | 0x80000u);
            put(at(r, 3), 0);
            put(at(r, 2), x | (static_cast<u32>(cy - hy2) << 20));
            put(at(r, 4), (u + 0x208) | ((static_cast<u32>(hh) * 16 + 8) << 16));
            x += 0x400;
            put(at(r, 7), 0);
            put(at(r, 6), x | (static_cast<u32>(cy + hy2) << 20));
            r = at(r, 8);
            u += 0x200;
        }
    }
    put(at(r, 1), 0);
    put(at(r, 0), 0x10000004);
    put(at(r, 2), 0x11000000);
    put(at(r, 3), 0x50000004);
    put(at(r, 4), 3);
    put(at(r, 6), 0xE);
    put(at(r, 5), 0x10000000);
    GlowCopyQword(at(r, 8), ctx + 0xD0);
    put(at(r, 0xC), 0x30000);
    put(at(r, 0xE), 0x47);
    put(at(r, 0x10), 0x56);
    put(at(r, 0x12), 0x42);
    put(at(r, 0x11), 0);
    put(at(r, 0x13), 0);
    put(at(r, 0x14), static_cast<u32>(n + 3) | 0x10000000u);
    put(at(r, 0x15), 0);
    put(at(r, 0x16), 0);
    put(at(r, 0x17), static_cast<u32>(n + 3) | 0x50000000u);
    put(at(r, 0x18), 1);
    put(at(r, 0x1A), 0x10);
    put(at(r, 0x19), 0x24000000);
    put(at(r, 0x1C), 0x46);
    put(at(r, 0x1D), 0);
    put(at(r, 0x1E), 0x80808080u);
    put(at(r, 0x1F), 0x3F800000u);
    put(at(r, 0x20), n_eop);
    put(at(r, 0x21), 0x24000000);
    put(at(r, 0x22), 0x44);
    r = at(r, 0x24);
    {
        u32 x = static_cast<u32>(fpu::FloatToInt(GRef<f32>(cam + 0x178)) - hw) * 16;
        const s32 hy2 = hh;
        for (s32 k = 0; k < n; ++k) {
            put(at(r, 1), 0);
            put(at(r, 0), x | (static_cast<u32>(cy - hy2) << 20));
            put(at(r, 2), (x + 0x400) | (static_cast<u32>(cy + hy2) << 20));
            put(at(r, 3), 0);
            r = at(r, 4);
            x += 0x400;
        }
    }
    put(at(r, 1), 0);
    put(at(r, 0), 0x10000004);
    put(at(r, 2), 0x11000000);
    put(at(r, 3), 0x50000004);
    put(at(r, 4), 0x8003);
    put(at(r, 5), 0x10000000);
    put(at(r, 6), 0xE);
    GlowCopyQword(at(r, 8), ctx + 0xD0);
    GlowCopyQword(at(r, 0xC), ctx + 0xE0);
    GlowCopyQword(at(r, 0x10), ctx + 0x110);
    EndPacket(list, bucket, (at(r, 0x14) + 0xF) & 0xFFFFFFF0u);
}

// EE: 0x001422E8
// Dead (no reference): full-screen quad packet: ALPHA_1 0x68 (0x62 with `alt`), FIX 0x80, PRIM 0x44,
// RGBAQ 0x80010101, and the four ftoi4 vertices v0-v3. Returns the end.
u32 func_001422E8(u32 p, u32 a1, u32 a2, u32 a3, u32 t0, u32 v0, u32 v1, u32 v2, u32 v3, s32 alt) {
    (void)a1;
    (void)a2;
    (void)a3;
    (void)t0;
    const auto xyz = [](u32 at, u32 v) {
        GRef<u32>(at) = GRef<u32>(v) | GRef<u32>(v + 4) << 16;
        GRef<s32>(at + 4) = GRef<s32>(v + 8) >> 4;
    };
    GRef<u32>(p + 4) = 0;
    GRef<u32>(p) = 0x10000007;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(p + 0xC) = 0x50000007;
    GRef<u32>(p + 0x10) = 1;
    GRef<u32>(p + 0x14) = 0x10000000;
    GRef<u32>(p + 0x18) = 0xE;
    GRef<u32>(p + 0x20) = alt != 0 ? 0x62 : 0x68;
    GRef<u32>(p + 0x24) = 0x80;
    GRef<u32>(p + 0x28) = 0x42;
    GRef<u32>(p + 0x2C) = 0;
    GRef<u32>(p + 0x30) = 1;
    GRef<u32>(p + 0x34) = 0x24000000;
    GRef<u32>(p + 0x38) = 0x10;
    GRef<u64>(p + 0x40) = 0x44;
    GRef<u32>(p + 0x48) = 0x80010101;
    GRef<f32>(p + 0x4C) = 1.0f;
    GRef<u32>(p + 0x50) = 0x8004;
    GRef<u32>(p + 0x54) = 0x14000000;
    GRef<u32>(p + 0x58) = 4;
    xyz(p + 0x60, v0);
    xyz(p + 0x68, v1);
    xyz(p + 0x70, v2);
    xyz(p + 0x78, v3);
    return p + 0x80;
}

// EE: 0x00149D08
// Dead (no reference): sets channel `channel` of the model instance of slot `n` (+8 table) to v.
void func_00149D08(f32 v, u32 rec, s32 n, s32 channel) {
    SetModelPartValue(v, GRef<u32>(GRef<u32>(rec + 8) + static_cast<u32>(n) * 4), channel);
}

// EE: 0x00149D20
// Dead (no reference): value of channel `channel` of the instance of slot `n` (+0 table); 0 when none.
f32 func_00149D20(u32 rec, s32 n, s32 channel) {
    const u32 inst = GRef<u32>(GRef<u32>(rec) + static_cast<u32>(n) * 4);
    if (inst == 0) return 0.0f;
    return func_00141000(inst, channel);
}

// EE: 0x00149E10
// Dead (no reference): stores v in entry n of table +0x18.
void func_00149E10(u32 rec, s32 n, u32 v) { GRef<u32>(GRef<u32>(rec + 0x18) + static_cast<u32>(n) * 4) = v; }

}  // namespace ac04::game
