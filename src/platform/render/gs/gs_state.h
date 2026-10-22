// GS register state machine (ADR-003): consumes the GIF data of one game step (PATH2 via VIF1
// DIRECT and PATH3), keeps the register file of both contexts, applies IMAGE transfers and CLUT
// loads to the local-memory model, and turns vertex kicks into draw commands for the Vulkan backend.
// No Vulkan here: the class is testable on its own.
//
// Draws carry their state by value. Textures are decoded as of kick time (later uploads to the same
// address do not affect earlier draws) into RGBA8 images, one per decoded texture; only the texel
// rectangle the primitive can reach is decoded. The decode runs on the GPU (gs_decode.comp, from the
// GPU's copy of local memory, which TextureOp keeps in step with the stream) or, as the reference,
// on the CPU (docs/port/renderer.md §2).
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

// One rectangle of local memory decoded into a texture image: `w` x `h` texels at (su, sv) of the
// buffer (bp, bw) in the texture's format, written at (dx, dy) of the packed image.
struct DecodeRect {
    std::uint32_t bp = 0, bw = 0, su = 0, sv = 0, w = 0, h = 0, dx = 0, dy = 0;
};

// Decoded texels for one draw: level 0 is `base_width` x `base_height` RGBA8 texels covering texture
// coordinates [origin_u, origin_u + base_width) x [origin_v, ...). Mipmap levels 1..`levels` (TEX1.MMIN
// 2-5 with MXL > 0) follow in full, to the right of level 0 and stacked top to bottom: level l is
// max(tex_width >> l, 1) x max(tex_height >> l, 1) at x = base_width, y = sum of the heights of levels
// 1..l-1 (gs.frag computes the same layout). `width` x `height` is the whole packed image; texels
// outside the rectangles are never read. Shared between draws with the same source.
struct DecodedTexture {
    std::uint32_t origin_u = 0, origin_v = 0;
    std::uint32_t width = 0, height = 0;
    std::uint32_t base_width = 0, base_height = 0;
    std::uint32_t levels = 0;
    std::uint32_t tex_width = 0, tex_height = 0;  // 2^TW, 2^TH
    // How the texels come from local memory (DecodeOnCpu and gs_decode.comp do the same): the
    // texture format, TEXA (TA0 bits 0-7, AEM bit 15, TA1 bits 16-23) for CT24/CT16, the palette as
    // RGBA8 for the paletted formats (16-bit CLUT entries expanded with TEXA; 4-bit formats: the 16
    // entries from CSA in 0-15), and the rectangles (level 0, then the mip levels).
    std::uint32_t psm = 0;
    std::uint32_t texa = 0;
    std::shared_ptr<const std::array<std::uint32_t, 256>> palette;
    std::vector<DecodeRect> rects;
    std::vector<std::uint8_t> rgba;  // CPU decode only (empty when only the GPU decodes); GS alpha in A
    std::uint64_t serial = 0;        // unique per decode: the backend's image for it
};

// The texels of every rectangle of `tex` decoded from `mem` into tex.rgba (sized width x height x 4).
// The CPU reference of gs_decode.comp.
void DecodeOnCpu(const GsMemory& mem, DecodedTexture& tex);

// GPU texture work in stream order (docs/port/renderer.md §2.3). Either a memory update (`blocks`:
// local-memory blocks, addr >> 8, ascending, with their 256 bytes each as they were at that point
// in `bytes`) for the GPU's copy of local memory, or a texture to decode (`decode`): from that copy
// on the GPU, or, when it carries rgba, uploaded as decoded on the CPU. The backend must apply every
// op, in order: the GPU copy is only right that way.
struct TextureOp {
    std::vector<std::uint32_t> blocks;
    std::vector<std::uint8_t> bytes;
    std::shared_ptr<const DecodedTexture> decode;
};

// Where textures are decoded: on the CPU (the reference; what the tests use by default), on the GPU,
// or both (--verify-texture-decode: the backend compares them).
enum class TextureDecode { kCpu, kGpu, kBoth };

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
    // Not a primitive: the anti-aliasing point of a flight view (SourceMark::kAntiAlias), with no
    // vertices. `frame` = FRAME_1 then (the buffer the view's 3D was drawn into), `scissor` = the
    // view's rectangle in SCISSOR layout.
    bool aa_point = false;

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
    // Takes the texture work produced since the last call (TextureOp).
    std::vector<TextureOp> TakeTextureOps();
    void SetTextureDecode(TextureDecode mode) { decode_mode_ = mode; }
    TextureDecode Decode() const { return decode_mode_; }
    // RGBA bytes of the textures held by the decoded-texture cache (their images on the GPU, and
    // their rgba with CPU decoding), bounded by kTextureCacheBytes.
    std::size_t TextureCacheBytes() const { return texture_cache_bytes_; }
    static constexpr std::size_t kTextureCacheBytes = std::size_t{256} << 20;
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
    // Emits the anti-aliasing points of marks_ at or before byte `pos` of the step's GIF data.
    void EmitAntiAliasPoints(std::size_t pos);
    std::size_t next_mark_ = 0;
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
    // Bits of the 32-bit pixel any draw has written per FBP (FBMSK and the frame format): a T8H /
    // T4HL / T4HH texture over a frame buffer whose draws all kept its bits (the mission 1 opening
    // draws RGB into FBP 0x70 with FBMSK 0xFF000000 over the aircraft's T8H texture) is what local
    // memory holds, not the buffer's picture.
    std::unordered_map<std::uint32_t, std::uint32_t> fb_written_bits_;
    void NoteFrameBuffer(const DrawCommand& d);
    void NoteTransferDestination();
    // Decoded textures by what they decode (TEX0, palette hash with TEXA and the mip registers, the
    // texel rectangle), one per key, with the address and generation of every GS block they read:
    // an entry is valid while those blocks keep their generations (checked again only after a
    // write to local memory, at WriteCount). A texture whose memory changed is decoded again and
    // replaces the entry; the CPU decoder redoes only the changed blocks (the terrain atlas gets
    // tiles uploaded every frame).
    using TextureKey = std::tuple<std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                  std::uint32_t>;
    struct CacheEntry {
        std::shared_ptr<const DecodedTexture> texture;
        std::vector<std::uint32_t> block_addrs;
        std::vector<std::uint32_t> block_gens;
        std::uint64_t valid_at = 0;  // WriteCount when last checked
    };
    std::map<TextureKey, CacheEntry> texture_cache_;
    std::size_t texture_cache_bytes_ = 0;
    TextureDecode decode_mode_ = TextureDecode::kCpu;
    std::vector<TextureOp> texture_ops_;
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
    std::uint64_t last_valid_at_ = 0;
    std::shared_ptr<const DecodedTexture> last_texture_;
};

}  // namespace ac04::render::gs
