// Native D3D12 shader compilation, isolated from Renderer GPU state.
#include "shader_compiler.hpp"
#include "../shared/material_hlsl.hpp"
#include "../shared/deferred_hlsl.hpp"
#include "../shared/postfx_hlsl.hpp"
#include "../shared/postfx_bloom_hlsl.hpp"
#include "../shared/postfx_hdr_hlsl.hpp"
#include "../shared/shadow_hlsl.hpp"
#include "../shared/editor_grid_hlsl.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace northstarcpp::render::d3d12::shader_compiler {
using Microsoft::WRL::ComPtr;
using northstarcpp::shaders::authored_material_shader;
using northstarcpp::shaders::deferred_light_shader;
using northstarcpp::shaders::scene_fxaa_shader;
using northstarcpp::shaders::scene_bloom_shader;
using northstarcpp::shaders::scene_hdr_shader;
using northstarcpp::shaders::directional_shadow_shader;
namespace {
void check(HRESULT result,const char* action) {
    if(FAILED(result)) {
        char code[24]{};
        std::snprintf(code,sizeof(code)," (0x%08lx)",static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(action)+code);
    }
}
}
ComPtr<ID3DBlob> compile(const char* entry) {
    static const char code[]=R"(
cbuffer Camera : register(b0) {row_major float4x4 vp;};
struct V {float3 pos:POSITION;float3 rgb:COLOR;};
struct P {float4 pos:SV_Position;float3 rgb:COLOR;float3 world:TEXCOORD0;};
P vs(V v){P p;p.pos=mul(float4(v.pos,1),vp);p.rgb=v.rgb;p.world=v.pos;return p;}
float4 ps(P p):SV_Target{return float4(p.rgb,1);}
struct GBufferOut {
    float4 albedo:SV_Target0;
    float4 normal_rough:SV_Target1;
    float4 emissive_metal:SV_Target2;
#if defined(NS_GBUFFER_V2)
    float4 auxiliary:SV_Target3;
#endif
};
GBufferOut ps_gbuffer(P p) {
    GBufferOut outp;
    float3 normal=cross(ddx(p.world),ddy(p.world));
    float length2=dot(normal,normal);
    normal=length2>1.e-10?normal*rsqrt(length2):float3(0,1,0);
    outp.albedo=float4(saturate(p.rgb),1);
    outp.normal_rough=float4(normal,0.7);
    outp.emissive_metal=float4(0,0,0,0);
#if defined(NS_GBUFFER_V2)
    outp.auxiliary=float4(1,0,0,0);
#endif
    return outp;
}
)";
    ComPtr<ID3DBlob> binary,errors;
    bool const extended=std::strcmp(entry,"ps_gbuffer_v2")==0;
    D3D_SHADER_MACRO definitions[2]{{"NS_GBUFFER_V2","1"},{nullptr,nullptr}};
    auto hr=D3DCompile(code,sizeof(code)-1,"NorthStar D3D12",
        extended?definitions:nullptr,nullptr,
        extended?"ps_gbuffer":entry,entry[0]=='v'?"vs_5_0":"ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&errors);
    if(FAILED(hr)&&errors)
        throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    check(hr,"Compile D3D12 shader");return binary;
}
ComPtr<ID3DBlob> compile_editor_grid(char const* entry,char const* profile) {
    auto const& source=northstarcpp::shaders::analytic_editor_grid_hlsl;
    ComPtr<ID3DBlob> binary,errors;
    auto const hr=D3DCompile(source,sizeof(source)-1,"NorthStar D3D12 grid",
        nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,
        &binary,&errors);
    if(FAILED(hr)&&errors)
        throw std::runtime_error(
            static_cast<const char*>(errors->GetBufferPointer()));
    check(hr,"Compile D3D12 analytic editor grid");
    return binary;
}
constexpr char sky_shader_code[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 view_projection; };
cbuffer SkyTint : register(b1) { float4 sky_tint; };
Texture2D sky_texture : register(t0);
SamplerState sky_sampler : register(s0);
struct SkyVertex { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct SkyPixel { float4 position : SV_Position; float3 direction : TEXCOORD0; };
SkyPixel sky_vertex_main(SkyVertex v) {
    SkyPixel p;
    p.position = mul(float4(v.position * 100.0, 1.0), view_projection);
    // Sky UVs are not a photographic cloud map; the Perlin map is volumetric noise.
    p.direction = v.position;
    return p;
}
float sky_noise(float3 dir, float frequency) {
    // Triplanar sampling avoids seam and pole artifacts.
    float3 weights = pow(abs(dir), 4.0);
    weights /= max(dot(weights, float3(1.0, 1.0, 1.0)), 0.0001);
    float3 yz = sky_texture.Sample(sky_sampler, dir.yz * frequency).rgb;
    float3 xz = sky_texture.Sample(sky_sampler, dir.xz * frequency).rgb;
    float3 xy = sky_texture.Sample(sky_sampler, dir.xy * frequency).rgb;
    float3 n = float3(dot(yz, float3(0.33333, 0.33333, 0.33334)),
                      dot(xz, float3(0.33333, 0.33333, 0.33334)),
                      dot(xy, float3(0.33333, 0.33333, 0.33334)));
    return dot(n, weights);
}
float4 sky_pixel_main(SkyPixel p) : SV_Target {
    float3 direction = normalize(p.direction);
    float altitude = saturate(direction.y * 0.5 + 0.5);
    float zenith = smoothstep(0.08, 0.97, altitude);
    float3 color = lerp(float3(0.46, 0.65, 0.88),
                        float3(0.075, 0.22, 0.47), zenith);
    // The authored base_color grades the atmosphere, not the noise RGB.
    color *= lerp(float3(1.0, 1.0, 1.0), sky_tint.rgb * 1.6, 0.18);
    float density = sky_noise(direction, 2.1) * 0.78 +
                    sky_noise(direction, 6.7) * 0.22;
    float clouds = smoothstep(0.54, 0.75, density);
    float visibility = smoothstep(0.02, 0.27, altitude);
    float3 cloud_color = lerp(float3(0.68, 0.78, 0.89),
                              float3(0.92, 0.94, 0.97), altitude);
    color = lerp(color, cloud_color, clouds * visibility * 0.67);
    return float4(saturate(color), 1.0);
}
)";

