#include "engine/renderer/TextureStreaming.h"
#include <fstream>

namespace aetheris {
bool TextureStreaming::Initialize(bool astcSupported) noexcept {
    selected_ = astcSupported ? TextureCompression::ASTC_6x6 : TextureCompression::RGBA8;
    return true;
}
bool TextureStreaming::InspectKTX2(const std::filesystem::path& path) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::array<unsigned char, 12> id{};
    in.read(reinterpret_cast<char*>(id.data()), id.size());
    static constexpr unsigned char ktx2[12] = {0xAB,0x4B,0x54,0x58,0x20,0x32,0x30,0xBB,0x0D,0x0A,0x1A,0x0A};
    for (size_t i=0;i<id.size();++i) if (id[i] != ktx2[i]) return false;
    return true;
}
bool TextureStreaming::RequestMips(const TextureStreamRequest& request) noexcept {
    if (request.mipCount == 0 || request.firstMip >= 16) return false;
    chunkCount_ = 0;
    for (uint32_t i=0; i<request.mipCount && chunkCount_<chunks_.size(); ++i) chunks_[chunkCount_++] = {request.firstMip+i,0,0,0,0};
    return true;
}
bool TextureStreaming::IsStandardMobileASTC(TextureCompression c) noexcept {
    return c == TextureCompression::ASTC_4x4 || c == TextureCompression::ASTC_6x6 || c == TextureCompression::ASTC_8x8;
}
}
