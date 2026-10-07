#include "GLES3Renderer.h"

#include "engine/assets/ObjMeshLoader.h"
#include "engine/core/AetherisLog.h"

#include <android/native_window.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include "engine/world/DemoWorldInitializer.h"

namespace aetheris {

namespace {

constexpr char kLogTag[] = "AetherisEditor_GLES";
constexpr float kPi = 3.14159265358979323846f;
constexpr uint32_t kMinStableFrames = 2u;
constexpr uint32_t kDefaultShadowSize = 1024u;

constexpr EGLint kConfigWithStencil[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
};

constexpr EGLint kConfigDepthOnly[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    EGL_STENCIL_SIZE, 0,
    EGL_NONE
};

constexpr EGLint kContextAttributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE
};

constexpr char kTier2Vertex[] = R"GLSL(
#version 300 es
precision highp float;

layout(location=0) in vec3 InPosition;
layout(location=1) in vec3 InNormal;
layout(location=2) in vec2 InUV;
layout(location=3) in vec4 InBaseColorMetallic;
layout(location=4) in vec2 InRoughnessAO;

layout(std140,binding=0) uniform FrameBlock {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
};

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 WorldPos;
out vec3 WorldNormal;
out vec2 UV;
out vec4 Material;
out vec2 RoughnessAO;

void main() {
    vec4 world = uModel * vec4(InPosition, 1.0);
    WorldPos = world.xyz;
    WorldNormal = normalize(mat3(transpose(inverse(uModel))) * InNormal);
    UV = InUV;
    Material = InBaseColorMetallic;
    RoughnessAO = InRoughnessAO;
    gl_Position = inverse(InvViewProj) * world;
}
)GLSL";

constexpr char kTier2Fragment[] = R"GLSL(
#version 300 es
precision highp float;

in vec3 WorldPos;
in vec3 WorldNormal;
in vec2 UV;
in vec4 Material;
in vec2 RoughnessAO;

layout(location=0) out vec4 FragColor;

layout(std140,binding=0) uniform FrameBlock {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
};

uniform sampler2D uAlbedo;
uniform sampler2D uNormal;

void main() {
    vec3 albedo = texture(uAlbedo, UV).rgb * Material.rgb;
    vec3 encodedNormal = texture(uNormal, UV).xyz * 2.0 - 1.0;
    vec3 N = normalize(WorldNormal);

    vec3 T = normalize(
        dFdx(WorldPos) * dFdy(UV).y -
        dFdy(WorldPos) * dFdx(UV).y
    );
    T = normalize(T - N * dot(N, T));
    vec3 B = normalize(cross(N, T));
    N = normalize(mat3(T, B, N) * encodedNormal);

    vec3 L = normalize(-SunDirection.xyz);
    vec3 V = normalize(CameraPosition.xyz - WorldPos);
    vec3 H = normalize(L + V);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0);
    float NoH = max(dot(N, H), 0.0);

    float diffuse = NoL * 0.92;
    float specularPower = mix(16.0, 64.0, 1.0 - clamp(RoughnessAO.x, 0.0, 1.0));
    float specular = pow(NoH, specularPower) * NoL * mix(0.04, 0.35, Material.a);

    vec3 ambient = albedo * (0.12 + 0.28 * max(N.y, 0.0)) * RoughnessAO.y;
    vec3 lighting = ambient + albedo * SunColor.rgb * diffuse + SunColor.rgb * specular * NoV;

    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    vec3 mapped = clamp((lighting * (a * lighting + b)) / (lighting * (c * lighting + d) + e), 0.0, 1.0);
    FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

constexpr char kTier3Vertex[] = R"GLSL(
#version 300 es
precision highp float;

layout(location=0) in vec3 InPosition;
layout(location=1) in vec3 InNormal;
layout(location=2) in vec2 InUV;
layout(location=3) in vec4 InBaseColorMetallic;
layout(location=4) in vec2 InRoughnessAO;

layout(std140,binding=0) uniform FrameBlock {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
};

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uLightViewProj;

out vec3 WorldPos;
out vec3 WorldNormal;
out vec2 UV;
out vec4 Material;
out vec2 RoughnessAO;
out vec4 ShadowCoord;

void main() {
    vec4 world = uModel * vec4(InPosition, 1.0);
    WorldPos = world.xyz;
    WorldNormal = normalize(mat3(transpose(inverse(uModel))) * InNormal);
    UV = InUV;
    Material = InBaseColorMetallic;
    RoughnessAO = InRoughnessAO;
    ShadowCoord = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
)GLSL";

constexpr char kTier3Fragment[] = R"GLSL(
#version 300 es
precision highp float;

in vec3 WorldPos;
in vec3 WorldNormal;
in vec2 UV;
in vec4 Material;
in vec2 RoughnessAO;
in vec4 ShadowCoord;

layout(location=0) out vec4 FragColor;

layout(std140,binding=0) uniform FrameBlock {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
};

uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uOrm;
uniform sampler2DShadow uShadow;

const float PI = 3.14159265359;

float DistributionGGX(float NoH, float roughness) {
    float a = max(0.045, roughness);
    float a2 = a * a;
    float f = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * f * f, 1e-5);
}

float GeometrySchlickGGX(float NoV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NoV / max(NoV * (1.0 - k) + k, 1e-5);
}

vec3 FresnelSchlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
}

float ShadowPCF(vec3 coord) {
    vec3 q = coord.xyz / max(coord.w, 1e-5);
    q = q * 0.5 + 0.5;

    if (q.x <= 0.0 || q.x >= 1.0 || q.y <= 0.0 || q.y >= 1.0 || q.z >= 1.0)
        return 1.0;

    float result = 0.0;
    vec2 texel = vec2(1.0 / 1024.0);

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = q.xy + vec2(float(x), float(y)) * texel;
            result += texture(uShadow, vec3(uv, q.z - 0.0015));
        }
    }
    return result / 9.0;
}

void main() {
    vec3 albedo = texture(uAlbedo, UV).rgb * Material.rgb;
    vec3 orm = texture(uOrm, UV).rgb;

    vec3 N = normalize(WorldNormal);
    vec3 T = normalize(
        dFdx(WorldPos) * dFdy(UV).y -
        dFdy(WorldPos) * dFdx(UV).y
    );
    T = normalize(T - N * dot(N, T));
    vec3 B = normalize(cross(N, T));
    vec3 tangentNormal = texture(uNormal, UV).xyz * 2.0 - 1.0;
    N = normalize(mat3(T, B, N) * tangentNormal);

    float metallic = clamp(Material.a + orm.g * 0.05, 0.0, 1.0);
    float roughness = clamp(RoughnessAO.x * max(orm.r, 0.35), 0.045, 1.0);
    float ao = clamp(RoughnessAO.y * orm.b, 0.0, 1.0);

    vec3 V = normalize(CameraPosition.xyz - WorldPos);
    vec3 L = normalize(-SunDirection.xyz);
    vec3 H = normalize(V + L);

    float NoV = max(dot(N, V), 0.0);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(VoH, F0);
    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySchlickGGX(NoV, roughness) * GeometrySchlickGGX(NoL, roughness);

    vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - metallic);

    float shadow = ShadowPCF(ShadowCoord);
    vec3 direct = (kd * albedo / PI + specular) * SunColor.rgb * NoL * shadow;

    float skyFactor = 0.5 + 0.5 * max(N.y, 0.0);
    vec3 ambient = albedo * vec3(0.06, 0.10, 0.18) * skyFactor * kd * ao;

    vec3 hdr = direct + ambient;
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    vec3 mapped = clamp((hdr * (a * hdr + b)) / (hdr * (c * hdr + d) + e), 0.0, 1.0);
    FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

constexpr char kShadowVertex[] = R"GLSL(
#version 300 es
precision highp float;

layout(location=0) in vec3 InPosition;
uniform mat4 uLightViewProj;
uniform mat4 uModel;

void main() {
    gl_Position = uLightViewProj * uModel * vec4(InPosition, 1.0);
}
)GLSL";

constexpr char kShadowFragment[] = R"GLSL(
#version 300 es
precision highp float;

void main() {
}
)GLSL";

constexpr char kSkyVertex[] = R"GLSL(
#version 300 es
precision highp float;

layout(std140,binding=0) uniform FrameBlock {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
};

out vec3 RayDirection;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 p = positions[gl_VertexID];
    vec4 nearPoint = InvViewProj * vec4(p, -1.0, 1.0);
    vec4 farPoint  = InvViewProj * vec4(p,  1.0, 1.0);

    vec3 nearWorld = nearPoint.xyz / nearPoint.w;
    vec3 farWorld  = farPoint.xyz / farPoint.w;
    RayDirection = normalize(farWorld - nearWorld);

    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

constexpr char kSkyFragment[] = R"GLSL(
#version 300 es
precision highp float;

in vec3 RayDirection;
layout(location=0) out vec4 FragColor;

layout(std140,binding=0) uniform FrameBlock {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
};

float Hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float Noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash(i);
    float b = Hash(i + vec2(1.0, 0.0));
    float c = Hash(i + vec2(0.0, 1.0));
    float d = Hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float Fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += Noise(p) * amplitude;
        p = p * 2.03 + vec2(17.0, 11.0);
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec3 ray = normalize(RayDirection);
    vec3 sun = normalize(-SunDirection.xyz);

    float horizon = pow(clamp(1.0 - abs(ray.y), 0.0, 1.0), 0.72);
    float day = clamp(0.35 + 0.65 * (sun.y * 0.5 + 0.5), 0.0, 1.0);

    vec3 zenith = mix(vec3(0.015, 0.028, 0.075), vec3(0.17, 0.38, 0.72), day);
    vec3 horizonColor = mix(vec3(0.12, 0.17, 0.23), vec3(0.64, 0.77, 0.93), day);
    vec3 sky = mix(zenith, horizonColor, horizon);

    float sunDisc = pow(max(dot(ray, sun), 0.0), 160.0);
    float sunGlow = pow(max(dot(ray, sun), 0.0), 12.0);
    sky += SunColor.rgb * (sunDisc * 1.8 + sunGlow * 0.16);

    float rayHeight = max(ray.y + 0.18, 0.18);
    vec2 cloudUv = ray.xz / rayHeight;
    float t = SkyParams.z;

    float layerA = Fbm(cloudUv * 0.115 + vec2(t * 0.012, t * 0.006));
    float layerB = Fbm(cloudUv * 0.245 + vec2(-t * 0.019, t * 0.009) + 37.0);
    float coverage = clamp(SkyParams.y, 0.15, 1.0);
    float clouds = smoothstep(0.48 - coverage * 0.14, 0.72 - coverage * 0.10, mix(layerA, layerB, 0.42));

    float cloudLight = 0.55 + 0.45 * max(dot(ray, sun), 0.0);
    sky = mix(sky, vec3(0.72, 0.75, 0.80) * cloudLight, clouds * 0.78);

    FragColor = vec4(max(sky, vec3(0.0)), 1.0);
}
)GLSL";

constexpr char kGizmoVertex[] = R"GLSL(
#version 300 es
precision highp float;

layout(location=0) in vec3 InPosition;
layout(location=1) in vec4 InColor;

uniform mat4 uViewProj;
uniform vec3 uOrigin;
uniform float uScale;

out vec4 Color;

void main() {
    vec3 p = uOrigin + InPosition * uScale;
    Color = InColor;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)GLSL";

constexpr char kGizmoFragment[] = R"GLSL(
#version 300 es
precision mediump float;

in vec4 Color;
layout(location=0) out vec4 FragColor;

void main() {
    FragColor = Color;
}
)GLSL";

struct GizmoVertex final {
    float position[3]{};
    float color[4]{};
};

constexpr uint32_t kMaxGizmoVertices = 192u;

Mat4 Ortho(
    float left,
    float right,
    float bottom,
    float top,
    float nearPlane,
    float farPlane
) noexcept {
    Mat4 r = {};
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (farPlane - nearPlane);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    r.m[15] = 1.0f;
    return r;
}

float Length3(const Vec4& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec4 Cross3(const Vec4& a, const Vec4& b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
        0.0f
    };
}

Vec4 Normalize3(const Vec4& v) noexcept {
    const float len = Length3(v);
    if (len <= std::numeric_limits<float>::epsilon())
        return {0.0f, 0.0f, 1.0f, 0.0f};
    const float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv, 0.0f};
}

bool LoadTextFile(
    const std::filesystem::path& path,
    std::string& out
) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    out = stream.str();
    return !out.empty();
}

} // namespace

GLES3Renderer::GLES3Renderer(
    std::filesystem::path projectRoot
) noexcept
    : shadowSize_(kDefaultShadowSize),
      projectRoot_(std::move(projectRoot)) {
    camera_.view = Identity();
    camera_.proj = Identity();
    camera_.viewProj = Identity();
    camera_.invViewProj = Identity();
    camera_.lightViewProj = Identity();
}

bool GLES3Renderer::CreateContext() {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglGetDisplay failed");
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(display_, &major, &minor) != EGL_TRUE) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglInitialize failed error=0x%x", eglGetError());
        display_ = EGL_NO_DISPLAY;
        return false;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglBindAPI GLES failed error=0x%x", eglGetError());
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        return false;
    }

    EGLint count = 0;
    if (eglChooseConfig(display_, kConfigWithStencil, &config_, 1, &count) != EGL_TRUE || count != 1) {
        count = 0;
        if (eglChooseConfig(display_, kConfigDepthOnly, &config_, 1, &count) != EGL_TRUE || count != 1) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "no GLES3 EGL config");
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            config_ = nullptr;
            return false;
        }
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "using depth-only EGL config fallback");
    }

    context_ = eglCreateContext(
        display_,
        config_,
        EGL_NO_CONTEXT,
        kContextAttributes
    );
    if (context_ == EGL_NO_CONTEXT) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglCreateContext failed error=0x%x", eglGetError());
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        config_ = nullptr;
        return false;
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "GLES context created EGL=%d.%d",
        major,
        minor
    );
    return true;
}

bool GLES3Renderer::CreateSurface() {
    if (display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT || !window_)
        return false;

    surface_ = eglCreateWindowSurface(display_, config_, window_, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglCreateWindowSurface failed error=0x%x", eglGetError());
        return false;
    }

    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglMakeCurrent failed error=0x%x", eglGetError());
        DestroyEGLSurface();
        return false;
    }

    EGLint width = 0;
    EGLint height = 0;
    if (eglQuerySurface(display_, surface_, EGL_WIDTH, &width) != EGL_TRUE ||
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &height) != EGL_TRUE ||
        width <= 0 || height <= 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "invalid EGL extent after surface creation");
        DestroyEGLSurface();
        return false;
    }

    surfaceWidth_ = static_cast<uint32_t>(width);
    surfaceHeight_ = static_cast<uint32_t>(height);
    stableExtentFrames_ = 0u;
    tier_ = QualityTier::Tier1Framebuffer0;
    return true;
}

bool GLES3Renderer::RecreateContextAndSurface() {
    const bool hadWindow = window_ != nullptr;

    if (context_ != EGL_NO_CONTEXT && display_ != EGL_NO_DISPLAY) {
        if (eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE) {
            DestroyGLResources();
        } else {
            offscreenFbo_ = 0u;
            offscreenColor_ = 0u;
            offscreenDepthStencil_ = 0u;
            shadowFbo_ = 0u;
            shadowDepth_ = 0u;
            frameUbo_ = 0u;
            defaultAlbedo_ = 0u;
            defaultNormal_ = 0u;
            defaultOrm_ = 0u;
            skyVao_ = 0u;
            skyProgram_ = 0u;
            shadowProgram_ = 0u;
            tier3Program_ = 0u;
            tier2Program_ = 0u;
            gizmoProgram_ = 0u;
            for (GpuMesh& mesh : meshes_) mesh = {};
        }
    }

    DestroyEGLSurface();

    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT)
        eglDestroyContext(display_, context_);
    if (display_ != EGL_NO_DISPLAY)
        eglTerminate(display_);

    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    config_ = nullptr;
    contextLost_ = false;
    deferredReady_ = false;
    tier2Ready_ = false;
    tier3Ready_ = false;
    validator_.Reset();

    if (!hadWindow) return false;
    if (!CreateContext()) return false;
    if (!CreateSurface()) return false;

    initialized_ = true;
    return true;
}

void GLES3Renderer::DestroyEGLSurface() noexcept {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE)
            eglDestroySurface(display_, surface_);
    }
    surface_ = EGL_NO_SURFACE;
    begun_ = false;
    stableExtentFrames_ = 0u;
    tier_ = QualityTier::Tier1Framebuffer0;
}

void GLES3Renderer::DestroyDeferredResources() noexcept {
    if (context_ == EGL_NO_CONTEXT || display_ == EGL_NO_DISPLAY)
        return;

    glBindVertexArray(0);

    if (gizmoProgram_) glDeleteProgram(gizmoProgram_);
    if (tier3Program_) glDeleteProgram(tier3Program_);
    if (tier2Program_) glDeleteProgram(tier2Program_);
    if (shadowProgram_) glDeleteProgram(shadowProgram_);
    if (skyProgram_) glDeleteProgram(skyProgram_);
    gizmoProgram_ = 0u;
    tier3Program_ = 0u;
    tier2Program_ = 0u;
    shadowProgram_ = 0u;
    skyProgram_ = 0u;

    for (GpuMesh& mesh : meshes_) {
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ibo) glDeleteBuffers(1, &mesh.ibo);
        mesh = {};
    }

    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowDepth_) glDeleteTextures(1, &shadowDepth_);
    shadowFbo_ = 0u;
    shadowDepth_ = 0u;

    if (frameUbo_) glDeleteBuffers(1, &frameUbo_);
    frameUbo_ = 0u;

    if (defaultAlbedo_) glDeleteTextures(1, &defaultAlbedo_);
    if (defaultNormal_) glDeleteTextures(1, &defaultNormal_);
    if (defaultOrm_) glDeleteTextures(1, &defaultOrm_);
    defaultAlbedo_ = 0u;
    defaultNormal_ = 0u;
    defaultOrm_ = 0u;

    if (skyVao_) glDeleteVertexArrays(1, &skyVao_);
    skyVao_ = 0u;

    if (offscreenFbo_) glDeleteFramebuffers(1, &offscreenFbo_);
    if (offscreenColor_) glDeleteTextures(1, &offscreenColor_);
    if (offscreenDepthStencil_) glDeleteRenderbuffers(1, &offscreenDepthStencil_);
    offscreenFbo_ = 0u;
    offscreenColor_ = 0u;
    offscreenDepthStencil_ = 0u;
    offscreenWidth_ = 0u;
    offscreenHeight_ = 0u;

    validator_.Reset();
}

