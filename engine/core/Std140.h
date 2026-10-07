#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aetheris::std140 {

struct alignas(16) Vec4 { float x{}, y{}, z{}, w{}; };
struct alignas(16) Mat4 { std::array<float, 16> m{}; };
struct alignas(16) UVec4 { uint32_t x{}, y{}, z{}, w{}; };

template <typename T>
struct alignas(16) BlockValue {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
};

struct alignas(16) CameraBlock {
    Mat4 viewProj{};
    Vec4 cameraPosition{};
    Vec4 viewportAndTime{};
};

static_assert(alignof(Vec4) == 16 && sizeof(Vec4) == 16);
static_assert(alignof(Mat4) == 16 && sizeof(Mat4) == 64);
static_assert(alignof(CameraBlock) == 16 && offsetof(CameraBlock, cameraPosition) == 64);

}
