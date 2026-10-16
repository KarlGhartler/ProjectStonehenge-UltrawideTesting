// Briefing (scene 4 subs 17-22, pages 3/4; docs/subsystems/frontend.md "Briefing"): the mission's
// briefing movie (MIS nn) with timed subtitles and the header texts, then the map view: the
// terrain as coloured point sprites, unit icons, the info texts and the camera presets.
//
// Page object (0x5D0 bytes, *0x00411820): +0 bucket list, +0x10 packet list, +0x20 data load queue,
// +0x230 DATA 0x111 + mission {font block, map data (+0x234), map frame image (+0x238)}, +0x250
// terrain map (camera object + 0x1C0 look-at, +0x200/+0x204 x/z, +0x208 scale, +0x210 heights,
// +0x214 colours), +0x470 fade (+0x47C level), +0x480 map fade (+0x48C level), +0x490 crosshair,
// +0x4CC panel (+0x4E0/+0x4E8 panel levels, +0x518 text level), +0x52C crosshair pointer, +0x530
// message box (subtitle line; +0x538 type-out position, +0x544 length), +0x554 state, +0x558/+0x55C
// MenuGoTo target, +0x560 mission index (0-17), +0x564 frames, +0x568 subtitle index, +0x56C
// subtitle count, +0x570 subtitle table {u32 message, f32 time}, +0x574 subtitle state (0 wait, 1
// typing, 2 shown, 3 done), +0x578 movie length (s), +0x57C movie time (s), +0x580 panel start,
// +0x584 panel open time, +0x588 message count, +0x58C/+0x590 coordinates, +0x5A0 camera preset,
// +0x5A4 preset count, +0x5A8 presets (0x18 bytes: x, z, scale, orbit radius, height, target y),
// +0x5AC.. the selected preset, +0x5C4 orbit phase.
#include "game/game.h"

#include <cmath>
#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_Briefing, 0x00411820)              // gp-0x41D0
AC04_GUEST_VAR(u32, g_BriefingCameraPresets, 0x00411078)  // -> {u32 count, u32 presets}[18]
AC04_GUEST_VAR(u32, g_BriefingUnitLists, 0x0041107C)      // -> {u32 count, u32 units}[18]
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

constexpr u32 kSpr = 0x70000000;              // scratchpad
constexpr u32 kHeaderTexts = 0x003A5330;      // 5 strings per mission
constexpr u32 kIconSprites = 0x003A5550;      // per unit type {u16 w, u16 h, .., u32 uv0 (+8), u32 uv1 (+0xC)}
constexpr u32 kFactionColours = 0x003A55C0;   // u32 per faction
constexpr u32 kEnemyCounts = 0x003A54B8;      // s16[4] per mission: AIR, GROUND, SEA, TARGET

f32 K(u32 address) { return GRef<f32>(address); }
u64 Sx(s64 v) { return static_cast<u64>(v); }
u32 MovieRequest() { return GRef<u32>(g_GameState().addr + 0x20); }

// The 3D map transform of the briefing (vmulax/vmadday/vmaddaz/vmaddw with the camera's
// world->screen columns, Q = 1 / w, xyz * Q, ftoi4). Returns the float result; `ints` the ftoi4.
vu0::Vec4 ProjectPoint(const vu0::Vec4 m[4], const vu0::Vec4& v, s32 ints[4]) {
    vu0::Vec4 r = vu0::MulMatrix(m[0], m[1], m[2], m[3], v);
    const f32 q = vu0::Div(1.0f, r.w);
    r.x = vu0::Mul(r.x, q);
    r.y = vu0::Mul(r.y, q);
    r.z = vu0::Mul(r.z, q);
    ints[0] = vu0::FloatToInt4(r.x);
    ints[1] = vu0::FloatToInt4(r.y);
    ints[2] = vu0::FloatToInt4(r.z);
    ints[3] = vu0::FloatToInt4(r.w);
    return r;
}

void StoreSpr(u32 offset, const vu0::Vec4& v) {
    GRef<f32>(kSpr + offset) = v.x;
    GRef<f32>(kSpr + offset + 4) = v.y;
    GRef<f32>(kSpr + offset + 8) = v.z;
    GRef<f32>(kSpr + offset + 12) = v.w;
}

vu0::Vec4 LoadVec(u32 a) { return {GRef<f32>(a), GRef<f32>(a + 4), GRef<f32>(a + 8), GRef<f32>(a + 12)}; }

}  // namespace

// EE: 0x00226320
// Campaign mission number (1-18) of mission id `id` (u8 table 0x003847A8, stride 2); 1 above 0x28.
u8 GetCampaignMissionNumber(u64 id) {
    if (id > 0x28) return 1;
    return GRef<u8>(0x003847A8 + static_cast<u32>(id) * 2);
}

// EE: 0x002363C8
// Shows subtitle `entry` ({u32 message, f32 time}) in message box `mb`.
void SetBriefingSubtitle(u32 mb, u32 entry) { SetMessageBoxText(mb, GRef<s32>(entry)); }

// EE: 0x002363D0
// Subtitles against movie time `time`: waits for the line's time (state 0 -> 1), types it out 2
// characters per frame (SE 9 on odd frames) until the line is complete (state 2), and moves to the
// next line once its time has passed.
void UpdateBriefingSubtitles(f32 time, u32 obj) {
    s32 state = GRef<s32>(obj + 0x574);
    if (state == 3) return;
    s32 n;
    if (state == 0) {
        n = GRef<s32>(obj + 0x568);
        if (GRef<f32>(GRef<u32>(obj + 0x570) + static_cast<u32>(n) * 8 + 4) < time) GRef<s32>(obj + 0x574) = 1;
    } else {
        const f32 pos = fpu::Add(GRef<f32>(obj + 0x538), 2.0f);
        GRef<f32>(obj + 0x538) = pos;
        if (fpu::FloatToInt(pos) < GRef<s16>(obj + 0x544)) {
            GRef<s32>(obj + 0x574) = 1;
            if ((GRef<u32>(obj + 0x564) & 1) != 0) PlayMenuSe(0, 9);
        } else {
            GRef<s32>(obj + 0x574) = 2;
        }
        n = GRef<s32>(obj + 0x568);
    }
    n += 1;
    if (n < GRef<s32>(obj + 0x56C)) {
        const u32 entry = static_cast<u32>(n) * 8 + GRef<u32>(obj + 0x570);
        if (GRef<f32>(entry + 4) < time) {
            GRef<s32>(obj + 0x568) = n;
            SetBriefingSubtitle(obj + 0x530, entry);
        }
    }
}

// EE: 0x002364D8
// The subtitle line, while a line is up and until 0.5 s before the movie ends.
void DrawBriefingSubtitle(u32 list, s32 index, u32 obj) {
    if (GRef<s32>(obj + 0x574) == 0) return;
    if (fpu::Sub(GRef<f32>(obj + 0x578), 0.5f) < GRef<f32>(obj + 0x57C)) return;
    UploadMessageBoxGlyphs(list, index, obj + 0x530);
    DrawMessageBox(list, index, obj + 0x530);
}