void GLES3Renderer::DestroyGLResources() noexcept {
    DestroyDeferredResources();
}

bool GLES3Renderer::ValidateCurrentContext() noexcept {
    if (!initialized_ || display_ == EGL_NO_DISPLAY ||
        context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE) {
        return false;
    }

    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
        const EGLint error = eglGetError();
        if (error == EGL_CONTEXT_LOST) {
            contextLost_ = true;
            tier_ = QualityTier::Tier1Framebuffer0;
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "EGL context lost");
        }
        return false;
    }

    return true;
}

bool GLES3Renderer::QuerySurfaceExtent(
    uint32_t& width,
    uint32_t& height
) noexcept {
    width = 0u;
    height = 0u;
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE)
        return false;

    EGLint w = 0;
    EGLint h = 0;
    if (eglQuerySurface(display_, surface_, EGL_WIDTH, &w) != EGL_TRUE ||
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &h) != EGL_TRUE ||
        w <= 0 || h <= 0)
        return false;

    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);

    if (width == surfaceWidth_ && height == surfaceHeight_) {
        if (stableExtentFrames_ < std::numeric_limits<uint32_t>::max())
            ++stableExtentFrames_;
    } else {
        surfaceWidth_ = width;
        surfaceHeight_ = height;
        stableExtentFrames_ = 1u;
        tier_ = QualityTier::Tier1Framebuffer0;
    }

    return true;
}

bool GLES3Renderer::EnsureFallbackTextures() {
    auto createSolid = [&](const char* label, const std::array<uint8_t,4>& pixel, GLuint& out) {
        glGenTextures(1, &out);
        if (!validator_.ValidateAllocation(
                AetherisValidationTracker::Kind::Texture,
                label,
                out)) {
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, out);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            1,
            1,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixel.data()
        );
        return validator_.ValidateGlError(
            AetherisValidationTracker::Pass::Tier2Opaque,
            label
        );
    };

    if (defaultAlbedo_ && defaultNormal_ && defaultOrm_)
        return true;

    if (defaultAlbedo_ == 0u &&
        !createSolid("fallback_albedo", {230u,230u,230u,255u}, defaultAlbedo_))
        return false;

    if (defaultNormal_ == 0u &&
        !createSolid("fallback_normal", {128u,128u,255u,255u}, defaultNormal_))
        return false;

    if (defaultOrm_ == 0u &&
        !createSolid("fallback_orm", {180u,0u,255u,255u}, defaultOrm_))
        return false;

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool GLES3Renderer::EnsureFrameUbo() {
    if (frameUbo_) return true;

    glGenBuffers(1, &frameUbo_);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::UniformBuffer,
            "frame_ubo",
            frameUbo_))
        return false;

    glBindBuffer(GL_UNIFORM_BUFFER, frameUbo_);
    glBufferData(
        GL_UNIFORM_BUFFER,
        static_cast<GLsizeiptr>(sizeof(std140::DeferredFrameBlock)),
        nullptr,
        GL_DYNAMIC_DRAW
    );

    if (!validator_.ValidateGlError(
            AetherisValidationTracker::Pass::Tier2Opaque,
            "frame_ubo_buffer_data"))
        return false;

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUbo_);
    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Tier2Opaque,
        "frame_ubo_bind_base"
    );
}

bool GLES3Renderer::CreateShader(
    GLenum type,
    const char* source,
    const char* label,
    AetherisValidationTracker::Pass pass,
    GLuint& outShader
) {
    outShader = glCreateShader(type);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Shader,
            label,
            outShader))
        return false;

    glShaderSource(outShader, 1, &source, nullptr);
    glCompileShader(outShader);

    return validator_.ValidateShader(outShader, label, pass);
}

bool GLES3Renderer::CreateProgramFromFiles(
    const std::filesystem::path& vertexPath,
    const std::filesystem::path& fragmentPath,
    const char* builtInVertex,
    const char* builtInFragment,
    AetherisValidationTracker::Pass pass,
    GLuint& outProgram
) {
    std::string vertexSource;
    std::string fragmentSource;

    const bool vertexLoaded = LoadTextFile(vertexPath, vertexSource);
    const bool fragmentLoaded = LoadTextFile(fragmentPath, fragmentSource);

    const char* vertex = vertexLoaded ? vertexSource.c_str() : builtInVertex;
    const char* fragment = fragmentLoaded ? fragmentSource.c_str() : builtInFragment;

    if (!vertex || !fragment)
        return false;

    GLuint vs = 0u;
    GLuint fs = 0u;

    if (!CreateShader(GL_VERTEX_SHADER, vertex, "vertex_shader", pass, vs)) {
        if (vs) glDeleteShader(vs);
        return false;
    }

    if (!CreateShader(GL_FRAGMENT_SHADER, fragment, "fragment_shader", pass, fs)) {
        glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    outProgram = glCreateProgram();
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Program,
            "gles_program",
            outProgram)) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glAttachShader(outProgram, vs);
    glAttachShader(outProgram, fs);
    glLinkProgram(outProgram);

    const bool linked = validator_.ValidateProgram(outProgram, "gles_program", pass);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!linked) {
        glDeleteProgram(outProgram);
        outProgram = 0u;
        return false;
    }

    GLint blockIndex = glGetUniformBlockIndex(outProgram, "FrameBlock");
    if (blockIndex != GL_INVALID_INDEX)
        glUniformBlockBinding(outProgram, static_cast<GLuint>(blockIndex), 0);

    return validator_.ValidateGlError(pass, "program_uniform_block");
}

bool GLES3Renderer::EnsureTier2Program() {
    if (tier2Program_) return true;

    const std::filesystem::path root = projectRoot_ / "assets/shaders/gles";
    return CreateProgramFromFiles(
        root / "tier2_blinn_phong_es300.vert",
        root / "tier2_blinn_phong_es300.frag",
        kTier2Vertex,
        kTier2Fragment,
        AetherisValidationTracker::Pass::Tier2Opaque,
        tier2Program_
    );
}

bool GLES3Renderer::EnsureTier3Programs() {
    if (tier3Program_ && skyProgram_ && shadowProgram_)
        return true;

    const std::filesystem::path root = projectRoot_ / "assets/shaders/gles";

    if (!skyProgram_) {
        if (!CreateProgramFromFiles(
                root / "sky_atmosphere_es300.vert",
                root / "sky_atmosphere_es300.frag",
                kSkyVertex,
                kSkyFragment,
                AetherisValidationTracker::Pass::Sky,
                skyProgram_)) {
            validator_.IsolatePass(AetherisValidationTracker::Pass::Sky, "sky_program");
            return false;
        }
    }

    if (!shadowProgram_) {
        if (!CreateProgramFromFiles(
                root / "shadow_depth_es300.vert",
                root / "shadow_depth_es300.frag",
                kShadowVertex,
                kShadowFragment,
                AetherisValidationTracker::Pass::Shadow,
                shadowProgram_)) {
            validator_.IsolatePass(AetherisValidationTracker::Pass::Shadow, "shadow_program");
            return false;
        }
    }

    if (!tier3Program_) {
        if (!CreateProgramFromFiles(
                root / "pbr_mobile_es300.vert",
                root / "pbr_mobile_es300.frag",
                kTier3Vertex,
                kTier3Fragment,
                AetherisValidationTracker::Pass::Tier3Opaque,
                tier3Program_)) {
            validator_.IsolatePass(AetherisValidationTracker::Pass::Tier3Opaque, "pbr_program");
            return false;
        }
    }

    return true;
}

