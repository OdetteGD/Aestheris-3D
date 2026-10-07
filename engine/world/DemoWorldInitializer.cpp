#include "DemoWorldInitializer.h"
#include "engine/core/AetherisLog.h"
#include "engine/project/VirtualFileSystem.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace aetheris {

namespace {

constexpr float kSpacing = 2.0f;
constexpr float kWallSpacing = 2.0f;

void AddBox(
    DemoCpuMesh& mesh,
    float hx,
    float hy,
    float hz,
    const float color[3],
    float metallic,
    float roughness,
    float ao
) noexcept {
    constexpr float uv[4][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {1.0f, 1.0f}, {0.0f, 1.0f}
    };

    const float p[6][4][3] = {
        {{-hx,-hy, hz},{ hx,-hy, hz},{ hx, hy, hz},{-hx, hy, hz}},
        {{ hx,-hy,-hz},{-hx,-hy,-hz},{-hx, hy,-hz},{ hx, hy,-hz}},
        {{-hx, hy, hz},{ hx, hy, hz},{ hx, hy,-hz},{-hx, hy,-hz}},
        {{-hx,-hy,-hz},{ hx,-hy,-hz},{ hx,-hy, hz},{-hx,-hy, hz}},
        {{ hx,-hy, hz},{ hx,-hy,-hz},{ hx, hy,-hz},{ hx, hy, hz}},
        {{-hx,-hy,-hz},{-hx,-hy, hz},{-hx, hy, hz},{-hx, hy,-hz}}
    };

    const float n[6][3] = {
        {0,0,1},{0,0,-1},{0,1,0},
        {0,-1,0},{1,0,0},{-1,0,0}
    };

    const uint32_t first = mesh.vertexCount;
    const uint32_t faces[6] = {0,1,2, 0,2,3};

    for (uint32_t face = 0; face < 6; ++face) {
        if (mesh.vertexCount + 4 > kMaxDemoVertices ||
            mesh.indexCount + 6 > kMaxDemoIndices) {
            return;
        }

        for (uint32_t corner = 0; corner < 4; ++corner) {
            DemoVertex v{};
            v.position[0] = p[face][corner][0];
            v.position[1] = p[face][corner][1];
            v.position[2] = p[face][corner][2];
            v.normal[0] = n[face][0];
            v.normal[1] = n[face][1];
            v.normal[2] = n[face][2];
            v.uv[0] = uv[corner][0];
            v.uv[1] = uv[corner][1];
            v.baseColorMetallic[0] = color[0];
            v.baseColorMetallic[1] = color[1];
            v.baseColorMetallic[2] = color[2];
            v.baseColorMetallic[3] = metallic;
            v.roughnessAO[0] = roughness;
            v.roughnessAO[1] = ao;
            mesh.vertices[mesh.vertexCount++] = v;
        }

        for (uint32_t j = 0; j < 6; ++j) {
            mesh.indices[mesh.indexCount++] = first + faces[j];
        }
    }
}

void AddSlope(DemoCpuMesh& mesh) noexcept {
    if (mesh.vertexCount + 8 > kMaxDemoVertices ||
        mesh.indexCount + 18 > kMaxDemoIndices) {
        return;
    }

    const float c[3] = {0.20f, 0.34f, 0.48f};
    const float pts[8][3] = {
        {-1,-1,-1}, {1,-1,-1}, {1,-1,1}, {-1,-1,1},
        {-1,1,-1}, {1,1,1}, {1,-1,1}, {-1,-1,-1}
    };

    const float normals[8][3] = {
        {0,-1,0}, {0,-1,0}, {0,-0.7071f,0.7071f}, {0,-0.7071f,0.7071f},
        {0,-0.7071f,0.7071f}, {0,-0.7071f,0.7071f},
        {0,0.7071f,0.7071f}, {0,0.7071f,0.7071f}
    };

    const uint32_t base = mesh.vertexCount;
    for (uint32_t i = 0; i < 8; ++i) {
        DemoVertex v{};
        v.position[0] = pts[i][0];
        v.position[1] = pts[i][1];
        v.position[2] = pts[i][2];
        v.normal[0] = normals[i][0];
        v.normal[1] = normals[i][1];
        v.normal[2] = normals[i][2];
        v.uv[0] = (pts[i][0] + 1.0f) * 0.5f;
        v.uv[1] = (pts[i][2] + 1.0f) * 0.5f;
        v.baseColorMetallic[0] = c[0];
        v.baseColorMetallic[1] = c[1];
        v.baseColorMetallic[2] = c[2];
        v.roughnessAO[0] = 0.6f;
        v.roughnessAO[1] = 1.0f;
        mesh.vertices[mesh.vertexCount++] = v;
    }

    constexpr uint32_t idx[] = {
        0,1,2, 0,2,3,
        0,4,5, 0,5,2,
        3,2,5, 3,5,7
    };

    for (uint32_t i : idx) mesh.indices[mesh.indexCount++] = base + i;
}

void AddStairs(DemoCpuMesh& mesh) noexcept {
    const float c[3] = {0.26f, 0.38f, 0.52f};
    for (uint32_t step = 0; step < 6; ++step) {
        if (mesh.vertexCount + 24 > kMaxDemoVertices ||
            mesh.indexCount + 36 > kMaxDemoIndices) {
            return;
        }
        AddBox(
            mesh,
            1.0f,
            0.18f + static_cast<float>(step) * 0.16f,
            0.65f,
            c,
            0.0f,
            0.78f,
            1.0f
        );

        // Move the just-created step upward and backward.
        const float ox = 0.0f;
        const float oy = static_cast<float>(step) * 0.32f;
        const float oz = static_cast<float>(step) * 0.28f;
        const uint32_t first = mesh.vertexCount - 24;
        for (uint32_t i = first; i < mesh.vertexCount; ++i) {
            mesh.vertices[i].position[0] += ox;
            mesh.vertices[i].position[1] += oy;
            mesh.vertices[i].position[2] += oz;
        }
    }
}

}