// EE: 0x00236568
// Header panel (5 rectangles, 0x003A51E0) and the briefing movie MIS(index + 1) (movie 0xD + index).
void StartBriefingMovie(u32 obj) {
    InitPanel(obj + 0x4CC, 5, 0x003A51E0, 0);
    GRef<u16>(obj + 0x4CE) |= 4;
    PushWorkHeapMark();
    StartMenuMovie(GRef<s32>(obj + 0x560) + 0xD, 0, 0);
}

// EE: 0x002365C0
// Creates the page object (work heap, 0x5D0 bytes): MenuGoTo target (1, 0x17 from sub 0x11 = the
// campaign path, else 0x45), the mission index from game[0x27], the per-mission timings
// (0x003AF8B8 length, 0x003AF900 panel start, 0x003AF948 panel open) and subtitle list
// (0x0039E528), the menu fade requests, and the mission's BGM.
u32 CreateBriefing() {
    const u32 obj = WorkHeapAlloc(0x5D0, 0x10);
    ResetPacketList(obj + 0x10);
    GRef<u32>(obj + 0x558) = 1;
    const u16 sub = Ctx()->sub;
    GRef<u32>(obj + 0x554) = 0;
    GRef<u32>(obj + 0x55C) = sub == 0x11 ? 0x17 : 0x45;
    u32 m = static_cast<u32>(GetCampaignMissionNumber(g_GameState()[0x27])) - 1;
    if (m > 0x11) m = 0;
    GRef<u32>(obj + 0x57C) = 0;
    GRef<u32>(obj + 0x578) = GRef<u32>(0x003AF8B8 + m * 4);
    GRef<u32>(obj + 0x560) = m;
    GRef<u32>(obj + 0x580) = GRef<u32>(0x003AF900 + m * 4);
    GRef<u32>(obj + 0x564) = 0;
    GRef<u32>(obj + 0x568) = 0;
    const u32 fade = g_pMenuFade();
    GRef<u16>(obj + 0x534) = 0x380;
    GRef<u16>(obj + 0x536) = 0x15D0;
    GRef<u32>(obj + 0x574) = 0;
    GRef<u32>(obj + 0x56C) = GRef<u32>(0x0039E528 + m * 8);
    GRef<u32>(obj + 0x570) = GRef<u32>(0x0039E52C + m * 8);
    GRef<u32>(obj + 0x584) = GRef<u32>(0x003AF948 + m * 4);
    GRef<u16>(fade + 0x32) = 0x10;
    GRef<u16>(fade + 0x30) = 0;
    InitFade(obj + 0x470, 1);
    PlayMenuBgm(static_cast<s16>(GetProgressBgmTrack(0, GRef<s32>(obj + 0x560) + 1)));
    return obj;
}

// EE: 0x002366F8
// Start ends the briefing movie (fade out 0.5 s, movie stop 1 s). Circle, once allowed (menu fade
// +0x30): completes a typing line (SE 0xB), shows a waiting line (SE 0xD), or moves to the next line
// (SE 0xD); after the last line it ends the movie as Start does.
void BriefingInput(u32 obj) {
    const u32 buttons = Ctx()->pad_buttons[0][1];
    if ((buttons & 0x40) != 0) {
        PlayMenuSe(0, 0xD);
        GRef<u32>(obj + 0x554) = 2;
        RequestMovieStop(1.0f);
        FadeOutStreamSlots(0.5f);
        StartFade(0.5f, obj + 0x470, 1);
        return;
    }
    if (GRef<s16>(g_pMenuFade() + 0x30) == 0 || (buttons & 0x10) == 0) return;
    const s32 state = GRef<s32>(obj + 0x574);
    if (state == 1) {
        PlayMenuSe(0, 0xB);
        GRef<f32>(obj + 0x538) = 1024.0f;
    } else if (state == 0) {
        PlayMenuSe(0, 0xD);
        GRef<u32>(obj + 0x574) = 1;
    } else if (state == 2) {
        PlayMenuSe(0, 0xD);
        const s32 n = GRef<s32>(obj + 0x568) + 1;
        if (GRef<s32>(obj + 0x568) < GRef<s32>(obj + 0x56C) - 1) {
            GRef<s32>(obj + 0x568) = n;
            SetBriefingSubtitle(obj + 0x530, GRef<u32>(obj + 0x570) + static_cast<u32>(n) * 8);
        } else {
            GRef<u32>(obj + 0x554) = 2;
            GRef<u32>(obj + 0x538) = 0;
            GRef<u32>(obj + 0x574) = 3;
            RequestMovieStop(1.0f);
            FadeOutStreamSlots(0.5f);
            StartFade(0.5f, obj + 0x470, 1);
        }
    }
}

// EE: 0x00236880
// Per movie frame: movie time = pictures shown * k; the header panel opens at its start time and
// unfolds after its open time; subtitles; input once 30 frames have passed (Circle only after
// time k2).
void BriefingUpdate(u32 obj) {
    const f32 k = K(0x00410298);
    GRef<s32>(obj + 0x564) += 1;
    const u32 fade = g_pMenuFade();
    const f32 time = static_cast<f32>(GRef<s32>(MovieRequest() + 0x18)) * k;
    GRef<f32>(obj + 0x57C) = time;
    if (GRef<f32>(obj + 0x580) <= time) {
        const u32 panel = obj + 0x4CC;
        if (GRef<f32>(obj + 0x584) < time && (GRef<u16>(obj + 0x4CC) & 0xA) == 0) SetPanelState(panel, 1);
        UpdatePanel(panel);
        UpdatePanel(panel);
    }
    GRef<u16>(fade + 0x30) = 0;
    UpdateBriefingSubtitles(time, obj);
    const u32 state = GRef<u32>(obj + 0x554);
    if (state == 0) {
        if (GRef<s32>(obj + 0x564) >= 0x1F) GRef<u32>(obj + 0x554) = 1;
    } else if (state == 1) {
        if (K(0x0041029C) <= time) GRef<u16>(fade + 0x30) = 1;
        BriefingInput(obj);
    }
}

// EE: 0x002369A8
// Header texts over the movie: 4 frame labels and the mission's DATE/AREA, PARALLEL, OPERATION,
// COORDINATES and CONDITION lines, typed out by the text level (+0x518 * 90 characters). SE 9 while
// the COORDINATES line is still typing; panel flag 0x100 cleared once it is done.
void DrawBriefingHeader(u32 list, s32 index, u32 obj) {
    bool typing = false;
    const u32 s = kHeaderTexts + GRef<u32>(obj + 0x560) * 0x14;
    BeginMenuTextLayer(list, index);
    if (0.0f < GRef<f32>(obj + 0x518)) {
        f32 t = GRef<f32>(obj + 0x518) * 90.0f;
        DrawMenuText(t, 0x620, 0x250, 5, 0x00411080);
        const f32 t8 = fpu::Sub(t, 8.0f);
        DrawMenuText(t8, 0x1E80, 0x250, 5, 0x00411088);
        const f32 t16 = fpu::Sub(t, 16.0f);
        DrawMenuText(t16, 0x620, 0x14A0, 5, 0x00411090);
        DrawMenuText(fpu::Sub(t, 24.0f), 0x1E80, 0x14A0, 5, 0x00411098);
        DrawMenuText(t, 0x200, 0x4B0, 4, GRef<u32>(s));
        DrawMenuText(t16, 0x200, 0x9E0, 4, GRef<u32>(s + 4));
        DrawMenuText(fpu::Sub(t, 32.0f), 0x200, 0xE80, 4, GRef<u32>(s + 8));
        DrawMenuText(t8, 0x1DC0, 0x4B0, 4, 0x004110A0);
        DrawMenuText(fpu::Sub(t, 9.0f), 0x1E80, 0x4B0, 4, GRef<u32>(s + 0xC));
        DrawMenuText(t16, 0x1DC0, 0xF60, 4, 0x004110A0);
        DrawMenuText(fpu::Sub(t, 17.0f), 0x1E80, 0xF60, 4, GRef<u32>(s + 0x10));
        const u32 len = ee::strlen(GRef<u32>(s + 0xC));
        f32 flen;
        if (static_cast<s32>(len) >= 0) {
            flen = static_cast<f32>(static_cast<s32>(len));
        } else {
            flen = static_cast<f32>(static_cast<s32>((len & 1) | (len >> 1)));
            flen = fpu::Add(flen, flen);
        }
        t = fpu::Sub(t, fpu::Add(flen, 9.0f));
        if (t < 0.0f) {
            typing = true;
        } else if (6.0f < t && (GRef<u16>(obj + 0x4CC) & 0x100) != 0) {
            GRef<u16>(obj + 0x4CC) &= 0xFEFF;
        }
    }
    EndMenuTextLayer();
    if (typing) PlayMenuSe(0, 9);
}

