#include "CSharpScriptEngine.h"

#include "engine/core/AetherisLog.h"
#include "engine/core/EngineCore.h"

#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>

namespace aetheris {

namespace {

constexpr char kTag[] = "AetherisEditor_GLES";
constexpr char kMonoLibrary[] = "libmonosgen-2.0.so";
constexpr uint32_t kMaxAssemblyBytes = 16u * 1024u * 1024u;

std::atomic<float> gTouchX{0.0f};
std::atomic<float> gTouchY{0.0f};
std::atomic<int32_t> gTouchPhase{0};
std::atomic<float> gPreviousTouchX{0.0f};
std::atomic<float> gPreviousTouchY{0.0f};
std::atomic_bool gTouchHasPrevious{false};

template <typename T>
T ResolveSymbol(
    void* library,
    const char* name
) noexcept {
    if (!library || !name)
        return nullptr;

    return reinterpret_cast<T>(
        dlsym(library, name)
    );
}

} // namespace

CSharpScriptEngine& CSharpScriptEngine::Instance() noexcept {
    static CSharpScriptEngine engine;
    return engine;
}

bool CSharpScriptEngine::ResolveMonoApi() noexcept {
    mono_jit_init_version_ =
        ResolveSymbol<fn_mono_jit_init_version>(
            monoLibrary_,
            "mono_jit_init_version"
        );

    mono_jit_cleanup_ =
        ResolveSymbol<fn_mono_jit_cleanup>(
            monoLibrary_,
            "mono_jit_cleanup"
        );

    mono_thread_attach_ =
        ResolveSymbol<fn_mono_thread_attach>(
            monoLibrary_,
            "mono_thread_attach"
        );

    mono_domain_set_ =
        ResolveSymbol<fn_mono_domain_set>(
            monoLibrary_,
            "mono_domain_set"
        );

    mono_image_open_from_data_full_ =
        ResolveSymbol<fn_mono_image_open_from_data_full>(
            monoLibrary_,
            "mono_image_open_from_data_full"
        );

    mono_image_close_ =
        ResolveSymbol<fn_mono_image_close>(
            monoLibrary_,
            "mono_image_close"
        );

    mono_assembly_load_from_full_ =
        ResolveSymbol<fn_mono_assembly_load_from_full>(
            monoLibrary_,
            "mono_assembly_load_from_full"
        );

    mono_assembly_get_image_ =
        ResolveSymbol<fn_mono_assembly_get_image>(
            monoLibrary_,
            "mono_assembly_get_image"
        );

    mono_class_from_name_ =
        ResolveSymbol<fn_mono_class_from_name>(
            monoLibrary_,
            "mono_class_from_name"
        );

    mono_class_get_method_from_name_ =
        ResolveSymbol<fn_mono_class_get_method_from_name>(
            monoLibrary_,
            "mono_class_get_method_from_name"
        );

    mono_runtime_invoke_ =
        ResolveSymbol<fn_mono_runtime_invoke>(
            monoLibrary_,
            "mono_runtime_invoke"
        );

    return
        mono_jit_init_version_ &&
        mono_jit_cleanup_ &&
        mono_thread_attach_ &&
        mono_domain_set_ &&
        mono_image_open_from_data_full_ &&
        mono_image_close_ &&
        mono_assembly_load_from_full_ &&
        mono_assembly_get_image_ &&
        mono_class_from_name_ &&
        mono_class_get_method_from_name_ &&
        mono_runtime_invoke_;
}

bool CSharpScriptEngine::MountAssembly() {
    const std::filesystem::path path =
        projectRoot_ /
        "assets/csharp/sdk/AestherisCore.dll";

    std::ifstream file(
        path,
        std::ios::binary |
        std::ios::ate
    );

    if (!file) {
        AETHERIS_LOGW(
            "C# SDK assembly not found: %s",
            path.string().c_str()
        );
        return false;
    }

    const std::streamoff size =
        file.tellg();

    if (size <= 0 ||
        static_cast<uint64_t>(size) >
            static_cast<uint64_t>(kMaxAssemblyBytes)) {
        AETHERIS_LOGE(
            "C# SDK assembly size rejected: %lld bytes",
            static_cast<long long>(size)
        );
        return false;
    }

    assemblyBytes_.resize(
        static_cast<size_t>(size)
    );

    file.seekg(0, std::ios::beg);

    file.read(
        reinterpret_cast<char*>(assemblyBytes_.data()),
        size
    );

    if (!file) {
        assemblyBytes_.clear();
        AETHERIS_LOGE(
            "C# SDK assembly read failed: %s",
            path.string().c_str()
        );
        return false;
    }

    AETHERIS_LOGI(
        "C# SDK assembly mounted: %s bytes=%zu",
        path.string().c_str(),
        assemblyBytes_.size()
    );

    if (!runtimeAvailable_)
        return true;

    MonoImageOpenStatus status{};
    MonoImage* image =
        mono_image_open_from_data_full_(
            reinterpret_cast<char*>(
                assemblyBytes_.data()
            ),
            static_cast<uint32_t>(
                assemblyBytes_.size()
            ),
            0,
            &status,
            0
        );

    if (!image) {
        AETHERIS_LOGE(
            "Mono image open failed status=%d",
            status
        );
        return false;
    }

    MonoAssembly* assembly =
        mono_assembly_load_from_full_(
            image,
            "AestherisCore.dll",
            &status,
            0
        );

    if (!assembly) {
        mono_image_close_(image);
        AETHERIS_LOGE(
            "Mono assembly load failed status=%d",
            status
        );
        return false;
    }

    assembly_ = assembly;
    AETHERIS_LOGI(
        "Mono assembly loaded from mounted AestherisCore.dll"
    );
    return true;
}

bool CSharpScriptEngine::BindScriptRuntime() {
    if (!assembly_)
        return false;

    MonoImage* image =
        mono_assembly_get_image_(
            assembly_
        );

    if (!image)
        return false;

    scriptRuntimeClass_ =
        mono_class_from_name_(
            image,
            "Aestheris.Scripting",
            "ScriptRuntime"
        );

    if (!scriptRuntimeClass_)
        return false;

    initializeMethod_ =
        mono_class_get_method_from_name_(
            scriptRuntimeClass_,
            "Initialize",
            0
        );

    updateMethod_ =
        mono_class_get_method_from_name_(
            scriptRuntimeClass_,
            "Update",
            1
        );

    if (!initializeMethod_ || !updateMethod_)
        return false;

    MonoObject* exception = nullptr;
    mono_runtime_invoke_(
        initializeMethod_,
        nullptr,
        nullptr,
        &exception
    );

    if (exception) {
        AETHERIS_LOGE(
            "C# ScriptRuntime.Initialize raised a managed exception"
        );
        return false;
    }

    AETHERIS_LOGI(
        "C# ScriptRuntime initialized"
    );
    return true;
}

void CSharpScriptEngine::DisableScript(
    const char* reason
) noexcept {
    scriptDisabled_ = true;
    scriptAvailable_ = false;

    AETHERIS_LOGW(
        "C# scripting disabled: %s",
        reason ? reason : "unknown"
    );
}

bool CSharpScriptEngine::Initialize(
    const std::filesystem::path& projectRoot
) {
    if (initialized_ &&
        projectRoot_ == projectRoot)
        return true;

    Shutdown();

    projectRoot_ = projectRoot;
    initialized_ = true;
    scriptDisabled_ = false;
    scriptAvailable_ = false;
    runtimeAvailable_ = false;

    monoLibrary_ =
        dlopen(
            kMonoLibrary,
            RTLD_NOW |
            RTLD_GLOBAL
        );

    if (!monoLibrary_) {
        AETHERIS_LOGW(
            "Mono runtime unavailable; C# assembly remains mounted but native scripting stays disabled"
        );
        MountAssembly();
        return true;
    }

    if (!ResolveMonoApi()) {
        AETHERIS_LOGE(
            "Mono runtime loaded but the required embedding symbols are unavailable"
        );
        dlclose(monoLibrary_);
        monoLibrary_ = nullptr;
        MountAssembly();
        return true;
    }

    rootDomain_ =
        mono_jit_init_version_(
            "Aetheris",
            "v4.0.30319"
        );

    if (!rootDomain_) {
        AETHERIS_LOGE(
            "mono_jit_init_version failed"
        );
        dlclose(monoLibrary_);
        monoLibrary_ = nullptr;
        MountAssembly();
        return true;
    }

    mono_thread_attach_(rootDomain_);
    mono_domain_set_(rootDomain_, 0);
    runtimeAvailable_ = true;

    if (!MountAssembly()) {
        DisableScript(
            "AestherisCore.dll could not be mounted"
        );
        return true;
    }

    if (!BindScriptRuntime()) {
        DisableScript(
            "managed ScriptRuntime binding failed"
        );
        return true;
    }

    scriptAvailable_ = true;
    return true;
}

bool CSharpScriptEngine::Update(
    float deltaTime
) noexcept {
    if (!initialized_ ||
        !scriptAvailable_ ||
        scriptDisabled_ ||
        !updateMethod_ ||
        !rootDomain_ ||
        !mono_runtime_invoke_) {
        return false;
    }

    if (!std::isfinite(deltaTime) ||
        deltaTime <= 0.0f)
        return true;

    mono_thread_attach_(rootDomain_);

    void* arguments[1] = {
        &deltaTime
    };

    MonoObject* exception = nullptr;

    mono_runtime_invoke_(
        updateMethod_,
        nullptr,
        arguments,
        &exception
    );

    if (exception) {
        DisableScript(
            "managed ScriptRuntime.Update raised an exception"
        );
        return false;
    }

    return true;
}

void CSharpScriptEngine::Shutdown() noexcept {
    scriptAvailable_ = false;
    scriptDisabled_ = false;

    if (rootDomain_ &&
        mono_jit_cleanup_) {
        mono_jit_cleanup_(
            rootDomain_
        );
    }

    rootDomain_ = nullptr;
    assembly_ = nullptr;
    scriptRuntimeClass_ = nullptr;
    initializeMethod_ = nullptr;
    updateMethod_ = nullptr;

    if (monoLibrary_) {
        dlclose(monoLibrary_);
        monoLibrary_ = nullptr;
    }

    mono_jit_init_version_ = nullptr;
    mono_jit_cleanup_ = nullptr;
    mono_thread_attach_ = nullptr;
    mono_domain_set_ = nullptr;
    mono_image_open_from_data_full_ = nullptr;
    mono_image_close_ = nullptr;
    mono_assembly_load_from_full_ = nullptr;
    mono_assembly_get_image_ = nullptr;
    mono_class_from_name_ = nullptr;
    mono_class_get_method_from_name_ = nullptr;
    mono_runtime_invoke_ = nullptr;

    assemblyBytes_.clear();
    projectRoot_.clear();

    initialized_ = false;
    runtimeAvailable_ = false;
}

void CSharpScriptEngine::SetTouchStateFromJNI(
    int32_t phase,
    float x,
    float y
) noexcept {
    if (!std::isfinite(x) ||
        !std::isfinite(y)) {
        return;
    }

    const bool hadPrevious =
        gTouchHasPrevious.load(
            std::memory_order_acquire
        );

    if (phase == 0) {
        gTouchX.store(
            0.0f,
            std::memory_order_relaxed
        );
        gTouchY.store(
            0.0f,
            std::memory_order_relaxed
        );
        gTouchPhase.store(
            0,
            std::memory_order_release
        );
        gTouchHasPrevious.store(
            false,
            std::memory_order_release
        );
        return;
    }

    const float previousX =
        gPreviousTouchX.load(
            std::memory_order_relaxed
        );
    const float previousY =
        gPreviousTouchY.load(
            std::memory_order_relaxed
        );

    if (phase == 1 || !hadPrevious) {
        gTouchX.store(
            0.0f,
            std::memory_order_relaxed
        );
        gTouchY.store(
            0.0f,
            std::memory_order_relaxed
        );
    } else {
        gTouchX.store(
            x - previousX,
            std::memory_order_relaxed
        );
        gTouchY.store(
            y - previousY,
            std::memory_order_relaxed
        );
    }

    gPreviousTouchX.store(
        x,
        std::memory_order_relaxed
    );
    gPreviousTouchY.store(
        y,
        std::memory_order_relaxed
    );
    gTouchHasPrevious.store(
        phase != 3,
        std::memory_order_release
    );
    gTouchPhase.store(
        phase,
        std::memory_order_release
    );
}

CSharpScriptEngine::TouchSnapshot
CSharpScriptEngine::PollTouchState() noexcept {
    TouchSnapshot snapshot{};
    snapshot.deltaX =
        gTouchX.exchange(
            0.0f,
            std::memory_order_acq_rel
        );
    snapshot.deltaY =
        gTouchY.exchange(
            0.0f,
            std::memory_order_acq_rel
        );
    snapshot.phase =
        gTouchPhase.exchange(
            0,
            std::memory_order_acq_rel
        );
    return snapshot;
}

extern "C" {

uint64_t Aetheris_GetNodeHandle(
    uint32_t entityIndex
) noexcept {
    const auto& engine =
        EngineCore::Instance();

    const SceneSnapshot scene =
        engine.SnapshotScene();

    if (entityIndex >=
        scene.transforms.size()) {
        return 0u;
    }

    return
        static_cast<uint64_t>(
            entityIndex
        ) + 1ull;
}

bool Aetheris_SetNodePosition(
    uint64_t nodeHandle,
    float x,
    float y,
    float z
) noexcept {
    if (nodeHandle == 0u ||
        !std::isfinite(x) ||
        !std::isfinite(y) ||
        !std::isfinite(z)) {
        return false;
    }

    return EngineCore::Instance().SetNodePosition(
        nodeHandle,
        Vec4{x, y, z, 1.0f}
    );
}

bool Aetheris_SetNodeRotation(
    uint64_t nodeHandle,
    float x,
    float y,
    float z,
    float w
) noexcept {
    if (nodeHandle == 0u ||
        !std::isfinite(x) ||
        !std::isfinite(y) ||
        !std::isfinite(z) ||
        !std::isfinite(w)) {
        return false;
    }

    return EngineCore::Instance().SetNodeRotation(
        nodeHandle,
        Vec4{x, y, z, w}
    );
}

bool Aetheris_SetNodeScale(
    uint64_t nodeHandle,
    float x,
    float y,
    float z
) noexcept {
    if (nodeHandle == 0u ||
        !std::isfinite(x) ||
        !std::isfinite(y) ||
        !std::isfinite(z) ||
        x <= 0.0f ||
        y <= 0.0f ||
        z <= 0.0f) {
        return false;
    }

    return EngineCore::Instance().SetNodeScale(
        nodeHandle,
        Vec4{x, y, z, 0.0f}
    );
}

bool Aetheris_GetNodePosition(
    uint64_t nodeHandle,
    float* x,
    float* y,
    float* z
) noexcept {
    if (!x || !y || !z)
        return false;

    Vec4 position{};
    if (!EngineCore::Instance().GetNodePosition(
            nodeHandle,
            position)) {
        return false;
    }

    *x = position.x;
    *y = position.y;
    *z = position.z;
    return true;
}

int32_t Aetheris_GetTouchState(
    float* deltaX,
    float* deltaY
) noexcept {
    if (!deltaX || !deltaY)
        return 0;

    const auto state =
        CSharpScriptEngine::PollTouchState();

    *deltaX = state.deltaX;
    *deltaY = state.deltaY;

    return state.phase;
}

} // extern "C"

} // namespace aetheris
