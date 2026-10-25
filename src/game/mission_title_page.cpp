// Mission title card (scene 4 subs 14-16, page 2; docs/subsystems/frontend.md "Page 2"): the
// mission's title image (DATA 0xBB + mission - 1) zooms in, Start/X or 5 s fade it out, then the
// mission id is set and the opening movie page (mission 1) or the briefing follows.
//
// Page object (0x260 bytes, *0x00411814): +0 bucket list (1 bucket), +0x10 data load queue, +0x220
// DATA item (the title image packet), +0x230 fade object, +0x23C fade level, +0x240 state (0 wait,
// 1 shown, 3 fading out), +0x244 loading, +0x248 f32 time on the page (s), +0x24C f32 zoom time
// (s), +0x250 f32 zoom level, +0x254 mission index (0-17).
#include "game/game.h"

#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_MissionTitle, 0x00411814)  // gp-0x41DC
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kTitleSprite = 0x0039D8B0;  // sprite descriptor of the title image

}  // namespace

// EE: 0x0022A478
// One textured sprite from a descriptor {u64 TEX0, u32 UV0 (+8), UV1 (+0xC), XY0 (+0x10), XY1
// (+0x14), RGBA (+0x18)}: a REGLIST tag (TEX0_2, RGBAQ, UV, XYZ3, UV, XYZ2) and its 6 registers.
// Returns the end of the data.
u32 WriteDescriptorSprite(u32 p, u32 desc) {
    GRef<u64>(p) = 0x6400000000008001ull;
    GRef<u64>(p + 8) = 0x53D317;
    GRef<u64>(p + 0x10) = GRef<u64>(desc);
    GRef<u64>(p + 0x18) = GRef<u32>(desc + 0x18);
    GRef<u64>(p + 0x20) = GRef<u32>(desc + 8);
    GRef<u64>(p + 0x28) = GRef<u32>(desc + 0x10);
    GRef<u64>(p + 0x30) = GRef<u32>(desc + 0xC);
    GRef<u64>(p + 0x38) = GRef<u32>(desc + 0x14);
    return p + 0x40;
}

// EE: 0x0022A4D0
// Dead (no reference): one textured sprite of descriptor `desc` (TEX0 +0, UV0 +8, UV1 +0xC, width
// +0x1C, height +0x1E) centred on (x, y) with colour `rgba`. Returns the end of the data.
u32 WriteCentredDescriptorSprite(u32 p, u32 desc, s32 x, s32 y, u32 rgba) {
    GRef<u64>(p) = 0x6400000000008001ull;
    GRef<u64>(p + 8) = 0x53D317;
    const u16 h = GRef<u16>(desc + 0x1E);
    const u16 w = GRef<u16>(desc + 0x1C);
    GRef<u64>(p + 0x10) = GRef<u64>(desc);
    GRef<u64>(p + 0x18) = rgba;
    const s32 y0 = y - (h >> 1);
    const s32 x0 = x - (w >> 1);
    GRef<u64>(p + 0x28) = static_cast<u64>(static_cast<s64>(x0)) | static_cast<u64>(static_cast<s64>(y0)) << 16;
    GRef<u64>(p + 0x20) = GRef<u32>(desc + 8);
    const s32 y1 = y0 + h, x1 = x0 + w;
    GRef<u64>(p + 0x38) = static_cast<u64>(static_cast<s64>(x1)) | static_cast<u64>(static_cast<s64>(y1)) << 16;
    GRef<u64>(p + 0x30) = GRef<u32>(desc + 0xC);
    return p + 0x40;
}

