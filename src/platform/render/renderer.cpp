#include "platform/render/renderer.h"

#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>

#include <cstring>

#include "output_frag_spv.h"
#include "output_vert_spv.h"
#include "platform/log.h"
#include "platform/render/output_layout.h"
#include "platform/render/test_pattern.h"

namespace ac04::render {
namespace {

struct OutputParams {
    float source_size[2];
    float output_size[2];
    int filter_mode;
};

VkShaderModule MakeShader(VkDevice device, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, nullptr, &m);
    return m;
}

}  // namespace

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(SDL_Window* window, const platform::PresentationSettings& presentation,
                    std::string& error) {
    window_ = window;
    presentation_ = presentation;
    internal_scale_ = 1;  // fixed at 1x until higher scales are validated (port-baseline "Later")
    if (!ctx_.Init(window, AC04_DEV_BUILD != 0, error)) return false;
    AC04_LOG_INFO("renderer", "GPU: %s, Vulkan %s, driver %s", ctx_.gpu.name.c_str(),
                  ctx_.gpu.api_version.c_str(), ctx_.gpu.driver.c_str());

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = ctx_.queue_family;
    vkCreateCommandPool(ctx_.device, &pci, nullptr, &command_pool_);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = command_pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = kFramesInFlight;
    vkAllocateCommandBuffers(ctx_.device, &cai, command_buffers_);
    for (int i = 0; i < kFramesInFlight; ++i) {
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(ctx_.device, &sci, nullptr, &image_available_[i]);
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(ctx_.device, &fci, nullptr, &in_flight_[i]);
    }

    if (!CreateNativeTarget(error)) return false;
    if (!UploadTestPattern(error)) return false;
    if (!RecreateSwapchain(error) && !minimised_) return false;
    if (!CreatePipeline(error)) return false;
    return true;
}

bool Renderer::CreateNativeTarget(std::string& error) {
    native_width_ = kNativeWidth * internal_scale_;
    native_height_ = kNativeHeight * internal_scale_;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {static_cast<uint32_t>(native_width_), static_cast<uint32_t>(native_height_), 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkResult r = vmaCreateImage(ctx_.allocator, &ici, &aci, &native_image_, &native_alloc_, nullptr);
    if (r != VK_SUCCESS) {
        error = std::string("cannot create the native render target: ") + VkResultName(r);
        return false;
    }
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = native_image_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx_.device, &vci, nullptr, &native_view_);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.maxLod = 0.0f;
    vkCreateSampler(ctx_.device, &sci, nullptr, &sampler_);
    return true;
}

bool Renderer::UploadTestPattern(std::string& error) {
    const std::vector<uint8_t> pixels = MakeTestPattern(native_width_, native_height_);
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = pixels.size();
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = nullptr;
    VmaAllocationInfo info{};
    VkResult r = vmaCreateBuffer(ctx_.allocator, &bci, &aci, &staging, &staging_alloc, &info);
    if (r != VK_SUCCESS) {
        error = std::string("cannot create a staging buffer: ") + VkResultName(r);
        return false;
    }
    std::memcpy(info.pMappedData, pixels.data(), pixels.size());
    vmaFlushAllocation(ctx_.allocator, staging_alloc, 0, VK_WHOLE_SIZE);

    VkCommandBuffer cmd = command_buffers_[0];
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = native_image_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<uint32_t>(native_width_), static_cast<uint32_t>(native_height_), 1};
    vkCmdCopyBufferToImage(cmd, staging, native_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx_.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.queue);
    vmaDestroyBuffer(ctx_.allocator, staging, staging_alloc);
    return true;
}

