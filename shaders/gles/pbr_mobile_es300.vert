#version 300 es
precision highp float;
layout(location=0) in vec3 InPosition;
layout(location=1) in vec3 InNormal;
layout(location=2) in vec2 InUV;
layout(location=3) in vec4 InBaseColorMetallic;
layout(location=4) in vec2 InRoughnessAO;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uLightViewProj;

out vec3 WorldPos;
out vec3 WorldNormal;
out vec2 UV;
out vec4 Material;
out vec2 RoughnessAO;
out vec4 ShadowCoord;

void main(){
    vec4 world=uModel*vec4(InPosition,1.0);
    WorldPos=world.xyz;
    WorldNormal=normalize(mat3(transpose(inverse(uModel)))*InNormal);
    UV=InUV;
    Material=InBaseColorMetallic;
    RoughnessAO=InRoughnessAO;
    ShadowCoord=uLightViewProj*world;
    gl_Position=uViewProj*world;
}
