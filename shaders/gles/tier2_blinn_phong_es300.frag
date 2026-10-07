#version 300 es
precision highp float;

in vec3 WorldPos;
in vec3 WorldNormal;
in vec2 UV;
in vec4 Material;
in vec2 RoughnessAO;

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

void main(){
    vec3 albedo=texture(uAlbedo,UV).rgb*Material.rgb;
    vec3 N=normalize(WorldNormal);
    vec3 tangentNormal=texture(uNormal,UV).xyz*2.0-1.0;
    vec3 T=dFdx(WorldPos)*dFdy(UV).y-dFdy(WorldPos)*dFdx(UV).y;
    T=normalize(T-N*dot(N,T));
    if(length(T)<0.001) T=normalize(cross(abs(N.y)<0.9?vec3(0,1,0):vec3(1,0,0),N));
    vec3 B=normalize(cross(N,T));
    N=normalize(mat3(T,B,N)*tangentNormal);

    vec3 L=normalize(-SunDirection.xyz);
    vec3 V=normalize(CameraPosition.xyz-WorldPos);
    vec3 H=normalize(L+V);

    float NoL=max(dot(N,L),0.0);
    float NoV=max(dot(N,V),0.0);
    float NoH=max(dot(N,H),0.0);
    float spec=pow(NoH,mix(16.0,64.0,1.0-clamp(RoughnessAO.x,0.0,1.0))) * NoL * mix(0.04,0.35,Material.a);

    vec3 ambient=albedo*(0.12+0.28*max(N.y,0.0))*RoughnessAO.y;
    vec3 lighting=ambient+albedo*SunColor.rgb*(0.92*NoL)+SunColor.rgb*spec*NoV;

    const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14;
    vec3 mapped=clamp((lighting*(a*lighting+b))/(lighting*(c*lighting+d)+e),0.0,1.0);
    FragColor=vec4(pow(mapped,vec3(1.0/2.2)),1.0);
}
