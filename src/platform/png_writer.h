// Minimal PNG writer for the native screenshot (docs/port/port-features.md §8.4.1): RGBA8, no
// compression (stored deflate blocks), CRC-32 and Adler-32 computed here. No dependency.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ac04::platform {

// Encodes `rgba` (width * height * 4 bytes, rows top to bottom) as a PNG file image.
std::vector<std::uint8_t> EncodePng(const std::uint8_t* rgba, int width, int height);
bool WritePng(const std::filesystem::path& path, const std::uint8_t* rgba, int width, int height);

}  // namespace ac04::platform
