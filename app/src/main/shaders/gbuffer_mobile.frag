#version 450
layout(location=0) in vec3 WorldPos;
layout(location=1) in vec3 Normal;
layout(location=2) in vec2 UV;

layout(location=0) out vec4 OutPosition; // xyz world position, w unused
layout(location=1) out vec4 OutNormal;   // encoded normal, w roughness
layout(location=2) out vec4 OutAlbedo;   // albedo, w AO

layout(set=0,binding=0,std140) uniform Material {
    vec4 BaseColorMetallic;
    vec4 RoughnessNormalAo;
} M;

layout(set=0,binding=1) uniform sampler2D AlbedoMap;
layout(set=0,binding=2) uniform sampler2D NormalMap;
layout(set=0,binding=3) uniform sampler2D ORMMap;

void main()
{
    vec4 albedoSample = texture(AlbedoMap, UV);
    vec3 albedo = albedoSample.rgb * M.BaseColorMetallic.rgb;
    float metallic = clamp(albedoSample.a * M.BaseColorMetallic.a, 0.0, 1.0);

    vec3 N = normalize(Normal);

    vec3 dp1 = dFdx(WorldPos);
    vec3 dp2 = dFdy(WorldPos);
    vec2 duv1 = dFdx(UV);
    vec2 duv2 = dFdy(UV);

    vec3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    T = normalize(T - N * dot(N, T));

    vec3 B = normalize(cross(N, T));
    vec3 tangentNormal = texture(NormalMap, UV).xyz * 2.0 - 1.0;
    tangentNormal.xy *= M.RoughnessNormalAo.y;
    N = normalize(mat3(T, B, N) * tangentNormal);

    vec4 orm = texture(ORMMap, UV);
    float roughness = clamp(
        orm.g * M.RoughnessNormalAo.x,
        0.045,
        1.0
    );
    float ao = clamp(
        orm.b * M.RoughnessNormalAo.z,
        0.0,
        1.0
    );

    OutPosition = vec4(WorldPos, metallic);
    OutNormal = vec4(N * 0.5 + 0.5, roughness);
    OutAlbedo = vec4(albedo, ao);
}
