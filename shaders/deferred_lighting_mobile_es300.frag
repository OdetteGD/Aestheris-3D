#version 300 es
precision highp float;
in vec2 UV;layout(location=0)out vec4 OutColor;
uniform sampler2D PositionTex;uniform sampler2D NormalTex;uniform sampler2D AlbedoTex;uniform vec3 CameraPosition;uniform vec3 SunDirection;uniform vec3 SunColor;
void main(){vec3 p=texture(PositionTex,UV).xyz,n=normalize(texture(NormalTex,UV).xyz*2.0-1.0),a=texture(AlbedoTex,UV).rgb;float ndl=max(dot(n,normalize(-SunDirection)),0.0);OutColor=vec4(a*SunColor*ndl,1.0);}
