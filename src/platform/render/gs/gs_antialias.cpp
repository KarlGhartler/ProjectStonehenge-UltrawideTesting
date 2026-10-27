// Optional anti-aliasing of the GS backend (presentation.antialiasing; User request 2026-10-20,
// docs/port/port-features.md §2): FXAA 3.11 or SMAA 1x on a flight view's rectangle of the frame
// buffer, at the point where the view's 3D is complete and before its 2D overlay (HUD, radio window,
// menus, fade) is drawn, so the HUD stays sharp. Presentation only: the GS state, guest memory and the
// frame buffer's alpha are unchanged. The SMV movies are never anti-aliased (User 2026-10-20: movies
// get only the FSR 1 upscaling stage, ADR-006 §5): points come only from BuildFlightDrawLists (scene
// 2), and the movies play in scenes 3 and 4.
//
// Per point: the whole target is copied into aa_color_ (the passes sample it), the passes render
// into the working images with the view's rectangle as scissor (the viewport covers the whole image,
// so the SMAA metrics are those of the image), and the result's rectangle is copied back.
#include <vk_mem_alloc.h>

#include <AreaTex.h>
#include <SearchTex.h>

#include <algorithm>
#include <cstring>

#include "aa_vert_spv.h"
#include "fxaa_frag_spv.h"
#include "platform/log.h"
#include "platform/render/gs/gs_vulkan.h"
#include "smaa_blend_frag_spv.h"
#include "smaa_edges_frag_spv.h"
#include "smaa_weights_frag_spv.h"

namespace ac04::render::gs {
namespace {

enum Pass { kEdges = 0, kWeights = 1, kBlend = 2, kFxaa = 3 };

std::uint64_t Bits(std::uint64_t v, int lo, int n) { return (v >> lo) & ((1ull << n) - 1); }

VkRenderPass MakePass(VkDevice device, VkFormat format) {
    VkAttachmentDescription att{};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    VkRenderPass pass = VK_NULL_HANDLE;
    vkCreateRenderPass(device, &rpi, nullptr, &pass);
    return pass;
}

VkShaderModule MakeShader(VkDevice device, const std::uint32_t* code, std::size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, nullptr, &m);
    return m;
}

}  // namespace

