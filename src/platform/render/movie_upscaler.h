// Movie upscaling stage (ADR-006 §5, User decisions 2026-10-13 and 2026-10-14): a replaceable
// component that scales a decoded movie picture by the renderer's internal-resolution factor N.
// - Input: the decoder's host copy of the picture (side path from sceMpegGetPicture, option b):
//   row-major RGBA8, 640x448 or 640x336. Guest RAM and game code are not involved.
// - At N = 1 the stage is not run (passthrough): the renderer draws the game's own GS upload.
// - At N > 1 it runs once per new picture (keyed by its serial number) and its output is reused
//   until the next picture; the renderer samples it in place of the uploaded movie texture.
// First implementation: AMD FidelityFX FSR 1 EASU (CreateFsr1MovieUpscaler). RCAS is not applied.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/render/movie_texel_map.h"
#include "platform/render/vk_context.h"

namespace ac04::render {

class MovieUpscaler {
public:
    virtual ~MovieUpscaler() = default;

    virtual bool Init(VkContext& ctx, std::string& error) = 0;
    virtual void Shutdown() = 0;

    // Records into `cmd` the upscaling of `rgba` (width x height, row-major RGBA8) by `factor`
    // (integer >= 2) per axis. `slot` is the frame-in-flight index: its host upload buffer is
    // rewritten, so the slot's previous command buffer must have completed.
    virtual bool Upscale(VkCommandBuffer cmd, int slot, const std::vector<std::uint8_t>& rgba, int width,
                         int height, int factor) = 0;

    // The result of the last Upscale (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL once the recorded
    // commands have run), or null before the first one.
    virtual VkImage OutputImage() const = 0;
    virtual VkImageView OutputView() const = 0;
    virtual int OutputWidth() const = 0;
    virtual int OutputHeight() const = 0;
};

std::unique_ptr<MovieUpscaler> CreateFsr1MovieUpscaler();

}  // namespace ac04::render
