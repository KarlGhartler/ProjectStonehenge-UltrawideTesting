// Shared front-end widgets (docs/subsystems/frontend.md "Shared menu widgets"): the menu fade and
// tab overlay, the full-screen filter passes, the list widget, the menu tree with its connector
// lines, and the message box. All packets are built in the current packet buffer exactly as on the
// PS2; static GS setup packets in .data (0x00384890, 0x00385C90) are reached by DMA "call" tags.
//
// List widget (stride 0x18): +0 s16 x, +2 s16 y, +4 s16 count, +6 s16 cursor, +0xA s16 state
// (0 closed, 1 opening, 2 open, 3 closing), +0xC f32 type-in progress, +0x10 f32* item scales,
// +0x14 char** item strings.
// Menu tree: +0 s32 active column, +4 s32 column count (<= 8), +8 list widgets[8],
// +0xC8 connectors[7] (stride 0x14: +0 s16 x0, +2 s16 y0, +4 s16 x1, +6 s16 y1, +8 s16 x2,
// +0xA s16 count, +0xC f32 alpha, +0x10 f32* scales), +0x154 f32 scales[64], +0x254 strings[64],
// +0x354 s32 visible items, +0x358 u32 hidden masks[8], +0x378 s16 first item[8], +0x388 s16 ids[].
// Message box: +0 u32 flags (1 typing, 2 done, 8 no frame), +4 u16 x, +6 u16 y, +8 f32 progress,
// +0xC s16 lines, +0xE s16 ids[3], +0x14 s16 total length, +0x16 s16 lengths[3], +0x1E s16 widths[3].
#include "game/game.h"

#include <cmath>
#include <cstddef>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

f32 K(u32 address) { return GRef<f32>(address); }
u64& Q(u32 p, u32 i) { return GRef<u64>(p + 8 * i); }
u64 Sx(s32 v) { return static_cast<u64>(static_cast<s64>(v)); }
u64 Lo32(u64 v) { return v & 0xFFFFFFFFull; }

AC04_GUEST_VAR(u32, g_MenuTextList, 0x00410E8C)   // gp-0x4B64: text layer bucket list (0: closed)
AC04_GUEST_VAR(u32, g_MenuTextIndex, 0x00411804)  // gp-0x41EC
AC04_GUEST_VAR(u32, g_MenuTextX, 0x00411808)      // gp-0x41E8: 0x8000 - draw_w * 8
AC04_GUEST_VAR(u32, g_MenuTextY, 0x0041180C)      // gp-0x41E4: 0x8000 - draw_h * 8

constexpr u32 kMenuFilterPacket = 0x00384890;  // .data: GS setup reached by a call tag
constexpr u32 kMenuSpritePacket = 0x00385C90;  // .data: GS setup reached by a call tag
constexpr u64 kTex0Glyphs = 0x2007DF82A9413D40ull;
constexpr u64 kTex0Panel = 0x2007DF02A9313C00ull;

}  // namespace

// ---- Fade, overlay levels ------------------------------------------------------------------------

// EE: 0x00226D70
// Darkening quad over the draw buffer with strength 1 - `level` (0..255).
void DrawBlackFade(f32 level, u32 list, s32 index) {
    const s32 alpha = fpu::FloatToInt(fpu::Sub(1.0f, level) * 255.0f);
    if (alpha <= 0) return;
    const u32 a = static_cast<u32>(alpha < 0x100 ? alpha : 0xFF) & 0xFF;
    const MainContext* ctx = Ctx();
    BuildFadeQuad(list, index, 0, a, 0,
                  static_cast<u64>(ctx->draw_w) << 4 | static_cast<u64>(ctx->draw_h) << 0x14);
}

// EE: 0x00226E58
// Pulse 0..1 from the unpaused frame counter: (sin(frac(n * k0) * k1) + 1) / 2.
f32 GetMenuPulse() {
    const u32 n = Ctx()->frame_count_unpaused;
    f32 f;
    if (static_cast<s32>(n) >= 0) {
        f = static_cast<f32>(static_cast<s32>(n));
    } else {
        f = static_cast<f32>(static_cast<s32>((n & 1) | (n >> 1)));
        f = fpu::Add(f, f);
    }
    const f32 x = f * K(0x00410050);
    const f32 s = ee::sinf(fpu::Sub(x, ee::floorf(x)) * K(0x00410054));
    return fpu::Add(s, 1.0f) * 0.5f;
}

// EE: 0x00226EE0
// Clears the 7 overlay levels (master + 6 tabs).
void ClearMenuTabLevels(u32 levels) {
    for (s32 i = 6; i >= 0; --i) GRef<u32>(levels + 4u * static_cast<u32>(i)) = 0;
}

// EE: 0x00226F18
// Moves `value` towards `target` by the factor `rate`; snaps when closer than the epsilon.
f32 ApproachValue(f32 value, f32 target, f32 rate) {
    if (std::fabs(fpu::Sub(target, value)) < K(0x00410058)) return target;
    return fpu::Add(target, fpu::Sub(value, target) * rate);
}

// EE: 0x00226F98
// Overlay levels: [0] towards 1 when `master` is set, [1..6] towards 1 for each set bit of `tabs`.
void UpdateMenuTabLevels(u32 levels, s16 master, u32 tabs) {
    tabs &= 0xFFFF;
    if (master == 0) {
        GRef<f32>(levels) = ApproachValue(GRef<f32>(levels), 0.0f, K(0x00410068));
    } else {
        GRef<f32>(levels) = ApproachValue(GRef<f32>(levels), 1.0f, K(0x00410064));
    }
    for (u32 i = 1; i < 7; ++i) {
        f32& v = GRef<f32>(levels + 4 * i);
        if ((tabs & 1) == 0) {
            v = ApproachValue(v, 0.0f, K(0x00410070));
        } else {
            v = ApproachValue(v, 1.0f, K(0x0041006C));
        }
        tabs >>= 1;
    }
}

// EE: 0x00226C78
// Menu fade object reset: overlay on (+0x10), fade set up (black), fade-in over 0.5 s started, tab
// levels cleared, master/tab requests (+0x30/+0x32) cleared.
void ResetMenuFade(u32 fade) {
    GRef<u32>(fade + 0x10) = 1;
    InitFade(fade, 0);
    StartFade(0.5f, fade, 0);
    ClearMenuTabLevels(fade + 0x14);
    GRef<u16>(fade + 0x32) = 0;
    GRef<u16>(fade + 0x30) = 0;
}

// EE: 0x00226CD0
void UpdateMenuOverlay(u32 fade) {
    UpdateMenuTabLevels(fade + 0x14, GRef<s16>(fade + 0x30), GRef<u16>(fade + 0x32));
    UpdateFade(fade);
}

// ---- Full-screen filter passes ------------------------------------------------------------------

// EE: 0x002275C0
// GS environment for a `w` x `h` buffer at FBP 0 (FRAME, SCISSOR, XYOFFSET, TEST, ... as A+D).
u32 BuildMenuDrawEnv(u32 p, s32 w, s32 h) {
    s32 fbw = w + 0x7E;
    if (w + 0x3F >= 0) fbw = w + 0x3F;
    Q(p, 0xD) = 0x15;
    Q(p, 0xE) = 0x80;
    Q(p, 0xF) = 0x3B;
    Q(p, 0x11) = 9;
    Q(p, 0x12) = 1;
    Q(p, 0x13) = 0x46;
    Q(p, 0) = 0x1000000000008009ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = Sx(fbw >> 6) << 0x10;
    Q(p, 3) = 0x4D;
    Q(p, 4) = Sx(w - 1) << 0x10 | Sx(h - 1) << 0x30;
    Q(p, 5) = 0x41;
    Q(p, 6) = Sx(w * -8 + 0x8000) | Sx(h * -8 + 0x8000) << 0x20;
    Q(p, 7) = 0x19;
    Q(p, 8) = 0x131000000ull;
    Q(p, 9) = 0x4F;
    Q(p, 0xA) = 0x30000;
    Q(p, 0xB) = 0x48;
    Q(p, 0xC) = 0x60;
    Q(p, 0x10) = 0;
    return p + 0xA0;
}

// EE: 0x002276E8
// The same environment for the current draw buffer (its FRAME register and size).
u32 BuildFrameDrawEnv(u32 p) {
    const MainContext* ctx = Ctx();
    Q(p, 0) = 0x1000000000008009ull;
    const s32 h = ctx->draw_h;
    const s32 w = ctx->draw_w;
    Q(p, 1) = 0xE;
    Q(p, 2) = ctx->drawenv_pkt[2];
    Q(p, 3) = 0x4D;
    Q(p, 0x11) = 9;
    Q(p, 0x12) = 1;
    Q(p, 0x13) = 0x46;
    Q(p, 4) = Sx(w - 1) << 0x10 | Sx(h - 1) << 0x30;
    Q(p, 5) = 0x41;
    Q(p, 6) = Sx(static_cast<s32>(static_cast<u32>(w) * 0xFFFFFFF8u + 0x8000)) |
              Sx(static_cast<s32>(static_cast<u32>(h) * 0xFFFFFFF8u + 0x8000)) << 0x20;
    Q(p, 7) = 0x19;
    Q(p, 8) = 0x131000000ull;
    Q(p, 9) = 0x4F;
    Q(p, 0xA) = 0x30000;
    Q(p, 0xB) = 0x48;
    Q(p, 0xC) = 0x60;
    Q(p, 0xD) = 0x15;
    Q(p, 0xE) = 0x80;
    Q(p, 0xF) = 0x3B;
    Q(p, 0x10) = 0;
    return p + 0xA0;
}

// EE: 0x00227800
// Two moving horizontal bands (scanline effect) whose position follows the frame counter `frame`.
u32 BuildScanlineBands(u32 p, u32 frame, s32 w, s32 h) {
    const f32 t = -(static_cast<f32>(static_cast<s32>(frame | 1)) * K(0x00410098));
    Q(p, 1) = 0xE;
    Q(p, 0) = 0x1000000000008002ull;
    Q(p, 3) = 7;
    Q(p, 2) = 0x2007D082A9307E90ull;
    Q(p, 4) = 0x58;
    Q(p, 5) = 0x43;
    u32 q = p + 0x30;
    s32 y = fpu::FloatToInt(t * K(0x0041009C)) % 0x33F0;
    if (y < 0) y += 0x33F0;
    y -= 0x720;
    if (y < (h << 4)) {
        Q(q, 0) = 0x6400000000008001ull;
        Q(q, 1) = 0x53D310;
        Q(q, 2) = 0x356;
        Q(q, 7) = Sx(w * 0x10 + 0x8000) | Sx(y + 0x8720) << 0x10;
        Q(q, 3) = 0x800C0604;
        Q(q, 4) = 0x803E8;
        Q(q, 5) = Sx(y + 0x8000) << 0x10 | 0x8000;
        Q(q, 6) = 0x38803E8;
        q += 0x40;
    }
    y = fpu::FloatToInt(t * K(0x004100A0)) % 0x33F0;
    if (y < 0) y += 0x33F0;
    y -= 0x1C0;
    if (y < (h << 4)) {
        Q(q, 0) = 0x6400000000008001ull;
        Q(q, 1) = 0x53D310;
        Q(q, 2) = 0x356;
        Q(q, 7) = Sx(w * 0x10 + 0x8000) | Sx(y + 0x81C0) << 0x10;
        Q(q, 3) = 0x80100906;
        Q(q, 4) = 0x803F8;
        Q(q, 5) = Sx(y + 0x8000) << 0x10 | 0x8000;
        Q(q, 6) = 0x1B803F8;
        q += 0x40;
    }
    return q;
}

