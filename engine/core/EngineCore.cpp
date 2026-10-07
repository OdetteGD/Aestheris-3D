#include "EngineCore.h"
#include "engine/renderer/VulkanRenderer.h"
#include "engine/renderer/OpenGLESRenderer.h"
#include "engine/core/AetherisLog.h"

#include <android/log.h>
#include <array>
#include <chrono>
#include <cinttypes>
#include <span>
#include <thread>
#include <unwind.h>

namespace aetheris {
namespace {
constexpr std::chrono::milliseconds kDeferredBootBudget{750};

_Unwind_Reason_Code TraceFrame(_Unwind_Context* context, void* opaque) noexcept {
    auto* depth = static_cast<unsigned*>(opaque);
    if (*depth >= 16u) return _URC_END_OF_STACK;
    const uintptr_t pc = _Unwind_GetIP(context);
    if (pc != 0u) {
        __android_log_print(
            ANDROID_LOG_ERROR,
            "AetherisEngine_Fatal",
            "native-stack[%u] pc=0x%" PRIxPTR,
            *depth,
            pc
        );
    }
    ++(*depth);
    return _URC_NO_REASON;
}

void FatalBoot(const char* stage, const char* reason) noexcept {
    __android_log_print(
        ANDROID_LOG_ERROR,
        "AetherisEngine_Fatal",
        "deferred boot failure stage=%s reason=%s",
        stage ? stage : "unknown",
        reason ? reason : "unknown"
    );
    unsigned depth = 0;
    _Unwind_Backtrace(TraceFrame, &depth);
}
}

void EngineCore::LogFatalBootFailureLocked(const char* stage, const char* reason) noexcept {
    FatalBoot(stage, reason);
}

void EngineCore::RenderOneFrameLocked() {
    if (!activeRenderer_ || state_ == EngineState::Uninitialized || frameActive_)
        return;

    if (state_ == EngineState::SurfaceReady) {
        if (!activeRenderer_->BeginFrame()) return;
        frameActive_ = true;
        static constexpr std::array<RenderItem,0> emptyItems{};
        activeRenderer_->DrawRenderQueue(
            RenderQueue(std::span<const RenderItem>(emptyItems)),
            std::span<const Transform>(scene_.transforms)
        );
        activeRenderer_->EndFrame();
        frameActive_ = false;
        firstSurfaceFramePresented_ = true;
        if (!assetBootFailed_) {
            state_ = EngineState::AllocatingAssets;
            assetBootStart_ = std::chrono::steady_clock::now();
        }
        return;
    }

    if (state_ == EngineState::AllocatingAssets) {
        const auto start = std::chrono::steady_clock::now();
        const bool ok = activeRenderer_->EnsureDeferredResources();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        );

        if (ok && elapsed <= kDeferredBootBudget) {
            state_ = EngineState::Rendering;
            AETHERIS_LOGI(
                "Engine state AllocatingAssets -> Rendering; boot=%lld ms",
                static_cast<long long>(elapsed.count())
            );
        } else {
            assetBootFailed_ = true;
            activeRenderer_->AbortDeferredResources();
            state_ = EngineState::SurfaceReady;
            LogFatalBootFailureLocked(
                elapsed > kDeferredBootBudget
                    ? "asset_timeout"
                    : "asset_allocation",
                elapsed > kDeferredBootBudget
                    ? "deferred Vulkan resource boot exceeded 750ms"
                    : "deferred Vulkan resource boot failed"
            );
        }
        return;
    }

    if (state_ != EngineState::Rendering)
        return;

    if (!activeRenderer_->BeginFrame()) return;
    frameActive_ = true;
    activeRenderer_->DrawRenderQueue(
        RenderQueue(std::span<const RenderItem>(scene_.renderItems)),
        std::span<const Transform>(scene_.transforms)
    );
    activeRenderer_->EndFrame();
    frameActive_ = false;
}

