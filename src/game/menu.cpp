// Scene 4: front end (docs/subsystems/frontend.md). The scene handler and page navigation, the
// campaign selection helpers, the data load queue, the fade object, the menu BGM, the attract demo
// and the title screen / main menu (page 0, subs 2-6). Pages translated in later slices are
// reached through the sub-state table 0x00384640 and end in a logged stub until then.
//
// Main menu object (0x6A0 bytes, work heap, pointer 0x00411810): +0 bucket list (2 buckets),
// +0x10 data load queue, +0x220 DATA 0xB0 items {font block, logo packet}, +0x240 fade object,
// +0x250 menu tree (menu_widgets.cpp), +0x658 message box (help line), +0x67C state, +0x680/+0x684
// MenuGoTo target, +0x688 f32 idle time, +0x68C f32 title time, +0x690 f32 logo alpha.
// Fade object: +0 state (1 fade in, 2 fade out, 4 in done, 8 out done), +4 f32 time, +8 f32
// duration, +0xC f32 value (0 black .. 1 clear). The menu fade object (pointer 0x00410E88) also has
// +0x10 overlay on, +0x14 f32 tab levels[7], +0x30 s16 master request, +0x32 u16 tab requests.
// Data load queue: +0 u16 read, +2 u16 write, +6 u16 loading, +8 f32 time, +0x10 entries[32]
// {u16 DATA entry, u16 item count, u32 buffer at +8, u32 item table at +0xC}.
//
// Port baseline (User approval 2026-10-22): QUIT GAME is appended to the main menu's first column
// (quit_item.h); it leaves through port state 9. The game's tables and ids are unchanged; the
// "port:" lines are the only additions.
#include "game/game.h"

#include <cstddef>
#include <cstdio>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "game/quit_item.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u16, g_MenuPage, 0x00410E7C)        // gp-0x4B74
AC04_GUEST_VAR(u16, g_MenuSubPage, 0x00410E7E)     // gp-0x4B72
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)       // gp-0x4B68
AC04_GUEST_VAR(u32, D_00410E98, 0x00410E98)        // gp-0x4B58: attract demo index
AC04_GUEST_VAR(u32, g_TitleMenu, 0x00411810)       // gp-0x41E0
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kMenuFadeList = 0x0042D1C0;  // one-bucket list of MenuEnterSubPage
constexpr u32 kMenuBgm = 0x0042D1D0;       // u32 slot, u32, s16 track[2] (+8), s16 state[2] (+0xC)
constexpr u32 kMainMenuColumns = 0x0039D5B0;
constexpr u32 kProgressFlags = 0x002A0318;

u32 Progress() { return GRef<u32>(kProgressFlags); }
u32& BgmSlot() { return GRef<u32>(kMenuBgm); }
s16& BgmTrack(u32 slot) { return GRef<s16>(kMenuBgm + 8 + 2 * slot); }
s16& BgmState(u32 slot) { return GRef<s16>(kMenuBgm + 0xC + 2 * slot); }

}  // namespace

// ---- Navigation ---------------------------------------------------------------------------------

// EE: 0x00226198
// HYPOTHESIS: EnterMenu(page): stores the menu page and requests scene 4.
void EnterMenu(u16 page) {
    g_MenuPage() = page;
    RequestScene(4);
}

// EE: 0x002261A8
void SetMenuSubPage(u16 sub) {
    g_MenuSubPage() = sub;
    RequestSubState(1);
}

// EE: 0x00226450
// kind 0: EnterMenu(arg); 1: SetMenuSubPage(arg); 2/3: the flight scene with flight-entry kind 0/1.
void MenuGoTo(s64 kind, u16 arg) {
    debug::OnGuestCheckpoint(0x00226450);
    if (kind == 1) {
        SetMenuSubPage(arg);
    } else if (kind == 0) {
        EnterMenu(arg);
    } else if (kind == 2 || kind == 3) {
        g_GameState()[0x2A] = kind == 2 ? 0 : 1;
        FadeOutMenuBgm(2.0f);
        RequestScene(2);
    }
}

// EE: 0x00226DE0
// 640x448-type menu display (draw width `w`, height `h`, display height 224), clear colour black
// with alpha 0x80, PSMZ16S depth, depth test on, then the draw environment with TEST 0x1310000DC.
void SetMenuDisplayMode(MainContext* ctx, u16 w, u16 h) {
    ctx->frame_psm = 0;
    ctx->draw_h = h;
    ctx->disp_w = w;
    ctx->disp_h = 0xE0;
    GRef<u32>(kMainContextAddress + offsetof(MainContext, clear_rgba)) = 0x80000000;
    ctx->zbuf_psm = 0x31;
    ctx->ztest = 1;
    ctx->draw_mode = 2;
    ctx->draw_w = w;
    BuildDrawEnvPackets();
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    ctx->drawenv_pkt[20] = 0x1310000DCull;
}

// EE: 0x002261B8
// Sub 0: packet buffers, heap mark, display mode, fade reset, then the page's first sub-state
// (table 0x003845F8); the menu BGM state is reset when coming from another scene.
void MenuEnterPage() {
    const u32 dbuf = kMainContextAddress + offsetof(MainContext, dbuf);
    AllocDoubleBuffer(dbuf, 0x40000);
    Ctx()->cur_buf.addr = SwapDoubleBuffer(dbuf);
    PushWorkHeapMark();
    SetMenuDisplayMode(Ctx(), 0x280, 0x1C0);
    ResetMenuFade(g_pMenuFade());
    u32 page = g_MenuPage();
    if (page > 0x22) page = 0;
    RequestSubState(GRef<u16>(0x003845F8 + 2 * page));
    if (Ctx()->prev_scene != 4) ResetMenuBgm();
}

