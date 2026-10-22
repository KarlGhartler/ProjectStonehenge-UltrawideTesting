// Options page "SCREEN SETTINGS" (scene 4 subs 0x29-0x2C; docs/subsystems/frontend.md
// "Options (page 17)"): a menu tree with SCREEN POSITION, BRIGHTNESS and the toggles SCREEN RATIO,
// CAPTIONS, HUD UNIT, INTERLUDE (option byte 0x002A15CC, written live), and the screen position
// and brightness adjust views.
//
// Page object (0x730 bytes, *0x00411834): +0x10 data load queue, +0x220 DATA 0xB4 (font block),
// +0x230 fade object, +0x23C fade level, +0x240 crosshair, +0x27C panel (+0x290/+0x294/+0x298
// outline times, +0x2C8/+0x2CC/+0x2D0 timers a/b/c, +0x2D4 crosshair start, +0x2DC crosshair),
// +0x2E0 menu tree (columns 0x003AF2D8, 5 x 0x18), +0x6E8 message box (help line), +0x70C view
// (0 list, 1 screen position, 2 brightness), +0x710 state, +0x714/+0x716 MenuGoTo target,
// +0x718/+0x71C screen position dx/dy, +0x720/+0x724 their values on entry, +0x728 brightness,
// +0x72C its value on entry.
//
// Port baseline (User approval 2026-10-20): GRAPHICS is appended to the list (graphics_page.h);
// it opens view 3 through state 6. The game's tables and ids are unchanged; the "port:" lines are
// the only additions.
#include "game/game.h"

#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/graphics_page.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u32, g_OptionsPage, 0x00411834)  // gp-0x41BC
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kOptionBytes = 0x002A15CC;  // flags; +1 brightness; +2/+3 screen position dx/dy
constexpr u32 kOptionColumns = 0x003AF2D8;  // menu tree columns (5 x 0x18, cursors kept here)

s32& State(u32 obj) { return GRef<s32>(obj + 0x710); }
u8& Flags() { return GRef<u8>(kOptionBytes); }

void MoveBrightnessCursor(u32 obj, s32 level) {
    GRef<u16>(obj + 0x2D6) = 0x610;
    const s32 x = level * 0x140 + 0xE80;
    GRef<s16>(obj + 0x2D4) = static_cast<s16>(x);
    MoveCrosshairAuto(GRef<u32>(obj + 0x2DC), static_cast<s16>(x), 0x610);
}

}  // namespace