const char* DemoWorldInitializer::AssetPath(DemoMeshSlot slot) noexcept {
    switch (slot) {
        case DemoMeshSlot::Ground: return "assets/models/kenney/floor-square.obj";
        case DemoMeshSlot::Wall: return "assets/models/kenney/wall.obj";
        case DemoMeshSlot::Ramp: return "assets/models/kenney/shape-slope.obj";
        case DemoMeshSlot::Stairs: return "assets/models/kenney/stairs.obj";
        case DemoMeshSlot::Crate: return "assets/models/kenney/crate.obj";
        case DemoMeshSlot::Column: return "assets/models/kenney/column.obj";
        case DemoMeshSlot::Cube: return "assets/models/kenney/floor-thick.obj";
        default: return "";
    }
}

bool DemoWorldInitializer::AppendItem(
    SceneSnapshot& scene,
    DemoMeshSlot mesh,
    const Vec4& position,
    const Vec4& rotation,
    const Vec4& scale,
    uint32_t materialId
) noexcept {
    if (scene.transforms.size() >= kMaxDemoItems ||
        scene.renderItems.size() >= kMaxDemoItems) {
        return false;
    }

    RenderItem item{};
    item.meshId = static_cast<uint32_t>(mesh);
    item.materialId = materialId;
    item.transformIndex = static_cast<uint32_t>(scene.transforms.size());
    item.sortKey = item.meshId;

    Transform transform{};
    transform.position = position;
    transform.rotation = rotation;
    transform.scale = scale;

    scene.transforms.push_back(transform);
    scene.renderItems.push_back(item);
    return true;
}

void DemoWorldInitializer::AddGrid(
    SceneSnapshot& scene,
    uint32_t& materialId
) noexcept {
    for (int z = -5; z <= 5; ++z) {
        for (int x = -5; x <= 5; ++x) {
            AppendItem(
                scene,
                DemoMeshSlot::Ground,
                {x * kSpacing, 0.0f, z * kSpacing, 1.0f},
                {0,0,0,1},
                {1,1,1,0},
                materialId++
            );
        }
    }

    for (int x = -6; x <= 6; ++x) {
        for (int side : {-6, 6}) {
            AppendItem(
                scene,
                DemoMeshSlot::Wall,
                {x * kWallSpacing, 1.0f, side * kWallSpacing, 1.0f},
                {0, side > 0 ? 3.14159265f : 0.0f, 0, 1},
                {1,1,1,0},
                materialId++
            );
        }
    }

    for (int z = -5; z <= 5; ++z) {
        for (int side : {-6, 6}) {
            AppendItem(
                scene,
                DemoMeshSlot::Wall,
                {side * kWallSpacing, 1.0f, z * kWallSpacing, 1.0f},
                {0, side < 0 ? 1.5707963f : -1.5707963f, 0, 1},
                {1,1,1,0},
                materialId++
            );
        }
    }
}

