// Post-mission menu (scene 4 subs 0x26-0x28, page 0xB; slice S7; docs/subsystems/frontend.md): the
// menu after a campaign mission's results (continue, retry, save, ...) as a 2-column menu tree
// with a help line.
//
// Page object (0x680 bytes, *0x0041181C): +0 bucket list, +0x10 data load queue, +0x220 DATA 0xB2
// (font block), +0x230 fade (+0x23C level), +0x240 menu tree (+0x240 open column), +0x648 message
// box (help line; +0x650 type-out position, +0x656 shown message), +0x66C entered from sub 0x26,
// +0x670 state (0 fade in, 1 menu, 2 leaving to a sub-page, 3 leaving to another page or scene),
// +0x674/+0x678 MenuGoTo target (kind, value).
#include "game/game.h"

#include <cstddef>

#include "common/ee_math.h"
#include "common/guest_stack.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_PostMissionMenu, 0x0041181C)  // gp-0x41D4
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

constexpr u32 kColumns = 0x0039E220;       // 2 columns
constexpr u32 kHelpMessages = 0x0039E268;  // s16 per menu item (6)

f32 K(u32 address) { return GRef<f32>(address); }
s32& State(u32 obj) { return GRef<s32>(obj + 0x670); }

}  // namespace

// EE: 0x00235C68
// Help line: in the menu, the selected item's help message (table 0x0039E268; item 2: message 6
// when entered from sub 0x26, else 5) is shown with the type-out restarted (K 0x0041028C); typed
// out (K 0x00410290) while the page is up.
void UpdatePostMissionHelp(u32 obj) {
    s32 state = State(obj);
    if (state == 1) {
        u64 sel = static_cast<u64>(static_cast<s64>(GetMenuTreeSelection(obj + 0x240)));
        if (!(sel < 6)) sel = 0;
        s16 msg;
        if (sel == 2)
            msg = GRef<s32>(obj + 0x66C) == 0 ? 5 : 6;
        else
            msg = GRef<s16>(kHelpMessages + static_cast<u32>(sel) * 2);
        if (GRef<s16>(obj + 0x656) != msg) {
            SetMessageBoxText(obj + 0x648, msg);
            GRef<f32>(obj + 0x650) = K(0x0041028C);
        }
        state = State(obj);
    }
    if (state != 0) UpdateMessageBox(K(0x00410290), obj + 0x648, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00235D20
// Creates the page object (work heap, 0x680 bytes): menu fade requests, fade, menu BGM fade-out
// over 3 s, and the menu tree (columns 0x0039E220, connections 0x0039E250, parents 0x00411070;
// column 0 hides items mask 8, or mask 3 when entered from sub 0x26, which also sets +0x66C).
u32 CreatePostMissionMenu() {
    const u32 obj = WorkHeapAlloc(0x680, 0x10);
    const u32 fade = g_pMenuFade();
    State(obj) = 0;
    GRef<s32>(obj + 0x66C) = 0;
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    InitFade(obj + 0x230, 1);
    FadeOutMenuBgm(3.0f);
    guest::GuestFrame frame(0x10);
    const u32 masks = frame.Address();
    GRef<u32>(masks) = 0;
    GRef<u32>(masks + 4) = 0;
    if (Ctx()->sub == 0x26) {
        GRef<s32>(obj + 0x66C) = 1;
        GRef<u32>(masks) = 3;
    } else {
        GRef<u32>(masks) = 8;
    }
    InitMenuTree(obj + 0x240, 2, kColumns, 0x0039E250, masks, 0x00411070);
    return obj;
}

// EE: 0x00235DE8
// Leaves with MenuGoTo(`kind`, `value`): the menu fade and the page fade out over 0.5 s.
void LeavePostMissionMenu(u32 obj, s32 kind, s32 value) {
    State(obj) = 3;
    StartFade(0.5f, g_pMenuFade(), 1);
    StartFade(0.5f, obj + 0x230, 1);
    GRef<s32>(obj + 0x678) = value;
    GRef<s32>(obj + 0x674) = kind;
}

// EE: 0x00235E68
// Leaves to sub-page `sub` (MenuGoTo kind 1): the page fades out over 0.5 s.
void LeavePostMissionMenuToSubPage(u32 obj, s32 sub) {
    State(obj) = 2;
    StartFade(0.5f, obj + 0x230, 1);
    GRef<s32>(obj + 0x678) = sub;
    GRef<s32>(obj + 0x674) = 1;
}

// EE: 0x00235EC0
// Triangle: closes the open column (SE 3).
void PostMissionMenuBack(u32 obj, u32 tree) {
    (void)obj;
    const s32 open = GRef<s32>(tree);
    if (open == 0) return;
    GRef<s32>(tree) = 0;
    SetListWidgetState(tree + static_cast<u32>(open) * 0x18 + 8, 3);
    PlayMenuSe(0, 3);
}

// EE: 0x00235F10
// X on a menu item (selection 0-5, jump table 0x00407390), SE 2:
// 0 apply the results, clamp the statistics, next mission, then the menu page 0x20 (mission 12:
// return value 6) or 2; 1 retry (MenuGoTo kind 3: scene 2 again); 2 the save pages (0x4C / 0x4B by
// +0x66C), or column 1 with ctx flag 0x80000; 3 apply, clamp, next mission, menu page 0; 4 sub-page
// 0x53 / 0x52; 5 sub-page 0x4C / 0x4B.
void PostMissionMenuSelect(u32 obj, u32 tree) {
    const u64 sel = static_cast<u64>(static_cast<s64>(GetMenuTreeSelection(tree)));
    switch (sel) {
        case 0:
            ApplyMissionResults();
            ClampStatistics(0x002A0318);
            AdvanceCampaignMission();
            if (GRef<u16>(0x002A15B8) == 0xC) {
                Ctx()->ret_sub = 6;
                LeavePostMissionMenu(obj, 0, 0x20);
            } else {
                LeavePostMissionMenu(obj, 0, 2);
            }
            break;
        case 1: LeavePostMissionMenu(obj, 3, 0); break;
        case 2:
            if ((Ctx()->flags & 0x80000) != 0) {
                GRef<s32>(tree) = 1;
                SetListWidgetState(tree + 0x20, 1);
            } else {
                LeavePostMissionMenuToSubPage(obj, GRef<s32>(obj + 0x66C) != 0 ? 0x4C : 0x4B);
            }
            break;
        case 3:
            ApplyMissionResults();
            ClampStatistics(0x002A0318);
            AdvanceCampaignMission();
            LeavePostMissionMenu(obj, 0, 0);
            break;
        case 4: LeavePostMissionMenuToSubPage(obj, GRef<s32>(obj + 0x66C) != 0 ? 0x53 : 0x52); break;
        case 5: LeavePostMissionMenuToSubPage(obj, GRef<s32>(obj + 0x66C) != 0 ? 0x4C : 0x4B); break;
        default: break;
    }
    PlayMenuSe(0, 2);
}

// EE: 0x00236090
// State 0 waits for the menu fade (K 0x00410294); state 1 runs the menu tree (tab request 0x13, 0x33
// with a column open): Triangle closes the open column, X/Circle select; states 2/3 wait for the
// page or menu fade, then MenuGoTo the stored target.
void PostMissionMenuUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    const u32 pressed = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    UpdatePostMissionHelp(obj);
    const s32 state = State(obj);
    if (state == 1) {
        GRef<u16>(fade + 0x32) = GRef<s32>(obj + 0x240) != 0 ? 0x33 : 0x13;
        const u32 tree = obj + 0x240;
        UpdateMenuTree(tree, repeat);
        if ((pressed & 0x20) != 0) {
            PostMissionMenuBack(obj, tree);
            return;
        }
        if ((pressed & 0x50) != 0) PostMissionMenuSelect(obj, tree);
    } else if (state == 0) {
        if (K(0x00410294) < GRef<f32>(fade + 0xC)) State(obj) = 1;
    } else if (state == 2) {
        if ((GRef<u32>(obj + 0x230) & 8) != 0)
            MenuGoTo(static_cast<s64>(GRef<s32>(obj + 0x674)), static_cast<u16>(GRef<u32>(obj + 0x678)));
    } else if (state == 3) {
        if ((GRef<u32>(fade) & 8) != 0)
            MenuGoTo(static_cast<s64>(GRef<s32>(obj + 0x674)), static_cast<u16>(GRef<u32>(obj + 0x678)));
    }
}

// EE: 0x00236210
// Draws the page: menu tree, help line, black fade, menu fade.
void DrawPostMissionMenuPage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(g_pMainContext().addr + offsetof(MainContext, vif1_list), obj);
    DrawMenuTree(obj, 0, obj + 0x240);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x648);
    DrawMessageBox(obj, 0, obj + 0x648);
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x002362A0
// Subs 0x25/0x26: menu draw environment, the page created, DATA 0xB2 queued; sub 0x27.
void EnterPostMissionMenuPage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreatePostMissionMenu();
    g_PostMissionMenu() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB2, 1, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x27);
}

// EE: 0x00236340
// Sub 0x27: when loaded, the font block and the help line; sub 0x28. The menu fade frame meanwhile.
void LoadPostMissionMenu() {
    const u32 obj = g_PostMissionMenu();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x648);
        RequestSubState(0x28);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x00236390
// Sub 0x28: fade, update, draw.
void PostMissionMenuRun() {
    const u32 obj = g_PostMissionMenu();
    UpdateFade(obj + 0x230);
    PostMissionMenuUpdate(obj);
    DrawPostMissionMenuPage(obj);
}

AC04_GUEST_FUNCTION(0x002362A0, EnterPostMissionMenuPage);
AC04_GUEST_FUNCTION(0x00236340, LoadPostMissionMenu);
AC04_GUEST_FUNCTION(0x00236390, PostMissionMenuRun);

}  // namespace ac04::game