// EE: 0x00236C50
// Draws the movie page (subtitle, header panel and texts, menu tabs, black fade, line blend) into
// the page's own packet list and sends it (the movie loop runs its own frame).
void DrawBriefingMoviePage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(obj + 0x10, obj);
    DrawBriefingSubtitle(obj, 0, obj);
    DrawPanel(obj, 0, obj + 0x4CC);
    DrawBriefingHeader(obj, 0, obj);
    DrawMenuTabs(obj, 0, g_pMenuFade() + 0x14);
    DrawBlackFade(GRef<f32>(obj + 0x47C), obj, 0);
    DrawMovieLineBlend(obj, 0, 0x68);
    FlushCache(0);
    SendDmaPacketList(1, obj + 0x10, 1);
    Ctx()->cur_buf.addr = SwapDoubleBuffer(g_pMainContext().addr + offsetof(MainContext, dbuf));
    sceGsSyncPath(0, 0);
}

// EE: 0x00236D28
// Selects camera preset `n` (6 floats) and moves the crosshair to its x/z on the map frame.
void SelectBriefingCameraPreset(u32 obj, s32 n) {
    const u32 e = static_cast<u32>(n) * 0x18 + GRef<u32>(obj + 0x5A8);
    const f32 x = GRef<f32>(e);
    GRef<f32>(obj + 0x5AC) = x;
    const f32 z = GRef<f32>(e + 4);
    GRef<f32>(obj + 0x5B0) = z;
    GRef<u32>(obj + 0x5B4) = GRef<u32>(e + 8);
    GRef<u32>(obj + 0x5B8) = GRef<u32>(e + 0xC);
    GRef<u32>(obj + 0x5BC) = GRef<u32>(e + 0x10);
    GRef<u32>(obj + 0x5C0) = GRef<u32>(e + 0x14);
    MoveCrosshairAuto(GRef<u32>(obj + 0x52C), fpu::FloatToInt(x * 0.00390625f) + 0x580,
                      fpu::FloatToInt(z * 0.0036621094f) + 0x10D0);
}

// EE: 0x00236DA8
// After the movie: fades, the map panel (7 rectangles, 0x003A5230, with the crosshair), the
// message box cleared and the terrain map from the map data.
void InitBriefingMap(u32 obj) {
    GRef<u32>(obj + 0x554) = 0;
    InitFade(obj + 0x470, 1);
    InitFade(obj + 0x480, 0);
    InitFade(g_pMenuFade(), 0);
    InitPanel(obj + 0x4CC, 7, 0x003A5230, obj + 0x490);
    GRef<u16>(obj + 0x4CE) |= 6;
    SetMessageBoxText(obj + 0x530, 0);
    GRef<u32>(obj + 0x538) = 0;
    InitTerrainMap(obj + 0x250, GRef<u32>(obj + 0x234));
}

// EE: 0x00236E30
// Map view start: 640 x 448 menu display, menu fade in, the mission's coordinates (0x003A52A0) and
// camera presets, preset 0.
void EnterBriefingMap(u32 obj) {
    SetMenuDisplayMode(Ctx(), 0x280, 0x1C0);
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[20] = 0x1310000DCull;
    StartFade(0.5f, g_pMenuFade(), 0);
    const u32 fade = g_pMenuFade();
    s32 m = GRef<s32>(obj + 0x560);
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    if (m > 0x11) {
        GRef<u32>(obj + 0x560) = 0;
        m = 0;
    }
    const u32 presets = g_BriefingCameraPresets() + static_cast<u32>(m) * 8;
    GRef<u32>(obj + 0x58C) = GRef<u32>(0x003A52A0 + static_cast<u32>(m) * 8);
    GRef<u32>(obj + 0x594) = 0;
    GRef<u32>(obj + 0x598) = 0;
    GRef<u32>(obj + 0x5A0) = 0;
    GRef<u32>(obj + 0x590) = GRef<u32>(0x003A52A4 + static_cast<u32>(m) * 8);
    GRef<f32>(obj + 0x59C) = 1.0f;
    GRef<u32>(obj + 0x5A4) = GRef<u32>(presets);
    GRef<u32>(obj + 0x5A8) = GRef<u32>(presets + 4);
    SelectBriefingCameraPreset(obj, 0);
    GRef<u32>(obj + 0x5C4) = 0;
}

// EE: 0x00236F28
// Map camera: the eye orbits the preset (radius +0x5B8, height +0x5BC) with phase frac(frames * k1
// * k2) * k3; the eye eases towards it (sceVu0InterVector, k4), the target height, scale and
// position ease towards the preset (ApproachValue).
void UpdateBriefingMapCamera(u32 obj) {
    const f32 a = static_cast<f32>(GRef<s32>(obj + 0x564)) * K(0x004102A0);
    f32 phase = a * K(0x004102A4);
    GRef<f32>(obj + 0x5C4) = a;
    phase = fpu::Sub(phase, ee::floorf(phase));
    const f32 radius = GRef<f32>(obj + 0x5B8);
    GRef<f32>(obj + 0x5C4) = phase;
    StoreSpr(0, {0.0f, 0.0f, 0.0f, 1.0f});
    const f32 rate = K(0x004102AC);
    phase = phase * K(0x004102A8);
    GRef<f32>(kSpr) = radius * ee::sinf(phase);
    GRef<f32>(kSpr + 8) = radius * ee::cosf(phase);
    GRef<u32>(kSpr + 4) = GRef<u32>(obj + 0x5BC);
    sceVu0InterVector(obj + 0x410, obj + 0x410, kSpr, rate);
    GRef<f32>(obj + 0x424) = ApproachValue(GRef<f32>(obj + 0x424), GRef<f32>(obj + 0x5C0), rate);
    GRef<f32>(obj + 0x458) = ApproachValue(GRef<f32>(obj + 0x458), GRef<f32>(obj + 0x5B4), K(0x004102B0));
    GRef<f32>(obj + 0x450) = ApproachValue(GRef<f32>(obj + 0x450), GRef<f32>(obj + 0x5AC), rate);
    GRef<f32>(obj + 0x454) = ApproachValue(GRef<f32>(obj + 0x454), GRef<f32>(obj + 0x5B0), rate);
}

