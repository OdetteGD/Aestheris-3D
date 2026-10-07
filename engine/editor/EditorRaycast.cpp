#include "EditorRaycast.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aetheris {

namespace {

constexpr float kEpsilon = 1e-6f;
constexpr float kInfinity = std::numeric_limits<float>::max();

glm::vec3 TransformPoint(
    const glm::mat4& matrix,
    const glm::vec3& point
) noexcept {
    const glm::vec4 value = matrix * glm::vec4(point, 1.0f);
    const float invW = std::fabs(value.w) > kEpsilon
        ? 1.0f / value.w
        : 1.0f;
    return {
        value.x * invW,
        value.y * invW,
        value.z * invW
    };
}

EditorRay TransformRay(
    const EditorRay& worldRay,
    const glm::mat4& inverseWorld
) noexcept {
    const glm::vec3 localOrigin =
        TransformPoint(inverseWorld, worldRay.origin);

    const glm::vec3 localTarget =
        TransformPoint(
            inverseWorld,
            worldRay.origin + worldRay.direction
        );

    return {
        localOrigin,
        glm::normalize(localTarget - localOrigin)
    };
}

bool IntersectAABBLocal(
    const EditorRay& ray,
    const AABB& bounds,
    float& outDistance
) noexcept {
    float tMin = 0.0f;
    float tMax = kInfinity;

    const float origin[3] = {
        ray.origin.x,
        ray.origin.y,
        ray.origin.z
    };

    const float direction[3] = {
        ray.direction.x,
        ray.direction.y,
        ray.direction.z
    };

    const float minimum[3] = {
        bounds.min.x,
        bounds.min.y,
        bounds.min.z
    };

    const float maximum[3] = {
        bounds.max.x,
        bounds.max.y,
        bounds.max.z
    };

    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        if (std::fabs(direction[axis]) <= kEpsilon) {
            if (origin[axis] < minimum[axis] ||
                origin[axis] > maximum[axis]) {
                return false;
            }
            continue;
        }

        const float invDirection = 1.0f / direction[axis];
        float t0 = (minimum[axis] - origin[axis]) * invDirection;
        float t1 = (maximum[axis] - origin[axis]) * invDirection;

        if (t0 > t1)
            std::swap(t0, t1);

        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);

        if (tMin > tMax)
            return false;
    }

    outDistance = tMin;
    return tMax >= 0.0f;
}

} // namespace

bool BuildEditorPickRay(
    float screenX,
    float screenY,
    float viewportWidth,
    float viewportHeight,
    const glm::mat4& inverseViewProjection,
    EditorRay& outRay
) noexcept {
    if (viewportWidth <= 0.0f ||
        viewportHeight <= 0.0f ||
        !std::isfinite(screenX) ||
        !std::isfinite(screenY)) {
        return false;
    }

    const float ndcX =
        (screenX / viewportWidth) * 2.0f - 1.0f;

    const float ndcY =
        1.0f - (screenY / viewportHeight) * 2.0f;

    const glm::vec4 nearPoint =
        inverseViewProjection *
        glm::vec4(ndcX, ndcY, -1.0f, 1.0f);

    const glm::vec4 farPoint =
        inverseViewProjection *
        glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

    if (std::fabs(nearPoint.w) <= kEpsilon ||
        std::fabs(farPoint.w) <= kEpsilon) {
        return false;
    }

    const float invNearW = 1.0f / nearPoint.w;
    const float invFarW = 1.0f / farPoint.w;

    outRay.origin = {
        nearPoint.x * invNearW,
        nearPoint.y * invNearW,
        nearPoint.z * invNearW
    };

    const glm::vec3 farWorld = {
        farPoint.x * invFarW,
        farPoint.y * invFarW,
        farPoint.z * invFarW
    };

    const glm::vec3 delta =
        farWorld - outRay.origin;

    if (glm::length(delta) <= kEpsilon)
        return false;

    outRay.direction = glm::normalize(delta);
    return true;
}

bool IntersectRayAABB(
    const EditorRay& ray,
    const AABB& bounds,
    float& outDistance
) noexcept {
    return IntersectAABBLocal(ray, bounds, outDistance);
}

