#version 300 es
precision highp float;

layout(location=0) out float OutAO;

uniform sampler2D uNormalTex;
uniform sampler2D uDepthTex;
uniform mat4 uInvViewProj;
uniform vec2 uTexelSize;
uniform float uRadius;
uniform float uBias;

const vec3 KERNEL[8] = vec3[8](
    vec3( 0.122, 0.045, 0.113),
    vec3(-0.179, 0.091, 0.074),
    vec3( 0.248,-0.112, 0.033),
    vec3(-0.312,-0.052, 0.138),
    vec3( 0.086, 0.162, 0.231),
    vec3(-0.102, 0.214, 0.171),
    vec3( 0.334, 0.058, 0.093),
    vec3(-0.286, 0.176, 0.241)
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
        uInvViewProj *
        clip;

    return
        world.xyz /
        max(
            world.w,
            1e-6
        );
}

void main()
{
    const vec2 safeUv =
        clamp(
            gl_FragCoord.xy *
                uTexelSize,
            vec2(0.0),
            vec2(1.0)
        );

    const float depth =
        texture(
            uDepthTex,
            safeUv
        ).r;

    if (depth >= 0.99999) {
        OutAO = 1.0;
        return;
    }

    const vec3 position =
        ReconstructWorld(
            safeUv,
            depth
        );

    const vec3 normal =
        normalize(
            texture(
                uNormalTex,
                safeUv
            ).xyz *
            2.0 -
            1.0
        );

    const vec3 randomVector =
        normalize(
            vec3(
                hash12(safeUv * 127.1),
                hash12(safeUv * 311.7),
                hash12(safeUv * 521.9)
            ) * 2.0 - 1.0
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

    const mat3 tbn =
        mat3(
            tangent,
            bitangent,
            normal
        );

    const float radius =
        clamp(
            uRadius,
            0.05,
            2.0
        );

    const float bias =
        max(
            uBias,
            0.0005
        );

    float occlusion = 0.0;

    for (int i = 0; i < 8; ++i) {
        const float scale =
            0.15 +
            pow(
                float(i + 1) / 8.0,
                2.0
            ) * 0.85;

        const vec3 samplePosition =
            position +
            tbn *
            KERNEL[i] *
            radius *
            scale;

        const vec4 projected =
            uInvViewProj *
            vec4(
                samplePosition,
                1.0
            );

        if (projected.w <= 1e-6)
            continue;

        vec2 sampleUv =
            projected.xy /
            projected.w;

        if (
            sampleUv.x < 0.0 ||
            sampleUv.x > 1.0 ||
            sampleUv.y < 0.0 ||
            sampleUv.y > 1.0
        ) {
            continue;
        }

        sampleUv =
            clamp(
                sampleUv,
                vec2(0.0),
                vec2(1.0)
            );

        const float sampledDepth =
            texture(
                uDepthTex,
                sampleUv
            ).r;

        const vec3 sampledPosition =
            ReconstructWorld(
                sampleUv,
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