// EE: 0x0023F350
// Help line of the options page: the selected item's message (toggles show their current state),
// 1 in the screen position view, 5 in the brightness view; hidden while leaving or adjusting.
void UpdateOptionsHelp(u32 obj) {
    const u32 state = GRef<u32>(obj + 0x710);
    if (state == 0 || (state < 6 && 1 < state) || state == 6) {  // port: 6 = opening GRAPHICS
        GRef<u32>(obj + 0x6F0) = 0;
        return;
    }
    const s32 view = GRef<s32>(obj + 0x70C);
    if (view == 3) {  // port: GRAPHICS view
        const u32 mb = obj + 0x6E8;
        if (GRef<s16>(mb + 0xC) != 2 || GRef<s16>(obj + 0x6F6) != 1) {
            SetGraphicsHelp(mb);
            GRef<f32>(obj + 0x6F0) = K(0x0041034C);
        }
        UpdateMessageBox(K(0x00410350), mb, static_cast<s32>(Ctx()->frame_count));
        return;
    }
    s16 id = -1;
    if (view == 1) {
        id = 1;
    } else if (view == 2) {
        id = 5;
    } else if (view == 0) {
        const s16 selection = OptionsSelection(obj + 0x2E0);  // port: GRAPHICS in the list
        const u64 sel = static_cast<u64>(static_cast<s64>(selection));
        const u64 item = sel < 0xE ? sel : 0;
        id = selection == kGraphicsItem ? -1 : GRef<s16>(0x003AF440 + static_cast<u32>(item) * 2);
        if (item == 2) id = (Flags() & 1) != 0 ? 10 : 8;
        else if (item == 3) id = (Flags() & 2) != 0 ? 0x12 : 0x10;
        else if (item == 4) id = (Flags() & 4) != 0 ? 0x18 : 0x16;
        else if (item == 5) id = (Flags() & 8) != 0 ? 0x20 : 0x1E;
    }
    const u32 mb = obj + 0x6E8;
    if (GRef<s16>(obj + 0x6F6) != id) {
        SetMessageBoxText(mb, id);
        GRef<f32>(obj + 0x6F0) = K(0x0041034C);
    }
    UpdateMessageBox(K(0x00410350), mb, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x0023F4D0
// Sets the cursors of the four toggle columns from the option flags.
void SyncOptionToggles(u32 cols, u32 flags) {
    const u8 f = GRef<u8>(flags);
    GRef<u16>(cols + 0x1E) = (f & 1) != 0 ? 1 : 0;
    GRef<u16>(cols + 0x36) = (f & 2) != 0 ? 1 : 0;
    GRef<u16>(cols + 0x4E) = (f & 4) != 0 ? 1 : 0;
    GRef<u16>(cols + 0x66) = (f & 8) != 0 ? 1 : 0;
}

// EE: 0x0023F538
// Switches the view: 0 the list (CAPTIONS hidden before the first clear), 1 screen position
// (frame rectangle for 4:3 or 16:9), 2 brightness.
void SetOptionsView(u32 obj, s32 view) {
    if (view == 3) {  // port: GRAPHICS view
        EnterGraphicsView(obj);
        return;
    }
    State(obj) = 0;
    InitFade(obj + 0x230, 1);
    if (view == 1) {
        GRef<s32>(obj + 0x70C) = 1;
        const s32 dx = GRef<s8>(kOptionBytes + 2);
        GRef<s32>(obj + 0x718) = dx;
        GRef<s32>(obj + 0x720) = dx;
        const s32 dy = GRef<s8>(kOptionBytes + 3);
        GRef<s32>(obj + 0x71C) = dy;
        GRef<s32>(obj + 0x724) = dy;
        const u32 src = (Flags() & 1) != 0 ? 0x003AF430 : 0x003AF420;
        GRef<u64>(0x003AF3C0) = GRef<u64>(src);
        GRef<u64>(0x003AF3C8) = GRef<u64>(src + 8);
        InitPanel(obj + 0x27C, 3, 0x003AF3A0, obj + 0x240);
        GRef<u16>(obj + 0x27E) |= 2;
        MoveCrosshairAuto(GRef<u32>(obj + 0x2DC), 0x1400, 0xE00);
    } else if (view < 2) {
        if (view != 0) return;
        GRef<s32>(obj + 0x70C) = 0;
        guest::GuestFrame frame(0x20);
        const u32 masks = frame.Address();
        ee::memset(masks, 0, 0x14);
        if ((GRef<u32>(0x002A0318) & 1) == 0) GRef<u32>(masks) |= 0x20;
        SyncOptionToggles(kOptionColumns, kOptionBytes);
        const GraphicsItemPatch patch(kOptionColumns);  // port: GRAPHICS in the list
        InitMenuTree(obj + 0x2E0, 5, kOptionColumns, 0x003AF350, masks, 0x004112C0);
    } else if (view == 2) {
        GRef<s32>(obj + 0x70C) = 2;
        const u32 level = GRef<u8>(kOptionBytes + 1);
        GRef<u32>(obj + 0x728) = level;
        GRef<u32>(obj + 0x72C) = level;
        InitPanel(obj + 0x27C, 5, 0x003AF3D0, obj + 0x240);
        GRef<u16>(obj + 0x27E) |= 0xE;
        MoveCrosshairAuto(GRef<u32>(obj + 0x2DC), GRef<s32>(obj + 0x728) * 0x140 + 0xE80, 0x610);
    }
}

// EE: 0x0023F740
// From the main menu (sub 0x29) back to page 4 with the menu BGM; otherwise (sub 0x2A) back to
// sub-page 0x18 with the campaign progress BGM.
u32 CreateOptionsPage() {
    const u32 obj = WorkHeapAlloc(0x730, 0x10);
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(obj + 0x714) = 1;
    if (Ctx()->sub == 0x29) {
        GRef<u16>(obj + 0x716) = 4;
        PlayMenuBgm(4);
    } else {
        GRef<u16>(obj + 0x716) = 0x18;
        PlayMenuBgm(static_cast<s16>(GetProgressBgmTrack(1, g_MissionNo())));
    }
    SetOptionsView(obj, 0);
    return obj;
}

// EE: 0x0023F7D8
// Triangle in the list: leaves the page from column 0, else closes the toggle column and restores
// its stored cursor.
void OptionsBack(u32 obj, u32 tree) {
    const s32 col = GRef<s32>(tree);
    if (col == 0) {
        StartFade(0.5f, obj + 0x230, 1);
        State(obj) = 5;
    } else {
        GRef<s32>(tree) = 0;
        GRef<u16>(tree + static_cast<u32>(col) * 0x18 + 0xE) = GRef<u16>(0x003AF2DE + static_cast<u32>(col) * 0x18);
        SetListWidgetState(tree + 8 + static_cast<u32>(col) * 0x18, 3);
        PlayMenuSe(0, 3);
    }
    PlayMenuSe(0, 3);
}

// EE: 0x0023F870
// Confirm in the list: SCREEN POSITION / BRIGHTNESS fade to their views; the toggles open their
// column on the stored setting, and a choice in a column closes it and stores it.
void OptionsSelect(u32 obj, u32 tree) {
    const s16 sel = OptionsSelection(tree);  // port: GRAPHICS in the list
    const f32 duration = K(0x00410354);
    auto open = [tree](s32 col) {
        GRef<s32>(tree) = col;
        GRef<u16>(tree + static_cast<u32>(col) * 0x18 + 0xE) = GRef<u16>(0x003AF2DE + static_cast<u32>(col) * 0x18);
        SetListWidgetState(tree + 8 + static_cast<u32>(col) * 0x18, 1);
    };
    auto close = [tree](s32 col) {
        GRef<s32>(tree) = 0;
        SetListWidgetState(tree + 8 + static_cast<u32>(col) * 0x18, 3);
        GRef<u16>(0x003AF2DE + static_cast<u32>(col) * 0x18) = GRef<u16>(tree + static_cast<u32>(col) * 0x18 + 0xE);
    };
    switch (sel) {
        case 0:
            State(obj) = 2;
            StartFade(duration, obj + 0x230, 1);
            break;
        case 1:
            State(obj) = 3;
            StartFade(duration, obj + 0x230, 1);
            break;
        case kGraphicsItem:  // port: GRAPHICS view through state 6
            State(obj) = 6;
            StartFade(duration, obj + 0x230, 1);
            break;
        case 2: open(1); break;
        case 3: open(2); break;
        case 4: open(3); break;
        case 5: open(4); break;
        case 6: case 7: close(1); break;
        case 8: case 9: close(2); break;
        case 10: case 11: close(3); break;
        case 12: case 13: close(4); break;
        default: break;
    }
    PlayMenuSe(0, 2);
}

// EE: 0x0023FB48
// List view: tree navigation; the toggle columns write the option flags live (SCREEN RATIO also
// the 16:9 context flag 0x10000). States 2/3 open the adjust views, 5 leaves the page.
void OptionsListUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    const s32 state = State(obj);
    const u32 edge = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    GRef<u16>(fade + 0x32) = 0;
    switch (state) {
        case 0:
            if (K(0x00410358) < GRef<f32>(fade + 0xC)) State(obj) = 1;
            break;
        case 1: {
            const u32 tree = obj + 0x2E0;
            GRef<u16>(fade + 0x32) = 0x33;
            UpdateMenuTree(tree, repeat);
            const s32 col = GRef<s32>(obj + 0x2E0);
            if ((edge & 0x20) != 0) {
                OptionsBack(obj, tree);
            } else if ((edge & 0x50) != 0) {
                OptionsSelect(obj, tree);
            }
            const s16 v = GRef<s16>(obj + static_cast<u32>(col) * 0x18 + 0x2EE);
            switch (col) {
                case 1:
                    if (v != 0) {
                        Flags() |= 1;
                        Ctx()->flags |= 0x10000;
                    } else {
                        Flags() &= 0xFE;
                        Ctx()->flags &= 0xFFFEFFFFu;
                    }
                    break;
                case 2: Flags() = v != 0 ? (Flags() | 2) : (Flags() & 0xFD); break;
                case 3: Flags() = v != 0 ? (Flags() | 4) : (Flags() & 0xFB); break;
                case 4: Flags() = v != 0 ? (Flags() | 8) : (Flags() & 0xF7); break;
                default: break;
            }
            break;
        }
        case 2:
        case 3:
        case 6:  // port: GRAPHICS
            if ((GRef<u32>(obj + 0x230) & 8) != 0) {
                {
                    const GraphicsItemPatch patch(kOptionColumns);  // port: GRAPHICS in the list
                    StoreMenuTreeCursors(kOptionColumns, 5, obj + 0x2E0);
                }
                SetOptionsView(obj, State(obj) == 6 ? 3 : (State(obj) != 2 ? 2 : 1));
            }
            break;
        case 5: {
            if ((GRef<u32>(obj + 0x230) & 8) == 0) return;
            MenuGoTo(GRef<s16>(obj + 0x714), GRef<u16>(obj + 0x716));
            const GraphicsItemPatch patch(kOptionColumns);  // port: GRAPHICS in the list
            StoreMenuTreeCursors(kOptionColumns, 5, obj + 0x2E0);
            break;
        }
        default:
            break;
    }
}

// EE: 0x0023FD10
// Cancel in an adjust view: restores the values from entering it.
void OptionsAdjustCancel(u32 obj) {
    SetPanelState(obj + 0x27C, 1);
    State(obj) = 4;
    PlayMenuSe(0, 3);
    if (GRef<s32>(obj + 0x70C) == 1) {
        GRef<u8>(kOptionBytes + 2) = GRef<u8>(obj + 0x720);
        GRef<u8>(kOptionBytes + 3) = GRef<u8>(obj + 0x724);
        ApplyDisplayOffset(kOptionBytes);
    } else if (GRef<s32>(obj + 0x70C) == 2) {
        GRef<u8>(kOptionBytes + 1) = GRef<u8>(obj + 0x72C);
        ApplyBrightness(kOptionBytes);
    }
}

// EE: 0x0023FDB8
// Confirm in an adjust view: keeps the adjusted values.
void OptionsAdjustAccept(u32 obj) {
    SetPanelState(obj + 0x27C, 1);
    State(obj) = 4;
    PlayMenuSe(0, 2);
    if (GRef<s32>(obj + 0x70C) == 1) {
        GRef<u8>(kOptionBytes + 2) = GRef<u8>(obj + 0x718);
        GRef<u8>(kOptionBytes + 3) = GRef<u8>(obj + 0x71C);
        ApplyDisplayOffset(kOptionBytes);
    } else if (GRef<s32>(obj + 0x70C) == 2) {
        GRef<u8>(kOptionBytes + 1) = GRef<u8>(obj + 0x728);
        ApplyBrightness(kOptionBytes);
    }
}

// EE: 0x0023FE60
// Screen position view: the D-pad moves the picture (-12..12 each way, applied at once), pad bit
// 0x4000 resets it.
void ScreenPositionUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    const u32 edge = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    UpdatePanel(obj + 0x27C);
    const s32 state = State(obj);
    if (state == 1) {
        GRef<u16>(fade + 0x32) = 0x3F;
        if ((repeat & 0xF) != 0) {
            const s32 dx0 = GRef<s32>(obj + 0x718);
            s32 dx = dx0;
            if ((repeat & 4) != 0) dx = dx0 - 1;
            s32 dy = GRef<s32>(obj + 0x71C) - static_cast<s32>(repeat & 1);
            if ((repeat & 8) != 0) dx += 1;
            if ((repeat & 2) != 0) dy += 1;
            if (dx < -0xC) dx = -0xC;
            else if (0xC < dx) dx = 0xC;
            if (dy < -0xC) dy = -0xC;
            else if (0xC < dy) dy = 0xC;
            if (dx != dx0 || dy != GRef<s32>(obj + 0x71C)) {
                PlayMenuSe(0, 4);
                GRef<s32>(obj + 0x718) = dx;
                GRef<s32>(obj + 0x71C) = dy;
                GRef<s8>(kOptionBytes + 2) = static_cast<s8>(dx);
                GRef<s8>(kOptionBytes + 3) = static_cast<s8>(dy);
                ApplyDisplayOffset(kOptionBytes);
            }
        }
        if ((edge & 0x20) != 0) {
            OptionsAdjustCancel(obj);
            return;
        }
        if ((edge & 0x50) != 0) {
            OptionsAdjustAccept(obj);
            return;
        }
        if ((edge & 0x4000) != 0) {
            PlayMenuSe(0, 3);
            ResetScreenPosition(kOptionBytes);
            ApplyDisplayOffset(kOptionBytes);
            GRef<s32>(obj + 0x718) = GRef<s8>(kOptionBytes + 2);
            GRef<s32>(obj + 0x71C) = GRef<s8>(kOptionBytes + 3);
        }
    } else if (state == 0) {
        if ((GRef<u16>(obj + 0x27C) & 4) != 0) State(obj) = 1;
    } else if (state == 4 && (GRef<u16>(obj + 0x27C) & 8) != 0) {
        SetOptionsView(obj, 0);
    }
}

