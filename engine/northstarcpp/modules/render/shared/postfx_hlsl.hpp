// NorthStar native post-processing. FXAA operates on 3D scene color before
// the HUD; UI text and reticles remain pixel-sharp.
#pragma once
namespace northstarcpp::shaders {
inline constexpr char scene_fxaa_shader[] = R"(
cbuffer PostFx : register(b0) {
    float4 viewport_size; // width, height, reciprocal width, reciprocal height
    float4 bloom_params; // bloom strength, bloom enabled, threshold, FXAA enabled
};
Texture2D scene_color : register(t0);
Texture2D bloom_color : register(t1);
SamplerState scene_sampler : register(s0);
struct FSOut { float4 position:SV_Position; float2 uv:TEXCOORD0; };
FSOut fxaa_vs(uint vertex_id:SV_VertexID) {
    FSOut result;
    float2 tex=float2((vertex_id << 1) & 2, vertex_id & 2);
    result.position=float4(tex*float2(2,-2)+float2(-1,1),0,1);
    result.uv=tex;
    return result;
}
float3 sample_scene(float2 uv) {
    return scene_color.SampleLevel(scene_sampler, saturate(uv), 0).rgb;
}
float luma(float3 color) {
    return dot(color,float3(0.299,0.587,0.114));
}
float4 compose_scene(float3 filtered, float2 uv) {
    if(bloom_params.y>0.5) {
        // Quarter-resolution prefiltered highlights, linear sampling
        // produces a soft halo without applying blur to the HUD.
        float3 glow=bloom_color.SampleLevel(scene_sampler,uv,0).rgb;
        filtered=saturate(filtered+glow*bloom_params.x);
    }
    return float4(filtered,1);
}
float4 bloom_additive_ps(FSOut p):SV_Target {
    // The unfiltered scene has already been copied into the backbuffer.
    // This pass only adds the prefiltered highlights with ONE/ONE blending.
    float3 glow=bloom_color.SampleLevel(scene_sampler,p.uv,0).rgb;
    return float4(glow*bloom_params.x,0);
}
float4 fxaa_ps(FSOut p):SV_Target {
    float2 inv_screen=viewport_size.zw;
    float3 rgbNW=sample_scene(p.uv+float2(-1,-1)*inv_screen);
    float3 rgbNE=sample_scene(p.uv+float2(1,-1)*inv_screen);
    float3 rgbSW=sample_scene(p.uv+float2(-1,1)*inv_screen);
    float3 rgbSE=sample_scene(p.uv+float2(1,1)*inv_screen);
    float3 rgbM=sample_scene(p.uv);
    float lumaNW=luma(rgbNW),lumaNE=luma(rgbNE);
    float lumaSW=luma(rgbSW),lumaSE=luma(rgbSE);
    float lumaM=luma(rgbM);
    float lumaMin=min(lumaM,min(min(lumaNW,lumaNE),min(lumaSW,lumaSE)));
    float lumaMax=max(lumaM,max(max(lumaNW,lumaNE),max(lumaSW,lumaSE)));
    // Skip flat regions; budget samples primarily for actual edges.
    if(lumaMax-lumaMin<0.0312) return compose_scene(rgbM,p.uv);
    float2 dir=float2(-((lumaNW+lumaNE)-(lumaSW+lumaSE)),
                       ((lumaNW+lumaSW)-(lumaNE+lumaSE)));
    float dirReduce=max((lumaNW+lumaNE+lumaSW+lumaSE)*0.25*0.0312,1.0/128.0);
    float invMin=1.0/(min(abs(dir.x),abs(dir.y))+dirReduce);
    dir=clamp(dir*invMin,-8,8)*inv_screen;
    float3 a=0.5*(sample_scene(p.uv+dir*(1.0/3.0-0.5))+
                  sample_scene(p.uv+dir*(2.0/3.0-0.5)));
    float3 b=a*0.5+0.25*(sample_scene(p.uv+dir*(-0.5))+
                           sample_scene(p.uv+dir*0.5));
    float lumaB=luma(b);
    return compose_scene((lumaB<lumaMin||lumaB>lumaMax)?a:b,p.uv);
}
)";
}