// EE: 0x00226248
// Sub 1: draws the fade frame and enters the sub-page (2 when out of range).
void MenuEnterSubPage() {
    DrawMenuFadeFrame(kMenuFadeList, g_pMenuFade());
    u16 sub = g_MenuSubPage();
    if (sub > 0x59) sub = 2;
    RequestSubState(sub);
}

// EE: 0x002262D0
// HYPOTHESIS: SceneMenu (scene 4): sub-state handler (table 0x00384640), the menu overlay and the
// menu BGM.
void SceneMenu() {
    guest::CallGuest(GRef<u32>(0x00384640 + 4u * Ctx()->sub));
    const u32 fade = g_pMenuFade();
    if (GRef<u32>(fade + 0x10) != 0) UpdateMenuOverlay(fade);
    UpdateMenuBgm();
}

// ---- Campaign selection -------------------------------------------------------------------------

// EE: 0x00226378
// Mission id for mode `kind` (table at .rodata 0x00406E08: {count, first} per kind) and 1-based
// number `n` (out of range: the first); kinds above 3 give 1.
s16 GetMissionId(u32 kind, s32 n) {
    if (kind >= 4) return 1;
    const u32 entry = 0x00406E08 + kind * 4;
    const s32 count = GRef<s16>(entry);
    const s32 first = GRef<s16>(entry + 2);
    s32 i = n - 1;
    if (!(static_cast<u64>(static_cast<s64>(i)) < static_cast<u64>(static_cast<s64>(count)))) i = 0;
    return GRef<s16>(0x00384800 + 2u * static_cast<u32>(i + first));
}

// EE: 0x0022CBB8
// Menu BGM track for page kind `kind` (0, 1, 2) and campaign mission `mission` (passed to
// PlayMenuBgm): kind 0 gives 0x41 for mission 18, else 1 before mission 13 and 2 from it; kind 1
// gives 3 / 5 / 6; kind 2 gives 8 for mission 18, else 7. Other kinds give 0.
s32 GetProgressBgmTrack(s32 kind, s32 mission) {
    s32 early;
    s32 late;
    bool before13;
    if (kind == 1) {
        if (mission == 0x12) return 3;
        before13 = mission < 0xD;
        early = 5;
        late = 6;
    } else if (kind < 2) {
        if (kind != 0) return 0;
        if (mission == 0x12) return 0x41;
        before13 = mission < 0xD;
        early = 1;
        late = 2;
    } else {
        if (kind != 2) return 0;
        before13 = (mission ^ 0x12) != 0;
        early = 7;
        late = 8;
    }
    return before13 ? early : late;
}

// EE: 0x00226A00
// Keeps the campaign selection valid: aircraft 1-21 (else 2), colour 0-2, special weapon 0-3, and
// falls back to aircraft 2 colour 0 / weapon 0 when the ownership bits (0x002A0320) are missing.
void ValidateCampaignSelection() {
    u16& aircraft = GRef<u16>(0x002A15BE);
    u16& color = GRef<u16>(0x002A15C0);
    u16& weapon = GRef<u16>(0x002A15C2);
    u32 c = color;
    u32 a = aircraft;
    u32 w = weapon;
    if (c >= 3) c = 0;
    if (a - 1 >= 0x15) a = 2;
    const u32 shift = ((a - 1) * 2) & 0x3F;
    if (w >= 4) w = 0;
    if (((GRef<u64>(0x002A0320 + c * 8) >> shift) & 3) != 3) {
        a = 2;
        c = 0;
    }
    aircraft = static_cast<u16>(a);
    color = static_cast<u16>(c);
    if (((GRef<u64>(0x002A0320 + (w + 3) * 8) >> shift) & 3) != 3) w = 0;
    weapon = static_cast<u16>(w);
}

// EE: 0x00226A98
// Copies the campaign selection into the game state for a campaign flight (mode 1).
void ApplyCampaignSelection() {
    ValidateCampaignSelection();
    g_GameState()[0x25] = 1;
    const u8 aircraft = GRef<u8>(0x002A15BE);
    g_GameState()[0] = aircraft;
    g_GameState()[0x10] = aircraft;
    const u8 color = GRef<u8>(0x002A15C0);
    g_GameState()[1] = color;
    g_GameState()[0x11] = color;
    const u8 weapon = GRef<u8>(0x002A15C2);
    g_GameState()[3] = weapon;
    g_GameState()[0x13] = weapon;
    g_GameState()[0x28] = GRef<u8>(0x002A15BA);
    g_GameState()[0x29] = 1;
    g_GameState()[0x27] = static_cast<u8>(GetMissionId(0, g_MissionNo()));
}

// ---- Data load queue ----------------------------------------------------------------------------

// EE: 0x0022C5E8
void ResetMenuDataLoad(u32 q) {
    for (s32 i = 0x20; i >= 0; --i) {
        const u32 e = q + static_cast<u32>(i) * 0x10;
        GRef<u32>(e) = 0;
        GRef<u32>(e + 4) = 0;
        GRef<u32>(e + 8) = 0;
        GRef<u32>(e + 0xC) = 0;
    }
}

