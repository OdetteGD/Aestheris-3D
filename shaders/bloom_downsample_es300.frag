#version 300 es
precision mediump float;
in vec2 UV;layout(location=0)out vec4 OutColor;uniform sampler2D Src;uniform float Threshold;
void main(){vec2 t=1.0/vec2(textureSize(Src,0));vec3 c=texture(Src,UV).rgb+texture(Src,UV+vec2(t.x,0)).rgb+texture(Src,UV+vec2(0,t.y)).rgb+texture(Src,UV+t).rgb;c*=.25;OutColor=vec4(max(c-vec3(Threshold),vec3(0)),1);}
