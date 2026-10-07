#pragma once
#include <vulkan/vulkan.h>

namespace aetheris {

struct MobileAttachmentPolicy final {
    static VkImageCreateInfo MakeTransientImage(VkFormat format, VkExtent2D extent,
                                                VkImageUsageFlags extraUsage = 0) noexcept {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = {extent.width, extent.height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | extraUsage;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return ci;
    }

    static VkAttachmentDescription MakeDontCareAttachment(VkFormat format,
                                                           VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) noexcept {
        VkAttachmentDescription a{};
        a.format = format;
        a.samples = samples;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return a;
    }
};

}
