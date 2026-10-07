#pragma once
#include "engine/renderer/IAetherisRenderer.h"
#include <memory>
#include <mutex>
namespace aetheris {
class EngineCore final {
 std::unique_ptr<IAetherisRenderer> activeRenderer_;
 RenderAPI activeApi_{RenderAPI::VULKAN};
 SceneSnapshot scene_;
 mutable std::mutex mutex_;
 bool frameActive_{};
 std::unique_ptr<IAetherisRenderer> MakeRenderer(RenderAPI);
 bool CreateRendererLocked(RenderAPI,ANativeWindow*);
 EngineCore()=default;
public:
 static EngineCore& Instance() noexcept;
 bool Initialize(RenderAPI,ANativeWindow*);
 bool SwitchGraphicsAPI(RenderAPI,ANativeWindow*);
 bool BeginFrame(); void Draw(const RenderQueue&); void EndFrame();
 void OnSurfaceChanged(ANativeWindow*);
 void OnSurfaceDestroyed() noexcept; void ApplyGizmo(const GizmoCommand&);
 void Shutdown() noexcept;
 RenderAPI ActiveAPI() const noexcept; SceneSnapshot SnapshotScene() const;
};
}