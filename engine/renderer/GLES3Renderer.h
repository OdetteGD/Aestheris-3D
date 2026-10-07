#pragma once

#include "IAetherisRenderer.h"
#include "AetherisValidationTracker.h"
#include "engine/assets/DemoMesh.h"
#include "engine/core/Std140.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <span>

namespace aetheris {

class GLES3Renderer final : public IAetherisRenderer {
public:
    enum class QualityTier : uint8_t {
        Tier1Framebuffer0 = 1,
        Tier2Safe3D = 2,
        Tier3Advanced = 3
    };

private:
    struct GpuMesh final {
        GLuint vao{};
        GLuint vbo{};
        GLuint ibo{};
        GLsizei indexCount{};
        bool valid{};
    };

    struct CameraState final {
        Mat4 view{};
        Mat4 proj{};
        Mat4 viewProj{};
        Mat4 invViewProj{};
        Mat4 lightViewProj{};
        Vec4 position{};
        Vec4 sunDirection{};
    };

    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLContext context_{EGL_NO_CONTEXT};
    EGLSurface surface_{EGL_NO_SURFACE};
    EGLConfig config_{};
    ANativeWindow* window_{};

    bool initialized_{};
    bool begun_{};
    bool deferredReady_{};
    bool tier2Ready_{};
    bool tier3Ready_{};
    bool contextLost_{};

    uint32_t surfaceWidth_{};
    uint32_t surfaceHeight_{};
    uint32_t stableExtentFrames_{};

    uint32_t offscreenWidth_{};
    uint32_t offscreenHeight_{};
    GLuint offscreenFbo_{};
    GLuint offscreenColor_{};
    GLuint offscreenDepthStencil_{};

    GLuint shadowFbo_{};
    GLuint shadowDepth_{};
    uint32_t shadowSize_{1024};

    GLuint frameUbo_{};
    GLuint defaultAlbedo_{};
    GLuint defaultNormal_{};
    GLuint defaultOrm_{};
    GLuint skyVao_{};

    GLuint skyProgram_{};
    GLuint shadowProgram_{};
    GLuint tier3Program_{};
    GLuint tier2Program_{};
    GLuint gizmoProgram_{};
    GLuint gizmoVao_{};
    GLuint gizmoVbo_{};
    uint32_t gizmoVertexCount_{};

    std::array<GpuMesh, static_cast<size_t>(DemoMeshSlot::Count)> meshes_{};

    AetherisValidationTracker validator_{};
    QualityTier tier_{QualityTier::Tier1Framebuffer0};

    std::filesystem::path projectRoot_{};

    CameraState camera_{};
    std140::DeferredFrameBlock frameBlock_{};
    float elapsedSeconds_{};

    bool CreateContext();
    bool CreateSurface();
    bool RecreateContextAndSurface();
    void DestroyEGLSurface() noexcept;
    void DestroyGLResources() noexcept;
    void DestroyDeferredResources() noexcept;

    bool ValidateCurrentContext() noexcept;
    bool QuerySurfaceExtent(uint32_t& width, uint32_t& height) noexcept;
    bool EnsureOffscreen(uint32_t width, uint32_t height);
    bool EnsureFallbackTextures();
    bool EnsureMeshes();
    bool EnsureFrameUbo();
    bool EnsureTier2Program();
    bool EnsureTier3Programs();
    bool EnsureShadowTarget();
    bool EnsureGizmoProgram();

    bool CreateProgramFromFiles(
        const std::filesystem::path& vertexPath,
        const std::filesystem::path& fragmentPath,
        const char* builtInVertex,
        const char* builtInFragment,
        AetherisValidationTracker::Pass pass,
        GLuint& outProgram
    );

    bool CreateShader(
        GLenum type,
        const char* source,
        const char* label,
        AetherisValidationTracker::Pass pass,
        GLuint& outShader
    );

    bool UploadMesh(
        DemoMeshSlot slot,
        const DemoCpuMesh& cpu,
        AetherisValidationTracker::Pass pass
    );

    void UpdateCamera() noexcept;
    void UpdateFrameUbo() noexcept;

    bool BeginOffscreenPass() noexcept;
    bool DrawTier3(
        const RenderQueue& queue,
        std::span<const Transform> transforms
    ) noexcept;
    bool DrawTier2(
        const RenderQueue& queue,
        std::span<const Transform> transforms
    ) noexcept;
    void DrawTier1() noexcept;
    bool DrawSky() noexcept;
    bool DrawShadowMap(
        const RenderQueue& queue,
        std::span<const Transform> transforms
    ) noexcept;
    bool DrawOpaqueTier3(
        const RenderQueue& queue,
        std::span<const Transform> transforms
    ) noexcept;
    bool DrawOpaqueTier2(
        const RenderQueue& queue,
        std::span<const Transform> transforms
    ) noexcept;
    void DrawGizmoOverlay() noexcept;

    static Mat4 Identity() noexcept;
    static Mat4 Multiply(const Mat4& a, const Mat4& b) noexcept;
    static Mat4 Perspective(float fovRadians, float aspect, float nearPlane, float farPlane) noexcept;
    static Mat4 LookAt(const Vec4& eye, const Vec4& target, const Vec4& up) noexcept;
    static Mat4 Inverse(const Mat4& m) noexcept;
    static Mat4 ModelFromTransform(const Transform& transform) noexcept;
    static Vec4 Normalize4(const Vec4& v) noexcept;

    static const char* TierName(QualityTier tier) noexcept;

public:
    explicit GLES3Renderer(
        std::filesystem::path projectRoot = {}
    ) noexcept;
    ~GLES3Renderer() override { Shutdown(); }

    bool Initialize(ANativeWindow*) override;
    bool EnsureDeferredResources() override;
    void AbortDeferredResources() noexcept override;
    bool IsReady() const noexcept override {
        return initialized_ && surface_ != EGL_NO_SURFACE;
    }
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

    QualityTier CurrentTier() const noexcept { return tier_; }
};

} // namespace aetheris
