// 2D packet helpers (docs/subsystems/rendering.md "Packet transport"): the per-frame bucket list
// of DMA chains, GIM texture upload packets, the screen fade quad, and the 2D camera/viewport
// block used by scenes 4-6. All packets are built in the current double-buffer half
// (MainContext::cur_buf) exactly as on the PS2, so RAM comparisons see the same bytes.
//
// Bucket list (InitBucketList): {u32 tags, u32 heads, u32 count}. `tags` is an array of `count`
// DMA "next" tags (one per bucket) followed by an "end" tag; `heads[i]` points at the last tag
// appended to bucket i. Appending (EndPacket/LinkPacket) inserts a new "next" tag after the
// bucket's current last tag.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

constexpr u32 kPacketMark = 0x004117A4;  // gp-0x424C: start of the packet being built

u32& CurBuf() { return Ctx()->cur_buf.addr; }

void Put(u32 p, u32 i, u32 v) { GRef<u32>(p + 4 * i) = v; }

}  // namespace

// EE: 0x00101060
// Allocates `qwords` quadwords from the work heap (16-byte aligned).
u32 AllocQwords(s32 qwords) { return WorkHeapAlloc(qwords << 4, 0x10); }

// EE: 0x00103E48
// Allocates both halves of a packet double buffer, `qwords` quadwords each.
void AllocDoubleBuffer(u32 db, s32 qwords) {
    GRef<u32>(db) = AllocQwords(qwords);
    GRef<u32>(db + 4) = AllocQwords(qwords);
}

// EE: 0x00103ED0
// Closes a packet that starts with a DMA tag at `tag` and ends at `end`: a "cnt" tag with a VIF
// DIRECT of the same size when it fits in 0xFFFF quadwords, otherwise a "next" tag followed by
// "ref" tags of 0xFFFF quadwords each, appended at `end`. Returns the new end.
u32 CloseDmaPacket(u32 tag, u32 end) {
    u32 qwc = (end + (0xFFFFFFFFu - tag)) >> 4;
    if (qwc < 0x10000) {
        Put(tag, 3, qwc | 0x50000000);
        Put(tag, 0, qwc | 0x10000000);
        Put(tag, 1, 0);
        Put(tag, 2, 0);
        return end;
    }
    Put(tag, 1, end & 0x0FFFFFFF);
    u32 addr = (tag + 0x100000) & 0x0FFFFFFF;
    Put(tag, 0, 0x2000FFFF);
    Put(tag, 3, 0x5000FFFF);
    Put(tag, 2, 0);
    for (;;) {
        qwc -= 0xFFFF;
        if (static_cast<s32>(qwc) <= 0xFFFE) break;
        Put(end, 1, addr);
        Put(end, 0, 0x3000FFFF);
        addr += 0xFFFF0;
        Put(end, 2, 0);
        Put(end, 3, 0x5000FFFF);
        end += 0x10;
    }
    if (static_cast<s32>(qwc) > 0) {
        Put(end, 0, qwc | 0x30000000);
        Put(end, 1, addr);
        Put(end, 3, qwc | 0x50000000);
        Put(end, 2, 0);
        end += 0x10;
    }
    return end;
}

// EE: 0x00103FD8
// Initialises a bucket list with `count` buckets in the current packet buffer.
void InitBucketList(u32 list, u32 count) {
    MainContext* ctx = Ctx();
    GRef<u32>(list) = ctx->cur_buf.addr;
    u32 words = count >> 2;
    if ((count & 3) != 0) words = (count >> 2) + 1;
    const u32 base = ctx->cur_buf.addr;
    GRef<u32>(list + 8) = count;
    const u32 heads = base + count * 0x10 + 0x10;
    GRef<u32>(list + 4) = heads;
    ctx->cur_buf.addr = heads + words * 0x10;
    for (s32 i = 0; i < static_cast<s32>(count); ++i) {
        const u32 t = GRef<u32>(list) + static_cast<u32>(i) * 0x10;
        Put(t, 0, 0);
        Put(t, 1, 0);
        Put(t, 2, 0);
        Put(t, 3, 0);
        Put(t, 0, 0x20000000);
        Put(t, 1, t + 0x10);
        GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(i) * 4) = t;
    }
    const u32 t = GRef<u32>(list) + count * 0x10;
    Put(t, 0, 0);
    Put(t, 1, 0);
    Put(t, 2, 0);
    Put(t, 3, 0);
    Put(t, 0, 0x70000000);
    GRef<u32>(kPacketMark) = 0;
}

// EE: 0x00104100
// Marks the start of a new packet at the current buffer position and returns it.
u32 BeginPacket() {
    const u32 p = Ctx()->cur_buf.addr;
    GRef<u32>(kPacketMark) = p;
    return p;
}

// EE: 0x00104118
// Ends the packet started by BeginPacket at `end` and links it into bucket `index`. The data
// already sits at cur_buf; the link tag goes right after it. An empty packet is dropped.
u32 EndPacket(u32 list, s32 index, u32 end) {
    const u32 start = GRef<u32>(kPacketMark);
    if (start == end) {
        GRef<u32>(kPacketMark) = 0;
        return start;
    }
    const u32 data = Ctx()->cur_buf.addr;
    const u32 tag = data + ((end - start) & 0xFFFFFFF0u);
    const u32 slot = GRef<u32>(list + 4) + static_cast<u32>(index) * 4;
    const u32 last = GRef<u32>(slot);
    Ctx()->cur_buf.addr = tag + 0x10;
    Put(tag, 0, 0);
    Put(tag, 1, 0);
    Put(tag, 2, 0);
    Put(tag, 3, 0);
    Put(tag, 0, 0x20000000);
    Put(tag, 1, GRef<u32>(last + 4));
    GRef<u32>(slot) = tag;
    GRef<u32>(last + 4) = data;
    GRef<u32>(kPacketMark) = 0;
    return data;
}

// EE: 0x00104198
// Links the packet [start, end) into bucket `index`, with a "next" tag written at `end`.
u32 LinkPacket(u32 list, s32 index, u32 start, u32 end) {
    MainContext* ctx = Ctx();
    const u32 slot = GRef<u32>(list + 4) + static_cast<u32>(index) * 4;
    const u32 last = GRef<u32>(slot);
    ctx->cur_buf.addr = end + 0x10;
    Put(end, 0, 0);
    Put(end, 1, 0);
    Put(end, 2, 0);
    Put(end, 3, 0);
    Put(end, 0, 0x20000000);
    Put(end, 1, GRef<u32>(last + 4));
    GRef<u32>(slot) = end;
    GRef<u32>(last + 4) = start;
    return ctx->cur_buf.addr;
}

// EE: 0x001041E8
// Appends a DMA "cnt" packet of `qwords` quadwords (tag words 2/3 = w2, w3) to bucket `index` at
// cur_buf and returns its data address.
u32 AllocBucketPacket(u32 list, s32 index, u32 qwords, u32 w2, u32 w3) {
    const u32 tag = CurBuf();
    CurBuf() = tag + qwords * 16 + 16;
    const u32 slot = GRef<u32>(list + 4) + static_cast<u32>(index) * 4;
    const u32 last = GRef<u32>(slot);
    Put(tag, 0, 0);
    Put(tag, 1, 0);
    Put(tag, 2, 0);
    Put(tag, 3, 0);
    Put(tag, 0, qwords | 0x20000000);
    Put(tag, 2, w2);
    Put(tag, 1, GRef<u32>(last + 4));
    Put(tag, 3, w3);
    GRef<u32>(last + 4) = tag;
    GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4) = tag;
    return tag + 16;
}

