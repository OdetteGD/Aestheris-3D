#pragma once
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
namespace aetheris {
struct AndroidHardwareBufferBridge final {
    static bool Supported(VkPhysicalDevice gpu) noexcept;
    static bool CanImportColor(VkPhysicalDevice gpu, VkFormat format) noexcept;
};
}