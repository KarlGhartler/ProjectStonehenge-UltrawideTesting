// Display and DMA helpers called every frame (docs/game/main-context.md, docs/subsystems/rendering.md):
// packet double buffer and lists, the draw/display environment, the field copy and the vsync wait.
#include "game/game.h"

#include <cmath>

#include "common/guest_stack.h"
#include "sce/ee_hw.h"
#include "sce/sce.h"
#include "common/ee_math.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00103E38
void ResetDoubleBuffer(u32 db) {
    GRef<u32>(db + 8) = 0;
    GRef<u32>(db + 4) = 0;
    GRef<u32>(db) = 0;
}

// EE: 0x00103E90
u32 SwapDoubleBuffer(u32 db) {
    GRef<u32>(db + 8) = GRef<u32>(db + 8) == 0 ? 1 : 0;
    return GRef<u32>(db + 4 * GRef<u32>(db + 8));
}

// EE: 0x001043B0
void ResetPacketList(u32 list) {
    GRef<u32>(list + 4) = 0;
    GRef<u32>(list) = 0;
}

// EE: 0x00104438
// Kicks a frame's DMA chain: sets/clears CHCR.TTE (bit 6) of the channel, sends the chain if any,
// and empties the list.
void SendDmaPacketList(s32 channel, u32 list, s64 tte) {
    const u32 chan = sceDmaGetChan(channel);
    u32 chcr = sce::EeDmaReadChcr(chan);
    chcr = tte == 1 ? (chcr | 0x40) : (chcr & 0xFFFFFFBFu);
    sce::EeDmaWriteChcr(chan, chcr);
    if (GRef<u32>(list) != 0) sceDmaSend(chan, GRef<u32>(list));
    GRef<u32>(list + 4) = 0;
    GRef<u32>(list) = 0;
}

// EE: 0x001044A8
void InitGraphics() {
    sceGsResetGraph(0, 1, 2, 1);  // NTSC, interlaced, field mode
    MainContext* ctx = Ctx();
    ctx->clear_rgba[0] = 0;
    ctx->draw_h = 0x1C0;
    ctx->disp_w = 0x200;
    ctx->disp_h = 0xE0;
    ctx->draw_w = 0x200;
    ctx->clear_rgba[1] = 0;
    ctx->clear_rgba[2] = 0;
    ctx->clear_rgba[3] = 0;
    ctx->frame_psm = 0;
    ctx->zbuf_psm = 0x31;
    ctx->ztest = 3;
    ctx->draw_mode = 0;
    ctx->brightness_b = 1.0f;
    ctx->brightness_r = 1.0f;
    ctx->disp_flags |= 0x8000;
    ctx->brightness_g = 1.0f;
    BuildDrawEnvPackets();
}