// EE: 0x00227A10
// Four scaled copies of the frame-buffer texture (zoom blur), the last one after an ALPHA change.
u32 BuildZoomBlur(f32 t, f32 scale, u32 p, s32 w, s32 h) {
    const s32 w8 = w << 3;
    const s32 h8 = h << 3;
    Q(p, 0) = 0x1000000000008003ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = Sx(0x8000 - w8) | Sx(0x8000 - h8) << 0x20;
    Q(p, 3) = 0x19;
    Q(p, 4) = 0x58;
    Q(p, 5) = 0x43;
    Q(p, 6) = 0x2007D082A9307E90ull;
    Q(p, 7) = 7;
    const u64 color = Lo32(LerpColor(t, 0x403000, 0xFFFFFFFFB4403000ull));
    u32 q = p + 0x40;
    f32 k = K(0x004100A4);
    const f32 k2 = K(0x004100A8);
    for (s32 i = 0; i < 4; ++i) {
        const f32 s = fpu::Add(k, k2);
        if (i == 3) {
            Q(q, 0) = 0x1000000000000001ull;
            Q(q, 1) = 0xE;
            Q(q, 2) = 0x48;
            Q(q, 3) = 0x43;
            q += 0x20;
        }
        Q(q, 2) = 0x356;
        Q(q, 0) = 0x2400000000000001ull;
        Q(q, 1) = 0x10;
        Q(q, 3) = color;
        const s32 dy = fpu::FloatToInt(s * static_cast<f32>(h8));
        const s32 dx = fpu::FloatToInt(s * static_cast<f32>(w8) * scale);
        q = BuildSpriteStripsUv(q + 0x20, Sx(0x8000 - dx) | Sx(0x8000 - dy) << 0x10,
                                Sx(dx + 0x8000) | Sx(dy + 0x8000) << 0x10, 0x180018, 0x2D803D8);
        k = fpu::Sub(k, K(0x004100AC));
    }
    return q;
}

// EE: 0x00227C10
// Frame filter over the draw buffer when `level` > 0: the frame copied onto itself with an
// interpolated tint (FBMSK keeps alpha), optional scanline bands (unless `mode` & 2), then the zoom
// blur. `mode` & 1 selects ALPHA 0x68 instead of 0x64.
void DrawMenuFrameFilter(f32 level, u32 list, s32 index, u64 mode) {
    if (level <= 0.0f) return;
    const MainContext* ctx = Ctx();
    const s64 fbp = static_cast<s64>(static_cast<s32>(ctx->drawenv_pkt[2] & 0x1FF));
    const u32 w = ctx->draw_w;
    const u32 h = ctx->draw_h;
    f32 aspect = 1.0f;
    if ((ctx->flags & 0x10000) != 0) aspect = K(0x004100B0);
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, kMenuFilterPacket);
    u32 p = BuildFrameDrawEnv(tag + 0x10);
    Q(p, 0) = 0x11AB400000000002ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x800000008000ull;
    Q(p, 3) = 0x19;
    const s64 alpha_mode = (mode & 1) == 0 ? 100 : 0x68;
    Q(p, 5) = 0x43;
    Q(p, 4) = Sx(fpu::FloatToInt(level * 128.0f)) << 0x20 | static_cast<u64>(alpha_mode);
    p += 0x30;
    Q(p, 0) = 0x2400000000000001ull;
    Q(p, 1) = 0x71;
    Q(p, 2) = Lo32(LerpColor(level, 0x98E000, 0x7098E000));
    Q(p, 3) = 0x2007D02511407E80ull;
    const s32 v = fpu::FloatToInt(aspect * 10240.0f);
    p = BuildSpriteStripsUv(p + 0x20, 0x80008000ull,
                            (static_cast<u64>(w << 4) + 0x8000) | (static_cast<u64>(h << 4) + 0x8000) << 0x10,
                            0x90008, Sx(v + 8) | 0x1E090000);
    Q(p, 0) = 0x1000000000008001ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = static_cast<u64>(fbp) | (static_cast<u64>(static_cast<s64>(static_cast<s32>(w + 0x3F))) >> 6) << 0x10 |
              0xFF00000000000000ull;
    Q(p, 3) = 0x4D;
    p += 0x20;
    if ((mode & 2) == 0) p = BuildScanlineBands(p, Ctx()->frame_count, static_cast<s32>(w), static_cast<s32>(h));
    p = BuildZoomBlur(level, 1.0f / aspect, p, static_cast<s32>(w), static_cast<s32>(h));
    LinkPacket(list, index, start, CloseDmaPacket(tag, p));
}

// EE: 0x00227F30
// Alpha-only frame filter (FBMSK 0xFFFFFF: only the alpha channel is written), strength `level`
// clamped to 1, followed by the scanline bands.
void DrawMenuAlphaFilter(f32 level, u32 list, s32 index) {
    if (level <= 0.0f) return;
    if (1.0f < level) level = 1.0f;
    f32 aspect = 1.0f;
    if ((Ctx()->flags & 0x10000) != 0) aspect = K(0x004100B4);
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, kMenuFilterPacket);
    const u32 w = Ctx()->draw_w;
    const u32 h = Ctx()->draw_h;
    u32 p = BuildMenuDrawEnv(tag + 0x10, static_cast<s32>(w), static_cast<s32>(h));
    const u64 fbw = (static_cast<u64>(static_cast<s64>(static_cast<s32>(w + 0x3F))) >> 6) << 0x10;
    Q(p, 0) = 0x118B400000000002ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = fbw | 0x00FFFFFF00000000ull;
    Q(p, 5) = 0x19;
    Q(p, 3) = 0x4D;
    Q(p, 4) = 0x800000008000ull;
    p += 0x30;
    Q(p, 0) = 0x2400000000000001ull;
    Q(p, 1) = 0x71;
    Q(p, 2) = Lo32(LerpColor(level, 0x98E000, 0x7098E000));
    Q(p, 3) = 0x2007D02511407E80ull;
    const s32 v = fpu::FloatToInt(aspect * 10240.0f);
    p = BuildSpriteStripsUv(p + 0x20, 0x80008000ull,
                            (static_cast<u64>(w << 4) + 0x8000) | (static_cast<u64>(h << 4) + 0x8000) << 0x10,
                            0x90008, Sx(v + 8) | 0x1E090000);
    Q(p, 1) = 0xE;
    Q(p, 3) = 0x4D;
    const u32 frame = Ctx()->frame_count;
    Q(p, 2) = fbw | 0xFF00000000000000ull;
    Q(p, 0) = 0x1000000000008001ull;
    p = BuildScanlineBands(p + 0x20, frame, static_cast<s32>(w), static_cast<s32>(h));
    LinkPacket(list, index, start, CloseDmaPacket(tag, p));
}

// ---- Textured sprites, tabs ---------------------------------------------------------------------

// EE: 0x002284C8
// Untextured-UV sprite (REGLIST PRIM RGBAQ UV XYZ2 UV XYZ2) centred on (x, y) from a sprite
// descriptor {u16 w, u16 h, .., u32 uv0 at +8, u32 uv1 at +0xC}.
u32 BuildCenteredSprite(u32 p, s32 x, s32 y, u64 color, u32 desc) {
    Q(p, 1) = 0x535310;
    Q(p, 2) = 0x356;
    Q(p, 0) = 0x6400000000008001ull;
    Q(p, 3) = Lo32(color);
    const u32 hh = GRef<u16>(desc + 2) >> 1;
    const u32 hw = GRef<u16>(desc) >> 1;
    Q(p, 4) = GRef<u32>(desc + 8);
    Q(p, 7) = Sx(static_cast<s32>(static_cast<u32>(x) + hw)) | Sx(static_cast<s32>(static_cast<u32>(y) + hh)) << 0x10;
    Q(p, 6) = GRef<u32>(desc + 0xC);
    Q(p, 5) = Sx(static_cast<s32>(static_cast<u32>(x) - hw)) | Sx(static_cast<s32>(static_cast<u32>(y) - hh)) << 0x10;
    return p + 0x40;
}

// EE: 0x00228550
// The same with a TEX0_2 setting (`tex0`) in the register list.
u32 BuildCenteredTexSprite(u32 p, s32 x, s32 y, u64 color, u32 desc, u64 tex0) {
    Q(p, 0) = 0x7400000000008001ull;
    Q(p, 1) = 0x5353170;
    Q(p, 2) = 0x356;
    Q(p, 3) = tex0;
    Q(p, 4) = Lo32(color);
    const u32 hh = GRef<u16>(desc + 2) >> 1;
    const u32 hw = GRef<u16>(desc) >> 1;
    Q(p, 5) = GRef<u32>(desc + 8);
    Q(p, 8) = Sx(static_cast<s32>(static_cast<u32>(x) + hw)) | Sx(static_cast<s32>(static_cast<u32>(y) + hh)) << 0x10;
    Q(p, 7) = GRef<u32>(desc + 0xC);
    Q(p, 6) = Sx(static_cast<s32>(static_cast<u32>(x) - hw)) | Sx(static_cast<s32>(static_cast<u32>(y) - hh)) << 0x10;
    return p + 0x50;
}

// EE: 0x002285D8
// HYPOTHESIS: the six menu tabs along the bottom (sprites 0x0039BCE0/0x0039BCF0, stride 0x20), each
// lit by its level levels[1 + i] over a pulsing base colour.
u32 BuildMenuTabs(u32 p, u32 levels) {
    const f32 pulse = GetMenuPulse();
    const u64 lit = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(LerpColor(pulse, 0x686868, 0x808080)))));
    const u64 back = LerpColor(pulse, 0x606060, 0x989898);
    u32 front_desc = 0x0039BCE0;
    u32 back_desc = 0x0039BCF0;
    s32 x = 0x6FC0;
    for (s32 i = 5; i >= 0; --i) {
        levels += 4;
        const f32 level = GRef<f32>(levels);
        const u64 c0 = LerpColor(level, 0x765834, lit);
        const u64 c1 = LerpColor(level, 0, back);
        p = BuildCenteredTexSprite(p, x, 0x7480, c0, front_desc, kTex0Glyphs);
        p = BuildCenteredTexSprite(p, x, 0x7480, c1, back_desc, kTex0Panel);
        x += 0x680;
        front_desc += 0x20;
        back_desc += 0x20;
    }
    return p;
}

// EE: 0x002281E0
// Page fade bar into bucket `index` (hangar confirm pages): when `level` > 0 (clamped to 1), the
// static packet 0x00384890 (DMA call), a draw environment, a full-screen textured sprite (alpha
// level x 128 << 24, FRAME mask of the colour bits, texture width 1 or the 16:9 factor x 10240),
// the screen again as an untextured sprite with the Z/alpha state, and the scan-line bands.
void DrawPageFadeBar(f32 level, u32 list, s32 index) {
    if (level <= 0.0f) return;
    if (1.0f < level) level = 1.0f;
    MainContext* ctx = Ctx();
    f32 aspect = 1.0f;
    const u32 w = ctx->draw_w, h = ctx->draw_h;
    if ((ctx->flags & 0x10000) != 0) aspect = GRef<f32>(0x004100B8);
    const u32 start = ctx->cur_buf.addr;
    const u32 pkt = WriteDmaCallTag(start, 0x00384890);
    u32 p = BuildMenuDrawEnv(pkt + 0x10, static_cast<s32>(w), static_cast<s32>(h));
    const s32 alpha = fpu::FloatToInt(fpu::Mul(level, 128.0f));
    const s32 tw = fpu::FloatToInt(fpu::Mul(aspect, 10240.0f));
    const u64 cols = static_cast<u64>((w + 0x3F) >> 6) << 16;
    const u64 xy1 = static_cast<u64>(static_cast<s64>(static_cast<s32>((w << 4) + 0x8000))) |
                    static_cast<u64>(static_cast<s64>(static_cast<s32>((h << 4) + 0x8000))) << 16;
    GRef<u64>(p) = 0x118B400000000002ull;
    GRef<u64>(p + 0x10) = cols | 0x00FFFFFF00000000ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p + 0x20) = 0x800000008000ull;
    GRef<u64>(p + 0x28) = 0x19;
    p += 0x30;
    GRef<u64>(p + 0x10) = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(alpha) << 24)));
    GRef<u64>(p + 0x18) = 0x2007D02511407E80ull;
    GRef<u64>(p) = 0x2400000000008001ull;
    GRef<u64>(p + 8) = 0x71;
    p = BuildSpriteStripsUv(p + 0x20, 0x80008000, xy1, 0x90008,
                            static_cast<u64>(static_cast<s64>(tw + 8)) | 0x1E090000u);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = cols | 0xFF00000000000000ull;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p) = 0x1123400000008003ull;
    GRef<u64>(p + 0x20) = 0x58;
    GRef<u64>(p + 0x28) = 0x43;
    GRef<u64>(p + 0x30) = 0x201000;
    GRef<u64>(p + 0x38) = 1;
    p = BuildSpriteStrips(p + 0x40, 0x80008000, xy1);
    p = BuildScanlineBands(p, Ctx()->frame_count, static_cast<s32>(w), static_cast<s32>(h));
    const u32 end = CloseDmaPacket(pkt, p);
    LinkPacket(list, index, start, end);
}