bool GsBackend::InitAntiAlias(std::string& error) {
    VkDevice dev = ctx_->device;
    aa_pass_rg_ = MakePass(dev, VK_FORMAT_R8G8_UNORM);
    aa_pass_rgba_ = MakePass(dev, VK_FORMAT_R8G8B8A8_UNORM);

    VkDescriptorSetLayoutBinding b[3]{};
    for (int i = 0; i < 3; ++i) {
        b[i].binding = static_cast<uint32_t>(i);
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 3;
    dli.pBindings = b;
    vkCreateDescriptorSetLayout(dev, &dli, nullptr, &aa_set_layout_);
    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &aa_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(dev, &pli, nullptr, &aa_layout_);
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 4;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(dev, &dpi, nullptr, &aa_pool_);
    const VkDescriptorSetLayout layouts[4] = {aa_set_layout_, aa_set_layout_, aa_set_layout_, aa_set_layout_};
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = aa_pool_;
    dai.descriptorSetCount = 4;
    dai.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(dev, &dai, aa_sets_) != VK_SUCCESS) {
        error = "cannot allocate the anti-aliasing descriptor sets";
        return false;
    }

    // Pipelines: full-screen triangle, no vertex input, no blending, viewport and scissor dynamic.
    VkShaderModule vs = MakeShader(dev, aa_vert_spv, sizeof(aa_vert_spv));
    const std::pair<const std::uint32_t*, std::size_t> fs[4] = {{smaa_edges_frag_spv, sizeof(smaa_edges_frag_spv)},
                                                                {smaa_weights_frag_spv, sizeof(smaa_weights_frag_spv)},
                                                                {smaa_blend_frag_spv, sizeof(smaa_blend_frag_spv)},
                                                                {fxaa_frag_spv, sizeof(fxaa_frag_spv)}};
    bool ok = true;
    for (int p = 0; p < 4; ++p) {
        VkShaderModule f = MakeShader(dev, fs[p].first, fs[p].second);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, f, "main", nullptr};
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
        cba.colorWriteMask = p == kEdges ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT)
                                         : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);
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
        gpi.layout = aa_layout_;
        gpi.renderPass = p == kEdges ? aa_pass_rg_ : aa_pass_rgba_;
        if (vkCreateGraphicsPipelines(dev, ctx_->pipeline_cache, 1, &gpi, nullptr, &aa_pipelines_[p]) != VK_SUCCESS) ok = false;
        vkDestroyShaderModule(dev, f, nullptr);
    }
    vkDestroyShaderModule(dev, vs, nullptr);
    if (!ok) {
        error = "cannot create the anti-aliasing pipelines";
        return false;
    }

    // SMAA lookup textures (AreaTex.h: 160 x 560 RG8; SearchTex.h: 64 x 16 R8), uploaded once.
    if (!CreateImage(aa_area_, AREATEX_WIDTH, AREATEX_HEIGHT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_FORMAT_R8G8_UNORM) ||
        !CreateImage(aa_search_, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_FORMAT_R8_UNORM)) {
        error = "cannot create the SMAA lookup textures";
        return false;
    }
    Buffer staging;
    if (!EnsureBuffer(staging, sizeof(areaTexBytes) + sizeof(searchTexBytes), VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        error = "cannot create the SMAA staging buffer";
        return false;
    }
    std::memcpy(staging.mapped, areaTexBytes, sizeof(areaTexBytes));
    std::memcpy(static_cast<std::uint8_t*>(staging.mapped) + sizeof(areaTexBytes), searchTexBytes, sizeof(searchTexBytes));
    vmaFlushAllocation(ctx_->allocator, staging.alloc, 0, VK_WHOLE_SIZE);
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = ctx_->queue_family;
    vkCreateCommandPool(dev, &cpi, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev, &cai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {AREATEX_WIDTH, AREATEX_HEIGHT, 1};
    Transition(cmd, aa_area_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(cmd, staging.buffer, aa_area_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    c.bufferOffset = sizeof(areaTexBytes);
    c.imageExtent = {SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1};
    Transition(cmd, aa_search_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(cmd, staging.buffer, aa_search_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    Transition(cmd, aa_area_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, aa_search_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx_->queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_->queue);
    vkDestroyCommandPool(dev, pool, nullptr);
    DestroyBuffer(staging);
    aa_ready_ = true;
    return true;
}

void GsBackend::ShutdownAntiAlias() {
    VkDevice dev = ctx_->device;
    for (VkFramebuffer* fb : {&aa_fb_edges_, &aa_fb_weights_, &aa_fb_out_}) {
        if (*fb) vkDestroyFramebuffer(dev, *fb, nullptr);
        *fb = VK_NULL_HANDLE;
    }
    for (Image* img : {&aa_color_, &aa_edges_, &aa_weights_, &aa_out_, &aa_area_, &aa_search_}) DestroyImage(*img);
    for (VkPipeline& p : aa_pipelines_) {
        if (p) vkDestroyPipeline(dev, p, nullptr);
        p = VK_NULL_HANDLE;
    }
    if (aa_pool_) vkDestroyDescriptorPool(dev, aa_pool_, nullptr);
    if (aa_layout_) vkDestroyPipelineLayout(dev, aa_layout_, nullptr);
    if (aa_set_layout_) vkDestroyDescriptorSetLayout(dev, aa_set_layout_, nullptr);
    if (aa_pass_rg_) vkDestroyRenderPass(dev, aa_pass_rg_, nullptr);
    if (aa_pass_rgba_) vkDestroyRenderPass(dev, aa_pass_rgba_, nullptr);
    aa_pool_ = VK_NULL_HANDLE;
    aa_layout_ = VK_NULL_HANDLE;
    aa_set_layout_ = VK_NULL_HANDLE;
    aa_pass_rg_ = aa_pass_rgba_ = VK_NULL_HANDLE;
    aa_w_ = aa_h_ = 0;
    aa_ready_ = false;
}

bool GsBackend::EnsureAntiAliasImages(int w, int h) {
    if (w == aa_w_ && h == aa_h_ && aa_out_.image) return true;
    VkDevice dev = ctx_->device;
    vkDeviceWaitIdle(dev);  // the images may be in use by the frame in flight
    for (VkFramebuffer* fb : {&aa_fb_edges_, &aa_fb_weights_, &aa_fb_out_}) {
        if (*fb) vkDestroyFramebuffer(dev, *fb, nullptr);
        *fb = VK_NULL_HANDLE;
    }
    for (Image* img : {&aa_color_, &aa_edges_, &aa_weights_, &aa_out_}) DestroyImage(*img);
    aa_w_ = aa_h_ = 0;
    const VkImageUsageFlags att = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!CreateImage(aa_color_, w, h, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !CreateImage(aa_edges_, w, h, att, VK_FORMAT_R8G8_UNORM) || !CreateImage(aa_weights_, w, h, att) ||
        !CreateImage(aa_out_, w, h, att | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        LogOnce("aa-images", "cannot create the anti-aliasing images (pass skipped)");
        return false;
    }
    auto framebuffer = [&](VkRenderPass pass, const Image& img, VkFramebuffer& fb) {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &img.view;
        fci.width = static_cast<uint32_t>(w);
        fci.height = static_cast<uint32_t>(h);
        fci.layers = 1;
        vkCreateFramebuffer(dev, &fci, nullptr, &fb);
    };
    framebuffer(aa_pass_rg_, aa_edges_, aa_fb_edges_);
    framebuffer(aa_pass_rgba_, aa_weights_, aa_fb_weights_);
    framebuffer(aa_pass_rgba_, aa_out_, aa_fb_out_);
    // Descriptor sets (written only here, never while a recorded frame may use them).
    const VkDescriptorImageInfo color{linear_sampler_, aa_color_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo edges{linear_sampler_, aa_edges_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo weights{linear_sampler_, aa_weights_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo area{linear_sampler_, aa_area_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo search{sampler_, aa_search_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo per_set[4][3] = {{color, color, color},
                                                 {edges, area, search},
                                                 {color, weights, color},
                                                 {color, color, color}};
    VkWriteDescriptorSet wr[12]{};
    for (int s = 0; s < 4; ++s) {
        for (int i = 0; i < 3; ++i) {
            VkWriteDescriptorSet& x = wr[s * 3 + i];
            x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            x.dstSet = aa_sets_[s];
            x.dstBinding = static_cast<uint32_t>(i);
            x.descriptorCount = 1;
            x.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            x.pImageInfo = &per_set[s][i];
        }
    }
    vkUpdateDescriptorSets(dev, 12, wr, 0, nullptr);
    aa_w_ = w;
    aa_h_ = h;
    return true;
}

void GsBackend::ApplyAntiAlias(VkCommandBuffer cmd, Target& t, const DrawCommand& d) {
    const int w = t.img.width, h = t.img.height, s = t.scale;
    // The view's rectangle (native pixels, inclusive) at the target's scale.
    const int x0 = std::clamp(static_cast<int>(Bits(d.scissor, 0, 11)) * s, 0, w);
    const int x1 = std::clamp((static_cast<int>(Bits(d.scissor, 16, 11)) + 1) * s, 0, w);
    const int y0 = std::clamp(static_cast<int>(Bits(d.scissor, 32, 11)) * s, 0, h);
    const int y1 = std::clamp((static_cast<int>(Bits(d.scissor, 48, 11)) + 1) * s, 0, h);
    if (x1 <= x0 || y1 <= y0 || !EnsureAntiAliasImages(w, h)) return;
    // Source: a copy of the whole target (the passes sample around the rectangle's edges too).
    VkImageCopy c{};
    c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.dstSubresource = c.srcSubresource;
    c.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    Transitions(cmd, {{&t.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}, {&aa_color_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}});
    vkCmdCopyImage(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aa_color_.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    CountCopy(kCopyAntiAlias, c.extent.width, c.extent.height);
    Transition(cmd, aa_color_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const float metrics[4] = {1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h), static_cast<float>(w),
                              static_cast<float>(h)};
    const VkRect2D rect{{x0, y0}, {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}};
    auto pass = [&](VkRenderPass rp, VkFramebuffer fb, Image& img, int p) {
        Transition(cmd, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkClearValue clear{};
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = rp;
        rbi.framebuffer = fb;
        rbi.renderArea.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        rbi.clearValueCount = 1;
        rbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        ++pass_stats_.render_passes;
        const VkViewport vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &rect);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, aa_pipelines_[p]);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, aa_layout_, 0, 1, &aa_sets_[p], 0, nullptr);
        vkCmdPushConstants(cmd, aa_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(metrics), metrics);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    };
    if (aa_mode_ == AntiAlias::kSmaa) {
        pass(aa_pass_rg_, aa_fb_edges_, aa_edges_, kEdges);
        Transition(cmd, aa_edges_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pass(aa_pass_rgba_, aa_fb_weights_, aa_weights_, kWeights);
        Transition(cmd, aa_weights_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pass(aa_pass_rgba_, aa_fb_out_, aa_out_, kBlend);
    } else {
        pass(aa_pass_rgba_, aa_fb_out_, aa_out_, kFxaa);
    }
    // The rectangle back into the target.
    c.srcOffset = {x0, y0, 0};
    c.dstOffset = c.srcOffset;
    c.extent = {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0), 1};
    Transitions(cmd, {{&aa_out_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}, {&t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}});
    vkCmdCopyImage(cmd, aa_out_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.img.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    CountCopy(kCopyAntiAlias, c.extent.width, c.extent.height);
    Transition(cmd, t.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    t.last_write = ++write_clock_;
}

}  // namespace ac04::render::gs