// EE: 0x0022C618
// Queues DATA `entry` (split into `count` items whose addresses go to `table`). -1 when full.
s32 QueueMenuDataLoad(u32 q, u16 entry, s32 count, u32 table) {
    const u16 write = GRef<u16>(q + 2);
    const s32 next = static_cast<s32>(write) + 1;
    const u32 wrapped = static_cast<u32>(next - (next >> 5) * 0x20);
    if (wrapped == GRef<u16>(q)) return -1;
    GRef<u16>(q + 2) = static_cast<u16>(wrapped);
    const u32 e = q + 0x10 + static_cast<u32>(write) * 0x10;
    GRef<u16>(e) = entry;
    GRef<u16>(e + 4) = 0;
    GRef<u16>(e + 6) = 0;
    GRef<u16>(e + 2) = static_cast<u16>(count);
    GRef<u16>(e + 8) = 0;
    GRef<u16>(e + 0xA) = 0;
    GRef<u32>(e + 0xC) = table;
    for (s32 i = 0; i < count; ++i) GRef<u32>(table + 4u * static_cast<u32>(i)) = 0;
    return 0;
}

// EE: 0x0022C6B0
// Runs the queue: starts the next load (work heap, 64-byte aligned), and when it has finished
// splits the container into its items. Returns -1 when idle, 1 while loading, 2 when an entry
// completed, -2 when the item count does not match.
s32 UpdateMenuDataLoad(u32 q) {
    if (GRef<u16>(q) == GRef<u16>(q + 2)) return -1;
    GRef<f32>(q + 8) = fpu::Add(GRef<f32>(q + 8), K(0x0041011C));
    const u32 e = q + 0x10 + static_cast<u32>(GRef<u16>(q)) * 0x10;
    if (GRef<u16>(q + 6) == 0) {
        GRef<u16>(q + 6) = GRef<u16>(e + 2);
        const u32 size = GetDataEntryAllocSize(GRef<u16>(e));
        const u32 buf = WorkHeapAlloc(static_cast<s32>(size), 0x40);
        LoadDataEntryAsync(GRef<u16>(e), buf);
        GRef<u32>(e + 8) = buf;
    }
    if (IsDataLoadDone() == 0) return 1;
    const u32 buf = GRef<u32>(e + 8);
    const u32 n = GRef<u32>(buf);
    if (n != GRef<u16>(e + 2)) return -2;
    const u16 read = GRef<u16>(q);
    for (s32 i = 0; i < static_cast<s32>(n); ++i) {
        GRef<u32>(GRef<u32>(e + 0xC) + 4u * static_cast<u32>(i)) = buf + GRef<u32>(buf + 4 + 4u * static_cast<u32>(i));
    }
    GRef<u16>(q + 6) = 0;
    const s32 next = static_cast<s32>(read) + 1;
    GRef<u16>(q) = static_cast<u16>(next - (next >> 5) * 0x20);
    return 2;
}

// ---- Fade object --------------------------------------------------------------------------------

// EE: 0x0022C7E8
void InitFade(u32 fade, s64 clear) {
    GRef<u32>(fade) = 0;
    GRef<u32>(fade + 4) = 0;
    GRef<u32>(fade + 8) = 0;
    GRef<f32>(fade + 0xC) = clear == 0 ? 0.0f : 1.0f;
}

// EE: 0x0022C810
// Starts a fade over `duration` seconds: in (0 -> 1) or, with `out`, out (1 -> 0).
void StartFade(f32 duration, u32 fade, s64 out) {
    GRef<f32>(fade + 8) = duration;
    GRef<u32>(fade + 4) = 0;
    if (out == 0) {
        GRef<f32>(fade + 0xC) = 0.0f;
        GRef<u32>(fade) = 1;
    } else {
        GRef<u32>(fade) = 2;
        GRef<f32>(fade + 0xC) = 1.0f;
    }
}

// EE: 0x0022C848
// Cosine ease; at the end the state becomes 4 (in done, value 1) or 8 (out done, value 0).
void UpdateFade(u32 fade) {
    const u32 state = GRef<u32>(fade);
    if ((state & 3) == 0) return;
    const u32 out = state & 2;
    f32 time = GRef<f32>(fade + 4);
    const f32 duration = GRef<f32>(fade + 8);
    f32 t = time / duration;
    if (out != 0) t = fpu::Sub(1.0f, t);
    const f32 c = ee::cosf(t * K(0x00410120));
    time = fpu::Add(time, K(0x00410124));
    const f32 value = fpu::Sub(1.0f, c) * 0.5f;
    GRef<f32>(fade + 4) = time;
    GRef<f32>(fade + 0xC) = value;
    if (!(duration <= time)) return;
    if (out == 0) {
        GRef<f32>(fade + 0xC) = 1.0f;
        GRef<u32>(fade) = 4;
    } else {
        GRef<f32>(fade + 0xC) = 0.0f;
        GRef<u32>(fade) = 8;
    }
    GRef<f32>(fade + 4) = 0.0f;
    GRef<f32>(fade + 8) = 0.0f;
}

// ---- Menu BGM -----------------------------------------------------------------------------------

// EE: 0x0022C920
// Clears the menu BGM state and fades all VAG slots out.
void ResetMenuBgm() {
    const f32 time = K(0x00410128);
    BgmSlot() = 0;
    BgmState(0) = 0;
    GRef<u32>(kMenuBgm + 4) = 0;
    GRef<u16>(kMenuBgm + 0xA) = 0;
    GRef<u16>(kMenuBgm + 8) = 0;
    BgmState(1) = 0;
    func_0021FB70(time);
}

