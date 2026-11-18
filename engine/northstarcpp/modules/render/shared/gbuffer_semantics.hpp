#pragma once
// Versioned NorthStar G-buffer layout; never reinterpret legacy material data.
#include <cstdint>
#include <stdexcept>
#include <string_view>
namespace northstarcpp::render {
enum class GbufferVersion : std::uint32_t {Legacy=1,Extended=2};
inline constexpr std::uint32_t gbuffer_mrt_count=3; // Legacy alias
inline constexpr std::uint32_t gbuffer_v2_mrt_count=4;
inline constexpr std::uint32_t gbuffer_color_count(GbufferVersion version) noexcept {
    return version==GbufferVersion::Extended?gbuffer_v2_mrt_count:gbuffer_mrt_count;
}
inline constexpr std::uint32_t gbuffer_bytes_per_pixel(GbufferVersion version) noexcept {
    // 4+8+8 legacy; v2 adds an 8-bit-per-channel auxiliary surface.
    return version==GbufferVersion::Extended?24u:20u;
}
enum class GbufferMrt : std::uint32_t {
    AlbedoCoverage=0,NormalRoughness=1,EmissiveMetallic=2,
    Auxiliary=3
};
enum class GbufferDebug : std::uint32_t {
    Off=0,Albedo=1,Normal=2,Roughness=3,
    Metallic=4,Emissive=5,Depth=6,AmbientOcclusion=7,
    Coverage=8,MaterialAo=9,Clearcoat=10,Sheen=11
};
// Stable feature/debug naming shared across D3D12/Vulkan. Unrecognized
// requests fail closed to Off; backend-specific diagnostics stay separate.
inline constexpr GbufferDebug parse_gbuffer_debug(std::string_view name) noexcept {
    if(name=="albedo")return GbufferDebug::Albedo;
    if(name=="normal")return GbufferDebug::Normal;
    if(name=="roughness")return GbufferDebug::Roughness;
    if(name=="metallic")return GbufferDebug::Metallic;
    if(name=="emissive")return GbufferDebug::Emissive;
    if(name=="depth")return GbufferDebug::Depth;
    if(name=="ao")return GbufferDebug::AmbientOcclusion;
    if(name=="coverage")return GbufferDebug::Coverage;
    if(name=="material_ao")return GbufferDebug::MaterialAo;
    if(name=="clearcoat")return GbufferDebug::Clearcoat;
    if(name=="sheen")return GbufferDebug::Sheen;
    return GbufferDebug::Off;
}
// RTV0: albedo RGB / geometry coverage A
// RTV1: world normal RGB / signed roughness A (negative means unlit)
// RTV2: emissive RGB / metallic A
// RTV3 in Extended only: material AO R (default 1), clearcoat G,
//     sheen B, reserved A (default 0).
// Depth stays in a separate D32/R32 sampleable resource.
// Existing XML YMAT and ABI v1/v2 material batches keep identical meanings.
// If a material has no auxiliary output, renderer clears RTV3 to (1,0,0,0).
static_assert(static_cast<std::uint32_t>(GbufferMrt::Auxiliary)+1==
              gbuffer_v2_mrt_count);
}
