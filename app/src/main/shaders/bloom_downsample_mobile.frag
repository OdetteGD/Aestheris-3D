#version 450
layout(location=0) in vec2 UV;
layout(location=0) out vec4 OutColor;
layout(set=0,binding=0) uniform sampler2D SourceHDR;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(SourceHDR,0));
    vec3 s = vec3(0.0);
    s += texture(SourceHDR, UV + texel*vec2(-1.0,-1.0)).rgb;
    s += texture(SourceHDR, UV + texel*vec2( 1.0,-1.0)).rgb;
    s += texture(SourceHDR, UV + texel*vec2(-1.0, 1.0)).rgb;
    s += texture(SourceHDR, UV + texel*vec2( 1.0, 1.0)).rgb;
    s += texture(SourceHDR, UV).rgb * 2.0;
    s /= 6.0;

    float luma =
        dot(
            s,
            vec3(
                0.2126,
                0.7152,
                0.0722
            )
        );

    s *= smoothstep(0.75, 2.5, luma);

    OutColor =
        vec4(
            s,
            1.0
        );
}
