// Records page "PERSONAL DATA" (scene 4 subs 0x37-0x39; docs/subsystems/frontend.md, page 21):
// total flight time, sortie and kill totals, and the aircraft/colour usage list, five per page.
//
// Page object (0x540 bytes, *0x00411838; allocated after a frame-count dependent pad, quirk in
// frontend.md): +0x10 data load queue, +0x220 DATA 0xB7 (aircraft icon packet), +0x230 fade
// object, +0x23C fade level, +0x240 crosshair, +0x27C panel (+0x280 outline count, +0x294 row
// outline times, +0x2C8/+0x2CC/+0x2D0 timers a/b/c, +0x2D4 crosshair start, +0x2DC crosshair),
// +0x2E0 state, +0x2E4/+0x2E6 MenuGoTo target, +0x2E8 page count, +0x2EC page, +0x2F0 flight time
// (frames), +0x2F4/+0x2F8 totals, +0x2FC sorties, +0x300 list length, +0x304 s32 counts[]
// (-1: aircraft/colour not owned), +0x408 u16 keys[] (aircraft << 4 | colour), +0x48A s16 shares[]
// (1/100 %), +0x50C f32 bar targets[5], +0x520 f32 bars[5], +0x538 visible rows mask.
#include "game/game.h"

#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u32, g_RecordsPage, 0x00411838)  // gp-0x41B8
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

constexpr u32 kSortBuffer = 0x70000000;  // scratchpad

s32& State(u32 obj) { return GRef<s32>(obj + 0x2E0); }

}  // namespace

// EE: 0x00240FA0
// Comb sort of `n` signed 64-bit values at `a`, largest first (gap shrink factor K 0x00410368,
// gaps 9 and 10 become 11).
void CombSortDescending(u32 a, s32 n) {
    const f32 shrink = K(0x00410368);
    f32 f = static_cast<f32>(n) * shrink;
    for (;;) {
        s32 gap = fpu::FloatToInt(f);
        if (gap == 0) {
            gap = 1;
        } else if (static_cast<u32>(gap - 9) < 2) {
            gap = 0xB;
        }
        bool swapped = false;
        for (s32 i = 0; i < n - gap; ++i) {
            s64& x = GRef<s64>(a + static_cast<u32>(i) * 8);
            s64& y = GRef<s64>(a + static_cast<u32>(i + gap) * 8);
            if (x < y) {
                const s64 t = x;
                x = y;
                y = t;
                swapped = true;
            }
        }
        f = static_cast<f32>(gap) * shrink;
        if (!swapped && gap < 2) return;
    }
}

// EE: 0x00241068
// The usage list: one entry per shown aircraft/colour (ownership words `own`), sorted by sortie
// count (unowned ones last, then by aircraft field 8 and colour), with the totals from `totals`
// (0x002A0350: time, two totals, sorties, then the per-aircraft/colour counts).
void BuildUsageList(u32 obj, u32 own, u32 totals) {
    s32 n = 0;
    for (u32 c = 0; c < 3; ++c) {
        u64 bits = GRef<u64>(own + c * 8);
        u32 out = kSortBuffer + static_cast<u32>(n) * 8;
        for (s32 a = 0; a < 0x15; ++a, bits >>= 2) {
            const u32 state = static_cast<u32>(bits & 3);
            if (state == 0) continue;
            const u64 count = state == 1 ? 0xFFFFFFFFull : GRef<u32>(totals + 0x10 + static_cast<u32>(a) * 0xC + c * 4);
            ++n;
            const s32 order = static_cast<s32>((0xFFu - static_cast<u32>(GetAircraftField8(a + 1))) | (0xF - c) << 8);
            GRef<u64>(out) = (count << 32) | static_cast<u64>(static_cast<u32>(a << 4) | c) | Sx(order) << 0x10;
            out += 8;
        }
    }
    CombSortDescending(kSortBuffer, n);
    GRef<s32>(obj + 0x300) = n;
    s32 pages = (n + 4) / 5;
    GRef<s32>(obj + 0x2E8) = pages;
    if (!(pages > 0)) GRef<s32>(obj + 0x2E8) = 1;
    GRef<u32>(obj + 0x2F0) = GRef<u32>(totals);
    s32 sorties = GRef<s32>(totals + 0xC);
    GRef<s32>(obj + 0x2FC) = sorties;
    if (!(0 < sorties)) sorties = 1;
    GRef<u32>(obj + 0x2F4) = GRef<u32>(totals + 4);
    GRef<u32>(obj + 0x2F8) = GRef<u32>(totals + 8);
    for (s32 i = 0; i < n; ++i) {
        const u64 e = GRef<u64>(kSortBuffer + static_cast<u32>(i) * 8);
        const s32 count = static_cast<s32>(e >> 32);
        GRef<s32>(obj + 0x304 + static_cast<u32>(i) * 4) = count;
        GRef<u16>(obj + 0x408 + static_cast<u32>(i) * 2) = static_cast<u16>(e & 0xFFF);
        GRef<s16>(obj + 0x48A + static_cast<u32>(i) * 2) = static_cast<s16>((count * 10000) / sorties);
    }
}

