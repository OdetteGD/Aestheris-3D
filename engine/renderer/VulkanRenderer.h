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

    VkImage ssaoImage_{};
    VkImageView ssaoView_{};
    VkDeviceMemory ssaoMemory_{};
    VkFormat ssaoFormat_{VK_FORMAT_R8_UNORM};

    VkImage environmentImage_{};
    VkImageView environmentView_{};
    VkDeviceMemory environmentMemory_{};
    uint32_t environmentWidth_{};
    uint32_t environmentHeight_{};
    uint32_t environmentMipLevels_{1};

    VkRenderPass geometryPass_{};
    VkRenderPass pass_{};
    VkRenderPass postPass_{};
    VkRenderPass ssaoPass_{};
    VkRenderPass bloomDownPass_{};
    VkRenderPass bloomUpPass_{};

    std::vector<VkImage> images_{};
    std::vector<VkImageView> views_{};
    std::vector<VkFramebuffer> framebuffers_{};
    std::vector<VkFramebuffer> postFramebuffers_{};
    VkFramebuffer geometryFramebuffer_{};
    VkFramebuffer ssaoFramebuffer_{};
    VkImage bloomA_{};
    VkImageView bloomAView_{};
    VkDeviceMemory bloomAMemory_{};
    VkImage bloomB_{};
    VkImageView bloomBView_{};
    VkDeviceMemory bloomBMemory_{};
    std::array<VkFramebuffer, 1> bloomDownFramebuffers_{};
    std::array<VkFramebuffer, 1> bloomUpFramebuffers_{};
    VkExtent2D bloomExtent_{};
    VkExtent2D ssaoExtent_{};

    std::array<Frame, Frames> frames_{};

    VkShaderModule geometryVert_{};
    VkShaderModule geometryFrag_{};
    VkShaderModule fullscreenVert_{};
    VkShaderModule lightingFrag_{};
    VkShaderModule postFrag_{};
    VkShaderModule shadowVert_{};
    VkShaderModule shadowFrag_{};
    VkShaderModule ssaoFrag_{};
    VkShaderModule bloomDownVert_{};
    VkShaderModule bloomUpVert_{};
    VkShaderModule bloomDownFrag_{};
    VkShaderModule bloomUpFrag_{};

    VkPipelineLayout geometryLayout_{};
    VkPipelineLayout lightingLayout_{};
    VkPipelineLayout postLayout_{};
    VkPipelineLayout shadowLayout_{};
    VkPipelineLayout ssaoLayout_{};

    VkPipeline geometryPipeline_{};
    VkPipeline lightingPipeline_{};
    VkPipeline postPipeline_{};
    VkPipeline shadowPipeline_{};
    VkPipeline ssaoPipeline_{};
    VkPipeline bloomDownPipeline_{};
    VkPipeline bloomUpPipeline_{};

    VkDescriptorSetLayout lightingInputLayout_{};
    VkDescriptorSetLayout lightingFrameLayout_{};
    VkDescriptorSetLayout postSetLayout_{};
    VkDescriptorSetLayout materialSetLayout_{};
    VkDescriptorSetLayout shadowSetLayout_{};
    VkDescriptorSetLayout ssaoSetLayout_{};
    VkDescriptorPool descriptorPool_{};

    VkDescriptorSet lightingInputSet_{};
    VkDescriptorSet lightingFrameSet_{};
    VkDescriptorSet postSet_{};
    VkDescriptorSet bloomDownSet_{};
    VkDescriptorSet bloomUpSet_{};
    std::array<VkDescriptorSet, kMaxDemoMeshes> materialSets_{};
    VkDescriptorSet shadowSet_{};
    VkDescriptorSet ssaoSet_{};

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

    VkImage csmImage_{};
    VkImageView csmArrayView_{};
    std::array<VkImageView, 3> csmLayerViews_{};
    VkDeviceMemory csmMemory_{};
    VkSampler shadowSampler_{};
    VkRenderPass shadowPass_{};
    std::array<VkFramebuffer, 3> shadowFramebuffers_{};
    VkExtent2D csmExtent_{1024, 1024};

    GpuBuffer frameUbo_{};
    VkDeviceSize frameUboStride_{64};

    struct MaterialGpu final {
        GpuBuffer uniform{};
        VkImage albedo{};
        VkImageView albedoView{};
        VkDeviceMemory albedoMemory{};
        VkImage normal{};
        VkImageView normalView{};
        VkDeviceMemory normalMemory{};
        VkImage orm{};
        VkImageView ormView{};
        VkDeviceMemory ormMemory{};
    };
    std::array<MaterialGpu, kMaxDemoMeshes> materials_{};

    GpuBuffer csmUbo_{};
    VkDeviceSize csmUboStride_{256};

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
    Mat4 invViewProj_{};
    std::array<Mat4, 3> csmMatrices_{};
    std::array<float, 3> csmSplits_{};

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
    bool CreateMaterialResources();
    bool CreateCSMResources();
    bool CreateSSAOTarget();
    bool CreateBloomResources();
    bool LoadOfflineEnvironment();
    bool CreateKtx2Cube(
        const std::filesystem::path& path,
        VkImage& image,
        VkDeviceMemory& memory,
        VkImageView& view,
        uint32_t& mipLevels
    );
    bool CreateHdrEnvironmentCube(
        const std::filesystem::path& path,
        VkImage& image,
        VkDeviceMemory& memory,
        VkImageView& view
    );
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

    bool CreateAttachmentImage(
        VkFormat,
        VkImageUsageFlags,
        VkImage&,
        VkDeviceMemory&,
        VkImageView&,
        VkImageAspectFlags
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

    bool CreateImageRawMip(
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageCreateFlags flags,
        VkExtent3D extent,
        uint32_t mipLevels,
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

    bool CreateImageViewRawMip(
        VkImage image,
        VkFormat format,
        VkImageViewType type,
        VkImageAspectFlags aspect,
        uint32_t mipLevels,
        uint32_t layers,
        VkImageView& view
    );

    bool CreateOneTimeCommand(VkCommandBuffer& commandBuffer);
    bool CreateProceduralMaterialTexture(uint32_t materialId, uint32_t kind, VkImage& image, VkDeviceMemory& memory, VkImageView& view);
    void DestroyOneTimeCommand(VkCommandBuffer commandBuffer) noexcept;

    bool UpdateFrameUniforms() noexcept;
    bool UpdateCSMUniforms() noexcept;
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
    static Mat4 Inverse(const Mat4& m) noexcept;
    static Mat4 MakeLookAtLight(const Vec4& direction, float cx, float cy, float cz) noexcept;
    static Mat4 MakeOrthographic(float l, float r, float b, float t, float n, float f) noexcept;
    static Mat4 MakePerspective(
        float fovRadians,
        float aspect,
        float nearPlane,
        float farPlane
    ) noexcept;

    bool RecordShadowMaps(const RenderQueue& queue, std::span<const Transform> transforms) noexcept;
    bool RecordGeometry(const RenderQueue& queue, std::span<const Transform> transforms);
    bool RecordLighting();
    bool RecordPost();

    void DestroyPipelines() noexcept;
    void DestroyDescriptors() noexcept;
    void DestroyDemoMeshes() noexcept;
    void DestroyDefaultIBL() noexcept;
    void DestroyMaterialResources() noexcept;
    void DestroyCSMResources() noexcept;
    void DestroySSAOTarget() noexcept;
    void DestroyEnvironment() noexcept;
    void DestroyBloomResources() noexcept;
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
