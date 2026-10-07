#version 450
layout(location=0)in vec2 UV;layout(location=0)out vec2 OutLut;
const float PI=3.14159265359;
float G_Smith(float NoV,float NoL,float r){float k=(r+1.0)*(r+1.0)/8.0;return NoV/(NoV*(1.0-k)+k)*NoL/(NoL*(1.0-k)+k);}
vec2 IntegrateBRDF(float NoV,float r){vec3 v=vec3(sqrt(max(0.0,1.0-NoV*NoV)),0,NoV);float A=0,B=0;const uint N=64u;for(uint i=0u;i<N;++i){float x=float(i)/float(N),phi=6.2831853*x;float c=sqrt((1.0-x)/(1.0+(r*r-1.0)*x)),s=sqrt(max(0.0,1.0-c*c));vec3 h=vec3(cos(phi)*s,sin(phi)*s,c);vec3 l=normalize(2.0*dot(v,h)*h-v);float NoL=max(l.z,0.0),NoH=max(h.z,0.0),VoH=max(dot(v,h),0.0);if(NoL>0.0){float G=G_Smith(NoV,NoL,r),Fc=pow(1.0-VoH,5.0),Gv=G*VoH/max(NoH*NoV,1e-5);A+=(1.0-Fc)*Gv;B+=Fc*Gv;}}return vec2(A,B)/float(N);}
void main(){OutLut=IntegrateBRDF(UV.x,UV.y);}