// EE: 0x002412B8
// Shows page `page` (0-12, else 0): cursor, and bar targets count / sorties (0..1).
void SelectRecordsPage(u32 obj, u64 page) {
    const s32 p = page < 0xD ? static_cast<s32>(page) : 0;
    GRef<s32>(obj + 0x2EC) = p;
    GRef<u16>(obj + 0x2D4) = 0xF80;
    const s32 y = p * 0x130 + 0x4A0;
    GRef<s16>(obj + 0x2D6) = static_cast<s16>(y);
    MoveCrosshairAuto(GRef<u32>(obj + 0x2DC), 0xF80, y);
    s32 sorties = GRef<s32>(obj + 0x2FC);
    if (sorties < 1) sorties = 1;
    const s32 n = GRef<s32>(obj + 0x300);
    for (s32 k = 0; k < 5; ++k) {
        const s32 idx = p * 5 + k;
        f32 v = 0.0f;
        if (idx < n) v = (1.0f / static_cast<f32>(sorties)) * static_cast<f32>(GRef<s32>(obj + 0x304 + static_cast<u32>(idx) * 4));
        f32& t = GRef<f32>(obj + 0x50C + static_cast<u32>(k) * 4);
        if (v < 0.0f) {
            t = 0.0f;
        } else if (1.0f < v) {
            t = 1.0f;
        } else {
            t = v;
        }
    }
}

// EE: 0x002413D0
// Up/down change the page (wrapping); the texts type out again.
void RecordsPageInput(u32 obj) {
    const u32 repeat = Ctx()->pad_buttons[0][2];
    GRef<u16>(g_pMenuFade() + 0x32) |= 3;
    if ((repeat & 3) == 0) return;
    s32 pages = GRef<s32>(obj + 0x2E8);
    s32 p = GRef<s32>(obj + 0x2EC) - static_cast<s32>(repeat & 1);
    if (pages < 1) pages = 1;
    if ((repeat & 2) != 0) p += 1;
    SelectRecordsPage(obj, static_cast<u64>(static_cast<s64>((p + pages) % pages)));
    GRef<u32>(obj + 0x2CC) = 0;
    GRef<u16>(obj + 0x27C) |= 0x200;
    PlayMenuSe(0, 4);
}

// EE: 0x00241480
// Bars ease towards their targets.
void AnimateRecordBars(u32 obj) {
    for (u32 k = 0; k < 5; ++k) {
        f32& bar = GRef<f32>(obj + 0x520 + k * 4);
        bar = ApproachValue(bar, GRef<f32>(obj + 0x50C + k * 4), K(0x0041036C));
    }
}

// EE: 0x002414E0
u32 CreateRecordsPage() {
    WorkHeapAlloc(static_cast<s32>(((Ctx()->frame_count & 0xF) + 1) * 0x10), 0x10);
    const u32 obj = WorkHeapAlloc(0x540, 0x10);
    const u32 fade = g_pMenuFade();
    State(obj) = 0;
    GRef<u16>(fade + 0x32) = 0;
    GRef<u16>(fade + 0x30) = 1;
    InitFade(obj + 0x230, 1);
    GRef<u16>(obj + 0x2E4) = 1;
    GRef<u16>(obj + 0x2E6) = 4;
    InitPanel(obj + 0x27C, 8, 0x003AF460, obj + 0x240);
    GRef<u16>(obj + 0x27E) |= 0xE;
    BuildUsageList(obj, 0x002A0320, 0x002A0350);
    for (u32 off = 0x530; off >= 0x520; off -= 4) GRef<u32>(obj + off) = 0;
    GRef<s32>(obj + 0x2EC) = 0;
    SelectRecordsPage(obj, 0);
    return obj;
}

