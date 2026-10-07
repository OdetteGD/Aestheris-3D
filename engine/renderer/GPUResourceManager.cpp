#include "engine/renderer/GPUResourceManager.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include "engine/core/AetherisLog.h"

namespace aetheris {

bool GPUResourceManager::Initialize(VkPhysicalDevice physical, VkDevice device, const std::filesystem::path& path) noexcept {
    physical_ = physical; device_ = device;
    AETHERIS_LOGI("GPUResourceManager initialize: physical=%p device=%p", static_cast<void*>(physical_), static_cast<void*>(device_));
    VkPipelineCacheCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    std::ifstream in(path, std::ios::binary);
    std::vector<char> data;
    if (in) data.assign(std::istreambuf_iterator<char>(in), {});
    if (!data.empty()) { pci.initialDataSize = data.size(); pci.pInitialData = data.data(); }
    VkResult cacheResult = vkCreatePipelineCache(device_, &pci, nullptr, &pipelineCache_);
    if (cacheResult != VK_SUCCESS && !data.empty()) {
        pci.initialDataSize = 0; pci.pInitialData = nullptr;
        cacheResult = vkCreatePipelineCache(device_, &pci, nullptr, &pipelineCache_);
    }
    if (cacheResult != VK_SUCCESS) {
        AETHERIS_LOGE("vkCreatePipelineCache failed: %s", VkResultName(cacheResult));
        return false;
    }
    AETHERIS_LOGI("GPUResourceManager ready: pipeline cache=0x%llx", static_cast<unsigned long long>(VkHandleValue(pipelineCache_)));
    return true;
}

uint32_t GPUResourceManager::FindMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const noexcept {
    VkPhysicalDeviceMemoryProperties p{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i) if (bits & (1u << i)) return i;
    return UINT32_MAX;
}

GpuAllocation GPUResourceManager::Allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags, bool dedicated) noexcept {
    GpuAllocation a{};
    const uint32_t type = FindMemoryType(req.memoryTypeBits, flags);
    if (type == UINT32_MAX) return a;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(req.alignment, 256);
    if (!dedicated) {
        for (uint32_t i = 0; i < blockCount_; ++i) {
            auto& b = blocks_[i];
            if (b.type != type) continue;
            const VkDeviceSize off = (b.cursor + alignment - 1) & ~(alignment - 1);
            if (off + req.size <= b.size) {
                b.cursor = off + req.size;
                a = {b.memory, off, req.size, type, false};
                return a;
            }
        }
    }
    if (blockCount_ >= MaxMemoryBlocks) return a;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = dedicated ? req.size : std::max<VkDeviceSize>(req.size * 16, 4ull * 1024ull * 1024ull);
    ai.memoryTypeIndex = type;
    VkDeviceMemory memory{};
    if (vkAllocateMemory(device_, &ai, nullptr, &memory) != VK_SUCCESS) return a;
    auto& b = blocks_[blockCount_++];
    b = {memory, ai.allocationSize, req.size, type};
    a = {memory, 0, req.size, type, dedicated};
    return a;
}

GpuImage GPUResourceManager::CreateImage(const VkImageCreateInfo& info, VkImageAspectFlags aspect, VkMemoryPropertyFlags flags) noexcept {
    GpuImage out{};
    if (vkCreateImage(device_, &info, nullptr, &out.image) != VK_SUCCESS) return out;
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, out.image, &req);
    out.allocation = Allocate(req, flags, info.tiling == VK_IMAGE_TILING_LINEAR);
    if (!out.allocation.memory || vkBindImageMemory(device_, out.image, out.allocation.memory, out.allocation.offset) != VK_SUCCESS) {
        vkDestroyImage(device_, out.image, nullptr); out = {}; return out;
    }
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = info.format;
    vi.subresourceRange = {aspect, 0, info.mipLevels, 0, info.arrayLayers};
    if (vkCreateImageView(device_, &vi, nullptr, &out.view) != VK_SUCCESS) { DestroyImage(out); return {}; }
    return out;
}

GpuBuffer GPUResourceManager::CreateBuffer(const VkBufferCreateInfo& info, VkMemoryPropertyFlags flags) noexcept {
    GpuBuffer out{};
    if (vkCreateBuffer(device_, &info, nullptr, &out.buffer) != VK_SUCCESS) return out;
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_, out.buffer, &req);
    out.allocation = Allocate(req, flags, false);
    if (!out.allocation.memory || vkBindBufferMemory(device_, out.buffer, out.allocation.memory, out.allocation.offset) != VK_SUCCESS) {
        vkDestroyBuffer(device_, out.buffer, nullptr); return {};
    }
    return out;
}