// EE: 0x001042D0
// Appends to bucket `index` a DMA "ref" tag to `qwords` quadwords at `addr` (a VU program packet),
// followed by a "next" tag that continues the bucket.
void AddBucketRef(u32 list, s32 index, u32 addr, u32 qwords) {
    const u32 p = CurBuf();
    const u32 slot = GRef<u32>(list + 4) + static_cast<u32>(index) * 4;
    const u32 last = GRef<u32>(slot);
    CurBuf() = p + 0x20;
    for (u32 i = 0; i < 8; ++i) Put(p, i, 0);
    Put(p, 0, qwords | 0x30000000);
    Put(p, 1, addr);
    Put(p, 4, 0x20000000);
    Put(p, 5, GRef<u32>(last + 4));
    GRef<u32>(last + 4) = p;
    GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4) = p + 0x10;
}

// EE: 0x00104090
// Links a whole bucket list `sub` (head, tail, count) into bucket `index` of `list`: a "next" tag at
// cur_buf continues the bucket after the sub list, whose end tag jumps back to it.
void func_00104090(u32 list, s32 index, u32 sub) {
    const u32 p = CurBuf();
    const u32 last = GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4);
    CurBuf() = p + 0x10;
    for (u32 i = 0; i < 4; ++i) Put(p, i, 0);
    Put(p, 0, 0x20000000);
    Put(p, 1, GRef<u32>(last + 4));
    GRef<u32>(last + 4) = GRef<u32>(sub);
    const u32 head = GRef<u32>(sub);
    GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4) = p;
    const u32 end = head + GRef<u32>(sub + 8) * 0x10;
    for (u32 i = 0; i < 4; ++i) Put(end, i, 0);
    Put(end, 1, p);
    Put(end, 0, 0x20000000);
}

// EE: 0x00104250
// Appends an empty "cnt" tag carrying the VIF words w2, w3 to bucket `index` at cur_buf (cur_buf is
// not advanced; the caller does) and returns the address after the tag.
u32 func_00104250(u32 list, s32 index, u32 w2, u32 w3) {
    const u32 p = CurBuf();
    const u32 last = GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4);
    for (u32 i = 0; i < 4; ++i) Put(p, i, 0);
    Put(p, 0, 0x20000000);
    Put(p, 2, w2);
    Put(p, 1, GRef<u32>(last + 4));
    Put(p, 3, w3);
    GRef<u32>(last + 4) = p;
    GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4) = p;
    return p + 0x10;
}

// EE: 0x001042A0
// A "cnt" packet of n quadwords at cur_buf (tag word 0 only); returns the tag address.
u32 func_001042A0(u32 n) {
    const u32 p = CurBuf();
    CurBuf() = p + n * 16 + 16;
    Put(p, 0, n | 0x20000000);
    return p;
}

// EE: 0x00104338
// Appends to bucket `index` a DMA "call" tag (0x50000000 | n, target) with n inline quadwords, then
// a "next" tag continuing the bucket; returns the inline data address.
u32 func_00104338(u32 list, s32 index, u32 target, u32 n) {
    const u32 p = CurBuf();
    CurBuf() = p + n * 16 + 32;
    const u32 last = GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4);
    for (u32 i = 0; i < 4; ++i) Put(p, i, 0);
    Put(p, 0, n | 0x50000000);
    Put(p, 1, target);
    const u32 next = p + n * 16 + 16;
    for (u32 i = 0; i < 4; ++i) Put(next, i, 0);
    Put(next, 0, 0x20000000);
    Put(next, 1, GRef<u32>(last + 4));
    GRef<u32>(last + 4) = p;
    GRef<u32>(GRef<u32>(list + 4) + static_cast<u32>(index) * 4) = next;
    return p + 16;
}

// EE: 0x001043E8
// Appends bucket list `sub` to packet list `pl` (head, tail): a "next" tag at the tail to the sub
// list's head; the tail becomes the sub list's end tag.
void func_001043E8(u32 pl, u32 sub) {
    const u32 tail = GRef<u32>(pl + 4);
    GRef<u32>(tail) = 0x20000000;
    GRef<u32>(tail + 4) = GRef<u32>(sub);
    GRef<u32>(pl + 4) = GRef<u32>(sub) + GRef<u32>(sub + 8) * 0x10;
}

// EE: 0x001043C0
// Points a DMA packet list (head, tail) at a bucket list: head = first bucket tag, tail = its end
// tag.
u32 SetPacketListBuckets(u32 packet_list, u32 list) {
    const u32 count = GRef<u32>(list + 8);
    GRef<u32>(packet_list) = GRef<u32>(list);
    GRef<u32>(packet_list + 4) = GRef<u32>(list) + count * 0x10;
    return GRef<u32>(list);
}

// ---- GIM textures --------------------------------------------------------------------------------
// Texture object (0x28 bytes, docs/formats/GIM.md): +0 GIM, +4 image header, +8 CLUT header (or 0),
// +0xC first GIM word, +0x10 flag bit 0, +0x11 flag bit 1, +0x14 pixel data, +0x18 pixel bytes,
// +0x1C CLUT data, +0x20 CLUT bytes, +0x24 log2 width, +0x26 log2 height.

namespace {

// Smallest n (0..10) with 2^n >= size, as the original's compare chain computes it.
u16 Log2Ceil(u16 size) {
    if (size < 2) return 0;
    u16 n = 1;
    for (u32 limit = 2; n < 10 && size > limit; limit <<= 1) ++n;
    return n;
}

}  // namespace

// EE: 0x00105A08
void InitGimTexture(u32 tex, u32 gim) {
    const u32 first = GRef<u32>(gim);
    GRef<u32>(tex) = gim;
    GRef<u32>(tex + 0xC) = first;
    GRef<u8>(tex + 0x10) = static_cast<u8>(GRef<u32>(gim + 4) & 1);
    const u32 flags = GRef<u32>(gim + 4);
    GRef<u32>(tex + 4) = gim + 0x10;
    GRef<u32>(tex + 0x14) = gim + 0x20;
    GRef<u8>(tex + 0x11) = static_cast<u8>((flags >> 1) & 1);
    GRef<u8>(gim + 0x12) = static_cast<u8>((GRef<u16>(gim + 0x1C) + 0x3F) >> 6);
    u32 hdr = GRef<u32>(tex + 4);
    if (static_cast<u32>(GRef<u8>(hdr + 3)) - 0x13 < 2) {
        // 8-bit and 4-bit CLUT images: the buffer width is rounded up to an even value.
        GRef<u8>(hdr + 2) = static_cast<u8>(GRef<u8>(hdr + 2) + (GRef<u8>(hdr + 2) & 1));
        hdr = GRef<u32>(tex + 4);
    }
    const u32 w = GRef<u16>(hdr + 0xC);
    const u32 h = GRef<u16>(hdr + 0xE);
    switch (GRef<u8>(hdr + 3)) {
        case 0x00: case 0x01: case 0x30: case 0x31:
            GRef<u32>(tex + 0x18) = w * h * 4;
            break;
        case 0x02: case 0x0A: case 0x32: case 0x3A:
            GRef<u32>(tex + 0x18) = w * h * 2;
            break;
        case 0x13: case 0x1B:
            GRef<u32>(tex + 0x18) = w * h;
            break;
        case 0x14: case 0x24: case 0x2C:
            GRef<s32>(tex + 0x18) = static_cast<s32>(w * h + 1) / 2;
            break;
        default:
            GRef<u32>(tex + 0x18) = 0;
            break;
    }
    GRef<u16>(tex + 0x24) = Log2Ceil(GRef<u16>(hdr + 0xC));
    GRef<u16>(tex + 0x26) = Log2Ceil(GRef<u16>(hdr + 0xE));
    if ((GRef<u32>(GRef<u32>(tex) + 4) & 1) == 0) {
        GRef<u32>(tex + 8) = 0;
        GRef<u32>(tex + 0x1C) = 0;
    } else {
        GRef<u32>(tex + 0x1C) = gim + ((GRef<u32>(tex + 0x18) + 0x3F) & 0xFFFFFFF0u);
        GRef<u32>(tex + 8) = gim + ((GRef<u32>(tex + 0x18) + 0x2F) & 0xFFFFFFF0u);
    }
    const u32 clut = GRef<u32>(tex + 8);
    if (clut != 0) {
        const u8 psm = GRef<u8>(clut + 3);
        if (psm == 0) {
            GRef<u32>(tex + 0x20) = static_cast<u32>(GRef<u16>(clut + 0xC)) * GRef<u16>(clut + 0xE) * 4;
            return;
        }
        if (psm == 2 || psm == 10) {
            GRef<u32>(tex + 0x20) = static_cast<u32>(GRef<u16>(clut + 0xC)) * GRef<u16>(clut + 0xE) * 2;
            return;
        }
    }
    GRef<u32>(tex + 0x20) = 0;
}

