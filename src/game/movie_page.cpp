// In-menu movie player (scene 4 subs 7-13, pages 31-34; docs/subsystems/frontend.md "In-menu
// movie player"), its subtitles, the movie display modes and the per-iteration frame sync, plus the
// subtitle text drawing shared with the message box (0x0022B650 / 0x0022C1B0).
//
// Page object (0x2B0 bytes, *0x00411824): +0x10 packet list, +0x20 data load queue, +0x230 DATA
// 0x103 + n (subtitle font block), +0x240 fade object, +0x24C fade level, +0x250 message box
// (subtitle line), +0x274 skip state (0 wait 30 frames, 1 skippable, 2 skipped), +0x278/+0x27C
// MenuGoTo target, +0x280 picture mode (0 none, 1 4:3, 2 16:9 stretched), +0x284 movie page n
// (0-11 story, 0xC opening INTRO, 0xD STAFF), +0x288 iterations, +0x28C f32 length (s), +0x290
// f32 time (s), +0x294 subtitles on, +0x298 subtitle index, +0x29C subtitle count, +0x2A0 subtitle
// table {u32 message, f32 start, f32 length} (table 0x003A6038, {count, table} per page).
#include "game/game.h"

#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/ee_hw.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_MenuMoviePage, 0x00411824)  // gp-0x41CC
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)
AC04_GUEST_VAR(u32, g_InterludeOptions, 0x002A15CC)

constexpr u32 kMovieLengths = 0x003AF880;  // s32 frames per page n
constexpr u32 kSubtitleTables = 0x003A6038;
constexpr u32 kVagSlots = 0x00353BF8;

u32 MovieRequest() { return GRef<u32>(g_GameState().addr + 0x20); }

// TBP0 of the draw buffer from its FRAME register value: FBP * 32.
u64 DrawBufferTex0(u64 frame) { return static_cast<u64>(static_cast<s64>((frame & 0x1FF) << 37) >> 32); }

}  // namespace

// EE: 0x0022B650
// Uploads the glyphs of the message box's lines (at most 3) to VRAM blocks 0x2C00 + line * 0x100,
// then the font CLUT to 0x2F00.
void UploadSubtitleGlyphs(u32 list, s32 index, u32 mb) {
    s32 lines = GRef<s16>(mb + 0xC);
    if (lines >= 4) lines = 3;
    for (s32 i = 0; i < lines; ++i) {
        UploadMessageGlyphs16(list, index, GRef<s16>(mb + 0xE + static_cast<u32>(i) * 2),
                      static_cast<u64>((i * 0x100 + 0x2C00) & 0xFF00));
    }
    UploadMessageGlyphs16(list, index, -1, 0x2F00);
}