// EE: 0x00228750
// Draws the tabs with the master level levels[0] as FIX alpha.
void DrawMenuTabs(u32 list, s32 index, u32 levels) {
    s32 alpha = fpu::FloatToInt(GRef<f32>(levels) * 128.0f);
    if (alpha <= 0) return;
    const u32 start = Ctx()->cur_buf.addr;
    if (alpha > 0x80) alpha = 0x80;
    const u32 tag = WriteDmaCallTag(start, kMenuSpritePacket);
    const u32 p = BuildFrameDrawEnv(tag + 0x10);
    Q(p, 1) = 0xE;
    Q(p, 2) = Sx(alpha) << 0x20 | 0x68;
    Q(p, 0) = 0x1000000000008001ull;
    Q(p, 3) = 0x43;
    const u32 end = BuildMenuTabs(p + 0x20, levels);
    LinkPacket(list, index, start, CloseDmaPacket(tag, end));
}

// EE: 0x00226D08
// Menu fade frame: the frame filter at full strength, the tabs and the darkening fade.
void DrawMenuFade(u32 list, s32 index, u32 fade) {
    DrawMenuFrameFilter(1.0f, list, index, 1);
    DrawMenuTabs(list, index, fade + 0x14);
    DrawBlackFade(GRef<f32>(fade + 0xC), list, index);
}

// EE: 0x00226280
// Builds a one-bucket list at `list` for this frame and draws the menu fade frame into it.
void DrawMenuFadeFrame(u32 list, u32 fade) {
    InitBucketList(list, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), list);
    DrawMenuFade(list, 0, fade);
}

// ---- List widget --------------------------------------------------------------------------------

// EE: 0x00227060
void ResetConnectorAlphas(u32 conns, s32 count) {
    for (; count > 0; --count) {
        GRef<u32>(conns + 0xC) = 0;
        conns += 0x14;
    }
}

// EE: 0x00227090
// Connector alpha: the one leading to the active column grows towards 1, the others fade out.
void UpdateConnectorAlphas(u32 conns, s32 count, s32 active) {
    const f32 grow = K(0x00410074);
    const f32 limit = K(0x00410078);
    const f32 decay = K(0x0041007C);
    const f32 epsilon = K(0x00410080);
    for (s32 i = 0; i < count; ++i, conns += 0x14) {
        f32& a = GRef<f32>(conns + 0xC);
        if (i == active) {
            if (a < 1.0f) {
                const f32 v = fpu::Add(fpu::Sub(a, 1.0f) * grow, 1.0f);
                a = limit < std::fabs(v) ? 1.0f : v;
            }
        } else if (0.0f < a) {
            const f32 v = a * decay;
            a = std::fabs(v) < epsilon ? 0.0f : v;
        }
    }
}

// EE: 0x00227140
// Closes `count` list widgets and clears their item scales.
void ResetListWidgets(u32 w, s32 count) {
    for (s32 i = 0; i < count; ++i, w += 0x18) {
        const s16 n = GRef<s16>(w + 4);
        GRef<u16>(w + 0xA) = 0;
        GRef<u32>(w + 0xC) = 0;
        for (s32 j = 0; j < n; ++j) GRef<u32>(GRef<u32>(w + 0x10) + 4u * static_cast<u32>(j)) = 0;
    }
}

// EE: 0x002271B8
// List widget command: 1 open (from closed/closing: scales 1, the cursor item 2, type-in restarts),
// 2 mark open (unless opening), 3 close (from opening/open).
void SetListWidgetState(u32 w, s32 cmd) {
    const s16 n = GRef<s16>(w + 4);
    const s16 cursor = GRef<s16>(w + 6);
    s16& state = GRef<s16>(w + 0xA);
    if (cmd == 2) {
        if (state != 1) state = 2;
    } else if (cmd == 1) {
        if (state == 0 || state == 3) {
            state = 1;
            GRef<u32>(w + 0xC) = 0;
            const u32 scales = GRef<u32>(w + 0x10);
            for (s32 j = 0; j < n; ++j) GRef<f32>(scales + 4u * static_cast<u32>(j)) = 1.0f;
            GRef<f32>(scales + 4u * static_cast<u32>(static_cast<s32>(cursor))) = 2.0f;
        }
    } else if (cmd == 3) {
        if (static_cast<u32>(static_cast<u16>(state)) - 1u < 2u) state = 3;
    }
}

// EE: 0x00227298
// Per frame: type-in progress (SE 9 every third frame while typing), cursor movement with wrap
// (pad bits 1/2 when `active`, SE 1) and the item scales (cursor item 2, others back to 1); while
// closing the scales shrink to 0 and the widget closes.
void UpdateListWidget(u32 w, s64 active, u32 pad) {
    const s16 n = GRef<s16>(w + 4);
    s16 cursor = GRef<s16>(w + 6);
    s16 state = GRef<s16>(w + 0xA);
    if (state == 1 || state == 3) {
        s32 longest = 0;
        for (s32 i = 0; i < n; ++i) {
            const s32 len = static_cast<s32>(ee::strlen(GRef<u32>(GRef<u32>(w + 0x14) + 4u * static_cast<u32>(i))));
            if (longest < len) longest = len;
        }
        f32 progress = GRef<f32>(w + 0xC);
        if (progress < static_cast<f32>(longest + 3)) {
            progress = fpu::Add(progress, K(0x00410084));
            GRef<f32>(w + 0xC) = progress;
        } else if (GRef<s16>(w + 0xA) == 1) {
            GRef<u16>(w + 0xA) = 2;
        }
        if (progress <= static_cast<f32>(longest) && Ctx()->frame_count % 3 == 0) PlayMenuSe(0, 9);
        state = GRef<s16>(w + 0xA);
    }
    if (state <= 0) return;
    if (state < 3) {
        if (active != 0 && n != 0 && (pad & 3) != 0) {
            s32 c = cursor - static_cast<s32>(pad & 1);
            if ((pad & 2) != 0) c = c + 1;
            c = (c + n) % n;
            cursor = static_cast<s16>(c);
            GRef<s16>(w + 6) = cursor;
            PlayMenuSe(0, 1);
        }
        const u32 scales = GRef<u32>(w + 0x10);
        for (s32 i = 0; i < n; ++i) {
            f32& s = GRef<f32>(scales + 4u * static_cast<u32>(i));
            if (i == cursor) {
                s = 2.0f;
            } else {
                f32 d = fpu::Sub(s, 1.0f) * K(0x00410088);
                if (std::fabs(d) < K(0x0041008C)) d = 0.0f;
                s = fpu::Add(d, 1.0f);
            }
        }
    } else if (state == 3) {
        bool closed = true;
        const u32 scales = GRef<u32>(w + 0x10);
        for (s32 i = 0; i < n; ++i) {
            f32& s = GRef<f32>(scales + 4u * static_cast<u32>(i));
            f32 v = s * K(0x00410090);
            if (std::fabs(v) < K(0x00410094)) {
                v = 0.0f;
            } else {
                closed = false;
            }
            s = v;
        }
        if (closed) GRef<u16>(w + 0xA) = 0;
    }
}

// EE: 0x00227530
// Updates `count` widgets; only the `active` one takes the pad.
void UpdateListWidgets(u32 w, s32 count, s32 active, u32 pad) {
    for (s32 i = 0; i < count; ++i, w += 0x18) UpdateListWidget(w, i == active ? 1 : 0, pad);
}

// EE: 0x00228870
// One menu string: a glyph sprite per character (font descriptors 0x0039BE20, stride 0x10, codes
// 0x1F..0x61) for the first `count` characters. `scale` above 1 lights the item (two extra layers);
// the last 4 typed characters ease in through a sine.
u32 BuildMenuString(f32 count, f32 scale, u32 p, s32 x, s32 y, u32 str) {
    if (count <= 0.0f) return p;
    if (GRef<s8>(str) == 0) return p;
    f32 i = 0.0f;
    if (!(i < count)) return p;
    u8 c = GRef<u8>(str);
    for (;;) {
        u32 glyph = static_cast<u32>(c) - 0x1F;
        if (glyph >= 0x43) glyph = 0x42;
        const u32 desc = 0x0039BE20 + glyph * 0x10;
        const u32 shadow = 0x0039C250 + glyph * 0x10;
        const s32 half = GRef<u16>(desc) >> 1;
        x += half;
        if (static_cast<s32>(glyph) >= 2) {
            f32 s = fpu::Sub(i, fpu::Sub(count, 4.0f));
            f32 level;
            if (0.0f <= s) {
                const f32 e = fpu::Sub(1.0f, ee::sinf(fpu::Sub(4.0f, s) * K(0x004100BC)));
                level = fpu::Add(scale * fpu::Sub(1.0f, e), fpu::Add(e, e));
            } else {
                level = scale;
            }
            f32 a = level;
            if (1.0f < a) a = fpu::Sub(2.0f, a);
            if (0.0f < a) {
                Q(p, 1) = 0xE;
                Q(p, 0) = 0x1000000000008001ull;
                Q(p, 3) = 0x43;
                Q(p, 2) = Sx(fpu::FloatToInt(a * 128.0f)) << 0x20 | 0x68;
                p = BuildCenteredTexSprite(p + 0x20, x, y, 0x765834, desc, kTex0Glyphs);
            }
            a = fpu::Sub(level, 1.0f);
            if (0.0f < a) {
                if (1.0f < a) a = 1.0f;
                Q(p, 1) = 0xE;
                Q(p, 0) = 0x1000000000008001ull;
                Q(p, 3) = 0x43;
                Q(p, 2) = Sx(fpu::FloatToInt(a * 128.0f)) << 0x20 | 0x68;
                p = BuildCenteredTexSprite(p + 0x20, x, y, 0x101010, shadow, kTex0Panel);
                p = BuildCenteredSprite(p, x, y, 0x383838, 0x0039BDA0 + GRef<u32>(shadow + 4) * 0x10);
                p = BuildCenteredTexSprite(p, x, y, 0xABABAB, desc, kTex0Glyphs);
            }
        }
        ++str;
        x = x + 0x30 + half;
        i = fpu::Add(i, 1.0f);
        if (GRef<s8>(str) == 0) break;
        if (!(i < count)) break;
        c = GRef<u8>(str);
    }
    return p;
}