// EE: 0x00237038
// Map view states: 0 start, 1 wait for the menu fade, 2/3 wait 60 frames each (map fade in, then
// the camera), 4 browse the camera presets (left/right, Circle past the last one or Start ends),
// 5 wait for the fade, then MenuGoTo the stored target.
void BriefingMapUpdate(u32 obj) {
    const u32 buttons = Ctx()->pad_buttons[0][1];
    const u32 fade = g_pMenuFade();
    GRef<s32>(obj + 0x564) += 1;
    GRef<u16>(fade + 0x32) = 0;
    u32 state = GRef<u32>(obj + 0x554);
    if (state >= 2) {
        UpdatePanel(obj + 0x4CC);
        state = GRef<u32>(obj + 0x554);
    }
    if (state >= 4) {
        UpdateMessageBox(K(0x004102B4), obj + 0x530, static_cast<s32>(Ctx()->frame_count));
        state = GRef<u32>(obj + 0x554);
    }
    if (state >= 3) UpdateBriefingMapCamera(obj);
    UpdateTerrainMapCamera(obj + 0x250);
    switch (GRef<u32>(obj + 0x554)) {
        case 0:
            EnterBriefingMap(obj);
            GRef<u32>(obj + 0x554) = 1;
            break;
        case 1:
            if (K(0x004102B8) < GRef<f32>(fade + 0xC)) {
                GRef<u32>(obj + 0x564) = 0;
                GRef<u32>(obj + 0x554) = 2;
                StartFade(0.5f, obj + 0x480, 0);
            }
            break;
        case 2:
            if (GRef<s32>(obj + 0x564) >= 0x3D) GRef<u32>(obj + 0x554) = 3;
            break;
        case 3:
            if (GRef<s32>(obj + 0x564) >= 0x3D) GRef<u32>(obj + 0x554) = 4;
            break;
        case 4: {
            GRef<u16>(fade + 0x32) = 0x1C;
            if ((buttons & 0x40) != 0 ||
                ((buttons & 0x10) != 0 && !(GRef<s32>(obj + 0x5A0) + 1 < GRef<s32>(obj + 0x5A4)))) {
                GRef<u32>(obj + 0x554) = 5;
                PlayMenuSe(0, 0xD);
                SetPanelState(obj + 0x4CC, 1);
                StartFade(0.5f, obj + 0x470, 1);
                StartFade(K(0x004102BC), obj + 0x480, 1);
                return;
            }
            if ((buttons & 0x1C) == 0) break;
            s32 cur = GRef<s32>(obj + 0x5A0);
            s32 n = GRef<s32>(obj + 0x5A4);
            if ((buttons & 0x18) != 0) cur += 1;
            if (!(0 < n)) n = 1;
            if ((buttons & 4) != 0) cur -= 1;
            cur = (cur + n) % n;
            GRef<s32>(obj + 0x5A0) = cur;
            SelectBriefingCameraPreset(obj, cur);
            PlayMenuSe(0, 4);
            break;
        }
        case 5:
            if ((GRef<u32>(obj + 0x480) & 8) != 0)
                MenuGoTo(static_cast<s64>(GRef<s32>(obj + 0x558)), static_cast<u16>(GRef<u32>(obj + 0x55C)));
            break;
        default:
            break;
    }
}

// EE: 0x002372A0
// Map info texts: DATE/AREA, the coordinates X=/Y= (value * 1000 as %03d.%04d), the enemy force
// labels (3 coloured), the 3 unit type labels, AIR/GROUND/SEA counts (NO DATA when negative), the
// TARGET count and the TOTAL, typed out by the text level. SE 9 while the TOTAL still types.
void DrawBriefingMapTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x110);
    const u32 buf = frame.Address();
    GRef<u32>(buf + 0x100) = 0;
    const u32 s = kHeaderTexts + GRef<u32>(obj + 0x560) * 0x14;
    BeginMenuTextLayer(list, index);
    if (0.0f < GRef<f32>(obj + 0x518)) {
        const f32 t = GRef<f32>(obj + 0x518) * 90.0f;
        DrawMenuText(t, 0x280, 0x4B0, 4, GRef<u32>(s));
        DrawMenuText(fpu::Sub(t, 8.0f), 0x280, 0x9D0, 4, 0x00408A18);
        s32 v = fpu::FloatToInt(GRef<f32>(obj + 0x58C) * 1000.0f);
        const u64 xargs[2] = {Sx(v / 1000), Sx(v % 1000)};
        ee::sprintf(buf, 0x00408A28, xargs, 2);
        f32 r = fpu::Sub(t, 16.0f);
        DrawMenuText(r, 0x280, 0xA80, 4, buf);
        v = fpu::FloatToInt(GRef<f32>(obj + 0x590) * 1000.0f);
        const u64 yargs[2] = {Sx(v / 1000), Sx(v % 1000)};
        ee::sprintf(buf, 0x00408A38, yargs, 2);
        const f32 t24 = fpu::Sub(t, 24.0f);
        DrawMenuText(t24, 0x280, 0xB30, 4, buf);
        DrawMenuText(r, 0x1F80, 0x4B0, 4, 0x00408A48);
        DrawMenuTextColored(t24, 0x1F80, 0x560, 10, 0x004110E0, 0x6176FF);
        DrawMenuTextColored(fpu::Sub(t, 32.0f), 0x1F80, 0x610, 10, 0x004110E0, 0x7CC1ED);
        DrawMenuTextColored(fpu::Sub(t, 40.0f), 0x1F80, 0x6C0, 10, 0x004110E0, 0xCCC6C0);
        r = fpu::Sub(t, 25.0f);
        s32 y = 0x560;
        for (u32 k = 0; k < 3; ++k) {
            DrawMenuText(r, 0x2060, y, 4, GRef<u32>(0x003A54AC + k * 4));
            y += 0xB0;
            r = fpu::Sub(r, 8.0f);
        }
        DrawMenuText(fpu::Sub(t, 24.0f), 0x1F80, 0x8E0, 4, 0x004110E8);
        s32 total = 0;
        y = 0x990;
        r = fpu::Sub(t, 32.0f);
        const u32 counts = kEnemyCounts + GRef<u32>(obj + 0x560) * 8;
        for (u32 k = 0; k < 3; ++k) {
            DrawMenuText(r, 0x1F80, y, 3, GRef<u32>(0x003A5498 + k * 4));
            const s16 n = GRef<s16>(counts + k * 2);
            if (n < 0) {
                ee::sprintf(buf, 0x004110F0, nullptr, 0);  // "NO DATA"
                total = -1;
            } else {
                const u64 args[1] = {Sx(n)};
                ee::sprintf(buf, 0x004110F8, args, 1);
                if (!(total < 0)) total += n;
            }
            r = fpu::Sub(r, 8.0f);
            DrawMenuText(r, 0x1F80, y + 0xD0, 3, buf);
            y += 0x1E0;
        }
        r = fpu::Sub(t, 56.0f);
        DrawMenuTextColored(r, 0x1F80, 0xF30, 3, GRef<u32>(0x003A54A4), 0x1C1FA0);
        const s16 target = GRef<s16>(kEnemyCounts + GRef<u32>(obj + 0x560) * 8 + 6);
        if (target < 0) {
            ee::sprintf(buf, 0x004110F0, nullptr, 0);
        } else {
            const u64 args[1] = {Sx(target)};
            ee::sprintf(buf, 0x004110F8, args, 1);
        }
        DrawMenuTextColored(fpu::Sub(r, 8.0f), 0x1F80, 0x1000, 3, buf, 0x1C1FA0);
        r = fpu::Sub(t, 64.0f);
        DrawMenuText(r, 0x1F80, 0x1110, 3, GRef<u32>(0x003A54A8));
        if (total < 0) {
            ee::sprintf(buf, 0x004110F0, nullptr, 0);
        } else {
            const u64 args[1] = {Sx(total)};
            ee::sprintf(buf, 0x004110F8, args, 1);
        }
        DrawMenuText(fpu::Sub(r, 8.0f), 0x1F80, 0x11E0, 3, buf);
        const f32 len = static_cast<f32>(static_cast<s32>(ee::strlen(buf)));
        const f32 rest = fpu::Sub(r, fpu::Add(len, 8.0f));
        if (rest < 0.0f) {
            GRef<u32>(buf + 0x100) = 1;
        } else if (6.0f < rest && (GRef<u16>(obj + 0x4CC) & 0x100) != 0) {
            GRef<u16>(obj + 0x4CC) &= 0xFEFF;
        }
    }
    EndMenuTextLayer();
    if (GRef<u32>(buf + 0x100) != 0) PlayMenuSe(0, 9);
}

