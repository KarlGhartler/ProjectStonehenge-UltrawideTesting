// Movie upscaling side path (ADR-006 §5): maps a GS upload from the movie image buffer back to the
// decoded picture, so the renderer can sample the upscaled picture in its place. No Vulkan here.
#pragma once

#include <cstdint>

namespace ac04::render {

// `guest_source` is the guest address of the upload's first texel (DrawCommand::texture_guest_source),
// `upload_width` its width in texels. The image buffer holds 16-pixel columns of `height` rows, one
// column after the other (sceMpegGetPicture), so an upload 16 texels wide that starts at a row of a
// column covers picture pixels (x + u, y + v). Returns false when the upload does not have that shape.
inline bool MovieTexelOrigin(std::uint32_t guest_source, std::uint32_t upload_width, std::uint32_t picture_addr,
                             std::uint32_t picture_bytes, int height, int& x, int& y) {
    if (upload_width != 16 || height <= 0 || guest_source < picture_addr ||
        guest_source - picture_addr >= picture_bytes)
        return false;
    const std::uint32_t offset = guest_source - picture_addr;
    if ((offset & 0x3F) != 0) return false;  // must start at a row of a column (16 texels x 4 bytes)
    const std::uint32_t texel = offset / 4;
    const std::uint32_t column_texels = 16u * static_cast<std::uint32_t>(height);
    x = static_cast<int>(texel / column_texels) * 16;
    y = static_cast<int>((texel % column_texels) / 16);
    return true;
}

}  // namespace ac04::render
