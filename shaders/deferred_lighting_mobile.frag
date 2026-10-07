#version 450
layout(input_attachment_index=0,set=0,binding=0)uniform subpassInput GPosition;
layout(input_attachment_index=1,set=0,binding=1)uniform subpassInput GNormal;
layout(input_attachment_index=2,set=0,binding=2)uniform subpassInput GAlbedo;
layout(location=0)out vec4 OutColor;
layout(set=1,binding=0,std140)uniform Frame{vec4 CameraPosition;vec4 SunDirection;vec4 SunColor;float MaxPrefilterMip;}F;
layout(set=1,binding=1)uniform samplerCube Irradiance;
layout(set=1,binding=2)uniform samplerCube PrefilteredEnv;
layout(set=1,binding=3)uniform sampler2D BrdfLut;
const float PI=3.14159265359;
float D_GGX(float NoH,float r){float a=r*r,a2=a*a,f=NoH*NoH*(a2-1.0)+1.0;return a2/max(PI*f*f,1e-5);}
float V_Smith(float NoV,float NoL,float r){float k=(r+1.0)*(r+1.0)/8.0;return NoV/(NoV*(1.0-k)+k)*NoL/(NoL*(1.0-k)+k);}
vec3 F_Schlick(vec3 F0,float VoH){return F0+(1.0-F0)*pow(1.0-VoH,5.0);}
vec3 ACESFilm(vec3 x){
    const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);
}
void main(){vec3 p=subpassLoad(GPosition).xyz;vec4 nn=subpassLoad(GNormal);vec4 aa=subpassLoad(GAlbedo);vec3 n=normalize(nn.xyz*2.0-1.0);float r=clamp(nn.w,.045,1.0),ao=clamp(aa.a,0.0,1.0);vec3 albedo=aa.rgb,v=normalize(F.CameraPosition.xyz-p),l=normalize(-F.SunDirection.xyz),h=normalize(v+l);float NoV=max(dot(n,v),0.0),NoL=max(dot(n,l),0.0),NoH=max(dot(n,h),0.0),VoH=max(dot(v,h),0.0);vec3 F0=vec3(.04),Fc=F_Schlick(F0,VoH);vec3 spec=D_GGX(NoH,r)*V_Smith(NoV,NoL,r)*Fc;vec3 direct=((1.0-Fc)*albedo/PI+spec)*F.SunColor.rgb*NoL;vec3 refl=reflect(-v,n);vec3 diff=texture(Irradiance,n).rgb*albedo/PI;vec3 pre=textureLod(PrefilteredEnv,refl,r*F.MaxPrefilterMip).rgb;vec2 br=texture(BrdfLut,vec2(NoV,r)).rg;vec3 hdr = direct + (diff + pre * (Fc * br.x + br.y)) * ao;
    vec3 ldr = ACESFilm(max(hdr, vec3(0.0)));
    ldr = pow(ldr, vec3(1.0 / 2.2));
    OutColor = vec4(ldr, 1.0);}
