// Message window widget and text engine (docs/subsystems/results-startup.md "Message window",
// docs/subsystems/sound.md "Text engine"). Used by scene 6 (startup and card messages) and the
// debriefing (scene 5).
//
// Window object (0x58 bytes):
//   +0x00 u8  flags (bit 0: page fully shown)
//   +0x04 s16 glyph TBP, +0x06 s8 glyph TBW, +0x07 s8 glyph PSM, +0x08 s16 font CBP, +0x0A s8 CPSM
//   +0x0B s8  number of messages in the text block
//   +0x0C u8  state (bit 0 loaded, bit 1 shown, bit 2 multi-page, bit 4 no frame)
//   +0x0D s8  current page, +0x10 s16 frame counter, +0x12 s16 type-out limit, +0x14 s16 chars shown
//   +0x18 u32 page table (text block), +0x1C..+0x33 RGBA colours (normal, highlight, codes 8-10)
//   +0x34/+0x36 s16 position, +0x3A u8 type-out speed, +0x3B s8, +0x3C s16 chars typed,
//   +0x3E s16 speed counter, +0x40/+0x44/+0x48 textures (cursor glow, frame, page arrow)
// Text block: {u32 page table offset, s16 pages, u8 multi-page, s8}; page = {u32 line table, s16
// lines, s16 total chars}; line = 8 bytes with the message id (u16) at +4.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "game/pc_texts.h"

