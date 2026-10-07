#include "engine/renderer/TransientAliasPlanner.h"
#include <algorithm>

namespace aetheris {

bool TransientAliasPlanner::Assign(ResourceHandle resource, VkMemoryRequirements req,
                                   uint32_t firstUse, uint32_t lastUse) noexcept {
    if (!resource.Valid() || resource.Value() >= resourceToSlot_.size() || firstUse > lastUse) return false;
    for (uint16_t i = 0; i < MaxSlots; ++i) {
        auto& slot = slots_[i];
        const bool compatible = slot.occupied &&
            (slot.memoryTypeBits & req.memoryTypeBits) != 0 &&
            slot.lastUse < firstUse;
        if (compatible) {
            slot.requiredSize = std::max(slot.requiredSize, req.size);
            slot.alignment = std::max(slot.alignment, req.alignment);
            slot.memoryTypeBits &= req.memoryTypeBits;
            slot.lastUse = lastUse;
            resourceToSlot_[resource.Value()] = i;
            return true;
        }
    }
    for (uint16_t i = 0; i < MaxSlots; ++i) {
        auto& slot = slots_[i];
        if (!slot.occupied) {
            slot = {req.size, req.alignment, req.memoryTypeBits, lastUse, true};
            resourceToSlot_[resource.Value()] = i;
            return true;
        }
    }
    return false;
}

}
