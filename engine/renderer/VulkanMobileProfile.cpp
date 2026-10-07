#include "engine/renderer/VulkanMobileProfile.h"
#include <algorithm>
#include <cstring>

namespace aetheris {

namespace {
bool HasDeviceExtension(VkPhysicalDevice gpu, const char* name) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr) != VK_SUCCESS) return false;
    // Capability probing is initialization-time only; this vector is never touched by the frame loop.
    std::vector<VkExtensionProperties> props(count);
    if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, props.data()) != VK_SUCCESS) return false;
    for (const auto& p : props) if (std::strcmp(p.extensionName, name) == 0) return true;
    return false;
}
}

VulkanMobileProfile VulkanMobileProfile::Query(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    VulkanMobileProfile p{};
    vkGetPhysicalDeviceProperties(gpu, &p.properties);
    vkGetPhysicalDeviceMemoryProperties(gpu, &p.memory);
    p.apiVersion = std::min(p.properties.apiVersion, VK_API_VERSION_1_2);
    p.hasVulkan12 = VK_VERSION_MINOR(p.apiVersion) >= 2;
    p.descriptorIndexing = p.hasVulkan12 ||
        HasDeviceExtension(gpu, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    p.bufferDeviceAddress = p.hasVulkan12 ||
        HasDeviceExtension(gpu, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    p.timelineSemaphore = p.hasVulkan12 ||
        HasDeviceExtension(gpu, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    p.synchronization2 = HasDeviceExtension(gpu, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    p.dynamicRendering = HasDeviceExtension(gpu, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    VkFormatProperties astcProps{};
    vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_ASTC_6x6_UNORM_BLOCK, &astcProps);
    p.astcLdr = (astcProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    p.externalAhb = HasDeviceExtension(gpu, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
    std::vector<VkQueueFamilyProperties> q(families);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, q.data());
    for (uint32_t i = 0; i < families; ++i) {
        VkBool32 present = VK_FALSE;
        if (surface != VK_NULL_HANDLE)
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
        if ((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            p.unifiedGraphicsPresent = true;
            break;
        }
    }
    return p;
}

bool VulkanMobileProfile::IsLowBandwidthPath() const noexcept {
    return properties.limits.maxImageDimension2D <= 4096u ||
           properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}

}
