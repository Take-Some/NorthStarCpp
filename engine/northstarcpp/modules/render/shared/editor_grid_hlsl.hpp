// NorthStarCpp analytic editor grid shader shared by D3D11/D3D12.
// Never included in game passes unless editor's optional grid API is active.
#pragma once
namespace northstarcpp::shaders {
inline constexpr char analytic_editor_grid_hlsl[]=R"HLSL(
cbuffer GridCamera : register(b0) {
    float4 eye_spacing;    // world eye XYZ, minor spacing in metres
    float4 camera_angles;  // yaw, pitch, roll, vertical FOV radians
    float4 screen_fade;    // viewport width, height, fade start/end (metres)
    float4 grid_options;   // opacity, major divisor, frame_index, reserved
};
struct GridPixel { float4 position:SV_Position; };
GridPixel grid_vs(uint id:SV_VertexID) {
    GridPixel output;
    float2 corner=float2((id<<1)&2,id&2);
    output.position=float4(corner*float2(2,-2)+float2(-1,1),0,1);
    return output;
}
float grid_line(float2 units, float fractional_width) {
    // Differentiate in screen space to suppress shimmering without
    // building geometry or an enormous world-space texture.
    float2 derivatives=max(fwidth(units),float2(.00005,.00005));
    float2 distance_to_line=abs(frac(units-.5)-.5);
    float2 coverage=1-smoothstep(
        derivatives*.25,derivatives*1.25+fractional_width,
        distance_to_line);
    return max(coverage.x,coverage.y);
}
float4 grid_color(float2 pixel_coord) {
    if(eye_spacing.w<=0 || screen_fade.x<=0 || screen_fade.y<=0)
        return float4(0,0,0,0);
    float yaw=camera_angles.x,pitch=camera_angles.y;
    float3 forward=float3(sin(yaw)*cos(pitch),sin(pitch),
                           cos(yaw)*cos(pitch));
    float3 right=float3(cos(yaw),0,-sin(yaw));
    float3 up=normalize(cross(forward,right));
    float roll=camera_angles.z;
    float3 rolled_up=up*cos(roll)+right*sin(roll);
    float3 rolled_right=right*cos(roll)-up*sin(roll);
    float2 screen=pixel_coord/screen_fade.xy;
    float2 ndc=float2(screen.x*2-1,1-screen.y*2);
    float tan_half=tan(camera_angles.w*.5);
    float aspect=screen_fade.x/screen_fade.y;
    float3 ray=normalize(forward+rolled_right*ndc.x*tan_half*aspect+
                        rolled_up*ndc.y*tan_half);
    if(abs(ray.y)<.00001)return float4(0,0,0,0);
    float distance=-eye_spacing.y/ray.y;
    if(distance<=0 || distance>screen_fade.w)
        return float4(0,0,0,0);
    float2 ground=eye_spacing.xz+ray.xz*distance;
    float2 minor_units=ground/eye_spacing.w;
    float minor=grid_line(minor_units,.0125);
    float2 pixel_width=fwidth(minor_units);
    // Gradually eliminate thin minor lines before they become subpixel.
    minor*=1-smoothstep(.8,2.2,max(pixel_width.x,pixel_width.y));
    float major=grid_line(minor_units/grid_options.y,.025);
    // D3DCompile/SM4 must evaluate each footprint component explicitly.
    float2 axis_footprint=float2(
        max(abs(ddx(ground.x))+abs(ddy(ground.x)),.025),
        max(abs(ddx(ground.y))+abs(ddy(ground.y)),.025));
    float x_axis=1-smoothstep(axis_footprint.y*.5,
        axis_footprint.y*1.4+.05,abs(ground.y));
    float z_axis=1-smoothstep(axis_footprint.x*.5,
        axis_footprint.x*1.4+.05,abs(ground.x));
    float alpha=max(minor*.21,major*.40);
    float3 color=major>minor?float3(.33,.44,.57):float3(.26,.35,.46);
    if(z_axis>alpha) {alpha=z_axis*.72;color=float3(.88,.38,.42);}
    if(x_axis>alpha) {alpha=x_axis*.72;color=float3(.33,.62,.89);}
    // World-distance fade is smooth at the horizon and prevents noisy
    // dFdx/ddy values from producing a harsh far-plane boundary.
    float fade=1-smoothstep(screen_fade.z,screen_fade.w,distance);
    alpha*=fade*saturate(grid_options.x);
    // Preserve zero-coverage pixels in the grid-only history target. A
    // discarded current sample cannot accumulate previous subpixel lines.
    return float4(color,saturate(alpha));
}
float4 grid_ps(GridPixel input):SV_Target {
    float4 color=grid_color(input.position.xy);
    clip(color.a-.001);
    return color;
}
// History texture holds ONLY analytic grid RGBA, never scene objects or HUD.
// TAA reprojects a ground intersection into the previous editor camera.
cbuffer PreviousGrid : register(b1) {
    float4 previous_eye_spacing;
    float4 previous_angles;
    float4 previous_screen_fade;
    float4 previous_options; // x=history valid, yzw reserved
};
Texture2D<float4> grid_history:register(t0);
SamplerState grid_sampler:register(s0);
float2 grid_reproject(float2 pixel_coord) {
    float3 forward=float3(sin(camera_angles.x)*cos(camera_angles.y),
        sin(camera_angles.y),cos(camera_angles.x)*cos(camera_angles.y));
    float3 right=float3(cos(camera_angles.x),0,-sin(camera_angles.x));
    float3 up=normalize(cross(forward,right));
    float3 rolled_up=up*cos(camera_angles.z)+right*sin(camera_angles.z);
    float3 rolled_right=right*cos(camera_angles.z)-up*sin(camera_angles.z);
    float2 ndc=float2(pixel_coord.x/screen_fade.x*2-1,
                      1-pixel_coord.y/screen_fade.y*2);
    float half_fov=tan(camera_angles.w*.5);
    float3 ray=normalize(forward+rolled_right*ndc.x*half_fov*
                      (screen_fade.x/screen_fade.y)+
                      rolled_up*ndc.y*half_fov);
    if(abs(ray.y)<.00001)return float2(-1,-1);
    float travel=-eye_spacing.y/ray.y;
    if(travel<=0)return float2(-1,-1);
    float3 world=eye_spacing.xyz+ray*travel;
    float3 prev_forward=float3(sin(previous_angles.x)*cos(previous_angles.y),
        sin(previous_angles.y),
        cos(previous_angles.x)*cos(previous_angles.y));
    float3 prev_right=float3(cos(previous_angles.x),0,
                            -sin(previous_angles.x));
    float3 prev_up=normalize(cross(prev_forward,prev_right));
    float3 prev_rolled_up=prev_up*cos(previous_angles.z)+
                         prev_right*sin(previous_angles.z);
    float3 prev_rolled_right=prev_right*cos(previous_angles.z)-
                            prev_up*sin(previous_angles.z);
    float3 delta=world-previous_eye_spacing.xyz;
    float depth=dot(delta,prev_forward);
    if(depth<=.02)return float2(-1,-1);
    float tan_previous=tan(previous_angles.w*.5);
    float2 projected=float2(
        dot(delta,prev_rolled_right)/
            (depth*tan_previous*(previous_screen_fade.x/previous_screen_fade.y)),
        dot(delta,prev_rolled_up)/(depth*tan_previous));
    return float2(projected.x*.5+.5,.5-projected.y*.5);
}
float2 grid_jitter(float index) {
    // Deterministic 4-phase subpixel jitter in raster space. The history
    // resolves these samples across time, unlike fwidth-only spatial AA.
    uint phase=(uint(index)&3u);
    return phase==0u?float2(-.25,-.125):
           phase==1u?float2(.25,.125):
           phase==2u?float2(-.125,.25):float2(.125,-.25);
}
float4 grid_temporal_ps(GridPixel input):SV_Target {
    float2 pixel=input.position.xy;
    float4 current=grid_color(pixel+grid_jitter(grid_options.z));
    if(previous_options.x<.5)
        return current;
    float2 history_uv=grid_reproject(pixel);
    if(any(history_uv<=float2(.001,.001)) ||
       any(history_uv>=float2(.999,.999)))
        return current;
    float4 history=grid_history.SampleLevel(grid_sampler,history_uv,0);
    // Confidence clamp limits trailing ghosts during rapidly changing LOD
    // without borrowing color/depth from the game scene.
    float history_weight=.65*
        (1-saturate(abs(history.a-current.a)*1.4));
    return lerp(current,history,history_weight);
}
float4 grid_composite_ps(GridPixel input):SV_Target {
    float2 uv=input.position.xy/screen_fade.xy;
    return grid_history.SampleLevel(grid_sampler,uv,0);
}
)HLSL";
}