// EE: 0x00105CF8
// Sets the VRAM block addresses (TBP of the image, CBP of the CLUT) in the texture's headers.
void SetTextureVramAddress(u32 tex, u16 tbp, u16 cbp) {
    const u32 image = GRef<u32>(tex + 4);
    if (image != 0) GRef<u16>(image) = tbp;
    const u32 clut = GRef<u32>(tex + 8);
    if (clut != 0) GRef<u16>(clut) = cbp;
}

// EE: 0x00105D18
// Builds the upload of a texture (IMAGE transfers of the pixels in chunks of 0x4000 quadwords,
// then the CLUT), TEXFLUSH and the TEX0 setting for context `ctx_mode` (2: TEX0_2, else TEX0_1).
// `clut_loads` > 0 appends that many extra TEX2 CLUT-load packets (two per call). Returns the end.
u32 BuildTextureUpload(u32 tex, u32 p, s32 ctx_mode, u32 clut_loads) {
    if (tex == 0) return p;
    const u32 bytes = GRef<u32>(tex + 0x18);
    Put(p, 1, 0);
    Put(p, 0, 0x10000005);
    Put(p, 2, 0x11000000);
    Put(p, 3, 0x50000005);
    Put(p, 4, 4);
    Put(p, 6, 0xE);
    Put(p, 5, 0x10000000);
    const u32 img = GRef<u32>(tex + 4);
    Put(p, 8, 0);
    Put(p, 0xA, 0x50);
    Put(p, 9, GRef<u32>(img));
    Put(p, 0xE, 0x51);
    Put(p, 0xD, GRef<u32>(img + 8));
    Put(p, 0xC, 0);
    Put(p, 0x10, GRef<u16>(img + 0xC));
    Put(p, 0x12, 0x52);
    Put(p, 0x11, GRef<u16>(img + 0xE));
    Put(p, 0x16, 0x53);
    Put(p, 0x14, 0);
    const s32 sbytes = static_cast<s32>(bytes);
    const s32 qwc = (sbytes + 0xF < 0 ? sbytes + 0x1E : sbytes + 0xF) >> 4;
    const s32 chunks = (qwc + 0x3FFF < 0 ? qwc + 0x7FFE : qwc + 0x3FFF) >> 14;
    u32 q = p + 0x60;
    u32 src = GRef<u32>(tex + 0x14);
    const u32 clut = GRef<u32>(tex + 8);
    const u32 log_h = GRef<u16>(tex + 0x26);
    const u32 log_w = GRef<u16>(tex + 0x24);
    for (s32 i = 0; i < chunks; ++i) {
        Put(q, 1, 0);
        Put(q, 0, 0x10000001);
        Put(q, 2, 0);
        Put(q, 3, 0x50000001);
        const u32 n = qwc < (i + 1) * 0x4000 ? static_cast<u32>(qwc - i * 0x4000) : 0x4000u;
        Put(q, 4, n | 0x8000);
        Put(q, 5, 0x08000000);
        Put(q, 6, 0);
        Put(q, 8, n | 0x30000000);
        Put(q, 9, src);
        src += n * 0x10;
        Put(q, 0xA, 0);
        Put(q, 0xB, n | 0x50000000);
        q += 0x30;
    }
    if (clut != 0) {
        Put(q, 1, 0);
        Put(q, 0, 0x10000006);
        Put(q, 2, 0);
        Put(q, 3, 0x50000006);
        Put(q, 4, 4);
        Put(q, 5, 0x10000000);
        Put(q, 6, 0xE);
        Put(q, 8, 0);
        Put(q, 0xA, 0x50);
        Put(q, 9, GRef<u32>(clut));
        Put(q, 0xC, 0);
        Put(q, 0xD, GRef<u32>(clut + 8));
        Put(q, 0xE, 0x51);
        Put(q, 0x10, GRef<u16>(clut + 0xC));
        Put(q, 0x11, GRef<u16>(clut + 0xE));
        Put(q, 0x12, 0x52);
        Put(q, 0x14, 0);
        Put(q, 0x16, 0x53);
        const s32 cb = GRef<s32>(tex + 0x20);
        const u32 cq = static_cast<u32>((cb < 0 ? cb + 0xF : cb) >> 4);
        Put(q, 0x19, 0x08000000);
        Put(q, 0x1A, 0);
        Put(q, 0x18, cq | 0x8000);
        Put(q, 0x1C, cq | 0x30000000);
        Put(q, 0x1D, GRef<u32>(tex + 0x1C));
        Put(q, 0x1E, 0);
        Put(q, 0x1F, cq | 0x50000000);
        q += 0x80;
    }
    Put(q, 1, 0);
    Put(q, 0, 0x10000002);
    Put(q, 2, 0);
    Put(q, 3, 0x50000002);
    Put(q, 4, 0x8001);
    Put(q, 5, 0x10000000);
    Put(q, 6, 0xE);
    Put(q, 0xA, 0x3F);  // TEXFLUSH
    Put(q, 0xC, 0x10000003);
    Put(q, 0xD, 0);
    Put(q, 0xE, 0);
    Put(q, 0xF, 0x50000003);
    Put(q, 0x10, 0x8002);
    Put(q, 0x11, 0x10000000);
    Put(q, 0x12, 0xE);
    // TEX0: TBP | TBW << 14 | PSM << 20 | TW << 26 | TH << 30 (low word), TH >> 2 | TCC (bit 34)
    // | CBP << 37 | CPSM << 51 | CLD 1 (bit 61) when a CLUT is present (high word).
    const u32 tex0_lo = static_cast<u32>(GRef<u16>(img)) | log_h << 30 | static_cast<u32>(GRef<u8>(img + 3)) << 20 |
                        static_cast<u32>(GRef<u8>(img + 2)) << 14 | log_w << 26;
    Put(q, 0x14, tex0_lo);
    u32 tex0_hi = ((log_h >> 2) & 3) | 4;
    if (clut != 0) {
        tex0_hi |= static_cast<u32>(GRef<u16>(clut)) << 5 | static_cast<u32>(GRef<u8>(clut + 3)) << 19;
        if (ctx_mode != 2) tex0_hi |= 0x20000000;
    }
    Put(q, 0x15, tex0_hi);
    Put(q, 0x16, ctx_mode == 2 ? 7u : 6u);
    Put(q, 0x17, 0);
    Put(q, 0x1A, ctx_mode == 2 ? 9u : 8u);  // CLAMP_2 / CLAMP_1: WMS/WMT 0, MAXU/MAXV = size - 1
    Put(q, 0x18, (static_cast<u32>(GRef<u16>(img + 0xC)) - 1) * 0x4000);
    Put(q, 0x19, (static_cast<u32>(GRef<u16>(img + 0xE)) - 1) * 4);
    Put(q, 0x1D, 0);
    Put(q, 0x1C, 0x10000002);
    Put(q, 0x1E, 0);
    Put(q, 0x1F, 0x50000002);
    Put(q, 0x22, 0xE);
    Put(q, 0x20, 0x8001);
    Put(q, 0x21, 0x10000000);
    Put(q, 0x26, 0x3F);  // TEXFLUSH
    p = q + 0xA0;
    if (clut_loads != 0) {
        // Two identical TEX2 packets that reload the CLUT (loop counter 1, 0).
        for (s32 n = 1; n >= 0; --n) {
            Put(p, 0, 0x10000002);
            Put(p, 1, 0);
            Put(p, 2, 0);
            Put(p, 3, clut_loads | 0x6C02C000);
            Put(p, 4, 1);
            Put(p, 5, 0x10000000);
            Put(p, 6, 0xE);
            Put(p, 8, static_cast<u32>(GRef<u8>(img + 3)) << 20);
            if (clut == 0) {
                Put(p, 9, 0);
            } else {
                Put(p, 9, static_cast<u32>(GRef<u16>(clut)) << 5 | static_cast<u32>(GRef<u8>(clut + 3)) << 19 | 0x20000000);
            }
            Put(p, 10, ctx_mode == 2 ? 0x17u : 0x16u);  // TEX2_2 / TEX2_1
            Put(p, 12, 0x10000000);
            Put(p, 13, 0);
            Put(p, 14, 0);
            Put(p, 15, 0x14000000);
            p += 0x40;
        }
    }
    return p;
}

