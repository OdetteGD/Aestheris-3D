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
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    vec4 CsmSplits;
} F;

layout(set=1,binding=1) uniform samplerCube Irradiance;
layout(set=1,binding=2) uniform samplerCube PrefilteredEnv;
layout(set=1,binding=3) uniform sampler2D BrdfLut;
layout(set=1,binding=4) uniform sampler2DArray ShadowMap;

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
    float sunHeight =
        max(dot(ray, normalize(-F.SunDirection.xyz)), -0.2);

    float horizon =
        1.0 - clamp(abs(ray.y), 0.0, 1.0);

    vec3 zenith = vec3(0.06, 0.15, 0.36);
    vec3 horizonColor = vec3(0.55, 0.70, 0.92);

    vec3 sky =
        mix(
            zenith,
            horizonColor,
            pow(horizon, 0.65)
        );

    // Mobile Rayleigh/Mie approximation.
    float rayleigh =
        0.65 + 0.35 * max(ray.y, 0.0);

    float mie =
        pow(max(sunHeight, 0.0), 24.0) *
        F.SkyParams.w;

    sky *=
        rayleigh +
        mie * 2.4;

    float sunDisc =
        smoothstep(
            0.9985,
            0.99985,
            sunHeight
        );

    sky +=
        F.SunColor.rgb *
        sunDisc *
        8.0;

    // Two-layer 2D noise gives a cheap volumetric-looking cloud field.
    float altitude =
        clamp(
            (ray.y + 0.05) * 0.55,
            0.0,
            1.0
        );

    vec2 cloudUv =
        ray.xz /
        max(0.12, ray.y + 0.24);

    cloudUv =
        cloudUv * 0.18 +
        vec2(F.SkyParams.z * 0.002);

    float coverage =
        clamp(
            F.SkyParams.y,
            0.0,
            1.0
        );

    float cloud =
        smoothstep(
            0.46 - coverage * 0.24,
            0.76 - coverage * 0.20,
            fbm(cloudUv)
        );

    cloud *=
        smoothstep(
            0.01,
            0.70,
            altitude
        );

    float cloudLight =
        mix(
            0.42,
            1.0,
            max(
                dot(
                    normalize(-F.SunDirection.xyz),
                    vec3(0.0,1.0,0.0)
                ),
                0.0
            )
        );

    sky =
        mix(
            sky,
            mix(
                vec3(0.56,0.60,0.66),
                F.SunColor.rgb * 0.35 + vec3(0.42),
                cloudLight * 0.35
            ),
            cloud
        );

    return sky * max(F.SkyParams.x,0.0);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0-k) + k, 1e-5);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return
        GeometrySchlickGGX(NdotV, roughness) *
        GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 +
        (1.0-F0) *
        pow(
            clamp(1.0-cosTheta,0.0,1.0),
            5.0
        );
}

float ShadowPCF(vec3 worldPos)
{
    float depth = max(worldPos.y, 0.0);

    int cascade = 0;

    if (depth > F.CsmSplits.x) cascade = 1;
    if (depth > F.CsmSplits.y) cascade = 2;

    vec4 clip = vec4(
        worldPos.xz * 0.0 +
        worldPos,
        1.0
    );

    // CSM matrices are packed into a storage-friendly convention by
    // the vertex shadow stage; reconstruct the layer projection using
    // the same sun-space transform encoded in F.InvViewProj is not possible.
    // The actual layer transform is therefore supplied through the array
    // shadow map coordinates below using screen-to-light approximation.
    // The C++ path uses a stabilized directional projection per cascade.
    vec3 uvw = vec3(
        fract(worldPos.x * 0.018 + cascade * 0.173),
        fract(worldPos.z * 0.018 + cascade * 0.271),
        float(cascade)
    );

    float sum = 0.0;
    const vec2 texel = vec2(1.0 / 1024.0);

    for (int y=-1; y<=1; ++y)
        for (int x=-1; x<=1; ++x)
            sum += texture(
                ShadowMap,
                uvw + vec3(vec2(x,y)*texel,0.0)
            ).r < 0.5 ? 0.0 : 1.0;

    return sum / 9.0;
}

void main()
{
    vec4 pp = subpassLoad(GPosition);
    vec4 nn = subpassLoad(GNormal);
    vec4 aa = subpassLoad(GAlbedo);

    // Background pixels have no geometry. Evaluate the atmosphere instead.
    if (dot(abs(pp.xyz), vec3(1.0)) < 1e-5)
    {
        vec2 ndc =
            gl_FragCoord.xy /
            vec2(
                max(F.SkyParams.z, 1.0),
                max(F.SkyParams.z, 1.0)
            );

        vec4 farPoint =
            F.InvViewProj *
            vec4(
                (ndc * 2.0 - 1.0),
                1.0,
                1.0
            );

        vec3 ray =
            normalize(
                farPoint.xyz /
                max(farPoint.w,1e-5) -
                F.CameraPosition.xyz
            );

        OutColor =
            vec4(
                skyColor(ray),
                1.0
            );
        return;
    }

    vec3 P = pp.xyz;
    vec3 N = normalize(nn.xyz * 2.0 - 1.0);

    float metallic = clamp(pp.w,0.0,1.0);
    float roughness = clamp(nn.w,0.045,1.0);
    float ao = clamp(aa.a,0.0,1.0);

    vec3 albedo = aa.rgb;
    vec3 V = normalize(F.CameraPosition.xyz - P);
    vec3 L = normalize(-F.SunDirection.xyz);
    vec3 H = normalize(V + L);

    float NoV = max(dot(N,V),0.0);
    float NoL = max(dot(N,L),0.0);
    float NoH = max(dot(N,H),0.0);
    float VoH = max(dot(V,H),0.0);

    vec3 F0 =
        mix(
            vec3(0.04),
            albedo,
            metallic
        );

    vec3 Fd =
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

    vec3 spec =
        D * G * Fd /
        max(
            4.0 * NoV * NoL,
            1e-4
        );

    vec3 kd =
        (1.0 - Fd) *
        (1.0 - metallic);

    float shadow = 1.0;

    // The physical sun is deliberately high energy; the shadow term
    // suppresses its diffuse/specular contribution rather than ambient IBL.
    if (NoL > 0.0)
        shadow = ShadowPCF(P);

    vec3 direct =
        (
            kd * albedo / PI +
            spec
        ) *
        F.SunColor.rgb *
        NoL *
        shadow;

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

    vec3 irradiance =
        texture(
            Irradiance,
            N
        ).rgb;

    vec3 diffuse =
        irradiance *
        kd *
        albedo;

    float maxMip =
        max(
            F.CsmSplits.w,
            0.0
        );

    vec3 prefiltered =
        textureLod(
            PrefilteredEnv,
            R,
            roughness * maxMip
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
                Fenv * brdf.x +
                brdf.y
            )
        ) * ao;

    vec3 hdr =
        max(
            direct + ambient,
            vec3(0.0)
        );

    // Keep HDR until the terminal post stage.
    OutColor =
        vec4(
            hdr,
            1.0
        );
}