void EngineCore::RenderLoop() noexcept {
    using namespace std::chrono_literals;
    while (!renderStop_.load(std::memory_order_acquire)) {
        {
            std::scoped_lock lock(mutex_);
            RenderOneFrameLocked();
        }
        std::this_thread::sleep_for(16ms);
    }
}

void EngineCore::StartRenderLoopLocked() {
    if (renderThread_.joinable()) return;
    renderStop_.store(false, std::memory_order_release);
    renderThread_ = std::thread([this] { RenderLoop(); });
}

void EngineCore::StopRenderLoop() noexcept {
    renderStop_.store(true, std::memory_order_release);
    if (renderThread_.joinable()) {
        if (renderThread_.get_id() == std::this_thread::get_id())
            renderThread_.detach();
        else
            renderThread_.join();
    }
}

EngineCore& EngineCore::Instance() noexcept {
    static EngineCore engine;
    return engine;
}

std::unique_ptr<IAetherisRenderer> EngineCore::MakeRenderer(RenderAPI api) {
    if (api == RenderAPI::VULKAN) {
        return std::make_unique<VulkanRenderer>(projectRoot_);
    }

    return std::make_unique<OpenGLESRenderer>();
}

bool EngineCore::CreateRendererLocked(RenderAPI api, ANativeWindow* window) {
    if (!window) return false;
    auto renderer = MakeRenderer(api);
    if (!renderer || !renderer->Initialize(window)) return false;
    activeRenderer_ = std::move(renderer);
    activeApi_ = api;
    return true;
}

bool EngineCore::Initialize(RenderAPI api, ANativeWindow* window) {
    std::scoped_lock lock(mutex_);
    if (activeRenderer_)
        return state_ != EngineState::Uninitialized;
    if (!CreateRendererLocked(api, window))
        return false;
    state_ = EngineState::SurfaceReady;
    firstSurfaceFramePresented_ = false;
    assetBootFailed_ = false;
    StartRenderLoopLocked();
    return true;
}

bool EngineCore::SwitchGraphicsAPI(RenderAPI api, ANativeWindow* window) {
    std::scoped_lock lock(mutex_);
    if (!window || frameActive_ || state_ == EngineState::AllocatingAssets)
        return false;

    state_ = EngineState::Uninitialized;

    if (activeRenderer_ && activeApi_ == api) {
        const bool ok = activeRenderer_->RecreateSwapchain(window);
        state_ = ok ? EngineState::SurfaceReady : EngineState::Uninitialized;
        firstSurfaceFramePresented_ = false;
        assetBootFailed_ = false;
        return ok;
    }

    if (activeRenderer_) {
        activeRenderer_->Shutdown();
        activeRenderer_.reset();
    }

    auto tryCreate = [&](RenderAPI requested) -> bool {
        auto renderer = MakeRenderer(requested);
        if (!renderer || !renderer->Initialize(window)) return false;
        activeRenderer_ = std::move(renderer);
        activeApi_ = requested;
        return true;
    };

    if (!tryCreate(api)) {
        const RenderAPI fallback =
            api == RenderAPI::VULKAN ? RenderAPI::OPENGL_ES3 : RenderAPI::VULKAN;
        if (!tryCreate(fallback)) {
            LogFatalBootFailureLocked(
                "api_switch",
                "no renderer could bind the supplied Android surface"
            );
            return false;
        }
    }

    state_ = EngineState::SurfaceReady;
    firstSurfaceFramePresented_ = false;
    assetBootFailed_ = false;
    StartRenderLoopLocked();
    return true;
}

bool EngineCore::BeginFrame() {
    std::scoped_lock lock(mutex_);
    if (!activeRenderer_ || state_ != EngineState::Rendering || frameActive_)
        return false;
    frameActive_ = activeRenderer_->BeginFrame();
    return frameActive_;
}

void EngineCore::Draw(const RenderQueue& queue) {
    std::scoped_lock lock(mutex_);
    if (activeRenderer_ && state_ == EngineState::Rendering && frameActive_)
        activeRenderer_->DrawRenderQueue(
            queue,
            std::span<const Transform>(scene_.transforms)
        );
}

