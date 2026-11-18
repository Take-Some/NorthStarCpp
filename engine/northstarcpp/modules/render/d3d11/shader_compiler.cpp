// Native D3D11 shader compilation isolated from renderer state and D3D11 Com<T>.
#include "shader_compiler.hpp"
#include "../shared/material_hlsl.hpp"
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <stdexcept>
namespace northstarcpp::render::d3d11::shader_compiler {
using northstarcpp::shaders::authored_material_shader;
namespace {
void check(HRESULT result,const char* action) {
    if(FAILED(result)) {
        char code[24]{};
        std::snprintf(code,sizeof(code)," (0x%08lx)",static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(action)+code);
    }
}
}
constexpr char shader_code[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 view_projection; };
struct Vertex {
    float3 position : POSITION;
    float3 color : COLOR;
    float surface : TEXCOORD1;
};
struct Pixel {
    float4 position : SV_Position;
    float3 color : COLOR;
    float3 world : TEXCOORD0;
    float surface : TEXCOORD1;
};
Texture2D sand_albedo : register(t1);
Texture2D sand_normal : register(t2);
Texture2D sand_roughness : register(t3);
Texture2D rock_albedo : register(t4);
Texture2D rock_normal : register(t5);
Texture2D rock_roughness : register(t6);
Texture2D forest_albedo : register(t7);
Texture2D forest_normal : register(t8);
Texture2D forest_roughness : register(t9);
SamplerState world_sampler : register(s0);
Pixel vertex_main(Vertex v) {
    Pixel p;
    p.position = mul(float4(v.position, 1.0), view_projection);
    p.color = v.color;
    p.world = v.position;
    p.surface = v.surface;
    return p;
}
float3 surface_triplanar(Texture2D tex,float3 world,float3 weights) {
    float3 yz=tex.Sample(world_sampler,world.yz * 0.16).rgb;
    float3 xz=tex.Sample(world_sampler,world.xz * 0.16).rgb;
    float3 xy=tex.Sample(world_sampler,world.xy * 0.16).rgb;
    return yz*weights.x+xz*weights.y+xy*weights.z;
}
float4 pixel_main(Pixel p) : SV_Target {
    if(p.surface<0.5)
        return float4(p.color,1.0);
    float3 normal=normalize(cross(ddy(p.world),ddx(p.world)));
    float3 weights=pow(abs(normal),4.0);
    weights/=max(dot(weights,1.0),0.0001);
    float3 albedo;
    float3 normalData;
    float roughness;
    if(p.surface<1.5) {
        albedo=surface_triplanar(sand_albedo,p.world,weights);
        normalData=surface_triplanar(sand_normal,p.world,weights);
        roughness=surface_triplanar(sand_roughness,p.world,weights).r;
    } else if(p.surface<2.5) {
        albedo=surface_triplanar(rock_albedo,p.world,weights);
        normalData=surface_triplanar(rock_normal,p.world,weights);
        roughness=surface_triplanar(rock_roughness,p.world,weights).r;
    } else {
        albedo=surface_triplanar(forest_albedo,p.world,weights);
        normalData=surface_triplanar(forest_normal,p.world,weights);
        roughness=surface_triplanar(forest_roughness,p.world,weights).r;
    }
    // Conservative terrain detail lighting, not a full PBR BRDF.
    float2 tangentNormal=normalData.rg*2.0-1.0;
    float3 worldNormal=normalize(normal + float3(tangentNormal.x,0.0,tangentNormal.y)*0.18);
    float lighting=0.56+0.36*saturate(dot(abs(worldNormal),
                                          normalize(float3(0.35,0.85,0.25))));
    float brightness=lerp(0.92,1.05,saturate(roughness));
    float3 litRgb=saturate(albedo*p.color*lighting*brightness);
    // The backbuffer is DXGI_FORMAT_R8G8B8A8_UNORM rather than SRGB.
    // sRGB BC1 samples are converted to linear by the GPU, so encode
    // the final lit material color before writing to the backbuffer.
    float3 encoded=lerp(litRgb*12.92,
        1.055*pow(litRgb,1.0/2.4)-0.055,step(0.0031308,litRgb));
    return float4(saturate(encoded),1.0);
}
)";