bool IntersectRayTriangle(
    const EditorRay& ray,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    RayHit& outHit
) noexcept {
    const glm::vec3 edge1 = b - a;
    const glm::vec3 edge2 = c - a;
    const glm::vec3 pvec =
        glm::cross(ray.direction, edge2);

    const float det =
        glm::dot(edge1, pvec);

    if (std::fabs(det) <= kEpsilon)
        return false;

    const float inverseDet = 1.0f / det;
    const glm::vec3 tvec = ray.origin - a;
    const float u =
        glm::dot(tvec, pvec) * inverseDet;

    if (u < 0.0f || u > 1.0f)
        return false;

    const glm::vec3 qvec =
        glm::cross(tvec, edge1);

    const float v =
        glm::dot(ray.direction, qvec) * inverseDet;

    if (v < 0.0f || u + v > 1.0f)
        return false;

    const float t =
        glm::dot(edge2, qvec) * inverseDet;

    if (t <= kEpsilon)
        return false;

    const glm::vec3 normal =
        glm::normalize(glm::cross(edge1, edge2));

    outHit.distance = t;
    outHit.position =
        ray.origin + ray.direction * t;
    outHit.normal = normal;
    return true;
}

bool IntersectRayMesh(
    const EditorRay& worldRay,
    const SceneNode& node,
    const MeshGeometryView& mesh,
    RayHit& outHit
) noexcept {
    if (mesh.positions.empty() ||
        mesh.indices.size() < 3u ||
        mesh.indices.size() % 3u != 0u) {
        return false;
    }

    const glm::mat4 inverseWorld =
        glm::inverse(node.WorldTransform());

    const EditorRay localRay =
        TransformRay(worldRay, inverseWorld);

    float boundsDistance = 0.0f;
    const glm::mat4 world = node.WorldTransform();

    AABB localBounds = node.LocalBounds();
    if (!IntersectAABBLocal(
            localRay,
            localBounds,
            boundsDistance)) {
        return false;
    }

    float nearest = kInfinity;
    RayHit nearestHit{};
    bool found = false;

    const size_t triangleCount =
        mesh.indices.size() / 3u;

    for (size_t triangle = 0u;
         triangle < triangleCount;
         ++triangle) {
        const uint32_t ia =
            mesh.indices[triangle * 3u];
        const uint32_t ib =
            mesh.indices[triangle * 3u + 1u];
        const uint32_t ic =
            mesh.indices[triangle * 3u + 2u];

        if (ia >= mesh.positions.size() ||
            ib >= mesh.positions.size() ||
            ic >= mesh.positions.size()) {
            continue;
        }

        RayHit candidate{};
        if (!IntersectRayTriangle(
                localRay,
                mesh.positions[ia],
                mesh.positions[ib],
                mesh.positions[ic],
                candidate)) {
            continue;
        }

        if (candidate.distance < nearest) {
            nearest = candidate.distance;
            nearestHit = candidate;
            found = true;
        }
    }

    if (!found)
        return false;

    outHit = nearestHit;
    outHit.position =
        TransformPoint(world, nearestHit.position);

    const glm::vec3 normalPoint =
        TransformPoint(
            world,
            nearestHit.position +
            nearestHit.normal
        );

    outHit.normal =
        glm::normalize(
            normalPoint -
            outHit.position
        );

    const glm::vec3 delta =
        outHit.position - worldRay.origin;

    outHit.distance =
        glm::length(delta);

    return true;
}

bool PickNodeByAABB(
    const EditorRay& worldRay,
    std::span<SceneNode* const> nodes,
    ScenePickResult& outResult
) noexcept {
    float nearest = kInfinity;
    SceneNode* nearestNode = nullptr;

    for (SceneNode* node : nodes) {
        if (!node || !node->Visible())
            continue;

        float distance = 0.0f;
        if (!IntersectRayAABB(
                worldRay,
                node->WorldBounds(),
                distance)) {
            continue;
        }

        if (distance < nearest) {
            nearest = distance;
            nearestNode = node;
        }
    }

    if (!nearestNode)
        return false;

    outResult.node = nearestNode;
    outResult.hit.distance = nearest;
    outResult.hit.position =
        worldRay.origin +
        worldRay.direction * nearest;
    outResult.hit.normal = {
        0.0f,
        1.0f,
        0.0f
    };
    outResult.hit.triangleIndex = 0u;
    return true;
}

} // namespace aetheris
