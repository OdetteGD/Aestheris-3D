#version 300 es
precision highp float;
in vec3 WorldPos;in vec3 Normal;in vec2 UV;layout(location=0)out vec4 OutColor;
uniform sampler2D BaseColor;uniform sampler2D MetallicRoughness;uniform samplerCube Irradiance;uniform samplerCube PrefilteredEnv;uniform sampler2D BrdfLut;
uniform vec3 CameraPosition;uniform vec3 SunDirection;uniform vec3 SunColor;uniform float MaxPrefilterMip;
const float PI=3.14159265359;
float D_GGX(float NoH,float r){float a=r*r,a2=a*a,f=NoH*NoH*(a2-1.0)+1.0;return a2/max(PI*f*f,1e-4);}
float V_Smith(float NoV,float NoL,float r){float k=(r+1.0)*(r+1.0)/8.0;return NoV/(NoV*(1.0-k)+k)*NoL/(NoL*(1.0-k)+k);}
vec3 F_Schlick(vec3 F0,float VoH){return F0+(1.0-F0)*pow(1.0-VoH,5.0);}
vec3 ACESFilm(vec3 x){
    const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);
}
void main(){vec3 albedo=pow(texture(BaseColor,UV).rgb,vec3(2.2));vec2 mr=texture(MetallicRoughness,UV).rg;float metallic=mr.x,r=max(mr.y,.045);vec3 n=normalize(Normal),v=normalize(CameraPosition-WorldPos),l=normalize(-SunDirection),h=normalize(v+l);float NoV=max(dot(n,v),0.0),NoL=max(dot(n,l),0.0),NoH=max(dot(n,h),0.0),VoH=max(dot(v,h),0.0);vec3 F0=mix(vec3(.04),albedo,metallic),F=F_Schlick(F0,VoH);vec3 spec=D_GGX(NoH,r)*V_Smith(NoV,NoL,r)*F;vec3 direct=((1.0-F)*(1.0-metallic)*albedo/PI+spec)*SunColor*NoL;vec3 refl=reflect(-v,n),diff=texture(Irradiance,n).rgb*albedo/PI,pre=textureLod(PrefilteredEnv,refl,r*MaxPrefilterMip).rgb;vec2 br=texture(BrdfLut,vec2(NoV,r)).rg;vec3 hdr = direct + (diff + pre * (F * br.x + br.y));
    vec3 ldr = ACESFilm(max(hdr, vec3(0.0)));
    ldr = pow(ldr, vec3(1.0 / 2.2));
    OutColor = vec4(ldr, 1.0);}
