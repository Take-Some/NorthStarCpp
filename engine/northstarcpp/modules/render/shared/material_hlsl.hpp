// Shared GPU PBR shader program for all first-class NorthStar render providers.
#pragma once
namespace northstarcpp::shaders {
inline constexpr char authored_material_shader[] = R"(
cbuffer Camera : register(b0) {
    row_major float4x4 view_projection;
    float4 camera_world;
};
cbuffer Material : register(b1) {
    float4 base_color;
    float4 factors; // x=roughness,y=metallic,z=unlit,w=reserved
    uint4 present;  // x is bitmask of available textures
};
Texture2D base_map : register(t0);
Texture2D normal_map : register(t1);
Texture2D rough_map : register(t2);
Texture2D metal_map : register(t3);
Texture2D emissive_map : register(t4);
#ifndef NS_GRAPH_BASE
#define NS_GRAPH_BASE(p) float4(1,1,1,1)
#endif
#ifndef NS_GRAPH_ROUGH
#define NS_GRAPH_ROUGH(p) float4(factors.x,factors.x,factors.x,factors.x)
#endif
#ifndef NS_GRAPH_METAL
#define NS_GRAPH_METAL(p) float4(factors.y,factors.y,factors.y,factors.y)
#endif
#ifndef NS_GRAPH_EMISSIVE
#define NS_GRAPH_EMISSIVE(p) float4(0,0,0,0)
#endif
#ifndef NS_GRAPH_OPACITY
#define NS_GRAPH_OPACITY(p) float4(1,1,1,1)
#endif
#ifndef NS_BLEND_MODE
#define NS_BLEND_MODE 0
#endif
#ifndef NS_ALPHA_CUTOFF
#define NS_ALPHA_CUTOFF 0.5
#endif
SamplerState material_sampler : register(s0);
#if defined(NS_GPU_SKINNING)
cbuffer NativeSkinPalette : register(b2) {
    row_major float4x4 native_skin_bones[256];
};
struct V {
    float3 pos:POSITION;float3 normal:NORMAL;float2 uv:TEXCOORD0;
    uint4 joints0:BLENDINDICES0;uint4 joints1:BLENDINDICES1;
    float4 weights0:BLENDWEIGHT0;float4 weights1:BLENDWEIGHT1;
};
#else
struct V {float3 pos:POSITION;float3 normal:NORMAL;float2 uv:TEXCOORD0;};
#endif
struct P {float4 clip:SV_Position;float3 world:TEXCOORD1;
          float3 normal:NORMAL;float2 uv:TEXCOORD0;};
P material_vs(V v) {
    P p;
#if defined(NS_GPU_SKINNING)
    float4 world=float4(0,0,0,0);
    float3 normal=float3(0,0,0);
    [unroll]for(uint i=0;i<4;++i) {
        if(v.weights0[i]>0) {
            uint joint=v.joints0[i];
            world+=mul(float4(v.pos,1),native_skin_bones[joint])*v.weights0[i];
            normal+=mul(v.normal,(float3x3)native_skin_bones[joint])*v.weights0[i];
        }
        if(v.weights1[i]>0) {
            uint joint=v.joints1[i];
            world+=mul(float4(v.pos,1),native_skin_bones[joint])*v.weights1[i];
            normal+=mul(v.normal,(float3x3)native_skin_bones[joint])*v.weights1[i];
        }
    }
    p.clip=mul(world,view_projection);
    p.world=world.xyz;p.normal=normal;
#else
    p.clip=mul(float4(v.pos,1),view_projection);
    p.world=v.pos;p.normal=v.normal;
#endif
    p.uv=v.uv;
    return p;
}
float3 srgb_encode(float3 c) {
    c=saturate(c);
    return lerp(c*12.92,1.055*pow(c,1.0/2.4)-0.055,step(0.0031308,c));
}
float3 material_forward_output(float3 linear_color) {
#if defined(NS_HDR_OUTPUT)
    return max(linear_color,0);
#else
    return srgb_encode(linear_color);
#endif
}
float4 material_ps(P p):SV_Target {
    float4 authored_color=NS_GRAPH_BASE(p);
    float3 albedo=base_color.rgb*authored_color.rgb;
    if(present.x & 1)albedo*=base_map.Sample(material_sampler,p.uv).rgb;
    float3 n=normalize(p.normal);
    if(present.x & 2) {
        float2 du=ddx(p.uv),dv=ddy(p.uv);
        float3 dx=ddx(p.world),dy=ddy(p.world);
        float3 tangent=dx*dv.y-dy*du.y;
        float3 bitangent=dy*du.x-dx*dv.x;
        if(dot(tangent,tangent)>1.e-8 && dot(bitangent,bitangent)>1.e-8) {
            tangent=normalize(tangent);
            bitangent=normalize(bitangent);
            float3 bump=normal_map.Sample(material_sampler,p.uv).rgb*2-1;
            n=normalize(tangent*bump.x+bitangent*bump.y+n*bump.z);
        }
    }
    float rough=saturate((NS_GRAPH_ROUGH(p)).x);
    float metal=saturate((NS_GRAPH_METAL(p)).x);
    if(present.x & 4)rough*=rough_map.Sample(material_sampler,p.uv).r;
    if(present.x & 8)metal*=metal_map.Sample(material_sampler,p.uv).r;
    rough=max(0.04,rough);
    float3 emissive=(present.x & 16)?
        emissive_map.Sample(material_sampler,p.uv).rgb:float3(0,0,0);
    emissive+=(NS_GRAPH_EMISSIVE(p)).rgb;
    float opacity=saturate(base_color.a*authored_color.a*
        (NS_GRAPH_OPACITY(p)).x);
#if NS_BLEND_MODE == 1
    clip(opacity - NS_ALPHA_CUTOFF);
#endif
    if(factors.z>0.5)return float4(material_forward_output(albedo+emissive),base_color.a);
    float3 light=normalize(float3(0.45,0.82,0.30));
    float3 view=normalize(camera_world.xyz-p.world);
    float3 half_v=normalize(light+view);
    float NoL=saturate(dot(n,light));
    float NoV=max(0.001,saturate(dot(n,view)));
    float NoH=saturate(dot(n,half_v));
    float VoH=saturate(dot(view,half_v));
    float alpha=rough*rough;
    float a2=alpha*alpha;
    float denom=NoH*NoH*(a2-1)+1;
    float distribution=a2/max(3.14159265*denom*denom,1.e-5);
    float k=(rough+1)*(rough+1)*0.125;
    float geom=(NoV/(NoV*(1-k)+k))*(NoL/(NoL*(1-k)+k));
    float3 f0=lerp(float3(.04,.04,.04),albedo,metal);
    float3 fresnel=f0+(1-f0)*pow(1-VoH,5);
    float3 spec=(distribution*geom*fresnel)/max(4*NoV*NoL,0.001);
    float3 diffuse=(1-fresnel)*(1-metal)*albedo/3.14159265;
    float3 lit=(diffuse+spec)*NoL*3.0+albedo*0.16+emissive;
    return float4(material_forward_output(lit),opacity);
}
// Deferred geometry: compact 3-MRT layout, no redundant world-position RT.
// Pixel coverage lives in g_albedo alpha. The depth SRV reconstructs position.
struct GBufferOut {
    float4 albedo_coverage : SV_Target0;
    float4 normal_roughness : SV_Target1;
    float4 emissive_metallic : SV_Target2;
};
GBufferOut material_gbuffer_ps(P p) {
    GBufferOut result;
    float4 authored_color=NS_GRAPH_BASE(p);
    float3 albedo=base_color.rgb*authored_color.rgb;
    if(present.x & 1) albedo*=base_map.Sample(material_sampler,p.uv).rgb;
    float3 n=normalize(p.normal);
    if(present.x & 2) {
        float2 du=ddx(p.uv),dv=ddy(p.uv);
        float3 dx=ddx(p.world),dy=ddy(p.world);
        float3 tangent=dx*dv.y-dy*du.y;
        float3 bitangent=dy*du.x-dx*dv.x;
        if(dot(tangent,tangent)>1.e-8 && dot(bitangent,bitangent)>1.e-8) {
            tangent=normalize(tangent);
            bitangent=normalize(bitangent);
            float3 bump=normal_map.Sample(material_sampler,p.uv).rgb*2-1;
            n=normalize(tangent*bump.x+bitangent*bump.y+n*bump.z);
        }
    }
    float rough=saturate((NS_GRAPH_ROUGH(p)).x);
    float metal=saturate((NS_GRAPH_METAL(p)).x);
    if(present.x & 4) rough*=rough_map.Sample(material_sampler,p.uv).r;
    if(present.x & 8) metal*=metal_map.Sample(material_sampler,p.uv).r;
    rough=max(0.04,rough);
    float3 emissive=(present.x & 16)?
        emissive_map.Sample(material_sampler,p.uv).rgb:float3(0,0,0);
    emissive+=(NS_GRAPH_EMISSIVE(p)).rgb;
    float opacity=saturate(base_color.a*authored_color.a*
        (NS_GRAPH_OPACITY(p)).x);
#if NS_BLEND_MODE == 1
    clip(opacity - NS_ALPHA_CUTOFF);
#endif
    result.albedo_coverage=float4(saturate(albedo),opacity);
    result.normal_roughness=float4(n,factors.z>0.5?-rough:rough);
    result.emissive_metallic=float4(emissive,saturate(metal));
    return result;
}
)";
 } // namespace northstarcpp::shaders
