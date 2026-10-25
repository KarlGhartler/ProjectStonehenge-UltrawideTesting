// Campaign menu (scene 4 subs 23-26, page 5; docs/subsystems/frontend.md): the menu between missions
// (briefing, hangar, save/load, options, records, extras ...) as a 6-column menu tree with a help
// line.
//
// Page object (0x680 bytes, *0x00411818): +0 bucket list, +0x10 data load queue, +0x220 DATA 0xB1
// (font block), +0x230 fade (+0x23C level), +0x240 menu tree (+0x240 open column), +0x648 message
// box (help line; +0x650 type-out position, +0x656 shown message), +0x66C state (0 fade in, 1 menu,
// 2 leaving to a sub-page, 3 leaving to another page), +0x670/+0x674 MenuGoTo target.
//
// Port baseline (User request 2026-10-24): QUIT GAME is appended to the first column, after LOAD
// (quit_item.h); it leaves through port state 9. The game's tables and ids are unchanged; the
// "port:" lines are the only additions.
#include "game/game.h"

#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "game/quit_item.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_CampaignMenu, 0x00411818)  // gp-0x41D8
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kColumns = 0x0039D920;      // 6 columns
constexpr u32 kHelpMessages = 0x0039DA28;  // s16 per menu item (19)

s32& State(u32 obj) { return GRef<s32>(obj + 0x66C); }

}  // namespace