// EE: 0x0022A558
// Black bars over the top `top` and bottom `bottom` lines (12.4 fixed point) of the draw buffer at
// level `level` (alpha level * 128, ALPHA 0x64).
void DrawLetterboxBars(f32 level, u32 list, s32 index, s64 top, s32 bottom) {
    const s32 alpha = fpu::FloatToInt(level * 128.0f);
    if (alpha <= 0) return;
    const MainContext* ctx = Ctx();
    const u32 start = ctx->cur_buf.addr;
    const u16 h = ctx->draw_h;
    const u64 w = static_cast<u64>(ctx->draw_w) << 4;
    const u32 p = BuildMenuDrawEnv(start + 0x10, ctx->draw_w, h);
    const s32 h16 = static_cast<s32>(h) * 0x10;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x20) = static_cast<u64>(static_cast<s64>(alpha)) << 32 | 100;
    GRef<u64>(p) = 0x1123400000000003ull;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x28) = 0x43;
    GRef<u64>(p + 0x38) = 1;
    GRef<u64>(p + 0x10) = 0;
    GRef<u64>(p + 0x30) = 0;
    u32 e = BuildSpriteStrips(p + 0x40, 0, w | static_cast<u64>(top) << 0x10);
    e = BuildSpriteStrips(e, static_cast<u64>(static_cast<s64>(h16 - bottom)) << 0x10,
                          w | static_cast<u64>(static_cast<s64>(h16)) << 0x10);
    LinkPacket(list, index, start, CloseDmaPacket(start, e));
}

// EE: 0x002301F0
// Initialises the page object: state `state`, the menu fade requests cleared, fade cleared, the
// mission index from g_MissionNo (0 when out of 1-18), timers and zoom 0.
void InitMissionTitle(u32 obj, s32 state) {
    const u32 fade = g_pMenuFade();
    GRef<s32>(obj + 0x240) = state;
    GRef<u16>(fade + 0x32) = 0;
    GRef<u16>(fade + 0x30) = 0;
    InitFade(obj + 0x230, 0);
    const u32 n = g_MissionNo();
    GRef<u32>(obj + 0x254) = n - 1;
    if (n - 1 > 0x11) GRef<u32>(obj + 0x254) = 0;
    GRef<u32>(obj + 0x250) = 0;
    GRef<u32>(obj + 0x248) = 0;
    GRef<u32>(obj + 0x24C) = 0;
}

// EE: 0x00230258
// Creates the page object (work heap, 0x260 bytes) and queues its title image, DATA 0xBB + index.
void CreateMissionTitle(s32 state) {
    PopWorkHeapMark();
    PushWorkHeapMark();
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = WorkHeapAlloc(0x260, 0x10);
    g_MissionTitle() = obj;
    InitMissionTitle(obj, state);
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, static_cast<u16>(GRef<s32>(obj + 0x254) + 0xBB), 1, obj + 0x220);
    GRef<u32>(obj + 0x244) = 1;
}

// EE: 0x00230308
// Per frame: time += dt; once shown, the zoom runs over k1 s as (1 - cos(t * k2)) * k3, then
// overshoots back to 1 by (cos((t - k1) * k4) + 1) * k5 / 2 + 1 until t = 1. After the zoom (t > k6)
// Start/X fade out in 0.5 s, or after 5 s on the page in 1.5 s. When the fade is out: game[0x27] =
// the mission id (g_MissionNo forced to 1 when out of range), then the opening movie page 0x1F for
// mission 1, else the briefing (page 3).
void MissionTitleUpdate(u32 obj) {
    const u32 buttons = Ctx()->pad_buttons[0][1];
    UpdateFade(obj + 0x230);
    const f32 dt = K(0x004101B8);
    GRef<f32>(obj + 0x248) = fpu::Add(GRef<f32>(obj + 0x248), dt);
    s32 state = GRef<s32>(obj + 0x240);
    if (state == 0) {
        GRef<u32>(obj + 0x250) = 0;
    } else {
        const f32 t = GRef<f32>(obj + 0x24C);
        const f32 k1 = K(0x004101BC);
        GRef<f32>(obj + 0x24C) = fpu::Add(t, dt);
        f32 zoom;
        if (t < k1) {
            const f32 c = ee::cosf(t * K(0x004101C0));
            state = GRef<s32>(obj + 0x240);
            zoom = fpu::Sub(1.0f, c) * K(0x004101C4);
        } else if (t < 1.0f) {
            const f32 c = ee::cosf(fpu::Sub(t, k1) * K(0x004101C8));
            state = GRef<s32>(obj + 0x240);
            zoom = fpu::Add(fpu::Add(c, 1.0f) * K(0x004101CC) * 0.5f, 1.0f);
        } else {
            zoom = 1.0f;
        }
        GRef<f32>(obj + 0x250) = zoom;
    }
    if (state == 1) {
        if (!(K(0x004101D0) < GRef<f32>(obj + 0x24C))) return;
        f32 time;
        if ((buttons & 0x50) != 0) {
            time = 0.5f;
        } else {
            if (!(5.0f < GRef<f32>(obj + 0x248))) return;
            time = 1.5f;
        }
        StartFade(time, obj + 0x230, 1);
        GRef<s32>(obj + 0x240) = 3;
    } else if (state == 0) {
        if (0.5f < GRef<f32>(obj + 0x248)) GRef<s32>(obj + 0x240) = 1;
    } else if (state == 3) {
        if ((GRef<u32>(obj + 0x230) & 8) == 0) return;
        u16 n = g_MissionNo();
        if (static_cast<u16>(n - 1) >= 0x12) {
            g_MissionNo() = 1;
            n = 1;
        }
        g_GameState()[0x27] = static_cast<u8>(GetMissionId(0, n));
        EnterMenu(n == 1 ? 0x1F : 3);
    }
}

