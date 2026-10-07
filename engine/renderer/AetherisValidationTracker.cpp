#include "AetherisValidationTracker.h"

#include <android/log.h>
#include <cstddef>
#include <cstring>

namespace aetheris {

namespace {
constexpr char kTag[] = "AetherisEditor_GLES";
}

void AetherisValidationTracker::CopyLabel(char* dst, const char* src) noexcept {
    if (!dst) return;
    if (!src) src = "unnamed";
    std::strncpy(dst, src, MaxLabel - 1u);
    dst[MaxLabel - 1u] = '\0';
}

const char* AetherisValidationTracker::KindName(Kind kind) noexcept {
    switch (kind) {
        case Kind::Texture: return "texture";
        case Kind::Buffer: return "buffer";
        case Kind::VertexArray: return "vertex-array";
        case Kind::Renderbuffer: return "renderbuffer";
        case Kind::Framebuffer: return "framebuffer";
        case Kind::Shader: return "shader";
        case Kind::Program: return "program";
        case Kind::UniformBuffer: return "uniform-buffer";
        default: return "unknown";
    }
}

AetherisValidationTracker::AetherisValidationTracker() noexcept {
    Reset();
}

void AetherisValidationTracker::Reset() noexcept {
    recordCount_ = 0u;
    allocationCount_ = 0u;
    failureCount_ = 0u;
    passes_.fill(PassRecord{});
    CopyLabel(passes_[static_cast<size_t>(Pass::Sky)].label, "Sky");
    CopyLabel(passes_[static_cast<size_t>(Pass::Shadow)].label, "Shadow");
    CopyLabel(passes_[static_cast<size_t>(Pass::Tier3Opaque)].label, "Tier3Opaque");
    CopyLabel(passes_[static_cast<size_t>(Pass::Tier2Opaque)].label, "Tier2Opaque");
    CopyLabel(passes_[static_cast<size_t>(Pass::Gizmo)].label, "Gizmo");
    CopyLabel(passes_[static_cast<size_t>(Pass::Offscreen)].label, "Offscreen");
}

bool AetherisValidationTracker::ValidateAllocation(
    Kind kind,
    const char* label,
    GLuint id
) noexcept {
    ++allocationCount_;

    if (recordCount_ >= MaxRecords) {
        ++failureCount_;
        __android_log_print(
            ANDROID_LOG_ERROR,
            kTag,
            "allocation tracker exhausted while registering %s",
            label ? label : "unnamed"
        );
        return false;
    }

    Record& record = records_[recordCount_++];
    record.kind = kind;
    record.id = id;
    record.valid = id != 0u;
    CopyLabel(record.label, label);

    if (id == 0u) {
        ++failureCount_;
        __android_log_print(
            ANDROID_LOG_ERROR,
            kTag,
            "GPU allocation invalid kind=%s label=%s id=0",
            KindName(kind),
            label ? label : "unnamed"
        );
        return false;
    }

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        ++failureCount_;
        record.valid = false;
        __android_log_print(
            ANDROID_LOG_ERROR,
            kTag,
            "GPU allocation error kind=%s label=%s glError=0x%04x",
            KindName(kind),
            label ? label : "unnamed",
            error
        );
        return false;
    }

    return true;
}

bool AetherisValidationTracker::ValidateShader(
    GLuint shader,
    const char* label,
    Pass pass
) noexcept {
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (compiled != GL_TRUE) {
        char info[2048]{};
        GLsizei length = 0;
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(info)), &length, info);
        IsolatePass(pass, label);
        __android_log_print(
            ANDROID_LOG_ERROR,
            kTag,
            "shader compilation failed label=%s log=%s",
            label ? label : "unnamed",
            info
        );
        return false;
    }

    return ValidateGlError(pass, label);
}

bool AetherisValidationTracker::ValidateProgram(
    GLuint program,
    const char* label,
    Pass pass
) noexcept {
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);

    if (linked != GL_TRUE) {
        char info[2048]{};
        GLsizei length = 0;
        glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(info)), &length, info);
        IsolatePass(pass, label);
        __android_log_print(
            ANDROID_LOG_ERROR,
            kTag,
            "program link failed label=%s log=%s",
            label ? label : "unnamed",
            info
        );
        return false;
    }

    return ValidateGlError(pass, label);
}

bool AetherisValidationTracker::ValidateFramebuffer(
    GLuint fbo,
    Pass pass,
    const char* label
) noexcept {
    if (fbo == 0u) {
        IsolatePass(pass, label);
        return false;
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        IsolatePass(pass, label);
        __android_log_print(
            ANDROID_LOG_ERROR,
            kTag,
            "FBO invalid label=%s status=0x%04x pass=%s",
            label ? label : "unnamed",
            status,
            PassName(pass)
        );
        return false;
    }

    return ValidateGlError(pass, label);
}

bool AetherisValidationTracker::ValidateGlError(
    Pass pass,
    const char* stage
) noexcept {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return true;

    IsolatePass(pass, stage);
    __android_log_print(
        ANDROID_LOG_ERROR,
        kTag,
        "GL validation failed pass=%s stage=%s glError=0x%04x",
        PassName(pass),
        stage ? stage : "unknown",
        error
    );
    return false;
}

void AetherisValidationTracker::IsolatePass(
    Pass pass,
    const char* reason
) noexcept {
    PassRecord& record = passes_[static_cast<size_t>(pass)];
    record.valid = false;
    ++record.failureCount;
    ++failureCount_;
    __android_log_print(
        ANDROID_LOG_ERROR,
        kTag,
        "ISOLATE pass=%s reason=%s",
        PassName(pass),
        reason ? reason : "unknown"
    );
}

void AetherisValidationTracker::RecoverPass(Pass pass) noexcept {
    PassRecord& record = passes_[static_cast<size_t>(pass)];
    record.valid = true;
    __android_log_print(
        ANDROID_LOG_INFO,
        kTag,
        "RECOVER pass=%s",
        PassName(pass)
    );
}

bool AetherisValidationTracker::IsPassValid(Pass pass) const noexcept {
    return passes_[static_cast<size_t>(pass)].valid;
}

const char* AetherisValidationTracker::PassName(Pass pass) const noexcept {
    return passes_[static_cast<size_t>(pass)].label;
}

AetherisValidationTracker::Snapshot
AetherisValidationTracker::GetSnapshot() const noexcept {
    return {
        failureCount_ == 0u,
        allocationCount_,
        failureCount_
    };
}

} // namespace aetheris
