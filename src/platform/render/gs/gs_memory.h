// GS local memory model (ADR-003, docs/platform/gs-subset.md): 4 MiB addressed in 256-byte blocks
// (block pointers TBP/CBP/DBP) and 8 KiB pages, with the page and block layout of each pixel format
// the game uses. Host -> local IMAGE transfers write here; textures and CLUTs are decoded from here.
//
// Layout per format (GS User's Manual, "Local Memory"):
//   PSMCT32 page 64x32, block 8x8;  PSMCT16/16S page 64x64, block 16x8;
//   PSMT8   page 128x64, block 16x16; PSMT4 page 128x128, block 32x16.
// Blocks are placed inside a page by the per-format block tables, so different textures and
// formats overlap exactly as on the GS at block granularity. Inside a block the pixels are stored
// in row-major order, not in the GS column order: identical results as long as a block is written
// and read in the same format, which holds for everything up to slice S1 (each texture is uploaded
// and sampled in one format; CLUTs are CT32 images read as CT32). HYPOTHESIS until a format
// reinterpretation (PSMT8H, depth as texture, heat haze) needs the exact column order.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ac04::render::gs {

enum Psm : std::uint32_t {
    kPsmCt32 = 0x00,
    kPsmCt24 = 0x01,
    kPsmCt16 = 0x02,
    kPsmCt16S = 0x0A,
    kPsmT8 = 0x13,
    kPsmT4 = 0x14,
    kPsmT8H = 0x1B,
    kPsmT4HL = 0x24,
    kPsmT4HH = 0x2C,
    kPsmZ32 = 0x30,
    kPsmZ24 = 0x31,
    kPsmZ16 = 0x32,
    kPsmZ16S = 0x3A,
};

// Bits per pixel of a format (0 = unsupported).
int BitsPerPixel(std::uint32_t psm);

class GsMemory {
public:
    static constexpr std::uint32_t kSize = 4u * 1024u * 1024u;

    GsMemory() : mem_(kSize, 0) {}

    // Byte address of pixel (x, y) of a buffer at block pointer `bp` (256-byte units) with width
    // `bw` (64-pixel units). For 4-bit formats `nibble` receives 0 (low) or 1 (high).
    static std::uint32_t PixelAddress(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x,
                                      std::uint32_t y, std::uint32_t* nibble = nullptr);

    std::uint32_t ReadPixel(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x,
                            std::uint32_t y) const;
    void WritePixel(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y,
                    std::uint32_t value);

    // Writes `bytes` of transfer data into the rectangle (dsax, dsay, rrw, rrh) of the buffer
    // (dbp, dbw, dpsm), continuing at pixel `pos` of the rectangle (transfers may be split over
    // several IMAGE packets). Returns the new position.
    std::uint32_t WriteImage(std::uint32_t dbp, std::uint32_t dbw, std::uint32_t dpsm, std::uint32_t dsax,
                             std::uint32_t dsay, std::uint32_t rrw, std::uint32_t rrh, std::uint32_t pos,
                             const std::uint8_t* data, std::uint32_t bytes);

    // Change counter per 8 KiB page, incremented when a write changes its bytes (texture cache
    // invalidation).
    std::uint32_t PageGeneration(std::uint32_t page) const { return page_gen_[page & 511]; }
    // Highest generation over the pages that the rectangle (0,0)-(w,h) of a buffer touches.
    std::uint64_t RegionGeneration(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t w,
                                   std::uint32_t h) const;

    // Content hash of the pages that the rectangle (0,0)-(w,h) of a buffer touches: decoded textures
    // are keyed by it, so a texture that returns to a shared slot (the hangar cycles several through
    // the same memory every frame) is found again. A page is rehashed only after its generation
    // changed.
    std::uint64_t RegionHash(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t w,
                             std::uint32_t h) const;

    const std::uint8_t* Data() const { return mem_.data(); }

private:
    void Touch(std::uint32_t addr) { ++page_gen_[(addr >> 13) & 511]; }

    std::vector<std::uint8_t> mem_;
    std::array<std::uint32_t, 512> page_gen_{};
    mutable std::array<std::uint64_t, 512> page_hash_{};
    mutable std::array<std::uint32_t, 512> page_hash_gen_ = MakeStaleGenerations();

    static std::array<std::uint32_t, 512> MakeStaleGenerations() {
        std::array<std::uint32_t, 512> a{};
        a.fill(~0u);
        return a;
    }
};

}  // namespace ac04::render::gs
