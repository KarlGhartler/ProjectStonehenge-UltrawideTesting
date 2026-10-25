#include "platform/render/pipeline_cache.h"

#include <cstring>
#include <fstream>
#include <system_error>

#include "platform/paths.h"

namespace ac04::render {
namespace {

std::uint32_t ReadU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

}  // namespace

bool PipelineCacheHeaderMatches(const std::uint8_t* data, std::size_t size, std::uint32_t vendor_id,
                                std::uint32_t device_id, const std::uint8_t* uuid) {
    // VkPipelineCacheHeaderVersionOne: headerSize, headerVersion, vendorID, deviceID (u32 each,
    // little endian on every supported host), then pipelineCacheUUID[16]: 32 bytes.
    constexpr std::size_t kHeaderBytes = 32;
    if (data == nullptr || size < kHeaderBytes || size > kMaxPipelineCacheBytes) return false;
    const std::uint32_t header_size = ReadU32(data);
    if (header_size < kHeaderBytes || header_size > size) return false;
    if (ReadU32(data + 4) != 1) return false;  // VK_PIPELINE_CACHE_HEADER_VERSION_ONE
    if (ReadU32(data + 8) != vendor_id || ReadU32(data + 12) != device_id) return false;
    return std::memcmp(data + 16, uuid, 16) == 0;
}

bool ReadPipelineCacheFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    bytes.clear();
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxPipelineCacheBytes) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    bytes.resize(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        return false;
    }
    return true;
}

bool WritePipelineCacheFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
                            std::string& error) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    std::error_code ec;
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
            !f.flush()) {
            f.close();
            std::filesystem::remove(temp, ec);
            error = "cannot write " + platform::PathToUtf8(temp);
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        error = "cannot replace " + platform::PathToUtf8(path) + ": " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

}  // namespace ac04::render
