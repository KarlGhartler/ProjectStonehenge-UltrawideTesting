// Builds a tiny ISO 9660 image in memory for the reader tests. It contains no game data: only
// made-up names and contents.
#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "platform/iso9660.h"

namespace test {

class MemorySectorSource final : public ac04::platform::SectorSource {
public:
    explicit MemorySectorSource(std::vector<std::uint8_t> data) : data_(std::move(data)) {}
    std::uint64_t SizeBytes() const override { return data_.size(); }
    bool ReadSectors(std::uint32_t lsn, std::uint32_t count, void* dst) const override {
        const std::uint64_t off = std::uint64_t(lsn) * ac04::platform::kSectorSize;
        const std::uint64_t n = std::uint64_t(count) * ac04::platform::kSectorSize;
        if (off + n > data_.size()) return false;
        std::memcpy(dst, data_.data() + off, n);
        return true;
    }

private:
    std::vector<std::uint8_t> data_;
};

inline void Put32Both(std::uint8_t* p, std::uint32_t v) {  // both-endian 32-bit field
    for (int i = 0; i < 4; ++i) {
        p[i] = std::uint8_t(v >> (8 * i));
        p[7 - i] = std::uint8_t(v >> (8 * i));
    }
}

inline std::uint32_t PutRecord(std::uint8_t* p, std::uint32_t lsn, std::uint32_t size, bool dir,
                               const std::string& name) {
    const std::uint32_t len = 33 + std::uint32_t(name.size()) + ((name.size() % 2 == 0) ? 1 : 0);
    std::memset(p, 0, len);
    p[0] = std::uint8_t(len);
    Put32Both(p + 2, lsn);
    Put32Both(p + 10, size);
    p[25] = dir ? 0x02 : 0x00;
    p[32] = std::uint8_t(name.size());
    std::memcpy(p + 33, name.data(), name.size());
    return len;
}

// Layout: 16 empty sectors, PVD (16), terminator (17), root dir (18), BIN dir (19),
// "SLUS_TEST.01;1" at 20 (3000 bytes, 2 sectors), "BIN/DATA.CDP;1" at 22 (10 bytes).
inline std::shared_ptr<MemorySectorSource> MakeTestIso(const std::string& volume_id = "TESTVOLUME") {
    constexpr std::uint32_t S = ac04::platform::kSectorSize;
    std::vector<std::uint8_t> img(23 * S, 0);
    std::uint8_t* pvd = &img[16 * S];
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    std::memset(pvd + 8, ' ', 64);
    std::memcpy(pvd + 8, "PLAYSTATION", 11);
    std::memcpy(pvd + 40, volume_id.data(), volume_id.size());
    Put32Both(pvd + 80, 23);
    pvd[128] = S & 0xFF;
    pvd[129] = S >> 8;
    PutRecord(pvd + 156, 18, S, true, std::string(1, '\0'));
    img[17 * S] = 255;
    std::memcpy(&img[17 * S + 1], "CD001", 5);

    std::uint8_t* root = &img[18 * S];
    std::uint32_t off = 0;
    off += PutRecord(root + off, 18, S, true, std::string(1, '\0'));
    off += PutRecord(root + off, 18, S, true, std::string(1, '\1'));
    off += PutRecord(root + off, 19, S, true, "BIN");
    off += PutRecord(root + off, 20, 3000, false, "SLUS_TEST.01;1");

    std::uint8_t* bin = &img[19 * S];
    off = 0;
    off += PutRecord(bin + off, 19, S, true, std::string(1, '\0'));
    off += PutRecord(bin + off, 18, S, true, std::string(1, '\1'));
    off += PutRecord(bin + off, 22, 10, false, "DATA.CDP;1");

    for (std::uint32_t i = 0; i < 3000; ++i) img[20 * S + i] = std::uint8_t(i * 7);
    std::memcpy(&img[22 * S], "0123456789", 10);
    return std::make_shared<MemorySectorSource>(std::move(img));
}

}  // namespace test
