// SHA-1 (FIPS 180-4), written in-house (docs/decisions/dependencies.md) for the disc image check
// (docs/platform/executable.md). Not used for security.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ac04::platform {

class Sha1 {
public:
    Sha1() { Reset(); }
    void Reset();
    void Update(const void* data, size_t bytes);
    // Finishes the hash and returns 40 lowercase hex digits. Call Reset() before reusing.
    std::string FinalHex();

private:
    void Block(const std::uint8_t* p);

    std::uint32_t h_[5];
    std::uint64_t length_ = 0;
    std::uint8_t buffer_[64];
    size_t used_ = 0;
};

std::string Sha1Hex(const void* data, size_t bytes);

}  // namespace ac04::platform
