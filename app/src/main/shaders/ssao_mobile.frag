#version 450

layout(location=0) in vec2 UV;
layout(location=0) out float OutAO;

layout(set=0,binding=0) uniform sampler2D NormalMap;
layout(set=0,binding=1) uniform sampler2D DepthMap;

layout(set=0,binding=2,std140) uniform Frame {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
} F;

const vec3 Kernel[8] = vec3[8](
    vec3( 0.122, 0.045, 0.113),
    vec3(-0.179, 0.091, 0.074),
    vec3( 0.248,-0.112, 0.033),
    vec3(-0.312,-0.052, 0.138),
    vec3( 0.086, 0.162, 0.231),
    vec3(-0.102, 0.214, 0.171),
    vec3( 0.334, 0.058, 0.093),
    vec3(-0.286, 0.176, 0.241)
);

float Hash12(vec2 p)
{
    vec3 p3 =
        fract(
            vec3(p.xyx) *
            0.1031
        );

    p3 +=
        dot(
            p3,
            p3.yzx + 33.33
        );

    return
        fract(
            (p3.x + p3.y) *
            p3.z
        );
}

vec3 ReconstructWorld(
    vec2 uv,
    float depth
) {
    vec4 clip =
        vec4(
            clamp(
                uv,
                vec2(0.0),
                vec2(1.0)
            ) * 2.0 - 1.0,
            clamp(
                depth,
                0.0,
                1.0
            ),
            1.0
        );

    vec4 world =
        F.InvViewProj *
        clip;

    world.xyz /=
        max(
            world.w,
            1e-6
        );

    return world.xyz;
}

void main()
{
    const vec2 safeUV =
        clamp(
            UV,
            vec2(0.0),
            vec2(1.0)
        );

    const float depth =
        texture(
            DepthMap,
            safeUV
        ).r;

    if (depth >= 0.99999) {
        OutAO = 1.0;
        return;
    }

    const vec3 position =
        ReconstructWorld(
            safeUV,
            depth
        );

    const vec3 normal =
        normalize(
            texture(
                NormalMap,
                safeUV
            ).xyz *
            2.0 -
            1.0
        );

    const vec3 randomVector =
        normalize(
            vec3(
                Hash12(safeUV),
                Hash12(safeUV + 17.23),
                Hash12(safeUV + 41.71)
            ) * 2.0 -
            1.0
        );

    const vec3 tangent =
        normalize(
            randomVector -
            normal *
            dot(
                randomVector,
                normal
            )
        );

    const vec3 bitangent =
        normalize(
            cross(
                normal,
                tangent
            )
        );

    const mat3 TBN =
        mat3(
            tangent,
            bitangent,
            normal
        );

    const float radius =
        1.10;

    const float bias =
        0.025;

    float occlusion = 0.0;

    for (int i=0; i<8; ++i) {
        const float scale =
            0.15 +
            pow(
                float(i + 1) / 8.0,
                2.0
            ) *
            0.85;

        const vec3 samplePosition =
            position +
            TBN *
            Kernel[i] *
            radius *
            scale;

        const vec4 projected =
            F.InvViewProj *
            vec4(
                samplePosition,
                1.0
            );

        if (projected.w <= 1e-6)
            continue;

        vec2 sampleUV =
            projected.xy /
            projected.w;

        if (
            sampleUV.x < 0.0 ||
            sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 ||
            sampleUV.y > 1.0
        ) {
            continue;
        }

        sampleUV =
            clamp(
                sampleUV,
                vec2(0.0),
                vec2(1.0)
            );

        const float sampledDepth =
            texture(
                DepthMap,
                sampleUV
            ).r;

        if (sampledDepth >= 0.99999)
            continue;

        const vec3 sampledPosition =
            ReconstructWorld(
                sampleUV,
                sampledDepth
            );

        const float rangeWeight =
            1.0 -
            smoothstep(
                0.0,
                radius,
                distance(
                    sampledPosition,
                    position
                )
            );

        const float blocked =
            dot(
                sampledPosition -
                samplePosition,
                normal
            ) > bias
                ? 1.0
                : 0.0;

        occlusion +=
            blocked *
            max(
                rangeWeight,
                0.0
            );
    }

    const float ao =
        1.0 -
        occlusion / 8.0;

    OutAO =
        clamp(
            mix(
                1.0,
                ao,
                0.80
            ),
            0.20,
            1.0
        );
}
