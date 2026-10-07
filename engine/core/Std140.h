#pragma once
#include "engine/core/AetherisTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aetheris::std140 {

// Reuse the engine's ABI-stable SIMD-friendly types. Both are explicitly
// alignas(16), so CPU structs and GLSL std140 members share identical storage.
using Vec4 = aetheris::Vec4;
using Mat4 = aetheris::Mat4;

struct alignas(16) DeferredFrameBlock {
    Vec4 cameraPosition{};
    Vec4 sunDirection{};
    Vec4 sunColor{};
    Vec4 skyParams{};
    Vec4 cameraRight{};
    Vec4 cameraUp{};
    Vec4 cameraForward{};
    Mat4 invViewProj{};
    Mat4 csmMatrices[3]{};
    Vec4 csmSplits{};
};

struct alignas(16) MaterialBlock {
    Vec4 baseColorMetallic{};
    Vec4 roughnessNormalAo{};
};

static_assert(std::is_trivially_copyable_v<Vec4>);
static_assert(std::is_trivially_copyable_v<Mat4>);
static_assert(std::is_trivially_copyable_v<DeferredFrameBlock>);
static_assert(std::is_trivially_copyable_v<MaterialBlock>);

static_assert(alignof(Vec4) == 16);
static_assert(sizeof(Vec4) == 16);
static_assert(alignof(Mat4) == 16);
static_assert(sizeof(Mat4) == 64);

static_assert(offsetof(DeferredFrameBlock, cameraPosition) == 0);
static_assert(offsetof(DeferredFrameBlock, sunDirection) == 16);
static_assert(offsetof(DeferredFrameBlock, sunColor) == 32);
static_assert(offsetof(DeferredFrameBlock, skyParams) == 48);
static_assert(offsetof(DeferredFrameBlock, cameraRight) == 64);
static_assert(offsetof(DeferredFrameBlock, cameraUp) == 80);
static_assert(offsetof(DeferredFrameBlock, cameraForward) == 96);
static_assert(offsetof(DeferredFrameBlock, invViewProj) == 112);
static_assert(offsetof(DeferredFrameBlock, csmMatrices) == 176);
static_assert(offsetof(DeferredFrameBlock, csmSplits) == 368);
static_assert(sizeof(DeferredFrameBlock) == 384);
static_assert(sizeof(MaterialBlock) == 32);

}