void DemoWorldInitializer::AddArenaProps(
    SceneSnapshot& scene,
    uint32_t& materialId
) noexcept {
    AppendItem(scene, DemoMeshSlot::Ramp, {-4, 0.9f, 0, 1}, {0,0,0,1}, {1.3f,1.3f,1.3f,0}, materialId++);
    AppendItem(scene, DemoMeshSlot::Stairs, {4, 0.0f, 0, 1}, {0,0,0,1}, {1.0f,1.0f,1.0f,0}, materialId++);

    for (int z = -2; z <= 2; ++z) {
        AppendItem(scene, DemoMeshSlot::Crate, {-2.0f, 0.9f, z * 2.0f, 1}, {0,0,0,1}, {0.9f,0.9f,0.9f,0}, materialId++);
        AppendItem(scene, DemoMeshSlot::Crate, {2.0f, 0.9f, z * 2.0f, 1}, {0,0.35f,0,1}, {0.9f,0.9f,0.9f,0}, materialId++);
    }

    for (int z = -2; z <= 2; ++z) {
        AppendItem(scene, DemoMeshSlot::Column, {0,1.5f,z * 3.0f,1}, {0,0,0,1}, {0.9f,1.5f,0.9f,0}, materialId++);
    }
}

bool DemoWorldInitializer::Initialize(
    const std::filesystem::path& projectRoot,
    SceneSnapshot& scene
) const noexcept {
    try {
        scene.transforms.clear();
        scene.renderItems.clear();
        scene.transforms.reserve(kMaxDemoItems);
        scene.renderItems.reserve(kMaxDemoItems);

        VirtualFileSystem vfs(projectRoot);
        if (!vfs.MountNewProject()) {
            AETHERIS_LOGW("DemoWorld: project mount failed; using procedural world");
        }

        uint32_t material = 1;
        AddGrid(scene, material);
        AddArenaProps(scene, material);

        scene.revision++;
        AETHERIS_LOGI(
            "DemoWorld initialized: items=%zu, kenneyRoot=%s",
            scene.renderItems.size(),
            (projectRoot / "assets/models/kenney").string().c_str()
        );
        return !scene.renderItems.empty();
    } catch (...) {
        AETHERIS_LOGE("DemoWorld initialization threw; falling back to minimal scene");
        scene.transforms.clear();
        scene.renderItems.clear();

        uint32_t material = 1;
        AppendItem(
            scene,
            DemoMeshSlot::Cube,
            {0,0,0,1},
            {0,0,0,1},
            {3,1,3,0},
            material
        );
        scene.revision++;
        return !scene.renderItems.empty();
    }
}

void DemoWorldInitializer::FallbackMesh(
    DemoMeshSlot slot,
    DemoCpuMesh& out
) noexcept {
    out.Clear();

    switch (slot) {
        case DemoMeshSlot::Ground: {
            const float c[3] = {0.09f, 0.13f, 0.18f};
            AddBox(out, 1.0f, 0.08f, 1.0f, c, 0.0f, 0.90f, 1.0f);
            break;
        }
        case DemoMeshSlot::Wall: {
            const float c[3] = {0.16f, 0.20f, 0.28f};
            AddBox(out, 1.0f, 1.0f, 0.16f, c, 0.05f, 0.72f, 1.0f);
            break;
        }
        case DemoMeshSlot::Ramp:
            AddSlope(out);
            break;
        case DemoMeshSlot::Stairs:
            AddStairs(out);
            break;
        case DemoMeshSlot::Crate: {
            const float c[3] = {0.45f, 0.20f, 0.08f};
            AddBox(out, 0.65f, 0.65f, 0.65f, c, 0.0f, 0.62f, 1.0f);
            break;
        }
        case DemoMeshSlot::Column: {
            const float c[3] = {0.28f, 0.31f, 0.36f};
            AddBox(out, 0.55f, 1.0f, 0.55f, c, 0.0f, 0.70f, 1.0f);
            break;
        }
        default: {
            const float c[3] = {0.22f, 0.26f, 0.32f};
            AddBox(out, 1.0f, 0.5f, 1.0f, c, 0.0f, 0.80f, 1.0f);
            break;
        }
    }
}

}
