#pragma once

#include "engine/assets/DemoMesh.h"
#include "engine/core/AetherisTypes.h"
#include <filesystem>
#include <array>

namespace aetheris {

class DemoWorldInitializer final {
public:
    static constexpr uint32_t MaxItems = kMaxDemoItems;

    bool Initialize(
        const std::filesystem::path& projectRoot,
        SceneSnapshot& scene
    ) const noexcept;

    static const char* AssetPath(DemoMeshSlot slot) noexcept;
    static void FallbackMesh(DemoMeshSlot slot, DemoCpuMesh& out) noexcept;

private:
    static bool AppendItem(
        SceneSnapshot& scene,
        DemoMeshSlot mesh,
        const Vec4& position,
        const Vec4& rotation,
        const Vec4& scale,
        uint32_t materialId
    ) noexcept;

    static void AddGrid(
        SceneSnapshot& scene,
        uint32_t& materialId
    ) noexcept;

    static void AddArenaProps(
        SceneSnapshot& scene,
        uint32_t& materialId
    ) noexcept;
};

}
