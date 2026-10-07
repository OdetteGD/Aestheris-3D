#version 450
layout(location=0) in vec3 InPosition;

layout(push_constant) uniform ShadowPC {
    mat4 LightViewProj;
    mat4 Model;
} PC;

void main()
{
    gl_Position =
        PC.LightViewProj *
        PC.Model *
        vec4(InPosition,1.0);
}
