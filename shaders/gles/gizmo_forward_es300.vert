#version 300 es
precision highp float;
layout(location=0) in vec3 InPosition;
layout(location=1) in vec4 InColor;
uniform mat4 uViewProj;
uniform vec3 uOrigin;
uniform float uScale;
out vec4 Color;
void main(){
    Color=InColor;
    gl_Position=uViewProj*vec4(uOrigin+InPosition*uScale,1.0);
}
