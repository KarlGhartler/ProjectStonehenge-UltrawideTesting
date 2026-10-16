// Reads guest RAM and EE registers from a PCSX2 2.8.2 save state for function test vectors
// (validation-strategy.md §4). States are never copied into the repo: tests find them in the folder
// named by AC04_TEST_STATES and are skipped without it.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace ac04::test {

class P2sState {
public:
    // Opens <AC04_TEST_STATES>/<name>; false when the variable is unset or the file is missing.
    bool Open(const std::string& name) {
        const char* dir = std::getenv("AC04_TEST_STATES");
        if (dir == nullptr || *dir == '\0') return false;
        std::ifstream f(std::string(dir) + "/" + name, std::ios::binary);
        if (!f) return false;
        data_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !data_.empty();
    }

    // A stored (uncompressed) zip entry; empty when missing or compressed.
    std::vector<uint8_t> Entry(const std::string& name) const {
        if (data_.size() < 22) return {};
        size_t eocd = data_.size() - 22;
        while (eocd > 0 && U32(eocd) != 0x06054B50u) --eocd;
        if (U32(eocd) != 0x06054B50u) return {};
        const uint16_t count = U16(eocd + 10);
        size_t p = U32(eocd + 16);
        for (uint16_t i = 0; i < count && p + 46 <= data_.size(); ++i) {
            const uint16_t method = U16(p + 10);
            const uint32_t size = U32(p + 24);
            const uint16_t name_len = U16(p + 28), extra_len = U16(p + 30), comment_len = U16(p + 32);
            const uint32_t local = U32(p + 42);
            const std::string entry(reinterpret_cast<const char*>(&data_[p + 46]), name_len);
            if (entry == name) {
                if (method != 0) return {};
                const size_t start = local + 30 + U16(local + 26) + U16(local + 28);
                if (start + size > data_.size()) return {};
                return std::vector<uint8_t>(data_.begin() + start, data_.begin() + start + size);
            }
            p += 46 + name_len + extra_len + comment_len;
        }
        return {};
    }

    // Low 64 bits of GPR n from "PCSX2 Internal Structures.dat" (register file at 0x162,
    // 16 bytes per register; validation.md B-1).
    static uint64_t Gpr(const std::vector<uint8_t>& internal, int n) {
        uint64_t v = 0;
        std::memcpy(&v, internal.data() + 0x162 + n * 16, 8);
        return v;
    }

private:
    uint16_t U16(size_t o) const { return static_cast<uint16_t>(data_[o] | data_[o + 1] << 8); }
    uint32_t U32(size_t o) const { return U16(o) | static_cast<uint32_t>(U16(o + 2)) << 16; }

    std::vector<uint8_t> data_;
};

}  // namespace ac04::test