// EE: 0x002415D0
void RecordsUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    const s32 st = State(obj);
    const u32 edge = Ctx()->pad_buttons[0][1];
    GRef<u16>(fade + 0x32) = 0;
    s32 state = 0;
    if (st != 0) {
        UpdatePanel(obj + 0x27C);
        state = State(obj);
    }
    if (state == 1) {
        GRef<u16>(fade + 0x32) = 0x20;
        if ((edge & 0x20) != 0) {
            PlayMenuSe(0, 3);
            StartFade(0.25f, obj + 0x230, 1);
            SetPanelState(obj + 0x27C, 1);
            State(obj) = 4;
        } else if ((GRef<u16>(obj + 0x27C) & 0x800) != 0) {
            AnimateRecordBars(obj);
            RecordsPageInput(obj);
        }
    } else if (state == 0) {
        if (K(0x00410370) < GRef<f32>(fade + 0xC)) State(obj) = 1;
    } else if (state == 2 || state == 4) {
        if ((GRef<u32>(obj + 0x230) & 8) == 0 && (GRef<u16>(obj + 0x27C) & 8) == 0) return;
        MenuGoTo(GRef<s16>(obj + 0x2E4), GRef<u16>(obj + 0x2E6));
    }
}

// EE: 0x00241708
// Texts: the totals (timer a), the rows of the page with their counts and shares, the aircraft
// names (timer b) and the page marks (timer c); the pulsing box on the current page mark.
void DrawRecordsTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u32 parts = GRef<u16>(obj + 0x27C);
    const u32 rows = GRef<u32>(obj + 0x538);
    GRef<u16>(obj + 0x27C) = static_cast<u16>(parts & 0x7FFF);
    BeginMenuTextLayer(list, index);
    const f32 a = GRef<f32>(obj + 0x2C8);
    if (0.0f < a) {
        const f32 t = a * 90.0f;
        DrawMenuText(t, 0x1C0, 0x4A0, 4, 0x004112E8);
        u32 done = 0x1F;
        bool typing = false;
        DrawMenuText(fpu::Sub(t, 1.0f), 0x280, 0x4A0, 4, 0x004096D0);
        const u32 time = GRef<u32>(obj + 0x2F0);
        const u64 targs[3] = {time / 0x34BC0u, (time / 0xE10u) % 0x3Cu, (time / 0x3Cu) % 0x3Cu};
        ee::sprintf(buf, 0x004096E8, targs, 3);
        DrawMenuText(fpu::Sub(t, 8.0f), 0x280, 0x560, 3, buf);
        DrawMenuText(fpu::Sub(t, 16.0f), 0x280, 0x660, 4, 0x004096F8);
        DrawMenuText(fpu::Sub(t, 24.0f), 0x280, 0x720, 3, 0x004112F0);
        DrawMenuText(fpu::Sub(t, 32.0f), 0x280, 0x830, 3, 0x00409710);
        const u64 k1[1] = {Sx(GRef<s32>(obj + 0x2F4))};
        ee::sprintf(buf, 0x004112F8, k1, 1);
        DrawMenuText(fpu::Sub(t, 33.0f), 0x840, 0x720, 3, buf);
        const u64 k2[1] = {Sx(GRef<s32>(obj + 0x2F8))};
        ee::sprintf(buf, 0x004112F8, k2, 1);
        DrawMenuText(fpu::Sub(t, 41.0f), 0x840, 0x830, 3, buf);
        f32 r = fpu::Sub(t, 5.0f);
        s32 idx = GRef<s32>(obj + 0x2EC) * 5;
        s32 y = 0x4A0;
        s32 y2 = 0x560;
        for (u32 i = 0; i < 5; ++i, ++idx, y += 0x340, y2 += 0x340, r = fpu::Sub(r, 4.0f)) {
            if (((static_cast<s32>(rows) >> i) & 1) == 0 || r <= 0.0f) continue;
            DrawMenuText(r, 0x1100, y, 4, 0x00409720);
            DrawMenuText(fpu::Sub(r, 12.0f), 0x1750, y, 4, 0x00409730);
            DrawMenuText(fpu::Sub(r, 24.0f), 0x1D10, y, 4, 0x00411300);
            const s32 count = GRef<s32>(obj + 0x304 + static_cast<u32>(idx) * 4);
            if (count < 0) {
                DrawMenuText(fpu::Sub(r, 17.0f), 0x1750, y2, 3, 0x00411308);
                DrawMenuText(fpu::Sub(r, 29.0f), 0x1D10, y2, 3, 0x00411310);
            } else {
                const u64 cargs[1] = {Sx(count)};
                ee::sprintf(buf, 0x00411318, cargs, 1);
                DrawMenuText(fpu::Sub(r, 17.0f), 0x1750, y2, 3, buf);
                const s32 share = GRef<s16>(obj + 0x48A + static_cast<u32>(idx) * 2);
                const u64 sargs[2] = {Sx(share / 100), Sx((share / 10) % 10)};
                ee::sprintf(buf, 0x00409740, sargs, 2);
                DrawMenuText(fpu::Sub(r, 29.0f), 0x1D10, y2, 3, buf);
            }
            const f32 left = fpu::Sub(r, 45.0f);
            if (left < 0.0f) typing = true;
            if (4.0f < left) done &= ~(1u << i);
        }
        const f32 left = fpu::Sub(t, 49.0f);
        if (left < 0.0f || typing) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (4.0f < left && (parts & 0x100) != 0 && done == 0) {
            GRef<u16>(obj + 0x27C) &= 0xFEFF;
        }
    }
    const f32 b = GRef<f32>(obj + 0x2CC);
    if (0.0f < b) {
        f32 r = fpu::Sub(b * 90.0f, 10.0f);
        const u32 page = GRef<u32>(obj + 0x2EC);
        bool typing = false;
        u32 done = 0x1F;
        s32 y = 0x4A0;
        for (u32 i = 0; i < 5; ++i, y += 0x340, r = fpu::Sub(r, 4.0f)) {
            if (((static_cast<s32>(rows) >> i) & 1) == 0 || r <= 0.0f) continue;
            const u32 e = page * 5 + i;
            if (GRef<s32>(obj + 0x304 + e * 4) < 0) {
                DrawMenuText(r, 0x1100, y + 0xC0, 3, 0x00409750);
            } else {
                u32 aircraft = static_cast<u32>(GRef<u16>(obj + 0x408 + e * 2)) >> 4;
                if (!(aircraft < 0x15)) aircraft = 0;
                DrawMenuText(r, 0x1100, y + 0xC0, 3, GRef<u32>(0x00384588 + aircraft * 4));
            }
            const f32 left = fpu::Sub(r, 14.0f);
            if (left < 0.0f) typing = true;
            if (4.0f < left) done &= ~(1u << i);
        }
        if (typing) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (done == 0 && (parts & 0x200) != 0) {
            GRef<u16>(obj + 0x27C) &= 0xFDFF;
        }
    }
    const f32 c = GRef<f32>(obj + 0x2D0);
    if (0.0f < c) {
        f32 t = c * 90.0f;
        const s32 pages = GRef<s32>(obj + 0x2E8);
        s32 y = 0x430;
        for (s32 i = 0; i < pages; ++i, y += 0x130) {
            DrawMenuText(t, 0xF00, y, 8, 0x00411320);
            t = fpu::Sub(t, 0.5f);
        }
        const f32 left = fpu::Sub(t, 1.0f);
        if (left < 0.0f) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (4.0f < left && (parts & 0x400) != 0) {
            GRef<u16>(obj + 0x27C) = static_cast<u16>((GRef<u16>(obj + 0x27C) & 0xFBFF) | 0x800);
        }
    }
    EndMenuTextLayer();
    if ((GRef<u16>(obj + 0x27C) & 0x800) != 0 && GRef<u16>(obj + 0x240) == 0) {
        DrawPulseBox(list, index, 0x1067, 0xF00, GRef<s32>(obj + 0x2EC) * 0x130 + 0x430, 0x100,
                     static_cast<s32>(Ctx()->frame_count));
    }
}