// EE: 0x001065E8
// TEX0 value of a texture (as BuildTextureUpload sets it, with CLD = 1 when it has a CLUT).
u64 GetTextureTex0(u32 tex) {
    const u32 clut = GRef<u32>(tex + 8);
    const u32 img = GRef<u32>(tex + 4);
    const u32 log_h = GRef<u16>(tex + 0x26);
    const u32 lo = static_cast<u32>(GRef<u16>(img)) | log_h << 30 | static_cast<u32>(GRef<u8>(img + 3)) << 20 |
                   static_cast<u32>(GRef<u8>(img + 2)) << 14 |
                   static_cast<u32>(static_cast<s32>(GRef<s16>(tex + 0x24)) << 26);
    u32 hi = ((log_h >> 2) & 3) | 4;
    if (clut != 0) {
        hi |= 0x20000000 | static_cast<u32>(GRef<u8>(clut + 3)) << 19 | static_cast<u32>(GRef<u16>(clut)) << 5;
    }
    return static_cast<u64>(hi) << 32 | lo;
}

// ---- Screen fade ---------------------------------------------------------------------------------

// EE: 0x001066D8
// DMA cnt + DIRECT + GIF A+D packet at `p` setting TEX0_1 of GIM texture record `tex` (the GIF
// tag's top word is left as it was); returns the end. TEX0 = TBP0 | TBW << 14 | PSM << 20 |
// TW/TH (+0x24) << 26 | TCC/TFX (+0x26) << 30; with a CLUT (+8) also CBP << 37, CPSM << 51, CSM.
u32 WriteTex0Tex1(u32 tex, u32 p) {
    Put(p, 1, 0);
    Put(p, 0, 0x10000002);
    Put(p, 2, 0);
    Put(p, 3, 0x50000002);  // DIRECT 2
    Put(p, 4, 0x8001);      // GIF tag: NLOOP 1, EOP, A+D
    Put(p, 5, 0x10000000);
    Put(p, 6, 0xE);
    const u32 q = p + 0x20;
    const u32 clut = GRef<u32>(tex + 8);
    const u32 img = GRef<u32>(tex + 4);
    const u32 mode = GRef<u16>(tex + 0x26);
    const u32 lo = GRef<u16>(img) | mode << 30 | static_cast<u32>(GRef<u8>(img + 3)) << 20 |
                   static_cast<u32>(GRef<u8>(img + 2)) << 14 | static_cast<u32>(static_cast<s32>(GRef<s16>(tex + 0x24)) << 26);
    Put(q, 0, lo);
    if (clut == 0) {
        Put(q, 1, ((mode >> 2) & 3) | 4);
    } else {
        Put(q, 1, ((mode >> 2) & 3) | 0x20000004u | static_cast<u32>(GRef<u8>(clut + 3)) << 19 |
                      static_cast<u32>(GRef<u16>(clut)) << 5);
    }
    Put(q, 2, 6);  // TEX0_1
    Put(q, 3, 0);
    return q + 0x10;
}

// EE: 0x00112820
// Writes vertical strips (at most 1024 units = 64 pixels wide, aligned to 64-pixel boundaries) of a
// sprite from corner `a` to corner `b` (XYZ2 values, x in the low 16 bits) after a REGLIST GIF
// tag {XYZ2, XYZ2}. Returns the end (or `p` when nothing was written).
u32 BuildSpriteStrips(u32 p, u64 a, u64 b) {
    const u32 x0 = static_cast<u32>(a) & 0xFFFF;
    u64 x = static_cast<u64>(static_cast<s64>(static_cast<s32>(x0)));
    s32 room = 0x400 - static_cast<s32>(static_cast<u32>(a) & 0x3FF);
    u64 strips = 0;
    u32 q = p;
    for (s32 left = static_cast<s32>((static_cast<u32>(b) & 0xFFFF) - x0);;) {
        q += 0x10;
        if (left <= 0) break;
        const s32 w = left < room ? left : room;
        GRef<u64>(q) = x | (a & 0xFFFFFFFFFFFF0000ull);
        x = static_cast<u64>(static_cast<s64>(static_cast<s32>(x) + w));
        room = 0x400;
        strips = static_cast<u64>(static_cast<s64>(static_cast<s32>(strips) + 1));
        GRef<u64>(q + 8) = x | (b & 0xFFFFFFFFFFFF0000ull);
        left -= w;
    }
    if (static_cast<s64>(strips) > 0) {
        GRef<u64>(p + 8) = 0x5D;  // REGLIST registers: XYZ2, XYZ2
        GRef<u64>(p) = strips | 0x2400000000008000ull;
        p = q;
    }
    return p;
}

// EE: 0x001130B8
// Fade quad over the rectangle [a, b): `mode` 0 = darken (FIX alpha (alpha+1)/2, black), else
// alpha blend with grey 0x80 and FIX `alpha`. Restores the draw environment's TEST/ALPHA/... registers
// afterwards and links the packet into bucket `index` of `list`.
void BuildFadeQuad(u32 list, s32 index, s64 mode, u32 alpha, u64 a, u64 b) {
    const u32 pkt = Ctx()->cur_buf.addr;
    const u32 g = pkt + 0x10;
    // GIF tag: PACKED, NREG 1 (A+D), NLOOP 5, PRE with PRIM 0x46 (sprite, alpha blend).
    GRef<u64>(pkt + 0x10) = 0x1023400000000005ull;
    GRef<u64>(g + 0x08) = 0xE;
    GRef<u64>(g + 0x10) = 0x00000001310000A8ull;  // ZBUF_1: ZBP 0xA8, PSMZ16S, ZMSK
    GRef<u64>(g + 0x18) = 0x4E;
    GRef<u64>(g + 0x20) = 0x30000;  // TEST_1: ZTE, ZTST ALWAYS
    GRef<u64>(g + 0x28) = 0x47;
    GRef<u64>(g + 0x38) = 0x18;  // XYOFFSET_1 = 0
    GRef<u64>(g + 0x30) = 0;
    // ALPHA_1 then RGBAQ: mode 0 (Cs-Cd)*FIX+Cd towards black, else Cs*FIX+Cd (additive grey).
    const u32 a8 = alpha & 0xFF;
    if (mode == 0) {
        GRef<u64>(g + 0x40) = static_cast<u64>((a8 + 1) >> 1) << 32 | 100;
        GRef<u64>(g + 0x48) = 0x42;
        GRef<u64>(g + 0x50) = 0x80000000;
        GRef<u64>(g + 0x58) = 1;
    } else {
        GRef<u64>(g + 0x40) = static_cast<u64>(a8) << 32 | 0x68;
        GRef<u64>(g + 0x48) = 0x42;
        GRef<u64>(g + 0x50) = 0x80808080;
        GRef<u64>(g + 0x58) = 1;
    }
    const u32 q = BuildSpriteStrips(g + 0x60, a, b);
    GRef<u64>(q) = 0x1000000000008003ull;
    GRef<u64>(q + 8) = 0xE;
    const u32 env = kMainContextAddress + offsetof(MainContext, drawenv_pkt);
    GRef<u128>(q + 0x10) = GRef<u128>(env + 0x20);
    GRef<u128>(q + 0x20) = GRef<u128>(env + 0x50);
    GRef<u128>(q + 0x30) = GRef<u128>(env + 0x30);
    LinkPacket(list, index, pkt, CloseDmaPacket(pkt, q + 0x40));
}

