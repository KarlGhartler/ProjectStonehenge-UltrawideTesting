// Determinism checker (docs/port/slices.md §B P1, docs/port/validation-strategy.md): a hash of guest
// RAM per vblank, with one hash per 64 KiB block so `tools/research/hashdiff.py` can name the first
// region where two runs differ. File format (little-endian): "AC4H", u32 version 1, u32 block size,
// u32 block count (RAM blocks + 1 for the scratchpad), then per record: u64 vblank, u64 hash of
// everything, u64 block hashes[block count].
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ac04::platform {

class RamHashLog {
public:
    static constexpr std::uint32_t kBlockSize = 64 * 1024;

    bool Open(const std::filesystem::path& path, std::uint32_t ram_size);
    // Called with the game parked (guest memory stable).
    void Write(std::uint64_t vblank, const std::uint8_t* ram, std::uint32_t ram_size, const std::uint8_t* spr,
               std::uint32_t spr_size);
    bool IsOpen() const { return out_.is_open(); }

private:
    std::ofstream out_;
    std::vector<std::uint64_t> blocks_;
};

// 64-bit hash of `n` bytes (n a multiple of 8): word-wise multiply/xor-shift. Not cryptographic.
std::uint64_t HashBytes(const std::uint8_t* p, std::size_t n);

}  // namespace ac04::platform
