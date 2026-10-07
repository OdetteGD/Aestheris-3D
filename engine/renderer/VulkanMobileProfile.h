#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace aetheris {

struct VulkanMobileProfile final {
    uint32_t apiVersion{VK_API_VERSION_1_1};
    bool hasVulkan12{false};
    bool descriptorIndexing{false};
    bool bufferDeviceAddress{false};
    bool timelineSemaphore{false};
    bool synchronization2{false};
    bool dynamicRendering{false};
    bool astcLdr{false};
    bool externalAhb{false};
    bool unifiedGraphicsPresent{false};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};

    static VulkanMobileProfile Query(VkPhysicalDevice gpu, VkSurfaceKHR surface);
    bool SupportsTransientAttachments() const noexcept { return true; }
    bool PreferSubpasses() const noexcept { return true; }
    bool IsLowBandwidthPath() const noexcept;
};

}
