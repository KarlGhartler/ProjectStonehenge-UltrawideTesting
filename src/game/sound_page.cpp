// Sound settings page "SOUND" (scene 4 subs 0x3A-0x3D; docs/subsystems/frontend.md) and the BGM
// player (extras, subs 0x31-0x33).
//
// Sound page object (0x330 bytes, *0x00411840): +0x10 data load queue, +0x220 DATA 0xB6 {font
// block}, +0x230 fade object, +0x23C fade level, +0x240 working copy of the 8-byte sound options
// 0x002A15C4 (+0x240 flags, bit 0 mono; +0x244 s8 levels[4]), +0x248 crosshair, +0x284 message
// box, +0x2A8 state, +0x2AC/+0x2AE MenuGoTo target, +0x2B0 panel (+0x2FC/+0x300/+0x304 timers
// a/b/c, +0x308/+0x30A crosshair start, +0x310 crosshair), +0x314 row (0 output, 1-4 levels),
// +0x318 s32 values[5] (output: 0 mono, 1 stereo; then the four levels 0-15).
//
// BGM player object (0x330 bytes, *0x00411844): +0x10 load queue, +0x220 DATA 0xB8 {font block},
// +0x230 fade object, +0x23C fade level, +0x240 crosshair, +0x27C panel (+0x2C8/+0x2CC/+0x2D0
// timers a/b/c, +0x2D4/+0x2D6 crosshair start, +0x2DC crosshair), +0x2E0 message box, +0x304
// state, +0x308/+0x30A MenuGoTo target, +0x30C column (0-17), +0x310 line (0-1), +0x314 cursor
// track, +0x318 playing, +0x31C playing track, +0x320 frames played, +0x324 track length.
// Track table 0x003AF6A0: 36 x {name, length in frames, menu BGM track}.
#include "game/game.h"

#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u32, g_SoundPage, 0x00411840)      // gp-0x41B0
AC04_GUEST_VAR(u32, g_BgmPlayerPage, 0x00411844)  // gp-0x41AC
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kSoundOptions = 0x002A15C4;
constexpr u32 kBgmTracks = 0x003AF6A0;

s32& SoundState(u32 obj) { return GRef<s32>(obj + 0x2A8); }
s32& SoundValue(u32 obj, u32 i) { return GRef<s32>(obj + 0x318 + i * 4); }
s32& BgmState(u32 obj) { return GRef<s32>(obj + 0x304); }

// The 8-byte options are copied with ldl/ldr + sdl/sdr (0x002A15C4 is only 4-byte aligned).
void CopyOptions(u32 dst, u32 src) {
    GRef<u32>(dst) = GRef<u32>(src);
    GRef<u32>(dst + 4) = GRef<u32>(src + 4);
}

void MoveSoundCrosshair(u32 obj) {
    MoveCrosshairAuto(GRef<u32>(obj + 0x310), GRef<s16>(obj + 0x308), GRef<s16>(obj + 0x30A));
}

}  // namespace