bool GLES3Renderer::EnsureGizmoProgram() {
    if (gizmoProgram_ && gizmoVao_ && gizmoVbo_)
        return true;

    const std::filesystem::path root = projectRoot_ / "assets/shaders/gles";
    if (!gizmoProgram_) {
        if (!CreateProgramFromFiles(
                root / "gizmo_forward_es300.vert",
                root / "gizmo_forward_es300.frag",
                kGizmoVertex,
                kGizmoFragment,
                AetherisValidationTracker::Pass::Gizmo,
                gizmoProgram_))
            return false;
    }

    if (!gizmoVao_) {
        glGenVertexArrays(1, &gizmoVao_);
        if (!validator_.ValidateAllocation(
                AetherisValidationTracker::Kind::VertexArray,
                "gizmo_vao",
                gizmoVao_))
            return false;
    }

    if (!gizmoVbo_) {
        glGenBuffers(1, &gizmoVbo_);
        if (!validator_.ValidateAllocation(
                AetherisValidationTracker::Kind::Buffer,
                "gizmo_vbo",
                gizmoVbo_))
            return false;

        std::array<GizmoVertex, kMaxGizmoVertices> vertices{};
        uint32_t count = 0u;

        auto addLine = [&](float x0,float y0,float z0,float x1,float y1,float z1,
                           float r,float g,float b) {
            if (count + 2u > kMaxGizmoVertices) return;
            vertices[count++] = {{{x0,y0,z0},{r,g,b,1.0f}}};
            vertices[count++] = {{{x1,y1,z1},{r,g,b,1.0f}}};
        };

        constexpr float arrow = 0.28f;
        constexpr float ring = 1.15f;
        addLine(0,0,0, 1.0f,0,0, 0.95f,0.15f,0.15f);
        addLine(1.0f,0,0, 1.0f-arrow,arrow*0.55f,0, 0.95f,0.15f,0.15f);
        addLine(1.0f,0,0, 1.0f-arrow,-arrow*0.55f,0, 0.95f,0.15f,0.15f);

        addLine(0,0,0, 0,1.0f,0, 0.25f,0.95f,0.32f);
        addLine(0,1.0f,0, arrow*0.55f,1.0f-arrow,0, 0.25f,0.95f,0.32f);
        addLine(0,1.0f,0, -arrow*0.55f,1.0f-arrow,0, 0.25f,0.95f,0.32f);

        addLine(0,0,0, 0,0,1.0f, 0.20f,0.45f,0.98f);
        addLine(0,0,1.0f, arrow*0.55f,0,1.0f-arrow, 0.20f,0.45f,0.98f);
        addLine(0,0,1.0f, -arrow*0.55f,0,1.0f-arrow, 0.20f,0.45f,0.98f);

        addLine(-0.16f,0,0,0.16f,0,0,0.95f,0.55f,0.10f);
        addLine(0,-0.16f,0,0,0.16f,0,0.95f,0.55f,0.10f);
        addLine(0,0,-0.16f,0,0,0.16f,0.95f,0.55f,0.10f);

        for (int axis = 0; axis < 3; ++axis) {
            constexpr int segments = 32;
            for (int i = 0; i < segments; ++i) {
                const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * kPi;
                const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * kPi;
                float x0=0.0f,y0=0.0f,z0=0.0f,x1=0.0f,y1=0.0f,z1=0.0f;
                if (axis == 0) { y0 = std::cos(a0)*ring; z0 = std::sin(a0)*ring; y1 = std::cos(a1)*ring; z1 = std::sin(a1)*ring; }
                if (axis == 1) { x0 = std::cos(a0)*ring; z0 = std::sin(a0)*ring; x1 = std::cos(a1)*ring; z1 = std::sin(a1)*ring; }
                if (axis == 2) { x0 = std::cos(a0)*ring; y0 = std::sin(a0)*ring; x1 = std::cos(a1)*ring; y1 = std::sin(a1)*ring; }
                const float r = axis == 0 ? 0.95f : 0.20f;
                const float g = axis == 1 ? 0.95f : 0.45f;
                const float b = axis == 2 ? 0.95f : 0.20f;
                addLine(x0,y0,z0,x1,y1,z1,r,g,b);
            }
        }

        gizmoVertexCount_ = count;
        glBindVertexArray(gizmoVao_);
        glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(sizeof(GizmoVertex) * count),
            vertices.data(),
            GL_STATIC_DRAW
        );
        if (!validator_.ValidateGlError(
                AetherisValidationTracker::Pass::Gizmo,
                "gizmo_buffer_data"))
            return false;

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(GizmoVertex)),
            reinterpret_cast<const void*>(offsetof(GizmoVertex, position))
        );
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            4,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(GizmoVertex)),
            reinterpret_cast<const void*>(offsetof(GizmoVertex, color))
        );
    }

    glBindVertexArray(0);
    return true;
}

bool GLES3Renderer::UploadMesh(
    DemoMeshSlot slot,
    const DemoCpuMesh& cpu,
    AetherisValidationTracker::Pass pass
) {
    if (cpu.Empty())
        return false;

    const size_t index = static_cast<size_t>(slot);
    GpuMesh replacement{};

    glGenVertexArrays(1, &replacement.vao);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::VertexArray,
            "mesh_vao",
            replacement.vao))
        return false;

    glGenBuffers(1, &replacement.vbo);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Buffer,
            "mesh_vbo",
            replacement.vbo)) {
        glDeleteVertexArrays(1, &replacement.vao);
        return false;
    }

    glGenBuffers(1, &replacement.ibo);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Buffer,
            "mesh_ibo",
            replacement.ibo)) {
        glDeleteBuffers(1, &replacement.vbo);
        glDeleteVertexArrays(1, &replacement.vao);
        return false;
    }

    glBindVertexArray(replacement.vao);
    glBindBuffer(GL_ARRAY_BUFFER, replacement.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(DemoVertex) * cpu.vertexCount),
        cpu.vertices.data(),
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, replacement.ibo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(uint32_t) * cpu.indexCount),
        cpu.indices.data(),
        GL_STATIC_DRAW
    );

    if (!validator_.ValidateGlError(pass, "mesh_buffer_upload")) {
        glBindVertexArray(0);
        glDeleteBuffers(1, &replacement.ibo);
        glDeleteBuffers(1, &replacement.vbo);
        glDeleteVertexArrays(1, &replacement.vao);
        return false;
    }

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(DemoVertex)),
                          reinterpret_cast<const void*>(offsetof(DemoVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(DemoVertex)),
                          reinterpret_cast<const void*>(offsetof(DemoVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(DemoVertex)),
                          reinterpret_cast<const void*>(offsetof(DemoVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(DemoVertex)),
                          reinterpret_cast<const void*>(offsetof(DemoVertex, baseColorMetallic)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(DemoVertex)),
                          reinterpret_cast<const void*>(offsetof(DemoVertex, roughnessAO)));

    replacement.indexCount = static_cast<GLsizei>(cpu.indexCount);
    replacement.valid = validator_.ValidateGlError(pass, "mesh_vertex_layout");

    glBindVertexArray(0);

    if (!replacement.valid) {
        glDeleteBuffers(1, &replacement.ibo);
        glDeleteBuffers(1, &replacement.vbo);
        glDeleteVertexArrays(1, &replacement.vao);
        return false;
    }

    GpuMesh& old = meshes_[index];
    if (old.vao) glDeleteVertexArrays(1, &old.vao);
    if (old.vbo) glDeleteBuffers(1, &old.vbo);
    if (old.ibo) glDeleteBuffers(1, &old.ibo);
    old = replacement;
    return true;
}

bool GLES3Renderer::EnsureMeshes() {
    static constexpr std::array<Vec4, static_cast<size_t>(DemoMeshSlot::Count)> colors = {
        Vec4{0.33f,0.40f,0.50f,0.0f},
        Vec4{0.20f,0.25f,0.33f,0.05f},
        Vec4{0.22f,0.37f,0.55f,0.05f},
        Vec4{0.28f,0.40f,0.56f,0.0f},
        Vec4{0.52f,0.28f,0.08f,0.0f},
        Vec4{0.31f,0.34f,0.38f,0.0f},
        Vec4{0.22f,0.27f,0.35f,0.0f}
    };

    static constexpr std::array<float, static_cast<size_t>(DemoMeshSlot::Count)> metallic = {
        0.0f,0.05f,0.05f,0.0f,0.0f,0.0f,0.0f
    };
    static constexpr std::array<float, static_cast<size_t>(DemoMeshSlot::Count)> roughness = {
        0.90f,0.72f,0.62f,0.78f,0.60f,0.70f,0.82f
    };

    ObjMeshLoader loader{};

    for (uint32_t i = 0u; i < static_cast<uint32_t>(DemoMeshSlot::Count); ++i) {
        const DemoMeshSlot slot = static_cast<DemoMeshSlot>(i);
        if (meshes_[i].valid) continue;

        DemoCpuMesh cpu{};
        bool loaded = false;

        if (!projectRoot_.empty()) {
            const std::filesystem::path path =
                projectRoot_ / DemoWorldInitializer::AssetPath(slot);
            loaded = loader.Load(
                path,
                colors[i],
                metallic[i],
                roughness[i],
                1.0f,
                cpu
            );
        }

        if (!loaded)
            DemoWorldInitializer::FallbackMesh(slot, cpu);

        if (!UploadMesh(
                slot,
                cpu,
                AetherisValidationTracker::Pass::Tier2Opaque)) {
            AETHERIS_LOGW(
                "GLES mesh slot %u failed upload; slot remains optional",
                i
            );
        }
    }

    bool any = false;
    for (const GpuMesh& mesh : meshes_) {
        any = any || mesh.valid;
    }
    return any;
}

bool GLES3Renderer::EnsureOffscreen(
    uint32_t width,
    uint32_t height
) {
    if (width == 0u || height == 0u) return false;
    if (offscreenFbo_ && offscreenWidth_ == width && offscreenHeight_ == height) {
        glBindFramebuffer(GL_FRAMEBUFFER, offscreenFbo_);
        if (!validator_.IsPassValid(AetherisValidationTracker::Pass::Offscreen))
            validator_.RecoverPass(AetherisValidationTracker::Pass::Offscreen);
        return validator_.ValidateFramebuffer(
            offscreenFbo_,
            AetherisValidationTracker::Pass::Offscreen,
            "editor_offscreen_fbo_revalidate"
        );
    }

    GLuint newFbo = 0u;
    GLuint newColor = 0u;
    GLuint newDepth = 0u;

    glGenFramebuffers(1, &newFbo);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Framebuffer,
            "editor_offscreen_fbo",
            newFbo))
        return false;

    glGenTextures(1, &newColor);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Texture,
            "editor_offscreen_color",
            newColor))
        goto fail;

    glBindTexture(GL_TEXTURE_2D, newColor);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    if (!validator_.ValidateGlError(
            AetherisValidationTracker::Pass::Offscreen,
            "offscreen_color_storage"))
        goto fail;

    glGenRenderbuffers(1, &newDepth);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Renderbuffer,
            "editor_offscreen_depth_stencil",
            newDepth))
        goto fail;

    glBindRenderbuffer(GL_RENDERBUFFER, newDepth);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height)
    );

    if (!validator_.ValidateGlError(
            AetherisValidationTracker::Pass::Offscreen,
            "offscreen_depth24_stencil8"))
        goto fail;

    glBindFramebuffer(GL_FRAMEBUFFER, newFbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        newColor,
        0
    );
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        newDepth
    );
    glDrawBuffers(1, std::array<GLenum,1>{GL_COLOR_ATTACHMENT0}.data());

    if (!validator_.ValidateFramebuffer(
            newFbo,
            AetherisValidationTracker::Pass::Offscreen,
            "editor_offscreen_fbo"))
        goto fail;

    {
        GLuint oldFbo = offscreenFbo_;
        GLuint oldColor = offscreenColor_;
        GLuint oldDepth = offscreenDepthStencil_;

        offscreenFbo_ = newFbo;
        offscreenColor_ = newColor;
        offscreenDepthStencil_ = newDepth;
        offscreenWidth_ = width;
        offscreenHeight_ = height;

        if (oldFbo) glDeleteFramebuffers(1, &oldFbo);
        if (oldColor) glDeleteTextures(1, &oldColor);
        if (oldDepth) glDeleteRenderbuffers(1, &oldDepth);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, offscreenFbo_);
    return true;