// EE: 0x00113248
// Screen fade over the camera's viewport with strength `fade` (0..1, scaled to 0..255).
void DrawScreenFade(f32 fade, u32 list, s32 index, s64 mode, u32 cam) {
    s32 a = static_cast<s32>(fade * 255.0f);  // cvt.w.s
    if (a <= 0) return;
    if (a > 0xFF) a = 0xFF;
    const s32 x = GRef<s16>(cam + 0x188);
    const s32 y = GRef<s16>(cam + 0x18A);
    const s32 w = GRef<s16>(cam + 0x18C);
    const s32 h = GRef<s16>(cam + 0x18E);
    const u64 p0 = static_cast<u64>(static_cast<u32>(x << 4)) | static_cast<u64>(static_cast<u32>(y << 4)) << 16;
    const u64 p1 = static_cast<u64>(static_cast<u32>((x + w) * 0x10)) |
                   static_cast<u64>(static_cast<u32>((y + h) * 0x10)) << 16;
    BuildFadeQuad(list, index, mode, static_cast<u32>(a) & 0xFF, p0, p1);
}

// ---- 2D camera -----------------------------------------------------------------------------------
// Camera block (0x1C0 bytes at scene work +0x10): +0x00..0x1C vectors, +0x20.. five unit matrices,
// +0x160 projection {width, height, distance, aspect, near}, +0x178/+0x17C screen centre (GS
// coordinates), +0x188.. viewport {x, y, w, h}, +0x190/+0x194 depth range, +0x198 dirty flags,
// +0x1A8/+0x1B8 register ids (0x40 SCISSOR_1, 0x41 SCISSOR_2? H).

// EE: 0x001026B8
void Init2dCamera(u32 cam) {
    GRef<f32>(cam + 0xC) = 1.0f;
    GRef<u32>(cam + 0x198) = 0;
    GRef<f32>(cam + 0x10) = 0.0f;
    GRef<f32>(cam + 0x8) = 0.0f;
    GRef<f32>(cam + 0x4) = 0.0f;
    GRef<f32>(cam + 0x0) = 0.0f;
    GRef<f32>(cam + 0x1C) = 0.0f;
    GRef<f32>(cam + 0x18) = 0.0f;
    GRef<f32>(cam + 0x14) = 0.0f;
    sceVu0UnitMatrix(cam + 0x20);
    sceVu0UnitMatrix(cam + 0x60);
    sceVu0UnitMatrix(cam + 0xA0);
    sceVu0UnitMatrix(cam + 0xE0);
    sceVu0UnitMatrix(cam + 0x120);
    SetCameraProjection(640.0f, 480.0f, 512.0f, 1.0f, GRef<f32>(0x0040DA08), cam);
    MainContext* ctx = Ctx();
    SetCameraViewport(1.0f, GRef<f32>(0x0040DA0C), cam, 0, 0, static_cast<s16>(ctx->draw_w), static_cast<s16>(ctx->draw_h));
    GRef<u64>(cam + 0x1B8) = 0x41;
    GRef<u64>(cam + 0x1A8) = 0x40;
}

// EE: 0x00102810
// Projection parameters.
void SetCameraProjection(f32 width, f32 height, f32 distance, f32 aspect, f32 near_z, u32 cam) {
    GRef<f32>(cam + 0x170) = near_z;
    GRef<f32>(cam + 0x160) = width;
    GRef<u32>(cam + 0x198) |= 1;
    GRef<f32>(cam + 0x164) = height;
    GRef<f32>(cam + 0x168) = distance;
    GRef<f32>(cam + 0x16C) = aspect;
}

// EE: 0x001028D8
// Viewport {x, y, w, h} in pixels, depth range, and the screen centre in GS coordinates (2048 is
// the centre of the GS primitive coordinate space).
void SetCameraViewport(f32 z_near, f32 z_far, u32 cam, s16 x, s16 y, s16 w, s16 h) {
    GRef<s16>(cam + 0x18C) = w;
    GRef<s16>(cam + 0x188) = x;
    GRef<s16>(cam + 0x18A) = y;
    GRef<s16>(cam + 0x18E) = h;
    MainContext* ctx = Ctx();
    const u16 dh = ctx->draw_h;
    const u16 dw = ctx->draw_w;
    GRef<f32>(cam + 0x190) = z_near;
    GRef<u32>(cam + 0x198) |= 1;
    GRef<f32>(cam + 0x194) = z_far;
    GRef<f32>(cam + 0x17C) = fpu::Add(fpu::Sub(2048.0f, static_cast<f32>(dh) * 0.5f), static_cast<f32>(y + (h >> 1)));
    GRef<f32>(cam + 0x178) = fpu::Add(fpu::Sub(2048.0f, static_cast<f32>(dw) * 0.5f), static_cast<f32>(x + (w >> 1)));
}

// ---- Containers (docs/formats/CDP.md: count + offsets) -------------------------------------------

// EE: 0x00108388
void InitContainerReader(u32 reader, u32 container) {
    const u32 count = GRef<u32>(container);
    GRef<u32>(reader + 8) = container + 4;
    GRef<u32>(reader) = count;
    GRef<u32>(reader + 4) = container;
}

// EE: 0x001083E0
u32 GetContainerItem(u32 reader, s32 index) {
    return GRef<u32>(GRef<u32>(reader + 8) + static_cast<u32>(index) * 4) + GRef<u32>(reader + 4);
}

// EE: 0x00103EB8
// Writes a DMA "call" tag to the packet at `target` (a static packet in .data or a loaded block)
// and returns the address after it. The caller's own data follows and is closed by CloseDmaPacket.
u32 WriteDmaCallTag(u32 p, u32 target) {
    Put(p, 1, target);
    Put(p, 0, 0x50000000);
    GRef<u64>(p + 8) = 0;
    return p + 16;
}

// EE: 0x00104FC8
// Per-byte blend of the 8 bytes of `a` towards `b` by `t` (MMI, translated from the disassembly):
// f = (s16)(int)(t * 4096); pmulth: p = (b - a) * f per halfword (32-bit product); psraw 12 and
// pinteh keep the low halfword; paddsh: saturating 16-bit add to a; clamped to 0..255; ppacb.
u64 LerpColor(f32 t, u64 a, u64 b) {
    return LerpColorFactor(static_cast<s16>(fpu::FloatToInt(t * 4096.0f)), a, b);
}

