#pragma once
#include <cstdint>
#include <string_view>

namespace aetheris {

enum class GLESShaderPath : uint8_t { NativeGLSL, SpirvCrossTranslatedGLSL, Unsupported };

struct GLESShaderFallback final {
    static constexpr bool SupportsNativeSpirv() noexcept { return false; }
    static GLESShaderPath Select(bool hasGLSL300ES, bool spirvCrossAvailable) noexcept {
        if (hasGLSL300ES) return spirvCrossAvailable ? GLESShaderPath::SpirvCrossTranslatedGLSL
                                                     : GLESShaderPath::NativeGLSL;
        return GLESShaderPath::Unsupported;
    }
    // Translation/import is performed off-thread. The render loop only consumes immutable GLSL text.
    static std::string_view LanguageVersion() noexcept { return "#version 300 es"; }
};

}