// EE: 0x0022C948
// Requests menu track `track`: nothing when it is already requested or playing on the current
// slot; otherwise the current slot (if busy) fades out over 3 s and the other slot takes over.
void PlayMenuBgm(s16 track) {
    u32 slot = BgmSlot();
    if (BgmTrack(slot) == track && static_cast<u64>(static_cast<s64>(BgmState(slot)) - 1) < 2) return;
    if (BgmState(slot) != 0) {
        func_0021F190(3.0f, static_cast<s32>(slot), 0, 0, 2);
        slot = (slot + 1) & 1;
        BgmSlot() = slot;
    }
    func_0021FDF8(static_cast<u8>(1u << slot));
    BgmTrack(slot) = track;
    BgmState(slot) = 1;
}

// EE: 0x0022CA18
// Fades both menu BGM slots out over `time` seconds.
void FadeOutMenuBgm(f32 time) {
    BgmSlot() = 0;
    BgmState(0) = 0;
    BgmState(1) = 0;
    func_0021F190(time, 0, 0, 0, 2);
    func_0021F190(time, 1, 0, 0, 2);
}

// EE: 0x0022CAF0
// Starts the requested track once its VAG slot is idle (slot state 1 at 0x00353BF8 + slot * 0x94
// + 0x34); until then keeps requesting the stop.
void UpdateMenuBgm() {
    const u32 slot = BgmSlot();
    if (BgmState(slot) != 1) return;
    if (GRef<s32>(0x00353BF8 + slot * 0x94 + 0x34) != 1) {
        func_0021FDF8(static_cast<u8>(1u << slot));
        return;
    }
    func_0021F190(0.0f, static_cast<s32>(slot), 0xFF, 0xFF, 1);
    func_0021FD40(slot, static_cast<u16>(BgmTrack(slot)));
    BgmState(slot) = 2;
}

// ---- Attract demo -------------------------------------------------------------------------------

// EE: 0x0022CC68
// Starts the next attract demo flight (6 entries, D_00410E98): entries 0/2/4 fly mission 0x23 with
// the campaign aircraft (0x11 before the first clear) in mode 1; 1/3/5 are the three demo
// recordings (mode 4, game[0x2B] = 0/1/2).
void StartTitleDemo() {
    const u32 index = D_00410E98();
    guest::ps2ptr<u8> game = g_GameState();
    switch (index) {
        case 1:
            game[0x25] = 1;
            g_GameState()[0x27] = 3;
            g_GameState()[0] = 0x10;
            g_GameState()[1] = 0;
            g_GameState()[3] = 1;
            g_GameState()[0x29] = 4;
            g_GameState()[0x2B] = 0;
            break;
        case 3:
            game[0x25] = 1;
            g_GameState()[0x27] = 0x10;
            g_GameState()[0] = 4;
            g_GameState()[1] = 0;
            g_GameState()[3] = 0;
            g_GameState()[0x29] = 4;
            g_GameState()[0x2B] = 1;
            break;
        case 5:
            game[0x25] = 1;
            g_GameState()[0x27] = 5;
            g_GameState()[0] = 3;
            g_GameState()[1] = 0;
            g_GameState()[3] = 1;
            g_GameState()[0x29] = 4;
            g_GameState()[0x2B] = 2;
            break;
        default:
            game[0x25] = 1;
            g_GameState()[0x27] = 0x23;
            if ((Progress() & 1) == 0) {
                g_GameState()[0] = 0x11;
                g_GameState()[1] = 0;
                g_GameState()[3] = 0;
            } else {
                g_GameState()[0] = GRef<u8>(0x002A15BE);
                g_GameState()[1] = GRef<u8>(0x002A15C0);
                g_GameState()[3] = GRef<u8>(0x002A15C2);
            }
            g_GameState()[0x29] = 1;
            break;
    }
    FadeOutMenuBgm(2.0f);
    g_GameState()[0x2A] = 0;
    RequestScene(2);
    const u32 next = index + 1;
    D_00410E98() = next < 6 ? next : 0;
}

// EE: 0x0022CC50
// HYPOTHESIS: 1 after the no-save intro request (argument 0), 0 on the way to the title (1).
void func_0022CC50(s64 mode) { D_00410E98() = mode == 0 ? 1u : 0u; }

// ---- Title screen and main menu -----------------------------------------------------------------

