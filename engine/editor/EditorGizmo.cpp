#include "EditorGizmo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aetheris {

namespace {

constexpr float kEpsilon = 1e-6f;
constexpr float kInfinity = std::numeric_limits<float>::max();

glm::vec3 SafeNormalize(
    const glm::vec3& vector,
    const glm::vec3& fallback
) noexcept {
    const float length = glm::length(vector);
    return length > kEpsilon
        ? vector / length
        : fallback;
}

} // namespace

glm::vec3 EditorGizmo::AxisVector(
    GizmoAxis axis
) noexcept {
    switch (axis) {
        case GizmoAxis::X: return {1.0f, 0.0f, 0.0f};
        case GizmoAxis::Y: return {0.0f, 1.0f, 0.0f};
        case GizmoAxis::Z: return {0.0f, 0.0f, 1.0f};
        case GizmoAxis::XYZ: return {1.0f, 1.0f, 1.0f};
        default: return {0.0f, 0.0f, 0.0f};
    }
}

bool EditorGizmo::IntersectPlane(
    const EditorRay& ray,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal,
    float& outT,
    glm::vec3& outPoint
) noexcept {
    const glm::vec3 normal =
        SafeNormalize(
            planeNormal,
            {0.0f, 1.0f, 0.0f}
        );

    const float denominator =
        glm::dot(normal, ray.direction);

    if (std::fabs(denominator) <= kEpsilon)
        return false;

    const float t =
        glm::dot(
            planePoint - ray.origin,
            normal
        ) / denominator;

    if (t < 0.0f)
        return false;

    outT = t;
    outPoint =
        ray.origin +
        ray.direction * t;
    return true;
}

float EditorGizmo::RaySegmentDistanceSquared(
    const EditorRay& ray,
    const glm::vec3& a,
    const glm::vec3& b,
    float& outRayT
) noexcept {
    const glm::vec3 v = b - a;
    const glm::vec3 w = ray.origin - a;

    const float aa = glm::dot(ray.direction, ray.direction);
    const float ab = glm::dot(ray.direction, v);
    const float bb = glm::dot(v, v);
    const float aw = glm::dot(ray.direction, w);
    const float bw = glm::dot(v, w);

    if (aa <= kEpsilon || bb <= kEpsilon)
        return kInfinity;

    const float denominator =
        aa * bb -
        ab * ab;

    float rayT = 0.0f;
    float segmentT = 0.0f;

    if (std::fabs(denominator) > kEpsilon) {
        rayT = (ab * bw - bb * aw) / denominator;
        segmentT = (aa * bw - ab * aw) / denominator;
    }

    rayT = std::max(0.0f, rayT);
    segmentT = std::clamp(segmentT, 0.0f, 1.0f);

    const glm::vec3 pointOnRay =
        ray.origin +
        ray.direction * rayT;
    const glm::vec3 pointOnSegment =
        a +
        v * segmentT;

    outRayT = rayT;

    const glm::vec3 delta =
        pointOnRay -
        pointOnSegment;

    return glm::dot(delta, delta);
}

bool EditorGizmo::PickAxisHandle(
    const EditorRay& ray,
    GizmoAxis axis,
    float gizmoScale,
    GizmoPick& out
) const noexcept {
    if (axis == GizmoAxis::None ||
        axis == GizmoAxis::XYZ)
        return false;

    const glm::vec3 direction =
        SafeNormalize(
            AxisVector(axis),
            {1.0f, 0.0f, 0.0f}
        );

    const glm::vec3 start =
        pivot_ +
        direction *
        (0.12f * gizmoScale);

    const glm::vec3 end =
        pivot_ +
        direction *
        (handleLength_ * gizmoScale);

    float rayT = 0.0f;

    const float distanceSquared =
        RaySegmentDistanceSquared(
            ray,
            start,
            end,
            rayT
        );

    const float radius =
        std::max(
            pickRadius_ * gizmoScale,
            0.03f
        );

    if (distanceSquared > radius * radius)
        return false;

    out.axis = axis;
    out.distance = std::sqrt(distanceSquared);
    out.rayDistance = rayT;
    return true;
}

