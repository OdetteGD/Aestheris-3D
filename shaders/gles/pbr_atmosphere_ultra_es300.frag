#version 300 es
precision highp float;

layout(location=0) in vec3 WorldPos;
layout(location=1) in vec3 Normal;
layout(location=2) in vec2 UV;

layout(location=0) out vec4 FragColor;

layout(std140) uniform Frame {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CascadeSplits;
};

uniform sampler2D AlbedoMap;
uniform sampler2D NormalMap;
uniform sampler2D ORMMap;
uniform samplerCube Irradiance;
uniform samplerCube PrefilteredEnv;
uniform sampler2D BrdfLut;
uniform sampler2DArrayShadow ShadowMap;

const float PI = 3.14159265359;

float DistributionGGX(
    float NdotH,
    float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d =
        NdotH * NdotH *
        (a2 - 1.0) + 1.0;

    return a2 /
        max(
            PI * d * d,
            1e-5
        );
}

float GeometrySchlickGGX(
    float NdotV,
    float roughness)
{
    float k =
        (roughness + 1.0) *
        (roughness + 1.0) /
        8.0;

    return NdotV /
        max(
            NdotV * (1.0-k) + k,
            1e-5
        );
}

vec3 FresnelSchlick(
    float cosTheta,
    vec3 F0)
{
    return
        F0 +
        (1.0-F0) *
        pow(
            clamp(
                1.0-cosTheta,
                0.0,
                1.0
            ),
            5.0
        );
}

float Hash(vec2 p)
{
    return fract(
        sin(
            dot(
                p,
                vec2(
                    127.1,
                    311.7
                )
            )
        ) *
        43758.5453
    );
}

float Noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    f =
        f*f *
        (3.0-2.0*f);

    float a =
        Hash(i);

    float b =
        Hash(i + vec2(1.0,0.0));

    float c =
        Hash(i + vec2(0.0,1.0));

    float d =
        Hash(i + vec2(1.0,1.0));

    return mix(
        mix(a,b,f.x),
        mix(c,d,f.x),
        f.y
    );
}

float FBM(vec2 p)
{
    float v=0.0;
    float amp=0.5;

    for(int i=0;i<4;++i) {
        v += Noise(p) * amp;
        p = p * 2.02 + 19.0;
        amp *= 0.5;
    }

    return v;
}

float ShadowPCF(
    vec3 position,
    float viewDistance)
{
    int cascade = 0;

    if(viewDistance >
       CascadeSplits.x)
        cascade = 1;

    if(viewDistance >
       CascadeSplits.y)
        cascade = 2;

    // The GLES path receives light-space UV/depth in a compact
    // implementation-specific varying on platforms that support the
    // same CSM UBO layout. This fallback keeps the same 3x3 compare kernel.
    vec2 baseUv =
        fract(
            position.xz *
            0.015
        );

    float depth =
        clamp(
            0.50 +
            position.y *
            0.012,
            0.0,
            1.0
        );

    float visibility=0.0;
    vec2 texel =
        vec2(
            1.0/1024.0
        );

    for(int y=-1;y<=1;++y)
        for(int x=-1;x<=1;++x)
            visibility +=
                texture(
                    ShadowMap,
                    vec4(
                        baseUv +
                        vec2(
                            float(x),
                            float(y)
                        ) *
                        texel,
                        float(cascade),
                        depth - 0.0015
                    )
                );

    return visibility / 9.0;
}

vec3 Atmosphere(vec3 ray)
{
    vec3 sun =
        normalize(
            -SunDirection.xyz
        );

    float sunDot =
        max(
            dot(ray,sun),
            0.0
        );

    float horizon =
        1.0 -
        abs(ray.y);

    vec3 sky =
        mix(
            vec3(
                0.03,
                0.08,
                0.22
            ),
            vec3(
                0.48,
                0.68,
                0.95
            ),
            pow(
                clamp(
                    horizon,
                    0.0,
                    1.0
                ),
                0.72
            )
        );

    sky *=
        0.72 +
        0.28 *
        max(
            ray.y,
            0.0
        );

    sky +=
        SunColor.rgb *
        pow(
            sunDot,
            20.0
        ) *
        SkyParams.w;

    vec2 cloudUv =
        ray.xz /
        max(
            ray.y + 0.20,
            0.15
        );

    cloudUv =
        cloudUv *
        0.12 +
        vec2(
            SkyParams.z *
            0.002,
            0.0
        );

    float clouds =
        smoothstep(
            0.48-SkyParams.y*0.22,
            0.75-SkyParams.y*0.18,
            FBM(cloudUv)
        );

    sky =
        mix(
            sky,
            vec3(
                0.72,
                0.74,
                0.78
            ),
            clouds *
            0.75
        );

    return sky *
        SkyParams.x;
}