void EngineCore::EndFrame() {
    std::scoped_lock lock(mutex_);
    if (activeRenderer_ && frameActive_)
        activeRenderer_->EndFrame();
    frameActive_ = false;
}

void EngineCore::SetProjectRoot(const std::filesystem::path& root) {
    std::scoped_lock lock(mutex_);
    if (activeRenderer_ || frameActive_) return;
    projectRoot_ = root;
}

void EngineCore::OnSurfaceChanged(ANativeWindow* window) {
    std::scoped_lock lock(mutex_);
    if (!window || frameActive_) return;

    if (!activeRenderer_) {
        if (!demoWorldInitialized_)
            demoWorldInitialized_ = demoWorld_.Initialize(projectRoot_, scene_);

        if (!CreateRendererLocked(RenderAPI::VULKAN, window) &&
            !CreateRendererLocked(RenderAPI::OPENGL_ES3, window)) {
            state_ = EngineState::Uninitialized;
            LogFatalBootFailureLocked(
                "surface_boot",
                "device or swapchain bootstrap failed for the supplied ANativeWindow"
            );
            return;
        }

        state_ = EngineState::SurfaceReady;
        firstSurfaceFramePresented_ = false;
        assetBootFailed_ = false;
        StartRenderLoopLocked();
        return;
    }

    const bool ok = activeRenderer_->RecreateSwapchain(window);
    if (!ok) {
        state_ = EngineState::Uninitialized;
        LogFatalBootFailureLocked(
            "surface_recreate",
            "swapchain or safe presentation rebuild failed"
        );
        return;
    }

    state_ = EngineState::SurfaceReady;
    firstSurfaceFramePresented_ = false;
    assetBootFailed_ = false;
    StartRenderLoopLocked();
}

void EngineCore::OnSurfaceDestroyed() noexcept {
    StopRenderLoop();
    std::scoped_lock lock(mutex_);
    frameActive_ = false;
    state_ = EngineState::Uninitialized;
    firstSurfaceFramePresented_ = false;
    assetBootFailed_ = false;
    if (activeRenderer_) activeRenderer_->ReleaseSurface();
}

void EngineCore::ApplyGizmo(const GizmoCommand& c) {
    std::scoped_lock lock(mutex_);
    if (c.entity >= scene_.transforms.size()) return;
    auto& t = scene_.transforms[c.entity];

    if (c.type == GizmoCommand::Type::Translate) {
        t.position.x += c.delta.x;
        t.position.y += c.delta.y;
        t.position.z += c.delta.z;
    } else if (c.type == GizmoCommand::Type::Rotate) {
        t.rotation.x += c.delta.x;
        t.rotation.y += c.delta.y;
        t.rotation.z += c.delta.z;
    } else {
        t.scale.x *= c.delta.x;
        t.scale.y *= c.delta.y;
        t.scale.z *= c.delta.z;
    }
    ++scene_.revision;
}

void EngineCore::Shutdown() noexcept {
    StopRenderLoop();
    std::scoped_lock lock(mutex_);
    frameActive_ = false;
    state_ = EngineState::Uninitialized;
    firstSurfaceFramePresented_ = false;
    assetBootFailed_ = false;
    if (activeRenderer_) {
        activeRenderer_->Shutdown();
        activeRenderer_.reset();
    }
    demoWorldInitialized_ = false;
    scene_.renderItems.clear();
    scene_.transforms.clear();
}

EngineState EngineCore::State() const noexcept {
    std::scoped_lock lock(mutex_);
    return state_;
}

RenderAPI EngineCore::ActiveAPI() const noexcept {
    std::scoped_lock lock(mutex_);
    return activeApi_;
}

SceneSnapshot EngineCore::SnapshotScene() const {
    std::scoped_lock lock(mutex_);
    return scene_;
}