// EE: 0x0022C1B0
// Draws the message box's lines (at most 3) centred at the bottom of the screen: per character a
// glyph sprite in three layers (two shadow offsets, then the white glyph), starting from 0x1400
// minus half the line width. Line spacing and position follow the screen ratio (16:9) and the
// box's raised-position flag (+0 bit 4).
void DrawSubtitleText(u32 list, s32 index, u32 mb) {
    s32 lines = GRef<s16>(mb + 0xC);
    if (lines <= 0 || GRef<f32>(mb + 8) <= 0.0f) return;
    if (lines >= 4) lines = 3;
    s32 spacing;
    s32 y;
    if ((Ctx()->flags & 0x10000) != 0) {
        spacing = 0x220;
        y = 0x1440;
    } else {
        spacing = 0x1A0;
        y = (GRef<u32>(mb) & 0x10) != 0 ? 0x14C0 : 0x1320;
    }
    if (lines == 1) {
        y = y + 0x40 + spacing;
    } else if (lines == 2) {
        y = y + 0x40 + (spacing >> 1);
    }
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildFrameDrawEnv(start + 0x10);
    GRef<u64>(p) = 0x1000000000008002ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x20) = 0x44;
    GRef<u64>(p + 0x28) = 0x43;
    GRef<u64>(p + 0x10) = 0;
    p += 0x30;
    for (s32 i = 0; i < lines; ++i) {
        const u32 t5 = static_cast<u32>(i) * 2;
        const s32 width = static_cast<s32>(static_cast<s16>(GRef<u16>(mb + 0x1E + t5))) >> 1;
        const s32 count = GRef<s16>(mb + 0x16 + t5);
        s32 x = 0x1400 - width;
        if (x < 0) x = 0;
        GRef<u64>(p) = 0x2400000000008001ull;
        GRef<u64>(p + 8) = 0x70;
        GRef<u64>(p + 0x10) = 0x356;
        GRef<u64>(p + 0x18) = static_cast<u64>(i * 0x100 + 0x2C00) | 0x2005E006A9428000ull;
        p += 0x20;
        const s32 next_y = y + spacing;
        for (s32 j = 0; j < count; ++j) {
            const u32 u = static_cast<u32>((j & 0xF) << 9) + 8;
            const u32 v = static_cast<u32>((j >> 4) << 9);
            const s32 w = GetMessageCharWidth(static_cast<u32>(GRef<s16>(mb + 0xE + t5)), j);
            const s32 x2 = x + w * 16;
            const u64 uv0 = u | static_cast<u64>(v + 8) << 16;
            const u64 uv1 = static_cast<u64>(u + static_cast<u32>(w * 16)) | static_cast<u64>(v + 0x208) << 16;
            GRef<u64>(p) = 0xE400000000008001ull;
            GRef<u64>(p + 8) = 0x0053D3153D353D31ull;
            const u32 d = p + 0x10;
            GRef<u64>(d) = 0x60080808;
            GRef<u64>(d + 0x48) = 0x80FFFFFF;
            GRef<u64>(d + 0x08) = uv0;
            GRef<u64>(d + 0x68) = Sx(x2) | Sx(y + 0x200) << 16;
            GRef<u64>(d + 0x18) = uv1;
            GRef<u64>(d + 0x10) = Sx(x + 0x10) | Sx(y + 0x10) << 16;
            GRef<u64>(d + 0x30) = Sx(x + 0x20) | Sx(y + 0x20) << 16;
            GRef<u64>(d + 0x58) = Sx(x) | Sx(y) << 16;
            GRef<u64>(d + 0x20) = Sx(x2 + 0x10) | Sx(y + 0x210) << 16;
            GRef<u64>(d + 0x40) = Sx(x2 + 0x20) | Sx(y + 0x220) << 16;
            GRef<u64>(d + 0x50) = uv0;
            GRef<u64>(d + 0x28) = uv0;
            GRef<u64>(d + 0x60) = uv1;
            GRef<u64>(d + 0x38) = uv1;
            p += 0x80;
            x = x2;
        }
        y = next_y;
    }
    LinkPacket(list, index, start, CloseDmaPacket(start, p));
}

// EE: 0x0022CA78
// Fades the stream slots 2-5 out over `time` seconds.
void FadeOutStreamSlots(f32 time) {
    func_0021F190(time, 2, 0, 0, 2);
    func_0021F190(time, 3, 0, 0, 2);
    func_0021F190(time, 4, 0, 0, 2);
    func_0021F190(time, 5, 0, 0, 2);
}

// EE: 0x002528C0
// UploadMessageGlyphs with a 16-bit VRAM block.
void UploadMessageGlyphs16(u32 list, s32 index, s64 id, u64 dbp) { UploadMessageGlyphs(list, index, id, dbp & 0xFFFF); }

