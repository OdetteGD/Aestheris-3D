#version 450
layout(location=0)in vec2 UV;layout(location=0)out vec4 Out;layout(set=0,binding=0)uniform sampler2D Src;
void main(){vec2 t=1.0/vec2(textureSize(Src,0));vec3 c=texture(Src,UV).rgb+texture(Src,UV+vec2(t.x,0)).rgb+texture(Src,UV+vec2(0,t.y)).rgb+texture(Src,UV+t).rgb;Out=vec4(max(c*.25-1.0,0.0),1);}