namespace ac04::game {

using guest::GRef;

namespace {

// Font state (0x004BD798..0x004BD7BB).
constexpr u32 kFont = 0x004BD798;
constexpr u32 kMessageTable = 0x004BCF70;  // 512 message pointers
AC04_GUEST_VAR(s32, g_MessageCount, 0x00411858)  // gp-0x4198

}  // namespace

// EE: 0x0024ED80
// Initialises a message window; `kind` 1 is the scene 6 variant (wider limit, grey text).
void InitMessageWindow(u32 win, s32 kind) {
    GRef<u16>(win + 4) = 0x2500;
    GRef<u8>(win + 6) = 8;
    GRef<u8>(win + 7) = 0x14;
    GRef<u16>(win + 8) = 0x3000;
    GRef<u16>(win + 0x12) = 0x1E0;
    GRef<u8>(win + 0x29) = 0xB1;
    GRef<u8>(win + 0x2C) = 0x99;
    GRef<u8>(win + 0xC) = 0;
    GRef<u8>(win) = 0;
    GRef<u8>(win + 0xA) = 0;
    GRef<u8>(win + 0xB) = 0;
    GRef<u8>(win + 0xD) = 0;
    GRef<u16>(win + 0xE) = 0;
    GRef<u32>(win + 0x18) = 0;
    GRef<u16>(win + 0x16) = 0;
    GRef<u16>(win + 0x10) = 0;
    GRef<u16>(win + 0x14) = 0;
    GRef<u16>(win + 0x3C) = 0;
    static const u8 kColours[] = {0x99, 0xB1, 0xC1, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0xFF, 0xB1, 0xC1, 0x80};
    for (u32 i = 0; i < sizeof(kColours); ++i) GRef<u8>(win + 0x1C + i) = kColours[i];
    GRef<u8>(win + 0x28) = 0x99;
    GRef<u8>(win + 0x2A) = 0xFF;
    GRef<u8>(win + 0x2B) = 0x80;
    GRef<u8>(win + 0x2D) = 0xFF;
    GRef<u8>(win + 0x2E) = 0xC1;
    GRef<u8>(win + 0x32) = 0xD1;
    GRef<u8>(win + 0x33) = 0x80;
    GRef<u8>(win + 0x3A) = 1;
    GRef<u16>(win + 0x34) = 0x38;
    GRef<u16>(win + 0x36) = 0x166;
    GRef<u16>(win + 0x38) = 0x14;
    GRef<u8>(win + 0x3B) = 0xFF;
    GRef<u8>(win + 0x2F) = 0x80;
    GRef<u8>(win + 0x30) = 0xFF;
    GRef<u8>(win + 0x31) = 0xD1;
    GRef<u32>(win + 0x40) = 0;
    GRef<u32>(win + 0x44) = 0;
    GRef<u32>(win + 0x48) = 0;
    if (kind == 1) {
        GRef<u8>(win + 0x1E) = 0xE8;
        GRef<u8>(win + 0xC) = 0x10;
        GRef<u16>(win + 0x36) = 0x50;
        GRef<u8>(win + 0x1C) = 0xE8;
        GRef<u8>(win + 0x1D) = 0xE8;
    }
}

// EE: 0x0024EEB0
// Loads the font/message block, relocates the page table of the text block and counts each page's
// characters, and (when `gfx` is given) the three window textures at TBP 0x2500 / CBP 0x3E00.
void LoadMessageWindow(u32 win, u32 font, u32 text, u32 gfx) {
    if ((GRef<u8>(win + 0xC) & 1) != 0) return;
    LoadFontBlock(font);
    GRef<s8>(win + 0xB) = static_cast<s8>(GetMessageCount());
    GRef<u32>(win + 0x18) = text;
    GRef<u32>(text) += text;
    if (GRef<s16>(text + 4) > 0) {
        s32 i = 0;
        u32 pages = GRef<u32>(text);
        for (;;) {
            s16 total = 0;
            s16 sum = 0;
            const u32 page = pages + static_cast<u32>(i) * 8;
            GRef<u32>(page) += text;
            for (s32 j = 0; j < GRef<s16>(page + 4); ++j) {
                total = static_cast<s16>(sum + static_cast<s16>(GetMessageLength(GRef<u16>(GRef<u32>(page) + j * 8 + 4))));
                sum = total;
            }
            ++i;
            GRef<s16>(page + 6) = total;
            const u32 block = GRef<u32>(win + 0x18);
            if (GRef<s16>(block + 4) <= i) break;
            pages = GRef<u32>(block);
        }
    }
    if (gfx != 0) {
        guest::GuestFrame reader(0x10);
        InitContainerReader(reader.Address(), gfx);
        for (u32 k = 0; k < 3; ++k) {
            const u32 slot = win + 0x40 + 4 * k;
            GRef<u32>(slot) = WorkHeapAlloc(0x28, 0x10);
            InitGimTexture(GRef<u32>(slot), GetContainerItem(reader.Address(), static_cast<s32>(k)));
            const u32 tex = GRef<u32>(slot);
            const u32 clut = GRef<u32>(tex + 8);
            GRef<u16>(GRef<u32>(tex + 4)) = 0x2500;
            GRef<u16>(clut) = 0x3E00;
        }
    }
    const s8 last = GRef<s8>(GRef<u32>(win + 0x18) + 7);
    GRef<s8>(win + 0x3B) = last;
    if (last != -1) GRef<s8>(win + 0x3B) = static_cast<s8>(last - 1);
    u8 state = GRef<u8>(win + 0xC);
    if (GRef<u8>(GRef<u32>(win + 0x18) + 6) == 1) {
        GRef<u8>(win + 0xC) = state | 4;
        state = GRef<u8>(win + 0xC);
    }
    GRef<u8>(win + 0xC) = state | 1;
}

// EE: 0x0024F1B8
// Shows page `page` (ignored in multi-page mode or when out of range).
void ShowMessagePage(u32 win, s16 page) {
    const u8 state = GRef<u8>(win + 0xC) & 0xFD;
    if ((GRef<u8>(win + 0xC) & 4) != 0) return;
    GRef<u8>(win + 0xC) = state;
    if (page < GRef<s16>(GRef<u32>(win + 0x18) + 4)) {
        GRef<s8>(win + 0xD) = static_cast<s8>(page);
        GRef<u8>(win + 0xC) = state | 2;
        GRef<u8>(win) &= 0xFE;
        GRef<u16>(win + 0x3C) = 0;
        GRef<u16>(win + 0x3E) = 0;
        GRef<u16>(win + 0x14) = 0;
        GRef<u16>(win + 0xE) = 0;
        GRef<u16>(win + 0x10) = 0;
    }
}

// EE: 0x0024F210
// Hides the window.
void HideMessageWindow(u32 win) {
    GRef<s8>(win + 0xD) = 0;
    GRef<u16>(win + 0xE) = 0;
    GRef<u8>(win + 0xC) &= 0xFD;
    GRef<u16>(win + 0x16) = 0;
}

// EE: 0x0024F230
// Per-frame type-out: advances the typed character count and sets the "page shown" flag.
void UpdateMessageWindow(u32 win) {
    const u8 page = GRef<u8>(win + 0xD);
    const u32 block = GRef<u32>(win + 0x18);
    const u8 state = GRef<u8>(win + 0xC);
    const s32 cur = static_cast<s8>(page);
    if ((state & 4) == 0 || cur == GRef<s16>(block + 4) - 1) {
        if (GRef<s16>(GRef<u32>(block) + cur * 8 + 6) < GRef<s16>(win + 0x14)) GRef<u8>(win) |= 1;
    }
    s16 shown = GRef<s16>(win + 0x14);
    if (GRef<s16>(win + 0x12) < shown) {
        if ((state & 4) != 0 && cur < GRef<s16>(block + 4) - 1) {
            GRef<u16>(win + 0x3C) = 0;
            GRef<u8>(win + 0xD) = static_cast<u8>(page + 1);
            shown = 0;
            GRef<u16>(win + 0x3E) = 0;
            GRef<u16>(win + 0x14) = 0;
        }
    } else {
        const s16 tick = static_cast<s16>(GRef<s16>(win + 0x3E) + 1);
        GRef<s16>(win + 0x3E) = tick;
        const u32 speed = GRef<u8>(win + 0x3A);
        // The original tests tick % (speed - 1) == speed - 1, which never holds: only speed 1 types.
        if (speed == 1 || (tick % static_cast<s32>(speed - 1)) == static_cast<s32>(speed - 1)) {
            GRef<s16>(win + 0x3C) = static_cast<s16>(GRef<s16>(win + 0x3C) + 1);
        }
    }
    GRef<s16>(win + 0x10) = static_cast<s16>(GRef<s16>(win + 0x10) + 1);
    if (shown < GRef<s16>(win + 0x3C)) GRef<s16>(win + 0x14) = GRef<s16>(win + 0x3C);
}

// EE: 0x0024F360
// Dead (no reference): draws message `line` (+4 message id, only when below the window's message
// count +0xB) as one sprite of the uploaded glyph strip: TEXFLUSH, TEST_2 0x30003, ALPHA_2 0x44,
// FBA_2 0, then PRIM 0x356 / TEX0_2 (glyph TBP/TBW/PSM, CLUT) / RGBAQ (+0x1C) / UV / XYZ2 at
// (x +0x34, y +0x36 + row * 20) with the message width (16-pixel-high strip). Words the
// original leaves unwritten (GIFtag register high words, NOP and UV high words) stay unwritten.
void func_0024F360(u32 win, u32 list, s32 index, u32 line, s32 row) {
    const s32 msg = GRef<s16>(line + 4);
    if (!(msg < GRef<s8>(win + 0xB))) return;
    UploadMessageGlyphs(list, 0, msg, GRef<u16>(win + 4));
    const s32 width = GetMessageWidth(GRef<s16>(line + 4));
    const u32 p = BeginPacket();
    Put(p, 0x00, 0x10000002);
    Put(p, 0x01, 0);
    Put(p, 0x02, 0);
    Put(p, 0x03, 0x50000002);
    Put(p, 0x04, 0x8001);
    Put(p, 0x05, 0x10000000);
    Put(p, 0x06, 0xE);
    Put(p, 0x0A, 0x3F);
    Put(p, 0x0C, 0x10000004);
    Put(p, 0x0D, 0);
    Put(p, 0x0E, 0x11000000);
    Put(p, 0x0F, 0x50000004);
    Put(p, 0x10, 0x8003);
    Put(p, 0x12, 0xE);
    Put(p, 0x11, 0x10000000);
    Put(p, 0x14, 0x30003);
    Put(p, 0x16, 0x48);
    Put(p, 0x18, 0x44);
    Put(p, 0x1A, 0x43);
    Put(p, 0x19, 0);
    Put(p, 0x1B, 0);
    Put(p, 0x1C, 0);
    Put(p, 0x1E, 0x4B);
    Put(p, 0x20, 0x10000005);
    Put(p, 0x21, 0);
    Put(p, 0x22, 0);
    Put(p, 0x23, 0x50000005);
    Put(p, 0x24, 0x8001);
    Put(p, 0x25, 0x84000000);
    Put(p, 0x26, 0x4343F170);
    GRef<u64>(p + 0xA0) = 0x356;
    Put(p, 0x2A, static_cast<u32>(GRef<s16>(win + 4)) | 0x80000000u |
                     (static_cast<u32>(GRef<s8>(win + 7)) << 20 | static_cast<u32>(GRef<s8>(win + 6)) << 14) | 0x28000000u);
    Put(p, 0x2B, static_cast<u32>(GRef<s16>(win + 8)) << 5 | 0x20000000u | (static_cast<u32>(GRef<s8>(win + 0xA)) << 19 | 6));
    Put(p, 0x2C, GRef<u8>(win + 0x1C) | static_cast<u32>(GRef<u8>(win + 0x1F)) << 24 |
                     (static_cast<u32>(GRef<u8>(win + 0x1E)) << 16 | static_cast<u32>(GRef<u8>(win + 0x1D)) << 8));
    GRef<f32>(p + 0xB4) = 1.0f;
    Put(p, 0x30, 0x80008);
    const s32 ly = row * 0x14 + 0x720;
    const s32 y = GRef<s16>(win + 0x36);
    const s32 x = GRef<s16>(win + 0x34);
    Put(p, 0x33, 0);
    Put(p, 0x32, static_cast<u32>(x + 0x6CE) << 4 | static_cast<u32>(y + ly) << 20);
    Put(p, 0x34, static_cast<u32>(width) << 4 | 0x1080008u);
    Put(p, 0x36, static_cast<u32>(x + width + 0x6CE) << 4 | static_cast<u32>(y + 0x10 + ly) << 20);
    Put(p, 0x37, 0);
    Put(p, 0x38, 0x10000000);
    Put(p, 0x39, 0);
    Put(p, 0x3A, 0);
    Put(p, 0x3B, 0x11000000);
    EndPacket(list, index, p + 0xF0);
}

// EE: 0x0024F698
// Draws the current page of a window into bucket `index` of `list`: the cursor glow behind the last
// typed characters, the page arrow, the text lines (glyphs uploaded per line, with the type-out
// SE and the fade of the last four characters) and the three-row frame.
void DrawMessagePage(u32 win, u32 list, s32 index, u32 page) {
    s32 line_len[8] = {};
    const s32 nlines = GRef<s16>(page + 4);
    for (s32 i = 0; i < nlines; ++i) line_len[i] = GetMessageLength(GRef<u16>(GRef<u32>(page) + i * 8 + 4));

    u32 p = BeginPacket();
    Put(p, 1, 0);
    Put(p, 0, 0x10000002);
    Put(p, 2, 0);
    Put(p, 3, 0x50000002);
    Put(p, 4, 0x8001);
    Put(p, 6, 0xE);
    Put(p, 5, 0x10000000);
    Put(p, 0xA, 0x3F);
    Put(p, 0xC, 0x10000005);
    Put(p, 0xD, 0);
    Put(p, 0xE, 0x11000000);
    Put(p, 0xF, 0x50000005);
    Put(p, 0x10, 0x8004);
    Put(p, 0x12, 0xE);
    Put(p, 0x11, 0x10000000);
    Put(p, 0x14, 0x30003);
    Put(p, 0x16, 0x48);
    Put(p, 0x18, 0x48);
    Put(p, 0x1A, 0x43);
    Put(p, 0x19, 0);
    Put(p, 0x1B, 0);
    Put(p, 0x1E, 0x4B);
    Put(p, 0x1C, 0);
    Put(p, 0x20, 0);
    Put(p, 0x21, 0x80);
    Put(p, 0x22, 0x3B);
    EndPacket(list, index, p + 0x90);

    // Cursor glow: up to four sprites behind the last typed characters.
    if ((GRef<u8>(win + 0xC) & 0x10) == 0 && GRef<u32>(win + 0x40) != 0) {
        const u32 glow = GRef<u32>(win + 0x40);
        p = BuildTextureUpload(glow, BeginPacket(), 1, 0);
        const u64 tex0 = GetTextureTex0(glow);
        s32 line_start = 0;
        for (s32 line = 0; line < nlines; ++line) {
            const u32 rec = GRef<u32>(page) + static_cast<u32>(line) * 8;
            const s32 y = line * 0x14 + 0x71E;
            for (s32 k = 0; k <= 3; ++k) {
                const s32 c = (GRef<s16>(win + 0x14) + k - 1) - line_start;
                if (c < 0 || c >= line_len[line]) continue;
                s32 xoff = 0;
                for (s32 j = 0; j < c; ++j) xoff += static_cast<s32>(GetMessageCharWidth(GRef<u16>(rec + 4), j));
                Put(p, 1, 0);
                Put(p, 0, 0x10000005);
                Put(p, 2, 0);
                Put(p, 3, 0x50000005);
                Put(p, 4, 0x8001);
                Put(p, 5, 0x84000000);
                Put(p, 6, 0x4343F710);
                GRef<u64>(p + 0x20) = 0x356;
                const u32 a = fpu::FixUnsSfSi(static_cast<f32>(k + 1) * 0.25f * 128.0f);
                Put(p, 0xA, a << 24 | 0x808080);
                GRef<f32>(p + 0x2C) = 1.0f;
                GRef<u64>(p + 0x30) = tex0;
                Put(p, 0x10, 0x80008);
                const s32 wx = GRef<s16>(win + 0x34);
                const s32 wy = GRef<s16>(win + 0x36);
                Put(p, 0x13, 0);
                Put(p, 0x12, static_cast<u32>((wx + xoff + 0x6CA) * 0x10) | static_cast<u32>(wy + y) * 0x100000);
                Put(p, 0x14, 0x2080208);
                Put(p, 0x17, 0);
                Put(p, 0x16, static_cast<u32>((wx + 0x10 + xoff + 0x6CA) * 0x10) |
                                 static_cast<u32>(wy + 0x20 + y) * 0x100000);
                p += 0x60;
            }
            line_start += line_len[line];
        }
        EndPacket(list, index, p);
    }

    // Page arrow (multi-page windows): blinks after the last line once the page is typed out.
    if (GRef<u32>(win + 0x48) != 0 && (GRef<u8>(win + 0xC) & 4) != 0) {
        s32 end = 0xF;
        for (s32 i = 0; i < nlines; ++i) end += line_len[i];
        const s16 shown = GRef<s16>(win + 0x14);
        if (end < shown && GRef<s8>(win + 0xD) < GRef<s16>(GRef<u32>(win + 0x18) + 4) - 1) {
            const s32 last = nlines - 1;
            const u32 rec = GRef<u32>(page) + static_cast<u32>(nlines) * 8 - 8;
            s32 xoff = 0;
            for (s32 j = 0; j < line_len[last]; ++j) xoff += static_cast<s32>(GetMessageCharWidth(GRef<u16>(rec + 4), j));
            const u32 arrow = GRef<u32>(win + 0x48);
            p = BuildTextureUpload(arrow, BeginPacket(), 1, 0);
            const u64 tex0 = GetTextureTex0(arrow);
            Put(p, 1, 0);
            Put(p, 0, 0x10000005);
            Put(p, 2, 0);
            Put(p, 3, 0x50000005);
            Put(p, 4, 0x8001);
            Put(p, 5, 0x84000000);
            Put(p, 6, 0x4343F710);
            GRef<u64>(p + 0x20) = 0x356;
            s32 phase = ((GRef<s16>(win + 0x14) - end) + 0xF) % 0x1E - 0xF;
            if (phase < 0) phase = -phase;
            const u32 a = fpu::FixUnsSfSi((static_cast<f32>(phase) / 30.0f) * 128.0f);
            Put(p, 0xA, a << 24 | 0x808080);
            GRef<f32>(p + 0x2C) = 1.0f;
            GRef<u64>(p + 0x30) = tex0;
            Put(p, 0x10, 0x80008);
            const s32 wx = GRef<s16>(win + 0x34);
            const s32 ly = GRef<s16>(win + 0x36) + last * 0x14;
            Put(p, 0x13, 0);
            Put(p, 0x12, static_cast<u32>((wx + xoff + 0x6CE) * 0x10) | static_cast<u32>(ly + 0x72E) * 0x100000);
            Put(p, 0x14, 0x580108);
            Put(p, 0x17, 0);
            Put(p, 0x16, static_cast<u32>((wx + 0x10 + xoff + 0x6CE) * 0x10) | static_cast<u32>(ly + 0x733) * 0x100000);
            EndPacket(list, index, p + 0x60);
        }
    }

    // Text lines.
    s64 line_start = 0;
    if (nlines > 0 && GRef<s16>(win + 0x14) >= 0) {
        s32 line = 0;
        for (;;) {
            const u32 rec = GRef<u32>(page) + static_cast<u32>(line) * 8;
            const s64 msg = GRef<s16>(rec + 4);
            const s32 start = static_cast<s32>(line_start);
            s32 count = GRef<s16>(win + 0x14) - start;
            if (line_len[line] < count) count = line_len[line];
            if (msg < GRef<s8>(win + 0xB)) {
                UploadMessageGlyphs(list, 0, msg, GRef<u16>(win + 4));
                s32 xacc = 0;
                p = BeginPacket();
                for (s32 k = 0; k < count; ++k) {
                    const s64 code = static_cast<s16>(GetMessageChar(static_cast<u32>(msg), k));
                    u32 width = GetMessageCharWidth(static_cast<u32>(msg), k);
                    u32 base = 0x1C;
                    if (code == 8) base = 0x24;
                    else if (code == 0xB) base = 0x28;
                    else if (code == 9) base = 0x2C;
                    else if (code == 10) base = 0x30;
                    const u32 r = GRef<u8>(win + base), g = GRef<u8>(win + base + 1), b = GRef<u8>(win + base + 2);
                    const u8 alpha = GRef<u8>(win + base + 3);
                    const s32 pos = k + start;
                    s16 shown = GRef<s16>(win + 0x14);
                    if (pos == shown - 1) {
                        PlayMenuSe(0, 9);  // type-out tick
                        shown = GRef<s16>(win + 0x14);
                    }
                    Put(p, 1, 0);
                    Put(p, 0, 0x10000005);
                    Put(p, 2, 0);
                    Put(p, 3, 0x50000005);
                    Put(p, 5, 0x84000000);
                    Put(p, 4, 0x8001);
                    Put(p, 6, 0x4343F170);
                    GRef<u64>(p + 0x20) = 0x356;
                    Put(p, 0xA, static_cast<u32>(static_cast<s32>(GRef<s16>(win + 4))) | 0x80000000u |
                                    static_cast<u32>(static_cast<s32>(GRef<s8>(win + 7)) << 20) |
                                    static_cast<u32>(static_cast<s32>(GRef<s8>(win + 6)) << 14) | 0x28000000u);
                    Put(p, 0xB, static_cast<u32>(static_cast<s32>(GRef<s16>(win + 8)) << 5) | 0x20000000u |
                                    static_cast<u32>(static_cast<s32>(GRef<s8>(win + 0xA)) << 19) | 6u);
                    if (pos < shown - 4) {
                        GRef<f32>(p + 0x34) = 1.0f;
                        Put(p, 0xC, r | static_cast<u32>(alpha) << 24 | b << 16 | g << 8);
                    } else {
                        const u32 hb = GRef<u8>(win + 0x22), hg = GRef<u8>(win + 0x21), hr = GRef<u8>(win + 0x20);
                        const u32 ha = GRef<u8>(win + 0x23);
                        const f32 t = static_cast<f32>((shown - pos) - 1) * 0.25f;
                        GRef<f32>(p + 0x34) = 1.0f;
                        const f32 u = fpu::Sub(1.0f, t);
                        const s32 cb = static_cast<s32>(fpu::Add(static_cast<f32>(b) * t, static_cast<f32>(hb) * u));
                        const s32 cg = static_cast<s32>(fpu::Add(static_cast<f32>(g) * t, static_cast<f32>(hg) * u));
                        const s32 cr = static_cast<s32>(fpu::Add(static_cast<f32>(r) * t, static_cast<f32>(hr) * u));
                        const s32 ca = static_cast<s32>(fpu::Add(static_cast<f32>(alpha) * t, static_cast<f32>(ha) * u));
                        Put(p, 0xC, (static_cast<u32>(cr) & 0xFF) | static_cast<u32>(ca) << 24 |
                                        (static_cast<u32>(cb) & 0xFF) << 16 | (static_cast<u32>(cg) & 0xFF) << 8);
                    }
                    const s32 row = k >> 4;  // k >= 0
                    const s32 col = k - row * 0x10;
                    const s32 y = line * 0x14 + 0x722;
                    Put(p, 0x10, static_cast<u32>(col * 0x200 + 8) | static_cast<u32>(row * 0x200 + 8) * 0x10000);
                    const s32 w = static_cast<s32>(width);
                    const s32 wx = GRef<s16>(win + 0x34);
                    const s32 wy = GRef<s16>(win + 0x36);
                    Put(p, 0x13, 0);
                    Put(p, 0x12, static_cast<u32>((wx + xacc + 0x6CE) * 0x10) | static_cast<u32>(wy + y) * 0x100000);
                    Put(p, 0x14, static_cast<u32>((col * 0x20 + w) * 0x10 + 8) | static_cast<u32>(row * 0x200 + 0x208) * 0x10000);
                    Put(p, 0x17, 0);
                    Put(p, 0x16, static_cast<u32>((wx + w + xacc + 0x6CE) * 0x10) | static_cast<u32>(wy + 0x20 + y) * 0x100000);
                    p += 0x60;
                    xacc += w;
                }
                Put(p, 1, 0);
                Put(p, 0, 0x10000000);
                Put(p, 2, 0);
                Put(p, 3, 0x11000000);
                EndPacket(list, index, p + 0x10);
                line_start = start + line_len[line];
            }
            ++line;
            if (line >= nlines || line_start > GRef<s16>(win + 0x14)) break;
        }
    }

    // Frame: three rows (left cap, body, right cap) faded in as the text reaches them, and a
    // separator line per row.
    if ((GRef<u8>(win + 0xC) & 0x10) == 0) {
        p = BeginPacket();
        Put(p, 1, 0);
        Put(p, 0, 0x10000002);
        Put(p, 2, 0x11000000);
        Put(p, 3, 0x50000002);
        Put(p, 4, 0x8001);
        Put(p, 5, 0x10000000);
        Put(p, 6, 0xE);
        const u32 frame = GRef<u32>(win + 0x44);
        Put(p, 9, 0);
        Put(p, 0xB, 0);
        Put(p, 8, 0x48);
        Put(p, 0xA, 0x43);
        p = BuildTextureUpload(frame, p + 0x30, 1, 0);
        const u64 tex0 = GetTextureTex0(GRef<u32>(win + 0x44));
        const s32 wy = GRef<s16>(win + 0x36);
        const s32 wx = GRef<s16>(win + 0x34);
        const s16 shown = GRef<s16>(win + 0x14);
        const s64 lshown = shown;
        s32 y_body = wy + 0x720;
        const u32 x_sep = static_cast<u32>((wx + 0x6C0) * 0x10);
        const u32 x_left = static_cast<u32>((wx + 0x6BB) * 0x10);
        const u32 x_right = static_cast<u32>((wx + 0x6CB) * 0x10);
        s32 y_off = 0x720;
        s32 y_top = wy + 0x71B;
        u32 y_body_bits = static_cast<u32>(y_body) * 0x100000;
        s64 row_start = 0;
        for (s32 row = 0; row < 3; ++row) {
            const s32 rs = static_cast<s32>(row_start);
            s32 a = 0;
            if (row_start <= lshown) {
                a = 0x80;
                if (rs + 8 <= lshown) a = lshown < rs + 0x10 ? (0x10 - (shown - rs)) * 0x10 : 0;
            }
            Put(p, 0, 0x10000005);
            Put(p, 1, 0);
            Put(p, 2, 0);
            if (nlines <= row) a = 0;
            Put(p, 3, 0x50000005);
            const u32 colour = static_cast<u32>(a) << 24 | 0xFFFFFF;
            Put(p, 4, 0x8001);
            Put(p, 5, 0x84000000);
            Put(p, 6, 0x4343F170);
            GRef<u64>(p + 0x20) = 0x356;
            GRef<u64>(p + 0x28) = tex0;
            Put(p, 0xC, colour);
            GRef<f32>(p + 0x34) = 1.0f;
            Put(p, 0x10, 0x80008);
            Put(p, 0x12, x_left | static_cast<u32>(y_top) << 20);
            Put(p, 0x13, 0);
            Put(p, 0x14, 0x580108);
            const u32 y_bottom_bits = static_cast<u32>(wy + 0x14 + y_off) * 0x100000;
            Put(p, 0x16, x_right | y_body_bits);
            Put(p, 0x17, 0);
            Put(p, 0x18, 0x10000005);
            Put(p, 0x19, 0);
            Put(p, 0x1A, 0);
            Put(p, 0x1B, 0x50000005);
            Put(p, 0x1C, 0x8001);
            Put(p, 0x1E, 0x4343F170);
            Put(p, 0x1D, 0x84000000);
            GRef<u64>(p + 0x80) = 0x356;
            GRef<u64>(p + 0x88) = tex0;
            Put(p, 0x24, colour);
            GRef<f32>(p + 0x94) = 1.0f;
            Put(p, 0x28, 0x580008);
            Put(p, 0x2A, x_left | y_body_bits);
            Put(p, 0x2B, 0);
            Put(p, 0x2C, 0x680108);
            Put(p, 0x2E, x_right | y_bottom_bits);
            Put(p, 0x2F, 0);
            Put(p, 0x30, 0x10000005);
            Put(p, 0x31, 0);
            Put(p, 0x32, 0);
            Put(p, 0x33, 0x50000005);
            Put(p, 0x34, 0x8001);
            Put(p, 0x35, 0x84000000);
            Put(p, 0x36, 0x4343F170);
            GRef<u64>(p + 0xE0) = 0x356;
            GRef<u64>(p + 0xE8) = tex0;
            Put(p, 0x3C, colour);
            GRef<f32>(p + 0xF4) = 1.0f;
            Put(p, 0x40, 0x680008);
            Put(p, 0x42, x_left | y_bottom_bits);
            Put(p, 0x43, 0);
            Put(p, 0x44, 0xB80108);
            Put(p, 0x46, x_right | static_cast<u32>(y_top + 0x1E) * 0x100000);
            Put(p, 0x47, 0);
            Put(p, 0x48, 0x10000003);
            Put(p, 0x49, 0);
            Put(p, 0x4A, 0);
            Put(p, 0x4B, 0x50000003);
            Put(p, 0x4D, 0x44000000);
            Put(p, 0x4E, 0x4410);
            Put(p, 0x4C, 0x8001);
            GRef<u64>(p + 0x140) = 0x241;  // line, alpha blend, context 2
            GRef<f32>(p + 0x14C) = 1.0f;
            Put(p, 0x52, (lshown < row_start || nlines <= row) ? 0x80686054u : 0x80D1C1A9u);
            y_top += 0x14;
            Put(p, 0x55, 0);
            Put(p, 0x54, x_sep | y_body_bits);
            Put(p, 0x57, 0);
            Put(p, 0x56, x_sep | static_cast<u32>(y_body + 0x14) * 0x100000);
            y_body_bits += 0x1400000;
            p += 0x160;
            y_body += 0x14;
            row_start = rs + line_len[row];
            y_off += 0x14;
        }
        EndPacket(list, index, p);
    }
}

// EE: 0x00250690
// Draws the window when it is loaded and shown: uploads the font CLUT, restores the draw
// environment registers (with XYOFFSET... high word forced to 1) and draws the current page.
void DrawMessageWindow(u32 win, u32 cam, u32 list) {
    (void)cam;
    const u8 state = GRef<u8>(win + 0xC);
    if (((state ^ 1) & 1) != 0 || (state & 2) == 0) return;
    UploadMessageGlyphs(list, 0, -1, GRef<u16>(win + 8));
    const u32 p = BeginPacket();
    Put(p, 1, 0);
    Put(p, 0, 0x10000005);
    Put(p, 2, 0);
    Put(p, 3, 0x50000005);
    Put(p, 4, 0x8004);
    Put(p, 5, 0x10000000);
    Put(p, 6, 0xE);
    const u32 env = kMainContextAddress + offsetof(MainContext, drawenv_pkt);
    GRef<u128>(p + 0x20) = GRef<u128>(env + 0x90);
    GRef<u128>(p + 0x30) = GRef<u128>(env + 0xA0);
    GRef<u32>(p + 0x34) = 1;
    GRef<u128>(p + 0x40) = GRef<u128>(env + 0xC0);
    GRef<u128>(p + 0x50) = GRef<u128>(env + 0xB0);
    EndPacket(list, 0, p + 0x60);
    DrawMessagePage(win, list, 0, GRef<u32>(GRef<u32>(win + 0x18)) + static_cast<u32>(static_cast<s32>(GRef<s8>(win + 0xD)) * 8));
}

// ---- Text engine ---------------------------------------------------------------------------------

// EE: 0x00250870
// Message `id` (strings of u16 glyph codes ending with 0xFFFE/0xFFFF).
u32 GetMessage(u32 id) { return GRef<u32>(kMessageTable + (id % static_cast<u32>(g_MessageCount())) * 4); }

// EE: 0x00250930
// Indexes a message block {s32 count, u16 strings...}: clears the 512-entry table to -1 and stores
// a pointer per string. Returns the count.
s32 IndexMessages(u32 block) {
    const s32 count = GRef<s32>(block);
    for (u32 i = 0; i < 0x200; ++i) GRef<u32>(kMessageTable + 0x7FC - 4 * i) = 0xFFFFFFFFu;
    u32 s = block + 4;
    for (s32 i = 0; i < count; ++i) {
        GRef<u32>(kMessageTable + static_cast<u32>(i) * 4) = s;
        while (GRef<u16>(s) < 0xFFFE) s += 2;
        s += 2;
    }
    g_MessageCount() = count;
    return count;
}

// EE: 0x002509F0
s32 GetMessageCount() { return g_MessageCount(); }

// EE: 0x00251D28
// Font/message block {.., +4 font offset, +8 messages offset, .., +0x10 glyph data offset}.
// Returns the message count (IndexMessages' result; the briefing keeps it).
s32 LoadFontBlock(u32 block) {
    const u32 messages = block + GRef<u32>(block + 8);
    SelectFont(block + GRef<u32>(block + 4), block + GRef<u32>(block + 0x10));
    const s32 count = IndexMessages(messages);
    ApplyComputerTexts(count);  // port: save texts for a computer (pc_texts.h)
    return count;
}

// EE: 0x00251D70
// Width of glyph `code` (first byte of its 4-byte metrics entry).
u8 GetGlyphWidth(u32 code) { return GRef<u8>(GRef<u32>(kFont + 0x20) + (code & 0xFFFF) * 4); }

// EE: 0x00251DD8
// Width of character `index` of message `id` (0 outside the string).
u16 GetMessageCharWidth(u32 id, s64 index) {
    const u32 s = GetMessage(id);
    const s64 len = GetMessageLength(id);
    if (index < 0 || len < index) return 0;
    return GetGlyphWidth(GRef<u16>(s + static_cast<u32>(index) * 2));
}

// EE: 0x00251E48
// Selects the font by its magic ("go18" 0, "go28" 1, else 2), sets the glyph table, metrics,
// glyph size and the CLUT address.
void SelectFont(u32 font, u32 glyphs) {
    u32 sel;
    if (GRef<u32>(font) == 0x38316F67) sel = 0;
    else sel = GRef<u32>(font) == 0x38326F67 ? 1u : 2u;
    GRef<u32>(kFont + 0x1C) = sel;
    const u32 f = GRef<u32>(0x003F5D80 + sel * 4);
    GRef<u32>(kFont + 0x20) = GRef<u32>(0x003B0A70 + sel * 4);
    const u32 n = GRef<u32>(f + 8);
    GRef<u32>(kFont + 0xC) = n;
    GRef<u32>(kFont + 0x18) = glyphs;
    const u32 size = GRef<u32>(f + 0x1C);
    GRef<u32>(kFont) = f;
    GRef<u16>(kFont + 0x10) = static_cast<u16>(size);
    GRef<u16>(kFont + 0x12) = static_cast<u16>(size >> 16);
    GRef<u32>(kFont + 4) = f + 0x20;
    const u32 bytes = static_cast<u32>(static_cast<s32>((size & 0xFFFF) * (size >> 16)) >> 1) + 0x10;
    GRef<u32>(kFont + 0x14) = bytes;
    GRef<u32>(kFont + 8) = n * bytes + f + 0x20 + 0x10;
}

// EE: 0x00251F08
// Length of a glyph string (up to the 0xFFFE/0xFFFF terminator).
s32 GetGlyphStringLength(u32 s) {
    s32 n = 0;
    while (GRef<u16>(s) < 0xFFFE) {
        s += 2;
        ++n;
    }
    return n;
}

// EE: 0x00251FD0
s32 GetMessageLength(u32 id) { return GetGlyphStringLength(GetMessage(id)); }

// EE: 0x00251FF8
// Width in pixels of message `id`: the sum of its glyph widths.
s32 GetMessageWidth(s32 id) {
    u32 s = GetMessage(static_cast<u32>(id));
    s32 n = GetGlyphStringLength(GetMessage(static_cast<u32>(id)));
    s32 width = 0;
    for (; n > 0; --n, s += 2) width += GetGlyphWidth(GRef<u16>(s));
    return width;
}

// EE: 0x00252070
// Dead (no reference): GetMessageWidth.
s32 func_00252070(s32 id) { return GetMessageWidth(id); }

// EE: 0x00252090
// Glyph code of character `index` of message `id` (the terminator outside the string).
u16 GetMessageChar(u32 id, s64 index) {
    const u32 s = GetMessage(id);
    const s64 len = GetMessageLength(id);
    const s32 i = (index < 0 || len < index) ? static_cast<s32>(len) : static_cast<s32>(index);
    return GRef<u16>(s + static_cast<u32>(i << 1));
}

// EE: 0x00252540
// Uploads the glyph images of message `id` to VRAM at block `dbp` (16 glyphs per row; 0xFFFD
// starts a new row), or with `id` < 0 the font CLUT. Links the packet into bucket `index`.
void UploadMessageGlyphs(u32 list, s32 index, s64 id, u64 dbp) {
    u32 x = 0;
    u64 y = 0;
    s32 n = 0;
    const u32 start = Ctx()->cur_buf.addr;
    u32 tag;
    u32 data;
    if (id < 0) {
        Put(start, 0, 0);
        Put(start, 1, 0);
        Put(start, 2, 0);
        Put(start, 3, 0);
        Put(start, 0, 0x10000005);
        Put(start, 3, 0x50000005);
        GRef<u64>(start + 0x10) = 0x1000000000000004ull;
        GRef<u64>(start + 0x18) = 0xE;
        GRef<u64>(start + 0x20) = (dbp & 0xFFFF) << 32 | 0x1000000000000ull;  // BITBLTBUF: CT32
        GRef<u64>(start + 0x28) = 0x50;
        GRef<u64>(start + 0x38) = 0x51;
        GRef<u64>(start + 0x30) = 0;
        GRef<u64>(start + 0x40) = 0x200000008ull;  // TRXREG 8x2
        GRef<u64>(start + 0x48) = 0x52;
        GRef<u64>(start + 0x58) = 0x53;
        GRef<u64>(start + 0x50) = 0;
        Put(start, 0x18, 0x30000005);
        Put(start, 0x1B, 0x50000005);
        Put(start, 0x19, GRef<u32>(kFont + 8));
        Put(start, 0x1A, 0);
        tag = start + 0x70;
        data = start + 0x80;
    } else {
        u32 s = GetMessage(static_cast<u32>(id));
        if (s == 0xFFFFFFFFu) return;
        data = start + 0x10;
        tag = start;
        while (GRef<u16>(s) < 0xFFFE) {
            const u16 code = GRef<u16>(s);
            if (code == 0xFFFD) {
                x = 0;
                y = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(y) + GRef<u16>(kFont + 0x12)))) & 0xFFFF;
            } else {
                const u32 glyphs = GRef<u32>(kFont + 4);
                const u32 bytes = GRef<u32>(kFont + 0x14);
                Put(tag, 0, 0);
                Put(tag, 1, 0);
                Put(tag, 2, 0);
                Put(tag, 3, 0);
                Put(tag, 0, 0x10000005);
                Put(tag, 3, 0x50000005);
                GRef<u64>(data) = 0x1000000000000004ull;
                GRef<u64>(data + 8) = 0xE;
                GRef<u64>(data + 0x10) = (dbp & 0xFFFF) << 32 | 0x1408000000000000ull;  // BITBLTBUF: T4, DBW 8
                GRef<u64>(data + 0x18) = 0x50;
                GRef<u64>(data + 0x20) = static_cast<u64>(x) << 32 | y << 48;  // TRXPOS
                GRef<u64>(data + 0x28) = 0x51;
                GRef<u64>(data + 0x38) = 0x52;
                GRef<u64>(data + 0x30) = static_cast<u64>(GRef<u16>(kFont + 0x10)) | static_cast<u64>(GRef<u16>(kFont + 0x12)) << 32;
                GRef<u64>(data + 0x48) = 0x53;
                GRef<u64>(data + 0x40) = 0;
                Put(data, 0x15, glyphs + code * bytes);
                Put(data, 0x16, 0);
                Put(data, 0x17, bytes >> 4 | 0x50000000);
                Put(data, 0x14, bytes >> 4 | 0x30000000);
                tag = data + 0x60;
                x = (x + GRef<u16>(kFont + 0x10)) & 0xFFFF;
                const s32 rounded = (n < 0 ? n + 0xF : n) >> 4;
                if ((rounded << 4) == n - 0xF) {
                    x = 0;
                    y = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(y) + GRef<u16>(kFont + 0x12)))) & 0xFFFF;
                }
                ++n;
                data += 0x70;
            }
            s += 2;
        }
    }
    Put(tag, 0, 0);
    Put(tag, 1, 0);
    Put(tag, 2, 0);
    Put(tag, 3, 0);
    Put(tag, 0, 0x10000002);
    Put(tag, 3, 0x50000002);
    GRef<u64>(data) = 0x1000000000008001ull;
    GRef<u64>(data + 8) = 0xE;
    GRef<u64>(data + 0x18) = 0x3F;
    GRef<u64>(data + 0x10) = 0;
    LinkPacket(list, index, start, data + 0x20);
}

