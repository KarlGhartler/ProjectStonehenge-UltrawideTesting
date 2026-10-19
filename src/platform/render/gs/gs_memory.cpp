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

// Element order inside a 256-byte block (GS User's Manual "column" layout; checked block for block
// against PCSX2 VRAM images with tools/research/gsvram.py): [y][x] -> word (32-bit), halfword
// (16-bit), byte (8-bit) or nibble (4-bit) number. Rows 4-7 (32/16-bit) and 8-15 (8/4-bit) repeat
// rows 0-3 / 0-7 one half-block further.
constexpr std::uint8_t kCol32[4][8] = {{0, 1, 4, 5, 8, 9, 12, 13},
                                       {2, 3, 6, 7, 10, 11, 14, 15},
                                       {16, 17, 20, 21, 24, 25, 28, 29},
                                       {18, 19, 22, 23, 26, 27, 30, 31}};
constexpr std::uint8_t kCol16[4][16] = {{0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
                                        {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31},
                                        {32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59},
                                        {36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63}};
constexpr std::uint8_t kCol8[8][16] = {
    {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
    {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
    {33, 37, 1, 5, 49, 53, 17, 21, 35, 39, 3, 7, 51, 55, 19, 23},
    {41, 45, 9, 13, 57, 61, 25, 29, 43, 47, 11, 15, 59, 63, 27, 31},
    {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
    {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
    {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
    {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127}};
constexpr std::uint8_t kCol4[8][32] = {
    {0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63},
    {192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174},
    {208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190},
    {129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239},
    {145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255}};

// Byte offset (nibble number for 4-bit) of pixel (x, y) inside its block.
inline std::uint32_t Off32(std::uint32_t x, std::uint32_t y) { return (kCol32[y & 3][x & 7] + ((y & 4) << 3)) * 4u; }
inline std::uint32_t Off16(std::uint32_t x, std::uint32_t y) { return (kCol16[y & 3][x & 15] + ((y & 4) << 4)) * 2u; }
inline std::uint32_t Off8(std::uint32_t x, std::uint32_t y) { return kCol8[y & 7][x & 15] + ((y & 8) << 4); }
inline std::uint32_t Nib4(std::uint32_t x, std::uint32_t y) { return kCol4[y & 7][x & 31] + ((y & 8) << 5); }

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
            return (block * 256 + Off32(x, y)) & kMask;
        }
        case kPsmCt16: case kPsmCt16S: case kPsmZ16: case kPsmZ16S: {
            const std::uint32_t page = (x >> 6) + (y >> 6) * bw;
            const auto& table = (psm == kPsmCt16S || psm == kPsmZ16S) ? kBlock16S : kBlock16;
            const std::uint32_t block = bp + page * 32 + table[(y >> 3) & 7][(x >> 4) & 3];
            return (block * 256 + Off16(x, y)) & kMask;
        }
        case kPsmT8: {
            const std::uint32_t page = (x >> 7) + (y >> 6) * (bw >> 1);
            const std::uint32_t block = bp + page * 32 + kBlock8[(y >> 4) & 3][(x >> 4) & 7];
            return (block * 256 + Off8(x, y)) & kMask;
        }
        case kPsmT4: {
            const std::uint32_t page = (x >> 7) + (y >> 7) * (bw >> 1);
            const std::uint32_t block = bp + page * 32 + kBlock4[(y >> 4) & 7][(x >> 5) & 3];
            const std::uint32_t index = Nib4(x, y);
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
    // Formats with whole-byte or 4-bit pixels: a run of pixels inside one block row is written with
    // the block base computed once and the in-block offset tables per pixel.
    std::uint32_t run_len = 0;
    switch (dpsm) {
        case kPsmCt32: case kPsmZ32: run_len = 8; break;
        case kPsmCt16: case kPsmCt16S: case kPsmZ16: case kPsmZ16S: case kPsmT8: run_len = 16; break;
        case kPsmT4: run_len = 32; break;
        default: break;
    }
    if (run_len != 0) {
        std::uint64_t i = 0;
        std::uint8_t* const m = mem_.data();
        while (i < pixels && pos < total) {
            const std::uint32_t x = (dsax + col) & 2047;
            const std::uint32_t y = (dsay + row) & 2047;
            std::uint64_t n = run_len - (x & (run_len - 1));
            n = std::min<std::uint64_t>(n, rrw - col);
            n = std::min<std::uint64_t>(n, 2048 - x);
            n = std::min<std::uint64_t>(n, pixels - i);
            n = std::min<std::uint64_t>(n, total - pos);
            const std::uint32_t base = PixelAddress(dpsm, dbp, dbw, x, y) & ~0xFFu;
            bool changed = false;
            std::uint32_t k = 0;
            if (bpp == 32 && (x & 1) == 0) {
                // CT32: pixels 2j and 2j+1 of a block row are adjacent words (column order).
                for (; k + 1 < n; k += 2) {
                    std::uint8_t* p = m + base + Off32(x + k, y);
                    std::uint64_t old_v, new_v;
                    std::memcpy(&old_v, p, 8);
                    std::memcpy(&new_v, data + (i + k) * 4, 8);
                    if (old_v != new_v) {
                        std::memcpy(p, &new_v, 8);
                        changed = true;
                    }
                }
            }
            for (; k < n; ++k) {
                const std::uint32_t xx = x + k;
                const std::uint64_t ii = i + k;
                switch (bpp) {
                    case 32: {
                        std::uint8_t* p = m + base + Off32(xx, y);
                        if (std::memcmp(p, data + ii * 4, 4) != 0) {
                            std::memcpy(p, data + ii * 4, 4);
                            changed = true;
                        }
                        break;
                    }
                    case 16: {
                        std::uint8_t* p = m + base + Off16(xx, y);
                        if (std::memcmp(p, data + ii * 2, 2) != 0) {
                            std::memcpy(p, data + ii * 2, 2);
                            changed = true;
                        }
                        break;
                    }
                    case 8: {
                        std::uint8_t* p = m + base + Off8(xx, y);
                        if (*p != data[ii]) {
                            *p = data[ii];
                            changed = true;
                        }
                        break;
                    }
                    default: {  // 4-bit
                        const std::uint32_t nib = Nib4(xx, y);
                        std::uint8_t* p = m + base + (nib >> 1);
                        const std::uint8_t v = static_cast<std::uint8_t>((data[ii >> 1] >> ((ii & 1) * 4)) & 0xF);
                        const std::uint8_t nv = (nib & 1) ? static_cast<std::uint8_t>((*p & 0x0F) | v << 4)
                                                          : static_cast<std::uint8_t>((*p & 0xF0) | v);
                        if (nv != *p) {
                            *p = nv;
                            changed = true;
                        }
                        break;
                    }
                }
            }
            if (changed) Touch(base);
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
