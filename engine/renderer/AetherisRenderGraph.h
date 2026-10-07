#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace aetheris {

struct ResourceHandle final {
    uint16_t index{0xffffu};
    constexpr bool Valid() const noexcept { return index != 0xffffu; }
    constexpr uint16_t Value() const noexcept { return index; }
    friend constexpr bool operator==(ResourceHandle a, ResourceHandle b) noexcept { return a.index == b.index; }
};

enum class RenderPassType : uint8_t { Graphics, Compute, Transfer };

struct RenderResourceDesc final {
    VkImageCreateInfo image{};
    VkBufferCreateInfo buffer{};
    bool isImage{true};
    bool transient{true};
    bool imported{false};
    bool exported{false};
};

struct RenderResourceState final {
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkPipelineStageFlags stages{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
    VkAccessFlags access{0};
};

class RenderGraphContext;

class RenderGraphPass final {
public:
    using ExecuteFn = void(*)(RenderGraphContext&, VkCommandBuffer, void*) noexcept;
    const char* name{nullptr};
    RenderPassType type{RenderPassType::Graphics};
    ExecuteFn execute{nullptr};
    void* userData{nullptr};
    std::array<ResourceHandle, 32> reads{};
    std::array<VkImageLayout, 32> readLayouts{};
    uint32_t readCount{0};
    std::array<ResourceHandle, 32> writes{};
    std::array<VkImageLayout, 32> writeLayouts{};
    uint32_t writeCount{0};
    bool sideEffect{false};
};

class AetherisRenderGraph final {
public:
    static constexpr uint32_t MaxResources = 256;
    static constexpr uint32_t MaxPasses = 128;

    class Builder final {
        AetherisRenderGraph& graph_;
        RenderGraphPass* pass_{};
    public:
        Builder(AetherisRenderGraph& g, RenderGraphPass& p) noexcept : graph_(g), pass_(&p) {}
        Builder& ReadResource(ResourceHandle h, VkImageLayout expected) noexcept;
        Builder& WriteResource(ResourceHandle h, VkImageLayout expected) noexcept;
        Builder& SideEffect(bool enabled = true) noexcept;
    };

    struct CompiledBarrier final {
        ResourceHandle resource{};
        VkImageMemoryBarrier image{};
        VkBufferMemoryBarrier buffer{};
        VkPipelineStageFlags srcStage{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
        VkPipelineStageFlags dstStage{VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
        bool isImage{true};
    };

private:
    std::array<RenderResourceDesc, MaxResources> resources_{};
    std::array<RenderResourceState, MaxResources> initialStates_{};
    std::array<RenderGraphPass, MaxPasses> passes_{};
    std::array<uint8_t, MaxPasses> live_{};
    std::array<uint16_t, MaxPasses> order_{};
    std::array<uint16_t, MaxPasses> indegree_{};
    std::array<std::array<uint8_t, MaxPasses>, MaxPasses> edges_{};
    std::array<std::array<CompiledBarrier, 64>, MaxPasses> barriers_{};
    std::array<uint8_t, MaxPasses> barrierCounts_{};
    std::array<VkImage, MaxResources> images_{};
    std::array<VkBuffer, MaxResources> buffers_{};
    uint32_t resourceCount_{0};
    uint32_t passCount_{0};
    uint32_t orderCount_{0};
    bool compiled_{false};

    bool BuildDependencies() noexcept;
    bool CullDeadPasses() noexcept;
    bool TopologicalSort() noexcept;
    bool BuildBarriers() noexcept;

public:
    AetherisRenderGraph() noexcept = default;
    void Reset() noexcept;
    ResourceHandle CreateImage(const RenderResourceDesc& desc) noexcept;
    ResourceHandle CreateBuffer(const RenderResourceDesc& desc) noexcept;
    void ImportImage(ResourceHandle h, VkImage image, VkImageLayout layout) noexcept;
    void ImportBuffer(ResourceHandle h, VkBuffer buffer) noexcept;
    Builder AddPass(const char* name, RenderPassType type, RenderGraphPass::ExecuteFn fn, void* user = nullptr) noexcept;
    bool Compile() noexcept;
    void Execute(VkCommandBuffer cmd, RenderGraphContext& context) noexcept;
    const RenderResourceDesc* GetResource(ResourceHandle h) const noexcept;
    VkImage GetImage(ResourceHandle h) const noexcept;
    VkBuffer GetBuffer(ResourceHandle h) const noexcept;
    uint32_t CompiledPassCount() const noexcept { return orderCount_; }
};

class RenderGraphContext final {
    AetherisRenderGraph& graph_;
public:
    explicit RenderGraphContext(AetherisRenderGraph& graph) noexcept : graph_(graph) {}
    VkImage Image(ResourceHandle h) const noexcept { return graph_.GetImage(h); }
    VkBuffer Buffer(ResourceHandle h) const noexcept { return graph_.GetBuffer(h); }
};

}
