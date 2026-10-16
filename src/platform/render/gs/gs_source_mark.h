// Where a block of GIF data came from in guest memory (movie upscaling side path, ADR-006 §5).
// Filled by the DMA walker (src/sce/libgraph.cpp), read by GsState. Presentation only.
#pragma once

#include <cstdint>

namespace ac04::render::gs {

struct SourceMark {
    std::uint32_t offset = 0;  // byte offset in the step's GIF data
    std::uint32_t bytes = 0;
    std::uint32_t guest_addr = 0;
};

}  // namespace ac04::render::gs
