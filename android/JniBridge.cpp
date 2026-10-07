#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include "engine/core/EngineCore.h"
#include <cinttypes>
#include <source_location>
#include <unwind.h>

namespace {
const char* StateName(aetheris::EngineState state) noexcept {
    switch (state) {
        case aetheris::EngineState::Uninitialized: return "Uninitialized";
        case aetheris::EngineState::SurfaceReady: return "SurfaceReady";
        case aetheris::EngineState::AllocatingAssets: return "AllocatingAssets";
        case aetheris::EngineState::Rendering: return "Rendering";
        default: return "Unknown";
    }
}

_Unwind_Reason_Code TraceFrame(_Unwind_Context* context, void* opaque) noexcept {
    auto* depth = static_cast<unsigned*>(opaque);
    if (*depth >= 12u) return _URC_END_OF_STACK;
    const uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        __android_log_print(
            ANDROID_LOG_ERROR,
            "AetherisEngine_Fatal",
            "jni-stack[%u] pc=0x%" PRIxPTR,
            *depth,
            pc
        );
    }
    ++(*depth);
    return _URC_NO_REASON;
}

void FatalJni(
    const char* operation,
    const char* reason,
    const std::source_location& location = std::source_location::current()
) noexcept {
    __android_log_print(
        ANDROID_LOG_ERROR,
        "AetherisEngine_Fatal",
        "%s failed: %s (%s:%u %s)",
        operation,
        reason,
        location.file_name(),
        location.line(),
        location.function_name()
    );
    unsigned depth = 0;
    _Unwind_Backtrace(TraceFrame, &depth);
}

aetheris::RenderAPI SelectApi(jint api) noexcept {
    return api == 1 ? aetheris::RenderAPI::OPENGL_ES3 : aetheris::RenderAPI::VULKAN;
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSetProjectRoot(
    JNIEnv* env,jclass,jstring root
) {
    if (!root) { FatalJni("nativeSetProjectRoot","null project root"); return; }
    const char* chars = env->GetStringUTFChars(root,nullptr);
    if (!chars) { FatalJni("nativeSetProjectRoot","GetStringUTFChars returned null"); return; }
    aetheris::EngineCore::Instance().SetProjectRoot(chars);
    env->ReleaseStringUTFChars(root,chars);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeRegisterEditorSurface(
    JNIEnv* env,jclass,jobject surface
) {
    if (!surface) { FatalJni("nativeRegisterEditorSurface","null Surface"); return; }
    ANativeWindow* window = ANativeWindow_fromSurface(env,surface);
    if (!window) { FatalJni("nativeRegisterEditorSurface","ANativeWindow_fromSurface returned null"); return; }
    auto& engine = aetheris::EngineCore::Instance();
    engine.OnSurfaceChanged(window);
    __android_log_print(
        ANDROID_LOG_INFO,
        "AetherisEngine",
        "nativeRegisterEditorSurface -> %s",
        StateName(engine.State())
    );
    ANativeWindow_release(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeResizeViewport(
    JNIEnv* env,jclass,jobject surface,jint width,jint height
) {
    if (!surface || width <= 0 || height <= 0) {
        FatalJni("nativeResizeViewport","invalid Surface/extent");
        return;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env,surface);
    if (!window) { FatalJni("nativeResizeViewport","ANativeWindow_fromSurface returned null"); return; }
    auto& engine = aetheris::EngineCore::Instance();
    engine.OnSurfaceChanged(window);
    ANativeWindow_release(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeReleaseSurface(JNIEnv*,jclass) {
    aetheris::EngineCore::Instance().OnSurfaceDestroyed();
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSurfaceChanged(
    JNIEnv* env,jclass,jobject surface
) {
    if (!surface) { FatalJni("nativeSurfaceChanged","null Surface"); return; }
    ANativeWindow* window = ANativeWindow_fromSurface(env,surface);
    if (!window) { FatalJni("nativeSurfaceChanged","ANativeWindow_fromSurface returned null"); return; }
    aetheris::EngineCore::Instance().OnSurfaceChanged(window);
    ANativeWindow_release(window);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aetheris_engine_AetherisNative_nativeInitialize(
    JNIEnv* env,jclass,jobject surface,jint api
) {
    if (!surface) { FatalJni("nativeInitialize","null Surface"); return JNI_FALSE; }
    ANativeWindow* window = ANativeWindow_fromSurface(env,surface);
    if (!window) { FatalJni("nativeInitialize","ANativeWindow_fromSurface returned null"); return JNI_FALSE; }
    const bool ok = aetheris::EngineCore::Instance().Initialize(SelectApi(api),window);
    ANativeWindow_release(window);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSwitchApi(
    JNIEnv* env,jclass,jobject surface,jint api
) {
    if (!surface) { FatalJni("nativeSwitchApi","null Surface"); return JNI_FALSE; }
    ANativeWindow* window = ANativeWindow_fromSurface(env,surface);
    if (!window) { FatalJni("nativeSwitchApi","ANativeWindow_fromSurface returned null"); return JNI_FALSE; }
    const bool ok = aetheris::EngineCore::Instance().SwitchGraphicsAPI(SelectApi(api),window);
    ANativeWindow_release(window);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeGizmo(
    JNIEnv*,jclass,jint type,jint entity,jfloat x,jfloat y,jfloat z,jfloat w
) {
    aetheris::GizmoCommand command{};
    command.entity = static_cast<uint32_t>(entity);
    command.delta = {x,y,z,w};
    command.type = type == 0
        ? aetheris::GizmoCommand::Type::Translate
        : type == 1
            ? aetheris::GizmoCommand::Type::Rotate
            : aetheris::GizmoCommand::Type::Scale;
    aetheris::EngineCore::Instance().ApplyGizmo(command);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeShutdown(JNIEnv*,jclass) {
    aetheris::EngineCore::Instance().Shutdown();
}
