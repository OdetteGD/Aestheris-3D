#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace aetheris {

struct OfflineImageLevel final {
    uint64_t offset{};
    uint64_t byteLength{};
    uint64_t uncompressedByteLength{};
    uint32_t width{};
    uint32_t height{};
};

struct OfflineEnvironmentData final {
    enum class Kind : uint8_t {
        EquirectangularHDR,
        CubeKTX2
    };

    Kind kind{Kind::EquirectangularHDR};
    uint32_t width{};
    uint32_t height{};
    uint32_t mipLevels{1};
    uint32_t faces{1};
    uint32_t bytesPerPixel{};
    uint32_t vkFormat{};
    std::vector<OfflineImageLevel> levels{};
    std::vector<uint8_t> bytes{};
};

class OfflineEnvironmentLoader final {
public:
    static bool Load(
        const std::filesystem::path& path,
        OfflineEnvironmentData& output
    ) noexcept;
};

}