// EE: 0x00238480
// Movie page display: 640 x 448 frame mode, black clear, draw mode 2 (display flags 0x8100), both
// draw contexts on frame buffer 0 and the display on 0.
void SetMoviePageDisplay() {
    MainContext* ctx = Ctx();
    ctx->clear_rgba[0] = 0;
    ctx->disp_w = 0x280;
    ctx->disp_h = 0x1C0;
    ctx->draw_w = 0x280;
    ctx->draw_h = 0x1C0;
    ctx->clear_rgba[1] = 0;
    Ctx()->clear_rgba[2] = 0;
    Ctx()->clear_rgba[3] = 0;
    Ctx()->frame_psm = 0;
    Ctx()->zbuf_psm = 0x31;
    Ctx()->ztest = 1;
    Ctx()->draw_mode = 2;
    Ctx()->disp_flags = 0x8100;
    BuildDrawEnvPackets();
    ctx = Ctx();
    const u64 frame = static_cast<u64>(ctx->draw_w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24;
    const u64 disp = ctx->dispenv[2];
    const u64 test = ctx->drawenv_pkt[4];
    ctx->drawenv_pkt[20] |= 0x100000000ull;
    ctx->dispenv[2] = disp & 0xFFFFFFFFFFFFFE00ull;
    ctx->drawenv_pkt[2] = frame;
    ctx->drawenv_pkt[4] = test | 0x100000000ull;
    ctx->drawenv_pkt[18] = frame;
}

// EE: 0x00238560
// Movie playback display: as SetMoviePageDisplay with draw mode 0 (display flags 0xC000); the
// pictures are drawn into frame buffer 0x8C while 0 is shown (MovieFrameSync swaps them).
void SetMoviePlayDisplay() {
    MainContext* ctx = Ctx();
    ctx->clear_rgba[0] = 0;
    ctx->disp_w = 0x280;
    ctx->disp_h = 0x1C0;
    ctx->draw_w = 0x280;
    ctx->draw_h = 0x1C0;
    ctx->clear_rgba[1] = 0;
    Ctx()->clear_rgba[2] = 0;
    Ctx()->clear_rgba[3] = 0;
    Ctx()->frame_psm = 0;
    Ctx()->zbuf_psm = 0x31;
    Ctx()->ztest = 1;
    Ctx()->draw_mode = 0;
    Ctx()->disp_flags = 0xC000;
    BuildDrawEnvPackets();
    ctx = Ctx();
    const u16 w = ctx->draw_w;
    const u8 psm = ctx->frame_psm;
    const u64 disp = ctx->dispenv[2];
    const u64 test = ctx->drawenv_pkt[4];
    ctx->drawenv_pkt[20] |= 0x100000000ull;
    ctx->dispenv[2] = disp & 0xFFFFFFFFFFFFFE00ull;
    ctx->drawenv_pkt[2] = static_cast<u64>(w >> 6) << 16 | static_cast<u64>(psm) << 24 | 0x8C;
    ctx->drawenv_pkt[18] = static_cast<u64>(w >> 6) << 16 | static_cast<u64>(psm) << 24 | 0x8C;
    ctx->drawenv_pkt[4] = test | 0x100000000ull;
}

// EE: 0x00238660
// Back to the menu display (display flags 0x8200), frame buffer 0.
void SetMenuDisplayAfterMovie() {
    Ctx()->disp_flags = 0x8200;
    BuildDrawEnvPackets();
    MainContext* ctx = Ctx();
    const u64 disp = ctx->dispenv[2];
    const u64 test = ctx->drawenv_pkt[4];
    const u64 frame = static_cast<u64>(ctx->draw_w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24;
    ctx->drawenv_pkt[20] |= 0x100000000ull;
    ctx->dispenv[2] = disp & 0xFFFFFFFFFFFFFE00ull;
    ctx->drawenv_pkt[2] = frame;
    ctx->drawenv_pkt[4] = test | 0x100000000ull;
    ctx->drawenv_pkt[18] = frame;
}

// EE: 0x002386D8
// 1 when VAG slots 0 and 2 are idle (state 1); otherwise prints both states and returns 0.
s32 AreMovieStreamSlotsIdle() {
    const s32 ch0 = GRef<s32>(kVagSlots + 0x34);
    const s32 ch2 = GRef<s32>(kVagSlots + 0x15C);
    if (ch0 == 1 && ch2 == ch0) return 1;
    const u64 args[2] = {static_cast<u64>(static_cast<s64>(ch0)), static_cast<u64>(static_cast<s64>(ch2))};
    ee::printf(0x00408A58, args, 2);  // "CH0:%08x  CH2:%08x\n"
    return 0;
}

// EE: 0x00238728
// One movie iteration for the current request; true at the end.
bool StepMovieToEnd() {
    const u32 desc = MovieRequest();
    return DecodeMovieFrame(desc, (GRef<u32>(MovieRequest() + 0xC) & 2) != 0 ? 1 : 0) != 0;
}

// EE: 0x00238770
void SetMoviePageDisplayThunk() { SetMoviePageDisplay(); }

// EE: 0x00238778
// Starts movie `id` (picture offset `x`, `y`) with a 6 MiB work buffer from the work heap: the
// movie display, timer 0 counting H-blanks, the reader; then waits for an odd field.
void StartMenuMovie(s32 id, s32 x, s32 y) {
    const u32 desc = MovieRequest();
    GRef<u32>(desc + 0x1C) = WorkHeapAlloc(0x600000, 0x40);
    SetMoviePlayDisplay();
    sce::EeHwWrite(sce::EeReg::kT0Mode, 0x83);
    GRef<s32>(desc) = id;
    GRef<s32>(desc + 4) = x;
    GRef<s32>(desc + 8) = y;
    StartMovie(desc);
    while (sceGsSyncV(0) != 0) {
    }
}

// EE: 0x00238808
void ResetMovieTimerThunk() { ResetMovieTimer(); }

// EE: 0x00238810
// One movie iteration: the PCM volume fade while playing; at the end the request is cleared.
s32 UpdateMenuMovie() {
    const u32 desc = MovieRequest();
    const s32 r = DecodeMovieFrame(desc, (GRef<u32>(MovieRequest() + 0xC) & 2) != 0 ? 1 : 0);
    if (r == 0) {
        UpdateMovieAudioFade();
    } else {
        ClearMovieRequest(MovieRequest());
    }
    return r;
}

// EE: 0x00238870
// End of a movie iteration: swaps the displayed and the drawn frame buffer (0 <-> 0x8C), waits
// until timer 0 has counted 0x118 H-blanks since the iteration began (past the next vblank: each
// iteration spans two fields) and counts the extra field in frame_count.
void MovieFrameSync() {
    debug::OnGuestCheckpoint(0x00238870);
    MainContext* ctx = Ctx();
    const u64 disp = ctx->dispenv[2];
    const u16 w = ctx->draw_w;
    u64 frame;
    if ((disp & 0x1FF) == 0x8C) {
        const u8 psm = ctx->frame_psm;
        ctx->dispenv[2] = disp & 0xFFFFFFFFFFFFFE00ull;
        frame = static_cast<u64>(w >> 6) << 16 | static_cast<u64>(psm) << 24 | 0x8C;
    } else {
        frame = static_cast<u64>(w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24;
        ctx->dispenv[2] = (disp & 0xFFFFFFFFFFFFFE00ull) | 0x8C;
    }
    ctx->drawenv_pkt[2] = frame;
    ctx->drawenv_pkt[18] = frame;
    while (sce::EeHwRead(sce::EeReg::kT0Count) < 0x118) sce::EeHwWaitTimer();
    Ctx()->frame_count += 1;
}

// EE: 0x00238918
void SetMenuDisplayAfterMovieThunk() { SetMenuDisplayAfterMovie(); }

// EE: 0x00238920
// Copies the draw buffer onto itself one line down with FIX alpha `alpha` (ALPHA 0x64 = (Cs - Cd)
// * FIX + Cd): blends each line with the one above (interlace flicker filter for the movie page).
void DrawMovieLineBlend(u32 list, s32 index, s64 alpha) {
    const u64 env = Ctx()->drawenv_pkt[2];
    const u32 start = Ctx()->cur_buf.addr;
    const u16 w = Ctx()->draw_w;
    const u16 h = Ctx()->draw_h;
    const u32 p = BuildFrameDrawEnv(start + 0x10);
    GRef<u64>(p) = 0x1000000000000002ull;
    GRef<u64>(p + 8) = 0xE;
    const s32 h16 = static_cast<s32>(h) * 0x10;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x20) = static_cast<u64>(alpha) << 32 | 100;
    GRef<u64>(p + 0x28) = 0x43;
    GRef<u64>(p + 0x10) = 0;
    const s32 w16 = static_cast<s32>(w) * 0x10;
    GRef<u64>(p + 0x30) = 0x8400000000008001ull;
    GRef<u64>(p + 0x38) = 0x53D31790;
    GRef<u64>(p + 0x40) = 0x356;
    GRef<u64>(p + 0x48) = 5;
    GRef<u64>(p + 0x78) = Sx(w16) | Sx(h16 - 0x10) << 16;
    GRef<u64>(p + 0x50) = (static_cast<u64>(static_cast<s64>(static_cast<s32>(w + 0x3F))) >> 6) << 14 | DrawBufferTex0(env) |
                          0x2A8000000ull;
    GRef<u64>(p + 0x58) = 0x80808080;
    GRef<u64>(p + 0x60) = 0x100008;
    GRef<u64>(p + 0x70) = Sx(w16 + 8) | Sx(h16) << 16;
    GRef<u64>(p + 0x68) = 0;
    LinkPacket(list, index, start, CloseDmaPacket(start, p + 0x80));
}

// EE: 0x00238AA8
// Shows subtitle entry `entry` ({message, start, length}) in the message box.
void SetSubtitleLine(u32 mb, u32 entry) { SetMessageBoxText(mb, GRef<s32>(entry)); }

// EE: 0x00238AB0
// Subtitle timing at movie time `t` (seconds): the box is shown (+0x258 = 1) while t is inside the
// current entry; past its end the next entry is loaded (and shown if it has started).
void UpdateSubtitles(f32 t, u32 obj) {
    s32 i = GRef<s32>(obj + 0x298);
    GRef<f32>(obj + 0x258) = -1.0f;
    if (i >= GRef<s32>(obj + 0x29C)) return;
    const u32 e = static_cast<u32>(i) * 0xC + GRef<u32>(obj + 0x2A0);
    const f32 begin = GRef<f32>(e + 4);
    if (t < begin) return;
    if (fpu::Add(begin, GRef<f32>(e + 8)) <= t) {
        ++i;
        GRef<s32>(obj + 0x298) = i;
        if (i >= GRef<s32>(obj + 0x29C)) return;
        SetSubtitleLine(obj + 0x250, GRef<u32>(obj + 0x2A0) + static_cast<u32>(i) * 0xC);
        if (t < GRef<f32>(static_cast<u32>(i) * 0xC + GRef<u32>(obj + 0x2A0) + 4)) return;
    }
    GRef<f32>(obj + 0x258) = 1.0f;
}

// EE: 0x00238B80
// Creates the movie page for the current sub: sub 8 the STAFF movie (n 0xD, then sub-page 0xC);
// subs 9/10 story movie n = ret_sub (0 = opening; then to the mission title (2), the ending (0x22)
// or the post-mission menu 0xB; sub 10 from the interlude viewer, back to 0x14) with the picture
// mode by the screen ratio; otherwise the opening INTRO (n 0xC, then 3). Subtitles for all but
// STAFF.
u32 CreateMenuMoviePage() {
    const u32 obj = WorkHeapAlloc(0x2B0, 0x10);
    ResetPacketList(obj + 0x10);
    const MainContext* ctx = Ctx();
    GRef<u32>(obj + 0x274) = 0;
    const u16 sub = ctx->sub;
    GRef<u32>(obj + 0x278) = 0;
    u32 n;
    if (sub == 8 || sub < 9 || sub >= 0xB) {
        u32 target;
        if (sub == 8) {
            target = 0xC;
            n = 0xD;
        } else {
            target = 3;
            n = 0xC;
        }
        GRef<u32>(obj + 0x27C) = target;
        GRef<u32>(obj + 0x280) = 0;
    } else {
        n = ctx->ret_sub;
        if (n >= 0xC) n = 0;
        u32 target;
        if (sub == 9) {
            if (n == 6 || n == 0) {
                target = 2;
            } else if (n == 0xB) {
                target = 0x22;
            } else {
                target = 0xB;
            }
        } else {
            target = 0x14;
        }
        GRef<u32>(obj + 0x27C) = target;
        GRef<u32>(obj + 0x280) = (ctx->flags & 0x10000) != 0 ? 2u : 1u;
    }
    GRef<u32>(obj + 0x284) = n;
    GRef<u32>(obj + 0x294) = n == 0xD ? 0u : 1u;
    GRef<u32>(obj + 0x288) = 0;
    GRef<u32>(obj + 0x290) = 0;
    GRef<f32>(obj + 0x28C) = static_cast<f32>(GRef<s32>(kMovieLengths + n * 4)) * K(0x004102CC);
    if (GRef<u32>(obj + 0x294) != 0) {
        GRef<u32>(obj + 0x298) = 0;
        GRef<u32>(obj + 0x29C) = GRef<u32>(kSubtitleTables + n * 8);
        GRef<u32>(obj + 0x2A0) = GRef<u32>(kSubtitleTables + n * 8 + 4);
    }
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0x10;
    GRef<u16>(fade + 0x30) = 0;
    InitFade(obj + 0x240, 1);
    return obj;
}

// EE: 0x00238D10
// Starts the page's movie: id n + 1 (SS01-SS12), 0 for n 0xC (INTRO), 0x1F for n 0xD (STAFF);
// 16:9 pictures 0x38 lines lower.
void StartPageMovie(u32 obj) {
    PushWorkHeapMark();
    s32 n = GRef<s32>(obj + 0x284);
    if (n > 0xE) n = 0;
    s32 id;
    if (n == 0xC) {
        id = 0;
    } else if (n == 0xD) {
        id = 0x1F;
    } else {
        id = n + 1;
    }
    StartMenuMovie(id, 0, GRef<s32>(obj + 0x280) == 2 ? 0x38 : 0);
}

// EE: 0x00238D80
// Per iteration: the movie time from the pictures shown, the subtitles, and the skip: after 30
// iterations Start (0x40) stops the movie with a 1 s audio fade and fades page and music out.
void HandleMenuMovieInput(u32 obj) {
    const u32 desc = MovieRequest();
    GRef<s32>(obj + 0x288) += 1;
    const f32 t = static_cast<f32>(GRef<s32>(desc + 0x18)) * K(0x004102D0);
    GRef<f32>(obj + 0x290) = t;
    if (GRef<u32>(obj + 0x294) != 0) UpdateSubtitles(t, obj);
    const u32 edge = Ctx()->pad_buttons[0][1];
    const u32 state = GRef<u32>(obj + 0x274);
    if (state == 0) {
        if (GRef<s32>(obj + 0x288) >= 0x1F) GRef<u32>(obj + 0x274) = 1;
        return;
    }
    if (state != 1 || (edge & 0x40) == 0) return;
    GRef<u32>(obj + 0x274) = 2;
    PlayMenuSe(0, 0xD);
    RequestMovieStop(1.0f);
    StartFade(0.5f, obj + 0x240, 1);
    FadeOutMenuBgm(0.5f);
    FadeOutStreamSlots(0.5f);
}

// EE: 0x00238E88
// 16:9 screen ratio: the 640 x 336 picture (drawn 0x38 lines lower, rows 112-447) is stretched over
// the whole 448-line draw buffer with the frame as texture (anamorphic picture for 16:9 sets).
void DrawMovieWidescreen(u32 list, s32 index) {
    const u64 env = Ctx()->drawenv_pkt[2];
    const u32 start = Ctx()->cur_buf.addr;
    const u16 w = Ctx()->draw_w;
    const u16 h = Ctx()->draw_h;
    const u32 p = BuildFrameDrawEnv(start + 0x10);
    GRef<u64>(p) = 0x118B400000000001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x10) = 0;
    GRef<u64>(p + 0x20) = 0x6400000000008001ull;
    GRef<u64>(p + 0x28) = 0x53D317;
    GRef<u64>(p + 0x30) = (static_cast<u64>(static_cast<s64>(static_cast<s32>(w + 0x3F))) >> 6) << 14 | DrawBufferTex0(env) |
                          0x2A8000000ull;
    const u64 w16 = Sx(static_cast<s32>(static_cast<u32>(w) << 4));
    GRef<u64>(p + 0x38) = 0x80808080;
    GRef<u64>(p + 0x58) = w16 | Sx(static_cast<s32>(static_cast<u32>(h) * 0x10 - 0x10)) << 16;
    GRef<u64>(p + 0x40) = 0x7080008;
    GRef<u64>(p + 0x50) = w16 | 0x1BF80008;
    GRef<u64>(p + 0x48) = 0;
    LinkPacket(list, index, start, CloseDmaPacket(start, p + 0x60));
}

// EE: 0x00238FE0
// Draws the page: the 16:9 stretch, subtitle glyphs and text, the black fade and the line blend;
// sends the list and swaps the packet double buffer.
void DrawMenuMoviePage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(obj + 0x10, obj);
    if (GRef<s32>(obj + 0x280) == 2) DrawMovieWidescreen(obj, 0);
    if (GRef<u32>(obj + 0x294) != 0) {
        UploadSubtitleGlyphs(obj, 0, obj + 0x250);
        DrawSubtitleText(obj, 0, obj + 0x250);
    }
    DrawBlackFade(GRef<f32>(obj + 0x24C), obj, 0);
    DrawMovieLineBlend(obj, 0, 0x68);
    FlushCache(0);
    SendDmaPacketList(1, obj + 0x10, 1);
    Ctx()->cur_buf.addr = SwapDoubleBuffer(g_pMainContext().addr + offsetof(MainContext, dbuf));
    sceGsSyncPath(0, 0);
}