bool EditorGizmo::PickRotateRing(
    const EditorRay& ray,
    GizmoAxis axis,
    float gizmoScale,
    GizmoPick& out
) const noexcept {
    const glm::vec3 axisVector =
        SafeNormalize(
            AxisVector(axis),
            {0.0f, 1.0f, 0.0f}
        );

    float t = 0.0f;
    glm::vec3 point{};

    if (!IntersectPlane(
            ray,
            pivot_,
            axisVector,
            t,
            point))
        return false;

    const float distance =
        glm::length(point - pivot_);

    const float radius =
        ringRadius_ * gizmoScale;

    const float tolerance =
        std::max(
            pickRadius_ * gizmoScale,
            0.035f
        );

    if (std::fabs(distance - radius) > tolerance)
        return false;

    out.axis = axis;
    out.distance = std::fabs(distance - radius);
    out.rayDistance = t;
    return true;
}

bool EditorGizmo::Pick(
    const EditorRay& ray,
    const glm::vec3& pivot,
    float gizmoScale,
    GizmoPick& out
) noexcept {
    if (gizmoScale <= 0.0f)
        return false;

    out = {};
    pivot_ = pivot;

    bool found = false;
    GizmoPick best{};
    best.distance = kInfinity;

    const std::array<GizmoAxis, 3u> axes = {
        GizmoAxis::X,
        GizmoAxis::Y,
        GizmoAxis::Z
    };

    for (const GizmoAxis axis : axes) {
        GizmoPick candidate{};

        const bool hit =
            mode_ == GizmoMode::Rotate
                ? PickRotateRing(
                    ray,
                    axis,
                    gizmoScale,
                    candidate)
                : PickAxisHandle(
                    ray,
                    axis,
                    gizmoScale,
                    candidate);

        if (!hit)
            continue;

        if (!found ||
            candidate.distance < best.distance) {
            found = true;
            best = candidate;
        }
    }

    if (!found &&
        mode_ == GizmoMode::Move) {
        float t = 0.0f;
        glm::vec3 point{};

        if (IntersectPlane(
                ray,
                pivot,
                {
                    0.0f,
                    1.0f,
                    0.0f
                },
                t,
                point)) {
            const float radius =
                0.14f * gizmoScale;

            if (glm::length(point - pivot) <= radius) {
                best.axis = GizmoAxis::XYZ;
                best.distance = glm::length(point - pivot);
                best.rayDistance = t;
                found = true;
            }
        }
    }

    if (found)
        out = best;

    return found;
}

bool EditorGizmo::BeginDrag(
    const EditorRay& ray,
    const glm::vec3& pivot,
    float gizmoScale,
    const GizmoTransform& initial,
    GizmoPick pick,
    const glm::vec3& cameraForward
) noexcept {
    if (pick.axis == GizmoAxis::None ||
        gizmoScale <= 0.0f)
        return false;

    pivot_ = pivot;
    start_ = initial;
    axis_ = pick.axis;
    dragging_ = false;

    if (mode_ == GizmoMode::Rotate) {
        const glm::vec3 axis =
            SafeNormalize(
                AxisVector(axis_),
                {0.0f, 1.0f, 0.0f}
            );

        float t = 0.0f;
        glm::vec3 point{};

        if (!IntersectPlane(
                ray,
                pivot_,
                axis,
                t,
                point))
            return false;

        dragStartVector_ =
            SafeNormalize(
                point - pivot_,
                glm::cross(
                    axis,
                    {0.0f, 1.0f, 0.0f}
                )
            );

        dragPlaneNormal_ = axis;
    } else if (axis_ == GizmoAxis::XYZ) {
        dragPlaneNormal_ =
            SafeNormalize(
                cameraForward,
                {0.0f, 1.0f, 0.0f}
            );

        float t = 0.0f;
        if (!IntersectPlane(
                ray,
                pivot_,
                dragPlaneNormal_,
                t,
                dragStartPoint_))
            return false;
    } else {
        const glm::vec3 axis =
            SafeNormalize(
                AxisVector(axis_),
                {1.0f, 0.0f, 0.0f}
            );

        glm::vec3 candidate =
            cameraForward -
            axis *
            glm::dot(cameraForward, axis);

        if (glm::length(candidate) <= kEpsilon)
            candidate = glm::cross(
                axis,
                {0.0f, 1.0f, 0.0f}
            );

        if (glm::length(candidate) <= kEpsilon)
            candidate = glm::cross(
                axis,
                {0.0f, 0.0f, 1.0f}
            );

        dragPlaneNormal_ =
            SafeNormalize(
                candidate,
                {0.0f, 1.0f, 0.0f}
            );

        float t = 0.0f;

        if (!IntersectPlane(
                ray,
                pivot_,
                dragPlaneNormal_,
                t,
                dragStartPoint_))
            return false;
    }

    dragging_ = true;
    return true;
}

