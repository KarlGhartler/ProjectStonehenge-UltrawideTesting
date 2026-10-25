// Vulkan pipeline cache file (docs/port/port-features.md, renderer caches). The driver compiles a
// GS pipeline the first time a draw state appears; on a new driver or GPU that took up to 0.9 s per
// pipeline inside a frame (validation.md §2ai). The cache data of the last session is kept in the
// user data folder and handed to vkCreatePipelineCache, so later sessions skip the compile even
// when the driver's own cache was cleared. Presentation only.
//
// The file is the data vkGetPipelineCacheData returns, unchanged. It is used only when its header
// names the same GPU and driver (vendor, device, pipelineCacheUUID); otherwise it is deleted and
// the session starts with an empty cache, as with a rejected settings.json.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ac04::render {

// Upper limit of a cache file the port loads (a larger file is treated as damaged).
constexpr std::size_t kMaxPipelineCacheBytes = std::size_t{64} << 20;

// True when `data` starts with a Vulkan pipeline cache header (VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
// for this vendor, device and pipelineCacheUUID (16 bytes).
bool PipelineCacheHeaderMatches(const std::uint8_t* data, std::size_t size, std::uint32_t vendor_id,
                                std::uint32_t device_id, const std::uint8_t* uuid);

// Reads the file into `bytes`. False when it does not exist, cannot be read or is too large.
bool ReadPipelineCacheFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes);

// Writes `bytes` to a temporary file next to `path` and renames it over `path`, so an interrupted
// write never leaves a half file. False with a reason in `error`.
bool WritePipelineCacheFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
                            std::string& error);

}  // namespace ac04::render
