#version 450

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUV;
layout(location = 3) in vec4 InBaseColorMetallic;
layout(location = 4) in vec2 InRoughnessAO;

layout(location = 0) out vec3 WorldPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 UV;
layout(location = 3) out vec4 BaseColorMetallic;
layout(location = 4) out vec2 RoughnessAO;

layout(push_constant) uniform GeometryPC {
    mat4 ViewProj;
    mat4 Model;
} PC;

void main()
{
    vec4 world = PC.Model * vec4(InPosition, 1.0);
    WorldPos = world.xyz;
    Normal = normalize(mat3(transpose(inverse(PC.Model))) * InNormal);
    UV = InUV;
    BaseColorMetallic = InBaseColorMetallic;
    RoughnessAO = InRoughnessAO;
    gl_Position = PC.ViewProj * world;
}
