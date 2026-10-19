// Vulkan backend of the GS renderer (ADR-003): replays the draw commands of GsState into render
// targets (one RGBA8 image per frame buffer address, each with a D32 depth image for the GS Z
// buffer drawn with it), with the textures of the frame packed into an atlas, and provides the image to present (ADR-006 §4: the draw buffer as it stands when the field
// copy reads it).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/render/gs/gs_state.h"
#include "platform/render/vk_context.h"

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace ac04::render::gs {

class GsBackend {
public:
    static constexpr int kFrames = 2;  // frames in flight (as the renderer)

    ~GsBackend();
    bool Init(VkContext& ctx, std::string& error);
    void Shutdown();

    // True when a frame buffer at block pointer `tbp` has been drawn (texture reads come from it).
    bool IsFrameBuffer(std::uint32_t tbp) const;

    // Records the draws into `cmd` (outside any render pass); `frame` selects the per-frame buffers.
    void Record(VkCommandBuffer cmd, int frame, const std::vector<DrawCommand>& draws);

    // Display registers of the last sceGsPutDispEnv (DISPFB2, DISPLAY2). The image to present is the
    // source of the field copy (ADR-006 §4): the first draw that reads a frame buffer as a texture
    // and writes into the display buffer at DISPFB's FBP. Other frame-as-texture reads (the flight
    // sky's work pages, glow, heat haze) are not the field copy. Before any DISPFB is known the first
    // frame-as-texture read is taken, as before.
    void SetDisplay(std::uint64_t dispfb, std::uint64_t display) {
        dispfb_ = dispfb;
        display_ = display;
    }
    // Size of the image to present (0 when nothing has been drawn yet).
    void PresentSize(int& w, int& h) const;
    // Copies the image to present into `dst` (layout SHADER_READ_ONLY before and after), at most
    // `dst_w` x `dst_h` pixels.
    void CopyPresent(VkCommandBuffer cmd, VkImage dst, int dst_w, int dst_h);
    // Colour scale of the field copy that read the presented image (GS MODULATE with the copy's
    // vertex colour / 128; 1.0 without a copy): the game's brightness option (ADR-006 §4).
    void PresentScale(float out[3]) const {
        for (int c = 0; c < 3; ++c) out[c] = present_scale_[c];
    }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        int width = 0, height = 0;
    };
    struct Target {
        Image img;
        Image depth;  // the GS Z buffer drawn with this frame buffer (D32: Z / 2^bits of the Z format)
        VkFramebuffer fb = VK_NULL_HANDLE;
        std::uint32_t fbp = 0, fbw = 0;
        std::uint32_t psm = 0;  // layout class: 0 (CT32/CT24), kPsmCt16 or kPsmCt16S
        std::uint64_t last_write = 0;  // draw clock of the last draw into it (the newest data wins)
        int used_height = 0;
    };
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        void* mapped = nullptr;
        std::size_t size = 0;
    };

    // Frame buffer shown when no field copy was drawn (PresentSize and CopyPresent use the same one).
    const Target* DisplayTarget() const;
    bool CreateImage(Image& img, int w, int h, VkImageUsageFlags usage, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
    void DestroyImage(Image& img);
    bool EnsureBuffer(Buffer& b, std::size_t size, VkBufferUsageFlags usage);
    void DestroyBuffer(Buffer& b);
    void Transition(VkCommandBuffer cmd, Image& img, VkImageLayout layout);
    Target* GetTarget(VkCommandBuffer cmd, std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm);
    // Builds the texture of draw `d` (TEX0 format, address, width) from target `src`, which holds
    // that memory in another format or width, into feedback_ (gs_conv.comp).
    void Convert(VkCommandBuffer cmd, int frame, Target& src, const DrawCommand& d);
    // One conversion (gs_conv.comp) of `src` into feedback_: texels [u0, u1) x [v0, v1) of the
    // buffer (psm, tbp, tbw), with TEXA (TA0 bits 0-7, AEM 15, TA1 16-23) and an optional CLUT.
    struct ConvRequest {
        std::uint32_t psm = 0, tbp = 0, tbw = 0, texa = 0;
        std::int32_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        const std::array<std::uint32_t, 256>* clut = nullptr;
    };
    bool ConvertCore(VkCommandBuffer cmd, int frame, Target& src, const ConvRequest& r);
    // The newest other view of `t`'s memory (same base page) drawn after `t`, or null.
    Target* NewerView(const Target& t);
    // Before a draw into `t`: converts `newer` into it.
    void Sync(VkCommandBuffer cmd, int frame, Target& t, Target& newer);
    bool InitConvert(std::string& error);
    VkPipeline GetPipeline(std::uint64_t key);
    void LogOnce(const char* key, const char* text);

    VkContext* ctx_ = nullptr;
    VkRenderPass pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkShaderModule vs_ = VK_NULL_HANDLE, fs_ = VK_NULL_HANDLE;
    std::unordered_map<std::uint64_t, VkPipeline> pipelines_;

    Image atlas_;
    Image feedback_;
    Image date_;  // destination alpha test: copy of the target region a DATE draw covers
    Image present_;
    bool present_valid_ = false;
    int present_w_ = 0, present_h_ = 0;
    float present_scale_[3] = {1.0f, 1.0f, 1.0f};
    std::uint32_t present_fbp_ = 0;
    std::uint64_t dispfb_ = 0, display_ = 0;
    Buffer staging_[kFrames];
    Buffer vertices_[kFrames];
    std::map<std::uint64_t, Target> targets_;
    std::uint64_t write_clock_ = 0;
    // Format conversion (gs_conv.comp): layout tables, per-frame descriptor pools and CLUT buffers.
    static constexpr std::uint32_t kMaxConversions = 512;  // per frame
    VkDescriptorSetLayout conv_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout conv_layout_ = VK_NULL_HANDLE;
    VkPipeline conv_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool conv_pool_[kFrames]{};
    Buffer conv_tables_;
    Buffer conv_clut_[kFrames];
    std::uint32_t conv_count_ = 0;
    // Z buffers seen in draws (ZBUF.ZBP -> target key and the ZBUF value): a later draw whose FRAME
    // points at a Z buffer (the game clears Z by drawing into it as a colour buffer) fills the
    // depth image of that target instead.
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> zbuf_owner_;
};

}  // namespace ac04::render::gs
