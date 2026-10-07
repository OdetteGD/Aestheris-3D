#pragma once
#include <cstdint>
#include <filesystem>
#include <array>

namespace aetheris {

enum class TextureCompression : uint8_t { Unknown, ASTC_4x4, ASTC_6x6, ASTC_8x8, ETC2, RGBA8 };
struct TextureMipChunk final { uint32_t mip{0}; uint64_t fileOffset{0}; uint32_t compressedBytes{0}; uint32_t width{0}; uint32_t height{0}; };
struct TextureStreamRequest final { std::filesystem::path path{}; uint32_t firstMip{0}; uint32_t mipCount{1}; };

class TextureStreaming final {
    TextureCompression selected_{TextureCompression::RGBA8};
    std::array<TextureMipChunk, 16> chunks_{};
    uint32_t chunkCount_{0};
public:
    bool Initialize(bool astcSupported) noexcept;
    TextureCompression Compression() const noexcept { return selected_; }
    bool InspectKTX2(const std::filesystem::path& path) noexcept;
    bool RequestMips(const TextureStreamRequest& request) noexcept;
    // KTX2/BasisU transcoding runs on an asset worker, never the render thread.
    static bool IsStandardMobileASTC(TextureCompression c) noexcept;
};

}