// EE: 0x00228BA8
// Draws `count` list widgets (their strings at x*16+0x6C00, y*16+0x7268, rows 0x180 apart).
void DrawListWidgets(u32 list, s32 index, u32 w, s32 count) {
    if (count <= 0) return;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, kMenuSpritePacket);
    const u32 first = BuildMenuDrawEnv(tag + 0x10, 0x280, 0x1C0);
    u32 p = first;
    for (s32 i = 0; i < count; ++i, w += 0x18) {
        const s16 state = GRef<s16>(w + 0xA);
        if (state == 0) continue;
        const s32 x = GRef<s16>(w) * 0x10 + 0x6C00;
        s32 y = GRef<s16>(w + 2) * 0x10 + 0x7268;
        const s16 n = GRef<s16>(w + 4);
        f32 typed = K(0x004100C0);
        if (state != 2) typed = GRef<f32>(w + 0xC);
        const f32 pulse = fpu::Add(GetMenuPulse() * K(0x004100C4), K(0x004100C8));
        for (s32 j = 0; j < n; ++j) {
            f32 s = GRef<f32>(GRef<u32>(w + 0x10) + 4u * static_cast<u32>(j));
            if (1.0f < s) s = fpu::Add(fpu::Sub(s, 1.0f) * pulse, 1.0f);
            const u32 str = GRef<u32>(GRef<u32>(w + 0x14) + 4u * static_cast<u32>(j));
            p = BuildMenuString(typed, s, p, x, y, str);
            y += 0x180;
        }
    }
    if (p == first) return;
    LinkPacket(list, index, start, CloseDmaPacket(tag, p));
}

// ---- Menu tree ----------------------------------------------------------------------------------

// EE: 0x00228D80
// Connector layout: row positions `ys` (count + 1 entries: the child rows from `y_child`, 0x180
// apart, with the parent row `y_parent` inserted once in order; the last row + 0x10) and per
// segment the largest child scale (`levels`, plus the overall largest at levels[count]).
void LayoutConnector(s32 count, s32 y_parent, s32 y_child, u32 scales, u32 ys, u32 levels) {
    s32 parent_at = -1;
    s32 i = 0;
    if (count >= 0) {
        do {
            if (parent_at < 0 && (!(y_child < y_parent) || i == count)) {
                parent_at = i;
                GRef<s32>(ys + 4u * static_cast<u32>(i)) = y_parent;
                ++i;
            }
            GRef<s32>(ys + 4u * static_cast<u32>(i)) = y_child;
            ++i;
            y_child += 0x180;
        } while (!(count < i));
    }
    GRef<s32>(ys + 4u * static_cast<u32>(count)) += 0x10;
    for (s32 k = 0; k < count; ++k) {
        s32 from = k;
        s32 to = count;
        if (k < parent_at) {
            from = 0;
            to = k + 1;
        }
        f32 m = 0.0f;
        for (s32 j = from; j < to; ++j) {
            const f32 v = GRef<f32>(scales + 4u * static_cast<u32>(j));
            if (m < v) m = v;
        }
        GRef<f32>(levels + 4u * static_cast<u32>(k)) = m;
    }
    f32 m = 0.0f;
    for (s32 j = 0; j < count; ++j) {
        const f32 v = GRef<f32>(scales + 4u * static_cast<u32>(j));
        if (m < v) m = v;
    }
    GRef<f32>(levels + 4u * static_cast<u32>(count)) = m;
}

// EE: 0x00228EB8
// Connector colour for scale `s` and alpha `a`: black -> 0x765834 -> 0xABABAB.
u64 GetConnectorColor(f32 s, f32 a) {
    a = fpu::Add(a, a);
    f32 v = s;
    if (a < v) v = a;
    if (v < 1.0f) return LerpColor(v, 0, 0x765834);
    a = fpu::Add(a, a);
    if (a < 1.0f) {
        v = 1.0f;
    } else if (a < v) {
        v = a;
    }
    return LerpColor(fpu::Sub(v, 1.0f), 0x765834, 0xABABAB);
}

// EE: 0x00228F60
// Connector end cap: a glow sprite along a horizontal (h < w) or vertical segment.
u32 BuildConnectorCap(f32 s, f32 pulse, u32 p, s64 x, s64 y, s64 w, s64 h) {
    u64 color;
    if (s < 1.0f) {
        color = LerpColor(s, 0, 0x80808);
    } else {
        color = LerpColor(fpu::Sub(s, 1.0f) * pulse, 0x80808, 0xA0A0A0);
    }
    if (color == 0) return p;
    const s32 xi = static_cast<s32>(x);
    const s32 yi = static_cast<s32>(y);
    if (h < w) {
        const u64 x_end = Sx(xi + static_cast<s32>(w));
        const u64 top = Sx(yi - 0x50) << 0x10;
        const u64 bottom = Sx(yi + 0x50) << 0x10;
        Q(p, 0) = 0xE400000000008001ull;
        Q(p, 1) = 0x0053D353D353D310ull;
        Q(p, 2) = 0x356;
        Q(p, 3) = Lo32(color);
        Q(p, 4) = 0xB6804E8;
        Q(p, 5) = Sx(xi - 0x40) | top;
        Q(p, 6) = 0xBF80540;
        Q(p, 7) = static_cast<u64>(x) | bottom;
        Q(p, 8) = 0xB680540;
        Q(p, 9) = static_cast<u64>(x) | top;
        Q(p, 0xA) = 0xBF80540;
        Q(p, 0xB) = x_end | bottom;
        Q(p, 0xC) = 0xB680540;
        Q(p, 0xD) = x_end | top;
        Q(p, 0xE) = 0xBF80598;
        Q(p, 0xF) = Sx(xi + static_cast<s32>(w) + 0x40) | bottom;
        return p + 0x80;
    }
    Q(p, 0) = 0x6400000000008001ull;
    Q(p, 1) = 0x53D310;
    Q(p, 2) = 0x356;
    Q(p, 3) = Lo32(color);
    Q(p, 4) = 0xBB004E8;
    Q(p, 5) = Sx(xi - 0x40) | static_cast<u64>(y) << 0x10;
    Q(p, 6) = 0xBB00598;
    Q(p, 7) = Sx(xi + 0x40) | Sx(yi + static_cast<s32>(h)) << 0x10;
    return p + 0x40;
}

// EE: 0x00229158
// One connector: line segments (parent row -> trunk, trunk, trunk -> child rows) coloured by the
// child scales, then the glow caps.
u32 BuildConnector(f32 alpha, u32 p, u32 conn) {
    const s32 n = GRef<s16>(conn + 0xA);
    if (n <= 0) return p;
    guest::GuestFrame frame(0x80);
    const u32 ys = frame.Address();
    const u32 levels = frame.Address() + 0x40;
    const s32 y_parent = GRef<s16>(conn + 2) * 0x10 + 0x7200;
    const s32 y_child0 = GRef<s16>(conn + 6) * 0x10 + 0x7200;
    LayoutConnector(n, y_parent, y_child0, GRef<u32>(conn + 0x10), ys, levels);
    const s32 x0 = GRef<s16>(conn) * 0x10 + 0x6C00;
    const s32 x1 = GRef<s16>(conn + 4) * 0x10 + 0x6C00;
    const s32 x2 = GRef<s16>(conn + 8) * 0x10 + 0x6C00;
    Q(p, 0) = 0x1125400000008001ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    u32 q = p + 0x20;
    Q(q, 0) = static_cast<u64>(static_cast<u32>(n << 1) | 0x8001) | 0x3400000000000000ull;
    Q(q, 1) = 0x5D1;
    Q(q, 2) = Lo32(GetConnectorColor(GRef<f32>(levels + 4u * static_cast<u32>(n)), alpha));
    Q(q, 4) = Sx(x1) | Sx(y_parent) << 0x10;
    Q(q, 3) = Sx(x0) | Sx(y_parent) << 0x10;
    u32 e = q + 0x28;
    for (s32 i = 0; i < n; ++i, e += 0x18) {
        const u64 c = GetConnectorColor(GRef<f32>(levels + 4u * static_cast<u32>(i)), alpha);
        const s32 ya = GRef<s32>(ys + 4u * static_cast<u32>(i));
        const s32 yb = GRef<s32>(ys + 4u * static_cast<u32>(i + 1));
        GRef<u64>(e) = Lo32(c);
        GRef<u64>(e + 8) = Sx(x1) | Sx(ya) << 0x10;
        GRef<u64>(e + 0x10) = Sx(x1) | Sx(yb) << 0x10;
    }
    s32 y_row = y_child0;
    for (s32 i = 0; i < n; ++i, e += 0x18) {
        const u64 c = GetConnectorColor(GRef<f32>(GRef<u32>(conn + 0x10) + 4u * static_cast<u32>(i)), alpha);
        GRef<u64>(e) = Lo32(c);
        GRef<u64>(e + 0x10) = Sx(x2) | Sx(y_row) << 0x10;
        GRef<u64>(e + 8) = Sx(x1 + 0x10) | Sx(y_row) << 0x10;
        y_row += 0x180;
    }
    // The register list above has an odd number of words: GRef(e) is the unwritten padding word.
    const f32 pulse = fpu::Add(GetMenuPulse() * K(0x004100CC), K(0x004100D0));
    GRef<u64>(e + 8) = 0x1400000000008001ull;
    GRef<u64>(e + 0x10) = 7;
    e += 0x18;
    GRef<u64>(e) = kTex0Panel;
    u32 out = BuildConnectorCap(GRef<f32>(levels + 4u * static_cast<u32>(n)), pulse, e + 0x10, x0, y_parent, x1 - x0, 0);
    const s32 y1 = GRef<s16>(conn + 6) * 0x10 + 0x7200;
    s32 y_cap = y1;
    for (s32 i = 0; i < n; ++i) {
        const s32 ya = GRef<s32>(ys + 4u * static_cast<u32>(i));
        const s32 yb = GRef<s32>(ys + 4u * static_cast<u32>(i + 1));
        out = BuildConnectorCap(GRef<f32>(levels + 4u * static_cast<u32>(i)), pulse, out, x1, ya, 0, yb - ya);
        out = BuildConnectorCap(GRef<f32>(GRef<u32>(conn + 0x10) + 4u * static_cast<u32>(i)), pulse, out, x1, y_cap,
                                x2 - x1, 0);
        y_cap += 0x180;
    }
    return out;
}

// EE: 0x002294F8
// Draws the connectors with a positive alpha.
void DrawConnectors(u32 list, s32 index, u32 conns, s32 count) {
    if (count <= 0) return;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, kMenuSpritePacket);
    const u32 first = BuildMenuDrawEnv(tag + 0x10, 0x280, 0x1C0);
    u32 p = first;
    for (; count != 0; --count, conns += 0x14) {
        const f32 alpha = GRef<f32>(conns + 0xC);
        if (alpha <= 0.0f) continue;
        p = BuildConnector(alpha, p, conns);
    }
    if (p == first) return;
    LinkPacket(list, index, start, CloseDmaPacket(tag, p));
}

// ---- Frame outlines and crosshair cursor (S2b) --------------------------------------------------
// Frame rectangle (stride 0x10): +0 u16 centre x, +2 u16 centre y, +4 u16 width, +6 u16 height
// (GS units), +8 u32 colour.
// Crosshair cursor: +0 u16 moving axes (bit 0 x, bit 1 y), +2 u16 distance, +4 u16 step, +6 u16
// steps, +8/+0xA u16 per-axis "moved" history (shifted each frame), +0xC u16 from x/y, +0x10 u16
// target x/y, +0x14 u16 current x/y, +0x18 previous positions (9 x u32, newest first).

