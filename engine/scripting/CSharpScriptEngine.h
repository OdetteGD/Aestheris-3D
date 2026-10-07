#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace aetheris {

class CSharpScriptEngine final {
public:
    struct TouchSnapshot final {
        float deltaX{};
        float deltaY{};
        int32_t phase{};
    };

private:
    using MonoDomain = void;
    using MonoAssembly = void;
    using MonoImage = void;
    using MonoClass = void;
    using MonoMethod = void;
    using MonoObject = void;
    using MonoThread = void;
    using MonoImageOpenStatus = int;

    using fn_mono_set_assemblies_path = void (*)(const char*);
    using fn_mono_set_dirs = void (*)(const char*, const char*);
    using fn_mono_jit_init_version = MonoDomain* (*)(const char*, const char*);
    using fn_mono_jit_cleanup = void (*)(MonoDomain*);
    using fn_mono_thread_attach = MonoThread* (*)(MonoDomain*);
    using fn_mono_domain_set = int (*)(MonoDomain*, int);
    using fn_mono_image_open_from_data_full = MonoImage* (*)(char*, uint32_t, int, MonoImageOpenStatus*, int);
    using fn_mono_image_close = void (*)(MonoImage*);
    using fn_mono_assembly_load_from_full = MonoAssembly* (*)(MonoImage*, const char*, MonoImageOpenStatus*, int);
    using fn_mono_assembly_get_image = MonoImage* (*)(MonoAssembly*);
    using fn_mono_class_from_name = MonoClass* (*)(MonoImage*, const char*, const char*);
    using fn_mono_class_get_method_from_name = MonoMethod* (*)(MonoClass*, const char*, int);
    using fn_mono_runtime_invoke = MonoObject* (*)(MonoMethod*, void*, void**, MonoObject**);

    void* monoLibrary_{};
    MonoDomain* rootDomain_{};
    MonoAssembly* assembly_{};
    MonoClass* scriptRuntimeClass_{};
    MonoMethod* initializeMethod_{};
    MonoMethod* updateMethod_{};

    fn_mono_set_assemblies_path mono_set_assemblies_path_{};
    fn_mono_set_dirs mono_set_dirs_{};
    fn_mono_jit_init_version mono_jit_init_version_{};
    fn_mono_jit_cleanup mono_jit_cleanup_{};
    fn_mono_thread_attach mono_thread_attach_{};
    fn_mono_domain_set mono_domain_set_{};
    fn_mono_image_open_from_data_full mono_image_open_from_data_full_{};
    fn_mono_image_close mono_image_close_{};
    fn_mono_assembly_load_from_full mono_assembly_load_from_full_{};
    fn_mono_assembly_get_image mono_assembly_get_image_{};
    fn_mono_class_from_name mono_class_from_name_{};
    fn_mono_class_get_method_from_name mono_class_get_method_from_name_{};
    fn_mono_runtime_invoke mono_runtime_invoke_{};

    std::vector<uint8_t> assemblyBytes_{};
    std::filesystem::path projectRoot_{};

    bool initialized_{};
    bool runtimeAvailable_{};
    bool scriptAvailable_{};
    bool scriptDisabled_{};
    std::atomic_bool inputInitialized_{false};

    bool ResolveMonoApi() noexcept;
    bool MountAssembly();
    bool BindScriptRuntime();
    void DisableScript(const char* reason) noexcept;

public:
    CSharpScriptEngine() noexcept = default;
    CSharpScriptEngine(const CSharpScriptEngine&) = delete;
    CSharpScriptEngine& operator=(const CSharpScriptEngine&) = delete;
    ~CSharpScriptEngine() { Shutdown(); }

    static CSharpScriptEngine& Instance() noexcept;

    bool Initialize(const std::filesystem::path& projectRoot);
    bool Update(float deltaTime) noexcept;
    void Shutdown() noexcept;

    bool IsInitialized() const noexcept { return initialized_; }
    bool IsRuntimeAvailable() const noexcept { return runtimeAvailable_; }
    bool IsScriptAvailable() const noexcept { return scriptAvailable_ && !scriptDisabled_; }

    static void SetTouchStateFromJNI(int32_t phase, float x, float y) noexcept;
    static TouchSnapshot PollTouchState() noexcept;
};

extern "C" {

#if defined(__GNUC__)
#define AETHERIS_SCRIPT_API __attribute__((visibility("default")))
#else
#define AETHERIS_SCRIPT_API
#endif

AETHERIS_SCRIPT_API uint64_t Aetheris_GetNodeHandle(uint32_t entityIndex) noexcept;
AETHERIS_SCRIPT_API int32_t Aetheris_SetNodePosition(
    uint64_t nodeHandle,
    float x,
    float y,
    float z
) noexcept;
AETHERIS_SCRIPT_API int32_t Aetheris_SetNodeRotation(
    uint64_t nodeHandle,
    float x,
    float y,
    float z,
    float w
) noexcept;
AETHERIS_SCRIPT_API int32_t Aetheris_SetNodeScale(
    uint64_t nodeHandle,
    float x,
    float y,
    float z
) noexcept;
AETHERIS_SCRIPT_API int32_t Aetheris_GetNodePosition(
    uint64_t nodeHandle,
    float* x,
    float* y,
    float* z
) noexcept;
AETHERIS_SCRIPT_API int32_t Aetheris_GetTouchState(
    float* deltaX,
    float* deltaY
) noexcept;

#undef AETHERIS_SCRIPT_API

}

} // namespace aetheris
