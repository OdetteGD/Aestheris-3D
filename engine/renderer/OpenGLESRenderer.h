#pragma once
#include "IAetherisRenderer.h"
#include <span>
#include <EGL/egl.h>
#include <cstdint>

namespace aetheris {

class OpenGLESRenderer final : public IAetherisRenderer {
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLContext context_{EGL_NO_CONTEXT};
    EGLSurface surface_{EGL_NO_SURFACE};
    EGLConfig config_{};
    ANativeWindow* window_{};
    bool initialized_{};
    bool begun_{};

    bool CreateContext();
    bool CreateSurface();
    void DestroyEGLSurface() noexcept;

public:
    OpenGLESRenderer() = default;
    ~OpenGLESRenderer() override { Shutdown(); }

    bool Initialize(ANativeWindow*) override;
    bool EnsureDeferredResources() override { return true; }
    void AbortDeferredResources() noexcept override {}
    bool IsReady() const noexcept override { return initialized_ && surface_ != EGL_NO_SURFACE; }
    bool BeginFrame() override;
    void EndFrame() override;
    bool RecreateSwapchain(ANativeWindow*) override;
    void ReleaseSurface() noexcept override;
    void DrawRenderQueue(const RenderQueue&, std::span<const Transform>) override;
    void Shutdown() noexcept override;

    uint64_t CreateOffscreenRenderTarget(uint32_t, uint32_t) override;
    bool ResizeOffscreenRenderTarget(uint64_t, uint32_t, uint32_t) override;
    uint64_t GetOffscreenColorHandle(uint64_t) const noexcept override;
};

} // namespace aetheris