// EE: 0x00104558
// Builds the display environment (sceGsSetDefDispEnv) and the two GIF packets sent every frame:
// the draw environment (both contexts) and the full-screen clear sprite.
void BuildDrawEnvPackets() {
    MainContext* ctx = Ctx();
    sceGsSetDefDispEnv(g_pMainContext().addr + offsetof(MainContext, dispenv), 1, static_cast<s16>(ctx->disp_w),
                       static_cast<s16>(ctx->disp_h), 0, 0);
    ctx = Ctx();
    const u32 draw_w = ctx->draw_w;
    const u32 draw_h = ctx->draw_h;
    u64* pkt = ctx->drawenv_pkt;
    pkt[1] = 0xE;
    pkt[0] = 0x100000000000800Dull;  // GIFtag: NLOOP 13, EOP, A+D
    // Display buffer page: right after the draw buffer.
    const s32 draw_pages = static_cast<s32>((draw_w + 0x3F) >> 6) * static_cast<s32>((draw_h + 0x1F) >> 5);
    ctx->dispenv[2] = (ctx->dispenv[2] & 0xFFFFFFFFFFFFFE00ull) | (static_cast<u64>(static_cast<s64>(draw_pages)) & 0x1FF);
    const u32 disp_w = ctx->disp_w;
    const u32 disp_h = ctx->disp_h;
    const u64 frame = static_cast<u64>(draw_w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24;
    pkt[2] = frame;     // FRAME_1
    pkt[0x12] = frame;  // FRAME_2
    const s32 zbp = static_cast<s32>((disp_w + 0x3F) >> 6) * static_cast<s32>((disp_h + 0x1F) >> 5) + draw_pages;
    const u64 xyoffset = static_cast<u64>(static_cast<s64>(static_cast<s32>(0x800 - (draw_w >> 1)))) << 4 |
                         static_cast<u64>(static_cast<s64>(static_cast<s32>(0x800 - (draw_h >> 1)))) << 0x24;
    const u64 scissor = static_cast<u64>(static_cast<s64>(static_cast<s32>(draw_w - 1))) << 16 |
                        static_cast<u64>(static_cast<s64>(static_cast<s32>(draw_h - 1))) << 0x30;
    const u64 zbuf = static_cast<u64>(static_cast<s64>(zbp)) | static_cast<u64>(ctx->zbuf_psm) << 24;
    pkt[8] = scissor;
    pkt[0x18] = scissor;
    pkt[4] = zbuf;
    pkt[6] = xyoffset;
    pkt[0x14] = zbuf;
    pkt[0x16] = xyoffset;
    ctx->zbuf_page = static_cast<u16>(zbp);
    const u64 test = static_cast<u64>(ctx->ztest) << 0x11 | 0x10000;
    pkt[0xE] = 1;     // COLCLAMP value
    pkt[0xC] = 1;     // PRMODECONT value
    pkt[0x10] = 0;    // DTHE value
    pkt[10] = test;
    pkt[0x1A] = test;
    pkt[3] = 0x4C;    // FRAME_1
    pkt[0x13] = 0x4D; // FRAME_2
    pkt[5] = 0x4E;    // ZBUF_1
    pkt[0x15] = 0x4F; // ZBUF_2
    pkt[7] = 0x18;    // XYOFFSET_1
    pkt[0x17] = 0x19; // XYOFFSET_2
    pkt[9] = 0x40;    // SCISSOR_1
    pkt[0x19] = 0x41; // SCISSOR_2
    pkt[0xB] = 0x47;  // TEST_1
    pkt[0x1B] = 0x48; // TEST_2
    pkt[0xD] = 0x1A;  // PRMODECONT
    pkt[0xF] = 0x46;  // COLCLAMP
    pkt[0x11] = 0x45; // DTHE

    u64* clr = ctx->clear_pkt;
    clr[0] = 0x1000000000008006ull;  // GIFtag: NLOOP 6, EOP, A+D
    clr[1] = 0xE;
    switch (ctx->draw_mode) {
        case 0: clr[2] = 0x30000; break;
        case 1: clr[2] = 0x30001; break;
        case 2: clr[2] = 0x31001; break;
        case 3: clr[2] = 0x32001; break;
        default: break;
    }
    ctx = Ctx();
    clr[4] = 6;  // PRIM: sprite
    const u32 half_h = static_cast<u32>(ctx->draw_h >> 1);
    const u32 half_w = static_cast<u32>(ctx->draw_w >> 1);
    clr[6] = static_cast<u64>(ctx->clear_rgba[0]) | 0x100000000ull | static_cast<u64>(ctx->clear_rgba[2]) << 16 |
             static_cast<u64>(ctx->clear_rgba[1]) << 8 | static_cast<u64>(ctx->clear_rgba[3]) << 24;
    clr[8] = static_cast<u64>(static_cast<s64>(static_cast<s32>(0x800 - half_w))) << 4 |
             static_cast<u64>(static_cast<s64>(static_cast<s32>(0x800 - half_h))) << 0x14;
    clr[10] = static_cast<u64>(static_cast<s64>(static_cast<s32>(half_w + 0x800))) << 4 |
              static_cast<u64>(static_cast<s64>(static_cast<s32>(half_h + 0x800))) << 0x14;
    clr[7] = 1;      // RGBAQ
    clr[0xB] = 5;    // XYZ2
    clr[0xD] = 0x47; // TEST_1
    clr[0xC] = static_cast<u64>(ctx->ztest) << 0x11 | 0x10000;
    clr[3] = 0x47;   // TEST_1
    clr[5] = 0;      // PRIM
    clr[9] = 5;      // XYZ2
    if (ctx->draw_mode == 1) {
        clr[0xB] = 0xD;  // XYZ3
        clr[9] = 0xD;
    }
    ApplyDisplayOffset(0x002A15CC);
}

// EE: 0x00104920
// Returns 1 for an even field: sceGsSyncV returns CSR.FIELD (1 = odd).
u32 WaitVSync() {
    sceGsSyncPath(0, 0);
    return sceGsSyncV(0) == 0 ? 1 : 0;
}

// EE: 0x00104950
void UpdateDisplayFrame(u32 even_field) {
    if ((Ctx()->disp_flags & 0x4000) == 0) {
        DrawDisplayTransfer((Ctx()->disp_flags & 0x8000) == 0 ? 1 : 0, even_field,
                            g_pMainContext().addr + offsetof(MainContext, brightness_r));
    }
    Ctx()->disp_flags &= 0x5FFF;
    PutDispAndDrawEnv();
}

// EE: 0x001049B0
// HYPOTHESIS: DrawDisplayTransfer. Builds a GIF packet at 0x00420000 that fills (mode 0) or copies
// the 448-line draw buffer into the 224-line display buffer in 64-pixel strips for the current
// field (modes 1/2, optional alpha blend with flag 0x2000), then sends it on DMA channel 2.
void DrawDisplayTransfer(u32 mode, u64 field, u32 brightness) {
    MainContext* ctx = Ctx();
    if (mode >= 3) return;
    auto Q = [](u32 address) -> u64& { return GRef<u64>(address); };
    Q(0x00420010) = 0x108B400000000008ull;
    Q(0x00420018) = 0xE;
    u32 width = ctx->disp_w;
    if (ctx->disp_w > 0x280) width = 0x280;
    const u64 dispfb = ctx->dispenv[2];
    Q(0x00420020) = (static_cast<u64>(static_cast<s64>(static_cast<s32>(dispfb))) & 0x1FF) |
                    ((static_cast<u64>(static_cast<s64>(dispfb << 0x17) >> 0x20) & 0x3F) << 0x10) |
                    0xFF00000000000000ull;  // FRAME_1 = display buffer, alpha masked
    Q(0x00420030) = static_cast<u64>(static_cast<s64>(static_cast<s32>(width - 1))) << 16 |
                    static_cast<u64>(static_cast<s64>(static_cast<s32>(ctx->disp_h - 1))) << 0x30;
    Q(0x00420028) = 0x4C;
    const s64 field_off = (field & 1) != 0 ? 0 : 0x10;
    Q(0x00420038) = 0x40;
    const s64 wpix = static_cast<s64>(static_cast<s32>(width * 0x10));
    Q(0x00420040) = 0x131000000ull;
    const s64 hpix = static_cast<s64>(static_cast<s32>(static_cast<u32>(ctx->disp_h) << 4));
    Q(0x00420048) = 0x4E;
    const s32 draw_h16 = static_cast<s32>(ctx->draw_h) * 0x10;
    Q(0x00420050) = 0x30000;
    Q(0x00420058) = 0x47;
    Q(0x00420068) = 0x18;
    Q(0x00420070) = 0x60;
    Q(0x00420078) = 0x14;
    Q(0x00420080) = 5;
    Q(0x00420088) = 8;
    Q(0x00420098) = 0x3F;
    Q(0x00420060) = 0;
    u32 p = 0x004200A0;
    u32 end;
    if (mode == 0) {
        Q(0x004200B0) = 6;
        Q(0x004200A0) = 0x4400000000008001ull;
        Q(0x004200A8) = 0x5510;
        const u64 color = LerpRgba(0, GRef<u32>(g_pMainContext().addr + offsetof(MainContext, clear_rgba)), brightness);
        Q(0x004200C8) = static_cast<u64>(wpix) | static_cast<u64>(hpix) << 0x10;
        Q(0x004200B8) = color & 0xFFFFFFFFull;
        Q(0x004200C0) = 0;
        end = 0x004200D0;
    } else {
        if ((ctx->disp_flags & 0x2000) != 0) {
            Q(0x004200A0) = 0x10AB400000000001ull;
            Q(0x004200B0) = static_cast<u64>(ctx->clear_rgba[3]) << 0x20 | 100;
            Q(0x004200A8) = 0xE;
            mode = 1;
            Q(0x004200B8) = 0x42;
            p = 0x004200C0;
        }
        Q(p + 8) = 0x16;
        Q(p) = 0x2400000000000001ull;
        const u64 frame1 = ctx->drawenv_pkt[2];
        Q(p + 16) = (frame1 & 0x1FF) << 5 | (frame1 >> 0x10 & 0x3F) << 0xE | 0x2A8000000ull;
        const u64 color = LerpRgba(0, 0x808080, brightness);
        const s32 strips = static_cast<s32>(width * 0x10) >> 10;
        Q(p + 24) = color & 0xFFFFFFFFull;
        Q(p + 40) = 0x5353;
        Q(p + 32) = static_cast<u64>(static_cast<s64>(strips)) | 0x4400000000008000ull;
        u32 q = p + 48;
        const s32 foff = static_cast<s32>(field_off);
        if (wpix != 0) {
            s64 x = 0;
            s64 next;
            do {
                const s32 xi = static_cast<s32>(x);
                next = static_cast<s64>(static_cast<s32>(xi + 0x400));
                Q(q + 8) = static_cast<u64>(x);
                Q(q) = static_cast<u64>(static_cast<s64>(xi + 8)) | static_cast<u64>(field_off) << 0x10;
                Q(q + 16) = static_cast<u64>(next) | static_cast<u64>(static_cast<s64>(foff + draw_h16)) << 0x10;
                Q(q + 24) = static_cast<u64>(static_cast<s64>(xi + 0x3F8)) | static_cast<u64>(hpix) << 0x10;
                q += 32;
                x = next;
            } while (next < wpix);
        }
        end = q;
        if (mode == 2) {
            Q(q) = 0x10AB400000000001ull;
            end = q + 48;
            Q(q + 8) = 0xE;
            Q(q + 16) = 0x4000000064ull;
            Q(q + 24) = 0x42;
            Q(q + 32) = static_cast<u64>(static_cast<s64>(strips)) | 0x4400000000008000ull;
            Q(q + 40) = 0x5353;
            if (wpix != 0) {
                s64 x = 0;
                s64 next;
                do {
                    const s32 xi = static_cast<s32>(x);
                    next = static_cast<s64>(static_cast<s32>(xi + 0x400));
                    Q(end + 8) = static_cast<u64>(x);
                    Q(end) = static_cast<u64>(static_cast<s64>(xi + 8)) |
                             static_cast<u64>(static_cast<s64>(foff + 0x10)) << 0x10;
                    Q(end + 16) = static_cast<u64>(next) |
                                  static_cast<u64>(static_cast<s64>(foff + draw_h16 + 0x10)) << 0x10;
                    Q(end + 24) = static_cast<u64>(static_cast<s64>(xi + 0x3F8)) | static_cast<u64>(hpix) << 0x10;
                    end += 32;
                    x = next;
                } while (next < wpix);
            }
        }
    }
    GRef<u32>(0x00420004) = 0;
    GRef<u32>(0x00420008) = 0;
    GRef<u32>(0x00420000) = ((end - 0x00420001u) >> 4) | 0x70000000u;  // DMA end tag + qwc
    FlushCache(0);
    sceDmaSend(sceDmaGetChan(2), 0x00420000);
    sceGsSyncPath(0, 0);
}

// EE: 0x00104E28
void PutDispAndDrawEnv() {
    MainContext* ctx = Ctx();
    const u32 ctx_addr = g_pMainContext().addr;
    sceGsPutDispEnv(ctx_addr + offsetof(MainContext, dispenv));
    sceGsSyncPath(0, 0);
    if ((Ctx()->disp_flags & 0x100) != 0) {
        sceGsResetGraph(0, 1, 2, 0);
        Ctx()->disp_flags &= 0xFEFF;
    } else if ((Ctx()->disp_flags & 0x200) != 0) {
        sceGsResetGraph(0, 1, 2, 1);
        Ctx()->disp_flags &= 0xFDFF;
    }
    // Two DMA tags on the stack: ref (0x15 qwords of the draw-environment packet), then end.
    guest::GuestFrame tags(0x20);
    const u32 t = tags.Address();
    GRef<u32>(t + 0x00) = 0x30000015;
    GRef<u32>(t + 0x04) = ctx_addr + offsetof(MainContext, drawenv_pkt);
    GRef<u32>(t + 0x08) = 0;
    GRef<u32>(t + 0x0C) = 0;
    GRef<u32>(t + 0x10) = 0x70000000;
    GRef<u32>(t + 0x14) = 0;
    GRef<u32>(t + 0x18) = 0;
    GRef<u32>(t + 0x1C) = 0;
    (void)ctx;
    FlushCache(0);
    sceDmaSend(sceDmaGetChan(2), t);
    sceGsSyncPath(0, 0);
}

// EE: 0x00105028
// HYPOTHESIS: LerpRgba. MMI + VU0 (translated from the disassembly):
//   pextlb/psubh/pextlh: d[i] = b.byte[i] - a.byte[i] (16-bit), zero-extended into word i;
//   lqc2/vftoi12/qmfc2: t = (int)(vec[i] * 4096) for the 4 floats at `vec` (saturating);
//   pmulth: p[i] = (s16)d[i] * (s16)t[i];  psraw 12;  ppach: low halfwords;
//   paddsw on 32-bit words of the halfword-packed values (a borrow can cross into the next
//   channel), pmaxw 0, pminh 0xFF per halfword, ppacb: low bytes.
u64 LerpRgba(u64 a, u64 b, u32 vec) {
    u16 ah[4], bh[4];
    for (int i = 0; i < 4; ++i) {
        ah[i] = static_cast<u16>((a >> (8 * i)) & 0xFF);
        bh[i] = static_cast<u16>((b >> (8 * i)) & 0xFF);
    }
    s16 prod_h[4];
    for (int i = 0; i < 4; ++i) {
        const s16 d = static_cast<s16>(static_cast<u16>(bh[i] - ah[i]));
        const f32 v = GRef<f32>(vec + 4u * static_cast<u32>(i));
        // vftoi12: v * 4096 converted toward zero, saturated to the int range.
        const double scaled = static_cast<double>(v) * 4096.0;
        s32 fixed;
        if (scaled >= 2147483647.0) fixed = 0x7FFFFFFF;
        else if (scaled <= -2147483648.0) fixed = static_cast<s32>(0x80000000u);
        else fixed = static_cast<s32>(scaled);
        const s32 product = static_cast<s32>(d) * static_cast<s32>(static_cast<s16>(fixed & 0xFFFF));
        prod_h[i] = static_cast<s16>(product >> 12);  // psraw 12, ppach keeps the low halfword
    }
    // Words of the halfword-packed operands: word0 = h0 | h1<<16, word1 = h2 | h3<<16.
    u16 out_h[4];
    for (int w = 0; w < 2; ++w) {
        const s32 x = static_cast<s32>(static_cast<u32>(ah[2 * w]) | static_cast<u32>(ah[2 * w + 1]) << 16);
        const s32 y = static_cast<s32>(static_cast<u32>(static_cast<u16>(prod_h[2 * w])) |
                                       static_cast<u32>(static_cast<u16>(prod_h[2 * w + 1])) << 16);
        s64 sum = static_cast<s64>(x) + y;  // paddsw: signed saturation
        if (sum > 0x7FFFFFFF) sum = 0x7FFFFFFF;
        if (sum < -0x80000000ll) sum = -0x80000000ll;
        s32 word = static_cast<s32>(sum);
        if (word < 0) word = 0;  // pmaxw with zero
        out_h[2 * w] = static_cast<u16>(word & 0xFFFF);
        out_h[2 * w + 1] = static_cast<u16>(static_cast<u32>(word) >> 16);
    }
    u64 result = 0;
    for (int i = 0; i < 4; ++i) {
        s16 h = static_cast<s16>(out_h[i]);
        if (h > 0xFF) h = 0xFF;  // pminh (signed)
        result |= static_cast<u64>(static_cast<u8>(h & 0xFF)) << (8 * i);
    }
    return result;  // bytes 4..7 come from the zero halfwords of the upper lanes
}

// EE: 0x00103C00
// Sets the DMAC scratchpad ring (RBOR 0x00418000, RBSR 0x7FF0) and clears D_CTRL.MFD; the ring is
// only used by dead code (rendering.md).
void InitSprRingBuffer() {
    sce::EeHwWrite(sce::EeReg::kDmacRbor, 0x00418000);
    sce::EeHwWrite(sce::EeReg::kDmacRbsr, 0x7FF0);
    const u32 ctrl = sce::EeHwRead(sce::EeReg::kDmacCtrl);
    GRef<u32>(0x00411798) = 0x70000000;
    GRef<u32>(0x0041179C) = 0;
    GRef<u32>(0x004117A0) = 0;
    sce::EeHwWrite(sce::EeReg::kDmacCtrl, ctrl & 0xFFFFFFF3u);
}

// EE: 0x00103D80
// Dead (only caller 0x00103CC8 is unreferenced).
u32 GetSprWorkBuffer() {
    return GRef<u32>(0x00411798) + GRef<u32>(0x0041179C) * 0x2000;
}

// EE: 0x00103D98
// Dead (only caller 0x00103CC8 is unreferenced): starts a scratchpad -> RAM DMA (ch8) once the
// GIF ring has room. Translated as in the original; never runs.
void FlushSprWorkBuffer(u32 sadr, s32 qwc) {
    while ((sce::EeHwRead(sce::EeReg::kD8Chcr) & 0x100) != 0) {
    }
    sce::EeHwWrite(sce::EeReg::kD8Sadr, sadr);
    sce::EeHwWrite(sce::EeReg::kD8Qwc, static_cast<u32>(qwc));
    u32 room;
    do {
        const s32 tadr = static_cast<s32>(sce::EeHwRead(sce::EeReg::kD2Tadr));
        const s32 madr = static_cast<s32>(sce::EeHwRead(sce::EeReg::kD8Madr));
        room = tadr == madr ? 0x8000u : static_cast<u32>(tadr - madr) & 0x7FF0u;
    } while (room <= static_cast<u32>(qwc << 4));
    sce::EeHwWrite(sce::EeReg::kD8Chcr, sce::EeHwRead(sce::EeReg::kD8Chcr) | 0x100);
    GRef<u32>(0x0041179C) ^= 1;
}

// EE: 0x00103C50
// Dead (no reference): waits for the GIF channel, sets D_CTRL bits 2-3 (stall control), points the
// fromSPR MADR and the GIF TADR at the ring 0x00418000 and starts the GIF chain (CHCR 0x104); flag
// 0x004117A0 = 1. Translated as in the original; never runs.
void func_00103C50() {
    GRef<u32>(0x004117A0) = 1;
    while ((sce::EeHwRead(sce::EeReg::kD2Chcr) & 0x100) != 0) {
    }
    sce::EeHwWrite(sce::EeReg::kDmacCtrl, sce::EeHwRead(sce::EeReg::kDmacCtrl) | 0xC);
    sce::EeHwWrite(sce::EeReg::kD8Madr, 0x00418000);
    sce::EeHwWrite(sce::EeReg::kD2Tadr, 0x00418000);
    sce::EeHwWrite(sce::EeReg::kD2Chcr, 0x104);
}

// EE: 0x00103CC8
// Dead (no reference): closes an empty VIF1 packet in the scratchpad work buffer, sends it to RAM
// (FlushSprWorkBuffer), waits for the GIF channel, clears flag 0x004117A0 and D_CTRL bits 2-3.
// Translated as in the original; never runs.
void func_00103CC8() {
    guest::GuestFrame frame(0x10);
    const u32 pkt = frame.Address();
    sceVif1PkInit(pkt, GetSprWorkBuffer());
    sceVif1PkReset(pkt);
    sceVif1PkEnd(pkt, 0);
    sceVif1PkTerminate(pkt);
    const u32 sadr = GRef<u32>(pkt + 4) & 0x3FF0;
    FlushSprWorkBuffer(sadr, static_cast<s32>(sceVif1PkSize(pkt)));
    while ((sce::EeHwRead(sce::EeReg::kD2Chcr) & 0x100) != 0) {
    }
    GRef<u32>(0x004117A0) = 0;
    sce::EeHwWrite(sce::EeReg::kDmacCtrl, sce::EeHwRead(sce::EeReg::kDmacCtrl) & 0xFFFFFFF3u);
}

// EE: 0x00105070
// Dead (no reference): per-byte blend of two packed colours, a + ((b - a) x (s16)(t x 4096) >> 12)
// clamped to 0-255 (MMI pextlb / pmulth / paddsh / ppacb). The register operands are the 32-bit
// arguments sign-extended to 64 bits, so bytes 4-7 come from their sign; the result's eight bytes
// are returned.
u64 func_00105070(u32 a, u32 b, f32 t) {
    const s16 k = static_cast<s16>(fpu::FloatToInt(t * 4096.0f));
    const u64 a64 = static_cast<u64>(static_cast<s64>(static_cast<s32>(a)));
    const u64 b64 = static_cast<u64>(static_cast<s64>(static_cast<s32>(b)));
    u64 out = 0;
    for (u32 i = 0; i < 8; ++i) {
        const s16 ah = static_cast<s16>((a64 >> (8 * i)) & 0xFF);
        const s16 bh = static_cast<s16>((b64 >> (8 * i)) & 0xFF);
        const s16 d = static_cast<s16>(bh - ah);
        const s16 prod = static_cast<s16>((static_cast<s32>(d) * k) >> 12);
        s32 sum = static_cast<s32>(ah) + prod;  // paddsh
        if (sum > 0x7FFF) sum = 0x7FFF;
        if (sum < -0x8000) sum = -0x8000;
        if (sum < 0) sum = 0;          // pmaxh with 0
        if (sum > 0xFF) sum = 0xFF;    // pminh with 0xFF
        out |= static_cast<u64>(static_cast<u8>(sum)) << (8 * i);
    }
    return out;
}

// EE: 0x00105210
// Dead (no reference): scratchpad -> RAM DMA (channel 8) of `qwc` quadwords from `sadr` to `madr`
// after the channel is idle.
void func_00105210(u32 qwc, u32 sadr, u32 madr) {
    while ((sce::EeHwRead(sce::EeReg::kD8Chcr) & 0x100) != 0) {
    }
    sce::EeHwWrite(sce::EeReg::kD8Madr, madr);
    sce::EeHwWrite(sce::EeReg::kD8Sadr, sadr);
    sce::EeHwWrite(sce::EeReg::kD8Qwc, qwc);
    sce::EeHwWrite(sce::EeReg::kD8Chcr, 0x100);
}

// EE: 0x00105278
// Dead (no reference): waits until channel 8 is idle.
void func_00105278() {
    while ((sce::EeHwRead(sce::EeReg::kD8Chcr) & 0x100) != 0) {
    }
}

// EE: 0x001052A8
// Dead (no reference): RAM -> scratchpad DMA (channel 9) of `qwc` quadwords from `madr` to `sadr`
// after the channel is idle.
void func_001052A8(u32 qwc, u32 madr, u32 sadr) {
    while ((sce::EeHwRead(sce::EeReg::kD9Chcr) & 0x100) != 0) {
    }
    sce::EeHwWrite(sce::EeReg::kD9Madr, madr);
    sce::EeHwWrite(sce::EeReg::kD9Sadr, sadr);
    sce::EeHwWrite(sce::EeReg::kD9Qwc, qwc);
    sce::EeHwWrite(sce::EeReg::kD9Chcr, 0x100);
}

// EE: 0x00144D68
// Dead (no reference): starts a RAM -> scratchpad DMA (channel 9) of `bytes` (rounded up to
// quadwords) without waiting (the two CHCR reads around it are kept).
void func_00144D68(u32 madr, u32 sadr, s32 bytes) {
    (void)sce::EeHwRead(sce::EeReg::kD9Chcr);
    sce::EeHwWrite(sce::EeReg::kD9Madr, madr);
    sce::EeHwWrite(sce::EeReg::kD9Sadr, sadr);
    const s32 t = bytes + 0xF;
    sce::EeHwWrite(sce::EeReg::kD9Qwc, static_cast<u32>((-1 < t ? t : bytes + 0x1E) >> 4));
    sce::EeHwWrite(sce::EeReg::kD9Chcr, sce::EeHwRead(sce::EeReg::kD9Chcr) | 0x100);
    (void)sce::EeHwRead(sce::EeReg::kD9Chcr);
}

}  // namespace ac04::game
