#include "ObjMeshLoader.h"
#include "engine/core/AetherisLog.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace aetheris {

namespace {

struct ObjIndex final {
    int position{-1};
    int uv{-1};
    int normal{-1};
};

bool ParseInt(
    std::string_view text,
    size_t& offset,
    int& out
) noexcept {
    if (offset >= text.size())
        return false;

    const char* begin = text.data() + offset;
    char* end = nullptr;
    const long value = std::strtol(begin, &end, 10);

    if (end == begin)
        return false;

    out = static_cast<int>(value);
    offset = static_cast<size_t>(end - text.data());
    return true;
}

bool ParseFaceToken(
    std::string_view token,
    ObjIndex& out
) noexcept {
    size_t offset = 0;

    if (!ParseInt(token, offset, out.position))
        return false;

    if (out.position == 0)
        return false;

    if (offset >= token.size())
        return true;

    if (token[offset] != '/')
        return false;

    ++offset;

    if (offset < token.size() && token[offset] != '/') {
        if (!ParseInt(token, offset, out.uv))
            return false;
    }

    if (offset >= token.size())
        return true;

    if (token[offset] != '/')
        return false;

    ++offset;

    if (offset < token.size()) {
        if (!ParseInt(token, offset, out.normal))
            return false;
    }

    return offset == token.size();
}

int ResolveIndex(int index, size_t count) noexcept {
    if (index > 0) {
        const int resolved = index - 1;
        return resolved >= 0 && static_cast<size_t>(resolved) < count
            ? resolved
            : -1;
    }

    if (index < 0) {
        const int resolved =
            static_cast<int>(count) + index;

        return resolved >= 0 &&
                       static_cast<size_t>(resolved) < count
            ? resolved
            : -1;
    }

    return -1;
}

DemoVertex MakeVertex(
    const ObjIndex& index,
    const std::vector<std::array<float,3>>& positions,
    const std::vector<std::array<float,2>>& uvs,
    const std::vector<std::array<float,3>>& normals,
    const Vec4& color,
    float metallic,
    float roughness,
    float ao,
    const std::array<float,3>* generatedNormal
) {
    DemoVertex v{};

    const int p =
        ResolveIndex(index.position, positions.size());

    if (p >= 0) {
        v.position[0] = positions[static_cast<size_t>(p)][0];
        v.position[1] = positions[static_cast<size_t>(p)][1];
        v.position[2] = positions[static_cast<size_t>(p)][2];
    }

    const int uv =
        ResolveIndex(index.uv, uvs.size());

    if (uv >= 0) {
        v.uv[0] = uvs[static_cast<size_t>(uv)][0];
        v.uv[1] = uvs[static_cast<size_t>(uv)][1];
    }

    const int n =
        ResolveIndex(index.normal, normals.size());

    if (n >= 0) {
        v.normal[0] = normals[static_cast<size_t>(n)][0];
        v.normal[1] = normals[static_cast<size_t>(n)][1];
        v.normal[2] = normals[static_cast<size_t>(n)][2];
    } else if (generatedNormal) {
        v.normal[0] = (*generatedNormal)[0];
        v.normal[1] = (*generatedNormal)[1];
        v.normal[2] = (*generatedNormal)[2];
    }

    v.baseColorMetallic[0] = color.x;
    v.baseColorMetallic[1] = color.y;
    v.baseColorMetallic[2] = color.z;
    v.baseColorMetallic[3] = metallic;

    v.roughnessAO[0] = roughness;
    v.roughnessAO[1] = ao;

    return v;
}

std::array<float,3> FaceNormal(
    const DemoVertex& a,
    const DemoVertex& b,
    const DemoVertex& c
) noexcept {
    const float ux = b.position[0] - a.position[0];
    const float uy = b.position[1] - a.position[1];
    const float uz = b.position[2] - a.position[2];

    const float vx = c.position[0] - a.position[0];
    const float vy = c.position[1] - a.position[1];
    const float vz = c.position[2] - a.position[2];

    std::array<float,3> n{
        uy * vz - uz * vy,
        uz * vx - ux * vz,
        ux * vy - uy * vx
    };

    const float len =
        std::sqrt(
            n[0] * n[0] +
            n[1] * n[1] +
            n[2] * n[2]
        );

    if (len > 1e-6f) {
        n[0] /= len;
        n[1] /= len;
        n[2] /= len;
    } else {
        n = {0.0f, 1.0f, 0.0f};
    }

    return n;
}

}

