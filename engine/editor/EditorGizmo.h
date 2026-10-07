#pragma once

#include "EditorRaycast.h"
#include "engine/math/GlmCompat.h"
#include "engine/world/SceneNode.h"

namespace aetheris {

enum class GizmoMode : uint8_t {
    Move = 0,
    Rotate = 1,
    Scale = 2
};

enum class GizmoAxis : uint8_t {
    None = 0,
    X,
    Y,
    Z,
    XYZ
};

struct GizmoTransform final {
    glm::vec3 position{};
    glm::quat rotation{};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct GizmoPick final {
    GizmoAxis axis{GizmoAxis::None};
    float distance{0.0f};
    float rayDistance{0.0f};
};

class EditorGizmo final {
    GizmoMode mode_{GizmoMode::Move};
    GizmoAxis axis_{GizmoAxis::None};
    GizmoTransform start_{};
    glm::vec3 pivot_{};
    glm::vec3 dragPlaneNormal_{0.0f, 1.0f, 0.0f};
    glm::vec3 dragStartPoint_{};
    glm::vec3 dragStartVector_{};
    float ringRadius_{1.15f};
    float handleLength_{1.35f};
    float pickRadius_{0.18f};
    bool dragging_{};

    static glm::vec3 AxisVector(GizmoAxis axis) noexcept;

    static bool IntersectPlane(
        const EditorRay& ray,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal,
        float& outT,
        glm::vec3& outPoint
    ) noexcept;

    static float RaySegmentDistanceSquared(
        const EditorRay& ray,
        const glm::vec3& a,
        const glm::vec3& b,
        float& outRayT
    ) noexcept;

    bool PickAxisHandle(
        const EditorRay& ray,
        GizmoAxis axis,
        float gizmoScale,
        GizmoPick& out
    ) noexcept;

    bool PickRotateRing(
        const EditorRay& ray,
        GizmoAxis axis,
        float gizmoScale,
        GizmoPick& out
    ) const noexcept;

public:
    EditorGizmo() noexcept = default;

    void SetMode(GizmoMode mode) noexcept {
        mode_ = mode;
    }

    GizmoMode Mode() const noexcept {
        return mode_;
    }

    bool Pick(
        const EditorRay& ray,
        const glm::vec3& pivot,
        float gizmoScale,
        GizmoPick& out
    ) const noexcept;

    bool BeginDrag(
        const EditorRay& ray,
        const glm::vec3& pivot,
        float gizmoScale,
        const GizmoTransform& initial,
        GizmoPick pick,
        const glm::vec3& cameraForward
    ) noexcept;

    bool UpdateDrag(
        const EditorRay& ray,
        const glm::vec3& cameraForward,
        GizmoTransform& ioTransform
    ) noexcept;

    void EndDrag() noexcept {
        dragging_ = false;
        axis_ = GizmoAxis::None;
    }

    bool IsDragging() const noexcept {
        return dragging_;
    }

    GizmoAxis ActiveAxis() const noexcept {
        return axis_;
    }

    static glm::mat4 ComposeTransform(
        const GizmoTransform& transform
    ) noexcept;

    static void ApplyToSceneNode(
        SceneNode& node,
        const GizmoTransform& transform
    ) noexcept;
};

} // namespace aetheris
