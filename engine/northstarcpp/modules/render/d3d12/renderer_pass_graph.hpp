#pragma once
// D3D12 frame plan: typed shared PostFX passes + opt-in fourth MRT.
// Stable pass IDs allow existing compiled GPU command handlers to be reused.
#include "../shared/render_graph.hpp"
#include "../shared/postfx_graph.hpp"
#include <cstdint>
namespace northstarcpp::render::d3d12::detail {
enum class FrameStage:std::uint32_t {
    Shadow=0,GBuffer=1,Deferred=2,Translucency=3,
    Bloom=4,Exposure=5,Tonemap=6,Composite=7,Present=8
};
enum class FrameResource:std::uint32_t {
    Shadow=0,GBufferAlbedo=1,GBufferNormal=2,GBufferMaterial=3,
    Depth=4,Scene=5,Output=6,Bloom=7,Exposure=8,ToneMapped=9,
    GBufferAuxiliary=10
};
inline northstarcpp::render::RenderGraph build_frame_graph(
    bool extended=false,bool bloom_enabled=false,
    bool hdr_enabled=false,bool composite_enabled=false) {
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
    auto auxiliary=graph.add_resource("G-buffer v2 auxiliary");
    using A=ResourceAccess;
    auto p0=graph.add_pass({"shadow",{}, {shadow}, {},{{shadow,A::DepthWrite}}});
    std::vector<ResourceId> gbuffer_writes{albedo,normal,material,depth,scene};
    std::vector<ResourceUsage> gbuffer_usage{
        {shadow,A::ShaderRead},{albedo,A::ColorAttachment},
        {normal,A::ColorAttachment},{material,A::ColorAttachment},
        {depth,A::DepthWrite},{scene,A::ColorAttachment}};
    if(extended) {
        gbuffer_writes.push_back(auxiliary);
        gbuffer_usage.push_back({auxiliary,A::ColorAttachment});
    }
    auto p1=graph.add_pass({"opaque G-buffer",{shadow},
        std::move(gbuffer_writes),{p0},std::move(gbuffer_usage)});
    std::vector<ResourceId> lighting_reads{albedo,normal,material,depth,shadow};
    std::vector<ResourceUsage> lighting_usage{
        {albedo,A::ShaderRead},{normal,A::ShaderRead},
        {material,A::ShaderRead},{depth,A::ShaderRead},
        {shadow,A::ShaderRead},{scene,A::ColorAttachment}};
    if(extended) {
        lighting_reads.push_back(auxiliary);
        lighting_usage.push_back({auxiliary,A::ShaderRead});
    }
    auto p2=graph.add_pass({"deferred lighting",
        std::move(lighting_reads),{scene},{p1},std::move(lighting_usage)});
    auto p3=graph.add_pass({"forward translucency",{}, {scene,depth},{p2},
        {{scene,A::ColorAttachment},{depth,A::DepthWrite}}});
    auto effects=append_postfx_graph(graph,{scene,output,bloom,exposure,tonemapped},
        p3,{bloom_enabled,hdr_enabled,composite_enabled});
    graph.add_pass({"HUD/Present",{output},{output},{effects.composite},
        {{output,A::ColorAttachment}}});
    return graph;
}
} // namespace northstarcpp::render::d3d12::detail