bool ObjMeshLoader::Load(
    const std::filesystem::path& path,
    const Vec4& baseColor,
    float metallic,
    float roughness,
    float ao,
    DemoCpuMesh& out
) const noexcept {
    out.Clear();

    try {
        std::ifstream file(path);

        if (!file) {
            AETHERIS_LOGW(
                "OBJ asset missing: %s",
                path.string().c_str()
            );
            return false;
        }

        std::vector<std::array<float,3>> positions;
        std::vector<std::array<float,2>> uvs;
        std::vector<std::array<float,3>> normals;

        positions.reserve(4096);
        uvs.reserve(4096);
        normals.reserve(4096);

        std::string line;

        while (std::getline(file, line)) {
            std::istringstream stream(line);

            std::string op;
            stream >> op;

            if (op == "v") {
                std::array<float,3> p{};
                stream >> p[0] >> p[1] >> p[2];

                if (!stream ||
                    positions.size() >= 8192) {
                    return false;
                }

                positions.push_back(p);

            } else if (op == "vt") {
                std::array<float,2> uv{};
                stream >> uv[0] >> uv[1];

                if (!stream ||
                    uvs.size() >= 8192) {
                    return false;
                }

                uvs.push_back(uv);

            } else if (op == "vn") {
                std::array<float,3> n{};
                stream >> n[0] >> n[1] >> n[2];

                if (!stream ||
                    normals.size() >= 8192) {
                    return false;
                }

                normals.push_back(n);

            } else if (op == "f") {
                std::array<ObjIndex, 8> face{};
                uint32_t count = 0;

                std::string token;

                while (stream >> token) {
                    if (count >= face.size() ||
                        !ParseFaceToken(token, face[count])) {
                        return false;
                    }

                    ++count;
                }

                if (count < 3)
                    continue;

                for (uint32_t i = 1; i + 1 < count; ++i) {
                    if (out.vertexCount + 3 > kMaxDemoVertices ||
                        out.indexCount + 3 > kMaxDemoIndices) {
                        AETHERIS_LOGW(
                            "OBJ asset exceeds demo mesh limits: %s",
                            path.string().c_str()
                        );
                        return false;
                    }

                    const uint32_t first =
                        out.vertexCount;

                    const DemoVertex va =
                        MakeVertex(
                            face[0],
                            positions,
                            uvs,
                            normals,
                            baseColor,
                            metallic,
                            roughness,
                            ao,
                            nullptr
                        );

                    const DemoVertex vb =
                        MakeVertex(
                            face[i],
                            positions,
                            uvs,
                            normals,
                            baseColor,
                            metallic,
                            roughness,
                            ao,
                            nullptr
                        );

                    const DemoVertex vc =
                        MakeVertex(
                            face[i + 1],
                            positions,
                            uvs,
                            normals,
                            baseColor,
                            metallic,
                            roughness,
                            ao,
                            nullptr
                        );

                    DemoVertex finalA = va;
                    DemoVertex finalB = vb;
                    DemoVertex finalC = vc;

                    const bool missingNormals =
                        face[0].normal == -1 ||
                        face[i].normal == -1 ||
                        face[i + 1].normal == -1;

                    if (missingNormals) {
                        const auto generated =
                            FaceNormal(
                                va,
                                vb,
                                vc
                            );

                        finalA.normal[0] = generated[0];
                        finalA.normal[1] = generated[1];
                        finalA.normal[2] = generated[2];

                        finalB.normal[0] = generated[0];
                        finalB.normal[1] = generated[1];
                        finalB.normal[2] = generated[2];

                        finalC.normal[0] = generated[0];
                        finalC.normal[1] = generated[1];
                        finalC.normal[2] = generated[2];
                    }

                    out.vertices[out.vertexCount++] = finalA;
                    out.vertices[out.vertexCount++] = finalB;
                    out.vertices[out.vertexCount++] = finalC;

                    out.indices[out.indexCount++] = first;
                    out.indices[out.indexCount++] = first + 1;
                    out.indices[out.indexCount++] = first + 2;
                }
            }
        }

        if (out.Empty()) {
            AETHERIS_LOGW(
                "OBJ asset produced no triangles: %s",
                path.string().c_str()
            );
            out.Clear();
            return false;
        }

        AETHERIS_LOGI(
            "OBJ loaded: %s vertices=%u indices=%u",
            path.string().c_str(),
            out.vertexCount,
            out.indexCount
        );

        return true;
    } catch (...) {
        out.Clear();
        AETHERIS_LOGW(
            "OBJ loader exception: %s",
            path.string().c_str()
        );
        return false;
    }
}

}
