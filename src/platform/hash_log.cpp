#include "platform/hash_log.h"

#include <cstring>

namespace ac04::platform {

std::uint64_t HashBytes(const std::uint8_t* p, std::size_t n) {
    // Four independent lanes so the multiplies overlap; combined at the end.
    std::uint64_t h[4] = {0x9E3779B97F4A7C15ull, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull, 0x27D4EB2F165667C5ull};
    std::size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        for (int k = 0; k < 4; ++k) {
            std::uint64_t w;
            std::memcpy(&w, p + i + 8 * k, 8);
            h[k] = (h[k] ^ w) * 0x100000001B3ull;
            h[k] ^= h[k] >> 29;
        }
    }
    for (; i + 8 <= n; i += 8) {
        std::uint64_t w;
        std::memcpy(&w, p + i, 8);
        h[0] = (h[0] ^ w) * 0x100000001B3ull;
        h[0] ^= h[0] >> 29;
    }
    std::uint64_t r = n;
    for (int k = 0; k < 4; ++k) {
        r = (r ^ h[k]) * 0x9E3779B97F4A7C15ull;
        r ^= r >> 31;
    }
    return r;
}

bool RamHashLog::Open(const std::filesystem::path& path, std::uint32_t ram_size) {
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) return false;
    const std::uint32_t header[4] = {0x48344341u /* "AC4H" */, 1, kBlockSize, ram_size / kBlockSize + 1};
    out_.write(reinterpret_cast<const char*>(header), sizeof(header));
    blocks_.assign(header[3], 0);
    return static_cast<bool>(out_);
}

void RamHashLog::Write(std::uint64_t vblank, const std::uint8_t* ram, std::uint32_t ram_size, const std::uint8_t* spr,
                       std::uint32_t spr_size) {
    if (!out_) return;
    const std::uint32_t n = ram_size / kBlockSize;
    std::uint64_t total = 0xCBF29CE484222325ull;
    for (std::uint32_t b = 0; b < n && b < blocks_.size(); ++b) {
        blocks_[b] = HashBytes(ram + static_cast<std::size_t>(b) * kBlockSize, kBlockSize);
        total = (total ^ blocks_[b]) * 0x100000001B3ull;
    }
    blocks_.back() = HashBytes(spr, spr_size);
    total = (total ^ blocks_.back()) * 0x100000001B3ull;
    out_.write(reinterpret_cast<const char*>(&vblank), 8);
    out_.write(reinterpret_cast<const char*>(&total), 8);
    out_.write(reinterpret_cast<const char*>(blocks_.data()), static_cast<std::streamsize>(blocks_.size() * 8));
}

}  // namespace ac04::platform
