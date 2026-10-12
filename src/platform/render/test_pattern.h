// Test image for the native render target until the GS renderer draws game frames (milestones
// A–C). Shows colour bars, a one-pixel grid, a fine checkerboard and a border, so the sharp and
// smooth output filters and the 4:3 placement can be checked by eye.
#pragma once

#include <cstdint>
#include <vector>

namespace ac04::render {

constexpr int kNativeWidth = 512;   // PS2 draw buffer (ADR-006)
constexpr int kNativeHeight = 448;

// RGBA8 pixels, row-major, width*height*4 bytes.
std::vector<std::uint8_t> MakeTestPattern(int width, int height);

}  // namespace ac04::render