// EE: 0x00243800
// Help line of the sound page (message per row, table 0x003AF670).
void UpdateSoundHelp(u32 obj) {
    s32 state = SoundState(obj);
    if (state == 1) {
        u32 row = GRef<u32>(obj + 0x314);
        if (4 < row) row = 0;
        const s16 id = GRef<s16>(0x003AF670 + row * 2);
        if (GRef<s16>(obj + 0x292) != id) {
            SetMessageBoxText(obj + 0x284, id);
            state = SoundState(obj);
            GRef<f32>(obj + 0x28C) = K(0x004103B8);
        }
    }
    if (state != 0) UpdateMessageBox(K(0x004103BC), obj + 0x284, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00243890
// From the main menu (sub 0x3A) back to page 4, otherwise to sub-page 0x18.
u32 CreateSoundPage() {
    const u32 obj = WorkHeapAlloc(0x330, 0x10);
    const u32 fade = g_pMenuFade();
    SoundState(obj) = 0;
    GRef<u16>(fade + 0x32) = 0;
    GRef<u16>(fade + 0x30) = 1;
    InitFade(obj + 0x230, 1);
    GRef<u16>(obj + 0x2AC) = 1;
    if (Ctx()->sub == 0x3A) {
        GRef<u16>(obj + 0x2AE) = 4;
        PlayMenuBgm(4);
    } else {
        GRef<u16>(obj + 0x2AE) = 0x18;
        PlayMenuBgm(static_cast<s16>(GetProgressBgmTrack(1, g_MissionNo())));
    }
    InitPanel(obj + 0x2B0, 3, 0x003AF640, obj + 0x248);
    GRef<u16>(obj + 0x2B2) |= 0xE;
    CopyOptions(obj + 0x240, kSoundOptions);
    GRef<s32>(obj + 0x314) = 0;
    SoundValue(obj, 0) = (GRef<u32>(obj + 0x240) & 1) == 0 ? 1 : 0;
    for (u32 i = 0; i < 4; ++i) SoundValue(obj, i + 1) = GRef<s8>(obj + 0x244 + i);
    ApplySoundPageValues(obj);
    MoveSoundCrosshair(obj);
    return obj;
}

// EE: 0x002439D8
// Cancel restores the saved options, accept stores the working copy; Select (0x4000) resets the
// level of the row to 15; up/down choose the row, left/right toggle the output or change the
// level (each change is applied at once).
void SoundUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    const MainContext* ctx = Ctx();
    const u32 edge = ctx->pad_buttons[0][1];
    const u32 repeat = ctx->pad_buttons[0][2];
    GRef<u16>(fade + 0x32) = 0;
    if (SoundState(obj) != 0) UpdatePanel(obj + 0x2B0);
    UpdateSoundHelp(obj);
    const s32 state = SoundState(obj);
    if (state == 0) {
        if (K(0x004103C0) < GRef<f32>(fade + 0xC)) SoundState(obj) = 1;
        return;
    }
    if (state == 2 || state == 4) {
        if ((GRef<u32>(obj + 0x230) & 8) == 0 && (GRef<u16>(obj + 0x2B0) & 8) == 0) return;
        MenuGoTo(GRef<s16>(obj + 0x2AC), GRef<u16>(obj + 0x2AE));
        return;
    }
    if (state != 1) return;
    GRef<u16>(fade + 0x32) = 0x30;
    if ((edge & 0x20) != 0) {
        PlayMenuSe(0, 3);
        ApplySoundOptions(kSoundOptions);
        SetPanelState(obj + 0x2B0, 1);
        StartFade(0.25f, obj + 0x230, 1);
        SoundState(obj) = 4;
        return;
    }
    if ((edge & 0x50) != 0) {
        PlayMenuSe(0, 2);
        CopyOptions(kSoundOptions, obj + 0x240);
        ApplySoundOptions(kSoundOptions);
        SetPanelState(obj + 0x2B0, 1);
        StartFade(0.25f, obj + 0x230, 1);
        SoundState(obj) = 2;
        return;
    }
    if (0 < GRef<s32>(obj + 0x314) && (edge & 0x4000) != 0) {
        SoundValue(obj, GRef<u32>(obj + 0x314)) = 0xF;
        ApplySoundPageValues(obj);
        MoveSoundCrosshair(obj);
        PlayMenuSe(0, 3);
    }
    if ((GRef<u16>(obj + 0x2B0) & 0x800) == 0) return;
    s32 row = GRef<s32>(obj + 0x314);
    s32 v = SoundValue(obj, static_cast<u32>(row));
    const u16 hints = static_cast<u16>(GRef<u16>(fade + 0x32) | 3);
    GRef<u16>(fade + 0x32) = hints;
    if (row == 0) {
        GRef<u16>(fade + 0x32) = static_cast<u16>(hints | 0xC);
    } else {
        if (0 < v) GRef<u16>(fade + 0x32) = static_cast<u16>(hints | 4);
        if (v < 0xF) GRef<u16>(fade + 0x32) |= 8;
    }
    if ((repeat & 0xF) == 0) return;
    if ((repeat & 0xC) != 0) {
        if (row == 0) {
            GRef<u32>(obj + 0x300) = 0;
            SoundValue(obj, 0) = (v + 1) & 1;
            GRef<u16>(obj + 0x2B0) |= 0x200;
        } else {
            if ((repeat & 4) != 0) v -= 1;
            if ((repeat & 8) != 0) v += 1;
            if (v < 0) {
                v = 0;
            } else if (0xF < v) {
                v = 0xF;
            }
            SoundValue(obj, static_cast<u32>(row)) = v;
        }
    }
    row -= static_cast<s32>(repeat & 1);
    if ((repeat & 2) != 0) row += 1;
    GRef<s32>(obj + 0x314) = (row + 5) % 5;
    ApplySoundPageValues(obj);
    MoveSoundCrosshair(obj);
    PlayMenuSe(0, 4);
}

// EE: 0x00243D18
// Crosshair start on the value of the row; the values go into the working copy, which is applied
// at once.
void ApplySoundPageValues(u32 obj) {
    const s32 row = GRef<s32>(obj + 0x314);
    const s32 v = SoundValue(obj, static_cast<u32>(row));
    GRef<s16>(obj + 0x30A) = static_cast<s16>(row * 0x1C0 + 0x610);
    GRef<s16>(obj + 0x308) = static_cast<s16>(v * 0x140 + 0xC00);
    if (SoundValue(obj, 0) == 0) {
        GRef<u32>(obj + 0x240) |= 1;
    } else {
        GRef<u32>(obj + 0x240) &= 0xFFFFFFFEu;
    }
    for (u32 i = 0; i < 4; ++i) GRef<u8>(obj + 0x244 + i) = static_cast<u8>(SoundValue(obj, i + 1));
    ApplySoundOptions(obj + 0x240);
}

// EE: 0x00243DB8
// Texts: row labels and levels (timer a), the output mode (timer b, restarted on a change).
void DrawSoundTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u16 parts = GRef<u16>(obj + 0x2B0);
    BeginMenuTextLayer(list, index);
    const f32 a = GRef<f32>(obj + 0x2FC);
    if (0.0f < a) {
        const f32 t = a * 90.0f;
        DrawMenuText(t, 0x240, 0x510, 4, 0x00411430);  // "|"
        f32 r = t;
        s32 y = 0x510;
        for (u32 k = 0; k < 5; ++k, y += 0x1C0, r = fpu::Sub(r, 8.0f)) {
            DrawMenuText(r, 0x300, y, 4, GRef<u32>(0x003AF680 + k * 4));
        }
        r = fpu::Sub(t, 12.0f);
        y = 0x790;
        for (u32 k = 0; k < 4; ++k, y += 0x1C0, r = fpu::Sub(r, 8.0f)) {
            const u64 args[1] = {static_cast<u64>(GRef<u32>(obj + 0x31C + k * 4))};
            ee::sprintf(buf, 0x00409900, args, 1);  // "LEVEL %02d"
            DrawMenuText(r, 0x300, y, 3, buf);
        }
        const f32 left = fpu::Sub(t, 60.0f);
        if (left < 0.0f) GRef<u16>(obj + 0x2B0) |= 0x8000;
        if (4.0f < left && (parts & 0x100) != 0) GRef<u16>(obj + 0x2B0) &= 0xFEFF;
    }
    const f32 b = GRef<f32>(obj + 0x300);
    if (0.0f < b) {
        const f32 t = b * 60.0f;
        const u32 text = GRef<u32>(SoundValue(obj, 0) == 0 ? 0x003AF694 : 0x003AF698);  // MONO / STEREO
        DrawMenuText(fpu::Sub(t, 4.0f), 0x300, 0x5D0, 3, text);
        const f32 left = fpu::Sub(t, 11.0f);
        if (left < 0.0f) GRef<u16>(obj + 0x2B0) |= 0x8000;
        if (4.0f < left && (parts & 0x200) != 0) GRef<u16>(obj + 0x2B0) &= 0xFDFF;
    }
    EndMenuTextLayer();
}

