#pragma once
#include "engine/core/AetherisTypes.h"
#include <span>
namespace aetheris {
class IAetherisRenderer {
public:
 virtual ~IAetherisRenderer()=default;
 IAetherisRenderer(const IAetherisRenderer&)=delete;
 IAetherisRenderer& operator=(const IAetherisRenderer&)=delete;
 virtual bool Initialize(ANativeWindow*)=0;
 virtual bool EnsureDeferredResources()=0;
 virtual bool IsReady() const noexcept=0;
 virtual bool BeginFrame()=0;
 virtual void EndFrame()=0;
 virtual bool RecreateSwapchain(ANativeWindow*)=0;
 virtual void ReleaseSurface() noexcept=0;
 virtual void DrawRenderQueue(const RenderQueue&, std::span<const Transform>)=0;
 virtual void Shutdown() noexcept=0;
 virtual uint64_t CreateOffscreenRenderTarget(uint32_t,uint32_t)=0;
 virtual bool ResizeOffscreenRenderTarget(uint64_t,uint32_t,uint32_t)=0;
 virtual uint64_t GetOffscreenColorHandle(uint64_t) const noexcept=0;
protected: IAetherisRenderer()=default;
};
}