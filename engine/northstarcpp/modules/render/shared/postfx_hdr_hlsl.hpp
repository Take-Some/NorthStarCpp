// NorthStar native HDR pipeline: log-average exposure and ACES-fitting
// tone mapping. Scene input and bloom remain linear FP16 until the last pass.
#pragma once
namespace northstarcpp::shaders {
inline constexpr char scene_hdr_shader[] = R"(
cbuffer PostFx : register(b0) {
    float4 viewport_size;
    float4 bloom_params; // strength, bloom enabled, threshold, FXAA enabled
    float4 exposure_params; // history valid, temporal response, reserved
};
Texture2D<float4> hdr_scene : register(t0);
Texture2D<float4> hdr_bloom : register(t1);
Texture2D<float4> exposure_map : register(t2);
SamplerState scene_sampler : register(s0);
struct FSOut {float4 pos:SV_Position;float2 uv:TEXCOORD0;};
FSOut hdr_vs(uint id:SV_VertexID){
    FSOut o;
    float2 v=float2((id<<1)&2,id&2);
    o.pos=float4(v*float2(2,-2)+float2(-1,1),0,1);
    o.uv=v;return o;
}
float4 exposure_ps(FSOut input):SV_Target {
    // One-pixel log average over 64 stratified screen locations.
    // This is a deterministic GPU exposure estimate, not yet a temporal
    // luminance histogram with eye-adaptation history.
    float sum_log=0;
    [unroll]for(int y=0;y<8;++y)
      [unroll]for(int x=0;x<8;++x) {
        float2 uv=float2((x+0.5)/8,(y+0.5)/8);
        float3 c=hdr_scene.SampleLevel(scene_sampler,uv,0).rgb;
        float lum=dot(max(c,float3(0,0,0)),float3(.2126,.7152,.0722));
        sum_log+=log(max(lum,0.0001));
      }
    float avg_lum=exp(sum_log*(1.0/64.0));
    float target_exposure=clamp(0.18/max(avg_lum,0.0001),0.25,4.0);
    float exposure=target_exposure;
    if(exposure_params.x>0.5) {
        float history=exposure_map.Load(int3(0,0,0)).r;
        exposure=lerp(history,target_exposure,saturate(exposure_params.y));
    }
    return float4(exposure,avg_lum,0,1);
}
float3 aces_fitted(float3 c) {
    c=max(c,0);
    return saturate((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14));
}
float3 linear_to_srgb(float3 c) {
    c=saturate(c);
    return lerp(c*12.92,1.055*pow(c,1.0/2.4)-.055,step(.0031308,c));
}
float4 hdr_tonemap_ps(FSOut input):SV_Target {
    float3 scene=hdr_scene.Load(int3(int2(input.pos.xy),0)).rgb;
    float3 bloom=bloom_params.y>0.5 ?
        hdr_bloom.SampleLevel(scene_sampler,input.uv,0).rgb * bloom_params.x :
        float3(0,0,0);
    float exposure=exposure_map.Load(int3(0,0,0)).r;
    return float4(linear_to_srgb(aces_fitted((scene+bloom)*exposure)),1);
}
)";
}
