#version 300 es
precision highp float;

/* Aetheris GLES3 sky vertex: fullscreen ray reconstruction. */
layout(std140,binding=0) uniform FrameBlock {
    vec4 CameraPosition;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyParams;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    mat4 InvViewProj;
    mat4 CsmMatrices[3];
    vec4 CsmSplits;
};

out vec3 RayDirection;

void main(){
    vec2 p=vec2(
        gl_VertexID==1?3.0:-1.0,
        gl_VertexID==2?3.0:-1.0
    );
    vec4 n=InvViewProj*vec4(p,-1.0,1.0);
    vec4 f=InvViewProj*vec4(p,1.0,1.0);
    vec3 nearWorld=n.xyz/max(n.w,1e-5);
    vec3 farWorld=f.xyz/max(f.w,1e-5);
    RayDirection=normalize(farWorld-nearWorld);
    gl_Position=vec4(p,0.0,1.0);
}
