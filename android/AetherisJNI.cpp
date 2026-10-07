#include <jni.h>
#include <android/native_window_jni.h>
#include "engine/core/EngineCore.h"
using namespace aetheris;
extern "C" JNIEXPORT jboolean JNICALL Java_com_aetheris_engine_AetherisNative_nativeInitialize(JNIEnv*e,jclass,jobject surface,jint api){
 ANativeWindow*w=ANativeWindow_fromSurface(e,surface);if(!w)return JNI_FALSE;
 bool ok=EngineCore::Instance().Initialize(api==1?RenderAPI::OPENGL_ES3:RenderAPI::VULKAN,w);ANativeWindow_release(w);return ok;
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_aetheris_engine_AetherisNative_nativeSwitchApi(JNIEnv*e,jclass,jobject surface,jint api){
 ANativeWindow*w=ANativeWindow_fromSurface(e,surface);if(!w)return JNI_FALSE;
 bool ok=EngineCore::Instance().SwitchGraphicsAPI(api==1?RenderAPI::OPENGL_ES3:RenderAPI::VULKAN,w);ANativeWindow_release(w);return ok;
}
extern "C" JNIEXPORT void JNICALL Java_com_aetheris_engine_AetherisNative_nativeSurfaceChanged(JNIEnv*e,jclass,jobject surface){
 ANativeWindow*w=ANativeWindow_fromSurface(e,surface);if(w){EngineCore::Instance().OnSurfaceChanged(w);ANativeWindow_release(w);}
}
extern "C" JNIEXPORT void JNICALL Java_com_aetheris_engine_AetherisNative_nativeGizmo(JNIEnv*,jclass,jint type,jint entity,jfloat x,jfloat y,jfloat z,jfloat w){
 GizmoCommand c{};c.entity=static_cast<uint32_t>(entity);c.delta={x,y,z,w};
 c.type=type==0?GizmoCommand::Type::Translate:type==1?GizmoCommand::Type::Rotate:GizmoCommand::Type::Scale;
 EngineCore::Instance().ApplyGizmo(c);
}
extern "C" JNIEXPORT void JNICALL Java_com_aetheris_engine_AetherisNative_nativeShutdown(JNIEnv*,jclass){EngineCore::Instance().Shutdown();}
}