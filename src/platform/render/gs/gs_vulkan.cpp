#include "platform/render/gs/gs_vulkan.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

#include "gs_frag_spv.h"
#include "gs_vert_spv.h"
#include "platform/log.h"

namespace ac04::render::gs {
namespace {

constexpr int kAtlasSize = 4096;
constexpr int kTargetHeight = 1024;
constexpr int kFeedbackWidth = 2048;

struct Vertex {
    float x, y, z;  // z: GS Z / 2^bits of the Z buffer format (the depth image value)
    std::uint8_t r, g, b, a;
    float s, t, q;
};
static_assert(sizeof(Vertex) == 28, "vertex layout");

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// GS Z of a vertex as the depth image value: clamped to the range of the Z format (ZBUF.PSM: Z32,
// Z24, Z16/Z16S) and divided by its size. D32 holds Z24 and Z16 exactly; Z32 loses the low bits.
float DepthValue(std::uint32_t z, std::uint64_t zbuf) {
    const std::uint32_t psm = static_cast<std::uint32_t>((zbuf >> 24) & 0xF);
    if (psm == 0) return static_cast<float>(static_cast<double>(z) / 4294967296.0);
    const std::uint32_t bits = psm == 1 ? 24 : 16;
    const std::uint32_t max = (1u << bits) - 1;
    return static_cast<float>(static_cast<double>(std::min(z, max)) / static_cast<double>(1u << bits));
}

// GS depth test (TEST.ZTE, ZTST: never, always, >=, >) and depth write (ZBUF.ZMSK) as pipeline key
// bits.
std::uint64_t DepthKey(std::uint64_t test, std::uint64_t zbuf) {
    std::uint64_t op = VK_COMPARE_OP_ALWAYS;
    if (((test >> 16) & 1) != 0) {
        switch ((test >> 17) & 3) {
            case 0: op = VK_COMPARE_OP_NEVER; break;
            case 1: op = VK_COMPARE_OP_ALWAYS; break;
            case 2: op = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
            default: op = VK_COMPARE_OP_GREATER; break;
        }
    }
    const std::uint64_t write = ((zbuf >> 32) & 1) == 0 ? 1 : 0;
    return op << 24 | write << 27;
}

// Push constants (std430 layout of gs.frag's Params block).
struct Params {
    float target_size[2];
    float pad0[2];
    std::int32_t rect[4];
    std::int32_t origin[2];
    std::int32_t tex_size[2];
    std::int32_t region[4];
    std::int32_t flags;
    std::int32_t aref;
    float fix;
};
static_assert(sizeof(Params) == 76, "push constant layout");

std::uint64_t Bits(std::uint64_t v, int lo, int n) { return (v >> lo) & ((1ull << n) - 1); }

// Blend state for the GS equation (A - B) * C + D, A/B/D in {Cs, Cd, 0}, C in {As, Ad, FIX}.
struct BlendState {
    bool enable = false;
    VkBlendFactor src = VK_BLEND_FACTOR_ONE, dst = VK_BLEND_FACTOR_ZERO;
    VkBlendOp op = VK_BLEND_OP_ADD;
    bool supported = true;
    bool src_over_128 = false;  // the shader outputs Cs / 128 (C = Ad: see MapBlend)
};

BlendState MapBlend(std::uint64_t alpha) {
    const std::uint32_t a = static_cast<std::uint32_t>(Bits(alpha, 0, 2)), b = static_cast<std::uint32_t>(Bits(alpha, 2, 2));
    const std::uint32_t c = static_cast<std::uint32_t>(Bits(alpha, 4, 2));
    const std::uint32_t d = static_cast<std::uint32_t>(Bits(alpha, 6, 2));
    BlendState s;
    s.enable = true;
    if (a == b) {
        if (d == 0) s.enable = false;
        else s.src = VK_BLEND_FACTOR_ZERO, s.dst = d == 1 ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
        return s;
    }
    const std::uint32_t k = a << 4 | b << 2 | d;
    if (c == 1) {
        // C = Ad. Targets store the GS alpha byte as a / 255, so DST_ALPHA is Ad / 255 instead of
        // Ad / 128. Cs * Ad + Cd (the menu filters' ALPHA 0x58) is exact when the shader outputs
        // Cs / 128 instead of Cs / 255, as long as Cs <= 128 (UNORM outputs clamp to 1; the menu
        // filter colours stay below 0x80). Other equations with Ad are not mapped.
        if (k == 0x09) {
            s.src = VK_BLEND_FACTOR_DST_ALPHA;
            s.dst = VK_BLEND_FACTOR_ONE;
            s.src_over_128 = true;
            return s;
        }
        s.supported = false;
        s.src = VK_BLEND_FACTOR_DST_ALPHA;
        s.dst = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        return s;
    }
    switch (k) {
        case 0x05: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; break;  // (Cs-Cd)C+Cd
        case 0x09: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ONE; break;                   // Cs*C+Cd
        case 0x0A: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ZERO; break;                  // Cs*C
        case 0x21: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ONE; s.op = VK_BLEND_OP_REVERSE_SUBTRACT; break;  // Cd-Cs*C
        case 0x10: s.src = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; break;  // (Cd-Cs)C+Cs
        case 0x18: s.src = VK_BLEND_FACTOR_ONE; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; break;                   // Cd*C+Cs
        case 0x25: s.src = VK_BLEND_FACTOR_ZERO; s.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; break;        // Cd-Cd*C
        default:
            s.supported = false;
            s.src = VK_BLEND_FACTOR_SRC1_COLOR;
            s.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            break;
    }
    return s;
}

std::uint64_t PipelineKey(const BlendState& b, std::uint32_t write_mask) {
    return static_cast<std::uint64_t>(b.enable) | static_cast<std::uint64_t>(b.src) << 1 |
           static_cast<std::uint64_t>(b.dst) << 8 | static_cast<std::uint64_t>(b.op) << 15 |
           static_cast<std::uint64_t>(write_mask) << 20;
}

}  // namespace

GsBackend::~GsBackend() { Shutdown(); }

void GsBackend::LogOnce(const char* key, const char* text) {
    const std::string k = std::string("gsvk:") + key;
    log::WriteOnce(k.c_str(), log::Level::Warning, "renderer", "%s", text);
}

bool GsBackend::CreateImage(Image& img, int w, int h, VkImageUsageFlags usage, VkFormat format) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(ctx_->allocator, &ici, &aci, &img.image, &img.alloc, nullptr) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format;
    img.aspect = format == kDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange = {img.aspect, 0, 1, 0, 1};
    vkCreateImageView(ctx_->device, &vci, nullptr, &img.view);
    img.width = w;
    img.height = h;
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void GsBackend::DestroyImage(Image& img) {
    if (img.view) vkDestroyImageView(ctx_->device, img.view, nullptr);
    if (img.image) vmaDestroyImage(ctx_->allocator, img.image, img.alloc);
    img = Image{};
}

bool GsBackend::EnsureBuffer(Buffer& b, std::size_t size, VkBufferUsageFlags usage) {
    if (b.buffer && b.size >= size) return true;
    DestroyBuffer(b);
    std::size_t n = 1 << 16;
    while (n < size) n <<= 1;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = n;
    bci.usage = usage;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(ctx_->allocator, &bci, &aci, &b.buffer, &b.alloc, &info) != VK_SUCCESS) return false;
    b.mapped = info.pMappedData;
    b.size = n;
    return true;
}