// ---- Text engine: in-flight text (slice S5g) -------------------------------------------------------

// EE: 0x00251340
// Textured sprite of the text texture at p (DMA cnt 15 + DIRECT): TEXFLUSH; ALPHA 0x44 (FIX 0x80
// for modes 2 and 8); CLAMP 5; TEST 0x3000D (mode 8) or 0x30002; ZBUF 0x131000000; TEX0 (TBP tbp,
// CBP cbp, T4 / CT32 layout constants); TEX1 0x100080060; then the sprite from (x, y, z) of size
// (w, h) (screen units) with texels (u, v)-(u + tw, v + th) and the colour qword at col. Returns
// the end of the data.
u32 func_00251340(u32 p, u32 x, u32 y, u32 z, u32 w, u32 h, u32 u, u32 v, u16 tw, u16 th, u32 col, u16 tbp, u16 cbp,
                  s32 mode) {
    x &= 0xFFFF, y &= 0xFFFF, w &= 0xFFFF, h &= 0xFFFF, u &= 0xFFFF, v &= 0xFFFF;
    fl::ZeroQ(p);
    Put(p, 0, 0x1000000F);
    Put(p, 3, 0x5000000F);
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x10) = 0x1000000000000007ull;
    GRef<u64>(p + 0x28) = 0x3F;
    GRef<u64>(p + 0x20) = 0;
    GRef<u64>(p + 0x30) = (mode == 2 || mode == 8) ? 0x8000000044ull : 0x44ull;
    GRef<u64>(p + 0x38) = 0x42;
    GRef<u64>(p + 0x40) = 5;
    GRef<u64>(p + 0x48) = 8;
    GRef<u64>(p + 0x50) = mode == 8 ? 0x3000Dull : 0x30002ull;
    GRef<u64>(p + 0x58) = 0x47;
    GRef<u64>(p + 0x60) = 0x131000000ull;
    GRef<u64>(p + 0x68) = 0x4E;
    GRef<u64>(p + 0x70) = static_cast<u64>(tbp) | 0x2000000000000000ull | (static_cast<u64>(cbp) << 37) | 0x665420000ull;
    GRef<u64>(p + 0x78) = 6;
    GRef<u64>(p + 0x80) = 0x100080060ull;
    GRef<u64>(p + 0x88) = 0x14;
    GRef<u64>(p + 0x90) = 0x30AB400000008002ull;
    GRef<u64>(p + 0x98) = 0x413;
    fl::ZeroQ(p + 0xA0);
    Put(p + 0xA0, 0, u * 16 + 8);
    Put(p + 0xA0, 1, v * 16 + 8);
    fl::CopyQ(p + 0xB0, col);
    Put(p + 0xC0, 1, y);
    Put(p + 0xC0, 0, x);
    Put(p + 0xC0, 3, 0x8000);
    Put(p + 0xC0, 2, z);
    fl::ZeroQ(p + 0xD0);
    Put(p + 0xD0, 0, (u + tw) * 16 - 8);
    Put(p + 0xD0, 1, (v + th) * 16 - 8);
    fl::CopyQ(p + 0xE0, col);
    Put(p + 0xF0, 0, x + w);
    Put(p + 0xF0, 1, y + h);
    Put(p + 0xF0, 2, z);
    Put(p + 0xF0, 3, 0);
    return p + 0x100;
}

