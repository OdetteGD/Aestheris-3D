#include "engine/renderer/AetherisRenderGraph.h"
#include <algorithm>

namespace aetheris {

AetherisRenderGraph::Builder& AetherisRenderGraph::Builder::ReadResource(ResourceHandle h, VkImageLayout expected) noexcept {
    if (pass_ && h.Valid() && pass_->readCount < pass_->reads.size()) {
        pass_->reads[pass_->readCount] = h;
        pass_->readLayouts[pass_->readCount++] = expected;
    }
    return *this;
}
AetherisRenderGraph::Builder& AetherisRenderGraph::Builder::WriteResource(ResourceHandle h, VkImageLayout expected) noexcept {
    if (pass_ && h.Valid() && pass_->writeCount < pass_->writes.size()) {
        pass_->writes[pass_->writeCount] = h;
        pass_->writeLayouts[pass_->writeCount++] = expected;
    }
    return *this;
}
AetherisRenderGraph::Builder& AetherisRenderGraph::Builder::SideEffect(bool enabled) noexcept {
    if (pass_) pass_->sideEffect = enabled;
    return *this;
}

void AetherisRenderGraph::Reset() noexcept {
    resourceCount_ = passCount_ = orderCount_ = 0;
    compiled_ = false;
    for (auto& x : live_) x = 0;
    for (auto& x : barrierCounts_) x = 0;
    for (auto& row : edges_) row.fill(0);
}

ResourceHandle AetherisRenderGraph::CreateImage(const RenderResourceDesc& desc) noexcept {
    if (resourceCount_ >= MaxResources) return {};
    const ResourceHandle h{static_cast<uint16_t>(resourceCount_++)};
    resources_[h.Value()] = desc;
    resources_[h.Value()].isImage = true;
    initialStates_[h.Value()] = {};
    return h;
}
ResourceHandle AetherisRenderGraph::CreateBuffer(const RenderResourceDesc& desc) noexcept {
    if (resourceCount_ >= MaxResources) return {};
    const ResourceHandle h{static_cast<uint16_t>(resourceCount_++)};
    resources_[h.Value()] = desc;
    resources_[h.Value()].isImage = false;
    initialStates_[h.Value()] = {};
    return h;
}
void AetherisRenderGraph::ImportImage(ResourceHandle h, VkImage image, VkImageLayout layout) noexcept {
    if (!h.Valid() || h.Value() >= resourceCount_) return;
    images_[h.Value()] = image;
    resources_[h.Value()].imported = true;
    initialStates_[h.Value()].layout = layout;
}
void AetherisRenderGraph::ImportBuffer(ResourceHandle h, VkBuffer buffer) noexcept {
    if (!h.Valid() || h.Value() >= resourceCount_) return;
    buffers_[h.Value()] = buffer;
    resources_[h.Value()].imported = true;
}
AetherisRenderGraph::Builder AetherisRenderGraph::AddPass(const char* name, RenderPassType type,
    RenderGraphPass::ExecuteFn fn, void* user) noexcept {
    if (passCount_ >= MaxPasses) return Builder(*this, passes_[MaxPasses - 1]);
    RenderGraphPass& p = passes_[passCount_++];
    p = {};
    p.name = name; p.type = type; p.execute = fn; p.userData = user;
    return Builder(*this, p);
}

bool AetherisRenderGraph::BuildDependencies() noexcept {
    for (auto& row : edges_) row.fill(0);
    for (uint32_t a = 0; a < passCount_; ++a) {
        for (uint32_t b = a + 1; b < passCount_; ++b) {
            bool dependency = false;
            for (uint32_t wa = 0; wa < passes_[a].writeCount && !dependency; ++wa) {
                const auto r = passes_[a].writes[wa];
                for (uint32_t rb = 0; rb < passes_[b].readCount; ++rb)
                    dependency |= (r == passes_[b].reads[rb]);
                for (uint32_t wb = 0; wb < passes_[b].writeCount; ++wb)
                    dependency |= (r == passes_[b].writes[wb]);
            }
            for (uint32_t ra = 0; ra < passes_[a].readCount && !dependency; ++ra)
                for (uint32_t wb = 0; wb < passes_[b].writeCount; ++wb)
                    dependency |= (passes_[a].reads[ra] == passes_[b].writes[wb]);
            if (dependency) edges_[a][b] = 1;
        }
    }
    return true;
}

bool AetherisRenderGraph::CullDeadPasses() noexcept {
    live_.fill(0);
    for (uint32_t i = 0; i < passCount_; ++i) if (passes_[i].sideEffect) live_[i] = 1;
    // Any resource exported/imported to the outside world is a graph sink.
    for (uint32_t i = 0; i < passCount_; ++i) {
        for (uint32_t w = 0; w < passes_[i].writeCount; ++w) {
            const auto h = passes_[i].writes[w];
            if (resources_[h.Value()].exported || resources_[h.Value()].imported) live_[i] = 1;
        }
    }
    for (int i = static_cast<int>(passCount_) - 1; i >= 0; --i) {
        if (!live_[static_cast<uint32_t>(i)]) continue;
        for (int p = i - 1; p >= 0; --p) if (edges_[static_cast<uint32_t>(p)][static_cast<uint32_t>(i)]) live_[static_cast<uint32_t>(p)] = 1;
    }
    return true;
}

bool AetherisRenderGraph::TopologicalSort() noexcept {
    indegree_.fill(0);
    for (uint32_t a = 0; a < passCount_; ++a) if (live_[a])
        for (uint32_t b = 0; b < passCount_; ++b)
            if (live_[b] && edges_[a][b]) ++indegree_[b];

    std::array<uint16_t, MaxPasses> queue{};
    uint32_t head = 0, tail = 0;
    for (uint32_t i = 0; i < passCount_; ++i) if (live_[i] && indegree_[i] == 0) queue[tail++] = static_cast<uint16_t>(i);
    orderCount_ = 0;
    while (head < tail) {
        const uint16_t n = queue[head++];
        order_[orderCount_++] = n;
        for (uint32_t j = 0; j < passCount_; ++j) if (live_[j] && edges_[n][j] && --indegree_[j] == 0) queue[tail++] = static_cast<uint16_t>(j);
    }
    return orderCount_ == std::count(live_.begin(), live_.begin() + passCount_, uint8_t{1});
}

bool AetherisRenderGraph::BuildBarriers() noexcept {
    for (auto& x : barrierCounts_) x = 0;
    std::array<RenderResourceState, MaxResources> state = initialStates_;
    for (uint32_t oi = 0; oi < orderCount_; ++oi) {
        const uint32_t pi = order_[oi];
        auto addImageBarrier = [&](ResourceHandle h, VkImageLayout desired, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
            if (!h.Valid() || !resources_[h.Value()].isImage) return;
            const bool layoutChange = state[h.Value()].layout != desired;
            const bool accessChange = state[h.Value()].access != dstAccess;
            if (!layoutChange && !accessChange) return;
            if (barrierCounts_[pi] >= barriers_[pi].size()) return;
            auto& b = barriers_[pi][barrierCounts_[pi]++];
            b.resource = h; b.isImage = true;
            b.image = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.srcStage = state[h.Value()].stages;
            b.dstStage = dstStage;
            b.image.oldLayout = state[h.Value()].layout;
            b.image.newLayout = desired;
            b.image.srcAccessMask = state[h.Value()].access;
            b.image.dstAccessMask = dstAccess;
            b.image.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image.image = images_[h.Value()];
            b.image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, resources_[h.Value()].image.mipLevels, 0, resources_[h.Value()].image.arrayLayers};
            state[h.Value()] = {desired, dstStage, dstAccess};
        };
        for (uint32_t r = 0; r < passes_[pi].readCount; ++r)
            addImageBarrier(passes_[pi].reads[r], passes_[pi].readLayouts[r],
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);
        for (uint32_t w = 0; w < passes_[pi].writeCount; ++w)
            addImageBarrier(passes_[pi].writes[w], passes_[pi].writeLayouts[w],
                passes_[pi].type == RenderPassType::Compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                passes_[pi].type == RenderPassType::Compute ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
    return true;
}

bool AetherisRenderGraph::Compile() noexcept {
    compiled_ = BuildDependencies() && CullDeadPasses() && TopologicalSort() && BuildBarriers();
    return compiled_;
}

void AetherisRenderGraph::Execute(VkCommandBuffer cmd, RenderGraphContext& context) noexcept {
    if (!compiled_) return;
    for (uint32_t oi = 0; oi < orderCount_; ++oi) {
        const uint32_t pi = order_[oi];
        const uint32_t count = barrierCounts_[pi];
        if (count) {
            std::array<VkImageMemoryBarrier, 64> ib{};
            uint32_t ic = 0;
            for (uint32_t i = 0; i < count; ++i) if (barriers_[pi][i].isImage) ib[ic++] = barriers_[pi][i].image;
            if (ic) {
                VkPipelineStageFlags srcStages = 0, dstStages = 0;
                for (uint32_t i = 0; i < count; ++i) { srcStages |= barriers_[pi][i].srcStage; dstStages |= barriers_[pi][i].dstStage; }
                if (!srcStages) srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                if (!dstStages) dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                vkCmdPipelineBarrier(cmd, srcStages, dstStages, 0, 0, nullptr, 0, nullptr, ic, ib.data());
            }
        }
        if (passes_[pi].execute) passes_[pi].execute(context, cmd, passes_[pi].userData);
    }
}
const RenderResourceDesc* AetherisRenderGraph::GetResource(ResourceHandle h) const noexcept {
    return h.Valid() && h.Value() < resourceCount_ ? &resources_[h.Value()] : nullptr;
}
VkImage AetherisRenderGraph::GetImage(ResourceHandle h) const noexcept { return h.Valid() ? images_[h.Value()] : VK_NULL_HANDLE; }
VkBuffer AetherisRenderGraph::GetBuffer(ResourceHandle h) const noexcept { return h.Valid() ? buffers_[h.Value()] : VK_NULL_HANDLE; }

}