// EE: 0x002307C0
// Help line: in the menu, the selected item's help message (table 0x0039DA28) is shown with the
// type-out restarted; typed out while the page is up.
void UpdateCampaignMenuHelp(u32 obj) {
    s32 state = State(obj);
    if (state == 1 && QuitItemSelection(obj + 0x240, kCampaignMenuItems) == kQuitItem) {  // port: no help text
        if (GRef<s16>(obj + 0x654) != 0) SetMessageBoxText(obj + 0x648, -1);
        GRef<s16>(obj + 0x656) = -1;  // the next item types its text again
    } else if (state == 1) {
        // port: QUIT GAME in the list
        u64 sel = static_cast<u64>(static_cast<s64>(QuitItemSelection(obj + 0x240, kCampaignMenuItems)));
        if (sel > 0x12) sel = 0;
        const s16 msg = GRef<s16>(kHelpMessages + static_cast<u32>(sel) * 2);
        if (GRef<s16>(obj + 0x656) != msg) {
            SetMessageBoxText(obj + 0x648, msg);
            GRef<f32>(obj + 0x650) = K(0x004101D4);
        }
        state = State(obj);
    }
    if (state != 0) UpdateMessageBox(K(0x004101D8), obj + 0x648, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00230858
// Creates the page object (work heap, 0x680 bytes): menu fade requests, fade, the campaign BGM for
// the mission, and the menu tree (columns 0x0039D920, connections 0x0039D9B0, parents 0x0039DA18,
// no hidden items). Coming back from the hangar path (sub 0x18) it reopens column 3 and fades in
// over k.
u32 CreateCampaignMenu() {
    const u32 obj = WorkHeapAlloc(0x680, 0x10);
    const u32 fade = g_pMenuFade();
    State(obj) = 0;
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    InitFade(obj + 0x230, 1);
    PlayMenuBgm(static_cast<s16>(GetProgressBgmTrack(1, g_MissionNo())));
    guest::GuestFrame frame(0x20);
    const u32 masks = frame.Address();
    for (u32 i = 0; i < 0x18; i += 4) GRef<u32>(masks + i) = 0;
    {
        const QuitItemPatch patch(kColumns, kCampaignMenuItems);  // port: QUIT GAME in the list
        InitMenuTree(obj + 0x240, 6, kColumns, 0x0039D9B0, masks, 0x0039DA18);
    }
    if (Ctx()->sub == 0x18) {
        GRef<u32>(obj + 0x240) = 3;
        SetListWidgetState(obj + 0x290, 1);
        StartFade(K(0x004101DC), obj + 0x230, 0);
    }
    return obj;
}

// EE: 0x00230940
// Leaves to menu page `page` (MenuGoTo kind 0): the menu fade and the page fade out over 0.5 s; the
// menu tree cursors are kept.
void LeaveCampaignMenu(u32 obj, u16 page) {
    const u32 fade = g_pMenuFade();
    State(obj) = 3;
    StartFade(0.5f, fade, 1);
    StartFade(0.5f, obj + 0x230, 1);
    GRef<u32>(obj + 0x674) = page;
    GRef<u32>(obj + 0x670) = 0;
    const QuitItemPatch patch(kColumns, kCampaignMenuItems);  // port: QUIT GAME in the list
    StoreMenuTreeCursors(kColumns, 6, obj + 0x240);
}

// EE: 0x002309C0
// Leaves to sub-page `sub` (MenuGoTo kind 1): the page fades out over 0.5 s; cursors kept.
void LeaveCampaignMenuToSubPage(u32 obj, u16 sub) {
    State(obj) = 2;
    StartFade(0.5f, obj + 0x230, 1);
    GRef<u32>(obj + 0x674) = sub;
    GRef<u32>(obj + 0x670) = 1;
    const QuitItemPatch patch(kColumns, kCampaignMenuItems);  // port: QUIT GAME in the list
    StoreMenuTreeCursors(kColumns, 6, obj + 0x240);
}

// EE: 0x00230A78
// X on a menu item (selection 0-18, jump table 0x00407120): pages 6 (hangar), 3 (briefing, with
// game[0x27] = the mission id), 0xD-0x10 and 0 (clears 0x0039D926); sub-pages 0x2E, 0x2A, 0x3B, 0x51,
// 0x57, and 0x4A/0x50 (the save/load pages; their HDD variants open columns 4/5 only with ctx flag
// 0x80000); items 2-4 open columns 1-3. SE 2, or SE 5 for an unknown selection.
void CampaignMenuSelect(u32 obj, u32 tree) {
    const s16 selection = QuitItemSelection(tree, kCampaignMenuItems);  // port: QUIT GAME in the list
    if (selection == kQuitItem) {
        // port: QUIT GAME leaves like the game's exits (menu and screen fade out, BGM fade) through
        // state 9; the host ends the session when the screen is black.
        State(obj) = 9;
        FadeOutMenuBgm(0.5f);
        StartFade(0.5f, g_pMenuFade(), 1);
        StartFade(0.5f, obj + 0x230, 1);
        PlayMenuSe(0, 2);
        return;
    }
    const u64 sel = static_cast<u64>(static_cast<s64>(selection));
    u32 column = 0;
    u32 widget = 0;
    switch (sel) {
        case 0: LeaveCampaignMenu(obj, 6); break;
        case 1:
            g_GameState()[0x27] = static_cast<u8>(GetMissionId(0, g_MissionNo()));
            LeaveCampaignMenu(obj, 3);
            break;
        case 2: column = 1; widget = tree + 0x20; break;
        case 3: column = 2; widget = tree + 0x38; break;
        case 4: column = 3; widget = tree + 0x50; break;
        case 5:
            if ((Ctx()->flags & 0x80000) == 0) {
                LeaveCampaignMenuToSubPage(obj, 0x4A);
                break;
            }
            column = 4;
            widget = tree + 0x68;
            break;
        case 6:
            if ((Ctx()->flags & 0x80000) == 0) {
                LeaveCampaignMenuToSubPage(obj, 0x50);
                break;
            }
            column = 5;
            widget = tree + 0x80;
            break;
        case 7: LeaveCampaignMenu(obj, 0xD); break;
        case 8: LeaveCampaignMenu(obj, 0xE); break;
        case 9: LeaveCampaignMenu(obj, 0xF); break;
        case 10: LeaveCampaignMenu(obj, 0x10); break;
        case 11: LeaveCampaignMenuToSubPage(obj, 0x2E); break;
        case 12: LeaveCampaignMenuToSubPage(obj, 0x2A); break;
        case 13: LeaveCampaignMenuToSubPage(obj, 0x3B); break;
        case 14:
            LeaveCampaignMenu(obj, 0);
            GRef<u16>(0x0039D926) = 0;
            break;
        case 15: LeaveCampaignMenuToSubPage(obj, 0x51); break;
        case 16: LeaveCampaignMenuToSubPage(obj, 0x4A); break;
        case 17: LeaveCampaignMenuToSubPage(obj, 0x57); break;
        case 18: LeaveCampaignMenuToSubPage(obj, 0x50); break;
        default:
            PlayMenuSe(0, 5);
            return;
    }
    if (widget != 0) {
        GRef<u32>(tree) = column;
        SetListWidgetState(widget, 1);
    }
    PlayMenuSe(0, 2);
}

// EE: 0x00230CA0
// State 0 waits for the menu fade; state 1 runs the menu tree (tab request 0x13, 0x33 with a column
// open): Triangle closes the open column (SE 3), X/Circle select; states 2/3 wait for the page or
// menu fade, then MenuGoTo the stored target.
void CampaignMenuUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    const u32 pressed = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    UpdateCampaignMenuHelp(obj);
    const s32 state = State(obj);
    if (state == 1) {
        GRef<u16>(fade + 0x32) = GRef<s32>(obj + 0x240) != 0 ? 0x33 : 0x13;
        const u32 tree = obj + 0x240;
        UpdateMenuTree(tree, repeat);
        if ((pressed & 0x20) != 0) {
            const s32 open = GRef<s32>(tree);
            if (open == 0) return;
            GRef<s32>(tree) = 0;
            SetListWidgetState(tree + static_cast<u32>(open) * 0x18 + 8, 3);
            PlayMenuSe(0, 3);
            return;
        }
        if ((pressed & 0x50) != 0) CampaignMenuSelect(obj, tree);
    } else if (state == 0) {
        if (K(0x004101E0) < GRef<f32>(fade + 0xC)) State(obj) = 1;
    } else if (state == 2) {
        if ((GRef<u32>(obj + 0x230) & 8) != 0)
            MenuGoTo(static_cast<s64>(GRef<s32>(obj + 0x670)), static_cast<u16>(GRef<u32>(obj + 0x674)));
    } else if (state == 3) {
        if ((GRef<u32>(fade) & 8) != 0)
            MenuGoTo(static_cast<s64>(GRef<s32>(obj + 0x670)), static_cast<u16>(GRef<u32>(obj + 0x674)));
    } else if (state == 9) {  // port: QUIT GAME, the screen is black
        if ((GRef<u32>(fade) & 8) != 0) port::Menu().quit = true;
    }
}

// EE: 0x00230E20
// Draws the page: menu tree, help line, black fade, menu fade.
void DrawCampaignMenuPage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(g_pMainContext().addr + offsetof(MainContext, vif1_list), obj);
    DrawMenuTree(obj, 0, obj + 0x240);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x648);
    DrawMessageBox(obj, 0, obj + 0x648);
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x00230EB0
// Subs 23/24: g_MissionNo clamped to 1-18, menu draw environment, the page created, DATA 0xB1
// queued; sub 25.
void EnterCampaignMenu() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    if (g_MissionNo() == 0) {
        g_MissionNo() = 1;
    } else if (g_MissionNo() > 0x12) {
        g_MissionNo() = 0x12;
    }
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateCampaignMenu();
    g_CampaignMenu() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB1, 1, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x19);
}

// EE: 0x00230F78
// Sub 25: when loaded, the font block and the help line; sub 26. The menu fade frame meanwhile.
void LoadCampaignMenu() {
    const u32 obj = g_CampaignMenu();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x648);
        RequestSubState(0x1A);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x00230FC8
// Sub 26: fade, update, draw.
void CampaignMenuRun() {
    debug::OnGuestCheckpoint(0x00230FC8);
    const u32 obj = g_CampaignMenu();
    UpdateFade(obj + 0x230);
    CampaignMenuUpdate(obj);
    DrawCampaignMenuPage(obj);
}

AC04_GUEST_FUNCTION(0x00230EB0, EnterCampaignMenu);
AC04_GUEST_FUNCTION(0x00230F78, LoadCampaignMenu);
AC04_GUEST_FUNCTION(0x00230FC8, CampaignMenuRun);

}  // namespace ac04::game
