#version 450
layout(input_attachment_index=0,set=0,binding=0) uniform subpassInput GPosition;
layout(input_attachment_index=1,set=0,binding=1) uniform subpassInput GNormal;
layout(input_attachment_index=2,set=0,binding=2) uniform subpassInput GAlbedo;

layout(location=0) out vec4 OutColor;

layout(set=1,binding=0,std140) uniform Frame {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;    // xyz = right, w = viewport width
    vec4 CameraUp;       // xyz = up,    w = viewport height
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;      // xyz = cascade distances, w = prefilter mip
} F;

layout(set=1,binding=1) uniform samplerCube Irradiance;
layout(set=1,binding=2) uniform samplerCube PrefilteredEnv;
layout(set=1,binding=3) uniform sampler2D BrdfLut;
layout(set=1,binding=4) uniform sampler2DArrayShadow ShadowMap;
layout(set=1,binding=5) uniform samplerCube EnvironmentCube;
layout(set=1,binding=6) uniform sampler2D SSAOMap;

const float PI = 3.14159265359;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise2(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f*f*(3.0-2.0*f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0,0.0));
    float c = hash21(i + vec2(0.0,1.0));
    float d = hash21(i + vec2(1.0,1.0));

    return mix(
        mix(a,b,f.x),
        mix(c,d,f.x),
        f.y
    );
}

float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;

    for (int i=0; i<4; ++i) {
        v += noise2(p) * a;
        p = p * 2.03 + 17.7;
        a *= 0.5;
    }

    return v;
}

vec3 skyColor(vec3 ray)
{
    vec3 sunDir =
        normalize(
            -F.SunDirection.xyz
        );

    float sunHeight =
        dot(
            ray,
            sunDir
        );

    float horizon =
        1.0 -
        clamp(
            abs(ray.y),
            0.0,
            1.0
        );

    vec3 zenith =
        vec3(
            0.035,
            0.085,
            0.22
        );

    vec3 horizonColor =
        vec3(
            0.48,
            0.68,
            0.95
        );

    vec3 sky =
        mix(
            zenith,
            horizonColor,
            pow(
                horizon,
                0.72
            )
        );

    float rayleigh =
        0.72 +
        0.28 *
        max(
            ray.y,
            0.0
        );

    float mie =
        pow(
            max(
                sunHeight,
                0.0
            ),
            18.0
        ) *
        F.SkyParams.w;

    sky *=
        rayleigh +
        mie *
        1.8;

    float sunDisc =
        smoothstep(
            0.999,
            0.99985,
            sunHeight
        );

    sky +=
        F.SunColor.rgb *
        sunDisc *
        10.0;

    // Cheap volumetric-looking cloud layer: 4-octave FBM with
    // view-dependent height attenuation and a moving wind phase.
    float coverage =
        clamp(
            F.SkyParams.y,
            0.0,
            1.0
        );

    float rayHeight =
        clamp(
            ray.y,
            0.05,
            1.0
        );

    vec2 cloudUv =
        ray.xz /
        rayHeight *
        0.105;

    cloudUv +=
        vec2(
            F.SkyParams.z *
            0.002,
            F.SkyParams.z *
            0.0007
        );

    float cloud =
        smoothstep(
            0.47 -
                coverage *
                0.22,
            0.74 -
                coverage *
                0.18,
            fbm(cloudUv)
        );

    cloud *=
        smoothstep(
            0.055,
            0.35,
            rayHeight
        );

    float cloudSun =
        0.35 +
        0.65 *
        max(
            dot(
                vec3(0.0,1.0,0.0),
                sunDir
            ),
            0.0
        );

    vec3 cloudColor =
        mix(
            vec3(
                0.36,
                0.39,
                0.44
            ),
            vec3(
                1.0,
                0.88,
                0.72
            ),
            cloudSun *
            0.55
        );

    sky =
        mix(
            sky,
            cloudColor,
            cloud *
            0.82
        );

    return sky *
        F.SkyParams.x;
}

float DistributionGGX(
    float NdotH,
    float roughness)
{
    float a =
        roughness *
        roughness;

    float a2 =
        a *
        a;

    float d =
        NdotH *
        NdotH *
        (a2 - 1.0) +
        1.0;

    return
        a2 /
        max(
            PI *
            d *
            d,
            1e-5
        );
}

float GeometrySchlickGGX(
    float NdotV,
    float roughness)
{
    float r =
        roughness +
        1.0;

    float k =
        (r*r) /
        8.0;

    return
        NdotV /
        max(
            NdotV *
            (1.0-k) +
            k,
            1e-5
        );
}