// EE: 0x00240088
// Brightness view: left/right change the level 0-6 (applied at once), pad bit 0x4000 resets it.
void BrightnessUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    u32 edge = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    UpdatePanel(obj + 0x27C);
    const s32 state = State(obj);
    if (state == 1) {
        const s32 level = GRef<s32>(obj + 0x728);
        GRef<u16>(fade + 0x32) = 0x30;
        if (0 < level) GRef<u16>(fade + 0x32) = 0x34;
        if (level < 6) GRef<u16>(fade + 0x32) |= 8;
        if ((repeat & 0xC) != 0) {
            s32 next = level;
            if ((repeat & 4) != 0) next = level - 1;
            if ((repeat & 8) != 0) next += 1;
            if (next < 0) next = 0;
            else if (6 < next) next = 6;
            if (next != level) {
                PlayMenuSe(0, 4);
                GRef<s32>(obj + 0x728) = next;
                MoveBrightnessCursor(obj, next);
                GRef<u8>(kOptionBytes + 1) = static_cast<u8>(next);
                ApplyBrightness(kOptionBytes);
            }
        }
        if ((edge & 0x20) != 0) {
            OptionsAdjustCancel(obj);
            return;
        }
        if ((edge & 0x50) != 0) {
            OptionsAdjustAccept(obj);
            return;
        }
        if ((edge & 0x4000) != 0) {
            PlayMenuSe(0, 3);
            ResetBrightness(kOptionBytes);
            edge = GRef<u8>(kOptionBytes + 1);
            if (GRef<u32>(obj + 0x728) != edge) {
                GRef<u32>(obj + 0x728) = edge;
                MoveBrightnessCursor(obj, static_cast<s32>(edge));
                ApplyBrightness(kOptionBytes);
            }
        }
    } else if (state == 0) {
        if ((GRef<u16>(obj + 0x27C) & 4) != 0) State(obj) = 1;
    } else if (state == 4 && (GRef<u16>(obj + 0x27C) & 8) != 0) {
        SetOptionsView(obj, 0);
    }
}