fail:
    if (newDepth) glDeleteRenderbuffers(1, &newDepth);
    if (newColor) glDeleteTextures(1, &newColor);
    if (newFbo) glDeleteFramebuffers(1, &newFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (offscreenFbo_ != 0u) {
        // Transactional resize failed: keep the previous complete FBO alive.
        validator_.RecoverPass(AetherisValidationTracker::Pass::Offscreen);
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "offscreen resize failed; retaining previous valid target %ux%u",
            offscreenWidth_,
            offscreenHeight_
        );
    } else {
        validator_.IsolatePass(
            AetherisValidationTracker::Pass::Offscreen,
            "offscreen_allocation"
        );
    }
    return false;
}

bool GLES3Renderer::EnsureShadowTarget() {
    if (shadowFbo_ && shadowDepth_)
        return validator_.IsPassValid(AetherisValidationTracker::Pass::Shadow);

    GLuint newFbo = 0u;
    GLuint newDepth = 0u;

    glGenFramebuffers(1, &newFbo);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Framebuffer,
            "shadow_fbo",
            newFbo))
        return false;

    glGenTextures(1, &newDepth);
    if (!validator_.ValidateAllocation(
            AetherisValidationTracker::Kind::Texture,
            "shadow_depth_texture",
            newDepth))
        goto fail;

    glBindTexture(GL_TEXTURE_2D, newDepth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT16,
        static_cast<GLsizei>(shadowSize_),
        static_cast<GLsizei>(shadowSize_),
        0,
        GL_DEPTH_COMPONENT,
        GL_UNSIGNED_SHORT,
        nullptr
    );

    if (!validator_.ValidateGlError(
            AetherisValidationTracker::Pass::Shadow,
            "shadow_depth_storage"))
        goto fail;

    glBindFramebuffer(GL_FRAMEBUFFER, newFbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D,
        newDepth,
        0
    );
    glDrawBuffers(0, nullptr);
    glReadBuffer(GL_NONE);

    if (!validator_.ValidateFramebuffer(
            newFbo,
            AetherisValidationTracker::Pass::Shadow,
            "shadow_fbo"))
        goto fail;

    shadowFbo_ = newFbo;
    shadowDepth_ = newDepth;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;

fail:
    if (newDepth) glDeleteTextures(1, &newDepth);
    if (newFbo) glDeleteFramebuffers(1, &newFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    validator_.IsolatePass(
        AetherisValidationTracker::Pass::Shadow,
        "shadow_allocation"
    );
    return false;
}

void GLES3Renderer::UpdateCamera() noexcept {
    const float time = elapsedSeconds_;
    const float orbitX = 14.0f * std::cos(time * 0.05f);
    const float orbitZ = 14.0f * std::sin(time * 0.05f);

    camera_.position = {orbitX, 8.5f, orbitZ, 1.0f};
    const Vec4 target{0.0f, 0.8f, 0.0f, 1.0f};
    const Vec4 up{0.0f, 1.0f, 0.0f, 0.0f};

    const float aspect =
        surfaceHeight_ > 0u
            ? static_cast<float>(surfaceWidth_) / static_cast<float>(surfaceHeight_)
            : 1.0f;

    camera_.view = LookAt(camera_.position, target, up);
    camera_.proj = Perspective(60.0f * kPi / 180.0f, aspect, 0.1f, 100.0f);
    camera_.viewProj = Multiply(camera_.proj, camera_.view);
    camera_.invViewProj = Inverse(camera_.viewProj);

    const float sunA = time * 0.03f;
    camera_.sunDirection = Normalize4({
        -0.45f + 0.22f * std::sin(sunA),
        -0.82f,
        -0.35f + 0.15f * std::cos(sunA),
        0.0f
    });

    const Vec4 sunPosition = {
        -camera_.sunDirection.x * 28.0f,
        -camera_.sunDirection.y * 28.0f + 10.0f,
        -camera_.sunDirection.z * 28.0f,
        1.0f
    };

    const Mat4 lightView = LookAt(
        sunPosition,
        target,
        {0.0f, 1.0f, 0.0f, 0.0f}
    );

    const Mat4 lightProjection = Ortho(-24.0f,24.0f,-24.0f,24.0f,-40.0f,60.0f);
    camera_.lightViewProj = Multiply(lightProjection, lightView);
}

void GLES3Renderer::UpdateFrameUbo() noexcept {
    frameBlock_.cameraPosition = camera_.position;
    frameBlock_.sunDirection = camera_.sunDirection;
    frameBlock_.sunColor = {1.0f, 0.94f, 0.82f, 1.0f};
    frameBlock_.skyParams = {1.0f, 0.74f, elapsedSeconds_, 1.0f};
    frameBlock_.cameraRight = {1.0f,0.0f,0.0f,0.0f};
    frameBlock_.cameraUp = {0.0f,1.0f,0.0f,0.0f};
    frameBlock_.cameraForward = {0.0f,0.0f,-1.0f,0.0f};
    frameBlock_.invViewProj = camera_.invViewProj;
    frameBlock_.csmMatrices[0] = camera_.lightViewProj;
    frameBlock_.csmMatrices[1] = camera_.lightViewProj;
    frameBlock_.csmMatrices[2] = camera_.lightViewProj;
    frameBlock_.csmSplits = {12.0f, 28.0f, 60.0f, 100.0f};

    glBindBuffer(GL_UNIFORM_BUFFER, frameUbo_);
    glBufferSubData(
        GL_UNIFORM_BUFFER,
        0,
        static_cast<GLsizeiptr>(sizeof(frameBlock_)),
        &frameBlock_
    );
    validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Tier2Opaque,
        "frame_ubo_update"
    );
}

