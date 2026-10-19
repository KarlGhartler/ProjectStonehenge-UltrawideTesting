// GS register state machine (ADR-003): consumes the GIF data of one game step (PATH2 via VIF1
// DIRECT and PATH3), keeps the register file of both contexts, applies IMAGE transfers and CLUT
// loads to the local-memory model, and turns vertex kicks into draw commands for the Vulkan backend.
// No Vulkan here: the class is testable on its own.
//
// Draws carry their state by value. Textures are decoded at kick time from local memory (so later
// uploads to the same address do not affect earlier draws) into RGBA8 rectangles that the backend
// packs into a per-frame atlas; only the texel rectangle the primitive can reach is decoded.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "platform/render/gs/gs_memory.h"
#include "platform/render/gs/gs_source_mark.h"

namespace ac04::render::gs {

struct GsVertex {
    float x = 0, y = 0;  // pixels in frame-buffer space (XYOFFSET applied)
    std::uint32_t z = 0;
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    float s = 0, t = 0, q = 1;  // STQ (FST = 0)
    float u = 0, v = 0;         // texels (FST = 1, UV / 16)
    std::uint8_t fog = 0;
};

// Decoded texels for one draw: level 0 is `base_width` x `base_height` RGBA8 texels covering texture
// coordinates [origin_u, origin_u + base_width) x [origin_v, ...). Mipmap levels 1..`levels` (TEX1.MMIN
// 2-5 with MXL > 0) follow in full, to the right of level 0 and stacked top to bottom: level l is
// max(tex_width >> l, 1) x max(tex_height >> l, 1) at x = base_width, y = sum of the heights of levels
// 1..l-1 (gs.frag computes the same layout). `width` x `height` is the whole packed image.
// Shared between draws with the same source.
struct DecodedTexture {
    std::uint32_t origin_u = 0, origin_v = 0;
    std::uint32_t width = 0, height = 0;
    std::uint32_t base_width = 0, base_height = 0;
    std::uint32_t levels = 0;
    std::uint32_t tex_width = 0, tex_height = 0;  // 2^TW, 2^TH
    std::vector<std::uint8_t> rgba;               // GS alpha in A (0x80 = 1.0)
};

enum class PrimKind : std::uint8_t { kPoint, kLine, kTriangle, kSprite };

struct DrawCommand {
    PrimKind kind = PrimKind::kTriangle;
    std::vector<GsVertex> vertices;  // triangles: 3 per primitive; sprites: 2; lines: 2; points: 1
    // Attributes (PRIM or PRMODE)
    bool gouraud = false, textured = false, fog = false, blend = false, aa1 = false, fst = false;
    // Context registers in force
    std::uint64_t tex0 = 0, tex1 = 0, clamp = 0, alpha = 0, test = 0, frame = 0, zbuf = 0, scissor = 0;
    std::uint64_t texa = 0, fba = 0, fogcol = 0;
    std::uint64_t miptbp1 = 0, miptbp2 = 0;  // MIPTBP1/2 of the context (mip levels 1-3 / 4-6)
    bool colclamp = true, pabe = false, dthe = false;
    std::shared_ptr<const DecodedTexture> texture;  // null when untextured or sourced from a target
    // Texture source is a frame buffer drawn earlier (block pointer of that frame buffer), or -1.
    std::int32_t texture_from_frame = -1;
    // For a paletted texture read from a frame buffer: the palette as RGBA8 (TEXA applied for 16-bit
    // CLUTs; 4-bit formats: the 16 entries from CSA in 0-15), for the backend's format conversion.
    std::shared_ptr<const std::array<std::uint32_t, 256>> frame_clut;
    // Movie upscaling side path (ADR-006 §5): when the texture's last IMAGE upload came from a marked
    // guest block, the guest address of the upload's first texel and the upload width (texels).
    std::uint32_t texture_guest_source = 0;
    std::uint32_t texture_guest_width = 0;

    std::uint32_t Fbp() const { return static_cast<std::uint32_t>(frame & 0x1FF); }
    std::uint32_t Fbw() const { return static_cast<std::uint32_t>((frame >> 16) & 0x3F); }
    std::uint32_t Fpsm() const { return static_cast<std::uint32_t>((frame >> 24) & 0x3F); }
    std::uint32_t Fbmsk() const { return static_cast<std::uint32_t>(frame >> 32); }
};

class GsState {
public:
    GsState();

    // Frame buffers written so far (block pointer -> width in 64-pixel units), so a texture at the
    // same address is read from the drawn target instead of local memory.
    using FrameQuery = std::function<bool(std::uint32_t tbp)>;
    void SetFrameQuery(FrameQuery q) { frame_query_ = std::move(q); }

    // Processes GIF packets (a sequence of GIF tags with their data, 16-byte units).
    // `marks` (optional) name the blocks of `data` read from known guest addresses.
    void ProcessGif(const std::uint8_t* data, std::size_t bytes, const std::vector<SourceMark>* marks = nullptr);
    // Takes the draws produced since the last call.
    std::vector<DrawCommand> TakeDraws();
    // Off: primitives update no draw list (registers, local memory and CLUTs still do). For steps that
    // are never shown (--fast-until).
    void SetDrawsEnabled(bool on) { draws_enabled_ = on; }

