#version 450
layout(location=0)in vec3 WorldPos;layout(location=1)in vec3 Normal;layout(location=0)out vec4 Out;
layout(set=0,binding=0)uniform sampler2DArrayShadow ShadowAtlas;
layout(set=0,binding=1,std140)uniform Cascades{mat4 LightVP[4];vec4 Splits;float InvShadowSize;}C;
float PCF(vec3 p,int layer){float s=0;vec2 d=vec2(C.InvShadowSize);for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)s+=texture(ShadowAtlas,vec4(p.xy+vec2(x,y)*d,p.z,layer));return s/9.0;}
void main(){float depth=abs(WorldPos.z);int c=depth<C.Splits.x?0:depth<C.Splits.y?1:depth<C.Splits.z?2:3;vec4 q=C.LightVP[c]*vec4(WorldPos,1);q.xyz=q.xyz/q.w*.5+.5;float shadow=PCF(q.xyz,c);Out=vec4(vec3(shadow),1);}
