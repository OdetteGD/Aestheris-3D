#include "SceneNode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace aetheris {

namespace {

glm::vec3 TransformPoint(
    const glm::mat4& matrix,
    const glm::vec3& point
) noexcept {
    const glm::vec4 v = matrix * glm::vec4(point, 1.0f);
    const float invW = std::fabs(v.w) > 1e-8f ? 1.0f / v.w : 1.0f;
    return {v.x * invW, v.y * invW, v.z * invW};
}

void AppendEscaped(
    std::string& out,
    const std::string& text
) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
}

void AppendMatrix(
    std::string& out,
    const glm::mat4& matrix
) {
    out.push_back('[');
    for (size_t i = 0u; i < 16u; ++i) {
        if (i) out.push_back(',');
        char value[32]{};
        const int written = std::snprintf(
            value,
            sizeof(value),
            "%.9g",
            aetheris_glm_compat::MatElement(matrix, i)
        );
        if (written > 0)
            out.append(value, static_cast<size_t>(written));
    }
    out.push_back(']');
}

} // namespace

SceneNode::SceneNode(
    uint32_t id,
    std::string name
)
    : id_(id),
      name_(std::move(name)) {
}

SceneNode::~SceneNode() = default;

void SceneNode::SetName(std::string name) {
    name_ = std::move(name);
}

AABB SceneNode::TransformBounds(
    const AABB& bounds,
    const glm::mat4& transform
) noexcept {
    const std::array<glm::vec3, 8u> corners = {
        glm::vec3{bounds.min.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.max.z}
    };

    AABB result{};
    result.min = TransformPoint(transform, corners[0]);
    result.max = result.min;

    for (size_t i = 1u; i < corners.size(); ++i) {
        const glm::vec3 point = TransformPoint(transform, corners[i]);

        result.min.x = std::min(result.min.x, point.x);
        result.min.y = std::min(result.min.y, point.y);
        result.min.z = std::min(result.min.z, point.z);

        result.max.x = std::max(result.max.x, point.x);
        result.max.y = std::max(result.max.y, point.y);
        result.max.z = std::max(result.max.z, point.z);
    }

    return result;
}

SceneNode& SceneNode::AddChild(
    std::unique_ptr<SceneNode> child
) {
    if (!child)
        return *this;

    child->parent_ = this;
    children_.push_back(std::move(child));
    return *children_.back();
}

SceneNode& SceneNode::CreateChild(
    uint32_t id,
    std::string name
) {
    return AddChild(
        std::make_unique<SceneNode>(
            id,
            std::move(name)
        )
    );
}

std::unique_ptr<SceneNode> SceneNode::RemoveChild(
    uint32_t id
) noexcept {
    const auto it = std::find_if(
        children_.begin(),
        children_.end(),
        [id](const std::unique_ptr<SceneNode>& node) noexcept {
            return node && node->id_ == id;
        }
    );

    if (it == children_.end())
        return {};

    std::unique_ptr<SceneNode> result = std::move(*it);
    children_.erase(it);

    if (result)
        result->parent_ = nullptr;

    return result;
}

SceneNode* SceneNode::FindById(uint32_t id) noexcept {
    if (id_ == id)
        return this;

    for (const auto& child : children_) {
        if (!child)
            continue;

        if (SceneNode* found = child->FindById(id))
            return found;
    }

    return nullptr;
}

const SceneNode* SceneNode::FindById(uint32_t id) const noexcept {
    if (id_ == id)
        return this;

    for (const auto& child : children_) {
        if (!child)
            continue;

        if (const SceneNode* found = child->FindById(id))
            return found;
    }

    return nullptr;
}

void SceneNode::UpdateHierarchy(
    const glm::mat4& parentTransform
) noexcept {
    worldTransform_ = parentTransform * localTransform_;
    worldBounds_ = TransformBounds(
        localBounds_,
        worldTransform_
    );

    for (const auto& child : children_) {
        if (child)
            child->UpdateHierarchy(worldTransform_);
    }
}

std::string SceneNode::SerializeJson() const {
    std::string out;
    out.reserve(768u);

    out += "{\"id\":";
    out += std::to_string(id_);
    out += ",\"name\":";
    AppendEscaped(out, name_);
    out += ",\"visible\":";
    out += visible_ ? "true" : "false";
    out += ",\"localTransform\":";
    AppendMatrix(out, localTransform_);
    out += ",\"children\":[";
    
    for (size_t i = 0u; i < children_.size(); ++i) {
        if (i)
            out.push_back(',');

        if (children_[i])
            out += children_[i]->SerializeJson();
    }

    out += "]}";
    return out;
}

#if defined(AETHERIS_HAS_NLOHMANN_JSON)

void SceneNode::SerializeJson(
    nlohmann::json& out
) const {
    out = nlohmann::json::object();
    out["id"] = id_;
    out["name"] = name_;
    out["visible"] = visible_;

    nlohmann::json matrix = nlohmann::json::array();
    for (size_t i = 0u; i < 16u; ++i)
        matrix.push_back(aetheris_glm_compat::MatElement(localTransform_, i));

    out["localTransform"] = std::move(matrix);
    out["children"] = nlohmann::json::array();

    for (const auto& child : children_) {
        if (!child)
            continue;

        nlohmann::json childJson;
        child->SerializeJson(childJson);
        out["children"].push_back(std::move(childJson));
    }
}

bool SceneNode::DeserializeJson(
    const nlohmann::json& in
) {
    if (!in.is_object())
        return false;

    if (in.contains("id") &&
        in["id"].is_number_unsigned()) {
        id_ = in["id"].get<uint32_t>();
    }

    if (in.contains("name") &&
        in["name"].is_string()) {
        name_ = in["name"].get<std::string>();
    }

    if (in.contains("visible") &&
        in["visible"].is_boolean()) {
        visible_ = in["visible"].get<bool>();
    }

    if (in.contains("localTransform") &&
        in["localTransform"].is_array() &&
        in["localTransform"].size() == 16u) {
        for (size_t i = 0u; i < 16u; ++i) {
            if (!in["localTransform"][i].is_number())
                return false;

            aetheris_glm_compat::SetMatElement(
                localTransform_,
                i,
                in["localTransform"][i].get<float>()
            );
        }
    }

    children_.clear();

    if (in.contains("children") &&
        in["children"].is_array()) {
        for (const auto& childJson : in["children"]) {
            auto child = std::make_unique<SceneNode>();
            if (!child->DeserializeJson(childJson))
                return false;

            child->parent_ = this;
            children_.push_back(std::move(child));
        }
    }

    return true;
}

#endif

} // namespace aetheris
