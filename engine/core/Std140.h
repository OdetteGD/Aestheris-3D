#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aetheris::std140 {

struct alignas(16) Vec4 { float x{}, y{}, z{}, w{}; };
struct alignas(16) Mat4 { std::array<float, 16> m{}; };

struct alignas(16) DeferredFrameBlock {
    Vec4 cameraPosition{};
    Vec4 sunDirection{};
    Vec4 sunColor{};
    Vec4 skyParams{};       // exposure, cloud coverage, cloud speed, Mie strength
    Vec4 cameraRight{};
    Vec4 cameraUp{};
    Vec4 cameraForward{};
    Mat4 invViewProj{};
    Mat4 csmMatrices[3]{};
    Vec4 csmSplits{};       // cascade 0/1/2 far distances, max prefilter mip in w
};

struct alignas(16) CsmBlock {
    Mat4 lightViewProj[3]{};
    Vec4 splits{};
};

struct alignas(16) MaterialBlock {
    Vec4 baseColorMetallic{};
    Vec4 roughnessNormalAo{};
};

static_assert(std::is_trivially_copyable_v<DeferredFrameBlock>);
static_assert(std::is_trivially_copyable_v<CsmBlock>);
static_assert(std::is_trivially_copyable_v<MaterialBlock>);
static_assert(alignof(Vec4) == 16 && sizeof(Vec4) == 16);
static_assert(alignof(Mat4) == 16 && sizeof(Mat4) == 64);
static_assert(sizeof(DeferredFrameBlock) == 384);
static_assert(offsetof(DeferredFrameBlock, invViewProj) == 96);
static_assert(offsetof(DeferredFrameBlock, csmMatrices) == 160);
static_assert(offsetof(DeferredFrameBlock, csmSplits) == 352);
static_assert(sizeof(CsmBlock) == 208);
static_assert(sizeof(MaterialBlock) == 32);

}