float GeometrySmith(
    float NdotV,
    float NdotL,
    float roughness)
{
    return
        GeometrySchlickGGX(
            NdotV,
            roughness
        ) *
        GeometrySchlickGGX(
            NdotL,
            roughness
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

float ShadowCascade(
    vec3 worldPos,
    int cascade)
{
    vec4 lightClip =
        F.CsmMatrices[cascade] *
        vec4(
            worldPos,
            1.0
        );

    if (lightClip.w <= 0.0)
        return 1.0;

    vec3 shadowCoord =
        lightClip.xyz /
        lightClip.w;

    vec2 uv =
        shadowCoord.xy *
        0.5 +
        0.5;

    float depth =
        shadowCoord.z;

    if (uv.x < 0.0 ||
        uv.x > 1.0 ||
        uv.y < 0.0 ||
        uv.y > 1.0 ||
        depth < 0.0 ||
        depth > 1.0) {
        return 1.0;
    }

    const float bias =
        0.0018;

    const vec2 texel =
        vec2(
            1.0 / 1024.0
        );

    float visibility = 0.0;

    for (int y=-1; y<=1; ++y) {
        for (int x=-1; x<=1; ++x) {
            vec2 offset =
                vec2(
                    float(x),
                    float(y)
                ) *
                texel;

            visibility +=
                texture(
                    ShadowMap,
                    vec4(
                        uv + offset,
                        float(cascade),
                        depth - bias
                    )
                );
        }
    }

    return
        visibility /
        9.0;
}

float ShadowPCF(
    vec3 worldPos)
{
    float viewDistance =
        length(
            worldPos -
            F.CameraPosition.xyz
        );

    int cascade = 0;

    if (viewDistance >
        F.CsmSplits.x) {
        cascade = 1;
    }

    if (viewDistance >
        F.CsmSplits.y) {
        cascade = 2;
    }

    float shadow =
        ShadowCascade(
            worldPos,
            cascade
        );

    // Small overlap between cascades removes visible split lines.
    float splitStart =
        cascade == 0
            ? F.CsmSplits.x * 0.90
            : cascade == 1
                ? F.CsmSplits.y * 0.90
                : F.CsmSplits.z * 0.90;

    float splitEnd =
        cascade == 0
            ? F.CsmSplits.x
            : cascade == 1
                ? F.CsmSplits.y
                : F.CsmSplits.z;

    if (cascade < 2 &&
        viewDistance > splitStart) {
        float nextShadow =
            ShadowCascade(
                worldPos,
                cascade + 1
            );

        float blend =
            smoothstep(
                splitStart,
                splitEnd,
                viewDistance
            );

        shadow =
            mix(
                shadow,
                nextShadow,
                blend
            );
    }

    return shadow;
}

void main()
{
    vec4 pp =
        subpassLoad(
            GPosition
        );

    vec4 nn =
        subpassLoad(
            GNormal
        );

    vec4 aa =
        subpassLoad(
            GAlbedo
        );

    if (dot(abs(pp.xyz),vec3(1.0)) < 1e-5)
    {
        vec2 uv =
            gl_FragCoord.xy /
            vec2(
                max(F.CameraRight.w,1.0),
                max(F.CameraUp.w,1.0)
            );

        vec4 farPoint =
            F.InvViewProj *
            vec4(
                uv * 2.0 - 1.0,
                1.0,
                1.0
            );

        vec3 ray =
            normalize(
                farPoint.xyz /
                max(
                    farPoint.w,
                    1e-5
                ) -
                F.CameraPosition.xyz
            );

        vec3 hdrSky =
            textureLod(
                EnvironmentCube,
                ray,
                0.0
            ).rgb;

        // Offline HDR environment is the canonical background. Procedural
        // atmosphere is retained only as an emergency fallback when the
        // sampled environment is effectively black.
        if (dot(hdrSky,hdrSky) < 1e-6)
            hdrSky =
                skyColor(ray);

        OutColor =
            vec4(
                hdrSky *
                F.SkyParams.x,
                1.0
            );
        return;
    }

    vec3 P = pp.xyz;
    vec3 N =
        normalize(
            nn.xyz * 2.0 -
            1.0
        );

    float metallic =
        clamp(
            pp.w,
            0.0,
            1.0
        );

    float roughness =
        clamp(
            nn.w,
            0.045,
            1.0
        );

    float ao =
        clamp(
            aa.a,
            0.0,
            1.0
        );

    vec3 albedo =
        max(
            aa.rgb,
            vec3(0.003)
        );

    vec3 V =
        normalize(
            F.CameraPosition.xyz -
            P
        );

    vec3 L =
        normalize(
            -F.SunDirection.xyz
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

    vec3 Fdirect =
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
        GeometrySmith(
            NoV,
            NoL,
            roughness
        );

    vec3 specular =
        D *
        G *
        Fdirect /
        max(
            4.0 *
            NoV *
            NoL,
            1e-4
        );

    vec3 kd =
        (
            1.0 -
            Fdirect
        ) *
        (
            1.0 -
            metallic
        );

    float sunShadow =
        NoL > 0.0
            ? ShadowPCF(P)
            : 1.0;

    vec3 direct =
        (
            kd *
            albedo /
            PI +
            specular
        ) *
        F.SunColor.rgb *
        NoL *
        sunShadow;

    vec3 R =
        reflect(
            -V,
            N
        );

    vec3 Fenv =
        FresnelSchlick(
            NoV,
            F0
        );

    vec3 diffuseIBL =
        texture(
            Irradiance,
            N
        ).rgb *
        kd *
        albedo;

    vec3 prefiltered =
        textureLod(
            PrefilteredEnv,
            R,
            roughness *
            F.CsmSplits.w
        ).rgb;

    vec2 brdf =
        texture(
            BrdfLut,
            vec2(
                NoV,
                roughness
            )
        ).rg;

    float ssao =
        clamp(
            texture(
                SSAOMap,
                gl_FragCoord.xy /
                vec2(
                    max(F.CameraRight.w,1.0),
                    max(F.CameraUp.w,1.0)
                )
            ).r,
            0.12,
            1.0
        );

    vec3 ambient =
        (
            diffuseIBL +
            prefiltered *
            (
                Fenv *
                brdf.x +
                brdf.y
            )
        ) *
        ao *
        ssao;

    OutColor =
        vec4(
            max(
                direct +
                ambient,
                vec3(0.0)
            ),
            1.0
        );
}
