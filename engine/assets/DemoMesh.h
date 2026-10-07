#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aetheris {

inline constexpr uint32_t kMaxDemoVertices = 8192;
inline constexpr uint32_t kMaxDemoIndices  = 12288;
inline constexpr uint32_t kMaxDemoMeshes   = 8;
inline constexpr uint32_t kMaxDemoItems    = 256;

struct DemoVertex final {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
    float baseColorMetallic[4]{};
    float roughnessAO[2]{};
};

static_assert(std::is_trivially_copyable_v<DemoVertex>);
static_assert(sizeof(DemoVertex) == 56);
static_assert(offsetof(DemoVertex, normal) == 12);
static_assert(offsetof(DemoVertex, uv) == 24);
static_assert(offsetof(DemoVertex, baseColorMetallic) == 32);
static_assert(offsetof(DemoVertex, roughnessAO) == 48);

struct DemoCpuMesh final {
    std::array<DemoVertex, kMaxDemoVertices> vertices{};
    std::array<uint32_t, kMaxDemoIndices> indices{};
    uint32_t vertexCount{};
    uint32_t indexCount{};

    void Clear() noexcept {
        vertexCount = 0;
        indexCount = 0;
    }

    bool Empty() const noexcept {
        return vertexCount == 0 || indexCount == 0;
    }
};

enum class DemoMeshSlot : uint32_t {
    Ground = 0,
    Wall = 1,
    Ramp = 2,
    Stairs = 3,
    Crate = 4,
    Column = 5,
    Cube = 6,
    Count = 7
};

}