// EE: 0x00251D90
// Metrics of glyph `code`: width and height bytes of its 4-byte entry.
void func_00251D90(u32 code, u32 w, u32 h) {
    const u32 m = GRef<u32>(kFont + 0x20) + (code & 0xFFFF) * 4;
    GRef<u16>(w) = GRef<u8>(m);
    GRef<u16>(h) = GRef<u8>(m + 1);
}

// EE: 0x00251DB8
// Glyph cell size of the current font.
void func_00251DB8(u32 w, u32 h) {
    GRef<u16>(w) = GRef<u16>(kFont + 0x10);
    GRef<u16>(h) = GRef<u16>(kFont + 0x12);
}

// EE: 0x002581C8
// Image upload at p: DMA cnt 5 + DIRECT (BITBLTBUF dbp / dbw / dpsm, TRXPOS (dx, dy), TRXREG w x
// h, TRXDIR 0) and a DMA ref of qwc quadwords at data. Returns the end.
u32 func_002581C8(u32 data, u32 qwc, u64 dbp, u64 dbw, u64 dpsm, u64 dx, u64 dy, u64 w, u16 h, u32 p) {
    fl::ZeroQ(p);
    Put(p, 0, 0x10000005);
    Put(p, 3, 0x50000005);
    GRef<u64>(p + 0x10) = 0x1000000000008004ull;
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x28) = 0x50;
    GRef<u64>(p + 0x20) = ((dbp & 0xFFFF) << 32) | ((dbw & 0xFF) << 48) | (dpsm << 56);
    GRef<u64>(p + 0x30) = ((dx & 0xFFFF) << 32) | (dy << 48);
    GRef<u64>(p + 0x38) = 0x51;
    GRef<u64>(p + 0x40) = (w & 0xFFFF) | (static_cast<u64>(h) << 32);
    GRef<u64>(p + 0x48) = 0x52;
    GRef<u64>(p + 0x50) = 0;
    GRef<u64>(p + 0x58) = 0x53;
    fl::ZeroQ(p + 0x60);
    Put(p + 0x60, 0, qwc | 0x30000000u);
    Put(p + 0x60, 1, data);
    Put(p + 0x60, 3, qwc | 0x50000000u);
    return p + 0x70;
}

