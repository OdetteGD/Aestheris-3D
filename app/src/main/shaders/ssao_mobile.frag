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

const vec3 Kernel[16] = vec3[16](
    vec3( 0.122, 0.045, 0.113),
    vec3(-0.179, 0.091, 0.074),
    vec3( 0.248,-0.112, 0.033),
    vec3(-0.312,-0.052, 0.138),
    vec3( 0.086, 0.162, 0.231),
    vec3(-0.102, 0.214, 0.171),
    vec3( 0.334, 0.058, 0.093),
    vec3(-0.286, 0.176, 0.241),
    vec3( 0.192,-0.254, 0.102),
    vec3(-0.094,-0.198, 0.312),
    vec3( 0.382,-0.081, 0.187),
    vec3(-0.421,0.102,0.155),
    vec3( 0.166,0.281,0.107),
    vec3(-0.205,0.337,0.084),
    vec3( 0.284,0.224,0.192),
    vec3(-0.351,0.254,0.062)
);

float hash12(vec2 p)
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
            (p3.x+p3.y) *
            p3.z
        );
}

vec3 ReconstructViewPosition(
    vec2 uv,
    float depth)
{
    vec4 clip =
        vec4(
            uv * 2.0 - 1.0,
            depth,
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

    return
        world.xyz -
        F.CameraPosition.xyz;
}

void main()
{
    float depth =
        texture(
            DepthMap,
            UV
        ).r;

    if(depth >= 0.99999) {
        OutAO = 1.0;
        return;
    }

    vec3 viewPosition =
        ReconstructViewPosition(
            UV,
            depth
        );

    vec3 normal =
        normalize(
            texture(
                NormalMap,
                UV
            ).xyz *
            2.0 - 1.0
        );

    vec2 noiseUv =
        UV *
        vec2(
            max(F.CameraRight.w,1.0),
            max(F.CameraUp.w,1.0)
        ) *
        0.03125;

    vec3 randomVector =
        normalize(
            vec3(
                hash12(noiseUv),
                hash12(
                    noiseUv +
                    17.23
                ),
                hash12(
                    noiseUv +
                    41.71
                )
            ) *
            2.0 - 1.0
        );

    vec3 tangent =
        normalize(
            randomVector -
            normal *
            dot(
                randomVector,
                normal
            )
        );

    vec3 bitangent =
        normalize(
            cross(
                normal,
                tangent
            )
        );

    mat3 TBN =
        mat3(
            tangent,
            bitangent,
            normal
        );

    float radius =
        1.35;

    float bias =
        0.025;

    float occlusion = 0.0;

    for(int i=0;i<16;++i) {

        float scale =
            float(i+1) / 16.0;

        scale =
            0.15 +
            scale *
            scale *
            0.85;

        vec3 sampleVector =
            TBN *
            Kernel[i];

        vec3 samplePosition =
            viewPosition +
            sampleVector *
            radius *
            scale;

        vec4 projected =
            F.InvViewProj *
            vec4(
                samplePosition +
                F.CameraPosition.xyz,
                1.0
            );

        if(projected.w <= 1e-6)
            continue;

        vec2 sampleUv =
            projected.xy /
            projected.w;

        sampleUv =
            sampleUv *
            0.5 +
            0.5;

        if(
            sampleUv.x < 0.0 ||
            sampleUv.x > 1.0 ||
            sampleUv.y < 0.0 ||
            sampleUv.y > 1.0
        )
            continue;

        float sampleDepth =
            texture(
                DepthMap,
                sampleUv
            ).r;

        vec3 realSample =
            ReconstructViewPosition(
                sampleUv,
                sampleDepth
            );

        float range =
            smoothstep(
                0.0,
                1.0,
                radius /
                max(
                    abs(
                        viewPosition.z -
                        realSample.z
                    ),
                    1e-4
                )
            );

        float blocked =
            realSample.z >
            samplePosition.z + bias
                ? 1.0
                : 0.0;

        occlusion +=
            blocked *
            range;
    }

    occlusion =
        1.0 -
        occlusion /
        16.0;

    // Preserve some grazing-angle ambient to avoid over-darkening mobile scenes.
    float horizon =
        clamp(
            0.35 +
            0.65 *
            dot(
                normal,
                normalize(
                    F.CameraForward.xyz
                ) * -1.0
            ),
            0.25,
            1.0
        );

    OutAO =
        clamp(
            mix(
                1.0,
                occlusion,
                0.82
            ) *
            horizon,
            0.12,
            1.0
        );
}