void GsBackend::DestroyBuffer(Buffer& b) {
    if (b.buffer) vmaDestroyBuffer(ctx_->allocator, b.buffer, b.alloc);
    b = Buffer{};
}

void GsBackend::Transition(VkCommandBuffer cmd, Image& img, VkImageLayout layout) {
    if (img.layout == layout) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = {img.aspect, 0, 1, 0, 1};
    b.oldLayout = img.layout;
    b.newLayout = layout;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &b);
    img.layout = layout;
}

bool GsBackend::Init(VkContext& ctx, std::string& error) {
    ctx_ = &ctx;
    if (!ctx.dual_src_blend) {
        error = "the GPU does not support dual-source blending (needed for the GS blend equation)";
        return false;
    }
    VkAttachmentDescription att[2]{};
    att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[1] = att[0];
    att[1].format = kDepthFormat;
    att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    sub.pDepthStencilAttachment = &dref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 2;
    rpi.pAttachments = att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    vkCreateRenderPass(ctx.device, &rpi, nullptr, &pass_);

    VkDescriptorSetLayoutBinding bindings[2]{};
    for (int i = 0; i < 2; ++i) {
        bindings[i].binding = static_cast<uint32_t>(i);
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 2;
    dli.pBindings = bindings;
    vkCreateDescriptorSetLayout(ctx.device, &dli, nullptr, &set_layout_);
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Params)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx.device, &pli, nullptr, &layout_);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device, &sci, nullptr, &sampler_);

    const VkImageUsageFlags tex_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!CreateImage(atlas_, kAtlasSize, kAtlasSize, tex_usage) ||
        !CreateImage(feedback_, kFeedbackWidth, kTargetHeight, tex_usage) ||
        !CreateImage(present_, kFeedbackWidth, kTargetHeight, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        error = "cannot create the GS renderer images";
        return false;
    }
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(ctx.device, &dpi, nullptr, &pool_);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    vkAllocateDescriptorSets(ctx.device, &dai, &set_);
    VkDescriptorImageInfo infos[2] = {{sampler_, atlas_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                      {sampler_, feedback_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet w[2]{};
    for (int i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set_;
        w[i].dstBinding = static_cast<uint32_t>(i);
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(ctx.device, 2, w, 0, nullptr);

    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(gs_vert_spv);
    smi.pCode = gs_vert_spv;
    vkCreateShaderModule(ctx.device, &smi, nullptr, &vs_);
    smi.codeSize = sizeof(gs_frag_spv);
    smi.pCode = gs_frag_spv;
    vkCreateShaderModule(ctx.device, &smi, nullptr, &fs_);

    // Initial layouts: images read by the shader start out cleared and readable.
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = ctx.queue_family;
    vkCreateCommandPool(ctx.device, &cpi, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx.device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Image* img : {&atlas_, &feedback_, &present_}) {
        Transition(cmd, *img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(cmd, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    }
    Transition(cmd, atlas_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, present_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.queue);
    vkDestroyCommandPool(ctx.device, pool, nullptr);
    return true;
}

void GsBackend::Shutdown() {
    if (!ctx_ || !ctx_->device) return;
    vkDeviceWaitIdle(ctx_->device);
    for (auto& [key, p] : pipelines_) vkDestroyPipeline(ctx_->device, p, nullptr);
    pipelines_.clear();
    for (auto& [key, t] : targets_) {
        if (t.fb) vkDestroyFramebuffer(ctx_->device, t.fb, nullptr);
        DestroyImage(t.img);
        DestroyImage(t.depth);
    }
    targets_.clear();
    DestroyImage(atlas_);
    DestroyImage(feedback_);
    DestroyImage(present_);
    for (int i = 0; i < kFrames; ++i) {
        DestroyBuffer(staging_[i]);
        DestroyBuffer(vertices_[i]);
    }
    if (vs_) vkDestroyShaderModule(ctx_->device, vs_, nullptr);
    if (fs_) vkDestroyShaderModule(ctx_->device, fs_, nullptr);
    if (sampler_) vkDestroySampler(ctx_->device, sampler_, nullptr);
    if (pool_) vkDestroyDescriptorPool(ctx_->device, pool_, nullptr);
    if (layout_) vkDestroyPipelineLayout(ctx_->device, layout_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(ctx_->device, set_layout_, nullptr);
    if (pass_) vkDestroyRenderPass(ctx_->device, pass_, nullptr);
    vs_ = fs_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    pass_ = VK_NULL_HANDLE;
    ctx_ = nullptr;
}

bool GsBackend::IsFrameBuffer(std::uint32_t tbp) const {
    for (const auto& [key, t] : targets_) {
        if (t.fbp * 32 == tbp) return true;
    }
    return false;
}

GsBackend::Target* GsBackend::GetTarget(VkCommandBuffer cmd, std::uint32_t fbp, std::uint32_t fbw) {
    const std::uint64_t key = static_cast<std::uint64_t>(fbp) << 8 | fbw;
    auto it = targets_.find(key);
    if (it != targets_.end()) return &it->second;
    Target t;
    t.fbp = fbp;
    t.fbw = fbw;
    const int w = std::max<int>(64, static_cast<int>(fbw) * 64);
    if (!CreateImage(t.img, w, kTargetHeight,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        LogOnce("target", "cannot create a render target");
        return nullptr;
    }
    if (!CreateImage(t.depth, w, kTargetHeight, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     kDepthFormat)) {
        LogOnce("target", "cannot create a render target");
        DestroyImage(t.img);
        return nullptr;
    }
    const VkImageView views[2] = {t.img.view, t.depth.view};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = pass_;
    fci.attachmentCount = 2;
    fci.pAttachments = views;
    fci.width = static_cast<uint32_t>(w);
    fci.height = kTargetHeight;
    fci.layers = 1;
    vkCreateFramebuffer(ctx_->device, &fci, nullptr, &t.fb);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Transition(cmd, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdClearColorImage(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    Transition(cmd, t.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkClearDepthStencilValue zero_depth{};
    VkImageSubresourceRange drange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    Transition(cmd, t.depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdClearDepthStencilImage(cmd, t.depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero_depth, 1, &drange);
    Transition(cmd, t.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    return &targets_.emplace(key, std::move(t)).first->second;
}

VkPipeline GsBackend::GetPipeline(std::uint64_t key) {
    if (auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs_, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[3] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                               {1, 0, VK_FORMAT_R8G8B8A8_UINT, 12},
                                               {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 16}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = va;
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
    cba.blendEnable = (key & 1) ? VK_TRUE : VK_FALSE;
    cba.srcColorBlendFactor = static_cast<VkBlendFactor>((key >> 1) & 0x7F);
    cba.dstColorBlendFactor = static_cast<VkBlendFactor>((key >> 8) & 0x7F);
    cba.colorBlendOp = static_cast<VkBlendOp>((key >> 15) & 0x1F);
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // the GS writes As unblended
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = static_cast<VkColorComponentFlags>((key >> 20) & 0xF);
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;
    dss.depthCompareOp = static_cast<VkCompareOp>((key >> 24) & 7);
    dss.depthWriteEnable = ((key >> 27) & 1) != 0 ? VK_TRUE : VK_FALSE;
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
    gpi.pDepthStencilState = &dss;
    gpi.pDynamicState = &ds;
    gpi.layout = layout_;
    gpi.renderPass = pass_;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx_->device, VK_NULL_HANDLE, 1, &gpi, nullptr, &p) != VK_SUCCESS) {
        LogOnce("pipeline", "cannot create a GS pipeline");
    }
    pipelines_[key] = p;
    return p;
}

void GsBackend::Record(VkCommandBuffer cmd, int frame, const std::vector<DrawCommand>& draws) {
    if (draws.empty()) return;
    // 1. Textures into the atlas (shelf packing), uploaded through this frame's staging buffer.
    std::unordered_map<const DecodedTexture*, std::pair<int, int>> placed;
    std::size_t staging_bytes = 0;
    int shelf_x = 0, shelf_y = 0, shelf_h = 0;
    std::vector<VkBufferImageCopy> copies;
    std::vector<std::pair<const DecodedTexture*, std::size_t>> uploads;
    for (const DrawCommand& d : draws) {
        const DecodedTexture* t = d.texture.get();
        if (!t || placed.count(t) || t->width == 0 || t->height == 0) continue;
        const int w = static_cast<int>(t->width), h = static_cast<int>(t->height);
        if (w > kAtlasSize || h > kAtlasSize) {
            LogOnce("atlas-big", "texture larger than the atlas (skipped)");
            continue;
        }
        if (shelf_x + w > kAtlasSize) {
            shelf_x = 0;
            shelf_y += shelf_h;
            shelf_h = 0;
        }
        if (shelf_y + h > kAtlasSize) {
            LogOnce("atlas-full", "texture atlas full for this frame (draws skipped)");
            continue;
        }
        placed[t] = {shelf_x, shelf_y};
        VkBufferImageCopy c{};
        c.bufferOffset = staging_bytes;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageOffset = {shelf_x, shelf_y, 0};
        c.imageExtent = {t->width, t->height, 1};
        copies.push_back(c);
        uploads.emplace_back(t, staging_bytes);
        staging_bytes += t->rgba.size();
        shelf_x += w;
        shelf_h = std::max(shelf_h, h);
    }
    if (!copies.empty()) {
        if (!EnsureBuffer(staging_[frame], staging_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return;
        for (const auto& [t, off] : uploads) std::memcpy(static_cast<std::uint8_t*>(staging_[frame].mapped) + off, t->rgba.data(), t->rgba.size());
        vmaFlushAllocation(ctx_->allocator, staging_[frame].alloc, 0, VK_WHOLE_SIZE);
        Transition(cmd, atlas_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd, staging_[frame].buffer, atlas_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(copies.size()), copies.data());
        Transition(cmd, atlas_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // 2. Vertices (sprites, lines and points become two triangles).
    std::vector<Vertex> verts;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;  // first, count per draw
    for (const DrawCommand& d : draws) {
        const std::size_t first = verts.size();
        // Sprites, lines and points take the Z of the vertex that gives their colour (`g`).
        auto vtx = [&](const GsVertex& g, float x, float y, float s, float t, float q) {
            verts.push_back(Vertex{x, y, DepthValue(g.z, d.zbuf), g.r, g.g, g.b, g.a, s, t, q});
        };
        auto tex = [&](const GsVertex& g, float& s, float& t, float& q) {
            if (d.fst) s = g.u, t = g.v, q = 1.0f;
            else s = g.s, t = g.t, q = g.q;
        };
        const std::size_t n = d.vertices.size();
        if (d.kind == PrimKind::kTriangle) {
            for (const GsVertex& g : d.vertices) {
                float s, t, q;
                tex(g, s, t, q);
                vtx(g, g.x, g.y, s, t, q);
            }
        } else if (d.kind == PrimKind::kSprite) {
            for (std::size_t i = 0; i + 1 < n; i += 2) {
                const GsVertex& a = d.vertices[i];
                const GsVertex& b = d.vertices[i + 1];
                float s0, t0, q0, s1, t1, q1;
                tex(a, s0, t0, q0);
                tex(b, s1, t1, q1);
                vtx(b, a.x, a.y, s0, t0, q1);
                vtx(b, b.x, a.y, s1, t0, q1);
                vtx(b, a.x, b.y, s0, t1, q1);
                vtx(b, b.x, a.y, s1, t0, q1);
                vtx(b, b.x, b.y, s1, t1, q1);
                vtx(b, a.x, b.y, s0, t1, q1);
            }
        } else if (d.kind == PrimKind::kLine) {
            for (std::size_t i = 0; i + 1 < n; i += 2) {
                const GsVertex& a = d.vertices[i];
                const GsVertex& b = d.vertices[i + 1];
                float dx = b.x - a.x, dy = b.y - a.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len <= 0.0f) continue;
                dx /= len;
                dy /= len;
                const float nx = -dy * 0.5f, ny = dx * 0.5f;
                float s0, t0, q0, s1, t1, q1;
                tex(a, s0, t0, q0);
                tex(b, s1, t1, q1);
                vtx(a, a.x + nx, a.y + ny, s0, t0, q0);
                vtx(b, b.x + nx, b.y + ny, s1, t1, q1);
                vtx(a, a.x - nx, a.y - ny, s0, t0, q0);
                vtx(b, b.x + nx, b.y + ny, s1, t1, q1);
                vtx(b, b.x - nx, b.y - ny, s1, t1, q1);
                vtx(a, a.x - nx, a.y - ny, s0, t0, q0);
            }
        } else {
            for (const GsVertex& g : d.vertices) {
                float s, t, q;
                tex(g, s, t, q);
                vtx(g, g.x, g.y, s, t, q);
                vtx(g, g.x + 1, g.y, s, t, q);
                vtx(g, g.x, g.y + 1, s, t, q);
                vtx(g, g.x + 1, g.y, s, t, q);
                vtx(g, g.x + 1, g.y + 1, s, t, q);
                vtx(g, g.x, g.y + 1, s, t, q);
            }
        }
        ranges.emplace_back(first, verts.size() - first);
    }
    if (verts.empty()) return;
    if (!EnsureBuffer(vertices_[frame], verts.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return;
    std::memcpy(vertices_[frame].mapped, verts.data(), verts.size() * sizeof(Vertex));
    vmaFlushAllocation(ctx_->allocator, vertices_[frame].alloc, 0, VK_WHOLE_SIZE);

    // 3. Draws, grouped by render target (targets are created first: not inside a render pass).
    // Untextured sprites into a known Z buffer are depth fills (no colour target of their own).
    auto depth_fill = [&](const DrawCommand& d) {
        return d.kind == PrimKind::kSprite && !d.textured && zbuf_owner_.count(d.Fbp()) != 0;
    };
    for (const DrawCommand& d : draws) {
        if (!depth_fill(d)) GetTarget(cmd, d.Fbp(), d.Fbw());
    }
    Target* open = nullptr;
    // Dynamic state already set in this render pass: draws that repeat it skip the call (the hangar
    // records ~4,000 draws per frame, and every call is costly under the validation layer).
    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    VkRect2D bound_scissor{{-1, -1}, {0, 0}};
    Params bound_params{};
    bool params_valid = false;
    auto end_pass = [&]() {
        if (open) vkCmdEndRenderPass(cmd);
        open = nullptr;
    };
    auto begin_pass = [&](Target* target) {
        if (open == target) return;
        end_pass();
        Transition(cmd, target->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = pass_;
        rbi.framebuffer = target->fb;
        rbi.renderArea.extent = {static_cast<uint32_t>(target->img.width), kTargetHeight};
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        open = target;
        bound_pipeline = VK_NULL_HANDLE;
        bound_scissor = VkRect2D{{-1, -1}, {0, 0}};
        params_valid = false;
        VkViewport vpt{0.0f, 0.0f, static_cast<float>(target->img.width), static_cast<float>(kTargetHeight), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vpt);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertices_[frame].buffer, &offset);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
    };
    present_valid_ = false;
    for (std::size_t i = 0; i < draws.size(); ++i) {
        const DrawCommand& d = draws[i];
        if (ranges[i].second == 0) continue;
        if (depth_fill(d)) {
            // The sprites' colour (RGB = Z bits 0-23, A = bits 24-31) is the Z written into the
            // owner's depth image over their rectangles (clipped to the scissor).
            const auto& [owner_key, owner_zbuf] = zbuf_owner_[d.Fbp()];
            auto it = targets_.find(owner_key);
            if (it == targets_.end()) continue;
            Target* owner = &it->second;
            begin_pass(owner);
            const int x0 = static_cast<int>(Bits(d.scissor, 0, 11)), x1 = static_cast<int>(Bits(d.scissor, 16, 11)) + 1;
            const int y0 = static_cast<int>(Bits(d.scissor, 32, 11)), y1 = static_cast<int>(Bits(d.scissor, 48, 11)) + 1;
            for (std::size_t v = 0; v + 1 < d.vertices.size(); v += 2) {
                const GsVertex& a = d.vertices[v];
                const GsVertex& b = d.vertices[v + 1];
                const int rx0 = std::clamp(static_cast<int>(std::ceil(std::min(a.x, b.x))), std::max(x0, 0), std::min(x1, owner->img.width));
                const int rx1 = std::clamp(static_cast<int>(std::ceil(std::max(a.x, b.x))), std::max(x0, 0), std::min(x1, owner->img.width));
                const int ry0 = std::clamp(static_cast<int>(std::ceil(std::min(a.y, b.y))), std::max(y0, 0), std::min(y1, kTargetHeight));
                const int ry1 = std::clamp(static_cast<int>(std::ceil(std::max(a.y, b.y))), std::max(y0, 0), std::min(y1, kTargetHeight));
                if (rx1 <= rx0 || ry1 <= ry0) continue;
                const std::uint32_t z = b.r | b.g << 8 | b.b << 16 | static_cast<std::uint32_t>(b.a) << 24;
                VkClearAttachment ca{VK_IMAGE_ASPECT_DEPTH_BIT, 0, {}};
                ca.clearValue.depthStencil.depth = DepthValue(z, owner_zbuf);
                VkClearRect cr{{{rx0, ry0}, {static_cast<uint32_t>(rx1 - rx0), static_cast<uint32_t>(ry1 - ry0)}}, 0, 1};
                vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
            }
            continue;
        }
        if (d.Fpsm() != kPsmCt32 && d.Fpsm() != kPsmCt24) LogOnce("fpsm", "frame buffer format other than CT32/CT24 drawn as RGBA8");
        if (Bits(d.test, 14, 1)) LogOnce("date", "destination alpha test not implemented yet");
        Target* target = nullptr;
        if (d.texture_from_frame >= 0) {
            // Frame buffer read as a texture (field copy, heat haze): copy it first. The first copy of
            // the draw buffer in a frame is also the image to present (ADR-006 §4).
            end_pass();
            // Several targets can share a block pointer (the menus' 640-wide and the flight's 512-wide
            // buffers both sit at FBP 0): prefer the one whose width matches TEX0.TBW.
            Target* src = nullptr;
            const std::uint32_t tbw = static_cast<std::uint32_t>(Bits(d.tex0, 14, 6));
            for (auto& [key, t] : targets_) {
                if (t.fbp * 32 != static_cast<std::uint32_t>(d.texture_from_frame)) continue;
                if (src == nullptr || t.fbw == tbw) src = &t;
            }
            if (src) {
                VkImageCopy c{};
                c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                c.dstSubresource = c.srcSubresource;
                c.extent = {static_cast<uint32_t>(std::min(src->img.width, kFeedbackWidth)), kTargetHeight, 1};
                Transition(cmd, src->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                Transition(cmd, feedback_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                vkCmdCopyImage(cmd, src->img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, feedback_.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
                const bool field_copy = dispfb_ == 0 || static_cast<std::uint32_t>(dispfb_ & 0x1FF) == d.Fbp();
                if (!present_valid_ && field_copy) {
                    Transition(cmd, present_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    vkCmdCopyImage(cmd, src->img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, present_.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
                    Transition(cmd, present_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                    present_valid_ = true;
                    present_fbp_ = src->fbp;
                    present_w_ = src->img.width;
                    present_h_ = std::max(src->used_height, 1);
                    // Field copy (DrawDisplayTransfer): MODULATE by the vertex colour LerpRgba(0, 0x808080,
                    // brightness), so the brightness option scales the displayed image.
                    const bool modulate = d.textured && Bits(d.tex0, 35, 2) == 0 && !d.vertices.empty();
                    present_scale_[0] = modulate ? d.vertices[0].r / 128.0f : 1.0f;
                    present_scale_[1] = modulate ? d.vertices[0].g / 128.0f : 1.0f;
                    present_scale_[2] = modulate ? d.vertices[0].b / 128.0f : 1.0f;
                }
                Transition(cmd, feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                Transition(cmd, src->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }
        target = GetTarget(cmd, d.Fbp(), d.Fbw());
        if (!target) continue;
        begin_pass(target);
        // Only Z buffers that are really tested (GEQUAL/GREATER) are owned: menus keep ZTE with
        // ZTST ALWAYS and their ZBP can alias other buffers.
        if (Bits(d.test, 16, 1) && Bits(d.test, 18, 1))
            zbuf_owner_[static_cast<std::uint32_t>(Bits(d.zbuf, 0, 9))] = {static_cast<std::uint64_t>(d.Fbp()) << 8 | d.Fbw(), d.zbuf};
        // Scissor (inclusive) and the height of the target that has been drawn.
        const int x0 = static_cast<int>(Bits(d.scissor, 0, 11)), x1 = static_cast<int>(Bits(d.scissor, 16, 11));
        const int y0 = static_cast<int>(Bits(d.scissor, 32, 11)), y1 = static_cast<int>(Bits(d.scissor, 48, 11));
        const int sx0 = std::clamp(x0, 0, target->img.width), sx1 = std::clamp(x1 + 1, 0, target->img.width);
        const int sy0 = std::clamp(y0, 0, kTargetHeight), sy1 = std::clamp(y1 + 1, 0, kTargetHeight);
        if (sx1 <= sx0 || sy1 <= sy0) continue;
        target->used_height = std::max(target->used_height, sy1);
        VkRect2D sc{{sx0, sy0}, {static_cast<uint32_t>(sx1 - sx0), static_cast<uint32_t>(sy1 - sy0)}};
        if (std::memcmp(&sc, &bound_scissor, sizeof(sc)) != 0) {
            vkCmdSetScissor(cmd, 0, 1, &sc);
            bound_scissor = sc;
        }

        // Blend and write mask.
        BlendState blend;
        if (d.blend) {
            blend = MapBlend(d.alpha);
            if (!blend.supported) LogOnce("blend", "GS blend equation not mapped (normal alpha blend used)");
            if (d.pabe) LogOnce("pabe", "PABE not implemented");
            if (!d.colclamp) LogOnce("colclamp", "COLCLAMP 0 (wrap) not implemented");
        }
        const std::uint32_t m = d.Fbmsk();
        std::uint32_t write = 0;
        for (int c = 0; c < 4; ++c) {
            const std::uint32_t bits = (m >> (c * 8)) & 0xFF;
            if (bits != 0xFF) write |= 1u << c;
            if (bits != 0 && bits != 0xFF) LogOnce("fbmsk", "partial FBMSK channel mask not implemented");
        }
        Params p{};
        p.target_size[0] = static_cast<float>(target->img.width);
        p.target_size[1] = static_cast<float>(kTargetHeight);
        std::int32_t flags = 0;
        if (d.textured) {
            flags |= 1;
            if (d.fst) flags |= 2;
            flags |= static_cast<std::int32_t>(Bits(d.tex0, 35, 2)) << 2;
            if (Bits(d.tex0, 34, 1)) flags |= 1 << 4;
            if (Bits(d.tex1, 5, 1)) flags |= 1 << 5;  // MMAG linear
            flags |= static_cast<std::int32_t>(Bits(d.clamp, 0, 2)) << 12;
            flags |= static_cast<std::int32_t>(Bits(d.clamp, 2, 2)) << 14;
            p.tex_size[0] = 1 << std::min<int>(static_cast<int>(Bits(d.tex0, 26, 4)), 10);
            p.tex_size[1] = 1 << std::min<int>(static_cast<int>(Bits(d.tex0, 30, 4)), 10);
            p.region[0] = static_cast<std::int32_t>(Bits(d.clamp, 4, 10));
            p.region[1] = static_cast<std::int32_t>(Bits(d.clamp, 14, 10));
            p.region[2] = static_cast<std::int32_t>(Bits(d.clamp, 24, 10));
            p.region[3] = static_cast<std::int32_t>(Bits(d.clamp, 34, 10));
            if (d.texture_from_frame >= 0) {
                flags |= 1 << 16;
            } else if (d.texture) {
                auto it = placed.find(d.texture.get());
                if (it == placed.end()) continue;
                p.rect[0] = it->second.first;
                p.rect[1] = it->second.second;
                p.rect[2] = static_cast<std::int32_t>(d.texture->width);
                p.rect[3] = static_cast<std::int32_t>(d.texture->height);
                p.origin[0] = static_cast<std::int32_t>(d.texture->origin_u);
                p.origin[1] = static_cast<std::int32_t>(d.texture->origin_v);
            } else {
                continue;
            }
        }
        // Alpha test with its fail mode (TEST.AFAIL): 0 KEEP and 2 ZB_ONLY write no colour (the depth
        // write of ZB_ONLY is not done), 1 FB_ONLY writes the colour, 3 RGB_ONLY writes RGB but not
        // A: a second draw of the failing pixels with alpha masked.
        std::uint32_t afail = 0;
        if (Bits(d.test, 0, 1)) {
            flags |= 1 << 6;
            flags |= static_cast<std::int32_t>(Bits(d.test, 1, 3)) << 7;
            p.aref = static_cast<std::int32_t>(Bits(d.test, 4, 8));
            afail = static_cast<std::uint32_t>(Bits(d.test, 12, 2));
            if (afail == 1) flags &= ~(1 << 6);
        }
        if (Bits(d.alpha, 4, 2) == 2) flags |= 1 << 17;
        if (d.blend && blend.src_over_128) flags |= 1 << 18;
        p.fix = static_cast<float>(Bits(d.alpha, 32, 8));
        p.flags = flags;
        const std::uint64_t depth = DepthKey(d.test, d.zbuf);
        auto bind = [&](VkPipeline pl) {
            if (pl != bound_pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl);
                bound_pipeline = pl;
            }
            if (!params_valid || std::memcmp(&p, &bound_params, sizeof(p)) != 0) {
                vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(p), &p);
                bound_params = p;
                params_valid = true;
            }
        };
        bind(GetPipeline(PipelineKey(blend, write) | depth));
        vkCmdDraw(cmd, static_cast<uint32_t>(ranges[i].second), 1, static_cast<uint32_t>(ranges[i].first), 0);
        if ((flags & (1 << 6)) != 0 && afail == 3 && (write & 7) != 0) {
            p.flags = flags | 1 << 19;  // keep only the pixels that fail the test
            bind(GetPipeline(PipelineKey(blend, write & 7) | depth));
            vkCmdDraw(cmd, static_cast<uint32_t>(ranges[i].second), 1, static_cast<uint32_t>(ranges[i].first), 0);
        }
    }
    end_pass();
}

void GsBackend::PresentSize(int& w, int& h) const {
    if (present_valid_) {
        w = present_w_;
        h = present_h_;
        return;
    }
    // No field copy this frame: the draw buffer at block 0.
    for (const auto& [key, t] : targets_) {
        if (t.fbp == 0) {
            w = t.img.width;
            h = std::max(t.used_height, 1);
            return;
        }
    }
    w = h = 0;
}

void GsBackend::CopyPresent(VkCommandBuffer cmd, VkImage dst, int dst_w, int dst_h) {
    int w = 0, h = 0;
    PresentSize(w, h);
    if (w == 0 || h == 0) return;
    Image* src = present_valid_ ? &present_ : nullptr;
    if (!src) {
        for (auto& [key, t] : targets_) {
            if (t.fbp == 0) src = &t.img;
        }
    }
    if (!src) return;
    const VkImageLayout restore = src->layout;
    Transition(cmd, *src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = dst;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkImageCopy c{};
    c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                c.dstSubresource = c.srcSubresource;
    c.extent = {static_cast<uint32_t>(std::min(w, dst_w)), static_cast<uint32_t>(std::min(h, dst_h)), 1};
    vkCmdCopyImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    Transition(cmd, *src, restore);
}

}  // namespace ac04::render::gs