// EE: 0x002390C8
// Subs 7-10: creates the page; sub 9 with INTERLUDE off (0x002A15CC bit 3) skips to its target.
// Otherwise resets the menu BGM, sets the page display and queues the subtitle font (DATA 0x103 +
// n); then sub 0xB.
void MenuMovieEnter() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    const u32 obj = CreateMenuMoviePage();
    g_MenuMoviePage() = obj;
    if (Ctx()->sub == 9 && (g_InterludeOptions() & 8) != 0) {
        MenuGoTo(GRef<s32>(obj + 0x278), static_cast<u16>(GRef<u32>(obj + 0x27C)));
        return;
    }
    ResetMenuBgm();
    SetMoviePageDisplayThunk();
    ResetMenuDataLoad(obj + 0x20);
    if (GRef<u32>(obj + 0x294) != 0) QueueMenuDataLoad(obj + 0x20, static_cast<u16>(GRef<u32>(obj + 0x284) + 0x103), 1, obj + 0x230);
    RequestSubState(0xB);
}

// EE: 0x00239188
// Sub 0xB: when the subtitle font is in and the stream slots are idle: the message box with the
// first subtitle (raised for the INTRO), then the movie starts; sub 0xC.
void MenuMovieWait() {
    const u32 obj = g_MenuMoviePage();
    if (UpdateMenuDataLoad(obj + 0x20) >= 0 || AreMovieStreamSlotsIdle() != 1) return;
    if (GRef<u32>(obj + 0x294) != 0) {
        LoadFontBlock(GRef<u32>(obj + 0x230));
        InitMessageBox(obj + 0x250);
        if (GRef<s32>(obj + 0x284) == 0xC) GRef<u32>(obj + 0x250) |= 0x10;
        SetSubtitleLine(obj + 0x250, GRef<u32>(obj + 0x2A0) + GRef<u32>(obj + 0x298) * 0xC);
    }
    StartPageMovie(obj);
    RequestSubState(0xC);
}

