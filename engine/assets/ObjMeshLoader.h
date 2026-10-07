#pragma once

#include "DemoMesh.h"
#include "engine/core/AetherisTypes.h"
#include <filesystem>

namespace aetheris {

class ObjMeshLoader final {
public:
    bool Load(
        const std::filesystem::path& path,
        const Vec4& baseColor,
        float metallic,
        float roughness,
        float ao,
        DemoCpuMesh& out
    ) const noexcept;
};

}