// EE: 0x00237808
// Unit icon at screen position `xy` ({u16 x, u16 y}): a sprite of type `type` (size, UVs from
// 0x003A5550 + type * 0x10) in the faction colour; for faction 0 two more sprites (0x003A55A0,
// 0x003A55B0: a frame and a marker 0x150 right, 0x60 up). Returns the end of the data.
u32 WriteBriefingUnitIcon(u32 p, u32 xy, s32 type, s32 faction) {
    const u32 d = kIconSprites + static_cast<u32>(type) * 0x10;
    GRef<u64>(p + 8) = 0x53D310;
    GRef<u64>(p + 0x10) = 0x356;
    GRef<u64>(p) = 0x6400000000008001ull;
    const u32 x = GRef<u16>(xy), y = GRef<u16>(xy + 2);
    const u32 w = GRef<u16>(d), h = GRef<u16>(d + 2);
    GRef<u64>(p + 0x18) = GRef<u32>(kFactionColours + static_cast<u32>(faction) * 4);
    s32 y0 = static_cast<s32>(y - (h >> 1));
    s32 x0 = static_cast<s32>(x - (w >> 1));
    GRef<u64>(p + 0x20) = GRef<u32>(d + 8);
    GRef<u64>(p + 0x28) = Sx(x0) | Sx(y0) << 16;
    GRef<u64>(p + 0x38) = Sx(static_cast<s32>(x0 + w)) | Sx(static_cast<s32>(y0 + h)) << 16;
    GRef<u64>(p + 0x30) = GRef<u32>(d + 0xC);
    if (faction != 0) return p + 0x40;
    const u32 q = p + 0x40;
    const u32 f = kIconSprites + 0x50;
    const u32 fw = GRef<u16>(f), fh = GRef<u16>(f + 2);
    GRef<u64>(q) = 0x4400000000008002ull;
    GRef<u64>(q + 8) = 0x53D3;
    y0 = static_cast<s32>(y - (fh >> 1));
    x0 = static_cast<s32>(x - (fw >> 1));
    GRef<u64>(q + 0x10) = GRef<u32>(f + 8);
    GRef<u64>(q + 0x18) = Sx(x0) | Sx(y0) << 16;
    GRef<u64>(q + 0x28) = Sx(static_cast<s32>(x0 + fw)) | Sx(static_cast<s32>(y0 + fh)) << 16;
    GRef<u64>(q + 0x20) = GRef<u32>(f + 0xC);
    const u32 g = kIconSprites + 0x60;
    const u32 gw = GRef<u16>(g), gh = GRef<u16>(g + 2);
    y0 = static_cast<s32>(y - ((gh >> 1) + 0x60));
    x0 = static_cast<s32>(x - ((gw >> 1) - 0x150));
    GRef<u64>(q + 0x30) = GRef<u32>(g + 8);
    GRef<u64>(q + 0x38) = Sx(x0) | Sx(y0) << 16;
    GRef<u64>(q + 0x48) = Sx(static_cast<s32>(x0 + gw)) | Sx(static_cast<s32>(y0 + gh)) << 16;
    GRef<u64>(q + 0x40) = GRef<u32>(g + 0xC);
    return q + 0x50;
}

// EE: 0x00237998
// Height link of a unit: lines from the icon (`xy0`) down to its ground point (`xy1`'s y) with a
// cross 0x40 x 0x20 wide, in the faction colour at half intensity. Returns the end of the data.
u32 WriteBriefingUnitLink(u32 p, u32 xy0, u32 xy1, s32 faction) {
    const s32 colour = GRef<s32>(kFactionColours + static_cast<u32>(faction) * 4);
    const u32 gy = GRef<u16>(xy1 + 2);
    const u32 x = GRef<u16>(xy0);
    const u32 y = GRef<u16>(xy0 + 2);
    GRef<u64>(p) = 0x8400000000008001ull;
    GRef<u64>(p + 8) = 0x5D5D5D10;
    GRef<u64>(p + 0x10) = 0x24A;
    const u64 c = LerpColor(0.5f, 0, Sx(colour));
    const u64 xr = Sx(static_cast<s32>(x + 0x40));
    const u64 xl = Sx(static_cast<s32>(x - 0x40));
    const u64 ylo = Sx(static_cast<s32>(gy - 0x20)) << 16;
    const u64 yhi = Sx(static_cast<s32>(gy + 0x20)) << 16;
    GRef<u64>(p + 0x48) = xl | yhi;
    GRef<u64>(p + 0x28) = x | static_cast<u64>(gy) << 16;
    GRef<u64>(p + 0x30) = xl | ylo;
    GRef<u64>(p + 0x18) = c & 0xFFFFFFFFull;
    GRef<u64>(p + 0x20) = x | static_cast<u64>(y) << 16;
    GRef<u64>(p + 0x38) = xr | yhi;
    GRef<u64>(p + 0x40) = xr | ylo;
    return p + 0x50;
}

