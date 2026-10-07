#version 300 es
precision highp float;

in vec3 WorldPos;
in vec3 WorldNormal;
in vec2 UV;
in vec4 Material;
in vec2 RoughnessAO;
in vec4 ShadowCoord;

layout(location=0) out vec4 FragColor;

layout(std140,binding=0) uniform FrameBlock {
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
};

uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uOrm;
uniform sampler2DShadow uShadow;

const float PI=3.14159265359;

float ggxD(float nH,float r){
    float a=max(r,0.045),a2=a*a;
    float d=nH*nH*(a2-1.0)+1.0;
    return a2/max(PI*d*d,1e-5);
}
float smith(float nV,float r){
    float k=((r+1.0)*(r+1.0))/8.0;
    return nV/max(nV*(1.0-k)+k,1e-5);
}
vec3 fresnel(float vH,vec3 f0){
    return f0+(1.0-f0)*pow(clamp(1.0-vH,0.0,1.0),5.0);
}
float pcf(vec3 sc){
    vec3 q=sc.xyz/max(sc.w,1e-5);
    q=q*0.5+0.5;
    if(q.x<=0.0||q.x>=1.0||q.y<=0.0||q.y>=1.0||q.z>=1.0)return 1.0;
    float r=0.0;
    vec2 texel=vec2(1.0/1024.0);
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)
        r+=texture(uShadow,vec3(q.xy+vec2(x,y)*texel,q.z-0.0015));
    return r/9.0;
}

void main(){
    vec3 albedo=texture(uAlbedo,UV).rgb*Material.rgb;
    vec3 orm=texture(uOrm,UV).rgb;

    vec3 N=normalize(WorldNormal);
    vec3 tangentN=texture(uNormal,UV).xyz*2.0-1.0;
    vec3 T=normalize(dFdx(WorldPos)*dFdy(UV).y-dFdy(WorldPos)*dFdx(UV).y);
    if(length(T)<0.001)T=normalize(cross(abs(N.y)<0.9?vec3(0,1,0):vec3(1,0,0),N));
    T=normalize(T-N*dot(N,T));
    vec3 B=normalize(cross(N,T));
    N=normalize(mat3(T,B,N)*tangentN);

    float metallic=clamp(Material.a+orm.g*0.05,0.0,1.0);
    float roughness=clamp(RoughnessAO.x*max(orm.r,0.35),0.045,1.0);
    float ao=clamp(RoughnessAO.y*orm.b,0.0,1.0);

    vec3 V=normalize(CameraPosition.xyz-WorldPos);
    vec3 L=normalize(-SunDirection.xyz);
    vec3 H=normalize(V+L);

    float nV=max(dot(N,V),0.0),nL=max(dot(N,L),0.0);
    float nH=max(dot(N,H),0.0),vH=max(dot(V,H),0.0);

    vec3 f0=mix(vec3(0.04),albedo,metallic);
    vec3 F=fresnel(vH,f0);
    float D=ggxD(nH,roughness);
    float G=smith(nV,roughness)*smith(nL,roughness);
    vec3 spec=(D*G*F)/max(4.0*nV*nL,1e-4);
    vec3 kd=(1.0-F)*(1.0-metallic);

    float shadow=pcf(ShadowCoord);
    vec3 direct=(kd*albedo/PI+spec)*SunColor.rgb*nL*shadow;
    vec3 ambient=albedo*vec3(0.06,0.10,0.18)*(0.5+0.5*max(N.y,0.0))*kd*ao;
    vec3 hdr=direct+ambient;

    const float A=2.51,Bc=0.03,C=2.43,Dc=0.59,E=0.14;
    vec3 mapped=clamp((hdr*(A*hdr+Bc))/(hdr*(C*hdr+Dc)+E),0.0,1.0);
    FragColor=vec4(pow(mapped,vec3(1.0/2.2)),1.0);
}