// EE: 0x002295F0
// One frame outline (closed line strip, alpha blended) with its open animation at time `t`: grows
// from nothing until 0.75, then overshoots by 128 (narrower until 0.875, wider until 1.0) and
// settles at its size. Nothing is drawn for t < 0 or inside the gap (K 0x004100D4, K 0x004100D8).
u32 BuildFrameOutline(f32 t, u32 p, u32 rect, u32 color) {
    if (t < 0.0f) return p;
    if (K(0x004100D4) < t && t < K(0x004100D8)) return p;
    f32 w = static_cast<f32>(static_cast<s32>(GRef<u16>(rect + 4)));
    f32 h = static_cast<f32>(static_cast<s32>(GRef<u16>(rect + 6)));
    if (t < 0.75f) {
        const f32 s = t * K(0x004100DC);
        h = h * s;
        w = w * s;
    } else if (t < 0.875f) {
        const f32 s = fpu::Sub(t, K(0x004100E0)) * 10.0f;
        const f32 r = fpu::Sub(1.0f, s);
        const f32 hs = h * s;
        const f32 ws = w * s;
        h = fpu::Add(fpu::Sub(h, 128.0f) * r, hs);
        w = fpu::Add(fpu::Sub(w, 128.0f) * r, ws);
    } else if (t < 1.0f) {
        const f32 s = fpu::Sub(t, K(0x004100E4)) * 10.0f;
        const f32 r = fpu::Sub(1.0f, s);
        const f32 hs = h * s;
        const f32 ws = w * s;
        h = fpu::Add(fpu::Add(h, 128.0f) * r, hs);
        w = fpu::Add(fpu::Add(w, 128.0f) * r, ws);
    }
    const s32 hi = fpu::FloatToInt(h);
    const s32 wi = fpu::FloatToInt(w);
    const s32 top = static_cast<s32>(GRef<u16>(rect + 2)) - (hi >> 1);
    const s32 left = static_cast<s32>(GRef<u16>(rect)) - (wi >> 1);
    const s32 bottom = hi + top;
    const s32 right = wi + left;
    Q(p, 1) = 0x5555D10;
    Q(p, 2) = 0x242;
    Q(p, 0) = 0x7400000000008001ull;
    Q(p, 7) = Sx(left) | Sx(bottom) << 0x10;
    Q(p, 3) = color;
    Q(p, 4) = Sx(left) | Sx(top) << 0x10;
    Q(p, 5) = Sx(right) | Sx(top) << 0x10;
    Q(p, 6) = Sx(right) | Sx(bottom) << 0x10;
    Q(p, 8) = Sx(left) | Sx(top) << 0x10;
    return p + 0x50;
}

// EE: 0x002297B8
// Frame outlines `count` of `rects` whose bit is set in `mask`, each at its time in `times`, into
// bucket `index` of `list` (ALPHA 0x68, FIX 0x80).
void DrawFrameOutlines(u32 list, s32 index, s32 count, u32 rects, u32 times, u64 mask) {
    if (!(count > 0)) return;
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildFrameDrawEnv(start + 0x10);
    Q(p, 0) = 0x1000000000008001ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    p += 0x20;
    for (s32 n = count; n != 0; --n, times += 4, rects += 0x10, mask >>= 1) {
        if ((mask & 1) != 0) p = BuildFrameOutline(GRef<f32>(times), p, rects, GRef<u32>(rects + 8));
    }
    LinkPacket(list, index, start, CloseDmaPacket(start, p));
}

// EE: 0x002298E8
// All `count` frame outlines.
void DrawAllFrameOutlines(u32 list, s32 index, s32 count, u32 rects, u32 times) {
    DrawFrameOutlines(list, index, count, rects, times, (1ull << (static_cast<u32>(count) & 0x3F)) - 1);
}

// EE: 0x002298F8
// Cursor at rest at GS (0x6BF0, 0x71F0), with the whole trail there.
void InitCrosshair(u32 c) {
    GRef<u16>(c) = 0;
    GRef<u16>(c + 6) = 0;
    GRef<u16>(c + 4) = 0;
    GRef<u32>(c + 8) = 0;
    GRef<u32>(c + 0x10) = 0x71F06BF0u;
    GRef<u32>(c + 0xC) = 0x71F06BF0u;
    for (s32 i = 9; i >= 0; --i) GRef<u32>(c + 0x38 - 4u * static_cast<u32>(9 - i)) = 0x71F06BF0u;
}

// EE: 0x00229948
// Starts a move to screen position (`x`, `y`) (offset by 0x6C00/0x7200) in `steps` frames (at
// least 1) from the current position; an axis that changes is marked in its history.
void MoveCrosshair(u32 c, s32 steps, s32 x, s32 y) {
    const s32 n = 0 < steps ? steps : 1;
    const u32 tx = static_cast<u32>(x + 0x6C00);
    GRef<u32>(c + 0xC) = GRef<u32>(c + 0x14);
    const u32 ty = static_cast<u32>(y + 0x7200);
    GRef<u16>(c + 6) = static_cast<u16>(n);
    u32 axes = 0;
    const u32 fx = GRef<u16>(c + 0xC);
    GRef<u16>(c + 0x10) = static_cast<u16>(tx);
    GRef<u16>(c + 0x12) = static_cast<u16>(ty);
    GRef<u16>(c + 4) = 0;
    if (tx != fx) {
        GRef<u16>(c + 8) |= 1;
        axes = 1;
    }
    const u32 fy = GRef<u16>(c + 0xE);
    if (ty != fy) {
        axes |= 2;
        GRef<u16>(c + 0xA) |= 1;
    }
    s32 dy = static_cast<s32>(fy - GRef<u16>(c + 0x12));
    GRef<u16>(c) = static_cast<u16>(axes);
    s32 dx = static_cast<s32>(fx - GRef<u16>(c + 0x10));
    if (!(-1 < dy)) dy = -dy;
    if (!(-1 < dx)) dx = -dx;
    if (dx < dy) dx = dy;
    GRef<u16>(c + 2) = static_cast<u16>(dx);
}

// EE: 0x002299F0
// Move to (`x`, `y`) with a step count from the distance: distance / 0x120 frames, at least 4.
void MoveCrosshairAuto(u32 c, s32 x, s32 y) {
    s32 dy = (y - static_cast<s32>(GRef<u16>(c + 0x16))) + 0x7200;
    s32 dx = (x - static_cast<s32>(GRef<u16>(c + 0x14))) + 0x6C00;
    if (!(-1 < dx)) dx = -dx;
    if (!(-1 < dy)) dy = -dy;
    if (dx < dy) dx = dy;
    const s32 q = dx / 0x120;
    MoveCrosshair(c, 3 < q ? q : 4, x, y);
}

// EE: 0x00229A60
// Per frame: shifts the moved history and the trail, then moves each active axis by
// step / steps * distance towards its target (clamped there); the move ends after `steps` steps.
void UpdateCrosshair(u32 c) {
    const u32 history = GRef<u32>(c + 8);
    if (history != 0) {
        GRef<u32>(c + 8) = (history << 1) & 0x03FF03FFu;
        for (u32 off = 0x38; off != 0x14; off -= 4) GRef<u32>(c + off) = GRef<u32>(c + off - 4);
    }
    const u32 axes = GRef<u16>(c);
    if (axes == 0) return;
    const u16 step = static_cast<u16>(GRef<u16>(c + 4) + 1);
    GRef<u16>(c + 4) = step;
    if (!(step < GRef<u16>(c + 6))) {
        GRef<u16>(c) = 0;
        GRef<u32>(c + 0x14) = GRef<u32>(c + 0x10);
        return;
    }
    const f32 steps = static_cast<f32>(static_cast<s32>(GRef<u16>(c + 6)));
    const f32 cur = static_cast<f32>(static_cast<s32>(GRef<u16>(c + 4)));
    const f32 dist = static_cast<f32>(static_cast<s32>(GRef<u16>(c + 2)));
    const f32 d = (cur / steps) * dist;
    for (u32 k = 0; k < 2; ++k) {
        if (((static_cast<s32>(axes) >> k) & 1) == 0) continue;
        const f32 from = static_cast<f32>(static_cast<s32>(GRef<u16>(c + 0xC + 2 * k)));
        const f32 to = static_cast<f32>(static_cast<s32>(GRef<u16>(c + 0x10 + 2 * k)));
        f32 v;
        if (from < to) {
            v = fpu::Add(from, d);
            if (to < v) {
                v = to;
            } else {
                GRef<u16>(c + 8 + 2 * k) |= 1;
            }
        } else {
            v = fpu::Sub(from, d);
            if (v < to) {
                v = to;
            } else {
                GRef<u16>(c + 8 + 2 * k) |= 1;
            }
        }
        GRef<u16>(c + 0x14 + 2 * k) = static_cast<u16>(fpu::FloatToInt(v));
    }
}

// EE: 0x00229BC0
// The crosshair: for the current position and each trail position whose axis moved recently, a
// full-height vertical line (x) and/or a full-width horizontal line (y), colour 0x5E4021 fading
// towards black along the trail (step K 0x004100E8); one REGLIST tag of RGBAQ/XYZ2/XYZ2 lines.
void DrawCrosshair(u32 list, s32 index, u32 c) {
    const u32 start = Ctx()->cur_buf.addr;
    f32 t = 0.0f;
    u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    Q(p, 0) = 0x1125400000008001ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    p += 0x20;
    u32 x_bits = static_cast<u32>(GRef<u16>(c + 8)) << 1 | 1;
    u32 y_bits = static_cast<u32>(GRef<u16>(c + 0xA)) << 1 | 1;
    u32 bits = x_bits | y_bits;
    u32 q = p + 0x10;
    u32 lines = 0;
    if (bits != 0) {
        const f32 step = K(0x004100E8);
        u32 pos = c + 0x16;
        for (s32 i = 0;;) {
            if ((bits & 1) != 0) {
                const u64 color = LerpColor(t, 0x5E4021, 0);
                if ((x_bits & 1) != 0) {
                    const u64 x = GRef<u16>(pos - 2);
                    ++lines;
                    GRef<u64>(q) = Lo32(color);
                    GRef<u64>(q + 0x10) = x | 0x8E000000ull;
                    GRef<u64>(q + 8) = 0x72000000ull | x;
                    q += 0x18;
                }
                if ((y_bits & 1) != 0) {
                    const u64 y = GRef<u16>(pos);
                    GRef<u64>(q) = Lo32(color);
                    ++lines;
                    GRef<u64>(q + 0x10) = y << 0x10 | 0x9400;
                    GRef<u64>(q + 8) = y << 0x10 | 0x6C00;
                    q += 0x18;
                }
            }
            ++i;
            pos += 4;
            bits >>= 1;
            x_bits >>= 1;
            y_bits >>= 1;
            t = fpu::Add(t, step);
            if (!(i < 10) || bits == 0) break;
        }
    }
    u32 end = p;
    if (lines != 0) {
        Q(p, 1) = 0x5D1;
        Q(p, 0) = static_cast<u64>(lines | 0x8000) | 0xD000ull << 46;
        end = (lines & 1) == 0 ? q : q + 8;
    }
    LinkPacket(list, index, start, CloseDmaPacket(start, end));
}

// EE: 0x00229DD0
// Opens the menu text layer: later strings (DrawMenuText) go to bucket `index` of `list`, offset
// to the centre of the draw buffer; emits the sprite GS setup (call tag to 0x00385C90).
void BeginMenuTextLayer(u32 list, s32 index) {
    MainContext* ctx = Ctx();
    const u32 start = ctx->cur_buf.addr;
    g_MenuTextList() = list;
    g_MenuTextIndex() = static_cast<u32>(index);
    g_MenuTextX() = 0x8000 - (static_cast<u32>(ctx->draw_w) << 3);
    g_MenuTextY() = 0x8000 - (static_cast<u32>(ctx->draw_h) << 3);
    const u32 p = WriteDmaCallTag(start, kMenuSpritePacket);
    LinkPacket(list, index, start, CloseDmaPacket(p, BuildFrameDrawEnv(p + 0x10)));
}

// EE: 0x00229E80
void EndMenuTextLayer() { g_MenuTextList() = 0; }

