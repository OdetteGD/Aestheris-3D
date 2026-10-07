#version 450
layout(location=0)in vec3 Direction;layout(location=0)out vec4 OutColor;
layout(set=0,binding=0)uniform samplerCube Environment;layout(push_constant)uniform Params{float Roughness;float Resolution;}P;
const float PI=3.14159265359;
float D_GGX(float NoH,float r){float a=r*r,a2=a*a,f=NoH*NoH*(a2-1.0)+1.0;return a2/max(PI*f*f,1e-6);}
void main(){vec3 n=normalize(Direction),v=n,sum=vec3(0);float wsum=0;const uint SAMPLE_COUNT=64u;for(uint i=0u;i<SAMPLE_COUNT;++i){float x=float(i)/float(SAMPLE_COUNT);float phi=6.2831853*x;float cosTheta=sqrt((1.0-x)/(1.0+(P.Roughness*P.Roughness-1.0)*x));float sinTheta=sqrt(max(0.0,1.0-cosTheta*cosTheta));vec3 h=normalize(vec3(cos(phi)*sinTheta,sin(phi)*sinTheta,cosTheta));vec3 l=normalize(2.0*dot(v,h)*h-v);float NoL=max(dot(n,l),0.0);if(NoL>0.0){float NoH=max(dot(n,h),0.0),VoH=max(dot(v,h),0.0),pdf=max(D_GGX(NoH,P.Roughness)*NoH/(4.0*VoH),1e-5);float sa=1.0/(float(SAMPLE_COUNT)*pdf),mip=0.5*log2(max((sa*P.Resolution*P.Resolution)/(4.0*PI),1e-4));sum+=textureLod(Environment,l,mip).rgb*NoL;wsum+=NoL;}}OutColor=vec4(sum/max(wsum,1e-4),1);}
