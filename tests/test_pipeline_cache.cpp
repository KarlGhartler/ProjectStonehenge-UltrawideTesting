#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/render/pipeline_cache.h"

using namespace ac04::render;

namespace {

const std::uint8_t kUuid[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

void PutU32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<std::uint8_t>(x >> (8 * i));
}

// A cache blob as vkGetPipelineCacheData returns it: the 32-byte header, then driver data.
std::vector<std::uint8_t> Blob(std::uint32_t vendor, std::uint32_t device, const std::uint8_t* uuid,
                               std::size_t payload = 64) {
    std::vector<std::uint8_t> v(32 + payload, 0xAB);
    PutU32(v, 0, 32);
    PutU32(v, 4, 1);
    PutU32(v, 8, vendor);
    PutU32(v, 12, device);
    for (int i = 0; i < 16; ++i) v[16 + i] = uuid[i];
    return v;
}

}  // namespace

TEST_CASE("pipeline cache: header must name the same GPU and driver") {
    const auto good = Blob(0x1002, 0x7550, kUuid);
    CHECK(PipelineCacheHeaderMatches(good.data(), good.size(), 0x1002, 0x7550, kUuid));
    CHECK_FALSE(PipelineCacheHeaderMatches(good.data(), good.size(), 0x10DE, 0x7550, kUuid));  // vendor
    CHECK_FALSE(PipelineCacheHeaderMatches(good.data(), good.size(), 0x1002, 0x7551, kUuid));  // device
    std::uint8_t other[16];
    for (int i = 0; i < 16; ++i) other[i] = kUuid[i];
    other[15] ^= 1;  // driver version (pipelineCacheUUID)
    CHECK_FALSE(PipelineCacheHeaderMatches(good.data(), good.size(), 0x1002, 0x7550, other));
}

TEST_CASE("pipeline cache: damaged headers are rejected") {
    auto v = Blob(0x1002, 0x7550, kUuid);
    CHECK_FALSE(PipelineCacheHeaderMatches(v.data(), 31, 0x1002, 0x7550, kUuid));  // truncated header
    CHECK_FALSE(PipelineCacheHeaderMatches(nullptr, 0, 0x1002, 0x7550, kUuid));
    auto bad_version = v;
    PutU32(bad_version, 4, 2);
    CHECK_FALSE(PipelineCacheHeaderMatches(bad_version.data(), bad_version.size(), 0x1002, 0x7550, kUuid));
    auto bad_size = v;
    PutU32(bad_size, 0, 16);  // header smaller than the version-one header
    CHECK_FALSE(PipelineCacheHeaderMatches(bad_size.data(), bad_size.size(), 0x1002, 0x7550, kUuid));
    PutU32(bad_size, 0, static_cast<std::uint32_t>(v.size() + 1));  // header past the end
    CHECK_FALSE(PipelineCacheHeaderMatches(bad_size.data(), bad_size.size(), 0x1002, 0x7550, kUuid));
}

TEST_CASE("pipeline cache: file written whole and read back") {
    const auto dir = std::filesystem::temp_directory_path() / "ac04_test_pipeline_cache";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    const auto path = dir / "pipelines.bin";
    std::vector<std::uint8_t> read;
    CHECK_FALSE(ReadPipelineCacheFile(path, read));  // no file yet

    const auto blob = Blob(0x10DE, 0x2684, kUuid, 1000);
    std::string error;
    REQUIRE(WritePipelineCacheFile(path, blob, error));
    CHECK_FALSE(std::filesystem::exists(dir / "pipelines.bin.tmp"));
    REQUIRE(ReadPipelineCacheFile(path, read));
    CHECK(read == blob);

    // Replaced by a later session's data.
    const auto smaller = Blob(0x10DE, 0x2684, kUuid, 10);
    REQUIRE(WritePipelineCacheFile(path, smaller, error));
    REQUIRE(ReadPipelineCacheFile(path, read));
    CHECK(read == smaller);
    std::filesystem::remove_all(dir, ec);
}