namespace {

// Shared body of DrawMenuText / DrawMenuTextColored (style 1 pulses with K pulse_scale/offset).
void DrawTextInLayer(f32 count, s32 x, s32 y, s64 style, u32 str, u32 pulse_scale, u32 pulse_offset, u32 color,
                     u32 color2) {
    if (g_MenuTextList() == 0) return;
    const u32 start = Ctx()->cur_buf.addr;
    x += static_cast<s32>(g_MenuTextX());
    u32 p = start + 0x10;
    y += static_cast<s32>(g_MenuTextY());
    if (style == 1) {
        y += 0x88;
        const f32 scale = fpu::Add(GetMenuPulse() * K(pulse_scale), K(pulse_offset));
        p = BuildMenuString(count, scale, p, x, y, str);
    } else if (!(style < 2)) {
        if (style < 0xC) p = BuildStyledString(count, p, x, y, str, static_cast<u64>(style), color, color2);
    } else if (style == 0) {
        p = BuildMenuString(count, 1.0f, p, x, y + 0x68, str);
    }
    if (p == start + 0x10) return;
    LinkPacket(g_MenuTextList(), static_cast<s32>(g_MenuTextIndex()), start, CloseDmaPacket(start, p));
}

}  // namespace

// EE: 0x00229E88
// One string into the open text layer: style 0 the menu font, style 1 the menu font pulsing,
// styles 2-11 a styled font in the default colours (0xC1B199, shadow 0x606060).
void DrawMenuText(f32 count, s32 x, s32 y, s64 style, u32 str) {
    DrawTextInLayer(count, x, y, style, str, 0x004100EC, 0x004100F0, 0xC1B199, 0x606060);
}

// EE: 0x00229FE0
// The same with colour `color` for styles 2-11 (also the shadow blend target).
void DrawMenuTextColored(f32 count, s32 x, s32 y, s64 style, u32 str, u32 color) {
    DrawTextInLayer(count, x, y, style, str, 0x004100F4, 0x004100F8, color, color);
}

// EE: 0x0022A158
// Glyph descriptor of character `ch` in font style `style` (0-11, else 0): style table 0x0039D420
// {u16 first code, u16 last index, u32 glyphs (stride 0x10)}; out-of-range codes give the first
// glyph. A space is drawn with glyph code 0x1F in styles 6 and 7.
u32 GetStyleGlyph(u64 style, s32 ch) {
    s32 code = ch;
    if (ch == 0x20) {
        code = 0x1F;
        if (1u < static_cast<u32>(style - 6)) code = ch;
    }
    if (!(style < 0xC)) style = 0;
    const u32 e = 0x0039D420 + static_cast<u32>(style) * 8;
    u32 i = static_cast<u32>(code) - GRef<u16>(e);
    if (GRef<u16>(e + 2) < i) i = 0;
    return GRef<u32>(e + 4) + i * 0x10;
}

// EE: 0x0022A1B8
// The first `count` characters of `str` in font style `style` (table 0x0039D480: u16 line height,
// u8 spacing, u8 line gap, u32 shadow glyph), from (`x`, `y`); '\n' starts a new line. When the
// string is typed out to its end (count - 4 <= length) and the style has a shadow glyph, the last
// four typed characters get a shadow fading from 0x040404 towards `color2` (cosine ease).
u32 BuildStyledString(f32 count, u32 p, s32 x, s32 y, u32 str, u64 style, u32 color, u32 color2) {
    if (!(style < 0xC)) return p;
    s32 n = fpu::FloatToInt(count);
    if (!(n > 0)) return p;
    const u32 e = 0x0039D480 + static_cast<u32>(style) * 8;
    const s32 tail = fpu::FloatToInt(fpu::Sub(count, 4.0f));
    const u32 spacing = GRef<u8>(e + 2);
    s32 line = GRef<u16>(e);
    const s32 x0 = x;
    u32 shadow = 0;
    y += line >> 1;
    line += GRef<u8>(e + 3);
    const s32 len = static_cast<s32>(ee::strlen(str));
    if (!(len < tail)) shadow = GRef<u32>(e + 4);
    Q(p, 0) = 0x1000000000008002ull;
    if (len < n) n = len;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    Q(p, 4) = kTex0Glyphs;
    Q(p, 5) = 7;
    p += 0x30;
    for (s32 i = 0; i < n; ++i, ++str) {
        const s32 ch = GRef<s8>(str);
        if (ch == '\n') {
            x = x0;
            y += line;
            continue;
        }
        const u32 glyph = GetStyleGlyph(style, ch);
        const u32 hw = static_cast<u32>(GRef<u16>(glyph)) >> 1;
        x += static_cast<s32>(hw);
        if (!(ch < 0x21)) {
            if (shadow == 0) {
                p = BuildCenteredSprite(p, x, y, Sx(static_cast<s32>(color)), glyph);
            } else {
                if (!(i < tail)) {
                    const f32 a = fpu::Sub(4.0f, fpu::Sub(count, fpu::IntToFloat(i)));
                    const f32 f = ee::cosf(a * 0.25f * K(0x004100FC));
                    const u64 c2 = LerpColor(f, 0x40404, Sx(static_cast<s32>(color2)));
                    p = BuildCenteredTexSprite(p, x, y, c2, shadow, kTex0Panel);
                }
                p = BuildCenteredTexSprite(p, x, y, Sx(static_cast<s32>(color)), glyph, kTex0Glyphs);
            }
        }
        x += static_cast<s32>(hw + spacing);
    }
    return p;
}

// EE: 0x0022A6C0
// A box (sprite, alpha blended) at (`x`, `y`) of width `w` (height scaled by 0x1C0/0x1E0), inset
// 0x10 and shrunk by 0x20, whose colour pulses between black and `color` with `frame`
// (cosine of frac(frame * K 0x00410100)).
void DrawPulseBox(u32 list, s32 index, u32 color, s32 x, s32 y, s32 w, s32 frame) {
    const s32 x0 = x + 0x6C10;
    const s32 y0 = y + 0x7210;
    const s32 h = (w * 0x1C0) / 0x1E0 - 0x20;
    const s32 wd = w - 0x20;
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    f32 t = static_cast<f32>(frame) * K(0x00410100);
    Q(p, 0) = 0x1000000000000001ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    const f32 fl = ee::floorf(t);
    p += 0x20;
    t = fpu::Sub(t, fl);
    const f32 c = ee::cosf(t * K(0x00410104));
    const u64 col = LerpColor(fpu::Add(c * 0.5f, 0.5f), 0, Sx(static_cast<s32>(color)));
    Q(p, 0) = 0x4400000000008001ull;
    Q(p, 1) = 0x5D10;
    Q(p, 2) = 0x246;
    Q(p, 5) = Sx(x0 + wd) | Sx(y0 + h) << 0x10;
    Q(p, 4) = Sx(x0) | Sx(y0) << 0x10;
    Q(p, 3) = Lo32(col);
    LinkPacket(list, index, start, CloseDmaPacket(start, p + 0x30));
}

// EE: 0x0022A998
// A gauge at (`x`, `y`), `w` wide: frame (triangle fan in 0xD1C1A9) and a bar filled to
// `t` * (w - 0x20) (sprite, colour 0x1067 with ALPHA 0x64 FIX 0x40).
u32 BuildGauge(f32 t, u32 p, s32 x, s32 y, s32 w) {
    const f32 span = static_cast<f32>(w - 0x20);
    const s32 right = x + w;
    Q(p, 0) = 0x1125400000000001ull;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 1) = 0xE;
    Q(p, 3) = 0x43;
    const s32 fill = fpu::FloatToInt(t * span);
    p += 0x20;
    Q(p, 0) = 0x8400000000000001ull;
    Q(p, 1) = 0x5D5555D1;
    Q(p, 2) = 0xD1C1A9;
    Q(p, 5) = Sx(right) | Sx(y + 0x50) << 0x10;
    Q(p, 3) = Sx(x) | Sx(y) << 0x10;
    Q(p, 4) = Sx(right) | Sx(y) << 0x10;
    Q(p, 6) = Sx(x) | Sx(y + 0x50) << 0x10;
    Q(p, 7) = Sx(x) | Sx(y) << 0x10;
    Q(p, 9) = Sx(right - 0x28) | Sx(y + 0x30) << 0x10;
    Q(p, 8) = Sx(x + 0x30) | Sx(y + 0x30) << 0x10;
    p += 0x50;
    Q(p, 0) = 0x1123400000000001ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x4000000064ull;
    Q(p, 3) = 0x43;
    p += 0x20;
    Q(p, 0) = 0x3400000000008001ull;
    Q(p, 1) = 0x551;
    Q(p, 2) = 0x1067;
    Q(p, 4) = Sx(x + fill + 0x18) | Sx(y + 0x40) << 0x10;
    Q(p, 3) = Sx(x + 0x10) | Sx(y + 0x10) << 0x10;
    return p + 0x30;
}

// ---- Panel animation -------------------------------------------------------------------------
// Panel: +0 u16 state (1 opening, 2 closing, 4 open, 8 closed, 0x100/0x200/0x400 extra timers
// running, 0x4000 open SE played, 0x8000 type-out SE), +2 u16 parts (1 frame outlines,
// 2 crosshair, 4 timers a/b, 8 timer c), +4 s32 outline count (<= 16), +8 f32 time, +0xC f32
// outline times[16], +0x4C/+0x50/+0x54 f32 timers a/b/c, +0x58/+0x5A s16 crosshair start x/y,
// +0x5C frame rectangles (+0xC f32 start delay), +0x60 crosshair.

// EE: 0x0022B108
// Starts the opening animation from the beginning.
void ResetPanel(u32 panel) {
    GRef<u16>(panel) = 1;
    const s32 n = GRef<s32>(panel + 4);
    for (s32 i = 0; i < n; ++i) GRef<u32>(panel + 0xC + 4u * static_cast<u32>(i)) = 0;
    const u32 crosshair = GRef<u32>(panel + 0x60);
    GRef<u32>(panel + 8) = 0;
    GRef<u32>(panel + 0x4C) = 0;
    GRef<u32>(panel + 0x50) = 0;
    GRef<u32>(panel + 0x54) = 0;
    if (crosshair == 0) return;
    InitCrosshair(crosshair);
    const u32 c = GRef<u32>(panel + 0x60);
    GRef<u16>(panel + 0x58) = static_cast<u16>(GRef<u16>(c + 0xC) - 0x6C00);
    GRef<u16>(panel + 0x5A) = static_cast<u16>(GRef<u16>(c + 0xE) - 0x7200);
}

// EE: 0x0022B1A8
void InitPanel(u32 panel, u64 count, u32 rects, u32 crosshair) {
    const u64 n = count < 0x11 ? count : 0x10;
    GRef<u16>(panel + 2) = 1;
    GRef<s32>(panel + 4) = static_cast<s32>(n);
    GRef<u32>(panel + 0x5C) = rects;
    GRef<u32>(panel + 0x60) = crosshair;
    GRef<u16>(panel + 0x58) = 0;
    GRef<u16>(panel + 0x5A) = 0;
    ResetPanel(panel);
}