// EE: 0x002402E0
void OptionsUpdate(u32 obj) {
    UpdateOptionsHelp(obj);
    const s32 view = GRef<s32>(obj + 0x70C);
    if (view == 1) {
        ScreenPositionUpdate(obj);
    } else if (view == 0) {
        OptionsListUpdate(obj);
    } else if (view == 2) {
        BrightnessUpdate(obj);
    } else if (view == 3) {
        GraphicsUpdate(obj);  // port: GRAPHICS view
    }
}

// EE: 0x00240368
// Screen position view: the screen frame (line strip in 0x686054 from the rectangle at
// 0x003AF3C0) and a bar inside it.
void DrawScreenFrame(u32 list, s32 index, u32 obj) {
    const u32 x = GRef<u16>(0x003AF3C0);
    const u32 y = GRef<u16>(0x003AF3C2);
    if (GRef<f32>(obj + 0x290) < K(0x0041035C)) return;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 hh = static_cast<u32>(GRef<u16>(0x003AF3C6) >> 1);
    const u32 hw = static_cast<u32>(GRef<u16>(0x003AF3C4) >> 1);
    const u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    const s32 right = static_cast<s32>(x + hw);
    const s32 bottom = static_cast<s32>(y + hh);
    const s32 left = static_cast<s32>(x - hw);
    const s32 top = static_cast<s32>(y - hh);
    Q(p, 0) = 0x1000000000000001ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    Q(p, 4) = 0xA400000000008001ull;
    Q(p, 5) = 0x5D105D5D10ull;
    Q(p, 6) = 0x2CA;
    Q(p, 7) = 0x686054;
    Q(p, 8) = Sx(left) | Sx(top) << 0x10;
    Q(p, 9) = Sx(right) | Sx(bottom) << 0x10;
    Q(p, 0xB) = Sx(right) | Sx(top) << 0x10;
    Q(p, 0xA) = Sx(left) | Sx(bottom) << 0x10;
    Q(p, 0xC) = 0x246;
    Q(p, 0xE) = Sx(left + 0x10) | Sx(top + 0x10) << 0x10;
    Q(p, 0xF) = Sx(right - 0x10) | Sx(bottom - 0x10) << 0x10;
    Q(p, 0xD) = 0x1067;
    LinkPacket(list, index, start, CloseDmaPacket(start, p + 0x80));
}

