#version 300 es
precision highp float;
layout(location=0) in vec3 InPosition;
uniform mat4 uLightViewProj;
uniform mat4 uModel;
void main(){gl_Position=uLightViewProj*uModel*vec4(InPosition,1.0);}
