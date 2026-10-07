#version 450
layout(location=0)in vec3 P;layout(location=1)in vec3 N;layout(location=2)in vec2 UV;layout(location=0)out vec4 O;
layout(set=2,binding=0)uniform sampler2D Base;layout(set=2,binding=1)uniform sampler2D MR;layout(set=2,binding=2)uniform sampler2D AO;layout(set=2,binding=3)uniform samplerCube Irr;layout(set=2,binding=4)uniform samplerCube Pref;layout(set=2,binding=5)uniform sampler2D BRDF;
layout(set=3,binding=0,std140)uniform Light{vec4 Camera;vec4 SunDir;vec4 SunColor;float MaxMip;}L;
const float PI=3.14159265359;
float Dggx(float n,float r){float a=r*r,a2=a*a,f=n*n*(a2-1)+1;return a2/max(PI*f*f,1e-5);}
float Vsmith(float nv,float nl,float r){float k=(r+1)*(r+1)/8;return nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);}
vec3 Fschlick(vec3 f0,float v){return f0+(1-f0)*pow(1-v,5);}
void main(){vec3 al=pow(texture(Base,UV).rgb,vec3(2.2));vec2 mr=texture(MR,UV).bg;float m=mr.x,r=max(mr.y,.045),ao=texture(AO,UV).r;vec3 n=normalize(N),v=normalize(L.Camera.xyz-P),l=normalize(-L.SunDir.xyz),h=normalize(v+l);float nv=max(dot(n,v),0),nl=max(dot(n,l),0),nh=max(dot(n,h),0),vh=max(dot(v,h),0);vec3 f0=mix(vec3(.04),al,m),F=Fschlick(f0,vh);vec3 spec=Dggx(nh,r)*Vsmith(nv,nl,r)*F;vec3 direct=((1-F)*(1-m)*al/PI+spec)*L.SunColor.rgb*nl;vec3 R=reflect(-v,n);vec3 diff=texture(Irr,n).rgb*al/PI;vec3 pre=textureLod(Pref,R,r*L.MaxMip).rgb;vec2 br=texture(BRDF,vec2(nv,r)).rg;O=vec4(direct+(diff+pre*(F*br.x+br.y))*ao,1);}
