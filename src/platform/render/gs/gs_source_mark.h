// Marks on the GIF data of a step, filled by the DMA walker (src/sce/libgraph.cpp) and read by
// GsState. Presentation only.
// - kSource: where a block of GIF data came from in guest memory (movie upscaling side path,
//   ADR-006 §5).
// - kAntiAlias: the point where a flight view's 3D is complete and its 2D overlay (HUD, menus,
//   fade) begins; the optional anti-aliasing pass runs there over `rect` (docs/port/port-features.md).
#pragma once

#include <cstdint>

namespace ac04::render::gs {

struct SourceMark {
    enum Kind : std::uint8_t { kSource = 0, kAntiAlias = 1 };
    std::uint32_t offset = 0;  // byte offset in the step's GIF data
    std::uint32_t bytes = 0;
    std::uint32_t guest_addr = 0;
    Kind kind = kSource;
    std::int16_t rect[4] = {0, 0, 0, 0};  // kAntiAlias: the view's x0, y0, x1, y1 (inclusive, pixels)
};

}  // namespace ac04::render::gs
