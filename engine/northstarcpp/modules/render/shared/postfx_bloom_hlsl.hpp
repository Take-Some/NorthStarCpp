// NorthStar Bloom prefilter: quarter-resolution bright-pass, LDR-safe.
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
float3 bright(float3 c) {
    float lum=dot(c,float3(.2126,.7152,.0722));
    float weight=saturate((lum-bloom_params.z)/max(0.001,lum));
    return c*weight;
}
float4 bloom_ps(FSOut p):SV_Target {
    float2 d=viewport_size.zw*2;
    float3 glow=bright(scene_color.SampleLevel(scene_sampler,p.uv,0).rgb)*.20;
    glow+=(bright(scene_color.SampleLevel(scene_sampler,p.uv+float2(d.x,0),0).rgb)+
            bright(scene_color.SampleLevel(scene_sampler,p.uv-float2(d.x,0),0).rgb)+
            bright(scene_color.SampleLevel(scene_sampler,p.uv+float2(0,d.y),0).rgb)+
            bright(scene_color.SampleLevel(scene_sampler,p.uv-float2(0,d.y),0).rgb))*.12;
    glow+=(bright(scene_color.SampleLevel(scene_sampler,p.uv+d,0).rgb)+
            bright(scene_color.SampleLevel(scene_sampler,p.uv-d,0).rgb)+
            bright(scene_color.SampleLevel(scene_sampler,p.uv+float2(d.x,-d.y),0).rgb)+
            bright(scene_color.SampleLevel(scene_sampler,p.uv+float2(-d.x,d.y),0).rgb))*.08;
    return float4(glow,1);
}
)";
}
