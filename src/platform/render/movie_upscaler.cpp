#include "platform/render/movie_upscaler.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>

#include "fsr1_easu_comp_spv.h"
#include "platform/log.h"

namespace ac04::render {
namespace {

struct EasuParams {
    uint32_t con0[4], con1[4], con2[4], con3[4];
    int32_t out_size[2];
};
static_assert(sizeof(EasuParams) == 72, "EASU push constant layout");

uint32_t FloatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// FsrEasuCon (ffx_fsr1.h, FSR 1.0.2) for a full-image pass: viewport = input size.
void EasuConstants(EasuParams& p, float in_w, float in_h, float out_w, float out_h) {
    p.con0[0] = FloatBits(in_w / out_w);
    p.con0[1] = FloatBits(in_h / out_h);
    p.con0[2] = FloatBits(0.5f * in_w / out_w - 0.5f);
    p.con0[3] = FloatBits(0.5f * in_h / out_h - 0.5f);
    p.con1[0] = FloatBits(1.0f / in_w);
    p.con1[1] = FloatBits(1.0f / in_h);
    p.con1[2] = FloatBits(1.0f / in_w);
    p.con1[3] = FloatBits(-1.0f / in_h);
    p.con2[0] = FloatBits(-1.0f / in_w);
    p.con2[1] = FloatBits(2.0f / in_h);
    p.con2[2] = FloatBits(1.0f / in_w);
    p.con2[3] = FloatBits(2.0f / in_h);
    p.con3[0] = FloatBits(0.0f);
    p.con3[1] = FloatBits(4.0f / in_h);
    p.con3[2] = 0;
    p.con3[3] = 0;
    p.out_size[0] = static_cast<int32_t>(out_w);
    p.out_size[1] = static_cast<int32_t>(out_h);
}

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    VkImageView view = VK_NULL_HANDLE;
    int width = 0, height = 0;
};

class Fsr1MovieUpscaler final : public MovieUpscaler {
public:
    ~Fsr1MovieUpscaler() override { Shutdown(); }

    bool Init(VkContext& ctx, std::string& error) override;
    void Shutdown() override;
    bool Upscale(VkCommandBuffer cmd, int slot, const std::vector<uint8_t>& rgba, int width, int height,
                 int factor) override;
    VkImage OutputImage() const override { return passes_.empty() ? VK_NULL_HANDLE : passes_.back().image; }
    VkImageView OutputView() const override { return passes_.empty() ? VK_NULL_HANDLE : passes_.back().view; }
    int OutputWidth() const override { return passes_.empty() ? 0 : passes_.back().width; }
    int OutputHeight() const override { return passes_.empty() ? 0 : passes_.back().height; }

private:
    static constexpr int kSlots = 2;    // frames in flight (Renderer::kFramesInFlight)
    static constexpr int kMaxPasses = 4;  // each EASU pass scales by at most 2 per axis: N <= 16

    bool Resize(int width, int height, int factor);
    void DestroyImages();
    bool CreateImage(Image& img, int width, int height, VkImageUsageFlags usage);
    void DestroyImage(Image& img);

    VkContext* ctx_ = nullptr;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[kMaxPasses]{};
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkBuffer staging_[kSlots]{};
    VmaAllocation staging_alloc_[kSlots]{};
    void* staging_ptr_[kSlots]{};
    Image source_;
    std::vector<Image> passes_;  // pass i writes passes_[i]; the last one is the output
    int width_ = 0, height_ = 0, factor_ = 0;
};

bool Fsr1MovieUpscaler::Init(VkContext& ctx, std::string& error) {
    ctx_ = &ctx;
    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 2;
    dli.pBindings = b;
    vkCreateDescriptorSetLayout(ctx.device, &dli, nullptr, &set_layout_);

    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EasuParams)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx.device, &pli, nullptr, &pipeline_layout_);

    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(fsr1_easu_comp_spv);
    smi.pCode = fsr1_easu_comp_spv;
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(ctx.device, &smi, nullptr, &module);
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                 module, "main", nullptr};
    cpi.layout = pipeline_layout_;
    const VkResult r = vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline_);
    vkDestroyShaderModule(ctx.device, module, nullptr);
    if (r != VK_SUCCESS) {
        error = std::string("cannot create the movie upscaling pipeline: ") + VkResultName(r);
        return false;
    }

    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxPasses},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxPasses}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = kMaxPasses;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = ps;
    vkCreateDescriptorPool(ctx.device, &dpi, nullptr, &pool_);
    VkDescriptorSetLayout layouts[kMaxPasses] = {set_layout_, set_layout_, set_layout_, set_layout_};
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = kMaxPasses;
    dai.pSetLayouts = layouts;
    vkAllocateDescriptorSets(ctx.device, &dai, sets_);

    // EASU reads with textureGather (filter unused); the alpha sample is bilinear.
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    vkCreateSampler(ctx.device, &sci, nullptr, &sampler_);
    return true;
}

bool Fsr1MovieUpscaler::CreateImage(Image& img, int width, int height, VkImageUsageFlags usage) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
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
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx_->device, &vci, nullptr, &img.view);
    img.width = width;
    img.height = height;
    return true;
}