// The blend of LerpColor with the 4.12 factor given directly (the same MMI sequence is inlined in
// several effect drawers with constant or precomputed factors).
u64 LerpColorFactor(s16 f, u64 a, u64 b) {
    u64 out = 0;
    for (u32 i = 0; i < 8; ++i) {
        const s16 ah = static_cast<s16>((a >> (8 * i)) & 0xFF);
        const s16 bh = static_cast<s16>((b >> (8 * i)) & 0xFF);
        const s16 d = static_cast<s16>(bh - ah);
        const s32 product = static_cast<s32>(d) * static_cast<s32>(f);
        const s16 step = static_cast<s16>(product >> 12);
        s32 sum = static_cast<s32>(ah) + step;
        if (sum > 0x7FFF) sum = 0x7FFF;
        if (sum < -0x8000) sum = -0x8000;
        if (sum < 0) sum = 0;
        if (sum > 0xFF) sum = 0xFF;
        out |= static_cast<u64>(sum) << (8 * i);
    }
    return out;
}

// EE: 0x001050D8
// 16-byte variant of LerpColor: the low and high 8 bytes of a blended towards b (pextlb /
// pextub halves, same factor and clamping), stored as one qword.
void func_001050D8(f32 t, u32 out, u32 a, u32 b) {
    const u64 lo = LerpColor(t, GRef<u64>(a), GRef<u64>(b));
    const u64 hi = LerpColor(t, GRef<u64>(a + 8), GRef<u64>(b + 8));
    GRef<u64>(out) = lo;
    GRef<u64>(out + 8) = hi;
}

// EE: 0x001128D8
// Textured variant of BuildSpriteStrips: splits the sprite from (xy0, uv0) to (xy1, uv1) into
// vertical strips at 64-pixel boundaries (0x400 units), interpolating U per strip. Each strip is
// {UV, XYZ3, UV, XYZ2}; the REGLIST GIF tag (regs 0x53D3) is written at `p` when at least one strip
// was produced. Upper bits of each input word (y, v, ...) are kept as they are.
u32 BuildSpriteStripsUv(u32 p, u64 xy0, u64 xy1, u64 uv0, u64 uv1) {
    const s32 x0 = static_cast<s32>(xy0 & 0xFFFF);
    const s32 u0 = static_cast<s32>(uv0 & 0xFFFF);
    s32 dx = static_cast<s32>(xy1 & 0xFFFF) - x0;
    const s32 du = static_cast<s32>(uv1 & 0xFFFF) - u0;
    const u64 xy0_hi = xy0 & ~0xFFFFull;
    const u64 xy1_hi = xy1 & ~0xFFFFull;
    const u64 uv0_hi = uv0 & ~0xFFFFull;
    const u64 uv1_hi = uv1 & ~0xFFFFull;
    // div.s runs before the loop; its result is only used when dx > 0.
    const f32 ratio = dx > 0 ? static_cast<f32>(du) / static_cast<f32>(dx) : 0.0f;
    s32 step = 0x400 - (x0 & 0x3FF);
    s32 done = 0;
    s32 count = 0;
    s32 u = u0;
    s32 x = x0;
    u32 q = p + 0x10;
    while (dx > 0) {
        if (dx < step) step = dx;
        GRef<u64>(q) = static_cast<u64>(static_cast<s64>(u)) | uv0_hi;
        GRef<u64>(q + 8) = static_cast<u64>(static_cast<s64>(x)) | xy0_hi;
        done += step;
        x = x0 + done;
        GRef<u64>(q + 0x18) = static_cast<u64>(static_cast<s64>(x)) | xy1_hi;
        dx -= step;
        u = u0 + fpu::FloatToInt(ratio * static_cast<f32>(done));
        GRef<u64>(q + 0x10) = static_cast<u64>(static_cast<s64>(u)) | uv1_hi;
        step = 0x400;
        ++count;
        q += 0x20;
    }
    if (count <= 0) return p;
    GRef<u64>(p + 8) = 0x53D3;
    GRef<u64>(p) = static_cast<u64>(static_cast<s64>(count)) | 0x8000 | 0x4400000000000000ull;
    return q;
}


// EE: 0x00112A10
// As BuildSpriteStripsUv, clipped to the rectangle `clip0`..`clip1` (XYZ2-style x/y words): the
// left/right edges by x with the texture u moved along, and the top/bottom by y with v moved
// (replacing the whole UV word). Strips of at most 1024 units after a REGLIST tag {UV, XYZ3, UV,
// XYZ2}. Returns the end (or `p` when nothing was written).
u32 BuildClippedSpriteStripsUv(u32 p, u64 xy0, u64 xy1, u64 uv0, u64 uv1, u64 clip0, u64 clip1) {
    auto sx = [](u64 v) { return static_cast<s32>(static_cast<u32>(v) & 0xFFFF); };
    auto sext = [](s32 v) { return static_cast<u64>(static_cast<s64>(v)); };
    const s32 x0 = sx(xy0), x1 = sx(xy1), u0 = sx(uv0), u1 = sx(uv1);
    s32 width = x1 - x0;
    const f32 du = fpu::Div(fpu::IntToFloat(u1 - u0), fpu::IntToFloat(x1 - x0));
    u64 y0w = xy0 & ~0xFFFFull, y1w = xy1 & ~0xFFFFull;
    u64 v0w = uv0 & ~0xFFFFull, v1w = uv1 & ~0xFFFFull;
    s32 skip = 0, u_skip = 0;
    const s32 cx0 = sx(clip0), cx1 = sx(clip1);
    if (x0 < cx0) {
        skip = cx0 - x0;
        width -= skip;
        u_skip = fpu::FloatToInt(fpu::Mul(du, fpu::IntToFloat(skip)));
    }
    if (cx1 < x1) width -= x1 - cx1;
    const s32 y0 = sx(y0w >> 16), cy0 = sx(clip0 >> 16), cy1 = sx(clip1 >> 16), y1 = sx(y1w >> 16);
    const bool clip_top = y0 < cy0, clip_bottom = cy1 < y1;
    if (clip_top || clip_bottom) {
        const s32 v0 = static_cast<s32>(v0w >> 16), v1 = static_cast<s32>(v1w >> 16);
        const f32 dv = fpu::Div(fpu::IntToFloat(v1 - v0), fpu::IntToFloat(y1 - y0));
        if (clip_top) {
            y0w = (xy0 & 0xFFFFFFFF00000000ull) | sext(cy0) << 16;
            v0w = sext(v0 + fpu::FloatToInt(fpu::Mul(dv, fpu::IntToFloat(cy0 - y0)))) << 16;
        }
        if (clip_bottom) {
            y1w = (xy1 & 0xFFFFFFFF00000000ull) | sext(cy1) << 16;
            v1w = sext(v0 + fpu::FloatToInt(fpu::Mul(dv, fpu::IntToFloat(cy1 - y0)))) << 16;
        }
    }
    s32 x = x0 + skip;
    s32 room = 0x400 - (x & 0x3FF);
    u32 q = p + 0x10;
    s64 strips = 0;
    while (width > 0) {
        const s32 w = width < room ? width : room;
        const u64 uv_a = sext(u0 + u_skip) | v0w;
        GRef<u64>(q + 8) = sext(x) | y0w;
        skip += w;
        x = x0 + skip;
        GRef<u64>(q) = uv_a;
        GRef<u64>(q + 0x18) = sext(x) | y1w;
        width -= w;
        room = 0x400;
        ++strips;
        u_skip = fpu::FloatToInt(fpu::Mul(du, fpu::IntToFloat(skip)));
        GRef<u64>(q + 0x10) = sext(u0 + u_skip) | v1w;
        q += 0x20;
    }
    if (strips > 0) {
        GRef<u64>(p + 8) = 0x53D3;  // REGLIST registers: UV, XYZ3, UV, XYZ2
        GRef<u64>(p) = static_cast<u64>(strips) | 0x8000 | 0x4400000000000000ull;
        p = q;
    }
    return p;
}