// EE: 0x00241E70
// The aircraft icons of the visible rows (sprites from the DATA 0xB7 texture; a placeholder for
// aircraft not owned) and the usage bars.
void DrawRecordIcons(u32 list, s32 index, u32 obj, s64 mask) {
    if (mask == 0) return;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, GRef<u32>(obj + 0x220));
    u32 p = BuildMenuDrawEnv(tag + 0x10, 0x280, 0x1C0);
    const s32 page = GRef<s32>(obj + 0x2EC);
    Q(p, 0) = 0x11AB400000008002ull;
    const s32 n = GRef<s32>(obj + 0x300);
    Q(p, 5) = 0x19;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    Q(p, 4) = 0;
    p += 0x30;
    u32 e = p + 0x10;
    u64 sprites = 0;
    s32 idx = page * 5;
    s32 y = 0x430;
    for (u32 i = 0; i < 5 && idx < n; ++i, ++idx, y += 0x340) {
        if (((static_cast<s32>(mask) >> i) & 1) == 0) continue;
        const u32 key = GRef<u16>(obj + 0x408 + static_cast<u32>(idx) * 2);
        const u32 c = key & 3;
        const s32 aircraft = static_cast<s32>(key) >> 4;
        if (GRef<s32>(obj + 0x304 + static_cast<u32>(idx) * 4) < 0) {
            GRef<u64>(e) = 0x20071002A932B780ull;
        } else {
            const u64 lo = static_cast<u32>(static_cast<s32>(c) * 0x300 + 0x2D00 + aircraft * 0x20) | 0x29328000u;
            const u64 cbp = static_cast<u32>(static_cast<s32>(c) * 0x60 + aircraft * 4 + 0x3600);
            GRef<u64>(e) = lo | 0x2000000000000000ull | (cbp << 37) | 0x280000000ull;
        }
        GRef<u64>(e + 0x18) = Sx(y) << 0x10 | 0x2180;
        ++sprites;
        GRef<u64>(e + 0x28) = Sx(y + 0x2D0) << 0x10 | 0x2600;
        GRef<u64>(e + 8) = 0x80808080;
        GRef<u64>(e + 0x10) = 0x80008;
        GRef<u64>(e + 0x20) = 0x2D80488;
        e += 0x30;
    }
    u32 end = p;
    if (0 < static_cast<s64>(sprites)) {
        Q(p, 1) = 0x53D317;
        Q(p, 0) = sprites | 0x6400000000008000ull;
        end = e;
    }
    idx = page * 5;
    s32 by = 0x620;
    for (u32 i = 0; i < 5 && idx < n; ++i, ++idx, by += 0x340) {
        if (((static_cast<s32>(mask) >> i) & 1) != 0) end = BuildGauge(GRef<f32>(obj + 0x520 + i * 4), end, 0x1100, by, 0xFF0);
    }
    LinkPacket(list, index, start, CloseDmaPacket(tag, end));
}

