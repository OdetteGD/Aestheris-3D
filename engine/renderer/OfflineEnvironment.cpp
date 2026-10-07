#include "OfflineEnvironment.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

namespace aetheris {
namespace {

constexpr uint8_t Ktx2Identifier[12] = {
    0xAB, 0x4B, 0x54, 0x58,
    0x20, 0x32, 0x30, 0xBB,
    0x0D, 0x0A, 0x1A, 0x0A
};

uint32_t ReadU32(
    const uint8_t* p) noexcept
{
    return
        static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8u) |
        (static_cast<uint32_t>(p[2]) << 16u) |
        (static_cast<uint32_t>(p[3]) << 24u);
}

uint64_t ReadU64(
    const uint8_t* p) noexcept
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8; ++i)
        v |=
            static_cast<uint64_t>(p[i])
            << (8u * i);
    return v;
}

bool LoadFile(
    const std::filesystem::path& path,
    std::vector<uint8_t>& bytes) noexcept
{
    std::ifstream file(
        path,
        std::ios::binary |
        std::ios::ate
    );

    if (!file)
        return false;

    const std::streamoff size =
        file.tellg();

    if (size <= 0 ||
        size >
            static_cast<std::streamoff>(
                std::numeric_limits<size_t>::max()
            )) {
        return false;
    }

    bytes.resize(
        static_cast<size_t>(size)
    );

    file.seekg(
        0,
        std::ios::beg
    );

    file.read(
        reinterpret_cast<char*>(
            bytes.data()
        ),
        size
    );

    return
        file.good() ||
        file.eof();
}

bool DecodeRadianceRGBE(
    const std::vector<uint8_t>& file,
    OfflineEnvironmentData& output) noexcept
{
    const char* ptr =
        reinterpret_cast<const char*>(
            file.data()
        );

    const size_t size =
        file.size();

    const char* end =
        ptr + size;

    if (size < 16)
        return false;

    const char* format =
        std::strstr(
            ptr,
            "FORMAT=32-bit_rle_rgbe"
        );

    if (!format)
        return false;

    const char* headerEnd =
        std::strstr(
            ptr,
            "

"
        );

    if (!headerEnd)
        return false;

    headerEnd += 2;

    unsigned width = 0;
    unsigned height = 0;

    if (std::sscanf(
            headerEnd,
            "-Y %u +X %u",
            &height,
            &width
        ) != 2) {
        if (std::sscanf(
                headerEnd,
                "+Y %u +X %u",
                &height,
                &width
            ) != 2) {
            return false;
        }
    }

    if (width == 0 ||
        height == 0 ||
        width > 4096 ||
        height > 4096) {
        return false;
    }

    output = {};
    output.kind =
        OfflineEnvironmentData::Kind::
            EquirectangularHDR;
    output.width = width;
    output.height = height;
    output.mipLevels = 1;
    output.faces = 1;
    output.bytesPerPixel = 16;
    output.vkFormat = 0;

    output.bytes.resize(
        static_cast<size_t>(
            width
        ) *
        static_cast<size_t>(
            height
        ) *
        16u
    );

    std::array<
        uint8_t,
        4096u * 4u
    > scanline{};

    size_t cursor =
        static_cast<size_t>(
            headerEnd - ptr
        );

    for (uint32_t y = 0;
         y < height;
         ++y) {

        if (cursor + 4 > size)
            return false;

        if (file[cursor] != 2 ||
            file[cursor + 1] != 2) {
            return false;
        }

        const uint32_t scanWidth =
            (
                static_cast<uint32_t>(
                    file[cursor + 2]
                ) << 8u
            ) |
            static_cast<uint32_t>(
                file[cursor + 3]
            );

        cursor += 4;

        if (scanWidth != width ||
            width > 4096) {
            return false;
        }

        for (uint32_t channel = 0;
             channel < 4;
             ++channel) {

            uint32_t x = 0;

            while (x < width) {

                if (cursor >= size)
                    return false;

                const uint8_t run =
                    file[cursor++];

                if (run > 128) {
                    const uint32_t count =
                        run - 128;

                    if (count == 0 ||
                        x + count > width ||
                        cursor >= size) {
                        return false;
                    }

                    const uint8_t value =
                        file[cursor++];

                    for (uint32_t i=0;
                         i<count;
                         ++i) {
                        scanline[
                            channel * width +
                            x++
                        ] = value;
                    }
                } else {
                    const uint32_t count =
                        run;

                    if (count == 0 ||
                        x + count > width ||
                        cursor + count > size) {
                        return false;
                    }

                    for (uint32_t i=0;
                         i<count;
                         ++i) {
                        scanline[
                            channel * width +
                            x++
                        ] = file[cursor++];
                    }
                }
            }
        }

        for (uint32_t x=0;
             x<width;
             ++x) {

            const uint8_t r =
                scanline[x];

            const uint8_t g =
                scanline[width+x];

            const uint8_t b =
                scanline[2u*width+x];

            const uint8_t e =
                scanline[3u*width+x];

            float* dst =
                reinterpret_cast<float*>(
                    output.bytes.data() +
                    (
                        static_cast<size_t>(y) *
                        width +
                        x
                    ) * 16u
                );

            if (e == 0) {
                dst[0] =
                    dst[1] =
                    dst[2] =
                    0.0f;
                dst[3] = 1.0f;
                continue;
            }

            const float scale =
                std::ldexp(
                    1.0f,
                    static_cast<int>(e) -
                        (128 + 8)
                );

            dst[0] =
                static_cast<float>(r) *
                scale;

            dst[1] =
                static_cast<float>(g) *
                scale;

            dst[2] =
                static_cast<float>(b) *
                scale;

            dst[3] = 1.0f;
        }
    }

    return true;
}