namespace {

void Pw2(u32 p, u32 v) { GRef<u32>(p) = v; }

// A sign-extended value as the 32-bit register pattern (shifts then wrap as on the EE).
u32 Sx32(s32 v) { return static_cast<u32>(v); }

// Quadwords of `bytes`, rounded up (as the original's (n + 15) >> 4 with the negative fix-up).
u32 BytesToQwc(s32 bytes) {
    const s32 t = bytes + 0xF;
    return static_cast<u32>((-1 < t ? t : bytes + 0x1E) >> 4);
}

}  // namespace

// EE: 0x00104418
// Dead (no reference): closes packet list `list` with a DMA next tag to the head of chain `chain`
// and takes over the chain's tail.
void func_00104418(u32 list, u32 chain) {
    const u32 tail = GRef<u32>(list + 4);
    Pw2(tail, 0x20000000);
    Pw2(tail + 4, GRef<u32>(chain));
    GRef<u32>(list + 4) = GRef<u32>(chain + 4);
}

// EE: 0x00106468
// Dead (no reference): texture packet of GIM record `tex` (with a CLUT, +8): TEX0 from the CLUT
// record, TEX1, and a 2-loop sprite with fixed coordinates; nothing without a CLUT. Returns the end.
u32 func_00106468(u32 tex, u32 p) {
    const u32 clut = GRef<u32>(tex + 8);
    if (clut == 0) return p;
    Pw2(p + 4, 0);
    Pw2(p, 0x10000003);
    Pw2(p + 8, 0);
    Pw2(p + 0xC, 0x50000003);
    Pw2(p + 0x10, 0x8002);
    Pw2(p + 0x14, 0x10000000);
    Pw2(p + 0x18, 0xE);
    Pw2(p + 0x24, 5);
    Pw2(p + 0x28, 6);
    Pw2(p + 0x2C, 0);
    Pw2(p + 0x20, (static_cast<u32>(GRef<u16>(clut)) | 0x4000u | static_cast<u32>(GRef<u8>(clut + 3)) << 20) | 0x10000000u);
    Pw2(p + 0x30, 0);
    Pw2(p + 0x34, 0);
    Pw2(p + 0x38, 0x14);
    Pw2(p + 0x40, 0x10000007);
    Pw2(p + 0x44, 0);
    Pw2(p + 0x48, 0);
    Pw2(p + 0x4C, 0x50000007);
    Pw2(p + 0x50, 0x8002);
    Pw2(p + 0x54, 0x300F4000);
    Pw2(p + 0x58, 0x412);
    Pw2(p + 0x60, 0);
    Pw2(p + 0x64, 0);
    GRef<f32>(p + 0x68) = 1.0f;
    Pw2(p + 0x70, 0x80);
    Pw2(p + 0x74, 0x80);
    Pw2(p + 0x78, 0x80);
    Pw2(p + 0x7C, 0x80);
    Pw2(p + 0x84, 0x7800);
    Pw2(p + 0x80, 0x7800);
    Pw2(p + 0x88, 0x0FFFFFFF);
    Pw2(p + 0x8C, 0);
    GRef<f32>(p + 0x98) = 1.0f;
    GRef<f32>(p + 0x90) = 1.0f;
    GRef<f32>(p + 0x94) = 1.0f;
    Pw2(p + 0xAC, 0x80);
    Pw2(p + 0xA0, 0x80);
    Pw2(p + 0xA4, 0x80);
    Pw2(p + 0xA8, 0x80);
    Pw2(p + 0xB4, 0x8800);
    Pw2(p + 0xB8, 0x0FFFFFFF);
    Pw2(p + 0xB0, 0x8800);
    Pw2(p + 0xBC, 0);
    return p + 0xC0;
}

// EE: 0x00106810
// Dead (no reference): development dump of a w x h RGBA32 image to "host0:<name>" as a GIM file:
// a 0x10-byte header "GIM", then {(w + 63) / 64 << 16, 0, 0, w | h << 16} and the pixels. Returns 1
// on success, 0 when a call fails (a failed write leaves the file open, as in the original). The
// host file calls are stubs in the port.
s32 func_00106810(u32 unused, u32 name, u32 data, s32 w, s32 h) {
    (void)unused;
    guest::GuestFrame frame(0x120);
    const u32 hdr = frame.Address(), path = hdr + 0x20;
    const u64 args[1] = {name};
    ee::sprintf(path, 0x00403B10, args, 1);  // "host0:%s"
    ee::sprintf(hdr, 0x004104B8, nullptr, 0);  // "GIM"
    const s32 blocks = (w + 0x3F) / 0x40;
    GRef<u32>(hdr + 0x10) = static_cast<u32>(blocks) << 16;
    GRef<u32>(hdr + 0x1C) = static_cast<u32>(w) | static_cast<u32>(h) << 16;
    GRef<u32>(hdr + 4) = 0;
    GRef<u32>(hdr + 8) = 0;
    GRef<u32>(hdr + 0xC) = 0;
    GRef<u32>(hdr + 0x14) = 0;
    GRef<u32>(hdr + 0x18) = 0;
    const s32 fd = sceOpen(path, 0x602);
    if (fd == -1) return 0;
    if (sceWrite(fd, hdr, 0x10) == -1) return 0;
    if (sceWrite(fd, hdr + 0x10, 0x10) == -1) return 0;
    if (sceWrite(fd, data, w * h * 4) == -1) return 0;
    sceClose(fd);
    return 1;
}

// EE: 0x00107238
// Dead (no reference): sets up 2D sprite record `rec`: colour (the bytes of `rgba` sign-extended and
// ORed, as the original), Q 1.0, scale (sx, sy), centre (x, y) + 0x800, frame `frame` and the half
// sizes (texture size x scale, halved toward zero).
void func_00107238(u32 rec, u32 rgba, s16 x, s16 y, u32 frame, f32 sx, f32 sy) {
    const s32 c = static_cast<s32>(rgba);
    const s32 b0 = static_cast<s8>(c & 0xFF);
    const s32 b1 = static_cast<s8>((c >> 8) & 0xFF);
    const s32 b2 = static_cast<s8>((c >> 16) & 0xFF);
    const s32 b3 = static_cast<s8>((c >> 24) & 0xFF);
    GRef<s32>(rec + 0x18) = (b0 | static_cast<s32>(static_cast<u32>(b3) << 24)) |
                            (static_cast<s32>(static_cast<u32>(b2) << 16) | static_cast<s32>(static_cast<u32>(b1) << 8));
    GRef<f32>(rec + 0x1C) = 1.0f;
    GRef<s32>(rec + 0x28) = x + 0x800;
    GRef<s32>(rec + 0x2C) = y + 0x800;
    GRef<f32>(rec + 0x20) = sx;
    GRef<f32>(rec + 0x24) = sy;
    const u32 tex = GRef<u32>(rec + 4);
    const s32 w = static_cast<s16>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(tex + 0xC)) * sx));
    GRef<s32>(rec + 0x30) = w / 2;
    GRef<u32>(rec + 0x38) = frame;
    const s32 h = static_cast<s16>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(tex + 0xE)) * sy));
    GRef<s32>(rec + 0x34) = h / 2;
}

