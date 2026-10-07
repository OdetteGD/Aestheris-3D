#include "OpenGLESRenderer.h"
#include <android/native_window.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

namespace aetheris {

namespace {
constexpr EGLint kConfigAttributes[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
};
constexpr EGLint kContextAttributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE
};
}

bool OpenGLESRenderer::CreateContext() {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) return false;

    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(display_, &major, &minor) != EGL_TRUE) {
        display_ = EGL_NO_DISPLAY;
        return false;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        return false;
    }

    EGLint count = 0;
    if (eglChooseConfig(display_, kConfigAttributes, &config_, 1, &count) != EGL_TRUE ||
        count != 1) {
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        config_ = nullptr;
        return false;
    }

    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, kContextAttributes);
    if (context_ == EGL_NO_CONTEXT) {
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        config_ = nullptr;
        return false;
    }

    return true;
}

bool OpenGLESRenderer::CreateSurface() {
    if (display_ == EGL_NO_DISPLAY || !window_) return false;

    surface_ = eglCreateWindowSurface(display_, config_, window_, nullptr);
    if (surface_ == EGL_NO_SURFACE) return false;

    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
        DestroyEGLSurface();
        return false;
    }

    return true;
}

void OpenGLESRenderer::DestroyEGLSurface() noexcept {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
    }
    surface_ = EGL_NO_SURFACE;
    begun_ = false;
}

bool OpenGLESRenderer::Initialize(ANativeWindow* window) {
    if (initialized_ || !window) return false;

    window_ = window;
    ANativeWindow_acquire(window_);

    if (!CreateContext() || !CreateSurface()) {
        Shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

bool OpenGLESRenderer::BeginFrame() {
    if (!initialized_ || begun_ || surface_ == EGL_NO_SURFACE || display_ == EGL_NO_DISPLAY) {
        return false;
    }

    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) return false;

    EGLint width = 0;
    EGLint height = 0;
    if (eglQuerySurface(display_, surface_, EGL_WIDTH, &width) != EGL_TRUE ||
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &height) != EGL_TRUE ||
        width <= 0 || height <= 0) {
        return false;
    }

    glViewport(0, 0, width, height);
    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    begun_ = true;
    return true;
}

void OpenGLESRenderer::EndFrame() {
    if (!begun_) return;

    if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
        begun_ = false;
        return;
    }

    begun_ = false;
}

bool OpenGLESRenderer::RecreateSwapchain(ANativeWindow* window) {
    if (!initialized_ || !window || display_ == EGL_NO_DISPLAY) return false;

    if (window != window_) {
        if (surface_ != EGL_NO_SURFACE) DestroyEGLSurface();
        if (window_) ANativeWindow_release(window_);
        window_ = window;
        ANativeWindow_acquire(window_);
    } else if (surface_ != EGL_NO_SURFACE) {
        DestroyEGLSurface();
    }

    return CreateSurface();
}

void OpenGLESRenderer::ReleaseSurface() noexcept {
    begun_ = false;
    DestroyEGLSurface();

    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void OpenGLESRenderer::DrawRenderQueue(const RenderQueue&, std::span<const Transform>) {
    // RenderGraph passes issue GLES draws here.
}

void OpenGLESRenderer::Shutdown() noexcept {
    ReleaseSurface();

    if (display_ != EGL_NO_DISPLAY) {
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
        }
        eglTerminate(display_);
    }

    context_ = EGL_NO_CONTEXT;
    display_ = EGL_NO_DISPLAY;
    config_ = nullptr;
    initialized_ = false;
    begun_ = false;
}

uint64_t OpenGLESRenderer::CreateOffscreenRenderTarget(uint32_t, uint32_t) {
    return 0;
}

bool OpenGLESRenderer::ResizeOffscreenRenderTarget(uint64_t, uint32_t, uint32_t) {
    return false;
}

uint64_t OpenGLESRenderer::GetOffscreenColorHandle(uint64_t) const noexcept {
    return 0;
}

} // namespace aetheris
