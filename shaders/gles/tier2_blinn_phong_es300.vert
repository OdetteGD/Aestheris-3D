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

void main(){
    vec4 world=uModel*vec4(InPosition,1.0);
    WorldPos=world.xyz;
    WorldNormal=normalize(mat3(transpose(inverse(uModel)))*InNormal);
    UV=InUV;
    Material=InBaseColorMetallic;
    RoughnessAO=InRoughnessAO;
    gl_Position=uViewProj*world;
}