// EE: 0x00237A98
// Unit icons on the map: for each unit of the mission's list (20-byte entries: x, y, y2, z, type,
// faction) within the view (|x - 4096 - cam x| and |4096 - z - cam z| <= scale / 2), the point
// (-x' * 16384 / scale, (y * 3.5 + k) * 10240 / scale, z' * 16384 / scale) is projected with the
// map camera (VU0); visible points (screen 2048 +- 1920) get the icon, and type 4 units a height
// link to y2. Works in the scratchpad as the original.
void DrawBriefingUnits(u32 list, s32 index, u32 obj) {
    GRef<f32>(kSpr + 0x70) = GRef<f32>(obj + 0x450);
    GRef<f32>(kSpr + 0x74) = GRef<f32>(obj + 0x454);
    const u32 units = g_BriefingUnitLists() + GRef<u32>(obj + 0x560) * 8;
    s32 count = GRef<s32>(units);
    u32 e = GRef<u32>(units + 4);
    const f32 scale = GRef<f32>(obj + 0x458);
    GRef<f32>(kSpr + 0x78) = scale;
    const f32 half = scale * 0.5f;
    const f32 ky = 10240.0f / scale;
    const f32 kxz = 16384.0f / scale;
    GRef<f32>(kSpr + 0x7C) = half;
    const vu0::Vec4 m[4] = {LoadVec(obj + 0x330), LoadVec(obj + 0x340), LoadVec(obj + 0x350), LoadVec(obj + 0x360)};
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildFrameDrawEnv(start + 0x10);
    GRef<u64>(p) = 0x1000000000008002ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0x8000000068ull;
    GRef<u64>(p + 0x18) = 0x43;
    GRef<u64>(p + 0x20) = 0x20060802A930B000ull;
    GRef<u64>(p + 0x28) = 7;
    p += 0x30;
    for (; count > 0; --count, e += 0x14) {
        const f32 ux = GRef<f32>(e);
        GRef<f32>(kSpr) = ux;
        const f32 uy = GRef<f32>(e + 4);
        GRef<f32>(kSpr + 4) = uy;
        const f32 dx = fpu::Sub(fpu::Sub(ux, 4096.0f), GRef<f32>(kSpr + 0x70));
        const f32 uz = GRef<f32>(e + 0xC);
        GRef<f32>(kSpr + 8) = uz;
        const f32 dz = fpu::Sub(fpu::Add(-uz, 4096.0f), GRef<f32>(kSpr + 0x74));
        if (half < std::fabs(dx) || half < std::fabs(dz)) continue;
        const u32 type = GRef<u8>(e + 0x10);
        if (type >= 5) continue;
        const u32 faction = GRef<u8>(e + 0x11);
        if (faction >= 5) continue;
        const f32 k = K(0x004102C0);
        GRef<f32>(kSpr + 8) = kxz * dz;
        GRef<f32>(kSpr + 0xC) = 1.0f;
        GRef<f32>(kSpr) = -kxz * dx;
        GRef<f32>(kSpr + 4) = ky * fpu::Add(uy * 3.5f, k);
        // After each projection (qmfc2 + psrlw/ppach/ppacw): x and y must fit 0..0xFFFF and w must
        // not be negative; then the float position must lie within 2048 +- 1920.
        s32 ints[4];
        vu0::Vec4 r = ProjectPoint(m, LoadVec(kSpr), ints);
        if (((static_cast<u32>(ints[0]) >> 16) | (static_cast<u32>(ints[1]) >> 16)) != 0 || ints[3] < 0) continue;
        StoreSpr(0x10, r);
        if (1920.0f < std::fabs(fpu::Sub(GRef<f32>(kSpr + 0x10), 2048.0f))) continue;
        if (1920.0f < std::fabs(fpu::Sub(GRef<f32>(kSpr + 0x14), 2048.0f))) continue;
        GRef<u64>(kSpr + 0x80) = (static_cast<u64>(ints[0]) & 0xFFFF) | (static_cast<u64>(ints[1]) & 0xFFFF) << 16;
        p = WriteBriefingUnitIcon(p, kSpr + 0x80, static_cast<s32>(type), static_cast<s32>(faction));
        if (type != 4) continue;
        GRef<f32>(kSpr + 4) = ky * fpu::Add(GRef<f32>(e + 8) * 3.5f, k);
        r = ProjectPoint(m, LoadVec(kSpr), ints);
        if (((static_cast<u32>(ints[0]) >> 16) | (static_cast<u32>(ints[1]) >> 16)) != 0 || ints[3] < 0) continue;
        StoreSpr(0x10, r);
        if (1920.0f < std::fabs(fpu::Sub(GRef<f32>(kSpr + 0x10), 2048.0f))) continue;
        if (1920.0f < std::fabs(fpu::Sub(GRef<f32>(kSpr + 0x14), 2048.0f))) continue;
        GRef<u64>(kSpr + 0x88) = (static_cast<u64>(ints[0]) & 0xFFFF) | (static_cast<u64>(ints[1]) & 0xFFFF) << 16;
        p = WriteBriefingUnitLink(p, kSpr + 0x80, kSpr + 0x88, static_cast<s32>(faction));
    }
    LinkPacket(list, index, start, CloseDmaPacket(start, p));
}

// EE: 0x00237EF8
// Map frame image (DATA item +0x238) and the view rectangle on it: the camera's x/z and scale mapped
// to the frame (clamped to x 0x180-0x980, y 0xD10-0x1490) as a line strip.
void DrawBriefingMapFrame(u32 list, s32 index, u32 obj) {
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, GRef<u32>(obj + 0x238));
    const u32 p = BuildFrameDrawEnv(tag + 0x10);
    const f32 cz = GRef<f32>(obj + 0x454);
    const f32 scale = GRef<f32>(obj + 0x458);
    const f32 cx = GRef<f32>(obj + 0x450);
    GRef<u64>(p) = 0x11AB400000000002ull;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 8) = 0xE;
    const s32 hz = fpu::FloatToInt(scale * 0.0018310547f);
    GRef<u64>(p + 0x28) = 0x43;
    GRef<u64>(p + 0x10) = 0;
    const s32 hx = fpu::FloatToInt(scale * 0.001953125f);
    const s32 zc = fpu::FloatToInt(cz * 0.0036621094f) + 0x10D0;
    const s64 y1 = zc + hz;
    const s64 y0 = zc - hz;
    const s32 xc = fpu::FloatToInt(cx * 0.00390625f) + 0x580;
    GRef<u64>(p + 0x20) = 0x8000000068ull;
    const s64 x1 = xc + hx;
    const s64 x0 = xc - hx;
    const s64 cy0 = 0xD0F < y0 ? y0 : 0xD10;
    GRef<u64>(p + 0x30) = 0x6400000000008001ull;
    GRef<u64>(p + 0x38) = 0x53D317;
    const s64 cy1 = y1 < 0x1491 ? y1 : 0x1490;
    GRef<u64>(p + 0x40) = 0x2005E002A930AD00ull;
    GRef<u64>(p + 0x68) = 0x14900980;
    GRef<u64>(p + 0x48) = 0xD1C1A9;
    GRef<u64>(p + 0x50) = 0x80008;
    const s64 cx0 = 0x17F < x0 ? x0 : 0x180;
    GRef<u64>(p + 0x58) = 0xD200190;
    GRef<u64>(p + 0x60) = 0x7880808;
    GRef<u64>(p + 0x78) = 0x5555D10;
    const s64 cx1 = x1 < 0x981 ? x1 : 0x980;
    GRef<u64>(p + 0x70) = 0x7400000000008001ull;
    GRef<u64>(p + 0x80) = 0x24A;
    const u64 a = Sx(cx0) | Sx(cy0) << 16;
    GRef<u64>(p + 0x88) = 0x1067;
    GRef<u64>(p + 0xA8) = Sx(cx0) | Sx(cy1) << 16;
    GRef<u64>(p + 0x90) = a;
    GRef<u64>(p + 0x98) = Sx(cx1) | Sx(cy0) << 16;
    GRef<u64>(p + 0xA0) = Sx(cx1) | Sx(cy1) << 16;
    GRef<u64>(p + 0xB0) = a;
    LinkPacket(list, index, start, CloseDmaPacket(tag, p + 0xC0));
}

