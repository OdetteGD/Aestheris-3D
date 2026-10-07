#version 300 es
precision highp float;

in vec3 RayDirection;
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

float hash2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123);}
float noise2(vec2 p){
    vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash2(i),hash2(i+vec2(1,0)),f.x),mix(hash2(i+vec2(0,1)),hash2(i+vec2(1,1)),f.x),f.y);
}
float fbm(vec2 p){
    float v=0.0,a=0.5;
    for(int i=0;i<5;i++){v+=noise2(p)*a;p=p*2.03+vec2(17,11);a*=0.5;}
    return v;
}

void main(){
    vec3 ray=normalize(RayDirection);
    vec3 sun=normalize(-SunDirection.xyz);
    float horizon=pow(clamp(1.0-abs(ray.y),0.0,1.0),0.72);
    float day=clamp(0.35+0.65*(sun.y*0.5+0.5),0.0,1.0);
    vec3 zenith=mix(vec3(0.015,0.028,0.075),vec3(0.17,0.38,0.72),day);
    vec3 horizonColor=mix(vec3(0.12,0.17,0.23),vec3(0.64,0.77,0.93),day);
    vec3 sky=mix(zenith,horizonColor,horizon);

    float sunDot=max(dot(ray,sun),0.0);
    sky+=SunColor.rgb*(pow(sunDot,160.0)*1.8+pow(sunDot,12.0)*0.16);

    float h=max(ray.y+0.18,0.18);
    vec2 uv=ray.xz/h;
    float t=SkyParams.z;
    float a=fbm(uv*0.115+vec2(t*0.012,t*0.006));
    float b=fbm(uv*0.245+vec2(-t*0.019,t*0.009)+37.0);
    float coverage=clamp(SkyParams.y,0.15,1.0);
    float clouds=smoothstep(0.48-coverage*0.14,0.72-coverage*0.10,mix(a,b,0.42));
    sky=mix(sky,vec3(0.72,0.75,0.80)*(0.55+0.45*sunDot),clouds*0.78);
    FragColor=vec4(max(sky,vec3(0)),1.0);
}