// EE: 0x002582B0
// Uploads the image block img ({+0x10 pixels, +0x14 qwc, +0x18 CLUT, +0x1C CLUT qwc; image header
// +0x20, CLUT header after the pixels: +3 PSM, +0xC width, +0xE height}) to (dbp, x, y) and its
// CLUT to cbp, then TEXFLUSH. Returns the end.
u32 func_002582B0(u32 img, u16 dbp, u16 x, u16 y, u16 cbp, u32 p) {
    const u32 hdr = img + 0x20;
    const u32 pix = img + GRef<u32>(img + 0x10);
    const u32 qwc = GRef<u32>(img + 0x14);
    const u32 ch = pix + qwc * 16;
    p = func_002581C8(pix, qwc, dbp, 8, GRef<u8>(hdr + 3), x, y, GRef<u16>(hdr + 0xC), GRef<u16>(hdr + 0xE), p);
    p = func_002581C8(img + GRef<u32>(img + 0x18), GRef<u32>(img + 0x1C), cbp, 8, GRef<u8>(ch + 3), 0, 0, GRef<u16>(ch + 0xC), GRef<u16>(ch + 0xE), p);
    fl::ZeroQ(p);
    Put(p, 0, 0x10000002);
    Put(p, 3, 0x50000002);
    GRef<u64>(p + 0x10) = 0x1000000000008001ull;
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x28) = 0x3F;
    GRef<u64>(p + 0x20) = 0;
    return p + 0x30;
}

