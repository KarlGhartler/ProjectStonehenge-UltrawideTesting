// Interlude viewer (extras, scene 4 subs 0x34-0x36, page 20; docs/subsystems/frontend.md): a row of
// 12 interlude boxes with a preview of the selected one; X plays it through the movie page (sub 10,
// EnterMenu(0x21) with ret_sub = the interlude), Triangle goes back.
//
// Page object (0x330 bytes, *0x00411848): +0x10 data load queue, +0x220 DATA 0xB9 {font block,
// preview images}, +0x224 preview packet, +0x240 fade object, +0x24C fade level, +0x250 crosshair,
// +0x264/+0x266 crosshair position, +0x28C panel (+0x28E flags, +0x2D8/+0x2DC/+0x2E0 timers a/b/c,
// +0x2E4/+0x2E6 crosshair start, +0x2EC crosshair), +0x2F0 message box (help line), +0x314 state,
// +0x318/+0x31A MenuGoTo target, +0x31C cursor (0-11), +0x320 interlude shown in the preview,
// +0x324 f32 preview level. The cursor is kept in 0x00411498 between visits.
#include "game/game.h"

#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_InterludePage, 0x00411848)    // gp-0x41A8
AC04_GUEST_VAR(s32, g_InterludeCursor, 0x00411498)  // gp-0x4558
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

constexpr u32 kMovieLengths = 0x003AF880;  // s32 frames per movie page n

s32& InterludeState(u32 obj) { return GRef<s32>(obj + 0x314); }

}  // namespace

