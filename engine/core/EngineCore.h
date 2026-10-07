#pragma once
#include "engine/renderer/IAetherisRenderer.h"
#include <memory>
#include <mutex>
#include <filesystem>
#include <atomic>
#include <thread>
namespace aetheris {
class EngineCore final {
 std::unique_ptr<IAetherisRenderer> activeRenderer_;
 RenderAPI activeApi_{RenderAPI::VULKAN};
 SceneSnapshot scene_;
 mutable std::mutex mutex_;
 bool frameActive_{};
 std::atomic_bool renderStop_{true};
 std::atomic_bool surfaceReady_{false};
 std::thread renderThread_{};
 std::filesystem::path projectRoot_{};
 std::unique_ptr<IAetherisRenderer> MakeRenderer(RenderAPI);
 bool CreateRendererLocked(RenderAPI,ANativeWindow*);
 void RenderLoop() noexcept;
 void StartRenderLoopLocked();
 void StopRenderLoop() noexcept;
 void RenderOneFrameLocked();
 EngineCore()=default;
public:
 static EngineCore& Instance() noexcept;
 bool Initialize(RenderAPI,ANativeWindow*);
 bool SwitchGraphicsAPI(RenderAPI,ANativeWindow*);
 void SetProjectRoot(const std::filesystem::path& root);
 bool BeginFrame(); void Draw(const RenderQueue&); void EndFrame();
 void OnSurfaceChanged(ANativeWindow*);
 void OnSurfaceDestroyed() noexcept; void ApplyGizmo(const GizmoCommand&);
 void Shutdown() noexcept;
 RenderAPI ActiveAPI() const noexcept; SceneSnapshot SnapshotScene() const;
};
}