// EE: 0x00240540
// Brightness view texts (typed out by timers a and c) and the level marks; the pulsing box on the
// current level once the marks are typed out.
void DrawBrightnessTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u32 parts = GRef<u16>(obj + 0x27C);
    GRef<u16>(obj + 0x27C) = static_cast<u16>(parts & 0x7FFF);
    BeginMenuTextLayer(list, index);
    const f32 a = GRef<f32>(obj + 0x2C8);
    if (0.0f < a) {
        f32 t = a * 90.0f;
        DrawMenuText(t, 0x240, 0x520, 4, 0x004112C8);
        t = fpu::Sub(t, 1.0f);
        DrawMenuText(t, 0x300, 0x520, 4, 0x00409668);
        const u64 args[1] = {static_cast<u64>(static_cast<s64>(GRef<s32>(obj + 0x728) + 1))};
        ee::sprintf(buf, 0x00409678, args, 1);  // "LEVEL %02d"
        DrawMenuText(fpu::Sub(t, 8.0f), 0x300, 0x5C0, 3, buf);
        const f32 t16 = fpu::Sub(t, 16.0f);
        DrawMenuText(t16, 0x300, 0x6D0, 4, 0x00409688);
        DrawMenuText(fpu::Sub(t, 20.0f), 0x340, 0x8C0, 4, 0x004112D0);
        DrawMenuText(fpu::Sub(t, 21.0f), 0x300, 0x950, 4, 0x004096A0);
        const f32 t24 = fpu::Sub(t, 24.0f);
        DrawMenuText(t24, 0x1340, 0x8C0, 4, 0x004112D0);
        DrawMenuText(fpu::Sub(t, 25.0f), 0x1100, 0x950, 4, 0x004112D8);
        DrawMenuText(t16, 0x240, 0xC20, 4, 0x004112C8);
        DrawMenuText(fpu::Sub(t16, 1.0f), 0x300, 0xC20, 4, 0x004096B0);
        DrawMenuText(t24, 0xB00, 0xC20, 4, 0x004096C0);
        if (a < 0.0f) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (6.0f < a && (parts & 0x100) != 0) {
            GRef<u16>(obj + 0x27C) &= 0xFEFF;
        }
    }
    const f32 c = GRef<f32>(obj + 0x2D0);
    if (0.0f < c) {
        const f32 t = c * 90.0f;
        f32 m = fpu::Sub(t, 4.0f);
        s32 x = 0xE00;
        for (s32 i = 6; i >= 0; --i) {
            DrawMenuText(m, x, 0x5A0, 8, 0x004112E0);
            x += 0x140;
            m = fpu::Sub(m, 1.0f);
        }
        const f32 left = fpu::Sub(t, 30.0f);
        if (left < 0.0f) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (5.0f < left && (parts & 0x400) != 0) {
            GRef<u16>(obj + 0x27C) = static_cast<u16>((GRef<u16>(obj + 0x27C) & 0xFBFF) | 0x800);
        }
    }
    EndMenuTextLayer();
    if ((GRef<u16>(obj + 0x27C) & 0x800) != 0 && GRef<u16>(obj + 0x240) == 0) {
        DrawPulseBox(list, index, 0x1067, GRef<s32>(obj + 0x728) * 0x140 + 0xE00, 0x5A0, 0x100,
                     static_cast<s32>(Ctx()->frame_count));
    }
}

