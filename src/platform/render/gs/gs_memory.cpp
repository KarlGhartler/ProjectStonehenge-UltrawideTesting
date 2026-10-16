#include "platform/render/gs/gs_memory.h"

#include <algorithm>
#include <cstring>

namespace ac04::render::gs {
namespace {

// Block index inside a page, by block row and column (GS User's Manual).
constexpr std::uint8_t kBlock32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21},
                                         {2, 3, 6, 7, 18, 19, 22, 23},
                                         {8, 9, 12, 13, 24, 25, 28, 29},
                                         {10, 11, 14, 15, 26, 27, 30, 31}};
constexpr std::uint8_t kBlock16[8][4] = {{0, 2, 8, 10},    {1, 3, 9, 11},    {4, 6, 12, 14},   {5, 7, 13, 15},
                                         {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
constexpr std::uint8_t kBlock16S[8][4] = {{0, 2, 16, 18},  {1, 3, 17, 19},   {8, 10, 24, 26},  {9, 11, 25, 27},
                                          {4, 6, 20, 22},  {5, 7, 21, 23},   {12, 14, 28, 30}, {13, 15, 29, 31}};
constexpr const std::uint8_t (&kBlock8)[4][8] = kBlock32;
constexpr const std::uint8_t (&kBlock4)[8][4] = kBlock16;

constexpr std::uint32_t kMask = GsMemory::kSize - 1;

}  // namespace

int BitsPerPixel(std::uint32_t psm) {
    switch (psm) {
        case kPsmCt32: case kPsmZ32: return 32;
        case kPsmCt24: case kPsmZ24: return 24;
        case kPsmCt16: case kPsmCt16S: case kPsmZ16: case kPsmZ16S: return 16;
        case kPsmT8: case kPsmT8H: return 8;
        case kPsmT4: case kPsmT4HL: case kPsmT4HH: return 4;
        default: return 0;
    }
}

std::uint32_t GsMemory::PixelAddress(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x,
                                     std::uint32_t y, std::uint32_t* nibble) {
    if (nibble) *nibble = 0;
    switch (psm) {
        case kPsmCt32: case kPsmCt24: case kPsmZ32: case kPsmZ24:
        case kPsmT8H: case kPsmT4HL: case kPsmT4HH: {
            const std::uint32_t page = (x >> 6) + (y >> 5) * bw;
            const std::uint32_t block = bp + page * 32 + kBlock32[(y >> 3) & 3][(x >> 3) & 7];
            return (block * 256 + ((y & 7) * 8 + (x & 7)) * 4) & kMask;
        }
        case kPsmCt16: case kPsmCt16S: case kPsmZ16: case kPsmZ16S: {
            const std::uint32_t page = (x >> 6) + (y >> 6) * bw;
            const auto& table = (psm == kPsmCt16S || psm == kPsmZ16S) ? kBlock16S : kBlock16;
            const std::uint32_t block = bp + page * 32 + table[(y >> 3) & 7][(x >> 4) & 3];
            return (block * 256 + ((y & 7) * 16 + (x & 15)) * 2) & kMask;
        }
        case kPsmT8: {
            const std::uint32_t page = (x >> 7) + (y >> 6) * (bw >> 1);
            const std::uint32_t block = bp + page * 32 + kBlock8[(y >> 4) & 3][(x >> 4) & 7];
            return (block * 256 + (y & 15) * 16 + (x & 15)) & kMask;
        }
        case kPsmT4: {
            const std::uint32_t page = (x >> 7) + (y >> 7) * (bw >> 1);
            const std::uint32_t block = bp + page * 32 + kBlock4[(y >> 4) & 7][(x >> 5) & 3];
            const std::uint32_t index = (y & 15) * 32 + (x & 31);
            if (nibble) *nibble = index & 1;
            return (block * 256 + (index >> 1)) & kMask;
        }
        default:
            return 0;
    }
}

std::uint32_t GsMemory::ReadPixel(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x,
                                  std::uint32_t y) const {
    std::uint32_t nib = 0;
    const std::uint32_t a = PixelAddress(psm, bp, bw, x, y, &nib);
    const std::uint8_t* p = mem_.data() + a;
    switch (psm) {
        case kPsmCt32: case kPsmZ32:
            return static_cast<std::uint32_t>(p[0]) | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24;
        case kPsmCt24: case kPsmZ24:
            return static_cast<std::uint32_t>(p[0]) | p[1] << 8 | p[2] << 16;
        case kPsmCt16: case kPsmCt16S: case kPsmZ16: case kPsmZ16S:
            return static_cast<std::uint32_t>(p[0]) | p[1] << 8;
        case kPsmT8: return p[0];
        case kPsmT4: return nib ? p[0] >> 4 : p[0] & 0xF;
        case kPsmT8H: return p[3];
        case kPsmT4HL: return p[3] & 0xF;
        case kPsmT4HH: return p[3] >> 4;
        default: return 0;
    }
}

void GsMemory::WritePixel(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y,
                          std::uint32_t v) {
    std::uint32_t nib = 0;
    const std::uint32_t a = PixelAddress(psm, bp, bw, x, y, &nib);
    std::uint8_t* p = mem_.data() + a;
    // Only a changed pixel bumps the page generation, so an identical re-upload (e.g. an image the
    // game sends every frame) keeps the decoded textures of that page valid.
    std::uint8_t old[4] = {p[0], 0, 0, 0};
    switch (psm) {
        case kPsmCt32: case kPsmZ32:
            old[1] = p[1], old[2] = p[2], old[3] = p[3];
            p[0] = static_cast<std::uint8_t>(v);
            p[1] = static_cast<std::uint8_t>(v >> 8);
            p[2] = static_cast<std::uint8_t>(v >> 16);
            p[3] = static_cast<std::uint8_t>(v >> 24);
            if (std::memcmp(old, p, 4) != 0) Touch(a);
            return;
        case kPsmCt24: case kPsmZ24:
            old[1] = p[1], old[2] = p[2];
            p[0] = static_cast<std::uint8_t>(v);
            p[1] = static_cast<std::uint8_t>(v >> 8);
            p[2] = static_cast<std::uint8_t>(v >> 16);
            if (std::memcmp(old, p, 3) != 0) Touch(a);
            return;
        case kPsmCt16: case kPsmCt16S: case kPsmZ16: case kPsmZ16S:
            old[1] = p[1];
            p[0] = static_cast<std::uint8_t>(v);
            p[1] = static_cast<std::uint8_t>(v >> 8);
            if (std::memcmp(old, p, 2) != 0) Touch(a);
            return;
        case kPsmT8: p[0] = static_cast<std::uint8_t>(v); break;
        case kPsmT4:
            p[0] = nib ? static_cast<std::uint8_t>((p[0] & 0x0F) | (v & 0xF) << 4)
                       : static_cast<std::uint8_t>((p[0] & 0xF0) | (v & 0xF));
            break;
        case kPsmT8H: old[0] = p[3]; p[3] = static_cast<std::uint8_t>(v); if (old[0] != p[3]) Touch(a); return;
        case kPsmT4HL: old[0] = p[3]; p[3] = static_cast<std::uint8_t>((p[3] & 0xF0) | (v & 0xF)); if (old[0] != p[3]) Touch(a); return;
        case kPsmT4HH: old[0] = p[3]; p[3] = static_cast<std::uint8_t>((p[3] & 0x0F) | (v & 0xF) << 4); if (old[0] != p[3]) Touch(a); return;
        default: return;
    }
    if (old[0] != p[0]) Touch(a);
}

std::uint32_t GsMemory::WriteImage(std::uint32_t dbp, std::uint32_t dbw, std::uint32_t dpsm, std::uint32_t dsax,
                                   std::uint32_t dsay, std::uint32_t rrw, std::uint32_t rrh, std::uint32_t pos,
                                   const std::uint8_t* data, std::uint32_t bytes) {
    const int bpp = BitsPerPixel(dpsm);
    if (bpp == 0 || rrw == 0 || rrh == 0) return pos;
    const std::uint32_t total = rrw * rrh;
    const std::uint64_t pixels = static_cast<std::uint64_t>(bytes) * 8 / static_cast<std::uint32_t>(bpp);
    std::uint32_t col = pos % rrw, row = pos / rrw;
    // Byte-sized formats: pixels in one block row (8 for 32-bit, 16 for 16- and 8-bit) are
    // contiguous in local memory, so such runs are copied at once.
    std::uint32_t run_len = 0, bytes_pp = 0;
    switch (dpsm) {
        case kPsmCt32: case kPsmZ32: run_len = 8; bytes_pp = 4; break;
        case kPsmCt16: case kPsmCt16S: case kPsmZ16: case kPsmZ16S: run_len = 16; bytes_pp = 2; break;
        case kPsmT8: run_len = 16; bytes_pp = 1; break;
        case kPsmT4: run_len = 32; break;  // nibbles: pixel pairs at even x and even data index
        default: break;
    }
    if (run_len != 0) {
        std::uint64_t i = 0;
        while (i < pixels && pos < total) {
            const std::uint32_t x = (dsax + col) & 2047;
            const std::uint32_t y = (dsay + row) & 2047;
            std::uint64_t n = run_len - (x & (run_len - 1));
            n = std::min<std::uint64_t>(n, rrw - col);
            n = std::min<std::uint64_t>(n, 2048 - x);
            n = std::min<std::uint64_t>(n, pixels - i);
            n = std::min<std::uint64_t>(n, total - pos);
            if (bytes_pp == 0) {
                // 4-bit: whole bytes when the run starts on a pixel pair; otherwise one pixel.
                if ((x & 1) == 0 && (i & 1) == 0 && n >= 2) {
                    n &= ~1ull;
                } else {
                    WritePixel(dpsm, dbp, dbw, x, y, (data[i >> 1] >> ((i & 1) * 4)) & 0xF);
                    n = 1;
                }
            }
            if (bytes_pp != 0 || n >= 2) {
                const std::uint32_t a = PixelAddress(dpsm, dbp, dbw, x, y);
                const std::size_t len = bytes_pp != 0 ? static_cast<std::size_t>(n) * bytes_pp : static_cast<std::size_t>(n / 2);
                const std::uint8_t* src = bytes_pp != 0 ? data + i * bytes_pp : data + (i >> 1);
                if (std::memcmp(mem_.data() + a, src, len) != 0) {
                    std::memcpy(mem_.data() + a, src, len);
                    Touch(a);
                }
            }
            i += n;
            pos += static_cast<std::uint32_t>(n);
            col += static_cast<std::uint32_t>(n);
            if (col == rrw) {
                col = 0;
                ++row;
            }
        }
        return pos;
    }
    for (std::uint64_t i = 0; i < pixels && pos < total; ++i, ++pos) {
        const std::uint32_t x = (dsax + col) & 2047;
        const std::uint32_t y = (dsay + row) & 2047;
        if (++col == rrw) {
            col = 0;
            ++row;
        }
        std::uint32_t v = 0;
        switch (bpp) {
            case 32: {
                const std::uint8_t* s = data + i * 4;
                v = static_cast<std::uint32_t>(s[0]) | s[1] << 8 | s[2] << 16 | static_cast<std::uint32_t>(s[3]) << 24;
                break;
            }
            case 24: {
                const std::uint8_t* s = data + i * 3;
                v = static_cast<std::uint32_t>(s[0]) | s[1] << 8 | s[2] << 16;
                break;
            }
            case 16: {
                const std::uint8_t* s = data + i * 2;
                v = static_cast<std::uint32_t>(s[0]) | s[1] << 8;
                break;
            }
            case 8: v = data[i]; break;
            case 4: v = (data[i >> 1] >> ((i & 1) * 4)) & 0xF; break;
            default: break;
        }
        WritePixel(dpsm, dbp, dbw, x, y, v);
    }
    return pos;
}

std::uint64_t GsMemory::RegionGeneration(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t w,
                                         std::uint32_t h) const {
    if (w == 0 || h == 0) return 0;
    const std::uint32_t first = PixelAddress(psm, bp, bw, 0, 0) >> 13;
    std::uint32_t last = PixelAddress(psm, bp, bw, w - 1, h - 1) >> 13;
    if (last < first) last += 512;  // wrapped around the end of memory
    std::uint64_t gen = 0;
    for (std::uint32_t p = first; p <= last && p < first + 512; ++p) gen += page_gen_[p & 511];
    return gen;
}

std::uint64_t GsMemory::RegionHash(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t w,
                                   std::uint32_t h) const {
    if (w == 0 || h == 0) return 0;
    const std::uint32_t first = PixelAddress(psm, bp, bw, 0, 0) >> 13;
    std::uint32_t last = PixelAddress(psm, bp, bw, w - 1, h - 1) >> 13;
    if (last < first) last += 512;  // wrapped around the end of memory
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (std::uint32_t p = first; p <= last && p < first + 512; ++p) {
        const std::uint32_t page = p & 511;
        if (page_hash_gen_[page] != page_gen_[page]) {
            const std::uint8_t* src = mem_.data() + static_cast<std::size_t>(page) * 8192;
            std::uint64_t ph = 0x9E3779B97F4A7C15ull ^ page;
            for (std::size_t i = 0; i < 8192; i += 8) {
                std::uint64_t word;
                std::memcpy(&word, src + i, 8);
                ph = (ph ^ word) * 0x100000001B3ull;
                ph ^= ph >> 29;
            }
            page_hash_[page] = ph;
            page_hash_gen_[page] = page_gen_[page];
        }
        hash = (hash ^ page_hash_[page]) * 0x100000001B3ull;
    }
    return hash;
}

}  // namespace ac04::render::gs
