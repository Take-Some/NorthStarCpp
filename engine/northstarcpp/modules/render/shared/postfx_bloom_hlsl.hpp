// NorthStar Bloom: quarter-resolution HDR soft-knee 13-tap prefilter.
// HDR extraction and multi-mip pyramids will replace this first pass later.
#pragma once
namespace northstarcpp::shaders {
inline constexpr char scene_bloom_shader[] = R"(
cbuffer PostFx : register(b0) {
    float4 viewport_size;
    float4 bloom_params; // intensity, bloom_enabled, threshold, fxaa_enabled
};
Texture2D scene_color : register(t0);
SamplerState scene_sampler : register(s0);
struct FSOut { float4 position:SV_Position; float2 uv:TEXCOORD0; };
FSOut bloom_vs(uint id:SV_VertexID) {
    FSOut p;
    float2 v=float2((id << 1) & 2, id & 2);
    p.position=float4(v*float2(2,-2)+float2(-1,1),0,1);
    p.uv=v; return p;
}
// Soft-knee bright-pass avoids flashing highlights as luminance crosses
// the threshold. The kernel is energy-normalized to preserve HDR intensity.
float3 bright(float3 c) {
    c=max(c,float3(0,0,0));
    float lum=dot(c,float3(.2126,.7152,.0722));
    float threshold=max(.0001,bloom_params.z);
    float knee=max(.0001,threshold*.5);
    float soft=clamp(lum-threshold+knee,0,2*knee);
    soft=soft*soft/(4*knee+.0001);
    float contribution=max(max(lum-threshold,0),soft)/max(lum,.0001);
    // Curb pathological single-pixel fireflies without losing ordinary
    // light-source highlight intensity in linear HDR.
    return c*contribution/(1+max(lum-24,0)*.01);
}
float3 sample_bright(float2 uv) {
    return bright(scene_color.SampleLevel(scene_sampler,saturate(uv),0).rgb);
}
float4 bloom_ps(FSOut p):SV_Target {
    float2 d=viewport_size.zw*2;
    // 13 weighted bilinear fetches. Center 0.20, cross 0.12*4,
    // diagonals 0.06*4, and farther cross 0.02*4 = 1.00 total.
    // Quarter-res target offers a 4x4 source-pixel footprint.
    float3 glow=sample_bright(p.uv)*.20;
    glow+=(sample_bright(p.uv+float2(d.x,0))+
           sample_bright(p.uv-float2(d.x,0))+
           sample_bright(p.uv+float2(0,d.y))+
           sample_bright(p.uv-float2(0,d.y)))*.12;
    glow+=(sample_bright(p.uv+d)+sample_bright(p.uv-d)+
           sample_bright(p.uv+float2(d.x,-d.y))+
           sample_bright(p.uv+float2(-d.x,d.y)))*.06;
    glow+=(sample_bright(p.uv+float2(2*d.x,0))+
           sample_bright(p.uv-float2(2*d.x,0))+
           sample_bright(p.uv+float2(0,2*d.y))+
           sample_bright(p.uv-float2(0,2*d.y)))*.02;
    return float4(glow,1);
}
)";
}