// EE: 0x00252100
// Uploads the inline icon of text code 8..11 (image idx = code - 8 of the glyph block) to (dbp,
// x, y), its CLUT at cbp + idx + 1. Returns the end.
u32 func_00252100(u32 p, u32 code, u32 dbp, u32 cbp, u32 x, u32 y) {
    const u32 idx = (code & 0xFFFF) - 8;
    const u32 glyphs = GRef<u32>(kFont + 0x18);
    const u32 img = glyphs + GRef<u32>(idx * 4 + glyphs + 4);
    return func_002582B0(img, static_cast<u16>(dbp), static_cast<u16>(x), static_cast<u16>(y),
                         static_cast<u16>((cbp & 0xFFFF) + idx + 1), p);
}

// EE: 0x00252160
// Lays out the glyph string s into the text texture at dbp (16 glyphs per row; 0xFFFD = new line;
// codes 8..11 are inline icons uploaded with their CLUTs from cbp + 1), then uploads the font CLUT
// to cbp and flushes. info receives {lines, total, glyphs on lines 0, 1, 2}. Returns the end.
u32 func_00252160(u32 p, u32 s, u32 info, u32 dbp, u32 cbp) {
    dbp &= 0xFFFF;
    cbp &= 0xFFFF;
    s32 n = 0, line = 0;
    u32 x = 0, y = 0;
    s32 counts[3] = {0, 0, 0};
    u32 lines = 1;
    while (GRef<u16>(s) < 0xFFFE) {
        const u32 code = GRef<u16>(s);
        if (code - 8 < 4) {
            ++n;
            p = func_00252100(p, GRef<u16>(s), dbp, cbp, x, y);
            s += 2;
            x = (x + GRef<u16>(kFont + 0x10)) & 0xFFFF;
            ++counts[line];
        } else if (code == 0xFFFD) {
            line = static_cast<s32>(lines);
            x = 0;
            n = 0;
            y = (y + GRef<u16>(kFont + 0x12)) & 0xFFFF;
            s += 2;
            ++lines;
        } else {
            const u32 bytes = GRef<u32>(kFont + 0x14);
            const u32 glyph = GRef<u32>(kFont + 4);
            Put(p, 0, 0x10000005);
            Put(p, 3, 0x50000005);
            Put(p, 1, 0);
            Put(p, 2, 0);
            GRef<u64>(p + 0x10) = 0x1000000000000004ull;
            GRef<u64>(p + 0x18) = 0xE;
            GRef<u64>(p + 0x20) = (static_cast<u64>(dbp) << 32) | 0x1408000000000000ull;
            GRef<u64>(p + 0x28) = 0x50;
            GRef<u64>(p + 0x30) = (static_cast<u64>(x) << 32) | (static_cast<u64>(y) << 48);
            GRef<u64>(p + 0x38) = 0x51;
            GRef<u64>(p + 0x48) = 0x52;
            GRef<u64>(p + 0x40) = static_cast<u64>(GRef<u16>(kFont + 0x10)) | (static_cast<u64>(GRef<u16>(kFont + 0x12)) << 32);
            GRef<u64>(p + 0x58) = 0x53;
            GRef<u64>(p + 0x50) = 0;
            s += 2;
            Put(p + 0x60, 1, glyph + code * bytes);
            Put(p + 0x60, 2, 0);
            Put(p + 0x60, 3, (GRef<u32>(kFont + 0x14) >> 4) | 0x50000000u);
            Put(p + 0x60, 0, (GRef<u32>(kFont + 0x14) >> 4) | 0x30000000u);
            p += 0x70;
            ++counts[line];
            x = (x + GRef<u16>(kFont + 0x10)) & 0xFFFF;
            if (((n < 0 ? n + 0xF : n) >> 4) << 4 == n - 0xF) {
                x = 0;
                y = (y + GRef<u16>(kFont + 0x12)) & 0xFFFF;
            }
            ++n;
        }
    }
    fl::ZeroQ(p);
    Put(p, 0, 0x10000005);
    Put(p, 3, 0x50000005);
    GRef<u64>(p + 0x10) = 0x1000000000000004ull;
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x20) = (static_cast<u64>(cbp) << 32) | 0x1000000000000ull;
    GRef<u64>(p + 0x28) = 0x50;
    GRef<u64>(p + 0x38) = 0x51;
    GRef<u64>(p + 0x30) = 0;
    GRef<u64>(p + 0x40) = 0x200000008ull;
    GRef<u64>(p + 0x48) = 0x52;
    GRef<u64>(p + 0x58) = 0x53;
    GRef<u64>(p + 0x50) = 0;
    Put(p + 0x60, 0, 0x30000005);
    Put(p + 0x60, 3, 0x50000005);
    Put(p + 0x60, 1, GRef<u32>(kFont + 8));
    Put(p + 0x60, 2, 0);
    fl::ZeroQ(p + 0x70);
    Put(p + 0x70, 0, 0x10000002);
    Put(p + 0x70, 3, 0x50000002);
    GRef<u64>(p + 0x80) = 0x1000000000008001ull;
    GRef<u64>(p + 0x88) = 0xE;
    GRef<u64>(p + 0x98) = 0x3F;
    GRef<u64>(p + 0x90) = 0;
    GRef<u8>(info) = static_cast<u8>(lines);
    const u8 c0 = static_cast<u8>(counts[0]), c1 = static_cast<u8>(counts[1]), c2 = static_cast<u8>(counts[2]);
    GRef<u8>(info + 2) = c0;
    GRef<u8>(info + 3) = c1;
    GRef<u8>(info + 1) = static_cast<u8>(c2 + (c0 + c1));
    GRef<u8>(info + 4) = c2;
    return p + 0xA0;
}