void Fsr1MovieUpscaler::DestroyImage(Image& img) {
    if (img.view) vkDestroyImageView(ctx_->device, img.view, nullptr);
    if (img.image) vmaDestroyImage(ctx_->allocator, img.image, img.alloc);
    img = Image{};
}

void Fsr1MovieUpscaler::DestroyImages() {
    if (ctx_ == nullptr || !ctx_->device) return;
    for (Image& img : passes_) DestroyImage(img);
    passes_.clear();
    DestroyImage(source_);
    for (int i = 0; i < kSlots; ++i) {
        if (staging_[i]) vmaDestroyBuffer(ctx_->allocator, staging_[i], staging_alloc_[i]);
        staging_[i] = VK_NULL_HANDLE;
        staging_alloc_[i] = nullptr;
        staging_ptr_[i] = nullptr;
    }
    width_ = height_ = factor_ = 0;
}

bool Fsr1MovieUpscaler::Resize(int width, int height, int factor) {
    vkDeviceWaitIdle(ctx_->device);  // only when the picture size or the factor changes
    DestroyImages();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
    for (int i = 0; i < kSlots; ++i) {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = bytes;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(ctx_->allocator, &bci, &aci, &staging_[i], &staging_alloc_[i], &info) != VK_SUCCESS)
            return false;
        staging_ptr_[i] = info.pMappedData;
    }
    if (!CreateImage(source_, width, height, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
        return false;
    // Pass chain: x2 per pass, the last pass to the exact factor.
    int scale = 1;
    while (scale < factor && static_cast<int>(passes_.size()) < kMaxPasses) {
        scale = std::min(scale * 2, factor);
        passes_.emplace_back();
        if (!CreateImage(passes_.back(), width * scale, height * scale,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            return false;
    }
    for (size_t i = 0; i < passes_.size(); ++i) {
        const Image& in = i == 0 ? source_ : passes_[i - 1];
        VkDescriptorImageInfo src{sampler_, in.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo dst{VK_NULL_HANDLE, passes_[i].view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w[2]{};
        w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = w[1].dstSet = sets_[i];
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[0].pImageInfo = &src;
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[1].pImageInfo = &dst;
        vkUpdateDescriptorSets(ctx_->device, 2, w, 0, nullptr);
    }
    width_ = width;
    height_ = height;
    factor_ = factor;
    AC04_LOG_INFO("renderer", "movie upscaling: %dx%d x%d (FSR 1 EASU, %d pass%s)", width, height, factor,
                  static_cast<int>(passes_.size()), passes_.size() == 1 ? "" : "es");
    return true;
}

bool Fsr1MovieUpscaler::Upscale(VkCommandBuffer cmd, int slot, const std::vector<uint8_t>& rgba, int width,
                                int height, int factor) {
    if (ctx_ == nullptr || !pipeline_ || factor < 2 || width <= 0 || height <= 0 ||
        rgba.size() < static_cast<size_t>(width) * height * 4)
        return false;
    if (width != width_ || height != height_ || factor != factor_) {
        if (!Resize(width, height, factor)) {
            AC04_LOG_ERROR("renderer", "movie upscaling: cannot create its images");
            DestroyImages();
            return false;
        }
    }
    slot %= kSlots;
    std::memcpy(staging_ptr_[slot], rgba.data(), static_cast<size_t>(width) * height * 4);
    vmaFlushAllocation(ctx_->allocator, staging_alloc_[slot], 0, VK_WHOLE_SIZE);

    // Upload the picture. The previous contents are discarded (earlier readers: compute passes).
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.image = source_.image;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, staging_[slot], source_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    for (size_t i = 0; i < passes_.size(); ++i) {
        const Image& in = i == 0 ? source_ : passes_[i - 1];
        Image& out = passes_[i];
        // Earlier readers of the output (fragment shaders, transfers, the next pass) finish first.
        b.image = out.image;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        EasuParams p{};
        EasuConstants(p, static_cast<float>(in.width), static_cast<float>(in.height), static_cast<float>(out.width),
                      static_cast<float>(out.height));
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(p), &p);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1, &sets_[i], 0, nullptr);
        vkCmdDispatch(cmd, static_cast<uint32_t>((out.width + 7) / 8), static_cast<uint32_t>((out.height + 7) / 8), 1);
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    }
    return true;
}

void Fsr1MovieUpscaler::Shutdown() {
    if (ctx_ == nullptr || !ctx_->device) return;
    vkDeviceWaitIdle(ctx_->device);
    DestroyImages();
    if (sampler_) vkDestroySampler(ctx_->device, sampler_, nullptr);
    if (pool_) vkDestroyDescriptorPool(ctx_->device, pool_, nullptr);
    if (pipeline_) vkDestroyPipeline(ctx_->device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(ctx_->device, pipeline_layout_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(ctx_->device, set_layout_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    ctx_ = nullptr;
}

}  // namespace

std::unique_ptr<MovieUpscaler> CreateFsr1MovieUpscaler() { return std::make_unique<Fsr1MovieUpscaler>(); }

}  // namespace ac04::render
