#include "android/AndroidHardwareBufferBridge.h"
#include <cstring>
#include <vector>
namespace aetheris {
bool AndroidHardwareBufferBridge::Supported(VkPhysicalDevice gpu) noexcept {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
    for (const auto& e : extensions)
        if (std::strcmp(e.extensionName, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) == 0) return true;
    return false;
}
bool AndroidHardwareBufferBridge::CanImportColor(VkPhysicalDevice gpu, VkFormat format) noexcept {
    (void)format;
    return Supported(gpu);
}
}