// EE: 0x00230548
// The title image (the DATA item's packet, called from the DMA chain) as the sprite of descriptor
// 0x0039D8B0 at alpha level * 128 (clamped to 0xFF), ALPHA 0xA8.
void DrawMissionTitleImage(f32 level, u32 list, s32 index, u32 image) {
    s32 alpha = fpu::FloatToInt(level * 128.0f);
    if (alpha <= 0) return;
    const u32 start = Ctx()->cur_buf.addr;
    if (alpha > 0xFF) alpha = 0xFF;
    const u32 tag = WriteDmaCallTag(start, image);
    const u32 p = BuildMenuDrawEnv(tag + 0x10, 0x280, 0x1C0);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p) = 0x11AB400000000002ull;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x20) = static_cast<u64>(static_cast<s64>(alpha)) << 32 | 0xA8;
    GRef<u64>(p + 0x28) = 0x43;
    GRef<u64>(p + 0x10) = 0;
    LinkPacket(list, index, start, CloseDmaPacket(tag, WriteDescriptorSprite(p + 0x30, kTitleSprite)));
}

// EE: 0x00230668
// Draws the page: the title image once shown, the menu frame filter, the 56-line letterbox bars and
// the black fade.
void DrawMissionTitlePage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(g_pMainContext().addr + offsetof(MainContext, vif1_list), obj);
    if (GRef<s32>(obj + 0x240) != 0) DrawMissionTitleImage(GRef<f32>(obj + 0x250), obj, 0, GRef<u32>(obj + 0x220));
    DrawMenuFrameFilter(1.0f, obj, 0, 3);
    DrawLetterboxBars(1.0f, obj, 0, 0x380, 0x380);
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
}

// EE: 0x00230708
// Sub 14: creates the page, then sub 15.
void EnterMissionTitle() {
    CreateMissionTitle(0);
    RequestSubState(0xF);
}

// EE: 0x00230728
// Sub 15: when the title image is loaded, fades in over 0.5 s, fades the menu BGM out over 3 s, then
// sub 16.
void LoadMissionTitle() {
    const u32 obj = g_MissionTitle();
    if (UpdateMenuDataLoad(obj + 0x10) >= 0) return;
    GRef<u32>(obj + 0x244) = 0;
    StartFade(0.5f, obj + 0x230, 0);
    RequestSubState(0x10);
    FadeOutMenuBgm(3.0f);
}

// EE: 0x00230790
// Sub 16: page update and draw.
void MissionTitleRun() {
    debug::OnGuestCheckpoint(0x00230790);
    const u32 obj = g_MissionTitle();
    MissionTitleUpdate(obj);
    DrawMissionTitlePage(obj);
}

AC04_GUEST_FUNCTION(0x00230708, EnterMissionTitle);
AC04_GUEST_FUNCTION(0x00230728, LoadMissionTitle);
AC04_GUEST_FUNCTION(0x00230790, MissionTitleRun);

}  // namespace ac04::game
