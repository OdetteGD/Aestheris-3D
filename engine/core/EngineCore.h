#pragma once
#include "engine/renderer/IAetherisRenderer.h"
#include "engine/world/DemoWorldInitializer.h"
#include <memory>
#include <mutex>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
namespace aetheris {

enum class EngineState : uint8_t { Uninitialized, SurfaceReady, AllocatingAssets, Rendering };
class EngineCore final {
 std::unique_ptr<IAetherisRenderer> activeRenderer_;
 RenderAPI activeApi_{RenderAPI::VULKAN};
 SceneSnapshot scene_;
 mutable std::recursive_mutex mutex_;
 bool frameActive_{};
 std::atomic_bool renderStop_{true};
 std::atomic_bool surfaceReady_{false};
 std::thread renderThread_{};
 DemoWorldInitializer demoWorld_{};
 bool demoWorldInitialized_{false};
 std::filesystem::path projectRoot_{};
 EngineState state_{EngineState::Uninitialized};
 bool firstSurfaceFramePresented_{};
 bool assetBootFailed_{};
 std::chrono::steady_clock::time_point assetBootStart_{};
 std::unique_ptr<IAetherisRenderer> MakeRenderer(RenderAPI);
 bool CreateRendererLocked(RenderAPI,ANativeWindow*);
 void RenderLoop() noexcept;
 void StartRenderLoopLocked();
 void StopRenderLoop() noexcept;
 void RenderOneFrameLocked();
 void LogFatalBootFailureLocked(const char*,const char*) noexcept;
 EngineCore()=default;
public:
 static EngineCore& Instance() noexcept;
 bool Initialize(RenderAPI,ANativeWindow*);
 bool SwitchGraphicsAPI(RenderAPI,ANativeWindow*);
 void SetProjectRoot(const std::filesystem::path& root);
 bool BeginFrame(); void Draw(const RenderQueue&); void EndFrame();
 void OnSurfaceChanged(ANativeWindow*);
 void OnSurfaceDestroyed() noexcept;
 void ApplyGizmo(const GizmoCommand&);
 bool SetNodePosition(uint64_t nodeHandle, const Vec4& position) noexcept;
 bool SetNodeRotation(uint64_t nodeHandle, const Vec4& rotation) noexcept;
 bool SetNodeScale(uint64_t nodeHandle, const Vec4& scale) noexcept;
 bool GetNodePosition(uint64_t nodeHandle, Vec4& outPosition) const noexcept;
 uint64_t GetNodeHandle(uint32_t entityIndex) const noexcept;
 void Shutdown() noexcept;
 EngineState State() const noexcept;
 RenderAPI ActiveAPI() const noexcept; SceneSnapshot SnapshotScene() const;
};
}