// EE: 0x0022CE70
// Help line under the menu: in the menu tree (state 6) shows the message of the selected item
// (table 0x0039D6B8; item messages 0 become 0x26 once the campaign is under way); types it out in
// states 1 and 4-8.
void UpdateMainMenuHelp(u32 obj) {
    u32 state = GRef<u32>(obj + 0x67C);
    if (state == 6 && MainMenuSelection(obj + 0x250) == kQuitItem) {  // port: QUIT GAME has no help text
        if (GRef<s16>(obj + 0x664) != 0) SetMessageBoxText(obj + 0x658, -1);
        GRef<s16>(obj + 0x666) = -1;  // the next item types its text again
    } else if (state == 6) {
        u32 id = static_cast<u32>(static_cast<s32>(MainMenuSelection(obj + 0x250)));  // port: QUIT GAME in the list
        if (id >= 0x24) id = 0;
        s16 msg = GRef<s16>(0x0039D6B8 + 2 * id);
        if (msg == 0 && !(g_MissionNo() < 2 && (Progress() & 1) == 0)) msg = 0x26;
        if (GRef<s16>(obj + 0x666) != msg) {
            SetMessageBoxText(obj + 0x658, msg);
            GRef<f32>(obj + 0x660) = K(0x0041012C);
        }
        state = GRef<u32>(obj + 0x67C);
    }
    if (state != 1 && state < 4) return;
    UpdateMessageBox(K(0x00410130), obj + 0x658, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x0022CF48
// Leaves the menu with the screen fading out (state 8): MenuGoTo(0, page).
void MainMenuExitWithFade(u32 obj, u32 page) {
    const u32 fade = g_pMenuFade();
    GRef<u32>(obj + 0x67C) = 8;
    StartFade(0.5f, fade, 1);
    StartFade(0.5f, obj + 0x240, 1);
    GRef<u32>(obj + 0x684) = page;
    GRef<u32>(obj + 0x680) = 0;
    const QuitItemPatch patch(kMainMenuColumns);  // port: QUIT GAME in the list
    StoreMenuTreeCursors(kMainMenuColumns, 6, obj + 0x250);
}

// EE: 0x0022CFC8
// Leaves the menu to a sub-page (state 7): MenuGoTo(1, sub).
void MainMenuExit(u32 obj, u32 sub) {
    GRef<u32>(obj + 0x67C) = 7;
    StartFade(0.5f, obj + 0x240, 1);
    GRef<u32>(obj + 0x684) = sub;
    GRef<u32>(obj + 0x680) = 1;
    const QuitItemPatch patch(kMainMenuColumns);  // port: QUIT GAME in the list
    StoreMenuTreeCursors(kMainMenuColumns, 6, obj + 0x250);
}

// EE: 0x0022D030
// Dead (no reference): one player; players 1 and 2 aircraft id 2, colour variant 0; game state +3
// and +0x13 cleared.
void func_0022D030() {
    const u32 game = g_GameState().addr;
    GRef<u8>(game + 0x25) = 1;
    GRef<u8>(g_GameState().addr + 0) = 2;
    GRef<u8>(g_GameState().addr + 1) = 0;
    GRef<u8>(g_GameState().addr + 3) = 0;
    GRef<u8>(g_GameState().addr + 0x10) = 2;
    GRef<u8>(g_GameState().addr + 0x11) = 0;
    GRef<u8>(g_GameState().addr + 0x13) = 0;
}

// EE: 0x0022D078
// Back (pad 0x20): from the first column back to the title (state 1); from a sub-column the tree
// returns to column 0 and that sub-column closes. SE 3.
void MainMenuBack(u32 obj, u32 tree) {
    const s32 col = GRef<s32>(tree);
    if (col == 0) {
        const QuitItemPatch patch(kMainMenuColumns);  // port: QUIT GAME in the list
        StoreMenuTreeCursors(kMainMenuColumns, 6, obj + 0x250);
        GRef<u32>(obj + 0x67C) = 1;
        StartFade(0.5f, obj + 0x240, 1);
    } else {
        GRef<s32>(tree) = 0;
        SetListWidgetState(tree + 8 + static_cast<u32>(col) * 0x18, 3);
    }
    PlayMenuSe(0, 3);
}

// EE: 0x0022D100
// Confirm in the menu tree (frontend.md "MainMenuSelect item ids"). SE 2.
void MainMenuSelect(u32 obj, u32 tree) {
    const s16 selection = MainMenuSelection(tree);  // port: QUIT GAME in the list
    if (selection == kQuitItem) {
        // port: QUIT GAME leaves like the game's exits (menu and screen fade out, BGM fade) through
        // state 9; the host ends the session when the screen is black.
        GRef<u32>(obj + 0x67C) = 9;
        FadeOutMenuBgm(0.5f);
        StartFade(0.5f, g_pMenuFade(), 1);
        StartFade(0.5f, obj + 0x240, 1);
        PlayMenuSe(0, 2);
        return;
    }
    const u32 id = static_cast<u32>(static_cast<s32>(selection));
    auto open_column = [tree](s32 col) {
        GRef<s32>(tree) = col;
        SetListWidgetState(tree + 8 + static_cast<u32>(col) * 0x18, 1);
    };
    auto free_mission = [obj](u32 sub) {
        MainMenuExit(obj, sub);
        ApplyCampaignSelection();
        g_GameState()[0x29] = 2;
        g_GameState()[0x28] = 2;
    };
    auto new_game = [obj](u32 difficulty, bool reset) {
        FadeOutMenuBgm(1.0f);
        MainMenuExitWithFade(obj, 0x20);
        Ctx()->ret_sub = 0;
        if (reset) ResetCampaignData();
        GRef<u16>(0x002A15BA) = static_cast<u16>(difficulty);
        g_MissionNo() = 1;
        ApplyCampaignSelection();
    };
    if (id < 0x24) {
        switch (id) {
            case 0: open_column(1); break;
            case 1: open_column(2); break;
            case 2:
                MainMenuExitWithFade(obj, 2);
                ApplyCampaignSelection();
                break;
            case 3:
                if ((Ctx()->flags & 0x80000) != 0) {
                    open_column(3);
                } else {
                    MainMenuExit(obj, 0x4F);
                }
                break;
            case 4: open_column(4); break;
            case 5: free_mission(0x42); break;
            case 6: free_mission(0x43); break;
            case 7:
                MainMenuExit(obj, 0x44);
                GRef<u16>(0x002A15C2) = 0;
                GRef<u16>(0x002A15BE) = 2;
                GRef<u16>(0x002A15C0) = 0;
                ApplyCampaignSelection();
                g_GameState()[0x29] = 2;
                g_GameState()[0x28] = 2;
                break;
            case 8: open_column(5); break;
            case 9: case 10: case 11: case 12: case 13: case 14:
                new_game(id - 9, true);
                break;
            case 15: case 16: case 17: case 18: case 19: case 20:
                new_game(id - 0xF, false);
                break;
            case 0x15: MainMenuExit(obj, 0x56); break;
            case 0x16: MainMenuExit(obj, 0x4F); break;
            case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C:
                MainMenuExit(obj, 0x41);
                ApplyCampaignSelection();
                g_GameState()[0x28] = static_cast<u8>(id - 0x17);
                g_GameState()[0x29] = 2;
                break;
            case 0x1D: MainMenuExit(obj, 0x2D); break;
            case 0x1E: MainMenuExit(obj, 0x29); break;
            case 0x1F: MainMenuExit(obj, 0x3A); break;
            case 0x20: MainMenuExit(obj, 0x37); break;
            case 0x21: MainMenuExit(obj, 0x34); break;
            case 0x22: MainMenuExit(obj, 0x31); break;
            case 0x23: MainMenuExit(obj, 0x3E); break;
            default: break;
        }
    }
    PlayMenuSe(0, 2);
}

// EE: 0x0022D3C0
// Mode 2: title (state 0, title BGM 3). Otherwise the menu (state 5, menu BGM 4): tree from the
// column/connector definitions 0x0039D5B0/0x0039D640 with item masks from the progress flags; mode
// 4 reopens column 5 (the save/load column) with a quick fade-in.
void MainMenuSetup(u32 obj, s32 mode) {
    const u32 fade = g_pMenuFade();
    if (mode == 2) {
        GRef<u32>(obj + 0x67C) = 0;
        GRef<u16>(fade + 0x32) = 0;
        GRef<u16>(fade + 0x30) = 0;
        InitFade(obj + 0x240, 1);
        GRef<u32>(obj + 0x688) = 0;
        PlayMenuBgm(3);
        return;
    }
    guest::GuestFrame frame(0x20);
    const u32 masks = frame.Address();
    ee::memset(masks, 0, 0x18);
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    GRef<u32>(obj + 0x67C) = 5;
    InitFade(obj + 0x240, 1);
    const bool cleared = (Progress() & 1) != 0;
    u32 m0 = cleared ? 2u : 0x32u;
    if (g_MissionNo() == 0) {
        m0 |= 4;
        if (cleared) m0 &= 0x34;
    }
    GRef<u32>(masks) = m0;
    u32 m1;
    if ((Progress() & 4) != 0) {
        m1 = 0;
    } else {
        m1 = (Progress() & 2) != 0 ? 0x20u : 0x30u;
    }
    GRef<u32>(masks + 0x10) = m1;
    GRef<u32>(masks + 8) = m1;
    GRef<u32>(masks + 4) = m1;
    u32 m5 = (Ctx()->flags & 0x80000) == 0 ? 0x40u : 0u;
    if ((Progress() & 1) == 0) m5 |= 0x30;
    GRef<u32>(masks + 0x14) = m5;
    {
        const QuitItemPatch patch(kMainMenuColumns);  // port: QUIT GAME in the list
        InitMenuTree(obj + 0x250, 6, kMainMenuColumns, 0x0039D640, masks, 0x0039D6A8);
    }
    if (mode == 4) {
        GRef<u32>(obj + 0x250) = 5;
        SetListWidgetState(obj + 0x2D0, 1);
        StartFade(K(0x00410134), obj + 0x240, 0);
    }
    InitMessageBox(obj + 0x658);
    PlayMenuBgm(4);
}

// EE: 0x0022D560
u32 CreateMainMenu() {
    const u32 obj = WorkHeapAlloc(0x6A0, 0x10);
    GRef<u32>(obj + 0x690) = 0;
    GRef<u32>(obj + 0x68C) = 0;
    MainMenuSetup(obj, Ctx()->sub);
    return obj;
}

// EE: 0x0022D5A8
// Title time (+0x68C), logo alpha (+0x690: up to 1 on the title, eased out in the menu) and the
// idle time (+0x688, title only); the menu tab master level is on while the menu is up.
void UpdateTitleTimers(u32 obj) {
    GRef<f32>(obj + 0x68C) = fpu::Add(GRef<f32>(obj + 0x68C), K(0x00410138));
    u32 state = GRef<u32>(obj + 0x67C);
    f32 alpha = GRef<f32>(obj + 0x690);
    if (state < 4) {
        alpha = fpu::Add(alpha, K(0x0041013C));
        if (1.0f < alpha) alpha = 1.0f;
        GRef<f32>(obj + 0x690) = alpha;
    } else if (!(0.0f < alpha)) {
        GRef<f32>(obj + 0x690) = alpha;
    } else {
        if (K(0x00410140) < alpha) {
            alpha = ApproachValue(alpha, K(0x00410140), K(0x00410144));
        } else {
            alpha = ApproachValue(alpha, 0.0f, K(0x00410148));
        }
        state = GRef<u32>(obj + 0x67C);
        GRef<f32>(obj + 0x690) = alpha;
    }
    const u32 fade = g_pMenuFade();
    if (state < 5) {
        GRef<f32>(obj + 0x688) = fpu::Add(GRef<f32>(obj + 0x688), K(0x0041014C));
        GRef<u16>(fade + 0x30) = 0;
    } else {
        GRef<u16>(fade + 0x30) = 1;
    }
}

// EE: 0x0022D698
// Main menu state machine (frontend.md): 0 wait for the fade-in, 1 back to the title, 2 title idle
// (Start/X after 2 s opens the menu; 19 s idle starts the attract demo), 3 demo fade, 4/5 menu
// opening, 6 menu tree, 7/8 leaving through MenuGoTo.
void MainMenuUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    const u32 edge = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    UpdateTitleTimers(obj);
    UpdateMainMenuHelp(obj);
    switch (GRef<u32>(obj + 0x67C)) {
        case 0:
            if (K(0x00410150) < GRef<f32>(fade + 0xC)) GRef<u32>(obj + 0x67C) = 2;
            break;
        case 1:
            if ((GRef<u32>(obj + 0x240) & 8) != 0) {
                GRef<u32>(obj + 0x688) = 0;
                GRef<u32>(obj + 0x67C) = 2;
                PlayMenuBgm(3);
            }
            break;
        case 2:
            if ((edge & 0x50) != 0 && 2.0f < GRef<f32>(obj + 0x68C)) {
                debug::OnGuestCheckpoint(0x0022D748);
                MainMenuSetup(obj, 3);
                StartFade(0.5f, obj + 0x240, 0);
                GRef<u32>(obj + 0x67C) = 4;
                PlayMenuSe(0, 2);
            } else if (19.0f < GRef<f32>(obj + 0x688)) {
                GRef<u32>(obj + 0x67C) = 3;
                StartFade(0.5f, fade, 1);
            }
            break;
        case 3:
            if ((GRef<u32>(fade) & 8) != 0) StartTitleDemo();
            break;
        case 4:
            if ((GRef<u32>(obj + 0x240) & 4) != 0) GRef<u32>(obj + 0x67C) = 6;
            break;
        case 5:
            if (K(0x00410154) < GRef<f32>(fade + 0xC)) GRef<u32>(obj + 0x67C) = 6;
            break;
        case 6: {
            const u32 tree = obj + 0x250;
            GRef<u16>(fade + 0x32) = 0x33;
            debug::OnGuestCheckpoint(0x0022D84C);
            UpdateMenuTree(tree, repeat);
            if ((edge & 0x20) != 0) {
                MainMenuBack(obj, tree);
            } else if ((edge & 0x50) != 0) {
                debug::OnGuestCheckpoint(0x0022D880);
                MainMenuSelect(obj, tree);
            }
            break;
        }
        case 7:
            if ((GRef<u32>(obj + 0x240) & 8) != 0) MenuGoTo(GRef<s32>(obj + 0x680), static_cast<u16>(GRef<u32>(obj + 0x684)));
            break;
        case 8:
            if ((GRef<u32>(fade) & 8) != 0) MenuGoTo(GRef<s32>(obj + 0x680), static_cast<u16>(GRef<u32>(obj + 0x684)));
            break;
        case 9:  // port: QUIT GAME, the screen is black
            if ((GRef<u32>(fade) & 8) != 0) port::Menu().quit = true;
            break;
        default:
            break;
    }
}

// EE: 0x0022D8F8
// Piecewise-linear curve {s32 count, {f32 t, f32 value}*}: clamped at both ends.
f32 EvalCurve(f32 t, u32 curve) {
    const s32 n = GRef<s32>(curve);
    const u32 pts = GRef<u32>(curve + 4);
    s32 i = 0;
    if (n > 0 && !(t < GRef<f32>(pts))) {
        for (i = 1; i < n; ++i) {
            if (t < GRef<f32>(pts + 8u * static_cast<u32>(i))) break;
        }
    }
    if (i == 0) return GRef<f32>(pts + 4);
    const u32 p = pts + 8u * static_cast<u32>(i);
    if (i == n) return GRef<f32>(p - 4);
    const f32 v0 = GRef<f32>(p - 4);
    const f32 t0 = GRef<f32>(p - 8);
    const f32 dv = fpu::Sub(GRef<f32>(p + 4), v0);
    const f32 dt = fpu::Sub(GRef<f32>(p), t0);
    return fpu::Add(v0, (dv * fpu::Sub(t, t0)) / dt);
}

// EE: 0x0022D9A8
// Title logo: 6 sprites (0x0039D700, stride 0x20: TEX0, uv0, uv1, x, y, w, h) faded in along
// their curves (0x0039D838, stride 8) by the title time, under the logo alpha; narrower in 16:9.
// The logo textures are uploaded by the DATA 0xB0 packet called first (+0x224).
void DrawTitleLogo(u32 list, s32 index, u32 obj) {
    s32 alpha = fpu::FloatToInt(GRef<f32>(obj + 0x690) * 128.0f);
    if (alpha <= 0) return;
    if (!(alpha < 0x81)) alpha = 0x80;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, GRef<u32>(obj + 0x224));
    u32 p = BuildFrameDrawEnv(tag + 0x10);
    GRef<u64>(p) = 0x11AB400000008002ull;
    const bool wide = (Ctx()->flags & 0x10000) != 0;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = static_cast<u64>(static_cast<s64>(alpha)) << 0x20 | 0x68;
    GRef<u64>(p + 0x18) = 0x43;
    const f32 t = fpu::Sub(GRef<f32>(obj + 0x68C), 0.5f);
    GRef<u64>(p + 0x28) = 0x19;
    GRef<u64>(p + 0x20) = 0;
    p += 0x30;
    u32 sprite = 0x0039D700;
    u32 curve = 0x0039D838;
    for (s32 i = 0; i < 6; ++i, p += 0x40, sprite += 0x20, curve += 8) {
        const u64 color = LerpColor(EvalCurve(t, curve), 0, 0x808080);
        const s32 h = GRef<s32>(sprite + 0x1C);
        const s32 x = GRef<s32>(sprite + 0x10);
        const s32 y = GRef<s32>(sprite + 0x14);
        s32 w = GRef<s32>(sprite + 0x18);
        if (wide) w = (w * 3) / 4;
        GRef<u64>(p) = 0x6400000000008001ull;
        GRef<u64>(p + 8) = 0x53D317;
        const s32 left = x - (w >> 1);
        const s32 top = y - (h >> 1);
        GRef<u64>(p + 0x18) = color & 0xFFFFFFFFull;
        GRef<u64>(p + 0x10) = GRef<u64>(sprite);
        GRef<u64>(p + 0x28) = static_cast<u64>(static_cast<s64>(left)) | static_cast<u64>(static_cast<s64>(top)) << 0x10;
        GRef<u64>(p + 0x20) = GRef<u32>(sprite + 8);
        GRef<u64>(p + 0x38) = static_cast<u64>(static_cast<s64>(left + w)) |
                              static_cast<u64>(static_cast<s64>(top + h)) << 0x10;
        GRef<u64>(p + 0x30) = GRef<u32>(sprite + 0xC);
    }
    LinkPacket(list, index, start, CloseDmaPacket(tag, p));
}