// EE: 0x002408E0
// Brightness test pattern: 17 grey steps from white to black.
u32 BuildGreyRamp(u32 p) {
    Q(p, 0) = 0x4400000000008011ull;
    Q(p, 1) = 0x5D10;
    u32 q = p + 0x10;
    s32 x0 = 0x300;
    s32 x1 = 0x400;
    for (s32 i = 0; i < 0x11; ++i, q += 0x20, x0 += 0x100, x1 += 0x100) {
        GRef<u64>(q) = 0x246;
        GRef<u64>(q + 8) = LerpColor(static_cast<f32>(i) * 0.0625f, 0xFFFFFF, 0) & 0xFFFFFFFFull;
        GRef<u64>(q + 0x10) = Sx(x0) | 0x7900000;
        GRef<u64>(q + 0x18) = Sx(x1) | 0x8800000;
    }
    return q;
}

// EE: 0x002409D0
// Brightness test pattern: three rows (white, grey, black) of four squares.
u32 BuildLevelSquares(u32 p) {
    Q(p, 0) = 0x440000000000800Cull;
    Q(p, 1) = 0x5D10;
    u32 q = p + 0x10;
    s32 y = 0xCD0;
    for (s32 row = 0; row < 3; ++row, y += 0x170) {
        u64 color = 0xFFFFFF;
        if (row != 0) color = row != 1 ? 0 : 0x808080;
        s32 x = 0x300;
        for (s32 k = 3; k >= 0; --k, x += 0x180, q += 0x20) {
            GRef<u64>(q + 0x10) = Sx(x) | Sx(y) << 0x10;
            GRef<u64>(q + 0x18) = Sx(x + 0xF0) | Sx(y + 0xF0) << 0x10;
            GRef<u64>(q) = 0x246;
            GRef<u64>(q + 8) = color;
        }
    }
    return q;
}

