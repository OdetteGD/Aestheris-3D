#version 450
layout(location=0)in vec3 Direction;layout(location=0)out vec4 OutColor;
layout(set=0,binding=0)uniform samplerCube Environment;
const float PI=3.14159265359;
void main(){vec3 n=normalize(Direction),up=abs(n.y)<.999?vec3(0,1,0):vec3(1,0,0),r=normalize(cross(up,n)),u=cross(n,r);vec3 sum=vec3(0);float wsum=0;const float step=0.05;for(float phi=0;phi<6.28318;phi+=step)for(float theta=0;theta<1.5708;theta+=step){vec3 l=normalize(r*cos(phi)*sin(theta)+u*sin(phi)*sin(theta)+n*cos(theta));float w=cos(theta)*sin(theta);sum+=textureLod(Environment,l,0.0).rgb*w;wsum+=w;}OutColor=vec4(PI*sum/max(wsum,1e-4),1);}
