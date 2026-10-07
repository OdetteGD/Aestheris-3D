#version 450
layout(location=0) in vec2 UV;
layout(location=0) out vec4 OutColor;
layout(set=0,binding=0) uniform sampler2D Bloom;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(Bloom,0));
    vec3 c = texture(Bloom,UV).rgb * 0.40;

    c += texture(Bloom,UV + vec2( texel.x,0.0)).rgb * 0.15;
    c += texture(Bloom,UV - vec2( texel.x,0.0)).rgb * 0.15;
    c += texture(Bloom,UV + vec2(0.0, texel.y)).rgb * 0.15;
    c += texture(Bloom,UV - vec2(0.0, texel.y)).rgb * 0.15;

    OutColor = vec4(c,1.0);
}