// EE: 0x00240A88
// Brightness test pattern: red, green and blue ramps of 17 steps.
u32 BuildColorRamps(u32 p) {
    Q(p, 0) = 0x4400000000008033ull;
    Q(p, 1) = 0x5D10;
    u32 q = p + 0x10;
    s32 y = 0xCE0;
    for (u32 c = 0; c < 3; ++c, y += 0xB0) {
        s32 x0 = 0xB00;
        s32 x1 = 0xB80;
        for (s32 i = 0; i < 0x11; ++i, q += 0x20, x0 += 0x80, x1 += 0x80) {
            GRef<u64>(q) = 0x246;
            GRef<u64>(q + 8) = LerpColor(static_cast<f32>(i) * 0.0625f, Sx(0xFF << ((c & 3) << 3)), 0) & 0xFFFFFFFFull;
            GRef<u64>(q + 0x10) = Sx(x0) | Sx(y) << 0x10;
            GRef<u64>(q + 0x18) = Sx(x1) | Sx(y + 0x70) << 0x10;
        }
    }
    return q;
}

// EE: 0x00240BB8
// The brightness test patterns at strength `level` (ALPHA 0x64, FIX level * 128), each once its
// panel outline is open.
void DrawBrightnessPatterns(f32 level, u32 list, s32 index, u32 obj) {
    if (!(0.0f < level)) return;
    const u32 start = Ctx()->cur_buf.addr;
    const f32 k = K(0x00410360);
    u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    const f32 t2 = GRef<f32>(obj + 0x290);
    Q(p, 0) = 0x1000000000008002ull;
    Q(p, 1) = 0xE;
    Q(p, 3) = 0x43;
    Q(p, 5) = 0x19;
    Q(p, 4) = 0;
    Q(p, 2) = Sx(fpu::FloatToInt(level * 128.0f)) << 0x20 | 100;
    p += 0x30;
    if (k < t2) p = BuildGreyRamp(p);
    if (k < GRef<f32>(obj + 0x294)) p = BuildLevelSquares(p);
    if (k < GRef<f32>(obj + 0x298)) p = BuildColorRamps(p);
    LinkPacket(list, index, start, CloseDmaPacket(start, p));
}