ComPtr<ID3DBlob> compile_sky(const char* entry,const char* profile) {
    ComPtr<ID3DBlob> compiled,errors;
    HRESULT result=D3DCompile(sky_shader_code,sizeof(sky_shader_code)-1,
        "NorthStar D3D12 sky",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&compiled,&errors);
    if(FAILED(result)&&errors)
        throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    check(result,"Compile D3D12 sky");
    return compiled;
}
ComPtr<ID3DBlob> compile_authored_material(char const* entry,char const* profile) {
    ComPtr<ID3DBlob> compiled,errors;
    HRESULT result=D3DCompile(authored_material_shader,sizeof(authored_material_shader)-1,
        "NorthStar D3D12 Material PBR",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&compiled,&errors);
    if(FAILED(result) && errors)
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(result,"Compile D3D12 authored PBR shader");
    return compiled;
}
ComPtr<ID3DBlob> compile_material_variant(
    std::string const& source,char const* entry,char const* profile)
{
    ComPtr<ID3DBlob> binary,errors;
    HRESULT const status=D3DCompile(source.data(),source.size(),
        "NorthStar D3D12 Material Graph",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&errors);
    if(FAILED(status)&&errors)
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(status,"Compile D3D12 material graph variant");
    return binary;
}
ComPtr<ID3DBlob> compile_deferred(char const* entry,char const* profile) {
    ComPtr<ID3DBlob> compiled,errors;
    HRESULT result=D3DCompile(deferred_light_shader,sizeof(deferred_light_shader)-1,
        "NorthStar deferred lighting",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&compiled,&errors);
    if(FAILED(result)&&errors)
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(result,"Compile D3D12 deferred lighting shader");
    return compiled;
}
ComPtr<ID3DBlob> compile_fxaa(char const* entry,char const* profile) {
    ComPtr<ID3DBlob> compiled,errors;
    HRESULT result=D3DCompile(scene_fxaa_shader,sizeof(scene_fxaa_shader)-1,
        "NorthStar FXAA post processing",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&compiled,&errors);
    if(FAILED(result)&&errors)
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(result,"Compile D3D12 FXAA shader");
    return compiled;
}
ComPtr<ID3DBlob> compile_bloom(char const* entry,char const* profile) {
    ComPtr<ID3DBlob> binary,errors;
    HRESULT result=D3DCompile(scene_bloom_shader,sizeof(scene_bloom_shader)-1,
        "NorthStar quarter-res bloom",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&errors);
    if(FAILED(result)&&errors)
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(result,"Compile D3D12 bloom shader");return binary;
}
ComPtr<ID3DBlob> compile_hdr(char const* entry,char const* profile) {
    ComPtr<ID3DBlob> binary,errors;
    HRESULT hr=D3DCompile(scene_hdr_shader,sizeof(scene_hdr_shader)-1,
        "NorthStar HDR postprocessing",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&errors);
    if(FAILED(hr)&&errors)
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(hr,"Compile HDR postprocessing shader");return binary;
}
ComPtr<ID3DBlob> compile_shadow(char const* entry,char const* profile) {
    ComPtr<ID3DBlob> binary,errors;
    HRESULT hr=D3DCompile(directional_shadow_shader,
        sizeof(directional_shadow_shader)-1,"NorthStar shadow depth",
        nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,
        0,&binary,&errors);
    if(FAILED(hr)&&errors)
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(hr,"Compile D3D12 shadow VS");return binary;
}
} // namespace northstarcpp::render::d3d12::shader_compiler
