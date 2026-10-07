#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace aetheris {

struct MobileSubpassPlan final {
    std::array<VkAttachmentReference, 8> color{};
    std::array<VkAttachmentReference, 8> input{};
    VkAttachmentReference depth{};
    uint32_t colorCount{0};
    uint32_t inputCount{0};
    bool hasDepth{false};

    VkSubpassDescription Description() const noexcept {
        VkSubpassDescription d{};
        d.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        d.colorAttachmentCount = colorCount;
        d.pColorAttachments = color.data();
        d.inputAttachmentCount = inputCount;
        d.pInputAttachments = input.data();
        d.pDepthStencilAttachment = hasDepth ? &depth : nullptr;
        return d;
    }
};

}