// EE: 0x00258418
// ALPHA_1 (A, B, C, D, FIX) as one A+D packet at p (DMA cnt 2 + DIRECT). Returns the end.
u32 func_00258418(u32 p, u64 a, u64 b, u64 c, u64 d, u64 fix) {
    fl::ZeroQ(p);
    Put(p, 0, 0x10000002);
    Put(p, 3, 0x50000002);
    GRef<u64>(p + 0x10) = 0x1000000000008001ull;
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x28) = 0x42;
    GRef<u64>(p + 0x20) = (a & 0xFFFF) | (fix << 32) | ((c & 0xFFFF) << 4) | ((b & 0xFFFF) << 2) | ((d & 0xFFFF) << 6);
    return p + 0x30;
}

// EE: 0x002584A0
// TEST_1 (ATE, ATST, AREF, AFAIL, DATE, DATM, ZTE, ZTST) as one A+D packet at p. Returns the end.
u32 func_002584A0(u32 p, u64 ate, u64 atst, u64 aref, u64 afail, u64 date, u64 datm, u64 zte, u16 ztst) {
    fl::ZeroQ(p);
    Put(p, 0, 0x10000002);
    Put(p, 3, 0x50000002);
    GRef<u64>(p + 0x10) = 0x1000000000008001ull;
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x20) = (ate & 0xFFFF) | (static_cast<u64>(ztst) << 17) | ((afail & 0xFFFF) << 12) | ((datm & 0xFFFF) << 15) |
                          ((atst & 0xFFFF) << 1) | ((aref & 0xFFFF) << 4) | ((date & 0xFFFF) << 14) | ((zte & 0xFFFF) << 16);
    GRef<u64>(p + 0x28) = 0x47;
    return p + 0x30;
}

