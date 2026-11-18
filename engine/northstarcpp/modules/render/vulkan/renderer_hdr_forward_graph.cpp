// Forward-world HDR pipeline. World BCn surfaces retain their established
// shading path, but render into the SAME FP16 scene/postprocess targets.
#include "renderer_private.hpp"
#include <vector>
namespace northstarcpp::render::vulkan {
void Renderer::create_hdr_forward_graph() {
    if(!hdr_enabled_)return;
    using namespace northstarcpp::render;
    RenderGraph graph;
    auto swap=graph.add_resource("swapchain",true,ResourceAccess::Present);
    auto depth=graph.add_resource("depth");
    auto scene=graph.add_resource("FP16 forward world scene");
    std::array<ResourceId,bloom_levels_> bloom{};
    for(unsigned i=0;i<bloom_levels_;++i)
        bloom[i]=graph.add_resource("FP16 bloom "+std::to_string(i));
    ResourceId luminance{};
    if(auto_exposure_enabled_)
        luminance=graph.add_resource("8x8 forward HDR luminance");
    ResourceId velocity{};
    if(velocity_enabled_)
        velocity=graph.add_resource("RG16F forward motion vectors");
    auto last=graph.add_pass({"FP16 world geometry",{}, {scene,depth},{},
        {{scene,ResourceAccess::ColorAttachment},
         {depth,ResourceAccess::DepthWrite}}});
    if(velocity_enabled_)
        last=graph.add_pass({"Forward world motion vectors",{depth},
            {velocity},{last},
            {{depth,ResourceAccess::DepthRead},
             {velocity,ResourceAccess::ColorAttachment}}});
    if(auto_exposure_enabled_) {
        auto meter=graph.add_pass({"Forward HDR luminance",
            {scene},{luminance},{last},
            {{scene,ResourceAccess::ShaderRead},
             {luminance,ResourceAccess::ColorAttachment}}});
        last=graph.add_pass({"Forward HDR meter readback",
            {luminance},{},{meter},
            {{luminance,ResourceAccess::CopySource}}});
    }
    for(unsigned i=0;i<active_bloom_levels_;++i) {
        auto input=i?bloom[i-1]:scene;
        last=graph.add_pass({"Forward HDR bloom "+std::to_string(i),
            {input},{bloom[i]},{last},
            {{input,ResourceAccess::ShaderRead},
             {bloom[i],ResourceAccess::ColorAttachment}}});
    }
    std::vector<ResourceId> inputs{scene};
    std::vector<ResourceUsage> accesses{{scene,ResourceAccess::ShaderRead}};
    if(velocity_enabled_) {
        inputs.push_back(velocity);
        accesses.push_back({velocity,ResourceAccess::ShaderRead});
    }
    if(auto_exposure_enabled_) {
        inputs.push_back(luminance);
        accesses.push_back({luminance,ResourceAccess::ShaderRead});
    }
    for(unsigned i=0;i<active_bloom_levels_;++i) {
        inputs.push_back(bloom[i]);
        accesses.push_back({bloom[i],ResourceAccess::ShaderRead});
    }
    accesses.push_back({swap,ResourceAccess::ColorAttachment});
    auto post=graph.add_pass({"Forward HDR Filmic",
        std::move(inputs),{swap},{last},std::move(accesses)});
    auto ui=graph.add_pass({"engine.ui overlay",{swap},{swap},{post},
        {{swap,ResourceAccess::ColorAttachment}}});
    auto capture=graph.add_pass({"GPU readback",{swap},{swap},{ui},
        {{swap,ResourceAccess::CopySource}}});
    graph.add_pass({"present",{swap},{},{capture},
        {{swap,ResourceAccess::Present}}});
    hdr_forward_graph_plan_=graph.compile();
}
}