// EE: 0x00238148
// Draws the map view: terrain, unit icons (from state 4), map fade, panel, info texts and the map
// frame (by panel levels), the message box (before state 5), black fade and the menu fade.
void DrawBriefingMapPage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(g_pMainContext().addr + offsetof(MainContext, vif1_list), obj);
    DrawTerrainMap(obj, 0, obj + 0x250, GRef<u32>(obj + 0x238));
    if (GRef<u32>(obj + 0x554) >= 4) DrawBriefingUnits(obj, 0, obj);
    const f32 k = K(0x004102C4);
    DrawBlackFade(GRef<f32>(obj + 0x48C), obj, 0);
    DrawPanel(obj, 0, obj + 0x4CC);
    if (k < GRef<f32>(obj + 0x4E0)) DrawBriefingMapTexts(obj, 0, obj);
    if (k < GRef<f32>(obj + 0x4E8)) DrawBriefingMapFrame(obj, 0, obj);
    if (GRef<u32>(obj + 0x554) < 5) {
        UploadMessageBoxGlyphs(obj, 0, obj + 0x530);
        DrawMessageBox(obj, 0, obj + 0x530);
    }
    DrawBlackFade(GRef<f32>(obj + 0x47C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x00238278
// Subs 17/18: creates the page, sets the camera preset and unit list tables, queues DATA 0x111 +
// mission (3 entries), movie page display; then sub 19.
void EnterBriefing() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    ResetMenuBgm();
    const u32 obj = CreateBriefing();
    g_BriefingCameraPresets() = 0x0039EB88;
    g_BriefingUnitLists() = 0x003A5150;
    g_Briefing() = obj;
    ResetMenuDataLoad(obj + 0x20);
    QueueMenuDataLoad(obj + 0x20, static_cast<u16>(GRef<s32>(obj + 0x560) + 0x111), 3, obj + 0x230);
    SetMoviePageDisplayThunk();
    RequestSubState(0x13);
}

// EE: 0x00238310
// Sub 19: when loaded, the font block, the first subtitle, the header panel and the movie; sub 20.
void LoadBriefing() {
    const u32 obj = g_Briefing();
    if (UpdateMenuDataLoad(obj + 0x20) >= 0) return;
    GRef<s32>(obj + 0x588) = LoadFontBlock(GRef<u32>(obj + 0x230));
    SetBriefingSubtitle(obj + 0x530, GRef<u32>(obj + 0x570) + GRef<u32>(obj + 0x568) * 8);
    StartBriefingMovie(obj);
    RequestSubState(0x14);
}

// EE: 0x00238380
// Sub 20: one movie iteration with the briefing on top; at the movie's end sub 21 (stream slots
// fade out, frames reset).
void RunBriefingMovie() {
    debug::OnGuestCheckpoint(0x00238380);
    const u32 obj = g_Briefing();
    ResetMovieTimerThunk();
    UpdateFade(obj + 0x470);
    BriefingUpdate(obj);
    if (UpdateMenuMovie() != 0) {
        RequestSubState(0x15);
        FadeOutStreamSlots(K(0x004102C8));
        GRef<u32>(obj + 0x564) = 0;
    }
    DrawBriefingMoviePage(obj);
    MovieFrameSync();
}

// EE: 0x002383E8
// Sub 21: when the movie has ended, back to the menu display, the movie memory released, the map
// prepared; sub 22.
void EndBriefingMovie() {
    const u32 obj = g_Briefing();
    if (!StepMovieToEnd()) return;
    SetMenuDisplayAfterMovieThunk();
    PopWorkHeapMark();
    InitBriefingMap(obj);
    RequestSubState(0x16);
}

// EE: 0x00238440
// Sub 22: map view update and draw.
void RunBriefingMap() {
    debug::OnGuestCheckpoint(0x00238440);
    const u32 obj = g_Briefing();
    UpdateFade(obj + 0x470);
    UpdateFade(obj + 0x480);
    BriefingMapUpdate(obj);
    DrawBriefingMapPage(obj);
}

// EE: 0x0023EBA8
// Terrain map camera (a 2D camera with a 640 x 480 projection, 512 distance; width k when the screen
// ratio is 16:9) looking from y -51200 at distance 1024; heights at data + 0x70 (256 x 256 bytes),
// colours at data + 0x100D0 (256 entries); scale 524288, position 0.
void InitTerrainMap(u32 map, u32 data) {
    f32 w = 640.0f;
    if ((Ctx()->flags & 0x10000) != 0) w = K(0x00410340);
    Init2dCamera(map);
    SetCameraProjection(w, 480.0f, 512.0f, 1.0f, K(0x00410344), map);
    SetCameraViewport(1.0f, K(0x00410348), map, 0, 0, static_cast<s16>(Ctx()->draw_w), static_cast<s16>(Ctx()->draw_h));
    ResetCameraLookAt(map + 0x1C0);
    GRef<f32>(map + 0x1C4) = -51200.0f;
    GRef<f32>(map + 0x1C8) = 10.0f;
    GRef<f32>(map + 0x1E0) = 1024.0f;
    GRef<u32>(map + 0x1C0) = 0;
    GRef<u32>(map + 0x1D0) = 0;
    GRef<u32>(map + 0x1D4) = 0;
    GRef<u32>(map + 0x1D8) = 0;
    GRef<u32>(map + 0x1E8) = 0;
    GRef<u32>(map + 0x1EC) = 0;
    GRef<f32>(map + 0x1F0) = 0.0f;
    GRef<f32>(map + 0x1F4) = 0.0f;
    GRef<f32>(map + 0x1F8) = 0.0f;
    GRef<f32>(map + 0x1FC) = 1.0f;
    GRef<f32>(map + 0x208) = 524288.0f;
    GRef<u32>(map + 0x214) = data + 0x100D0;
    GRef<u32>(map + 0x210) = data + 0x70;
    GRef<u32>(map + 0x200) = 0;
    GRef<u32>(map + 0x204) = 0;
}

// EE: 0x0023EE20
// Terrain as 128 x 128 point sprites (80 x 80 in 1/16 pixel) over the view (scale around x/z):
// - the colour table copied to the scratchpad (0x70, entries with bits 3 and 4 swapped: CSM1 order);
// - per row: the two height rows around it (heights at +0x210, 256 per row) interpolated by the
//   row's 12-bit fraction (MMI: h0 * 4096 + (h1 - h0) * f, clamped 0..0xFF000) into the scratchpad
//   (0x570);
// - per column: the height interpolated again, the point (col - 64, h - 0x80000, row - 64) scaled
//   by (-128, -320 / scale, 128) and projected with the camera (VU0); points inside the screen
//   window get a sprite coloured by height ((h + 0x800) >> 12).
// REGLIST tags of up to 0x7FFE sprites (RGBAQ UV XYZ3 UV XYZ2). Returns the end of the data.
u32 WriteTerrainPoints(u32 p, u32 map) {
    const f32 scale = GRef<f32>(map + 0x208);
    const u32 colours = GRef<u32>(map + 0x214);
    const u32 heights = GRef<u32>(map + 0x210);
    const f32 px = GRef<f32>(map + 0x200);
    f32 pz = GRef<f32>(map + 0x204);
    for (u32 t = 0; t < 0x100; ++t) {
        const u32 i = (t & 0xE7) | ((t & 0x10) >> 1) | ((t & 8) << 1);
        GRef<u32>(kSpr + 0x70 + 4 * t) = GRef<u32>(colours + 4 * i);
    }
    GRef<u64>(p) = 0x2400000000008001ull;
    GRef<u64>(p + 0x10) = 0x356;
    GRef<u64>(p + 8) = 0x70;
    GRef<u64>(p + 0x18) = 0x20060802A930B000ull;
    u32 tag = p + 0x20;
    u32 out = tag + 0x10;
    s32 count = 0;
    StoreSpr(0x20, {0.0f, 0.0f, 0.0f, 1.0f});
    const f32 sy = -320.0f / scale;
    GRef<f32>(kSpr + 0x20) = -128.0f;
    GRef<f32>(kSpr + 0x28) = 128.0f;
    GRef<f32>(kSpr + 0x24) = sy;
    const vu0::Vec4 m[4] = {LoadVec(map + 0xE0), LoadVec(map + 0xF0), LoadVec(map + 0x100), LoadVec(map + 0x110)};
    const vu0::Vec4 k = LoadVec(kSpr + 0x20);
    GRef<s32>(kSpr + 0x1C) = 1;
    pz = fpu::Add(pz, 262144.0f);
    const f32 half = scale * 0.5f;
    f32 x_start = fpu::Add(px, 262144.0f);
    const f32 step = scale * 0.0078125f;
    pz = fpu::Sub(pz, half);
    x_start = fpu::Sub(x_start, half);
    f32 pz2 = fpu::Add(pz, pz);
    for (s32 row = 0; row < 0x80; ++row) {
        // The original recomputes pz + pz in the delay slot of the loop branch.
        if (row != 0) pz2 = fpu::Add(pz, pz);
        const s32 a = fpu::FloatToInt(pz2);
        if ((a >> 20) != 0) {
            pz = fpu::Add(pz, step);
            continue;
        }
        const s32 r0 = a >> 12;
        const u32 row0 = heights + static_cast<u32>(r0) * 0x100;
        const u32 row1 = ((r0 + 1) >> 8) == 0 ? heights + static_cast<u32>(r0 + 1) * 0x100 : row0;
        pz = fpu::Add(pz, step);
        const s32 f = static_cast<s16>(a & 0xFFF);
        for (u32 i = 0; i < 0x100; ++i) {
            const s32 h0 = GRef<u8>(row0 + i);
            const s32 h1 = GRef<u8>(row1 + i);
            const s32 d = static_cast<s16>(h1 - h0);
            s32 v = h0 * 0x1000 + d * f;
            if (v < 0) v = 0;
            if (v > 0xFF000) v = 0xFF000;
            GRef<s32>(kSpr + 0x570 + 4 * i) = v;
        }
        GRef<s32>(kSpr + 0x18) = row - 0x40;
        f32 x = x_start;
        for (s32 col = 0; col < 0x80; ++col, x = fpu::Add(x, step)) {
            const s32 b = fpu::FloatToInt(fpu::Add(x, x));
            if ((b >> 20) != 0) continue;
            const s32 c0 = b >> 12;
            const s32 ha = GRef<s32>(kSpr + 0x570 + static_cast<u32>(c0) * 4);
            const s32 hb = ((c0 + 1) >> 8) == 0 ? GRef<s32>(kSpr + 0x570 + static_cast<u32>(c0 + 1) * 4) : ha;
            const s32 h = ha + (static_cast<s32>(static_cast<u32>(hb - ha) * static_cast<u32>(b & 0xFFF)) >> 12);
            GRef<s32>(kSpr + 0x10) = col - 0x40;
            GRef<s32>(kSpr + 0x14) = static_cast<s32>(static_cast<u32>(h) + 0xFFF80000u);
            const vu0::Vec4 iv{vu0::IntToFloat(GRef<s32>(kSpr + 0x10)), vu0::IntToFloat(GRef<s32>(kSpr + 0x14)),
                               vu0::IntToFloat(GRef<s32>(kSpr + 0x18)), vu0::IntToFloat(GRef<s32>(kSpr + 0x1C))};
            const vu0::Vec4 v{vu0::Mul(iv.x, k.x), vu0::Mul(iv.y, k.y), vu0::Mul(iv.z, k.z), vu0::Mul(iv.w, k.w)};
            s32 ints[4];
            ProjectPoint(m, v, ints);
            for (u32 j = 0; j < 4; ++j) GRef<s32>(kSpr + 4 * j) = ints[j];
            const u32 sx = static_cast<u32>(ints[0]);
            if (sx - 0x6C00u >= 0x2829u) continue;
            const u32 sy2 = static_cast<u32>(ints[1]);
            if (sy2 - 0x7200u >= 0x1C29u) continue;
            const u32 colour = GRef<u32>(kSpr + 0x70 + ((static_cast<u32>(h + 0x800) >> 10) & 0x3FC));
            GRef<u64>(out) = colour;
            GRef<u64>(out + 0x10) = Sx(static_cast<s32>(sx - 0x28)) | Sx(static_cast<s32>(sy2 - 0x28)) << 16;
            GRef<u64>(out + 0x20) = Sx(static_cast<s32>(sx + 0x28)) | Sx(static_cast<s32>(sy2 + 0x28)) << 16;
            GRef<u64>(out + 8) = 0x80008;
            GRef<u64>(out + 0x18) = 0x1080108;
            out += 0x28;
            if (++count >= 0x7FFE) {
                GRef<u64>(tag + 8) = 0x53D31;
                count = 0;
                GRef<u64>(tag) = 0x540000000000FFFEull;
                tag = out;
                out = tag + 0x10;
            }
        }
    }
    if (count <= 0) return tag;
    GRef<u64>(tag + 8) = 0x53D31;
    GRef<u64>(tag) = static_cast<u64>(static_cast<u32>(count) | 0x8000u) | 0x5400000000000000ull;
    return (count & 1) != 0 ? out + 8 : out;
}

// EE: 0x0023F260
// The terrain map: a DMA call to the map image packet `image`, the frame draw environment, TEST
// 0x48, then the terrain points.
void DrawTerrainMap(u32 list, s32 index, u32 map, u32 image) {
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, image);
    const u32 p = BuildFrameDrawEnv(tag + 0x10);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0x48;
    GRef<u64>(p) = 0x1000000000008001ull;
    GRef<u64>(p + 0x18) = 0x43;
    LinkPacket(list, index, start, CloseDmaPacket(tag, WriteTerrainPoints(p + 0x20, map)));
}

// EE: 0x0023F320
// Map camera from its look-at record, then its matrices.
void UpdateTerrainMapCamera(u32 map) {
    SetCameraLookAt(map, map + 0x1C0);
    UpdateCameraMatrices(map);
}

AC04_GUEST_FUNCTION(0x00238278, EnterBriefing);
AC04_GUEST_FUNCTION(0x00238310, LoadBriefing);
AC04_GUEST_FUNCTION(0x00238380, RunBriefingMovie);
AC04_GUEST_FUNCTION(0x002383E8, EndBriefingMovie);
AC04_GUEST_FUNCTION(0x00238440, RunBriefingMap);

}  // namespace ac04::game