// EE: 0x00242198
// Draw: rows shown on this page (outline count 3 + rows while drawing), visible rows whose
// outlines are open.
void DrawRecordsPage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), obj);
    const f32 k = K(0x00410374);
    s32 rows = 5 - ((GRef<s32>(obj + 0x2EC) + 1) * 5 - GRef<s32>(obj + 0x300));
    if (rows < 0) {
        rows = 0;
    } else if (5 < rows) {
        rows = 5;
    }
    GRef<s32>(obj + 0x280) = rows + 3;
    u32 open = 0;
    for (u32 i = 0; i < 5; ++i) {
        if (k <= GRef<f32>(obj + 0x294 + i * 4)) open |= 1u << i;
    }
    GRef<u32>(obj + 0x538) = open & ((1u << (static_cast<u32>(rows) & 0x1F)) - 1u);
    DrawPanel(obj, 0, obj + 0x27C);
    DrawRecordsTexts(obj, 0, obj);
    DrawRecordIcons(obj, 0, obj, GRef<s32>(obj + 0x538));
    GRef<s32>(obj + 0x280) = 8;
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x002422B8
// Subs 0x37: creates the page (DATA 0xB7) with the menu BGM.
void EnterRecordsPage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    MainContext* ctx = Ctx();
    ctx->draw_mode = 2;
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    ctx->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateRecordsPage();
    g_RecordsPage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB7, 1, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x38);
    PlayMenuBgm(4);
}

// EE: 0x00242360
void LoadRecordsPage() {
    const u32 obj = g_RecordsPage();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) RequestSubState(0x39);
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x002423A0
void RunRecordsPage() {
    const u32 obj = g_RecordsPage();
    debug::OnGuestCheckpoint(0x002423A0);
    UpdateFade(obj + 0x230);
    RecordsUpdate(obj);
    DrawRecordsPage(obj);
}

AC04_GUEST_FUNCTION(0x002422B8, EnterRecordsPage);
AC04_GUEST_FUNCTION(0x00242360, LoadRecordsPage);
AC04_GUEST_FUNCTION(0x002423A0, RunRecordsPage);

}  // namespace ac04::game
