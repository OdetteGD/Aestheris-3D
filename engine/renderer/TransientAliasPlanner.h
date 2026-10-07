#pragma once
#include "engine/renderer/AetherisRenderGraph.h"
#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace aetheris {

struct TransientAliasSlot final {
    VkDeviceSize requiredSize{0};
    VkDeviceSize alignment{1};
    uint32_t memoryTypeBits{0};
    uint32_t lastUse{0};
    bool occupied{false};
};

class TransientAliasPlanner final {
    static constexpr uint32_t MaxSlots = 128;
    std::array<TransientAliasSlot, MaxSlots> slots_{};
    std::array<uint16_t, AetherisRenderGraph::MaxResources> resourceToSlot_{};
public:
    TransientAliasPlanner() noexcept { resourceToSlot_.fill(0xffffu); }
    void Reset() noexcept { slots_.fill({}); resourceToSlot_.fill(0xffffu); }

    bool Assign(ResourceHandle resource, VkMemoryRequirements requirements,
                uint32_t firstUse, uint32_t lastUse) noexcept;
    uint16_t Slot(ResourceHandle resource) const noexcept {
        return resource.Valid() ? resourceToSlot_[resource.Value()] : 0xffffu;
    }
    const TransientAliasSlot* GetSlot(uint16_t i) const noexcept {
        return i < MaxSlots ? &slots_[i] : nullptr;
    }
};

}