// EE: 0x00240D28
void DrawOptionsPage(u32 obj) {
    InitBucketList(obj, 2);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), obj);
    const s32 view = GRef<s32>(obj + 0x70C);
    if (view == 1) {
        DrawPanel(obj, 0, obj + 0x27C);
        DrawScreenFrame(obj, 0, obj);
    } else if (view == 0) {
        DrawMenuTree(obj, 0, obj + 0x2E0);
    } else if (view == 2) {
        DrawBrightnessTexts(obj, 0, obj);
        f32 level = GRef<f32>(obj + 0x2CC) * K(0x00410364);
        if (1.0f < level) level = 1.0f;
        DrawBrightnessPatterns(level, obj, 1, obj);
        DrawPanel(obj, 1, obj + 0x27C);
    } else if (view == 3) {
        DrawGraphicsView(obj);  // port: GRAPHICS view
    }
    UploadMessageBoxGlyphs(obj, 0, obj + 0x6E8);
    DrawMessageBox(obj, 0, obj + 0x6E8);
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x00240E78
// Subs 0x29/0x2A: creates the page and queues DATA 0xB4.
void EnterOptionsPage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    MainContext* ctx = Ctx();
    ctx->draw_mode = 2;
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    ctx->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateOptionsPage();
    g_OptionsPage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB4, 1, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x2B);
}

// EE: 0x00240F18
// Sub 0x2B: waits for DATA 0xB4.
void LoadOptionsPage() {
    const u32 obj = g_OptionsPage();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x6E8);
        RequestSubState(0x2C);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x00240F68
// Sub 0x2C: the page.
void RunOptionsPage() {
    const u32 obj = g_OptionsPage();
    debug::OnGuestCheckpoint(0x00240F68);
    UpdateFade(obj + 0x230);
    OptionsUpdate(obj);
    DrawOptionsPage(obj);
}

AC04_GUEST_FUNCTION(0x00240E78, EnterOptionsPage);
AC04_GUEST_FUNCTION(0x00240F18, LoadOptionsPage);
AC04_GUEST_FUNCTION(0x00240F68, RunOptionsPage);

}  // namespace ac04::game
