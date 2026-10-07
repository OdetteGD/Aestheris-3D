#pragma once

#include "engine/math/GlmCompat.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#define AETHERIS_HAS_NLOHMANN_JSON 1
#endif

namespace aetheris {

class Mesh;

struct AABB final {
    glm::vec3 min{-0.5f, -0.5f, -0.5f};
    glm::vec3 max{ 0.5f,  0.5f,  0.5f};
};

class SceneNode final {
    uint32_t id_{};
    std::string name_{};
    SceneNode* parent_{};
    std::vector<std::unique_ptr<SceneNode>> children_{};

    Mesh* mesh_{};
    AABB localBounds_{};
    AABB worldBounds_{};

    glm::mat4 localTransform_{1.0f};
    glm::mat4 worldTransform_{1.0f};
    bool visible_{true};

    static AABB TransformBounds(
        const AABB& bounds,
        const glm::mat4& transform
    ) noexcept;

public:
    explicit SceneNode(
        uint32_t id = 0u,
        std::string name = {}
    );

    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode(SceneNode&&) = delete;
    SceneNode& operator=(SceneNode&&) = delete;
    ~SceneNode();

    uint32_t Id() const noexcept { return id_; }
    void SetId(uint32_t id) noexcept { id_ = id; }

    const std::string& Name() const noexcept { return name_; }
    void SetName(std::string name);

    SceneNode* Parent() const noexcept { return parent_; }

    Mesh* GetMesh() const noexcept { return mesh_; }
    void SetMesh(Mesh* mesh) noexcept { mesh_ = mesh; }

    const AABB& LocalBounds() const noexcept { return localBounds_; }
    const AABB& WorldBounds() const noexcept { return worldBounds_; }
    void SetLocalBounds(const AABB& bounds) noexcept {
        localBounds_ = bounds;
    }

    glm::mat4& LocalTransform() noexcept { return localTransform_; }
    const glm::mat4& LocalTransform() const noexcept {
        return localTransform_;
    }

    const glm::mat4& WorldTransform() const noexcept {
        return worldTransform_;
    }

    bool Visible() const noexcept { return visible_; }
    void SetVisible(bool visible) noexcept { visible_ = visible; }

    size_t ChildCount() const noexcept { return children_.size(); }
    SceneNode* ChildAt(size_t index) noexcept {
        return index < children_.size() ? children_[index].get() : nullptr;
    }
    const SceneNode* ChildAt(size_t index) const noexcept {
        return index < children_.size() ? children_[index].get() : nullptr;
    }

    SceneNode& AddChild(std::unique_ptr<SceneNode> child);
    SceneNode& CreateChild(uint32_t id, std::string name);
    std::unique_ptr<SceneNode> RemoveChild(uint32_t id) noexcept;

    SceneNode* FindById(uint32_t id) noexcept;
    const SceneNode* FindById(uint32_t id) const noexcept;

    void UpdateHierarchy(
        const glm::mat4& parentTransform
    ) noexcept;

    std::string SerializeJson() const;

#if defined(AETHERIS_HAS_NLOHMANN_JSON)
    void SerializeJson(nlohmann::json& out) const;
    bool DeserializeJson(const nlohmann::json& in);
#endif
};

} // namespace aetheris
