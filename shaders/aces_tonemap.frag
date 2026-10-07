#version 450
layout(location=0)in vec2 UV;layout(location=0)out vec4 O;layout(set=0,binding=0)uniform sampler2D HDR;
vec3 aces(vec3 x){const float a=2.51,b=.03,c=2.43,d=.59,e=.14;return clamp((x*(a*x+b))/(x*(c*x+d)+e),0,1);}
void main(){O=vec4(pow(aces(texture(HDR,UV).rgb),vec3(1.0/2.2)),1);}