bool GLES3Renderer::EnsureDeferredResources() {
    if (!initialized_ || surface_ == EGL_NO_SURFACE || display_ == EGL_NO_DISPLAY)
        return false;

    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!QuerySurfaceExtent(width, height))
        return true;

    UpdateCamera();

    const bool offscreen = EnsureOffscreen(width, height);
    const bool fallbackTextures = EnsureFallbackTextures();
    const bool frameUbo = EnsureFrameUbo();
    tier2Ready_ = fallbackTextures && frameUbo && EnsureTier2Program();

    if (!offscreen) {
        tier_ = QualityTier::Tier1Framebuffer0;
        deferredReady_ = true;
        tier3Ready_ = false;
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "offscreen editor FBO unavailable; GLES remains Tier1");
        return true;
    }

    if (!tier2Ready_) {
        tier_ = QualityTier::Tier1Framebuffer0;
        deferredReady_ = true;
        tier3Ready_ = false;
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Tier2 initialization failed; Tier1 is the active safety path");
        return true;
    }

    EnsureMeshes();

    tier3Ready_ =
        stableExtentFrames_ >= kMinStableFrames &&
        EnsureTier3Programs() &&
        EnsureShadowTarget();

    EnsureGizmoProgram();

    deferredReady_ = true;
    tier_ = tier3Ready_
        ? QualityTier::Tier3Advanced
        : QualityTier::Tier2Safe3D;

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "GLES deferred resources ready: tier=%s extent=%ux%u stable=%u",
        TierName(tier_),
        width,
        height,
        stableExtentFrames_
    );
    return true;
}

void GLES3Renderer::AbortDeferredResources() noexcept {
    if (context_ != EGL_NO_CONTEXT && display_ != EGL_NO_DISPLAY &&
        eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE) {
        DestroyDeferredResources();
    } else {
        offscreenFbo_ = 0u;
        offscreenColor_ = 0u;
        offscreenDepthStencil_ = 0u;
        shadowFbo_ = 0u;
        shadowDepth_ = 0u;
        frameUbo_ = 0u;
        defaultAlbedo_ = 0u;
        defaultNormal_ = 0u;
        defaultOrm_ = 0u;
        skyVao_ = 0u;
        gizmoVao_ = 0u;
        gizmoVbo_ = 0u;
        skyProgram_ = 0u;
        shadowProgram_ = 0u;
        tier3Program_ = 0u;
        tier2Program_ = 0u;
        gizmoProgram_ = 0u;
        for (GpuMesh& mesh : meshes_) mesh = {};
    }

    deferredReady_ = false;
    tier2Ready_ = false;
    tier3Ready_ = false;
    tier_ = QualityTier::Tier1Framebuffer0;
}

bool GLES3Renderer::BeginOffscreenPass() noexcept {
    if (!offscreenFbo_ || offscreenWidth_ == 0u || offscreenHeight_ == 0u ||
        !validator_.IsPassValid(AetherisValidationTracker::Pass::Offscreen))
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, offscreenFbo_);
    glViewport(0, 0, static_cast<GLsizei>(offscreenWidth_), static_cast<GLsizei>(offscreenHeight_));
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glClearColor(0.025f, 0.045f, 0.075f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Offscreen,
        "begin_offscreen"
    );
}

bool GLES3Renderer::DrawSky() noexcept {
    if (!skyProgram_ ||
        !validator_.IsPassValid(AetherisValidationTracker::Pass::Sky))
        return false;

    glUseProgram(skyProgram_);
    glBindVertexArray(skyVao_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Sky,
        "sky_draw"
    );
}

bool GLES3Renderer::DrawShadowMap(
    const RenderQueue& queue,
    std::span<const Transform> transforms
) noexcept {
    if (!shadowFbo_ || !shadowProgram_ ||
        !validator_.IsPassValid(AetherisValidationTracker::Pass::Shadow))
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, static_cast<GLsizei>(shadowSize_), static_cast<GLsizei>(shadowSize_));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glCullFace(GL_FRONT);
    glEnable(GL_CULL_FACE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(shadowProgram_);
    const GLint lightLocation = glGetUniformLocation(shadowProgram_, "uLightViewProj");
    const GLint modelLocation = glGetUniformLocation(shadowProgram_, "uModel");
    glUniformMatrix4fv(lightLocation, 1, GL_FALSE, camera_.lightViewProj.m.data());

    for (const RenderItem& item : queue.Items()) {
        if (item.transformIndex >= transforms.size()) continue;
        const uint32_t meshIndex = item.meshId % static_cast<uint32_t>(meshes_.size());
        const GpuMesh& mesh = meshes_[meshIndex];
        if (!mesh.valid) continue;

        const Mat4 model = ModelFromTransform(transforms[item.transformIndex]);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, model.m.data());
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glCullFace(GL_BACK);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, offscreenFbo_);
    glViewport(0, 0, static_cast<GLsizei>(offscreenWidth_), static_cast<GLsizei>(offscreenHeight_));

    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Shadow,
        "shadow_draw"
    );
}

bool GLES3Renderer::DrawOpaqueTier3(
    const RenderQueue& queue,
    std::span<const Transform> transforms
) noexcept {
    if (!tier3Program_ ||
        !validator_.IsPassValid(AetherisValidationTracker::Pass::Tier3Opaque))
        return false;

    glUseProgram(tier3Program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, defaultAlbedo_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, defaultNormal_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, defaultOrm_);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowDepth_);

    glUniform1i(glGetUniformLocation(tier3Program_, "uAlbedo"), 0);
    glUniform1i(glGetUniformLocation(tier3Program_, "uNormal"), 1);
    glUniform1i(glGetUniformLocation(tier3Program_, "uOrm"), 2);
    glUniform1i(glGetUniformLocation(tier3Program_, "uShadow"), 3);

    const GLint viewProjLocation = glGetUniformLocation(tier3Program_, "uViewProj");
    const GLint modelLocation = glGetUniformLocation(tier3Program_, "uModel");
    const GLint lightLocation = glGetUniformLocation(tier3Program_, "uLightViewProj");
    glUniformMatrix4fv(viewProjLocation, 1, GL_FALSE, camera_.viewProj.m.data());
    glUniformMatrix4fv(lightLocation, 1, GL_FALSE, camera_.lightViewProj.m.data());

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    for (const RenderItem& item : queue.Items()) {
        if (item.transformIndex >= transforms.size()) continue;
        const GpuMesh& mesh = meshes_[item.meshId % static_cast<uint32_t>(meshes_.size())];
        if (!mesh.valid) continue;

        const Mat4 model = ModelFromTransform(transforms[item.transformIndex]);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, model.m.data());
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Tier3Opaque,
        "tier3_opaque_draw"
    );
}

bool GLES3Renderer::DrawOpaqueTier2(
    const RenderQueue& queue,
    std::span<const Transform> transforms
) noexcept {
    if (!tier2Program_ ||
        !validator_.IsPassValid(AetherisValidationTracker::Pass::Tier2Opaque))
        return false;

    glUseProgram(tier2Program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, defaultAlbedo_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, defaultNormal_);
    glUniform1i(glGetUniformLocation(tier2Program_, "uAlbedo"), 0);
    glUniform1i(glGetUniformLocation(tier2Program_, "uNormal"), 1);

    const GLint modelLocation = glGetUniformLocation(tier2Program_, "uModel");
    const GLint viewProjLocation = glGetUniformLocation(tier2Program_, "uViewProj");
    glUniformMatrix4fv(viewProjLocation, 1, GL_FALSE, camera_.viewProj.m.data());
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    for (const RenderItem& item : queue.Items()) {
        if (item.transformIndex >= transforms.size()) continue;
        const GpuMesh& mesh = meshes_[item.meshId % static_cast<uint32_t>(meshes_.size())];
        if (!mesh.valid) continue;

        const Mat4 model = ModelFromTransform(transforms[item.transformIndex]);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, model.m.data());
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Tier2Opaque,
        "tier2_opaque_draw"
    );
}

void GLES3Renderer::DrawGizmoOverlay() noexcept {
    if (!gizmoProgram_ || !gizmoVao_ || !gizmoVertexCount_ ||
        !validator_.IsPassValid(AetherisValidationTracker::Pass::Gizmo))
        return;

    glUseProgram(gizmoProgram_);
    glBindVertexArray(gizmoVao_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUniformMatrix4fv(
        glGetUniformLocation(gizmoProgram_, "uViewProj"),
        1,
        GL_FALSE,
        camera_.viewProj.m.data()
    );
    glUniform3f(glGetUniformLocation(gizmoProgram_, "uOrigin"), 0.0f, 0.8f, 0.0f);
    glUniform1f(glGetUniformLocation(gizmoProgram_, "uScale"), 1.25f);

    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gizmoVertexCount_));

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);

    if (!validator_.ValidateGlError(
            AetherisValidationTracker::Pass::Gizmo,
            "gizmo_draw")) {
        validator_.IsolatePass(
            AetherisValidationTracker::Pass::Gizmo,
            "gizmo_draw_failure"
        );
    }
}