// EE: 0x00244018
// Level marks (timer c): 2 for the output row, 16 per level row; then the pulsing boxes on the
// values (on the cursor row only while the crosshair is idle).
void DrawSoundMarks(u32 list, s32 index, u32 obj) {
    const u16 parts = GRef<u16>(obj + 0x2B0);
    BeginMenuTextLayer(list, index);
    const f32 c = GRef<f32>(obj + 0x304);
    if (0.0f < c) {
        const f32 t = c * 90.0f;
        f32 r = fpu::Sub(t, 0.0f);
        s32 y = 0x5A0;
        for (s32 row = 0; row < 5; ++row, y += 0x1C0, r = fpu::Sub(r, 8.0f)) {
            const s32 n = row == 0 ? 2 : 0x10;
            f32 m = r;
            s32 x = 0xB80;
            for (s32 k = 0; k < n; ++k, x += 0x140, m = fpu::Sub(m, 1.0f)) {
                DrawMenuText(m, x, y, 8, 0x00411438);
            }
        }
        const f32 left = fpu::Sub(t, 49.0f);
        if (left < 0.0f) GRef<u16>(obj + 0x2B0) |= 0x8000;
        if (4.0f < left && (parts & 0x400) != 0) {
            GRef<u16>(obj + 0x2B0) = static_cast<u16>((GRef<u16>(obj + 0x2B0) & 0xFBFF) | 0x800);
        }
    }
    EndMenuTextLayer();
    if ((GRef<u16>(obj + 0x2B0) & 0x800) == 0) return;
    s32 y = 0x5A0;
    for (s32 row = 0; row < 5; ++row, y += 0x1C0) {
        s32 frame_no = 0;
        if (GRef<s32>(obj + 0x314) == row) {
            if (GRef<u16>(obj + 0x248) != 0) continue;
            frame_no = static_cast<s32>(Ctx()->frame_count);
        }
        DrawPulseBox(list, index, 0x1067, SoundValue(obj, static_cast<u32>(row)) * 0x140 + 0xB80, y, 0x100, frame_no);
    }
}

