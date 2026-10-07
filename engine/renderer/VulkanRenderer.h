#pragma once

#include "IAetherisRenderer.h"
#include "engine/assets/DemoMesh.h"
#include "engine/renderer/GPUResourceManager.h"

#include <array>
#include <filesystem>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

namespace aetheris {

class VulkanRenderer final : public IAetherisRenderer {
    static constexpr uint32_t Frames = 3;

    struct Frame final {
        VkCommandPool pool{};
        VkCommandBuffer cmd{};
        VkSemaphore imageAvailable{};
        VkSemaphore renderFinished{};
        VkFence fence{};
    };

    struct MeshGpu final {
        GpuBuffer vertex{};
        GpuBuffer index{};
        uint32_t indexCount{};
    };

    std::filesystem::path projectRoot_{};
    GPUResourceManager resources_{};

    VkInstance instance_{};
    VkPhysicalDevice gpu_{};
    VkDevice device_{};
    VkSurfaceKHR surface_{};
    VkQueue queue_{};

    VkSwapchainKHR swapchain_{};
    VkFormat format_{};
    VkExtent2D extent_{};

    std::array<VkImage, 3> gbufferImages_{};
    std::array<VkImageView, 3> gbufferViews_{};
    std::array<VkDeviceMemory, 3> gbufferMemory_{};

    VkImage depthImage_{};
    VkImageView depthView_{};
    VkDeviceMemory depthMemory_{};
    VkFormat depthFormat_{VK_FORMAT_D24_UNORM_S8_UINT};

    VkImage hdrImage_{};
    VkImageView hdrView_{};
    VkDeviceMemory hdrMemory_{};
    VkFormat hdrFormat_{VK_FORMAT_R16G16B16A16_SFLOAT};

    VkRenderPass pass_{};
    VkRenderPass postPass_{};

    std::vector<VkImage> images_{};
    std::vector<VkImageView> views_{};
    std::vector<VkFramebuffer> framebuffers_{};
    std::vector<VkFramebuffer> postFramebuffers_{};

    std::array<Frame, Frames> frames_{};

    VkShaderModule geometryVert_{};
    VkShaderModule geometryFrag_{};
    VkShaderModule fullscreenVert_{};
    VkShaderModule lightingFrag_{};
    VkShaderModule postFrag_{};

    VkPipelineLayout geometryLayout_{};
    VkPipelineLayout lightingLayout_{};
    VkPipelineLayout postLayout_{};

    VkPipeline geometryPipeline_{};
    VkPipeline lightingPipeline_{};
    VkPipeline postPipeline_{};

    VkDescriptorSetLayout lightingInputLayout_{};
    VkDescriptorSetLayout lightingFrameLayout_{};
    VkDescriptorSetLayout postSetLayout_{};
    VkDescriptorPool descriptorPool_{};

    VkDescriptorSet lightingInputSet_{};
    VkDescriptorSet lightingFrameSet_{};
    VkDescriptorSet postSet_{};

    VkSampler linearSampler_{};

    VkImage irradianceImage_{};
    VkImageView irradianceView_{};
    VkDeviceMemory irradianceMemory_{};

    VkImage prefilteredImage_{};
    VkImageView prefilteredView_{};
    VkDeviceMemory prefilteredMemory_{};

    VkImage brdfImage_{};
    VkImageView brdfView_{};
    VkDeviceMemory brdfMemory_{};

    GpuBuffer frameUbo_{};
    VkDeviceSize frameUboStride_{64};

    std::array<MeshGpu, kMaxDemoMeshes> demoMeshes_{};

    ANativeWindow* window_{};
    uint32_t family_{UINT32_MAX};
    uint32_t frame_{0};
    uint32_t image_{UINT32_MAX};

    bool initialized_{false};
    bool begun_{false};
    bool mainRenderPassActive_{false};
    bool frameRecorded_{false};

    Mat4 viewProj_{};

    bool CreateInstance();
    bool CreateSurface();
    bool PickGPU();
    bool CreateDevice();
    bool CreateSwapchain();

    bool CreateFrames();

    bool CreateGBufferAttachments();
    bool CreateHDRTarget();
    bool CreatePasses();
    bool CreateViews();
    bool CreateFramebuffers();

    bool CreateShaderModules();
    bool CreateDescriptorLayouts();
    bool CreateDescriptorPoolAndSets();
    bool CreatePipelines();
    bool CreateFrameUniformBuffer();
    bool CreateDefaultIBL();
    bool CreateDemoMeshes();

    bool UploadBufferToDeviceLocal(
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        GpuBuffer& output
    );

    bool UploadImage(
        VkImage image,
        const void* data,
        VkDeviceSize bytes,
        const VkBufferImageCopy* copies,
        uint32_t copyCount
    );

    bool CreateImageRaw(
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageCreateFlags flags,
        VkExtent3D extent,
        uint32_t arrayLayers,
        VkImage& image,
        VkDeviceMemory& memory
    );

    bool CreateImageViewRaw(
        VkImage image,
        VkFormat format,
        VkImageViewType type,
        VkImageAspectFlags aspect,
        uint32_t layers,
        VkImageView& view
    );

    bool CreateOneTimeCommand(VkCommandBuffer& commandBuffer);
    void DestroyOneTimeCommand(VkCommandBuffer commandBuffer) noexcept;

    bool UpdateFrameUniforms() noexcept;
    void UpdateCamera() noexcept;

    static Mat4 Identity() noexcept;
    static Mat4 Multiply(const Mat4& a, const Mat4& b) noexcept;
    static Mat4 MakeTranslation(const Vec4& p) noexcept;
    static Mat4 MakeRotation(const Vec4& r) noexcept;
    static Mat4 MakeScale(const Vec4& s) noexcept;
    static Mat4 MakeModel(const Transform& t) noexcept;
    static Mat4 MakeLookAt(
        float ex, float ey, float ez,
        float cx, float cy, float cz
    ) noexcept;
    static Mat4 MakePerspective(
        float fovRadians,
        float aspect,
        float nearPlane,
        float farPlane
    ) noexcept;

    bool RecordGeometry(const RenderQueue& queue, std::span<const Transform> transforms);
    bool RecordLighting();
    bool RecordPost();

    void DestroyPipelines() noexcept;
    void DestroyDescriptors() noexcept;
    void DestroyDemoMeshes() noexcept;
    void DestroyDefaultIBL() noexcept;
    void DestroyGBufferAttachments() noexcept;
    void DestroySwapchain() noexcept;

    uint32_t FindMemoryType(
        uint32_t typeBits,
        VkMemoryPropertyFlags properties
    ) const noexcept;

    bool LoadOrFallbackMesh(
        DemoMeshSlot slot,
        DemoCpuMesh& mesh
    ) noexcept;

public:
    explicit VulkanRenderer(std::filesystem::path projectRoot = {})
        : projectRoot_(std::move(projectRoot)) {}

    ~VulkanRenderer() override {
        Shutdown();
    }

    bool Initialize(ANativeWindow*) override;
    bool BeginFrame() override;
    void EndFrame() override;

    bool RecreateSwapchain(ANativeWindow*) override;
    void ReleaseSurface() noexcept override;

    void DrawRenderQueue(
        const RenderQueue&,
        std::span<const Transform>
    ) override;

    void Shutdown() noexcept override;

    uint64_t CreateOffscreenRenderTarget(
        uint32_t,
        uint32_t
    ) override;

    bool ResizeOffscreenRenderTarget(
        uint64_t,
        uint32_t,
        uint32_t
    ) override;

    uint64_t GetOffscreenColorHandle(
        uint64_t
    ) const noexcept override;
};

}
