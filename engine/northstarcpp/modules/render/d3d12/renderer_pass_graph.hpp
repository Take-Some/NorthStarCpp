#pragma once
// D3D12 frame scheduler with typed resource dependencies and PostFX subpasses.
// Stable C renderer ABI: these types are entirely provider-private.
#include "../shared/render_graph.hpp"
#include <cstdint>

namespace northstarcpp::render::d3d12::detail {
enum class FrameStage : std::uint32_t {
    Shadow=0, GBuffer=1, Deferred=2, Translucency=3,
    Bloom=4, Exposure=5, Tonemap=6, Composite=7, Present=8
};
enum class FrameResource : std::uint32_t {
    Shadow=0, GBufferAlbedo=1, GBufferNormal=2, GBufferMaterial=3,
    Depth=4, Scene=5, Output=6, Bloom=7, Exposure=8, ToneMapped=9
};

inline northstarcpp::render::RenderGraph build_frame_graph() {
    using namespace northstarcpp::render;
    RenderGraph graph;
    auto shadow=graph.add_resource("shadow depth array",true,ResourceAccess::ShaderRead);
    auto albedo=graph.add_resource("G-buffer albedo");
    auto normal=graph.add_resource("G-buffer normal");
    auto material=graph.add_resource("G-buffer material");
    auto depth=graph.add_resource("scene depth",true,ResourceAccess::DepthWrite);
    auto scene=graph.add_resource("scene color",true,ResourceAccess::ColorAttachment);
    auto output=graph.add_resource("swapchain output",true,ResourceAccess::ColorAttachment);
    auto bloom=graph.add_resource("bloom temporary");
    auto exposure=graph.add_resource("temporal exposure",true,ResourceAccess::ShaderRead);
    auto tonemapped=graph.add_resource("tonemapped LDR",true,ResourceAccess::ShaderRead);
    auto p0=graph.add_pass({"shadow",{}, {shadow}, {},
        {{shadow,ResourceAccess::DepthWrite}}});
    auto p1=graph.add_pass({"opaque G-buffer",{shadow},
        {albedo,normal,material,depth,scene},{p0},
        {{shadow,ResourceAccess::ShaderRead},
         {albedo,ResourceAccess::ColorAttachment},
         {normal,ResourceAccess::ColorAttachment},
         {material,ResourceAccess::ColorAttachment},
         {depth,ResourceAccess::DepthWrite},
         {scene,ResourceAccess::ColorAttachment}}});
    auto p2=graph.add_pass({"deferred lighting",
        {albedo,normal,material,depth,shadow},{scene},{p1},
        {{albedo,ResourceAccess::ShaderRead},
         {normal,ResourceAccess::ShaderRead},
         {material,ResourceAccess::ShaderRead},
         {depth,ResourceAccess::ShaderRead},
         {shadow,ResourceAccess::ShaderRead},
         {scene,ResourceAccess::ColorAttachment}}});
    auto p3=graph.add_pass({"forward translucency",{}, {scene,depth},{p2},
        {{scene,ResourceAccess::ColorAttachment},{depth,ResourceAccess::DepthWrite}}});
    auto p4=graph.add_pass({"bloom extraction",{scene},{bloom},{p3},
        {{scene,ResourceAccess::ShaderRead},
         {bloom,ResourceAccess::ColorAttachment}}});
    auto p5=graph.add_pass({"exposure metering",{scene},{exposure},{p4},
        {{scene,ResourceAccess::ShaderRead},
         {exposure,ResourceAccess::ColorAttachment}}});
    auto p6=graph.add_pass({"HDR tonemapping",{scene,bloom,exposure},{tonemapped},{p5},
        {{scene,ResourceAccess::ShaderRead},
         {bloom,ResourceAccess::ShaderRead},
         {exposure,ResourceAccess::ShaderRead},
         {tonemapped,ResourceAccess::ColorAttachment}}});
    auto p7=graph.add_pass({"FXAA/SDR composite",{tonemapped,bloom},{output},{p6},
        {{tonemapped,ResourceAccess::ShaderRead},
         {bloom,ResourceAccess::ShaderRead},
         {output,ResourceAccess::ColorAttachment}}});
    graph.add_pass({"HUD/Present",{output},{output},{p7},
        {{output,ResourceAccess::ColorAttachment}}});
    return graph;
}
} // namespace northstarcpp::render::d3d12::detail