// EE: 0x00244248
void DrawSoundPage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), obj);
    DrawPanel(obj, 0, obj + 0x2B0);
    const u16 shown = GRef<u16>(obj + 0x2B2);
    GRef<u16>(obj + 0x2B0) &= 0x7FFF;
    if ((shown & 4) != 0) DrawSoundTexts(obj, 0, obj);
    if ((GRef<u16>(obj + 0x2B2) & 8) != 0) DrawSoundMarks(obj, 0, obj);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x284);
    DrawMessageBox(obj, 0, obj + 0x284);
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x00244320
// Subs 0x3A/0x3B: creates the page and queues DATA 0xB6.
void EnterSoundPage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    MainContext* ctx = Ctx();
    ctx->draw_mode = 2;
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    ctx->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateSoundPage();
    g_SoundPage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB6, 1, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x3C);
}

// EE: 0x002443C0
void LoadSoundPage() {
    const u32 obj = g_SoundPage();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x284);
        RequestSubState(0x3D);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x00244410
void RunSoundPage() {
    const u32 obj = g_SoundPage();
    debug::OnGuestCheckpoint(0x00244410);
    UpdateFade(obj + 0x230);
    SoundUpdate(obj);
    DrawSoundPage(obj);
}

// EE: 0x00244448
// Help line of the BGM player (message 0).
void UpdateBgmPlayerHelp(u32 obj) {
    s32 state = BgmState(obj);
    if (state == 1 && GRef<s16>(obj + 0x2EE) != 0) {
        SetMessageBoxText(obj + 0x2E0, 0);
        state = BgmState(obj);
        GRef<f32>(obj + 0x2E8) = K(0x004103C4);
    }
    if (state != 0) UpdateMessageBox(K(0x004103C8), obj + 0x2E0, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x002444B8
// Crosshair on column/line; cursor track = line * 18 + column.
void SetBgmPlayerCursor(u32 obj) {
    const s32 col = GRef<s32>(obj + 0x30C);
    const s32 line = GRef<s32>(obj + 0x310);
    const s32 x = col * 0x140 + 0x960;
    const s32 y = line * 0x130 + 0xBC0;
    GRef<s16>(obj + 0x2D4) = static_cast<s16>(x);
    GRef<s32>(obj + 0x314) = line * 0x12 + col;
    GRef<s16>(obj + 0x2D6) = static_cast<s16>(y);
    MoveCrosshairAuto(GRef<u32>(obj + 0x2DC), x, y);
}

// EE: 0x00244508
// Always returns to page 4.
u32 CreateBgmPlayer() {
    const u32 obj = WorkHeapAlloc(0x330, 0x10);
    const u32 fade = g_pMenuFade();
    BgmState(obj) = 0;
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    InitFade(obj + 0x230, 1);
    GRef<u16>(obj + 0x308) = 1;
    GRef<u16>(obj + 0x30A) = 4;
    InitPanel(obj + 0x27C, 3, 0x003AF850, obj + 0x240);
    GRef<s32>(obj + 0x30C) = 0;
    GRef<s32>(obj + 0x310) = 0;
    GRef<u16>(obj + 0x27E) |= 0xE;
    SetBgmPlayerCursor(obj);
    GRef<u32>(obj + 0x318) = 0;
    GRef<u32>(obj + 0x31C) = 0;
    GRef<u32>(obj + 0x320) = 0;
    GRef<u32>(obj + 0x324) = 0;
    return obj;
}

// EE: 0x002445B0
// Fades the track out over 6 s once its length has played.
void UpdateBgmPlayerTimer(u32 obj) {
    if (GRef<s32>(obj + 0x318) == 0) return;
    const s32 played = GRef<s32>(obj + 0x320) + 1;
    GRef<s32>(obj + 0x320) = played;
    if (GRef<s32>(obj + 0x324) <= played) {
        GRef<u32>(obj + 0x318) = 0;
        FadeOutMenuBgm(6.0f);
    }
}

// EE: 0x002445F8
// 0x10 plays the cursor track (unless it is already playing), 0x2000 stops (2 s fade); the d-pad
// moves over the 18 x 2 grid. The cancel branch (1 s fade) is unreachable: BgmPlayerUpdate handles
// cancel first and clears panel flag 0x800 (docs/port/dead-code.md section 5).
void BgmPlayerInput(u32 obj) {
    const MainContext* ctx = Ctx();
    const u32 edge = ctx->pad_buttons[0][1];
    const u32 repeat = ctx->pad_buttons[0][2];
    GRef<u16>(g_pMenuFade() + 0x32) |= 0x3F;
    if ((edge & 0x10) != 0) {
        const u32 track = static_cast<u32>(GRef<s32>(obj + 0x310) * 0x12 + GRef<s32>(obj + 0x30C));
        if (track < 0x24 && (GRef<s32>(obj + 0x318) != 1 || GRef<u32>(obj + 0x31C) != track)) {
            GRef<u32>(obj + 0x31C) = track;
            GRef<u32>(obj + 0x318) = 1;
            GRef<u32>(obj + 0x320) = 0;
            GRef<u32>(obj + 0x324) = GRef<u32>(kBgmTracks + track * 0xC + 4);
            PlayMenuBgm(static_cast<s16>(GRef<u32>(kBgmTracks + track * 0xC + 8)));
            return;
        }
    }
    if ((edge & 0x2000) != 0) {
        GRef<u32>(obj + 0x318) = 0;
        FadeOutMenuBgm(2.0f);
        return;
    }
    if ((edge & 0x20) != 0) {
        PlayMenuSe(0, 3);
        StartFade(0.25f, obj + 0x230, 1);
        SetPanelState(obj + 0x27C, 1);
        BgmState(obj) = 4;
        FadeOutMenuBgm(1.0f);
        return;
    }
    if ((repeat & 0xF) == 0) return;
    if ((repeat & 0xC) != 0) {
        s32 col = GRef<s32>(obj + 0x30C);
        if ((repeat & 8) != 0) col += 1;
        if ((repeat & 4) != 0) col -= 1;
        GRef<s32>(obj + 0x30C) = (col + 0x12) % 0x12;
    }
    if ((repeat & 3) != 0) {
        s32 line = GRef<s32>(obj + 0x310) - static_cast<s32>(repeat & 1);
        if ((repeat & 2) != 0) line += 1;
        GRef<s32>(obj + 0x310) = (line + 2) % 2;
    }
    SetBgmPlayerCursor(obj);
    GRef<u32>(obj + 0x2CC) = 0;
    GRef<u16>(obj + 0x27C) |= 0x200;
    PlayMenuSe(0, 4);
}

// EE: 0x002447D0
// Cancel is checked here and in BgmPlayerInput; SetPanelState clears panel flag 0x800 here, so
// BgmPlayerInput is not called in that frame (its cancel branch never runs).
void BgmPlayerUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    const u32 edge = Ctx()->pad_buttons[0][1];
    GRef<u16>(fade + 0x32) = 0;
    if (BgmState(obj) != 0) UpdatePanel(obj + 0x27C);
    UpdateBgmPlayerHelp(obj);
    UpdateBgmPlayerTimer(obj);
    const s32 state = BgmState(obj);
    if (state == 1) {
        GRef<u16>(fade + 0x32) |= 0x20;
        if ((edge & 0x20) != 0) {
            PlayMenuSe(0, 3);
            StartFade(0.25f, obj + 0x230, 1);
            SetPanelState(obj + 0x27C, 1);
            BgmState(obj) = 4;
        }
        if ((GRef<u16>(obj + 0x27C) & 0x800) != 0) BgmPlayerInput(obj);
    } else if (state == 0) {
        if (K(0x004103CC) < GRef<f32>(fade + 0xC)) BgmState(obj) = 1;
    } else if (state == 2 || state == 4) {
        if ((GRef<u32>(obj + 0x230) & 8) == 0 && (GRef<u16>(obj + 0x27C) & 8) == 0) return;
        MenuGoTo(GRef<s16>(obj + 0x308), GRef<u16>(obj + 0x30A));
    }
}

// EE: 0x00244930
// Texts: labels (timer a), number and title of the cursor track (timer b, restarted on a move),
// the 36 track marks (timer c), then the pulsing box on the cursor while the crosshair is idle.
void DrawBgmPlayerTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u16 parts = GRef<u16>(obj + 0x27C);
    GRef<u16>(obj + 0x27C) = static_cast<u16>(parts & 0x7FFF);
    BeginMenuTextLayer(list, index);
    u32 track = GRef<u32>(obj + 0x314);
    if (!(track < 0x24)) track = 0;
    const u32 entry = kBgmTracks + track * 0xC;
    const f32 a = GRef<f32>(obj + 0x2C8);
    if (0.0f < a) {
        const f32 t = a * 90.0f;
        DrawMenuText(t, 0x240, 0x520, 4, 0x00411480);                  // "|"
        DrawMenuText(fpu::Sub(t, 1.0f), 0x300, 0x520, 4, 0x00409B08);  // "BGM NUMBER >"
        DrawMenuText(fpu::Sub(t, 8.0f), 0x300, 0x6A0, 4, 0x00409B18);  // "BGM TITLE >"
        const f32 left = fpu::Sub(t, 19.0f);
        if (left < 0.0f) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (6.0f < left && (parts & 0x100) != 0) {
            GRef<u16>(obj + 0x27C) &= 0xFEFF;
        }
    }
    const f32 b = GRef<f32>(obj + 0x2CC);
    if (0.0f < b) {
        f32 t = fpu::Sub(b * 90.0f, 8.0f);
        const u64 args[1] = {Sx(static_cast<s32>(track) + 1)};
        ee::sprintf(buf, 0x00411488, args, 1);  // "#%02d"
        DrawMenuText(t, 0x300, 0x5D0, 3, buf);
        DrawMenuText(fpu::Sub(t, 8.0f), 0x300, 0x750, 3, GRef<u32>(entry));
        if (0.0f < t) {
            const s32 len = static_cast<s32>(ee::strlen(GRef<u32>(entry)));
            t = fpu::Sub(t, fpu::Add(static_cast<f32>(len), 8.0f));
            if (t < 0.0f) {
                GRef<u16>(obj + 0x27C) |= 0x8000;
            } else if (8.0f < t && (parts & 0x200) != 0) {
                GRef<u16>(obj + 0x27C) &= 0xFDFF;
            }
        }
    }
    const f32 c = GRef<f32>(obj + 0x2D0);
    if (0.0f < c) {
        f32 r = c * 90.0f;
        f32 m = r;
        s32 left = 0x24;
        s32 line = 0;
        s32 y = 0xB50;
        do {
            m = r;
            s32 x = 0x8E0;
            for (s32 k = 0; 0 < left;) {
                DrawMenuText(m, x, y, 8, 0x00411490);
                ++k;
                --left;
                x += 0x140;
                m = fpu::Sub(m, 0.75f);
                if (!(k < 0x12)) break;
            }
            ++line;
            y += 0x130;
            r = fpu::Sub(r, 8.0f);
        } while (line < 2 && 0 < left);
        r = fpu::Sub(r, 10.0f);
        if (m < 0.0f) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (6.0f < r && (parts & 0x400) != 0) {
            GRef<u16>(obj + 0x27C) = static_cast<u16>((GRef<u16>(obj + 0x27C) & 0xFBFF) | 0x800);
        }
    }
    EndMenuTextLayer();
    if ((GRef<u16>(obj + 0x27C) & 0x800) == 0 || GRef<u16>(obj + 0x240) != 0) return;
    DrawPulseBox(list, index, 0x1067, GRef<s32>(obj + 0x30C) * 0x140 + 0x8E0, GRef<s32>(obj + 0x310) * 0x130 + 0xB50,
                 0x100, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00244D08
void DrawBgmPlayer(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), obj);
    DrawPanel(obj, 0, obj + 0x27C);
    DrawBgmPlayerTexts(obj, 0, obj);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x2E0);
    DrawMessageBox(obj, 0, obj + 0x2E0);
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x00244DA8
// Sub 0x31: creates the page, queues DATA 0xB8 and fades the menu BGM out over 0.5 s.
void EnterBgmPlayer() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    MainContext* ctx = Ctx();
    ctx->draw_mode = 2;
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    ctx->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateBgmPlayer();
    g_BgmPlayerPage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB8, 1, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x32);
    FadeOutMenuBgm(0.5f);
}

// EE: 0x00244E50
void LoadBgmPlayer() {
    const u32 obj = g_BgmPlayerPage();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x2E0);
        RequestSubState(0x33);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x00244EA0
void RunBgmPlayer() {
    const u32 obj = g_BgmPlayerPage();
    debug::OnGuestCheckpoint(0x00244EA0);
    UpdateFade(obj + 0x230);
    BgmPlayerUpdate(obj);
    DrawBgmPlayer(obj);
}

AC04_GUEST_FUNCTION(0x00244320, EnterSoundPage);
AC04_GUEST_FUNCTION(0x002443C0, LoadSoundPage);
AC04_GUEST_FUNCTION(0x00244410, RunSoundPage);
AC04_GUEST_FUNCTION(0x00244DA8, EnterBgmPlayer);
AC04_GUEST_FUNCTION(0x00244E50, LoadBgmPlayer);
AC04_GUEST_FUNCTION(0x00244EA0, RunBgmPlayer);

}  // namespace ac04::game
