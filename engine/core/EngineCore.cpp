#include "EngineCore.h"
#include "engine/renderer/VulkanRenderer.h"
#include "engine/renderer/OpenGLESRenderer.h"
#include <array>
#include <chrono>
#include <span>

namespace aetheris {

void EngineCore::RenderOneFrameLocked() {
    if (!activeRenderer_ || frameActive_) return;
    if (!activeRenderer_->BeginFrame()) return;
    frameActive_ = true;
    static constexpr std::array<RenderItem, 0> kEmptyItems{};
    activeRenderer_->DrawRenderQueue(RenderQueue(std::span<const RenderItem>(kEmptyItems)));
    activeRenderer_->EndFrame();
    frameActive_ = false;
}

void EngineCore::RenderLoop() noexcept {
    using namespace std::chrono_literals;
    while (!renderStop_.load(std::memory_order_acquire)) {
        {
            std::scoped_lock l(mutex_);
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
        if (renderThread_.get_id() == std::this_thread::get_id()) renderThread_.detach();
        else renderThread_.join();
    }
}

EngineCore& EngineCore::Instance() noexcept {
    static EngineCore e;
    return e;
}

std::unique_ptr<IAetherisRenderer> EngineCore::MakeRenderer(RenderAPI a) {
    if (a == RenderAPI::VULKAN) {
        return std::make_unique<VulkanRenderer>(projectRoot_);
    }
    return std::make_unique<OpenGLESRenderer>();
}

bool EngineCore::CreateRendererLocked(RenderAPI a, ANativeWindow* w) {
    if (!w) return false;
    auto r = MakeRenderer(a);
    if (!r || !r->Initialize(w)) return false;
    activeRenderer_ = std::move(r);
    activeApi_ = a;
    return true;
}

bool EngineCore::Initialize(RenderAPI a, ANativeWindow* w) {
    std::scoped_lock l(mutex_);
    if (activeRenderer_) return true;
    if (!CreateRendererLocked(a, w)) return false;
    StartRenderLoopLocked();
    return true;
}

bool EngineCore::SwitchGraphicsAPI(RenderAPI a, ANativeWindow* w) {
    std::scoped_lock l(mutex_);
    if (frameActive_ || !w) return false;
    if (activeRenderer_ && activeApi_ == a) {
        return activeRenderer_->RecreateSwapchain(w);
    }

    SceneSnapshot saved = scene_;
    if (activeRenderer_) {
        activeRenderer_->Shutdown();
        activeRenderer_.reset();
    }

    scene_ = std::move(saved);
    if (CreateRendererLocked(a, w)) {
        StartRenderLoopLocked();
        return true;
    }

    // Preserve the active renderer if a requested API fails to initialize.
    if (a != RenderAPI::VULKAN) {
        if (CreateRendererLocked(RenderAPI::VULKAN, w)) return true;
    } else {
        if (CreateRendererLocked(RenderAPI::OPENGL_ES3, w)) return true;
    }
    return false;
}

bool EngineCore::BeginFrame() {
    std::scoped_lock l(mutex_);
    if (!activeRenderer_ || frameActive_) return false;
    return frameActive_ = activeRenderer_->BeginFrame();
}

void EngineCore::Draw(const RenderQueue& q) {
    std::scoped_lock l(mutex_);
    if (activeRenderer_ && frameActive_) activeRenderer_->DrawRenderQueue(q);
}

void EngineCore::EndFrame() {
    std::scoped_lock l(mutex_);
    if (activeRenderer_ && frameActive_) activeRenderer_->EndFrame();
    frameActive_ = false;
}

void EngineCore::SetProjectRoot(const std::filesystem::path& root) {
    std::scoped_lock l(mutex_);
    if (frameActive_ || activeRenderer_) return;
    projectRoot_ = root;
}

void EngineCore::OnSurfaceChanged(ANativeWindow* w) {
    std::scoped_lock l(mutex_);
    if (!w || frameActive_) return;

    if (!activeRenderer_) {
        // SurfaceView.surfaceCreated is the first legal point at which the native
        // Android window exists. Create the renderer only from this callback.
        if (!CreateRendererLocked(RenderAPI::VULKAN, w)) {
            CreateRendererLocked(RenderAPI::OPENGL_ES3, w);
        }
        if (activeRenderer_) StartRenderLoopLocked();
        return;
    }

    if (!activeRenderer_->RecreateSwapchain(w)) {
        // Drop the stale surface so the next SurfaceView callback can bind a fresh one.
        activeRenderer_->ReleaseSurface();
    }
    if (activeRenderer_) StartRenderLoopLocked();
}

void EngineCore::OnSurfaceDestroyed() noexcept {
    StopRenderLoop();
    std::scoped_lock l(mutex_);
    frameActive_ = false;
    if (activeRenderer_) activeRenderer_->ReleaseSurface();
}

void EngineCore::ApplyGizmo(const GizmoCommand& c) {
    std::scoped_lock l(mutex_);
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
    std::scoped_lock l(mutex_);
    frameActive_ = false;
    if (activeRenderer_) {
        activeRenderer_->Shutdown();
        activeRenderer_.reset();
    }
}

RenderAPI EngineCore::ActiveAPI() const noexcept {
    std::scoped_lock l(mutex_);
    return activeApi_;
}

SceneSnapshot EngineCore::SnapshotScene() const {
    std::scoped_lock l(mutex_);
    return scene_;
}

} // namespace aetheris