// EE: 0x00107668
// Dead (no reference): draws 2D sprite record `rec` into bucket `index` of `list`: uploads frame
// (+0x38 mod the frame count) of its image (IMAGE transfer by a DMA ref to the frame table +0xC) and
// its CLUT (+0x10), then TEXFLUSH, TEST_1, ZBUF_1 (ZMSK), ALPHA_1, CLAMP_1 and one sprite (PRIM
// 0x156) at the record's centre and half sizes, then TEST_1 and ZBUF_1 again.
void func_00107668(u32 rec, u32 list, s32 index) {
    const u32 p = BeginPacket();
    const u32 frames = GRef<u32>(rec);
    const s32 frame = static_cast<s32>(GRef<u32>(rec + 0x38)) % static_cast<s32>(GRef<u32>(frames + 4));
    const auto upload = [&](u32 at, u32 img, u32 data_addr) {
        Pw2(at, 0x10000006);
        Pw2(at + 4, 0);
        Pw2(at + 8, 0);
        Pw2(at + 0xC, 0x50000006);
        Pw2(at + 0x10, 4);
        Pw2(at + 0x14, 0x10000000);
        Pw2(at + 0x18, 0xE);
        Pw2(at + 0x20, 0);
        Pw2(at + 0x28, 0x50);
        Pw2(at + 0x24, Sx32(GRef<s16>(img)) | Sx32(GRef<s8>(img + 2)) << 16 | Sx32(GRef<s8>(img + 3)) << 24);
        Pw2(at + 0x30, 0);
        Pw2(at + 0x38, 0x51);
        Pw2(at + 0x34, 0);
        Pw2(at + 0x40, static_cast<u32>(static_cast<s32>(GRef<s16>(img + 0xC))));
        Pw2(at + 0x48, 0x52);
        Pw2(at + 0x44, static_cast<u32>(static_cast<s32>(GRef<s16>(img + 0xE))));
        Pw2(at + 0x50, 0);
        Pw2(at + 0x58, 0x53);
        const u32 qwc = BytesToQwc(GRef<s32>(img + 4));
        Pw2(at + 0x64, 0x08000000);
        Pw2(at + 0x68, 0);
        Pw2(at + 0x60, qwc);
        Pw2(at + 0x74, data_addr);
        Pw2(at + 0x70, qwc | 0x30000000);
        Pw2(at + 0x78, 0);
        Pw2(at + 0x7C, qwc | 0x50000000);
    };
    const u32 tex = GRef<u32>(rec + 4);
    upload(p, tex, GRef<u32>(static_cast<u32>(frame) * 4 + GRef<u32>(rec + 0xC)));
    const u32 clut = GRef<u32>(rec + 8);
    upload(p + 0x80, clut, GRef<u32>(rec + 0x10));
    const u32 ctx = g_pMainContext().addr;
    Pw2(p + 0x100, 0x10000006);
    Pw2(p + 0x104, 0);
    Pw2(p + 0x108, 0);
    Pw2(p + 0x10C, 0x50000006);
    Pw2(p + 0x118, 0xE);
    Pw2(p + 0x110, 5);
    Pw2(p + 0x114, 0x10000000);
    Pw2(p + 0x128, 0x3F);
    Pw2(p + 0x130, 0x30000);
    Pw2(p + 0x138, 0x47);
    GRef<u128>(p + 0x140) = GRef<u128>(ctx + 0xE0);
    Pw2(p + 0x144, 1);
    Pw2(p + 0x150, 0x44);
    Pw2(p + 0x154, 0);
    Pw2(p + 0x158, 0x42);
    Pw2(p + 0x15C, 0);
    const u32 t = GRef<u32>(rec + 4);
    const s32 tw = GRef<s16>(t + 0xC);
    const s32 th = GRef<s16>(t + 0xE);
    Pw2(p + 0x160, Sx32(tw - 1) << 14 | 0xA);
    Pw2(p + 0x168, 8);
    Pw2(p + 0x164, Sx32(th - 1) << 2);
    const u32 uv1 = Sx32(tw) << 4 | Sx32(th) << 20;
    Pw2(p + 0x170, 0x10000005);
    Pw2(p + 0x174, 0);
    Pw2(p + 0x178, 0);
    Pw2(p + 0x17C, 0x50000005);
    Pw2(p + 0x180, 1);
    Pw2(p + 0x184, 0x84000000);
    Pw2(p + 0x188, 0x4343F610);
    GRef<u64>(p + 0x190) = 0x156;
    GRef<u64>(p + 0x198) = GRef<u64>(rec + 0x18);
    Pw2(p + 0x1A0, (Sx32(GRef<s16>(t)) | 0x80000000u) | (Sx32(GRef<s8>(t + 3)) << 20 | Sx32(GRef<s8>(t + 2)) << 14) |
                       0x28000000u);
    const u32 cl = GRef<u32>(rec + 8);
    Pw2(p + 0x1A4, ((Sx32(GRef<s16>(cl)) << 5 | 0x20000000u) | (Sx32(GRef<s8>(cl + 2)) << 19 | 6u)) |
                       Sx32(GRef<s8>(cl + 3)) << 23);
    Pw2(p + 0x1B0, 0);
    Pw2(p + 0x1B8, static_cast<u32>(GRef<s32>(rec + 0x28) - GRef<s32>(rec + 0x30)) << 4 |
                       static_cast<u32>(GRef<s32>(rec + 0x2C) - GRef<s32>(rec + 0x34)) << 20);
    Pw2(p + 0x1BC, 0);
    Pw2(p + 0x1C0, uv1);
    Pw2(p + 0x1CC, 0);
    Pw2(p + 0x1C8, static_cast<u32>(GRef<s32>(rec + 0x28) + GRef<s32>(rec + 0x30)) << 4 |
                       static_cast<u32>(GRef<s32>(rec + 0x2C) + GRef<s32>(rec + 0x34)) << 20);
    Pw2(p + 0x1D0, 0x10000003);
    Pw2(p + 0x1D4, 0);
    Pw2(p + 0x1D8, 0);
    Pw2(p + 0x1DC, 0x50000003);
    Pw2(p + 0x1E0, 0x8002);
    Pw2(p + 0x1E8, 0xE);
    Pw2(p + 0x1E4, 0x10000000);
    Pw2(p + 0x1F0, 0x50000);
    Pw2(p + 0x1F8, 0x47);
    GRef<u128>(p + 0x200) = GRef<u128>(ctx + 0xE0);
    EndPacket(list, index, p + 0x210);
}

// EE: 0x001083A0
// Dead (no reference): turns the offset table of container `c` (count, then offsets) into absolute
// addresses in place; returns the table.
u32 func_001083A0(u32 unused, u32 c) {
    (void)unused;
    const u32 table = c + 4;
    const u32 n = GRef<u32>(c);
    for (u32 i = 0; i < n; ++i) GRef<u32>(table + 4 * i) += c;
    return table;
}

// EE: 0x00113010
// Dead (no reference): splits an IMAGE source of `qwc` quadwords at `addr` into pieces that do not
// cross a 1024-quadword page; one REGLIST pair per piece after the GIF tag at p (NLOOP = pieces, EOP,
// NREG 2, registers 0x5D): {addr | hi << 16, end | lo << 16}. Returns the end (p when qwc is 0).
u32 func_00113010(u32 p, u32 addr, u32 qwc, u32 hi, u32 lo) {
    u32 room = 0x400 - (addr & 0x3FF);
    u32 count = 0;
    u32 at = p + 0x10;
    while (qwc != 0) {
        const u32 n = static_cast<s32>(qwc) < static_cast<s32>(room) ? qwc : room;
        GRef<u64>(at) = static_cast<u64>(addr) | (static_cast<u64>(hi) << 16);
        addr += n;
        GRef<u64>(at + 8) = static_cast<u64>(addr) | (static_cast<u64>(lo) << 16);
        qwc -= n;
        room = 0x400;
        ++count;
        at += 0x10;
    }
    if (static_cast<s32>(count) <= 0) return p;
    GRef<u64>(p + 8) = 0x5D;
    GRef<u64>(p) = static_cast<u64>(count | 0x8000) | 0x2400000000000000ull;
    return at;
}

}  // namespace ac04::game