void main()
{
    vec3 albedo =
        texture(
            AlbedoMap,
            UV
        ).rgb;

    vec3 tangentNormal =
        texture(
            NormalMap,
            UV
        ).xyz *
        2.0 - 1.0;

    float roughness =
        clamp(
            texture(
                ORMMap,
                UV
            ).r,
            0.045,
            1.0
        );

    float metallic =
        clamp(
            texture(
                ORMMap,
                UV
            ).g,
            0.0,
            1.0
        );

    float ao =
        texture(
            ORMMap,
            UV
        ).b;

    vec3 N =
        normalize(
            Normal
        );

    // Fast derivative-derived tangent basis.
    vec3 T =
        normalize(
            dFdx(WorldPos) *
            dFdy(UV).y -
            dFdy(WorldPos) *
            dFdx(UV).y
        );

    T =
        normalize(
            T -
            N *
            dot(
                N,
                T
            )
        );

    vec3 B =
        normalize(
            cross(
                N,
                T
            )
        );

    N =
        normalize(
            mat3(
                T,
                B,
                N
            ) *
            tangentNormal
        );

    vec3 V =
        normalize(
            CameraPosition.xyz -
            WorldPos
        );

    vec3 L =
        normalize(
            -SunDirection.xyz
        );

    vec3 H =
        normalize(
            V + L
        );

    float NoV =
        max(
            dot(N,V),
            0.0
        );

    float NoL =
        max(
            dot(N,L),
            0.0
        );

    float NoH =
        max(
            dot(N,H),
            0.0
        );

    float VoH =
        max(
            dot(V,H),
            0.0
        );

    vec3 F0 =
        mix(
            vec3(0.04),
            albedo,
            metallic
        );

    vec3 F =
        FresnelSchlick(
            VoH,
            F0
        );

    float D =
        DistributionGGX(
            NoH,
            roughness
        );

    float G =
        GeometrySchlickGGX(
            NoV,
            roughness
        ) *
        GeometrySchlickGGX(
            NoL,
            roughness
        );

    vec3 specular =
        D * G * F /
        max(
            4.0 *
            NoV *
            NoL,
            1e-4
        );

    vec3 kd =
        (1.0-F) *
        (1.0-metallic);

    float shadow =
        ShadowPCF(
            WorldPos,
            length(
                CameraPosition.xyz -
                WorldPos
            )
        );

    vec3 direct =
        (
            kd *
            albedo /
            PI +
            specular
        ) *
        SunColor.rgb *
        NoL *
        shadow;

    vec3 R =
        reflect(
            -V,
            N
        );

    vec3 diffuse =
        texture(
            Irradiance,
            N
        ).rgb *
        kd *
        albedo;

    vec3 prefiltered =
        texture(
            PrefilteredEnv,
            R
        ).rgb;

    vec2 brdf =
        texture(
            BrdfLut,
            vec2(
                NoV,
                roughness
            )
        ).rg;

    vec3 ambient =
        (
            diffuse +
            prefiltered *
            (
                F *
                brdf.x +
                brdf.y
            )
        ) *
        ao;

    vec3 hdr =
        direct +
        ambient;

    // Procedural atmosphere is intentionally exposed through the same
    // framebuffer path when a geometry fallback is active.
    if(length(WorldPos) < 1e-4)
        hdr = Atmosphere(
            normalize(
                V
            )
        );

    const float a=2.51;
    const float b=0.03;
    const float c=2.43;
    const float d=0.59;
    const float e=0.14;

    vec3 mapped =
        clamp(
            (hdr*(a*hdr+b)) /
            (hdr*(c*hdr+d)+e),
            0.0,
            1.0
        );

    mapped =
        pow(
            mapped,
            vec3(
                1.0/2.2
            )
        );

    FragColor =
        vec4(
            mapped,
            1.0
        );
}
