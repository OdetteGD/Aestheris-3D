#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace aetheris {

struct GpuAllocation final {
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize offset{0};
    VkDeviceSize size{0};
    uint32_t memoryType{0};
    bool dedicated{false};
};

struct GpuImage final {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    GpuAllocation allocation{};
};

struct GpuBuffer final {
    VkBuffer buffer{VK_NULL_HANDLE};
    GpuAllocation allocation{};
};

class GPUResourceManager final {
public:
    static constexpr uint32_t MaxMemoryBlocks = 32;
    static constexpr uint32_t MaxDescriptorPools = 8;
    static constexpr uint32_t MaxPipelineLayouts = 128;

private:
    struct MemoryBlock { VkDeviceMemory memory{}; VkDeviceSize size{}; VkDeviceSize cursor{}; uint32_t type{}; };
    struct DescriptorPoolSlot { VkDescriptorPool pool{}; uint32_t remainingSets{}; };
    struct PipelineLayoutEntry { uint64_t hash{}; VkPipelineLayout layout{}; };

    VkPhysicalDevice physical_{};
    VkDevice device_{};
    std::array<MemoryBlock, MaxMemoryBlocks> blocks_{};
    std::array<DescriptorPoolSlot, MaxDescriptorPools> descriptorPools_{};
    std::array<PipelineLayoutEntry, MaxPipelineLayouts> layoutCache_{};
    VkPipelineCache pipelineCache_{};
    uint32_t blockCount_{0}, descriptorPoolCount_{0}, layoutCount_{0};

    uint32_t FindMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const noexcept;
    GpuAllocation Allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags, bool dedicated) noexcept;
    VkDescriptorPool AcquireDescriptorPool() noexcept;

public:
    GPUResourceManager() noexcept = default;
    bool Initialize(VkPhysicalDevice physical, VkDevice device, const std::filesystem::path& pipelineCachePath) noexcept;
    void Shutdown() noexcept;
    GpuImage CreateImage(const VkImageCreateInfo& info, VkImageAspectFlags aspect, VkMemoryPropertyFlags flags) noexcept;
    GpuBuffer CreateBuffer(const VkBufferCreateInfo& info, VkMemoryPropertyFlags flags) noexcept;
    void DestroyImage(GpuImage& image) noexcept;
    void DestroyBuffer(GpuBuffer& buffer) noexcept;
    VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout, uint32_t maxSetsPerPool = 64) noexcept;
    VkPipelineLayout GetOrCreatePipelineLayout(const VkPipelineLayoutCreateInfo& info, uint64_t stableHash) noexcept;
    void SavePipelineCache(const std::filesystem::path& path) noexcept;
    void ResetTransientFrameArena() noexcept;
};

}