// EE: 0x00244ED8
// Help line: in state 1 a pending text (+0x2FE) is replaced by message 0 with the type-out timer;
// typed out while the page is up.
void UpdateInterludeHelp(u32 obj) {
    s32 state = InterludeState(obj);
    if (state == 1 && GRef<s16>(obj + 0x2FE) != 0) {
        SetMessageBoxText(obj + 0x2F0, 0);
        state = InterludeState(obj);
        GRef<f32>(obj + 0x2F8) = K(0x004103D0);
    }
    if (state != 0) UpdateMessageBox(K(0x004103D4), obj + 0x2F0, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00244F48
// Preview level: while the cursor rests (no crosshair motion, left/right not held) on the shown
// interlude it rises to 1 (SE 5 when it starts); otherwise it falls to 0, and the resting cursor's
// interlude becomes the shown one once it is 0.
void UpdateInterludePreview(u32 obj) {
    if ((GRef<u16>(obj + 0x28E) & 8) == 0) return;
    bool idle = false;
    if (GRef<u16>(obj + 0x250) == 0) idle = (Ctx()->pad_buttons[0][0] & 0xC) == 0;
    if (idle && GRef<s32>(obj + 0x31C) == GRef<s32>(obj + 0x320)) {
        const f32 a = GRef<f32>(obj + 0x324);
        f32 n = fpu::Add(a, K(0x004103D8));
        if (1.0f < n) n = 1.0f;
        if (a <= 0.0f) PlayMenuSe(0, 5);
        GRef<f32>(obj + 0x324) = n;
        return;
    }
    f32 n = fpu::Sub(GRef<f32>(obj + 0x324), K(0x004103DC));
    if (n < 0.0f) n = 0.0f;
    GRef<f32>(obj + 0x324) = n;
    if (idle && n <= 0.0f) GRef<s32>(obj + 0x320) = GRef<s32>(obj + 0x31C);
}

// EE: 0x00245038
// Cursor on interlude `i` (0 when out of range): crosshair to its box.
void SetInterludeCursor(u32 obj, u64 i) {
    if (i >= 0xC) i = 0;
    const s32 x = static_cast<s32>(i) * 0x140 + 0xD20;
    GRef<s32>(obj + 0x31C) = static_cast<s32>(i);
    GRef<u16>(obj + 0x2E6) = 0xC50;
    GRef<u16>(obj + 0x2E4) = static_cast<u16>(x);
    MoveCrosshairAuto(GRef<u32>(obj + 0x2EC), x, 0xC50);
}

// EE: 0x00245070
// X/Circle (0x50) plays the selected interlude (menu music, page and panel fade out: state 5);
// left/right move the cursor with wrap.
void HandleInterludeInput(u32 obj) {
    const u32 fade = g_pMenuFade();
    const u32 edge = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    GRef<u16>(fade + 0x32) |= 0x1C;
    if ((edge & 0x50) != 0) {
        FadeOutMenuBgm(1.0f);
        StartFade(0.5f, g_pMenuFade(), 1);
        SetPanelState(obj + 0x28C, 1);
        InterludeState(obj) = 5;
        PlayMenuSe(0, 2);
        return;
    }
    if ((repeat & 0xC) == 0) return;
    s32 i = GRef<s32>(obj + 0x31C);
    if ((repeat & 8) != 0) i += 1;
    GRef<u32>(obj + 0x2DC) = 0;
    if ((repeat & 4) != 0) i -= 1;
    GRef<u16>(obj + 0x28C) |= 0x200;
    i = (i + 0xC) % 0xC;
    GRef<s32>(obj + 0x31C) = i;
    SetInterludeCursor(obj, static_cast<u64>(static_cast<s64>(i)));
    PlayMenuSe(0, 4);
}

// EE: 0x00245170
// Allocates and sets up the page: back target page 4, panel (3 rects 0x003AF990), cursor from the
// last visit.
u32 CreateInterludePage() {
    const u32 obj = WorkHeapAlloc(0x330, 0x10);
    const u32 fade = g_pMenuFade();
    InterludeState(obj) = 0;
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    InitFade(obj + 0x240, 1);
    GRef<u16>(obj + 0x318) = 1;
    GRef<u16>(obj + 0x31A) = 4;
    InitPanel(obj + 0x28C, 3, 0x003AF990, obj + 0x250);
    const s32 cursor = g_InterludeCursor();
    GRef<u16>(obj + 0x28E) |= 0xE;
    GRef<s32>(obj + 0x31C) = cursor;
    SetInterludeCursor(obj, static_cast<u64>(static_cast<s64>(cursor)));
    GRef<s32>(obj + 0x320) = -1;
    GRef<f32>(obj + 0x324) = 0.0f;
    return obj;
}

// EE: 0x00245218
// Page states: 0 wait for the fade-in, 1 input (Triangle 0x20 leaves: state 4), 2/4 leaving
// through MenuGoTo once page or panel has faded, 5 to the movie page (sub 10 via EnterMenu(0x21),
// ret_sub = the interlude) once the menu fade has ended.
void UpdateInterludePage(u32 obj) {
    const u32 fade = g_pMenuFade();
    const u32 edge = Ctx()->pad_buttons[0][1];
    GRef<u16>(fade + 0x32) = 0;
    if (InterludeState(obj) != 0) {
        UpdatePanel(obj + 0x28C);
        UpdateInterludePreview(obj);
    }
    UpdateInterludeHelp(obj);
    switch (InterludeState(obj)) {
        case 0:
            if (K(0x004103E0) < GRef<f32>(fade + 0xC)) InterludeState(obj) = 1;
            break;
        case 1:
            GRef<u16>(fade + 0x32) |= 0x20;
            if ((edge & 0x20) != 0) {
                PlayMenuSe(0, 3);
                StartFade(0.25f, obj + 0x240, 1);
                SetPanelState(obj + 0x28C, 1);
                InterludeState(obj) = 4;
                break;
            }
            if ((GRef<u16>(obj + 0x28C) & 0x800) != 0) HandleInterludeInput(obj);
            break;
        case 2:
        case 4:
            if ((GRef<u32>(obj + 0x240) & 8) == 0 && (GRef<u16>(obj + 0x28C) & 8) == 0) break;
            MenuGoTo(GRef<u16>(obj + 0x318), GRef<u16>(obj + 0x31A));
            g_InterludeCursor() = GRef<s32>(obj + 0x31C);
            break;
        case 5:
            if ((GRef<u32>(fade) & 8) != 0) {
                g_InterludeCursor() = GRef<s32>(obj + 0x31C);
                Ctx()->ret_sub = GRef<u16>(0x00411498);
                EnterMenu(0x21);
            }
            break;
        default:
            break;
    }
}

// EE: 0x00245390
// Texts: "INTERLUDE NUMBER >" and the number (timer a), the length mm'ss (timer b), the row of 12
// boxes (timer c); then the pulsing box of the cursor once the row is up. Each part sets panel
// flag 0x8000 (type-out sound) while typing and clears its restart flag (0x100/0x200/0x400) when
// done.
void DrawInterludeTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u16 parts = GRef<u16>(obj + 0x28C);
    GRef<u16>(obj + 0x28C) = parts & 0x7FFF;
    BeginMenuTextLayer(list, index);
    const f32 a = GRef<f32>(obj + 0x2D8);
    if (0.0f < a) {
        const f32 t = a * 90.0f;
        DrawMenuText(t, 0x240, 0x520, 4, 0x004114A0);
        DrawMenuText(fpu::Sub(t, 1.0f), 0x300, 0x520, 4, 0x00409B48);
        const u64 args[1] = {static_cast<u64>(static_cast<s64>(GRef<s32>(obj + 0x31C) + 1))};
        ee::sprintf(buf, 0x004114A8, args, 1);
        DrawMenuText(fpu::Sub(t, 8.0f), 0x300, 0x5D0, 3, buf);
        DrawMenuText(fpu::Sub(t, 16.0f), 0x300, 0x6A0, 4, 0x004114B0);
        const f32 u = fpu::Sub(t, 31.0f);
        if (u < 0.0f) {
            GRef<u16>(obj + 0x28C) |= 0x8000;
        } else if (4.0f < u && (parts & 0x100) != 0) {
            GRef<u16>(obj + 0x28C) &= 0xFEFF;
        }
    }
    const f32 b = GRef<f32>(obj + 0x2DC);
    if (0.0f < b) {
        const f32 t = b * 90.0f;
        const s32 len = GRef<s32>(kMovieLengths + static_cast<u32>(GRef<s32>(obj + 0x31C)) * 4);
        const u64 args[2] = {static_cast<u64>(static_cast<s64>(len / 3600)), static_cast<u64>(static_cast<s64>((len / 60) % 60))};
        ee::sprintf(buf, 0x00409B60, args, 2);  // "%02d'%02d"
        const f32 u = fpu::Sub(t, 24.0f);
        DrawMenuText(u, 0x300, 0x750, 3, buf);
        if (0.0f < u && u < 6.0f) GRef<u16>(obj + 0x28C) |= 0x8000;
        if (4.0f < fpu::Sub(t, 30.0f) && (parts & 0x200) != 0) GRef<u16>(obj + 0x28C) &= 0xFDFF;
    }
    const f32 c = GRef<f32>(obj + 0x2E0);
    if (0.0f < c) {
        const f32 t = c * 90.0f;
        f32 r = t;
        s32 x = 0xCA0;
        for (s32 i = 0xB; i >= 0; --i) {
            DrawMenuText(r, x, 0xBE0, 8, 0x004114B8);
            x += 0x140;
            r = fpu::Sub(r, 0.5f);
        }
        const f32 u = fpu::Sub(t, 7.0f);
        if (u < 0.0f) {
            GRef<u16>(obj + 0x28C) |= 0x8000;
        } else if (4.0f < u && (parts & 0x400) != 0) {
            GRef<u16>(obj + 0x28C) = static_cast<u16>((GRef<u16>(obj + 0x28C) & 0xFBFF) | 0x800);
        }
    }
    EndMenuTextLayer();
    if ((GRef<u16>(obj + 0x28C) & 0x800) != 0 && GRef<u16>(obj + 0x250) == 0) {
        DrawPulseBox(list, index, 0x1067, GRef<s32>(obj + 0x31C) * 0x140 + 0xCA0, 0xBE0, 0x100,
                     static_cast<s32>(Ctx()->frame_count));
    }
}

// EE: 0x00245728
// Preview of interlude `n` at level `level` (0-1): its 64 x 64 still (VRAM 0x2500 + n * 0x40, CLUT
// 0x2800 + n * 4) on a sprite that grows from the crosshair position by sin(level * k), blended
// with ALPHA 0x64 and FIX 0x60.
void DrawInterludePreview(f32 level, u32 list, s32 index, u32 obj, u64 n) {
    if ((GRef<u16>(obj + 0x28E) & 8) == 0 || level <= 0.0f) return;
    if (1.0f < level) level = 1.0f;
    if (n >= 0xC) n = 0;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, GRef<u32>(obj + 0x224));
    u32 p = BuildMenuDrawEnv(tag + 0x10, 0x280, 0x1C0);
    GRef<u64>(p) = 0x11AB400000000001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0x6000000064ull;
    GRef<u64>(p + 0x18) = 0x43;
    const f32 s = ee::sinf(level * K(0x004103E4));
    p += 0x20;
    const s32 xscale = (Ctx()->flags & 0x10000) != 0 ? 0x600 : 0x800;
    const s32 dy = fpu::FloatToInt(s * 1472.0f);
    const s32 dx = fpu::FloatToInt(s * static_cast<f32>(xscale));
    const u16 y0 = GRef<u16>(obj + 0x266);
    const u16 x0 = GRef<u16>(obj + 0x264);
    GRef<u64>(p + 8) = 0x53D317;
    GRef<u64>(p) = 0x6400000000008001ull;
    GRef<u64>(p + 0x10) = Sx(static_cast<s32>(n) * 0x40 + 0x2500) | 0x2000000029308000ull |
                          static_cast<u64>(static_cast<s64>(static_cast<s32>(n) * 4 + 0x2800)) << 37 | 0x280000000ull;
    GRef<u64>(p + 0x38) = Sx(static_cast<s32>(x0) + dx) | Sx(static_cast<s32>(y0) + dy) << 16;
    GRef<u64>(p + 0x18) = 0x80808080;
    GRef<u64>(p + 0x20) = 0x80008;
    GRef<u64>(p + 0x28) = static_cast<u64>(x0) | static_cast<u64>(y0) << 16;
    GRef<u64>(p + 0x30) = 0x5D80808;
    LinkPacket(list, index, start, CloseDmaPacket(tag, p + 0x40));
}

