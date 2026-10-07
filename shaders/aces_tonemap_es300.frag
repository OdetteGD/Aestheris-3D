#version 300 es
precision highp float;

in vec2 UV;
layout(location = 0) out vec4 OutColor;

uniform sampler2D HDR;
uniform float Exposure;

vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = max(texture(HDR, UV).rgb * max(Exposure, 0.0), vec3(0.0));
    vec3 ldr = ACESFilm(hdr);
    ldr = pow(ldr, vec3(1.0 / 2.2));
    OutColor = vec4(ldr, 1.0);
}
