#version 450

layout(location = 0) in vec2 UV;
layout(location = 0) out vec4 OutColor;

layout(set = 0, binding = 0) uniform sampler2D HDR;

layout(push_constant) uniform PostPC {
    vec4 Params;
} PC;

vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp(
        (x * (a * x + b)) /
        (x * (c * x + d) + e),
        0.0,
        1.0
    );
}

float Luma(vec3 x)
{
    return dot(
        x,
        vec3(
            0.2126,
            0.7152,
            0.0722
        )
    );
}

void main()
{
    const vec2 taps[9] = vec2[9](
        vec2(-1.0,-1.0),
        vec2( 0.0,-1.0),
        vec2( 1.0,-1.0),
        vec2(-1.0, 0.0),
        vec2( 0.0, 0.0),
        vec2( 1.0, 0.0),
        vec2(-1.0, 1.0),
        vec2( 0.0, 1.0),
        vec2( 1.0, 1.0)
    );

    vec3 base = texture(HDR, UV).rgb;

    vec3 bloom = vec3(0.0);
    float weight = 0.0;

    for (int i = 0; i < 9; ++i) {
        vec3 sampleColor =
            texture(
                HDR,
                UV +
                taps[i] *
                PC.Params.yz
            ).rgb;

        float bright =
            max(
                Luma(sampleColor) - 1.0,
                0.0
            );

        bloom += sampleColor * bright;
        weight += bright;
    }

    if (weight > 1e-5)
        bloom /= weight;

    vec3 hdr =
        max(
            base +
            bloom * PC.Params.w,
            vec3(0.0)
        ) *
        max(
            PC.Params.x,
            0.0
        );

    vec3 ldr = ACESFilm(hdr);

    ldr =
        pow(
            ldr,
            vec3(1.0 / 2.2)
        );

    OutColor = vec4(ldr, 1.0);
}