// EE: 0x00258550
// Textured sprite at p (DMA cnt 7 + DIRECT): (x, y, z) to (x + w, y + h) in pixels, texels (u, v)
// to (u + w, v + h), colour (r, g, b, a). Returns the end.
u32 func_00258550(u32 p, u32 x, u32 y, s32 z, u32 u, u32 v, u32 w, u32 h, u16 r, u16 g, u16 b, u16 a) {
    x &= 0xFFFF, y &= 0xFFFF, u &= 0xFFFF, v &= 0xFFFF, w &= 0xFFFF, h &= 0xFFFF;
    fl::ZeroQ(p);
    Put(p, 3, 0x50000007);
    Put(p, 0, 0x10000007);
    GRef<u64>(p + 0x10) = 0x30AB400000008002ull;
    GRef<u64>(p + 0x18) = 0x413;
    fl::ZeroQ(p + 0x20);
    Put(p + 0x20, 1, v * 16 + 8);
    Put(p + 0x20, 0, u * 16 + 8);
    const u32 c[4] = {r, g, b, a};
    for (u32 i = 0; i < 4; ++i) Put(p + 0x30, i, c[i]);
    Put(p + 0x40, 0, x << 4);
    Put(p + 0x40, 1, y << 4);
    Put(p + 0x40, 3, 0x8000);
    Put(p + 0x40, 2, static_cast<u32>(z) << 4);
    fl::ZeroQ(p + 0x50);
    Put(p + 0x50, 0, (u + w) * 16 - 8);
    Put(p + 0x50, 1, (v + h) * 16 - 8);
    for (u32 i = 0; i < 4; ++i) Put(p + 0x60, i, c[i]);
    Put(p + 0x70, 0, (x + w) << 4);
    Put(p + 0x70, 1, (y + h) << 4);
    Put(p + 0x70, 2, static_cast<u32>(z) << 4);
    Put(p + 0x70, 3, 0);
    return p + 0x80;
}
}  // namespace ac04::game
