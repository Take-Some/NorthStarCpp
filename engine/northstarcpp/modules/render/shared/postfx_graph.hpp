#pragma once
// Typed, backend-neutral PostFX subgraph. Backends bind physical surfaces
// to these resource IDs and execute their own GPU commands; no API objects.
#include "render_graph.hpp"
namespace northstarcpp::render {
struct PostFxGraphOptions {
    bool bloom=false;
    bool hdr=false;
    bool composite=false; // enabled by HDR, FXAA, bloom or Film Look
};
struct PostFxGraphResources {
    ResourceId scene,output,bloom,exposure,tonemapped;
};
struct PostFxGraphPasses {PassId bloom,exposure,tonemap,composite;};
inline PostFxGraphPasses append_postfx_graph(RenderGraph& graph,
        PostFxGraphResources const& resources,PassId after,
        PostFxGraphOptions opts) {
    using A=ResourceAccess;
    // Every stage retains a stable ID for provider dispatch; disabled stages
    // are zero-resource nodes, not fake GPU reads/writes.
    Pass bloom{"bloom extraction",{}, {}, {after}, {}};
    if(opts.bloom) {
        bloom.reads={resources.scene};
        bloom.writes={resources.bloom};
        bloom.usages={{resources.scene,A::ShaderRead},
                      {resources.bloom,A::ColorAttachment}};
    }
    auto b=graph.add_pass(std::move(bloom));
    Pass exposure{"exposure metering",{}, {}, {b}, {}};
    if(opts.hdr) {
        exposure.reads={resources.scene};
        exposure.writes={resources.exposure};
        exposure.usages={{resources.scene,A::ShaderRead},
                         {resources.exposure,A::ColorAttachment}};
    }
    auto e=graph.add_pass(std::move(exposure));
    Pass tonemap{"HDR tonemapping",{}, {}, {e}, {}};
    if(opts.hdr) {
        tonemap.reads={resources.scene,resources.exposure};
        tonemap.usages={{resources.scene,A::ShaderRead},
                        {resources.exposure,A::ShaderRead},
                        {resources.tonemapped,A::ColorAttachment}};
        if(opts.bloom) {
            tonemap.reads.push_back(resources.bloom);
            tonemap.usages.push_back({resources.bloom,A::ShaderRead});
        }
        tonemap.writes={resources.tonemapped};
    }
    auto t=graph.add_pass(std::move(tonemap));
    Pass composite{"scene color composite",{}, {}, {t}, {}};
    if(opts.composite) {
        const auto source=opts.hdr?resources.tonemapped:resources.scene;
        composite.reads={source};
        composite.writes={resources.output};
        composite.usages={{source,A::ShaderRead},
                          {resources.output,A::ColorAttachment}};
        if(opts.bloom && !opts.hdr) {
            composite.reads.push_back(resources.bloom);
            composite.usages.push_back({resources.bloom,A::ShaderRead});
        }
    }
    return {b,e,t,graph.add_pass(std::move(composite))};
}
} // namespace northstarcpp::render