// EE: 0x0022DC20
// Title/menu frame: the tree, help glyphs and help box (title-to-menu states 1 and 4+), the
// object fade, the logo, then (bucket 1) the frame filter, alpha filter, tabs and menu fade.
void DrawMainMenu(u32 obj) {
    const u32 fade = g_pMenuFade();
    InitBucketList(obj, 2);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), obj);
    const u32 state = GRef<u32>(obj + 0x67C);
    if (state == 1 || !(state < 4)) {
        DrawMenuTree(obj, 0, obj + 0x250);
        UploadMessageBoxGlyphs(obj, 0, obj + 0x658);
        DrawMessageBox(obj, 0, obj + 0x658);
    }
    DrawBlackFade(GRef<f32>(obj + 0x24C), obj, GRef<u32>(obj + 0x67C) < 7 ? 0 : 1);
    if (0.0f < GRef<f32>(obj + 0x690)) DrawTitleLogo(obj, 0, obj);
    DrawMenuFrameFilter(1.0f, obj, 1, 3);
    DrawMenuAlphaFilter(fpu::Sub(1.0f, GRef<f32>(obj + 0x690)) * K(0x00410158), obj, 1);
    DrawMenuTabs(obj, 1, fade + 0x14);
    DrawBlackFade(GRef<f32>(fade + 0xC), obj, 1);
}