bool GLES3Renderer::DrawTier3(
    const RenderQueue& queue,
    std::span<const Transform> transforms
) noexcept {
    if (!BeginOffscreenPass()) return false;
    if (!skyVao_) {
        glGenVertexArrays(1, &skyVao_);
        if (!validator_.ValidateAllocation(
                AetherisValidationTracker::Kind::VertexArray,
                "sky_vao",
                skyVao_))
            return false;
    }

    if (!DrawSky()) return false;
    if (!DrawShadowMap(queue, transforms)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, offscreenFbo_);
    glClear(GL_DEPTH_BUFFER_BIT);

    if (!DrawOpaqueTier3(queue, transforms)) return false;
    DrawGizmoOverlay();
    glBindVertexArray(0);

    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Tier3Opaque,
        "tier3_complete"
    );
}

bool GLES3Renderer::DrawTier2(
    const RenderQueue& queue,
    std::span<const Transform> transforms
) noexcept {
    if (!BeginOffscreenPass()) return false;

    if (!skyVao_) {
        glGenVertexArrays(1, &skyVao_);
        if (!validator_.ValidateAllocation(
                AetherisValidationTracker::Kind::VertexArray,
                "sky_vao",
                skyVao_))
            return false;
    }

    glUseProgram(skyProgram_ ? skyProgram_ : 0);
    if (skyProgram_ && validator_.IsPassValid(AetherisValidationTracker::Pass::Sky)) {
        if (!DrawSky()) {
            glDisable(GL_DEPTH_TEST);
            glClearColor(0.10f, 0.18f, 0.24f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
        }
    } else {
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.10f,0.18f,0.24f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    if (!DrawOpaqueTier2(queue, transforms))
        return false;

    return validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Tier2Opaque,
        "tier2_complete"
    );
}

void GLES3Renderer::DrawTier1() noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(
        0,
        0,
        static_cast<GLsizei>(surfaceWidth_),
        static_cast<GLsizei>(surfaceHeight_)
    );
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glClearColor(0.10f, 0.18f, 0.24f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    validator_.ValidateGlError(
        AetherisValidationTracker::Pass::Offscreen,
        "tier1_framebuffer0_clear"
    );
}

bool GLES3Renderer::Initialize(ANativeWindow* window) {
    if (initialized_ || !window) return false;

    window_ = window;
    ANativeWindow_acquire(window_);

    if (!CreateContext() || !CreateSurface()) {
        Shutdown();
        return false;
    }

    initialized_ = true;
    tier_ = QualityTier::Tier1Framebuffer0;

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "OpenGLESRenderer initialized; Tier1 safe-present path armed"
    );
    return true;
}

bool GLES3Renderer::BeginFrame() {
    if (!initialized_ || begun_ ||
        display_ == EGL_NO_DISPLAY ||
        context_ == EGL_NO_CONTEXT ||
        surface_ == EGL_NO_SURFACE)
        return false;

    if (!ValidateCurrentContext()) {
        tier_ = QualityTier::Tier1Framebuffer0;
        return false;
    }

    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!QuerySurfaceExtent(width, height)) {
        tier_ = QualityTier::Tier1Framebuffer0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.10f,0.18f,0.24f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        begun_ = true;
        return true;
    }

    elapsedSeconds_ =
        static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        ) / 1000.0f;

    UpdateCamera();
    if (frameUbo_) UpdateFrameUbo();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.10f,0.18f,0.24f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    begun_ = true;
    return true;
}

void GLES3Renderer::EndFrame() {
    if (!begun_) return;

    bool presentSourceValid =
        tier_ != QualityTier::Tier1Framebuffer0 &&
        offscreenFbo_ != 0u &&
        validator_.IsPassValid(AetherisValidationTracker::Pass::Offscreen);

    if (presentSourceValid) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, offscreenFbo_);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(
            0,
            0,
            static_cast<GLint>(offscreenWidth_),
            static_cast<GLint>(offscreenHeight_),
            0,
            0,
            static_cast<GLint>(surfaceWidth_),
            static_cast<GLint>(surfaceHeight_),
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );

        if (!validator_.ValidateGlError(
                AetherisValidationTracker::Pass::Offscreen,
                "editor_to_window_blit")) {
            tier_ = QualityTier::Tier1Framebuffer0;
            DrawTier1();
        }
    } else {
        DrawTier1();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const EGLBoolean swapped = eglSwapBuffers(display_, surface_);
    if (swapped != EGL_TRUE) {
        const EGLint error = eglGetError();
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "eglSwapBuffers failed error=0x%x; switching to Tier1",
            error
        );
        tier_ = QualityTier::Tier1Framebuffer0;

        if (error == EGL_CONTEXT_LOST) {
            contextLost_ = true;
            deferredReady_ = false;
            tier2Ready_ = false;
            tier3Ready_ = false;
        } else if (error == EGL_BAD_SURFACE || error == EGL_BAD_NATIVE_WINDOW) {
            surface_ = EGL_NO_SURFACE;
        }
    }

    begun_ = false;
}

bool GLES3Renderer::RecreateSwapchain(ANativeWindow* window) {
    if (!initialized_ || !window) return false;

    if (window != window_) {
        DestroyEGLSurface();
        if (window_) ANativeWindow_release(window_);
        window_ = window;
        ANativeWindow_acquire(window_);
    } else {
        DestroyEGLSurface();
    }

    if (contextLost_)
        return RecreateContextAndSurface();

    if (!CreateSurface()) {
        contextLost_ = true;
        tier_ = QualityTier::Tier1Framebuffer0;
        return RecreateContextAndSurface();
    }

    deferredReady_ = false;
    tier2Ready_ = false;
    tier3Ready_ = false;
    tier_ = QualityTier::Tier1Framebuffer0;

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "surface recreated; advanced rendering gated until extent stabilizes"
    );
    return true;
}

void GLES3Renderer::ReleaseSurface() noexcept {
    begun_ = false;
    tier_ = QualityTier::Tier1Framebuffer0;
    stableExtentFrames_ = 0u;
    DestroyEGLSurface();

    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void GLES3Renderer::DrawRenderQueue(
    const RenderQueue& queue,
    std::span<const Transform> transforms
) {
    if (!begun_)
        return;

    try {
        if (!deferredReady_ || !tier2Ready_) {
            DrawTier1();
            return;
        }

        if (tier3Ready_ &&
            stableExtentFrames_ >= kMinStableFrames &&
            validator_.IsPassValid(AetherisValidationTracker::Pass::Sky) &&
            validator_.IsPassValid(AetherisValidationTracker::Pass::Shadow) &&
            validator_.IsPassValid(AetherisValidationTracker::Pass::Tier3Opaque)) {

            try {
                if (DrawTier3(queue, transforms)) {
                    tier_ = QualityTier::Tier3Advanced;
                    return;
                }
            } catch (...) {
                validator_.IsolatePass(
                    AetherisValidationTracker::Pass::Tier3Opaque,
                    "tier3_exception"
                );
            }

            tier3Ready_ = false;
            tier_ = QualityTier::Tier2Safe3D;
            __android_log_print(
                ANDROID_LOG_WARN,
                kLogTag,
                "Tier3 fault isolated; dropping to Tier2 Safe 3D"
            );
        }

        if (tier2Ready_) {
            try {
                if (DrawTier2(queue, transforms)) {
                    tier_ = QualityTier::Tier2Safe3D;
                    return;
                }
            } catch (...) {
                validator_.IsolatePass(
                    AetherisValidationTracker::Pass::Tier2Opaque,
                    "tier2_exception"
                );
            }
        }

        tier2Ready_ = false;
        tier_ = QualityTier::Tier1Framebuffer0;
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "Tier2 fault isolated; entering Framebuffer-0 bulletproof path"
        );
        DrawTier1();
    } catch (...) {
        tier2Ready_ = false;
        tier3Ready_ = false;
        tier_ = QualityTier::Tier1Framebuffer0;
        DrawTier1();
    }
}

void GLES3Renderer::Shutdown() noexcept {
    if (!initialized_ && display_ == EGL_NO_DISPLAY && !window_)
        return;

    if (display_ != EGL_NO_DISPLAY &&
        context_ != EGL_NO_CONTEXT &&
        surface_ != EGL_NO_SURFACE) {
        if (eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE)
            DestroyGLResources();
    }

    ReleaseSurface();

    if (display_ != EGL_NO_DISPLAY) {
        if (context_ != EGL_NO_CONTEXT)
            eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }

    context_ = EGL_NO_CONTEXT;
    display_ = EGL_NO_DISPLAY;
    config_ = nullptr;
    initialized_ = false;
    deferredReady_ = false;
    tier2Ready_ = false;
    tier3Ready_ = false;
    contextLost_ = false;
    begun_ = false;
    tier_ = QualityTier::Tier1Framebuffer0;
}

uint64_t GLES3Renderer::CreateOffscreenRenderTarget(
    uint32_t width,
    uint32_t height
) {
    if (!initialized_ || !ValidateCurrentContext())
        return 0u;

    return EnsureOffscreen(width, height)
        ? 1u
        : 0u;
}