bool EditorGizmo::UpdateDrag(
    const EditorRay& ray,
    const glm::vec3& cameraForward,
    GizmoTransform& ioTransform
) noexcept {
    if (!dragging_ ||
        axis_ == GizmoAxis::None)
        return false;

    if (mode_ == GizmoMode::Rotate) {
        const glm::vec3 axis =
            SafeNormalize(
                AxisVector(axis_),
                {0.0f, 1.0f, 0.0f}
            );

        float t = 0.0f;
        glm::vec3 point{};

        if (!IntersectPlane(
                ray,
                pivot_,
                axis,
                t,
                point))
            return false;

        const glm::vec3 currentVector =
            SafeNormalize(
                point - pivot_,
                dragStartVector_
            );

        const float sine =
            glm::dot(
                axis,
                glm::cross(
                    dragStartVector_,
                    currentVector
                )
            );

        const float cosine =
            std::clamp(
                glm::dot(
                    dragStartVector_,
                    currentVector
                ),
                -1.0f,
                1.0f
            );

        const float angle =
            std::atan2(sine, cosine);

        const glm::quat delta =
            glm::angleAxis(
                angle,
                axis
            );

        ioTransform = start_;
        ioTransform.rotation =
            glm::normalize(
                delta *
                start_.rotation
            );
        return true;
    }

    float t = 0.0f;
    glm::vec3 point{};

    if (!IntersectPlane(
            ray,
            pivot_,
            dragPlaneNormal_,
            t,
            point))
        return false;

    if (axis_ == GizmoAxis::XYZ) {
        const glm::vec3 delta =
            point -
            dragStartPoint_;

        const float amount =
            glm::dot(
                delta,
                SafeNormalize(
                    cameraForward,
                    {0.0f, 0.0f, -1.0f}
                )
            );

        ioTransform = start_;

        if (mode_ == GizmoMode::Move) {
            ioTransform.position =
                start_.position +
                delta;
        } else {
            const float factor =
                std::max(
                    0.01f,
                    1.0f + amount
                );
            ioTransform.scale =
                start_.scale *
                factor;
        }

        return true;
    }

    const glm::vec3 axis =
        SafeNormalize(
            AxisVector(axis_),
            {1.0f, 0.0f, 0.0f}
        );

    const float amount =
        glm::dot(
            point - dragStartPoint_,
            axis
        );

    ioTransform = start_;

    if (mode_ == GizmoMode::Move) {
        ioTransform.position =
            start_.position +
            axis * amount;
    } else {
        const float base =
            std::max(
                std::fabs(
                    start_.scale.x +
                    start_.scale.y +
                    start_.scale.z
                ) / 3.0f,
                0.001f
            );

        const float factor =
            std::max(
                0.01f,
                1.0f + amount / base
            );

        if (axis_ == GizmoAxis::X) {
            ioTransform.scale.x =
                std::max(
                    0.001f,
                    start_.scale.x * factor
                );
        } else if (axis_ == GizmoAxis::Y) {
            ioTransform.scale.y =
                std::max(
                    0.001f,
                    start_.scale.y * factor
                );
        } else {
            ioTransform.scale.z =
                std::max(
                    0.001f,
                    start_.scale.z * factor
                );
        }
    }

    return true;
}

glm::mat4 EditorGizmo::ComposeTransform(
    const GizmoTransform& transform
) noexcept {
    glm::mat4 result =
        glm::toMat4(
            transform.rotation
        );

    result[0][0] *= transform.scale.x;
    result[0][1] *= transform.scale.x;
    result[0][2] *= transform.scale.x;

    result[1][0] *= transform.scale.y;
    result[1][1] *= transform.scale.y;
    result[1][2] *= transform.scale.y;

    result[2][0] *= transform.scale.z;
    result[2][1] *= transform.scale.z;
    result[2][2] *= transform.scale.z;

    result[3][0] = transform.position.x;
    result[3][1] = transform.position.y;
    result[3][2] = transform.position.z;
    result[3][3] = 1.0f;

    return result;
}

void EditorGizmo::ApplyToSceneNode(
    SceneNode& node,
    const GizmoTransform& transform
) noexcept {
    node.LocalTransform() =
        ComposeTransform(transform);
}

} // namespace aetheris