bool Renderer::CreatePipeline(std::string& error) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1;
    dli.pBindings = &binding;
    vkCreateDescriptorSetLayout(ctx_.device, &dli, nullptr, &set_layout_);

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutputParams)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx_.device, &pli, nullptr, &pipeline_layout_);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(ctx_.device, &dpi, nullptr, &descriptor_pool_);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptor_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    vkAllocateDescriptorSets(ctx_.device, &dai, &descriptor_set_);
    VkDescriptorImageInfo dii{sampler_, native_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = descriptor_set_;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);

    VkShaderModule vs = MakeShader(ctx_.device, output_vert_spv, sizeof(output_vert_spv));
    VkShaderModule fs = MakeShader(ctx_.device, output_frag_spv, sizeof(output_frag_spv));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &ds;
    gpi.layout = pipeline_layout_;
    gpi.renderPass = render_pass_;
    VkResult r = vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline_);
    vkDestroyShaderModule(ctx_.device, vs, nullptr);
    vkDestroyShaderModule(ctx_.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        error = std::string("cannot create the output pipeline: ") + VkResultName(r);
        return false;
    }
    return true;
}

bool Renderer::RecreateSwapchain(std::string& error) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    vkDeviceWaitIdle(ctx_.device);
    const VkFormat old_format = ctx_.swapchain_format;
    DestroySwapchainResources();
    if (!ctx_.CreateSwapchain(w, h, presentation_.vsync, error)) {
        minimised_ = error.empty();
        return false;
    }
    minimised_ = false;

    if (!render_pass_ || old_format != ctx_.swapchain_format) {
        if (render_pass_) vkDestroyRenderPass(ctx_.device, render_pass_, nullptr);
        VkAttachmentDescription att{};
        att.format = ctx_.swapchain_format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // clears the bars to black
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpi.attachmentCount = 1;
        rpi.pAttachments = &att;
        rpi.subpassCount = 1;
        rpi.pSubpasses = &sub;
        rpi.dependencyCount = 1;
        rpi.pDependencies = &dep;
        vkCreateRenderPass(ctx_.device, &rpi, nullptr, &render_pass_);
        if (pipeline_) {
            vkDestroyPipeline(ctx_.device, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
            vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
            vkDestroyDescriptorPool(ctx_.device, descriptor_pool_, nullptr);
            vkDestroyDescriptorSetLayout(ctx_.device, set_layout_, nullptr);
            if (!CreatePipeline(error)) return false;
        }
    }

    framebuffers_.resize(ctx_.swapchain_views.size());
    render_finished_.resize(ctx_.swapchain_views.size());
    for (size_t i = 0; i < ctx_.swapchain_views.size(); ++i) {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = render_pass_;
        fci.attachmentCount = 1;
        fci.pAttachments = &ctx_.swapchain_views[i];
        fci.width = ctx_.swapchain_extent.width;
        fci.height = ctx_.swapchain_extent.height;
        fci.layers = 1;
        vkCreateFramebuffer(ctx_.device, &fci, nullptr, &framebuffers_[i]);
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(ctx_.device, &sci, nullptr, &render_finished_[i]);
    }
    swapchain_dirty_ = false;
    return true;
}

void Renderer::DestroySwapchainResources() {
    for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(ctx_.device, fb, nullptr);
    framebuffers_.clear();
    for (VkSemaphore s : render_finished_) vkDestroySemaphore(ctx_.device, s, nullptr);
    render_finished_.clear();
}

void Renderer::ApplySettings(const platform::PresentationSettings& presentation) {
    const bool vsync_changed = presentation.vsync != presentation_.vsync;
    presentation_ = presentation;
    if (vsync_changed) swapchain_dirty_ = true;
}