    void WriteRegister(std::uint32_t reg, std::uint64_t value);
    const GsMemory& Memory() const { return mem_; }
    GsMemory& Memory() { return mem_; }
    std::uint64_t Reg(std::uint32_t reg) const { return regs_[reg & 0x7F]; }
    const std::array<std::uint32_t, 512>& Clut() const { return clut_; }

private:
    void Kick(bool draw);
    void EmitPrimitive(const GsVertex* v, int count);
    void LoadClut(std::uint64_t tex);
    std::shared_ptr<const DecodedTexture> DecodeTexture(const DrawCommand& d, const GsVertex* v, int count);
    // The palette of a paletted texture as RGBA8 (DrawCommand::frame_clut).
    std::shared_ptr<const std::array<std::uint32_t, 256>> ClutRgba(const DrawCommand& d) const;
    // Texel rectangle [u0, u1) x [v0, v1) that the primitive can reach (DecodeTexture's footprint).
    void TexelRange(const DrawCommand& d, const GsVertex* v, int count, std::uint32_t r[4]) const;
    void AppendVertices(std::vector<GsVertex>& out, const DrawCommand& d, const GsVertex* v, int count);
    void ImageData(const std::uint8_t* data, std::uint32_t bytes);
    void LogOnce(const char* key, const char* fmt, std::uint64_t a = 0, std::uint64_t b = 0);

    GsMemory mem_;
    std::array<std::uint64_t, 128> regs_{};
    mutable std::shared_ptr<const std::array<std::uint32_t, 256>> last_frame_clut_;  // ClutRgba reuse
    std::array<std::uint32_t, 512> clut_{};  // CLUT buffer: 32-bit entries (CT32 palettes)
    std::uint32_t cbp_[2] = {0, 0};
    // What the CLUT buffer holds, per 16-entry slot (0-31) and for a whole 8-bit palette (32): the
    // source buffer and a copy of its raw bytes at load time (LoadClut skips identical loads).
    struct ClutSource {
        bool valid = false;
        std::uint32_t cbp = 0, psm = 0, len = 0;
        std::array<std::uint8_t, 1024> raw{};
    };
    std::array<ClutSource, 33> clut_src_{};
    // Recently loaded 8-bit palettes (the two passes of a model alternate between two): raw source
    // bytes, the decoded entries and their hash; a load that matches one is a copy.
    struct ClutCacheEntry {
        ClutSource src;
        std::array<std::uint32_t, 256> entries{};
        std::uint64_t hash = 0;
        std::uint64_t used = 0;
    };
    std::array<ClutCacheEntry, 8> clut_cache_{};
    std::uint64_t clut_cache_clock_ = 0;
    // Vertex queue
    GsVertex cur_{};
    float q_ = 1.0f;
    std::array<GsVertex, 3> queue_{};
    int queued_ = 0;
    int strip_count_ = 0;
    GsVertex fan_first_{};
    // IMAGE transfer in progress
    bool transfer_active_ = false;
    std::uint32_t transfer_pos_ = 0;
    // Bytes of a pixel split across two IMAGE packets (PSMCT24/Z24: 3 bytes per pixel, packets are qwords).
    std::array<std::uint8_t, 4> transfer_rest_{};
    std::uint32_t transfer_rest_n_ = 0;
    // Source marks of the data being processed, and the guest source of the last upload per block
    // pointer (only for uploads from marked data): address of the first texel, upload width.
    const std::uint8_t* gif_base_ = nullptr;
    const std::vector<SourceMark>* marks_ = nullptr;
    std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> upload_source_;
    std::vector<DrawCommand> draws_;
    FrameQuery frame_query_;
    // Frame buffers drawn so far, in stream order (FBP -> pages covered by FBW x scissor height), and
    // those a later host->local or local->local transfer wrote over: a texture at such an address is
    // what the transfer stored (the HUD font is uploaded each frame over a sky work page), so it is
    // read from local memory until the buffer is drawn again.
    std::unordered_map<std::uint32_t, std::uint32_t> fb_pages_;
    std::unordered_set<std::uint32_t> fb_overwritten_;
    void NoteFrameBuffer(const DrawCommand& d);
    void NoteTransferDestination();
    using TextureKey = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t,
                                  std::uint32_t, std::uint32_t>;
    std::map<TextureKey, std::shared_ptr<const DecodedTexture>> texture_cache_;
    // Speed-ups for long runs of primitives with the same state (e.g. the briefing map's ~10,000
    // sprites): the draw being built (reused, so no allocation per primitive), the palette hash
    // per palette load, and the last texture lookup.
    DrawCommand building_;
    // No register that feeds the draw state was written since the last primitive, which went to
    // draws_.back() with texel range fast_range_ (fast path: append without rebuilding the state).
    bool state_dirty_ = true;
    bool draws_enabled_ = true;
    std::uint32_t fast_range_[4] = {};
    std::uint32_t clut_gen_ = 0;
    std::uint32_t clut_hash_gen_ = ~0u;
    std::uint32_t clut_hash_args_ = ~0u;
    std::uint64_t clut_hash_ = 0;
    bool last_key_valid_ = false;
    TextureKey last_key_{};
    std::shared_ptr<const DecodedTexture> last_texture_;
};

}  // namespace ac04::render::gs