// EE: 0x0022B1E0
// Per frame: opening moves each outline's time to (time - its delay) * 6 (SE 0xD and the extra
// timers when the last outlines pass 0.75 / K 0x0041010C; open at 1); closing sets all outline
// times to 1 - time * 6 down to 0. Then the extra timers, the type-out SE every other frame and the
// crosshair.
void UpdatePanel(u32 panel) {
    u32 state = GRef<u16>(panel);
    const s32 n = GRef<s32>(panel + 4);
    const u32 parts = GRef<u16>(panel + 2);
    if (n > 0 && (state & 3) != 0) {
        const f32 time = fpu::Add(GRef<f32>(panel + 8), K(0x00410108));
        GRef<f32>(panel + 8) = time;
        const u32 times = panel + 0xC;
        if ((state & 1) != 0) {
            const u32 rects = GRef<u32>(panel + 0x5C);
            for (s32 i = 0; i < n; ++i) {
                GRef<f32>(times + 4u * static_cast<u32>(i)) =
                    fpu::Sub(time, GRef<f32>(rects + static_cast<u32>(i) * 0x10 + 0xC)) * 6.0f;
            }
            const s32 last = n - 1;
            const s32 k = last < 2 ? last : 2;
            if ((state & 0x300) != 0x300 && K(0x0041010C) <= GRef<f32>(times + 4u * static_cast<u32>(k))) state |= 0x300;
            if ((state & 0x4000) == 0 && 0.75f <= GRef<f32>(times + 4u * static_cast<u32>(last))) {
                PlayMenuSe(0, 0xD);
                state |= 0x4400;
            }
            if (1.0f <= GRef<f32>(times + 4u * static_cast<u32>(last))) state = (state & 0xFFFFFFFEu) | 4;
            GRef<u16>(panel) = static_cast<u16>(state);
        } else if ((state & 2) != 0) {
            f32 v = fpu::Sub(1.0f, time * 6.0f);
            if (v < 0.0f) {
                v = 0.0f;
                state = (state & 0xFFFFFFFDu) | 8;
            }
            for (s32 i = 0; i < n; ++i) GRef<f32>(times + 4u * static_cast<u32>(i)) = v;
            GRef<u16>(panel) = static_cast<u16>(state);
        } else {
            GRef<u16>(panel) = static_cast<u16>(state);
        }
    }
    if ((parts & 4) != 0) {
        if ((state & 0x100) != 0) GRef<f32>(panel + 0x4C) = fpu::Add(GRef<f32>(panel + 0x4C), K(0x00410110));
        if ((state & 0x200) != 0) GRef<f32>(panel + 0x50) = fpu::Add(GRef<f32>(panel + 0x50), K(0x00410114));
    }
    if ((parts & 8) != 0 && (state & 0x400) != 0) GRef<f32>(panel + 0x54) = fpu::Add(GRef<f32>(panel + 0x54), K(0x00410118));
    if ((state & 0x8000) != 0 && (Ctx()->frame_count & 1) == 0) PlayMenuSe(0, 9);
    const u32 crosshair = GRef<u32>(panel + 0x60);
    if (crosshair != 0 && (parts & 2) != 0) UpdateCrosshair(crosshair);
}

// EE: 0x0022B4D0
// 0: restart the opening; 1: start closing (frame outlines only).
void SetPanelState(u32 panel, s64 cmd) {
    if (cmd == 0) {
        ResetPanel(panel);
    } else if (cmd == 1) {
        GRef<u32>(panel + 8) = 0;
        GRef<u16>(panel) = 2;
        const f32 zero = GRef<f32>(panel + 8);
        GRef<u16>(panel + 2) = static_cast<u16>(cmd);
        GRef<f32>(panel + 0x4C) = zero;
        GRef<f32>(panel + 0x54) = zero;
        GRef<f32>(panel + 0x50) = zero;
    }
}

// EE: 0x0022B510
void DrawPanel(u32 list, s32 index, u32 panel) {
    const u32 rects = GRef<u32>(panel + 0x5C);
    const u32 parts = GRef<u16>(panel + 2);
    if (rects != 0 && (parts & 1) != 0) DrawAllFrameOutlines(list, index, GRef<s32>(panel + 4), rects, panel + 0xC);
    const u32 crosshair = GRef<u32>(panel + 0x60);
    if (crosshair != 0 && (parts & 2) != 0) DrawCrosshair(list, index, crosshair);
}

// EE: 0x0022AB20
// Filled box: REGLIST GIF tag {PRIM 0x24A (sprite, alpha), RGBAQ `color`, XYZ2 `xy0`, XYZ2 `xy1`}.
// Returns the end.
u32 BuildFilledBox(u32 p, u64 color, u64 xy0, u64 xy1) {
    GRef<u64>(p + 8) = 0x5510;
    GRef<u64>(p) = 0x4400000000000001ull;
    GRef<u64>(p + 0x10) = 0x24A;
    GRef<u64>(p + 0x28) = xy1;
    GRef<u64>(p + 0x18) = color & 0xFFFFFFFFull;
    GRef<u64>(p + 0x20) = xy0;
    return p + 0x30;
}

// EE: 0x0022A868
// Icon box at (`x`, `y`) of `w` x `h` (12.4 units): a GIF tag with PRIM setting ALPHA (A+D), then
// the outline as REGLIST {RGBAQ, XYZ3, XYZ2 x 4} in 0xD1C1A9; with `highlight` a second outline
// inset by 0x40 in 0x1067. Returns the end.
u32 BuildIconBox(u32 p, u64 x, u64 y, s32 w, s32 h, s32 highlight) {
    auto outline = [](u32 q, u64 color, u64 x0, u64 y0, s32 width, s32 height) {
        const u64 xr = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(x0) + static_cast<u32>(width))));
        const u64 yb = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(y0) + static_cast<u32>(height)))) << 16;
        const u64 yt = y0 << 16;
        GRef<u64>(q) = 0x6400000000008001ull;  // REGLIST, 6 registers {RGBAQ, XYZ3, XYZ2 x 4}
        GRef<u64>(q + 8) = 0x5555D1;
        GRef<u64>(q + 0x10) = color;
        GRef<u64>(q + 0x18) = x0 | yt;
        GRef<u64>(q + 0x20) = xr | yt;
        GRef<u64>(q + 0x28) = xr | yb;
        GRef<u64>(q + 0x30) = x0 | yb;
        GRef<u64>(q + 0x38) = x0 | yt;
        return q + 0x40;
    };
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x43;
    GRef<u64>(p + 0x10) = 0x8000000068ull;
    GRef<u64>(p) = 0x1125400000000001ull;
    u32 q = outline(p + 0x20, 0xD1C1A9, x, y, w, h);
    if (highlight == 1) {
        const u64 xi = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(x) + 0x40)));
        const u64 yi = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(y) + 0x40)));
        q = outline(q, 0x1067, xi, yi, w - 0x80, h - 0x80);
    }
    return q;
}

// EE: 0x0022AB60
// Tree reset: column 0 active, all widgets closed, connectors hidden, column 0 opening.
void ResetMenuTree(u32 tree) {
    const s32 n = GRef<s32>(tree + 4);
    GRef<s32>(tree) = 0;
    ResetListWidgets(tree + 8, n);
    ResetConnectorAlphas(tree + 0xC8, n - 1);
    SetListWidgetState(tree + 8, 1);
}

// EE: 0x0022ABC0
// Builds the tree from `count` column definitions (`cols`, stride 0x18), connector definitions
// (`conns`, stride 0x14), hidden-item masks and the parent item id of each connector: visible items
// get consecutive slots in the tree's scale/string arrays and their global item ids.
void InitMenuTree(u32 tree, u32 count, u32 cols, u32 conns, u32 masks, u32 parents) {
    const s32 n = count < 9 ? static_cast<s32>(count) : 8;
    GRef<s32>(tree + 4) = n;
    s32 visible = 0;
    s32 id = 0;
    for (s32 col = 0; col < n; ++col) {
        u32 mask = GRef<u32>(masks + 4u * static_cast<u32>(col));
        const u32 src = cols + static_cast<u32>(col) * 0x18;
        const u32 dst = tree + 8 + static_cast<u32>(col) * 0x18;
        for (u32 b = 0; b < 0x18; b += 8) GRef<u64>(dst + b) = GRef<u64>(src + b);
        GRef<u32>(dst + 0x10) = tree + 0x154 + 4u * static_cast<u32>(visible);
        GRef<u32>(dst + 0x14) = tree + 0x254 + 4u * static_cast<u32>(visible);
        GRef<s16>(tree + 0x378 + 2u * static_cast<u32>(col)) = static_cast<s16>(visible);
        GRef<u32>(tree + 0x358 + 4u * static_cast<u32>(col)) = mask;
        s32 cursor = GRef<s16>(src + 6);
        const s32 orig_cursor = cursor;
        const s32 items = GRef<s16>(src + 4);
        s32 shown = 0;
        for (s32 i = 0; i < items; ++i) {
            if ((mask & 1) == 0) {
                GRef<s16>(tree + 0x388 + 2u * static_cast<u32>(visible)) = static_cast<s16>(id);
                ++visible;
                const u32 text = GRef<u32>(GRef<u32>(src + 0x14) + 4u * static_cast<u32>(i));
                GRef<u32>(GRef<u32>(tree + 0x1C + static_cast<u32>(col) * 0x18) + 4u * static_cast<u32>(shown)) = text;
                ++shown;
            } else if (!(orig_cursor < i)) {
                --cursor;
            }
            ++id;
            mask >>= 1;
        }
        if (cursor < 0 || shown < cursor) cursor = 0;
        GRef<s16>(dst + 6) = static_cast<s16>(cursor);
        GRef<s16>(dst + 4) = static_cast<s16>(shown);
    }
    GRef<s32>(tree + 0x354) = visible;
    for (s32 k = 0; k < n - 1; ++k) {
        const u32 dst = tree + 0xC8 + static_cast<u32>(k) * 0x14;
        const u32 src = conns + static_cast<u32>(k) * 0x14;
        GRef<u64>(dst) = GRef<u64>(src);
        GRef<u64>(dst + 8) = GRef<u64>(src + 8);
        GRef<u32>(dst + 0x10) = GRef<u32>(src + 0x10);
        GRef<u16>(dst + 0xA) = GRef<u16>(tree + 0xC + static_cast<u32>(k + 1) * 0x18);
        GRef<u32>(dst + 0x10) =
            tree + 0x154 + 4u * static_cast<u32>(static_cast<s32>(GRef<s16>(tree + 0x378 + 2u * static_cast<u32>(k + 1))));
        if (visible > 0) {
            const s16 parent = GRef<s16>(parents + 2u * static_cast<u32>(k));
            if (GRef<s16>(tree + 0x388) == parent) {
                GRef<s16>(dst + 2) = 0x6F;
            } else {
                for (s32 j = 1; j < visible; ++j) {
                    if (GRef<s16>(tree + 0x388 + 2u * static_cast<u32>(j)) == parent) {
                        GRef<s16>(dst + 2) = static_cast<s16>(j * 0x18 + 0x6F);
                        break;
                    }
                }
            }
        }
    }
    ResetMenuTree(tree);
}

// EE: 0x0022AF60
void UpdateMenuTree(u32 tree, u32 pad) {
    const s32 n = GRef<s32>(tree + 4);
    const s32 active = GRef<s32>(tree);
    UpdateListWidgets(tree + 8, n, active, pad);
    UpdateConnectorAlphas(tree + 0xC8, n - 1, active - 1);
}

// EE: 0x0022AFB8
// Global id of the item under the cursor of the active column.
s16 GetMenuTreeSelection(u32 tree) {
    const s32 col = GRef<s32>(tree);
    const s32 first = GRef<s16>(tree + 0x378 + 2u * static_cast<u32>(col));
    const s32 cursor = GRef<s16>(tree + 0xE + static_cast<u32>(col) * 0x18);
    return GRef<s16>(tree + 0x388 + 2u * static_cast<u32>(first + cursor));
}

// EE: 0x0022AFF0
void DrawMenuTree(u32 list, s32 index, u32 tree) {
    const s32 n = GRef<s32>(tree + 4);
    DrawListWidgets(list, index, tree + 8, n);
    DrawConnectors(list, index, tree + 0xC8, n - 1);
}

// EE: 0x0022B050
// Writes each column's cursor back into the column definitions (`cols`, stride 0x18) as an index
// over all items (hidden ones included), so the menu reopens on the same item.
void StoreMenuTreeCursors(u32 cols, s32 count, u32 tree) {
    for (s32 col = 0; col < count; ++col, cols += 0x18) {
        const s32 items = GRef<s16>(cols + 4);
        const s32 cursor = GRef<s16>(tree + 0xE + static_cast<u32>(col) * 0x18);
        u32 mask = GRef<u32>(tree + 0x358 + 4u * static_cast<u32>(col));
        s32 raw = 0;
        s32 seen = 0;
        bool in_range = raw < items;
        if (in_range) {
            bool search = true;
            if ((mask & 1) == 0) {
                if (cursor <= 0) search = false;  // item 0 is the cursor item
                else ++seen;
            }
            while (search) {
                ++raw;
                in_range = raw < items;
                mask >>= 1;
                if (!in_range) break;
                if ((mask & 1) != 0) continue;
                const bool more = seen < cursor;
                ++seen;
                if (!more) break;
            }
        }
        if (!in_range) raw = 0;
        GRef<s16>(cols + 6) = static_cast<s16>(raw);
    }
}