// Dedicated authored-object material pipeline: real mesh UVs and normals,
// GGX microfacet lighting, Base Color/Normal/Roughness/Metallic/Emissive maps.
constexpr char sky_shader_code[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 view_projection; };
cbuffer SkyTint : register(b1) { float4 sky_tint; };
Texture2D sky_texture : register(t0);
SamplerState sky_sampler : register(s0);
struct SkyVertex { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct SkyPixel { float4 position : SV_Position; float2 uv : TEXCOORD0; };
SkyPixel sky_vertex_main(SkyVertex v) {
    SkyPixel p;
    p.position = mul(float4(v.position * 100.0, 1.0), view_projection);
    // Sky UVs are not a photographic cloud map; the Perlin map is volumetric noise.
    p.uv = v.uv;
    return p;
}
float4 sky_pixel_main(SkyPixel p) : SV_Target {
    // engine.sky composites the authored HDR/cloud projection/flow maps
    // into one equirectangular RGBA8 source BEFORE the provider call.
    // Sampling it directly keeps Vulkan and D3D12 on the same data path.
    const float2 uv=float2(frac(p.uv.x),saturate(p.uv.y));
    float3 source=sky_texture.Sample(sky_sampler,uv).rgb;
    return float4(saturate(source*sky_tint.rgb),1.0);
}
)";

constexpr char character_shader_code[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 view_projection; }
cbuffer Material : register(b1) { float alpha_cutoff; }
Texture2D albedo : register(t0);
SamplerState material_sampler : register(s0);
struct Vertex {float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0;};
struct Pixel {float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0;};
Pixel character_vs(Vertex v) {
    Pixel p;
    p.position = mul(float4(v.position, 1), view_projection);
    p.normal = v.normal;
    p.uv = v.uv;
    return p;
}
float4 character_ps(Pixel p) : SV_Target {
    float4 color = albedo.Sample(material_sampler,p.uv);
    clip(color.a - alpha_cutoff);
    float lighting = 0.44 + 0.56 * saturate(dot(normalize(p.normal),
        normalize(float3(-0.45,0.8,-0.3))));
    float3 lit = saturate(color.rgb * lighting);
    return float4(lerp(lit*12.92, 1.055*pow(lit,1.0/2.4)-0.055,step(0.0031308,lit)),1);
}
)";
void compile_character(char const* entry,char const* profile,ID3DBlob** output)
{
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    auto const status = D3DCompile(character_shader_code,sizeof(character_shader_code)-1,
        "character",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,
        output,errors.GetAddressOf());
    if (FAILED(status) && errors.Get())
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(status,"Compile character shader");
}

void compile_authored_material(const char* entry,const char* profile,ID3DBlob** result) {
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    std::string const program=std::string("#define NS_D3D11_DYNAMIC_LIGHTS 1\n")+
        authored_material_shader;
    auto const status=D3DCompile(program.data(),
        program.size(),"NorthStar Material PBR",
        nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,
        result,errors.GetAddressOf());
    if(FAILED(status) && errors.Get())
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(status,"Compile authored material shader");
}
void compile_sky(char const* entry, char const* profile, ID3DBlob** output)
{
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    auto const hr = D3DCompile(sky_shader_code, sizeof(sky_shader_code)-1,
        "skydome", nullptr, nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS,
        0, output, errors.GetAddressOf());
    if (FAILED(hr) && errors.Get())
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(hr, "Compile skydome shader");
}

void compile(char const* entry, char const* profile, ID3DBlob** output)
{
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    auto const hr = D3DCompile(shader_code, sizeof(shader_code) - 1, "viewport", nullptr,
        nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, output, errors.GetAddressOf());
    if (FAILED(hr) && errors.Get())
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(hr, "Compile viewport shader");
}

} // namespace northstarcpp::render::d3d11::shader_compiler
