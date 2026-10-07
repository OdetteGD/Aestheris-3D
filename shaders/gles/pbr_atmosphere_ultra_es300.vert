#version 300 es
precision highp float;

layout(location=0) in vec3 InPosition;
layout(location=1) in vec3 InNormal;
layout(location=2) in vec2 InUV;

layout(location=0) out vec3 WorldPos;
layout(location=1) out vec3 Normal;
layout(location=2) out vec2 UV;

layout(std140) uniform Camera {
    mat4 ViewProj;
    mat4 Model;
};

void main()
{
    vec4 world =
        Model *
        vec4(
            InPosition,
            1.0
        );

    WorldPos = world.xyz;
    Normal = normalize(
        mat3(transpose(inverse(Model))) *
        InNormal
    );
    UV = InUV;

    gl_Position =
        ViewProj *
        world;
}
