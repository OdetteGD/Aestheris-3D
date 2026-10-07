#version 450
layout(location=0) in vec2 UV;
layout(location=0) out vec4 OutColor;

layout(set=0,binding=0) uniform sampler2D HDR;
layout(set=0,binding=1) uniform sampler2D Bloom;

layout(push_constant) uniform PostPC {
    vec4 Params; // exposure, bloomStrength, invW, invH
} PC;

vec3 ACESFilm(vec3 x)
{
    const float a=2.51;
    const float b=0.03;
    const float c=2.43;
    const float d=0.59;
    const float e=0.14;

    return clamp(
        (x*(a*x+b))/(x*(c*x+d)+e),
        0.0,
        1.0
    );
}

void main()
{
    vec3 hdr =
        texture(
            HDR,
            UV
        ).rgb;

    vec3 bloom =
        texture(
            Bloom,
            UV
        ).rgb;

    hdr += bloom * PC.Params.y;
    hdr *= PC.Params.x;

    vec3 mapped =
        ACESFilm(
            max(
                hdr,
                vec3(0.0)
            )
        );

    mapped =
        pow(
            mapped,
            vec3(1.0 / 2.2)
        );

    OutColor =
        vec4(
            mapped,
            1.0
        );
}