bool GLES3Renderer::ResizeOffscreenRenderTarget(
    uint64_t handle,
    uint32_t width,
    uint32_t height
) {
    if (handle == 0u || !initialized_ || !ValidateCurrentContext())
        return false;
    return EnsureOffscreen(width, height);
}

uint64_t GLES3Renderer::GetOffscreenColorHandle(
    uint64_t handle
) const noexcept {
    return handle == 1u
        ? static_cast<uint64_t>(offscreenColor_)
        : 0u;
}

Mat4 GLES3Renderer::Identity() noexcept {
    Mat4 r{};
    r.m[0] = 1.0f;
    r.m[5] = 1.0f;
    r.m[10] = 1.0f;
    r.m[15] = 1.0f;
    return r;
}

Mat4 GLES3Renderer::Multiply(
    const Mat4& a,
    const Mat4& b
) noexcept {
    Mat4 r{};
    for (uint32_t col = 0u; col < 4u; ++col) {
        for (uint32_t row = 0u; row < 4u; ++row) {
            float value = 0.0f;
            for (uint32_t k = 0u; k < 4u; ++k)
                value += a.m[k * 4u + row] * b.m[col * 4u + k];
            r.m[col * 4u + row] = value;
        }
    }
    return r;
}

Mat4 GLES3Renderer::Perspective(
    float fovRadians,
    float aspect,
    float nearPlane,
    float farPlane
) noexcept {
    Mat4 r{};
    const float f = 1.0f / std::tan(fovRadians * 0.5f);
    r.m[0] = f / std::max(aspect, 0.001f);
    r.m[5] = f;
    r.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return r;
}

Mat4 GLES3Renderer::LookAt(
    const Vec4& eye,
    const Vec4& target,
    const Vec4& up
) noexcept {
    const Vec4 z = Normalize3({
        eye.x - target.x,
        eye.y - target.y,
        eye.z - target.z,
        0.0f
    });
    const Vec4 x = Normalize3(Cross3(up, z));
    const Vec4 y = Cross3(z, x);

    Mat4 r = Identity();
    r.m[0] = x.x; r.m[1] = y.x; r.m[2] = z.x;
    r.m[4] = x.y; r.m[5] = y.y; r.m[6] = z.y;
    r.m[8] = x.z; r.m[9] = y.z; r.m[10] = z.z;

    r.m[12] = -(x.x * eye.x + x.y * eye.y + x.z * eye.z);
    r.m[13] = -(y.x * eye.x + y.y * eye.y + y.z * eye.z);
    r.m[14] = -(z.x * eye.x + z.y * eye.y + z.z * eye.z);
    return r;
}

Mat4 GLES3Renderer::Inverse(const Mat4& m) noexcept {
    const float* a = m.m.data();
    Mat4 r{};
    float* inv = r.m.data();

    inv[0] = a[5]  * a[10] * a[15] -
             a[5]  * a[11] * a[14] -
             a[9]  * a[6]  * a[15] +
             a[9]  * a[7]  * a[14] +
             a[13] * a[6]  * a[11] -
             a[13] * a[7]  * a[10];
    inv[4] = -a[4]  * a[10] * a[15] +
              a[4]  * a[11] * a[14] +
              a[8]  * a[6]  * a[15] -
              a[8]  * a[7]  * a[14] -
              a[12] * a[6]  * a[11] +
              a[12] * a[7]  * a[10];
    inv[8] = a[4]  * a[9] * a[15] -
             a[4]  * a[11] * a[13] -
             a[8]  * a[5] * a[15] +
             a[8]  * a[7] * a[13] +
             a[12] * a[5] * a[11] -
             a[12] * a[7] * a[9];
    inv[12] = -a[4]  * a[9] * a[14] +
               a[4]  * a[10] * a[13] +
               a[8]  * a[5] * a[14] -
               a[8]  * a[6] * a[13] -
               a[12] * a[5] * a[10] +
               a[12] * a[6] * a[9];
    inv[1] = -a[1]  * a[10] * a[15] +
              a[1]  * a[11] * a[14] +
              a[9]  * a[2]  * a[15] -
              a[9]  * a[3]  * a[14] -
              a[13] * a[2]  * a[11] +
              a[13] * a[3]  * a[10];
    inv[5] = a[0]  * a[10] * a[15] -
             a[0]  * a[11] * a[14] -
             a[8]  * a[2] * a[15] +
             a[8]  * a[3] * a[14] +
             a[12] * a[2] * a[11] -
             a[12] * a[3] * a[10];
    inv[9] = -a[0]  * a[9] * a[15] +
              a[0]  * a[11] * a[13] +
              a[8]  * a[1] * a[15] -
              a[8]  * a[3] * a[13] -
              a[12] * a[1] * a[11] +
              a[12] * a[3] * a[9];
    inv[13] = a[0]  * a[9] * a[14] -
              a[0]  * a[10] * a[13] -
              a[8]  * a[1] * a[14] +
              a[8]  * a[2] * a[13] +
              a[12] * a[1] * a[10] -
              a[12] * a[2] * a[9];
    inv[2] = a[1]  * a[6] * a[15] -
             a[1]  * a[7] * a[14] -
             a[5]  * a[2] * a[15] +
             a[5]  * a[3] * a[14] +
             a[13] * a[2] * a[7] -
             a[13] * a[3] * a[6];
    inv[6] = -a[0]  * a[6] * a[15] +
              a[0]  * a[7] * a[14] +
              a[4]  * a[2] * a[15] -
              a[4]  * a[3] * a[14] -
              a[12] * a[2] * a[7] +
              a[12] * a[3] * a[6];
    inv[10] = a[0]  * a[5] * a[15] -
              a[0]  * a[7] * a[13] -
              a[4]  * a[1] * a[15] +
              a[4]  * a[3] * a[13] +
              a[12] * a[1] * a[7] -
              a[12] * a[3] * a[5];
    inv[14] = -a[0]  * a[5] * a[14] +
               a[0]  * a[6] * a[13] +
               a[4]  * a[1] * a[14] -
               a[4]  * a[2] * a[13] -
               a[12] * a[1] * a[6] +
               a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] +
              a[1] * a[7] * a[10] +
              a[5] * a[2] * a[11] -
              a[5] * a[3] * a[10] -
              a[9] * a[2] * a[7] +
              a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] -
             a[0] * a[7] * a[10] -
             a[4] * a[2] * a[11] +
             a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] -
             a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] +
               a[0] * a[7] * a[9] +
               a[4] * a[1] * a[11] -
               a[4] * a[3] * a[9] -
               a[8] * a[1] * a[7] +
               a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] -
              a[0] * a[6] * a[9] -
              a[4] * a[1] * a[10] +
              a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] -
              a[8] * a[2] * a[5];

    float determinant =
        a[0] * inv[0] + a[1] * inv[4] +
        a[2] * inv[8] + a[3] * inv[12];

    if (std::fabs(determinant) <= 1e-8f)
        return Identity();

    determinant = 1.0f / determinant;
    for (float& value : r.m)
        value *= determinant;
    return r;
}

Mat4 GLES3Renderer::ModelFromTransform(
    const Transform& transform
) noexcept {
    const float x = transform.rotation.x;
    const float y = transform.rotation.y;
    const float z = transform.rotation.z;
    const float w = transform.rotation.w;

    Mat4 r = Identity();
    r.m[0] = 1.0f - 2.0f * (y*y + z*z);
    r.m[1] = 2.0f * (x*y + z*w);
    r.m[2] = 2.0f * (x*z - y*w);

    r.m[4] = 2.0f * (x*y - z*w);
    r.m[5] = 1.0f - 2.0f * (x*x + z*z);
    r.m[6] = 2.0f * (y*z + x*w);

    r.m[8] = 2.0f * (x*z + y*w);
    r.m[9] = 2.0f * (y*z - x*w);
    r.m[10] = 1.0f - 2.0f * (x*x + y*y);

    r.m[0] *= transform.scale.x;
    r.m[1] *= transform.scale.x;
    r.m[2] *= transform.scale.x;

    r.m[4] *= transform.scale.y;
    r.m[5] *= transform.scale.y;
    r.m[6] *= transform.scale.y;

    r.m[8] *= transform.scale.z;
    r.m[9] *= transform.scale.z;
    r.m[10] *= transform.scale.z;

    r.m[12] = transform.position.x;
    r.m[13] = transform.position.y;
    r.m[14] = transform.position.z;
    return r;
}

Vec4 GLES3Renderer::Normalize4(const Vec4& v) noexcept {
    return Normalize3(v);
}

const char* GLES3Renderer::TierName(
    QualityTier tier
) noexcept {
    switch (tier) {
        case QualityTier::Tier1Framebuffer0: return "Tier1Framebuffer0";
        case QualityTier::Tier2Safe3D: return "Tier2Safe3D";
        case QualityTier::Tier3Advanced: return "Tier3Advanced";
        default: return "Unknown";
    }
}

} // namespace aetheris