void GPUResourceManager::DestroyImage(GpuImage& x) noexcept {
    if (x.view) vkDestroyImageView(device_, x.view, nullptr);
    if (x.image) vkDestroyImage(device_, x.image, nullptr);
    x = {};
}
GpuImage GPUResourceManager::CreateAliasedImage(const VkImageCreateInfo& input, VkImageAspectFlags aspect, const GpuAllocation& allocation) noexcept {
    GpuImage out{};
    VkImageCreateInfo info = input;
    info.flags |= VK_IMAGE_CREATE_ALIAS_BIT;
    if (vkCreateImage(device_, &info, nullptr, &out.image) != VK_SUCCESS) return out;
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_, out.image, &req);
    if (!allocation.memory || allocation.offset % req.alignment != 0 || allocation.size < req.size ||
        (req.memoryTypeBits & (1u << allocation.memoryType)) == 0 ||
        vkBindImageMemory(device_, out.image, allocation.memory, allocation.offset) != VK_SUCCESS) {
        vkDestroyImage(device_, out.image, nullptr); return {};
    }
    out.allocation = allocation;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = info.format;
    vi.subresourceRange = {aspect, 0, info.mipLevels, 0, info.arrayLayers};
    if (vkCreateImageView(device_, &vi, nullptr, &out.view) != VK_SUCCESS) { DestroyImage(out); return {}; }
    return out;
}

void GPUResourceManager::DestroyBuffer(GpuBuffer& x) noexcept {
    if (x.buffer) vkDestroyBuffer(device_, x.buffer, nullptr);
    x = {};
}

VkDescriptorPool GPUResourceManager::AcquireDescriptorPool() noexcept {
    for (auto& p : descriptorPools_) if (p.pool && p.remainingSets) return p.pool;
    if (descriptorPoolCount_ >= MaxDescriptorPools) return VK_NULL_HANDLE;
    std::array<VkDescriptorPoolSize, 6> sizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 512},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512}
    }};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 256; pi.poolSizeCount = static_cast<uint32_t>(sizes.size()); pi.pPoolSizes = sizes.data();
    auto& slot = descriptorPools_[descriptorPoolCount_];
    if (vkCreateDescriptorPool(device_, &pi, nullptr, &slot.pool) != VK_SUCCESS) return VK_NULL_HANDLE;
    slot.remainingSets = pi.maxSets; ++descriptorPoolCount_;
    return slot.pool;
}
VkDescriptorSet GPUResourceManager::AllocateDescriptorSet(VkDescriptorSetLayout layout, uint32_t) noexcept {
    for (auto& slot : descriptorPools_) {
        if (!slot.pool || !slot.remainingSets) continue;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = slot.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &layout;
        VkDescriptorSet set{};
        const VkResult result = vkAllocateDescriptorSets(device_, &ai, &set);
        if (result == VK_SUCCESS) { --slot.remainingSets; return set; }
    }
    const VkDescriptorPool pool = AcquireDescriptorPool();
    if (!pool) return VK_NULL_HANDLE;
    for (auto& slot : descriptorPools_) if (slot.pool == pool) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &layout;
        VkDescriptorSet set{};
        if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
        --slot.remainingSets; return set;
    }
    return VK_NULL_HANDLE;
}

void GPUResourceManager::ResetDescriptorPool(uint32_t frameIndex) noexcept {
    if (!device_ || descriptorPoolCount_ == 0) return;
    const uint32_t index = frameIndex % descriptorPoolCount_;
    if (descriptorPools_[index].pool) {
        if (vkResetDescriptorPool(device_, descriptorPools_[index].pool, 0) == VK_SUCCESS)
            descriptorPools_[index].remainingSets = 256;
    }
}
VkPipelineLayout GPUResourceManager::GetOrCreatePipelineLayout(const VkPipelineLayoutCreateInfo& info, uint64_t hash) noexcept {
    for (uint32_t i = 0; i < layoutCount_; ++i) if (layoutCache_[i].hash == hash) return layoutCache_[i].layout;
    if (layoutCount_ >= MaxPipelineLayouts) return VK_NULL_HANDLE;
    VkPipelineLayout layout{};
    if (vkCreatePipelineLayout(device_, &info, nullptr, &layout) != VK_SUCCESS) return VK_NULL_HANDLE;
    layoutCache_[layoutCount_++] = {hash, layout};
    return layout;
}
void GPUResourceManager::SavePipelineCache(const std::filesystem::path& path) noexcept {
    if (!pipelineCache_) return;
    size_t size = 0; if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS) return;
    std::vector<uint8_t> data(size);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) != VK_SUCCESS) return;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(size));
}
void GPUResourceManager::ResetTransientFrameArena() noexcept {}
void GPUResourceManager::Shutdown() noexcept {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    for (auto& e : layoutCache_) if (e.layout) vkDestroyPipelineLayout(device_, e.layout, nullptr);
    for (auto& p : descriptorPools_) if (p.pool) vkDestroyDescriptorPool(device_, p.pool, nullptr);
    if (pipelineCache_) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
    for (auto& b : blocks_) if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    blocks_.fill({}); descriptorPools_.fill({}); layoutCache_.fill({});
    blockCount_ = descriptorPoolCount_ = layoutCount_ = 0; pipelineCache_ = VK_NULL_HANDLE; device_ = VK_NULL_HANDLE;
    AETHERIS_LOGI("GPUResourceManager shutdown complete");
}

}