// EE: 0x00239248
// Sub 0xC, one movie iteration (two fields): timer reset, fade, input and subtitles, the reader
// step and picture; at the end sub 0xD. Then the page is drawn and the frame synced.
void MenuMoviePlay() {
    const u32 obj = g_MenuMoviePage();
    debug::OnGuestCheckpoint(0x00239248);
    ResetMovieTimerThunk();
    UpdateFade(obj + 0x240);
    HandleMenuMovieInput(obj);
    if (UpdateMenuMovie() != 0) {
        RequestSubState(0xD);
        GRef<u32>(obj + 0x288) = 0;
    }
    DrawMenuMoviePage(obj);
    MovieFrameSync();
}

// EE: 0x002392A8
// Sub 0xD: once the reader has finished: menu display, work heap, menu BGM and the menu fade
// reset, then the page's MenuGoTo target.
void MenuMovieEnd() {
    const u32 obj = g_MenuMoviePage();
    debug::OnGuestCheckpoint(0x002392A8);
    if (!StepMovieToEnd()) return;
    SetMenuDisplayAfterMovieThunk();
    PopWorkHeapMark();
    ResetMenuBgm();
    InitFade(g_pMenuFade(), 0);
    MenuGoTo(GRef<s32>(obj + 0x278), static_cast<u16>(GRef<u32>(obj + 0x27C)));
}

AC04_GUEST_FUNCTION(0x002390C8, MenuMovieEnter);
AC04_GUEST_FUNCTION(0x00239188, MenuMovieWait);
AC04_GUEST_FUNCTION(0x00239248, MenuMoviePlay);
AC04_GUEST_FUNCTION(0x002392A8, MenuMovieEnd);

}  // namespace ac04::game
