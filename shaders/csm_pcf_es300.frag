#version 300 es
precision highp float;
in vec3 WorldPos;in vec4 ShadowCoord0;in vec4 ShadowCoord1;in vec4 ShadowCoord2;in vec4 ShadowCoord3;layout(location=0)out vec4 OutColor;
uniform sampler2DShadow Shadow0;uniform sampler2DShadow Shadow1;uniform sampler2DShadow Shadow2;uniform sampler2DShadow Shadow3;uniform vec4 CascadeSplits;uniform float InvShadowSize;
float pcf(sampler2DShadow s,vec3 q){float r=0.;for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)r+=texture(s,q+vec3(float(x),float(y),0.)*InvShadowSize);return r/9.;}
void main(){float d=abs(WorldPos.z);int c=d<CascadeSplits.x?0:d<CascadeSplits.y?1:d<CascadeSplits.z?2:3;vec3 q=c==0?ShadowCoord0.xyz/ShadowCoord0.w:c==1?ShadowCoord1.xyz/ShadowCoord1.w:c==2?ShadowCoord2.xyz/ShadowCoord2.w:ShadowCoord3.xyz/ShadowCoord3.w;q=q*.5+.5;float s=c==0?pcf(Shadow0,q):c==1?pcf(Shadow1,q):c==2?pcf(Shadow2,q):pcf(Shadow3,q);OutColor=vec4(vec3(s),1.);}
