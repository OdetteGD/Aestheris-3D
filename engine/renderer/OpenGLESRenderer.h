#pragma once
#include "IAetherisRenderer.h"
namespace aetheris { class OpenGLESRenderer final:public IAetherisRenderer{
public:bool Initialize(ANativeWindow*)override;bool BeginFrame()override;void EndFrame()override;bool RecreateSwapchain(ANativeWindow*)override;void ReleaseSurface()noexcept override;void DrawRenderQueue(const RenderQueue&)override;void Shutdown()noexcept override;uint64_t CreateOffscreenRenderTarget(uint32_t,uint32_t)override;bool ResizeOffscreenRenderTarget(uint64_t,uint32_t,uint32_t)override;uint64_t GetOffscreenColorHandle(uint64_t)const noexcept override;
};}