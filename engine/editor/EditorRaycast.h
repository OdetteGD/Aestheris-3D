#pragma once

#include "engine/math/GlmCompat.h"
#include "engine/world/SceneNode.h"
#include <cstdint>
#include <span>

namespace aetheris {

struct EditorRay final {
    glm::vec3 origin{};
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
};

struct MeshGeometryView final {
    std::span<const glm::vec3> positions{};
    std::span<const uint32_t> indices{};
};

struct RayHit final {
    float distance{0.0f};
    uint32_t triangleIndex{0u};
    glm::vec3 position{};
    glm::vec3 normal{};
};

struct ScenePickResult final {
    SceneNode* node{};
    RayHit hit{};
};

bool BuildEditorPickRay(
    float screenX,
    float screenY,
    float viewportWidth,
    float viewportHeight,
    const glm::mat4& inverseViewProjection,
    EditorRay& outRay
) noexcept;

bool IntersectRayAABB(
    const EditorRay& ray,
    const AABB& bounds,
    float& outDistance
) noexcept;

bool IntersectRayTriangle(
    const EditorRay& ray,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    RayHit& outHit
) noexcept;

bool IntersectRayMesh(
    const EditorRay& worldRay,
    const SceneNode& node,
    const MeshGeometryView& mesh,
    RayHit& outHit
) noexcept;

bool PickNodeByAABB(
    const EditorRay& worldRay,
    std::span<SceneNode* const> nodes,
    ScenePickResult& outResult
) noexcept;

} // namespace aetheris