// EE: 0x00245980
// Draws the page: menu fade frame (bucket 1 until the panel is up), preview, panel, texts, help
// line and the black fade.
void DrawInterludePage(u32 obj) {
    const u16 flags = GRef<u16>(obj + 0x28E);
    InitBucketList(obj, 2);
    SetPacketListBuckets(g_pMainContext().addr + offsetof(MainContext, vif1_list), obj);
    DrawMenuFade(obj, (flags & 8) == 0 ? 1 : 0, g_pMenuFade());
    DrawInterludePreview(GRef<f32>(obj + 0x324), obj, 0, obj, static_cast<u64>(static_cast<s64>(GRef<s32>(obj + 0x320))));
    DrawPanel(obj, 0, obj + 0x28C);
    DrawInterludeTexts(obj, 0, obj);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x2F0);
    DrawMessageBox(obj, 0, obj + 0x2F0);
    DrawBlackFade(GRef<f32>(obj + 0x24C), obj, 0);
}

// EE: 0x00245A58
// Sub 0x34: creates the page, queues DATA 0xB9 (2 entries), menu BGM 4; then sub 0x35.
void EnterInterludePage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateInterludePage();
    g_InterludePage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB9, 2, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x35);
    PlayMenuBgm(4);
}

// EE: 0x00245B00
// Sub 0x35: when the data is in, the font and the help box; then sub 0x36.
void LoadInterludePage() {
    const u32 obj = g_InterludePage();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x2F0);
        RequestSubState(0x36);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x00245B50
// Sub 0x36: page update and draw.
void RunInterludePage() {
    const u32 obj = g_InterludePage();
    debug::OnGuestCheckpoint(0x00245B50);
    UpdateFade(obj + 0x240);
    UpdateInterludePage(obj);
    DrawInterludePage(obj);
}

AC04_GUEST_FUNCTION(0x00245A58, EnterInterludePage);
AC04_GUEST_FUNCTION(0x00245B00, LoadInterludePage);
AC04_GUEST_FUNCTION(0x00245B50, RunInterludePage);

}  // namespace ac04::game