bool Renderer::DrawFrame() {
    std::string error;
    if (swapchain_dirty_ || minimised_) {
        if (!RecreateSwapchain(error)) {
            if (minimised_) return true;  // nothing to draw into while minimised
            AC04_LOG_ERROR("renderer", "%s", error.c_str());
            return false;
        }
    }

    vkWaitForFences(ctx_.device, 1, &in_flight_[frame_], VK_TRUE, UINT64_MAX);
    uint32_t image = 0;
    VkResult r = vkAcquireNextImageKHR(ctx_.device, ctx_.swapchain, UINT64_MAX, image_available_[frame_],
                                       VK_NULL_HANDLE, &image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty_ = true;
        return true;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        AC04_LOG_ERROR("renderer", "vkAcquireNextImageKHR: %s", VkResultName(r));
        return r != VK_ERROR_DEVICE_LOST;
    }
    vkResetFences(ctx_.device, 1, &in_flight_[frame_]);

    VkCommandBuffer cmd = command_buffers_[frame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = render_pass_;
    rbi.framebuffer = framebuffers_[image];
    rbi.renderArea.extent = ctx_.swapchain_extent;
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    const Rect rect = FitAspect(static_cast<int>(ctx_.swapchain_extent.width),
                                static_cast<int>(ctx_.swapchain_extent.height), aspect_w_, aspect_h_);
    if (rect.width > 0 && rect.height > 0) {
        VkViewport viewport{static_cast<float>(rect.x), static_cast<float>(rect.y),
                            static_cast<float>(rect.width), static_cast<float>(rect.height), 0.0f, 1.0f};
        VkRect2D scissor{{rect.x, rect.y}, {static_cast<uint32_t>(rect.width), static_cast<uint32_t>(rect.height)}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                                &descriptor_set_, 0, nullptr);
        OutputParams params{{static_cast<float>(native_width_), static_cast<float>(native_height_)},
                            {static_cast<float>(rect.width), static_cast<float>(rect.height)},
                            presentation_.filter == platform::ScaleFilter::Sharp ? 0 : 1};
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), &params);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    if (overlay_hook_) {
        // The overlay covers the whole window, not only the game rectangle.
        VkViewport full{0.0f, 0.0f, static_cast<float>(ctx_.swapchain_extent.width),
                        static_cast<float>(ctx_.swapchain_extent.height), 0.0f, 1.0f};
        VkRect2D full_scissor{{0, 0}, ctx_.swapchain_extent};
        vkCmdSetViewport(cmd, 0, 1, &full);
        vkCmdSetScissor(cmd, 0, 1, &full_scissor);
        overlay_hook_(cmd);
    }
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &image_available_[frame_];
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_finished_[image];
    r = vkQueueSubmit(ctx_.queue, 1, &si, in_flight_[frame_]);
    if (r != VK_SUCCESS) {
        AC04_LOG_ERROR("renderer", "vkQueueSubmit: %s", VkResultName(r));
        return r != VK_ERROR_DEVICE_LOST;
    }

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished_[image];
    pi.swapchainCount = 1;
    pi.pSwapchains = &ctx_.swapchain;
    pi.pImageIndices = &image;
    r = vkQueuePresentKHR(ctx_.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapchain_dirty_ = true;
    else if (r != VK_SUCCESS) AC04_LOG_ERROR("renderer", "vkQueuePresentKHR: %s", VkResultName(r));

    frame_ = (frame_ + 1) % kFramesInFlight;
    return true;
}

void Renderer::Shutdown() {
    if (!ctx_.device) return;
    vkDeviceWaitIdle(ctx_.device);
    DestroySwapchainResources();
    if (pipeline_) vkDestroyPipeline(ctx_.device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
    if (descriptor_pool_) vkDestroyDescriptorPool(ctx_.device, descriptor_pool_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(ctx_.device, set_layout_, nullptr);
    if (render_pass_) vkDestroyRenderPass(ctx_.device, render_pass_, nullptr);
    if (sampler_) vkDestroySampler(ctx_.device, sampler_, nullptr);
    if (native_view_) vkDestroyImageView(ctx_.device, native_view_, nullptr);
    if (native_image_) vmaDestroyImage(ctx_.allocator, native_image_, native_alloc_);
    for (int i = 0; i < kFramesInFlight; ++i) {
        if (image_available_[i]) vkDestroySemaphore(ctx_.device, image_available_[i], nullptr);
        if (in_flight_[i]) vkDestroyFence(ctx_.device, in_flight_[i], nullptr);
    }
    if (command_pool_) vkDestroyCommandPool(ctx_.device, command_pool_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    render_pass_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    native_view_ = VK_NULL_HANDLE;
    native_image_ = VK_NULL_HANDLE;
    command_pool_ = VK_NULL_HANDLE;
    ctx_.Shutdown();
}

}  // namespace ac04::render
