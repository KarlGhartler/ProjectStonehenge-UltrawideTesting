// Renderer front end for the skeleton (ADR-003, ADR-006).
// - A native render target (512x448 x internal scale, scale fixed at 1) that the GS renderer will
//   draw into; for now it holds a test pattern.
// - The output pass scales it into the window at the display aspect with the selected filter.
// - An overlay hook draws after the output pass (debug overlay, milestone D).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/render/gs/gs_state.h"
#include "platform/render/gs/gs_vulkan.h"
#include "platform/render/movie_upscaler.h"
#include "platform/render/vk_context.h"
#include "platform/settings.h"

struct SDL_Window;
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace ac04::render {

class Renderer {
public:
    using OverlayHook = std::function<void(VkCommandBuffer)>;

    ~Renderer();

    // `validation`: request the Vulkan validation layer (development builds; used when installed).
    bool Init(SDL_Window* window, const platform::PresentationSettings& presentation, bool validation,
              std::string& error);
    void Shutdown();

    void ApplySettings(const platform::PresentationSettings& presentation);
    void OnWindowResized() { swapchain_dirty_ = true; }

    // Draws and presents one frame. Returns false only on a fatal device error.
    bool DrawFrame();

    void SetOverlayHook(OverlayHook hook) { overlay_hook_ = std::move(hook); }

    VkContext& Context() { return ctx_; }
    VkRenderPass OutputRenderPass() const { return render_pass_; }
    uint32_t SwapchainImageCount() const { return static_cast<uint32_t>(ctx_.swapchain_images.size()); }
    int InternalScale() const { return internal_scale_; }

    // GS data of completed game steps (in order) with their source marks (same order, may be
    // shorter); processed now, drawn with the next frame.
    // Processes the steps' GIF data; the emptied step buffers go to `pool` (kept for reuse).
    void SubmitGsSteps(std::vector<std::vector<uint8_t>>& steps, std::vector<std::vector<gs::SourceMark>>& marks,
                       std::vector<std::vector<uint8_t>>& pool);
    // Off: submitted steps update GS registers and local memory but produce no draws (--fast-until:
    // steps that are never shown).
    void SetGsDrawsEnabled(bool on) { gs_state_.SetDrawsEnabled(on); }
    // GS local memory (4 MiB, gs_memory.h layout) for development dumps (--dump-vram).
    const std::uint8_t* GsLocalMemory() const { return gs_state_.Memory().Data(); }
    // Loads a 4 MiB GS memory image (--play-gsdump).
    void LoadGsLocalMemory(const std::uint8_t* image) { gs_state_.Memory().LoadImage(image); }

    // Display registers of the last sceGsPutDispEnv (DISPFB2, DISPLAY2), taken with the GS steps.
    void SetDisplay(uint64_t dispfb, uint64_t display) { gs_backend_.SetDisplay(dispfb, display); }

    // Movie upscaling stage (ADR-006 §5). The factor is the internal scale; 1 = passthrough (the
    // decoder side path is not needed). Development builds can set it for the stage alone
    // (--movie-upscale), with the GS still drawn at 1x, to inspect the stage output.
    int MovieUpscaleFactor() const { return movie_factor_; }
    void SetMovieUpscaleFactor(int factor) { movie_factor_ = factor < 1 ? 1 : factor; }
    // A new decoded picture from the side path (row-major RGBA8; `rgba` is taken). Upscaled once,
    // with the next frame, when the factor is above 1.
    void SubmitMoviePicture(uint64_t serial, int width, int height, std::vector<uint8_t>& rgba);
    // Reads back the last upscaled picture (RGBA8). False when there is none. Waits idle.
    bool CaptureMovieUpscaled(std::vector<uint8_t>& rgba, int& width, int& height);
    // Native (pre-output-pass) image size: the draw buffer size the game uses.
    int NativeWidth() const { return native_width_; }
    int NativeHeight() const { return native_height_; }
    VkImage NativeImage() const { return native_image_; }
    // Reads back the native image (RGBA8, rows top to bottom) for the native screenshot. Waits idle.
    bool CaptureNative(std::vector<uint8_t>& rgba, int& width, int& height);

    // Display aspect of the game image (4:3, or 16:9 when the game's SCREEN RATIO flag is set).
    void SetDisplayAspect(int w, int h) { aspect_w_ = w; aspect_h_ = h; }

private:
    static constexpr int kFramesInFlight = 2;

    bool RecreateSwapchain(std::string& error);
    void DestroySwapchainResources();
    bool CreateNativeTarget(std::string& error, int width = 0, int height = 0);
    bool ResizeNativeTarget(int width, int height);
    bool CreatePipeline(std::string& error);
    bool UploadTestPattern(std::string& error);
    bool ReadbackImage(VkImage image, int width, int height, std::vector<uint8_t>& rgba);

    SDL_Window* window_ = nullptr;
    VkContext ctx_;
    platform::PresentationSettings presentation_;
    int internal_scale_ = 1;
    int aspect_w_ = 4, aspect_h_ = 3;
    bool swapchain_dirty_ = false;
    bool minimised_ = false;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkImage native_image_ = VK_NULL_HANDLE;
    VmaAllocation native_alloc_ = nullptr;
    VkImageView native_view_ = VK_NULL_HANDLE;
    int native_width_ = 0, native_height_ = 0;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffers_[kFramesInFlight]{};
    VkSemaphore image_available_[kFramesInFlight]{};
    VkFence in_flight_[kFramesInFlight]{};
    std::vector<VkSemaphore> render_finished_;  // one per swapchain image
    int frame_ = 0;

    OverlayHook overlay_hook_;

    gs::GsState gs_state_;
    gs::GsBackend gs_backend_;
    bool gs_ready_ = false;
    std::vector<gs::DrawCommand> gs_pending_;
    int native_request_w_ = 0, native_request_h_ = 0;
    float present_scale_[3] = {1.0f, 1.0f, 1.0f};  // field-copy colour scale (brightness option)

    // Movie upscaling stage
    std::unique_ptr<MovieUpscaler> movie_upscaler_;
    bool movie_upscaler_ready_ = false;
    int movie_factor_ = 1;
    uint64_t movie_serial_ = 0, movie_done_serial_ = 0;
    int movie_width_ = 0, movie_height_ = 0;
    std::vector<uint8_t> movie_rgba_;
};

}  // namespace ac04::render
