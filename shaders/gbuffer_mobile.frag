#version 450
layout(location=0) in vec3 WorldPos;
layout(location=1) in vec3 Normal;
layout(location=2) in vec2 UV;
layout(location=3) in vec4 BaseColorMetallic;
layout(location=4) in vec2 RoughnessAO;
layout(location=0) out vec4 OutPosition;
layout(location=1) out vec4 OutNormal;
layout(location=2) out vec4 OutAlbedo;
void main(){vec3 n=normalize(Normal);OutPosition=vec4(WorldPos,clamp(BaseColorMetallic.a,0.0,1.0));OutNormal=vec4(n*0.5+0.5,RoughnessAO.x);OutAlbedo=vec4(BaseColorMetallic.rgb,RoughnessAO.y);}
