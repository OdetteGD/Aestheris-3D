#include <jni.h>
#include <android/native_window_jni.h>
#include "engine/core/EngineCore.h"
#include "engine/core/AetherisLog.h"

using namespace aetheris;

namespace {
RenderAPI SelectApi(jint api) noexcept {
    return api == 1 ? RenderAPI::OPENGL_ES3 : RenderAPI::VULKAN;
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSetProjectRoot(
    JNIEnv* env, jclass, jstring root) {
    if (!root) return;
    const char* chars = env->GetStringUTFChars(root, nullptr);
    if (!chars) return;
    EngineCore::Instance().SetProjectRoot(chars);
    AETHERIS_LOGI("Project root registered");
    env->ReleaseStringUTFChars(root, chars);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aetheris_engine_AetherisNative_nativeInitialize(
    JNIEnv* env, jclass, jobject surface, jint api) {
    if (!surface) return JNI_FALSE;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        AETHERIS_LOGE("nativeInitialize: no ANativeWindow");
        return JNI_FALSE;
    }

    const RenderAPI selected = SelectApi(api);
    const bool ok = EngineCore::Instance().Initialize(selected, window);
    ANativeWindow_release(window);
    AETHERIS_LOGI("nativeInitialize api=%d result=%d", api, ok ? 1 : 0);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSwitchApi(
    JNIEnv* env, jclass, jobject surface, jint api) {
    if (!surface) return JNI_FALSE;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        AETHERIS_LOGE("nativeSwitchApi: no ANativeWindow");
        return JNI_FALSE;
    }

    const bool ok = EngineCore::Instance().SwitchGraphicsAPI(SelectApi(api), window);
    ANativeWindow_release(window);
    AETHERIS_LOGI("nativeSwitchApi api=%d result=%d", api, ok ? 1 : 0);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeRegisterEditorSurface(
    JNIEnv* env, jclass, jobject surface) {
    if (!surface) {
        AETHERIS_LOGW("surfaceCreated ignored: null Surface");
        return;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        AETHERIS_LOGE("surfaceCreated failed: ANativeWindow_fromSurface returned null");
        return;
    }

    AETHERIS_LOGI("surfaceCreated: native window=%p; bootstrapping renderer",
                  static_cast<void*>(window));
    EngineCore::Instance().OnSurfaceChanged(window);
    ANativeWindow_release(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeResizeViewport(
    JNIEnv* env, jclass, jobject surface, jint width, jint height) {
    if (!surface) {
        AETHERIS_LOGW("surfaceChanged ignored: null Surface (%d x %d)", width, height);
        return;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        AETHERIS_LOGE("surfaceChanged failed: ANativeWindow_fromSurface returned null");
        return;
    }

    AETHERIS_LOGI("surfaceChanged: %d x %d; rebuilding Vulkan-dependent resources",
                  width, height);
    EngineCore::Instance().OnSurfaceChanged(window);
    ANativeWindow_release(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeReleaseSurface(JNIEnv*, jclass) {
    AETHERIS_LOGI("surfaceDestroyed: stopping native frame production");
    EngineCore::Instance().OnSurfaceDestroyed();
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSurfaceChanged(
    JNIEnv* env, jclass, jobject surface) {
    if (!surface) {
        AETHERIS_LOGW("surfaceChanged ignored: null Surface");
        return;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        AETHERIS_LOGE("surfaceChanged failed: ANativeWindow_fromSurface returned null");
        return;
    }
    EngineCore::Instance().OnSurfaceChanged(window);
    ANativeWindow_release(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeGizmo(
    JNIEnv*, jclass, jint type, jint entity,
    jfloat x, jfloat y, jfloat z, jfloat w) {
    GizmoCommand c{};
    c.entity = static_cast<uint32_t>(entity);
    c.delta = {x, y, z, w};
    c.type = type == 0 ? GizmoCommand::Type::Translate :
             type == 1 ? GizmoCommand::Type::Rotate :
                         GizmoCommand::Type::Scale;
    EngineCore::Instance().ApplyGizmo(c);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeShutdown(JNIEnv*, jclass) {
    AETHERIS_LOGI("nativeShutdown");
    EngineCore::Instance().Shutdown();
}
