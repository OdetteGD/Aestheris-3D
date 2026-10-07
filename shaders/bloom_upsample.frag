#version 450
layout(location=0)in vec2 UV;layout(location=0)out vec4 OutColor;
layout(set=0,binding=0)uniform sampler2D LowRes;
layout(set=0,binding=1)uniform sampler2D HighRes;
void main(){vec2 t=1.0/vec2(textureSize(LowRes,0));vec3 l=vec3(0);l+=texture(LowRes,UV+vec2(-t.x,-t.y)).rgb;l+=texture(LowRes,UV+vec2(0,-t.y)).rgb;l+=texture(LowRes,UV+vec2(t.x,-t.y)).rgb;l+=texture(LowRes,UV+vec2(-t.x,0)).rgb;l+=texture(LowRes,UV).rgb;l+=texture(LowRes,UV+vec2(t.x,0)).rgb;l+=texture(LowRes,UV+vec2(-t.x,t.y)).rgb;l+=texture(LowRes,UV+vec2(0,t.y)).rgb;l+=texture(LowRes,UV+vec2(t.x,t.y)).rgb;l/=9.0;OutColor=vec4(texture(HighRes,UV).rgb+l,1.0);}