// EE: 0x0022DD50
// Background while the title data loads (one bucket at `list`).
void DrawTitleBackground(u32 list, u32 obj) {
    const u32 fade = g_pMenuFade();
    InitBucketList(list, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), list);
    DrawMenuFrameFilter(1.0f, list, 0, 3);
    DrawMenuAlphaFilter(fpu::Sub(1.0f, GRef<f32>(obj + 0x690)) * K(0x0041015C), list, 0);
    DrawMenuTabs(list, 0, fade + 0x14);
    DrawBlackFade(GRef<f32>(fade + 0xC), list, 0);
}

// EE: 0x0022DE00
// Subs 2-4: heap back to the page mark, main menu object, DATA 0xB0 queued (font block, logo
// packet), then sub 5.
void EnterTitle() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateMainMenu();
    g_TitleMenu() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB0, 2, obj + 0x220);
    DrawTitleBackground(obj, obj);
    RequestSubState(5);
}

// EE: 0x0022DEA0
// Sub 5: waits until the queue is idle, then the font block and the help box; sub 6.
void LoadTitle() {
    const u32 obj = g_TitleMenu();
    DrawTitleBackground(obj, obj);
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x658);
        RequestSubState(6);
    }
}

// EE: 0x0022DF00
// Sub 6: title screen and main menu.
void RunTitle() {
    debug::OnGuestCheckpoint(0x0022DF00);
    const u32 obj = g_TitleMenu();
    UpdateFade(obj + 0x240);
    MainMenuUpdate(obj);
    DrawMainMenu(obj);
}

