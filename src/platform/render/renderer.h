// Renderer front end for the skeleton (ADR-003, ADR-006).
// - A native render target (512x448 x internal scale, scale fixed at 1) that the GS renderer will
//   draw into; for now it holds a test pattern.
// - The output pass scales it into the window at the display aspect with the selected filter.
// - An overlay hook draws after the output pass (debug overlay, milestone D).
#pragma once

#include <functional>
#include <string>
#include <vector>

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

    bool Init(SDL_Window* window, const platform::PresentationSettings& presentation, std::string& error);
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

    // Display aspect of the game image (4:3, or 16:9 when the game's SCREEN RATIO flag is set).
    void SetDisplayAspect(int w, int h) { aspect_w_ = w; aspect_h_ = h; }

private:
    static constexpr int kFramesInFlight = 2;

    bool RecreateSwapchain(std::string& error);
    void DestroySwapchainResources();
    bool CreateNativeTarget(std::string& error);
    bool CreatePipeline(std::string& error);
    bool UploadTestPattern(std::string& error);

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
};

}  // namespace ac04::render
