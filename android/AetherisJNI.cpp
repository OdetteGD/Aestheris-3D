#include <jni.h>
#include <android/native_window_jni.h>
#include "engine/core/EngineCore.h"

using namespace aetheris;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aetheris_engine_AetherisNative_nativeInitialize(JNIEnv* env, jclass, jobject surface, jint api) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return JNI_FALSE;
    const RenderAPI selected = api == 1 ? RenderAPI::OPENGL_ES3 : RenderAPI::VULKAN;
    const bool ok = EngineCore::Instance().Initialize(selected, window);
    ANativeWindow_release(window);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSwitchApi(JNIEnv* env, jclass, jobject surface, jint api) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return JNI_FALSE;
    const RenderAPI selected = api == 1 ? RenderAPI::OPENGL_ES3 : RenderAPI::VULKAN;
    const bool ok = EngineCore::Instance().SwitchGraphicsAPI(selected, window);
    ANativeWindow_release(window);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeRegisterEditorSurface(JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window) {
        EngineCore::Instance().OnSurfaceChanged(window);
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeResizeViewport(JNIEnv* env, jclass, jobject surface, jint, jint) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window) {
        EngineCore::Instance().OnSurfaceChanged(window);
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeReleaseSurface(JNIEnv*, jclass) {
    EngineCore::Instance().OnSurfaceDestroyed();
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeSurfaceChanged(JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window) {
        EngineCore::Instance().OnSurfaceChanged(window);
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeGizmo(JNIEnv*, jclass, jint type, jint entity,
    jfloat x, jfloat y, jfloat z, jfloat w) {
    GizmoCommand c{};
    c.entity = static_cast<uint32_t>(entity);
    c.delta = {x,y,z,w};
    c.type = type == 0 ? GizmoCommand::Type::Translate :
             type == 1 ? GizmoCommand::Type::Rotate : GizmoCommand::Type::Scale;
    EngineCore::Instance().ApplyGizmo(c);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aetheris_engine_AetherisNative_nativeShutdown(JNIEnv*, jclass) {
    EngineCore::Instance().Shutdown();
}