// EE: 0x0022DF38
// Dead (no reference): 0 while a - b (unsigned) <= 0x20, else (a - b)^3 * k (0x00410160).
f32 func_0022DF38(u32 a, u32 b) {
    const u32 d = a - b;
    if (d < 0x21) return 0.0f;
    const f32 f = static_cast<f32>(static_cast<s32>(d));
    return f * f * f * GRef<f32>(0x00410160);
}

// EE: 0x0023C7B0
// Space a save needs on the card, in KB (75).
s32 GetSaveSizeKb() { return 0x4B; }

// ---- Sub-states of later slices -----------------------------------------------------------------

namespace {

// Sub-state handlers of table 0x00384640 that belong to later slices (docs/port/slices.md): they
// log once and do nothing, so the menu keeps running (the fade overlay and BGM still update).
template <u32 kAddress>
void MenuSubStub() {
    char key[40];
    std::snprintf(key, sizeof(key), "menu-sub-stub-%08X", kAddress);
    log::WriteOnce(key, log::Level::Info, "game", "scene 4 sub-state %u: handler 0x%08X not translated yet (later slice)",
                   static_cast<unsigned>(Ctx()->sub), static_cast<unsigned>(kAddress));
}

}  // namespace

AC04_GUEST_FUNCTION(0x002262D0, SceneMenu);
AC04_GUEST_FUNCTION(0x002261B8, MenuEnterPage);
AC04_GUEST_FUNCTION(0x00226248, MenuEnterSubPage);
AC04_GUEST_FUNCTION(0x0022DE00, EnterTitle);
AC04_GUEST_FUNCTION(0x0022DEA0, LoadTitle);
AC04_GUEST_FUNCTION(0x0022DF00, RunTitle);

#define AC04_MENU_STUB(addr) AC04_GUEST_FUNCTION(addr, MenuSubStub<addr>);
#undef AC04_MENU_STUB

}  // namespace ac04::game