// ---- Message box --------------------------------------------------------------------------------

// EE: 0x0022B5A8
// Uploads the glyphs of up to 3 message lines (DBP 0x2C00 + line * 0x100) and the box frame
// glyphs (DBP 0x2F00).
void UploadMessageBoxGlyphs(u32 list, s32 index, u32 mb) {
    s32 lines = GRef<s16>(mb + 0xC);
    if (lines > 3) lines = 3;
    for (s32 i = 0; i < lines; ++i) {
        UploadMessageGlyphs(list, index, GRef<s16>(mb + 0xE + 2u * static_cast<u32>(i)),
                            static_cast<u64>(static_cast<u32>(i * 0x100 + 0x2C00) & 0xFF00));
    }
    UploadMessageGlyphs(list, index, -1, 0x2F00);
}

// EE: 0x0022B6F8
void InitMessageBox(u32 mb) {
    GRef<u16>(mb + 4) = 0x380;
    GRef<u16>(mb + 6) = 0x15D0;
    GRef<u32>(mb) = 0;
    GRef<u32>(mb + 8) = 0;
    GRef<u16>(mb + 0xC) = 0;
    GRef<u16>(mb + 0x14) = 0;
    for (u32 i = 0; i < 3; ++i) {
        GRef<u16>(mb + 0xE + 2 * i) = 0xFFFF;
        GRef<u16>(mb + 0x16 + 2 * i) = 0;
        GRef<u16>(mb + 0x1E + 2 * i) = 0;
    }
}

// EE: 0x0022B758
// Line lengths and pixel widths (x16) of the box's lines, and the total length.
void MeasureMessageBox(u32 mb) {
    s32 lines = GRef<s16>(mb + 0xC);
    if (lines > 3) lines = 3;
    s32 total = 0;
    s32 i = 0;
    for (; i < lines; ++i) {
        const u32 id = static_cast<u32>(static_cast<s32>(GRef<s16>(mb + 0xE + 2u * static_cast<u32>(i))));
        const s32 len = GetMessageLength(id);
        GRef<s16>(mb + 0x16 + 2u * static_cast<u32>(i)) = static_cast<s16>(len);
        total += len;
        s16 width = 0;
        for (s32 c = 0; c < len; ++c) width = static_cast<s16>(width + GetMessageCharWidth(id, c) * 0x10);
        GRef<s16>(mb + 0x1E + 2u * static_cast<u32>(i)) = width;
    }
    for (; i < 3; ++i) {
        GRef<s16>(mb + 0x16 + 2u * static_cast<u32>(i)) = 0;
        GRef<s16>(mb + 0x1E + 2u * static_cast<u32>(i)) = 0;
    }
    GRef<s16>(mb + 0x14) = static_cast<s16>(total);
}

// EE: 0x0022B8B8
// Shows message `id` and the following ones up to the end marker (char 0xFFFE at index -1), at
// most 3 lines, with the typewriter restarted; a negative id clears the box.
void SetMessageBoxText(u32 mb, s32 id) {
    if (id < 0) {
        GRef<u16>(mb + 0xC) = 0;
        GRef<u32>(mb) = 0;
        GRef<u32>(mb + 8) = 0;
        return;
    }
    GRef<u32>(mb + 8) = 0;
    GRef<u32>(mb) = (GRef<u32>(mb) & 0xFFFFFFFDu) | 1;
    s16 lines = 0;
    for (s32 i = 0; i < 3; ++i) {
        GRef<s16>(mb + 0xE + 2u * static_cast<u32>(i)) = static_cast<s16>(id);
        ++lines;
        if (GetMessageChar(static_cast<u32>(id), -1) == 0xFFFE) break;
        ++id;
    }
    GRef<s16>(mb + 0xC) = lines;
    MeasureMessageBox(mb);
}

// EE: 0x0022B9A0
// Typewriter: advances the progress by `speed`; SE 9 every third `frame` while characters are
// still appearing; done (flag 2) 5 characters after the end.
void UpdateMessageBox(f32 speed, u32 mb, s32 frame) {
    if ((GRef<u32>(mb) & 1) == 0) return;
    const f32 progress = fpu::Add(GRef<f32>(mb + 8), speed);
    GRef<f32>(mb + 8) = progress;
    if (!(0.0f < progress)) return;
    const f32 rest = fpu::Sub(progress, static_cast<f32>(GRef<s16>(mb + 0x14)));
    if (rest < 0.0f && frame % 3 == 0) PlayMenuSe(0, 9);
    if (5.0f < rest) GRef<u32>(mb) = (GRef<u32>(mb) & 0xFFFFFFFEu) | 2;
}

// EE: 0x0022BA68
// Message box: the typed characters of each line (colour codes 8-11), a cursor block and glow on
// the newest characters, and (unless flag 8) the frame lines, brighter for lines already started.
void DrawMessageBox(u32 list, s32 index, u32 mb) {
    s32 lines = GRef<s16>(mb + 0xC);
    f32 progress = GRef<f32>(mb + 8);
    if (lines < 0) return;
    const u32 start = Ctx()->cur_buf.addr;
    u32 started = 0;
    if (!(lines < 4)) lines = 3;
    const u32 tag = WriteDmaCallTag(start, kMenuSpritePacket);
    u32 p = BuildFrameDrawEnv(tag + 0x10);
    const s32 x0 = static_cast<s32>(GRef<u16>(mb + 4)) + 0x100;
    s32 y = static_cast<s32>(GRef<u16>(mb + 6)) + 0x20;
    Q(p, 0) = 0x11AB400000008002ull;
    Q(p, 1) = 0xE;
    Q(p, 3) = 0x19;
    Q(p, 4) = 0x8000000048ull;
    Q(p, 5) = 0x43;
    Q(p, 2) = 0;
    p += 0x30;
    if (lines > 0) {
        s32 line = 0;
        progress = fpu::Sub(progress, static_cast<f32>(line));
        if (!(progress <= 0.0f)) {
            for (;;) {
                started |= 1u << line;
                s32 chars = fpu::FloatToInt(progress);
                const u32 line_id = static_cast<u32>(static_cast<s32>(GRef<s16>(mb + 0xE + 2u * static_cast<u32>(line))));
                const s32 len = GRef<s16>(mb + 0x16 + 2u * static_cast<u32>(line));
                if (chars > 0) {
                    if (len < chars) chars = len;
                    Q(p, 0) = 0x1400000000000001ull;
                    Q(p, 1) = 7;
                    Q(p, 2) = Sx(line * 0x100 + 0x2C00) | 0x2005E006A9420000ull;
                    p += 0x20;
                    Q(p, 0) = static_cast<u64>(static_cast<u32>(chars) | 0x8000) | 0x6400000000000000ull;
                    Q(p, 1) = 0x53D310;
                    u32 e = p + 0x10;
                    s32 x = x0;
                    for (s32 c = 0; c < chars; ++c, e += 0x30) {
                        const u16 code = GetMessageChar(line_id, c);
                        u64 color = 0x80C1B199;
                        if (code == 9) color = 0x8050F050;
                        else if (code == 8) color = 0x804040F0;
                        else if (code == 10) color = 0x80E070F0;
                        else if (code == 11) color = 0x80F04040;
                        const s32 row = (c >> 4) << 9;
                        const s32 col = (c & 0xF) * 0x200 + 8;
                        GRef<u64>(e) = 0x356;
                        GRef<u64>(e + 8) = color;
                        GRef<u64>(e + 0x10) = Sx(col) | Sx(row + 8) << 0x10;
                        GRef<u64>(e + 0x18) = Sx(x) | Sx(y) << 0x10;
                        const s32 width = GetMessageCharWidth(line_id, c) << 4;
                        x += width;
                        GRef<u64>(e + 0x20) = Sx(col + width) | Sx(row + 0x208) << 0x10;
                        GRef<u64>(e + 0x28) = Sx(x) | Sx(y + 0x200) << 0x10;
                    }
                    p = e;
                }
                s32 newest = fpu::FloatToInt(fpu::Sub(progress, 4.0f));
                if (newest < 0) {
                    newest = 0;
                    if ((GRef<u32>(mb) & 8) == 0) {
                        const f32 t = fpu::Sub(progress, static_cast<f32>(newest)) * 0.25f;
                        const s32 by = static_cast<s32>(GRef<u16>(mb + 6)) + line * 0x140;
                        const s32 bx = GRef<u16>(mb + 4);
                        Q(p, 1) = 0x5353170;
                        Q(p, 2) = 0x356;
                        Q(p, 0) = 0x7400000000008001ull;
                        Q(p, 3) = kTex0Panel;
                        Q(p, 4) = Lo32(LerpColor(t, 0xFFFFFFFF80808080ull, 0xFFFFFFFF80101010ull));
                        Q(p, 5) = 0xB680848;
                        Q(p, 6) = Sx(bx - 0x60) | Sx(by - 0x30) << 0x10;
                        Q(p, 7) = 0xCC809A8;
                        Q(p, 8) = Sx(bx + 0x60) | Sx(by + 0x170) << 0x10;
                        p += 0x50;
                    }
                }
                s32 x = x0;
                for (s32 c = 0; c < chars; ++c) {
                    const s32 width = GetMessageCharWidth(line_id, c) << 4;
                    const s32 x_end = x + width;
                    if (!(c < newest) && GetMessageChar(line_id, c) != 0xD) {
                        const f32 t = fpu::Sub(progress, static_cast<f32>(c)) * 0.25f;
                        if (!(1.5f < t)) {
                            Q(p, 0) = 0x7400000000008001ull;
                            Q(p, 2) = 0x356;
                            Q(p, 1) = 0x5353170;
                            Q(p, 3) = kTex0Panel;
                            Q(p, 4) = Lo32(LerpColor(t, 0xFFFFFFFF80C0C0C0ull, 0xFFFFFFFF80202020ull));
                            Q(p, 5) = 0xB680848;
                            Q(p, 6) = Sx(x - 0x40) | Sx(y - 0x20) << 0x10;
                            Q(p, 7) = 0xCC809A8;
                            Q(p, 8) = Sx(x_end + 0x40) | Sx(y + 0x160) << 0x10;
                            p += 0x50;
                        }
                    }
                    x = x_end;
                }
                ++line;
                if (!(line < lines)) break;
                y += 0x130;
                progress = fpu::Sub(progress, static_cast<f32>(len));
                if (progress <= 0.0f) break;
            }
        }
    }
    if ((GRef<u32>(mb) & 8) == 0) {
        const u64 x = GRef<u16>(mb + 4);
        u64 fy = GRef<u16>(mb + 6);
        Q(p, 0) = 0x4400000000008003ull;
        Q(p, 1) = 0x5D10;
        u32 e = p + 0x10;
        for (s32 i = 0; i < 3; ++i, e += 0x20) {
            GRef<u64>(e) = 0x24A;
            GRef<u64>(e + 8) = (static_cast<s32>(started) >> i & 1) == 0 ? 0x30C1B199ull : 0xFFFFFFFF80C1B199ull;
            const u64 next = fy + 0x140;
            GRef<u64>(e + 0x10) = x | fy << 0x10;
            GRef<u64>(e + 0x18) = x | next << 0x10;
            fy = next;
        }
        p = e;
    }
    LinkPacket(list, index, start, CloseDmaPacket(tag, p));
}

}  // namespace ac04::game