bool LoadKtx2(
    const std::vector<uint8_t>& file,
    OfflineEnvironmentData& output) noexcept
{
    if (file.size() < 80)
        return false;

    if (std::memcmp(
            file.data(),
            Ktx2Identifier,
            sizeof(Ktx2Identifier)
        ) != 0) {
        return false;
    }

    const uint8_t* h =
        file.data();

    const uint32_t vkFormat =
        ReadU32(h + 12);

    const uint32_t width =
        ReadU32(h + 20);

    const uint32_t height =
        ReadU32(h + 24);

    const uint32_t depth =
        ReadU32(h + 28);

    const uint32_t layers =
        ReadU32(h + 32);

    const uint32_t faces =
        ReadU32(h + 36);

    const uint32_t levels =
        ReadU32(h + 40);

    const uint32_t supercompression =
        ReadU32(h + 44);

    if (width == 0 ||
        height == 0 ||
        depth > 1 ||
        faces != 6 ||
        layers > 1 ||
        levels == 0 ||
        supercompression != 0) {
        return false;
    }

    // Aetheris intentionally accepts only raw KTX2 Vulkan formats here.
    // Basis/UASTC transcoding belongs in the texture-streaming module.
    if (vkFormat != VK_FORMAT_R16G16B16A16_SFLOAT &&
        vkFormat != VK_FORMAT_R32G32B32A32_SFLOAT) {
        return false;
    }

    const uint32_t bytesPerPixel =
        vkFormat ==
            VK_FORMAT_R16G16B16A16_SFLOAT
            ? 8u
            : 16u;

    const uint64_t levelTableBytes =
        static_cast<uint64_t>(levels) *
        24u;

    if (80u + levelTableBytes >
        file.size()) {
        return false;
    }

    output = {};
    output.kind =
        OfflineEnvironmentData::Kind::CubeKTX2;
    output.width = width;
    output.height = height;
    output.mipLevels = levels;
    output.faces = 6;
    output.bytesPerPixel =
        bytesPerPixel;
    output.vkFormat =
        vkFormat;

    output.levels.resize(levels);

    for (uint32_t level=0;
         level<levels;
         ++level) {

        const uint8_t* p =
            file.data() +
            80u +
            static_cast<size_t>(level) *
            24u;

        const uint64_t offset =
            ReadU64(p);

        const uint64_t length =
            ReadU64(p+8);

        const uint64_t uncompressed =
            ReadU64(p+16);

        if (offset >
                file.size() ||
            length >
                file.size() - offset ||
            uncompressed != length) {
            return false;
        }

        const uint32_t levelWidth =
            std::max(
                1u,
                width >> level
            );

        const uint32_t levelHeight =
            std::max(
                1u,
                height >> level
            );

        const uint64_t expected =
            static_cast<uint64_t>(
                levelWidth
            ) *
            levelHeight *
            6u *
            bytesPerPixel;

        if (length != expected)
            return false;

        output.levels[level] = {
            offset,
            length,
            uncompressed,
            levelWidth,
            levelHeight
        };
    }

    output.bytes = file;
    return true;
}

}
bool OfflineEnvironmentLoader::Load(
    const std::filesystem::path& path,
    OfflineEnvironmentData& output
) noexcept
{
    output = {};

    std::vector<uint8_t> file{};

    if (!LoadFile(path,file))
        return false;

    if (path.extension() == ".ktx2")
        return LoadKtx2(file,output);

    if (path.extension() == ".hdr")
        return DecodeRadianceRGBE(file,output);

    return false;
}

}
