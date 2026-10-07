#pragma once

#include <GLES3/gl3.h>
#include <array>
#include <cstdint>
#include <cstddef>

namespace aetheris {

class AetherisValidationTracker final {
public:
    enum class Kind : uint8_t {
        Texture,
        Buffer,
        VertexArray,
        Renderbuffer,
        Framebuffer,
        Shader,
        Program,
        UniformBuffer
    };

    enum class Pass : uint8_t {
        Sky = 0,
        Shadow,
        Tier3Opaque,
        Tier2Opaque,
        Gizmo,
        Offscreen,
        Count
    };

    struct Snapshot final {
        bool valid{true};
        uint32_t allocations{0};
        uint32_t failures{0};
    };

private:
    static constexpr uint32_t MaxRecords = 192;
    static constexpr uint32_t MaxLabel = 48;

    struct Record final {
        Kind kind{};
        uint32_t id{};
        bool valid{};
        char label[MaxLabel]{};
    };

    struct PassRecord final {
        bool valid{true};
        char label[MaxLabel]{};
        uint32_t failureCount{};
    };

    std::array<Record, MaxRecords> records_{};
    std::array<PassRecord, static_cast<size_t>(Pass::Count)> passes_{};
    uint32_t recordCount_{};
    uint32_t allocationCount_{};
    uint32_t failureCount_{};

    static void CopyLabel(char* dst, const char* src) noexcept;
    static const char* KindName(Kind kind) noexcept;

public:
    AetherisValidationTracker() noexcept;

    void Reset() noexcept;

    bool ValidateAllocation(Kind kind, const char* label, GLuint id) noexcept;
    bool ValidateShader(GLuint shader, const char* label, Pass pass) noexcept;
    bool ValidateProgram(GLuint program, const char* label, Pass pass) noexcept;
    bool ValidateFramebuffer(GLuint fbo, Pass pass, const char* label) noexcept;
    bool ValidateGlError(Pass pass, const char* stage) noexcept;

    void IsolatePass(Pass pass, const char* reason) noexcept;
    void RecoverPass(Pass pass) noexcept;

    bool IsPassValid(Pass pass) const noexcept;
    const char* PassName(Pass pass) const noexcept;

    Snapshot GetSnapshot() const noexcept;
};

} // namespace aetheris
