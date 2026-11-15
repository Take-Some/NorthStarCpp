// NorthStarCpp D3D12 directional depth-only shadow map VS.
// Native shader / non-copy implementation, intended as the first cascade.
#pragma once
namespace northstarcpp::shaders {
inline constexpr char directional_shadow_shader[] = R"(
cbuffer ShadowCamera : register(b0) {
    row_major float4x4 light_view_projection;
};
struct V {float3 position:POSITION;};
struct P {float4 clip:SV_Position;};
P shadow_vs(V vertex) {
    P result;
    result.clip=mul(float4(vertex.position,1),light_view_projection);
    return result;
}
)";
}
