#pragma once
// D3D12 camera-reprojection velocity pass. Semantics match Vulkan's RG16F
// depth-reconstructed camera motion, previous pixel minus current pixel.
namespace northstarcpp::shaders {
inline constexpr char camera_motion_shader[] = R"(
cbuffer MotionCamera : register(b0) {
    float4 current_camera;   // camera origin xyz, yaw
    float4 current_project;  // pitch, near, far, fov scale
    float4 previous_camera;
    float4 previous_project;
    float4 viewport;         // width, height, inverse width, inverse height
    float4 options;          // history valid, jitter delta x,y, reserved
};
Texture2D<float> g_depth : register(t3);
struct FSOut {float4 position:SV_Position;};
FSOut motion_vs(uint id:SV_VertexID) {
    FSOut output;
    float2 uv=float2((id<<1)&2,id&2);
    output.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);
    return output;
}
float2 motion_ps(FSOut input):SV_Target0 {
    if(options.x<.5)return float2(0,0);
    int2 pixel=int2(input.position.xy);
    float depth=g_depth.Load(int3(pixel,0));
    if(depth>=.999999||depth<=0)return float2(0,0);
    float near_clip=current_project.y,far_clip=current_project.z;
    float distance=far_clip*near_clip/
        max(1.e-5,far_clip-depth*(far_clip-near_clip));
    float2 ndc=input.position.xy*viewport.zw*2-1;
    float horizontal=ndc.x*distance*
        (viewport.x/viewport.y)/current_project.w;
    float vertical=-ndc.y*distance/current_project.w;
    float cp=cos(current_project.x),sp=sin(current_project.x);
    float forward=distance*cp-vertical*sp;
    float y=vertical*cp+distance*sp;
    float cy=cos(current_camera.w),sy=sin(current_camera.w);
    float3 world=current_camera.xyz+
        float3(horizontal*cy+forward*sy,
               y,-horizontal*sy+forward*cy);
    float3 delta=world-previous_camera.xyz;
    cp=cos(previous_project.x);sp=sin(previous_project.x);
    cy=cos(previous_camera.w);sy=sin(previous_camera.w);
    horizontal=delta.x*cy-delta.z*sy;
    forward=delta.x*sy+delta.z*cy;
    vertical=delta.y*cp-forward*sp;
    distance=delta.y*sp+forward*cp;
    if(distance<=previous_project.y||
       distance>=previous_project.z)return float2(0,0);
    float2 prev_ndc=float2(
        horizontal*previous_project.w/
            ((viewport.x/viewport.y)*distance),
        -vertical*previous_project.w/distance);
    float2 previous=(prev_ndc*.5+.5)*viewport.xy;
    float2 velocity=previous-input.position.xy+options.yz;
    if(any(isnan(velocity))||any(isinf(velocity)))return float2(0,0);
    return clamp(velocity,-32760.,32760.);
}
)";
}
