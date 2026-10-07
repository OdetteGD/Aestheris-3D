#pragma once
#include <android/native_window.h>
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace aetheris {
enum class RenderAPI : uint8_t { VULKAN, OPENGL_ES3 };
struct alignas(16) Vec4 { float x{},y{},z{},w{}; };
struct alignas(16) Mat4 { std::array<float,16> m{}; };
struct alignas(16) Transform { Vec4 position{0,0,0,1}; Vec4 rotation{0,0,0,1}; Vec4 scale{1,1,1,0}; };
struct RenderItem { uint32_t meshId{}, materialId{}, transformIndex{}, sortKey{}; };
class RenderQueue { std::span<const RenderItem> items_; public: explicit RenderQueue(std::span<const RenderItem> i) noexcept:items_(i){} auto Items()const noexcept{return items_;} };
struct SceneSnapshot { uint64_t revision{}; std::vector<Transform> transforms; };
struct GizmoCommand { enum class Type:uint8_t{Translate,Rotate,Scale}; Type type{Type::Translate}; uint32_t entity{}; Vec4 delta{}; };
}