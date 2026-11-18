#include "../engine/northstarcpp/modules/render/d3d12/renderer_pass_graph.hpp"
#include <algorithm>
#include <stdexcept>
#define REQUIRE(condition) do { if(!(condition)) \
    throw std::runtime_error("Render graph smoke failed: " #condition); } while(false)
#include <string>

int main() {
    using namespace northstarcpp::render::d3d12::detail;
    auto graph = build_frame_graph(false,true,true,true);
    REQUIRE(graph.passes().size() == 9);
    REQUIRE(graph.resources().size() ==
        static_cast<std::size_t>(FrameResource::GBufferAuxiliary)+1);
    // Physical placed alias allocator depends on these enum IDs.
    REQUIRE(graph.resources()[static_cast<std::size_t>(FrameResource::GBufferNormal)].name
            == "G-buffer normal");
    REQUIRE(graph.resources()[static_cast<std::size_t>(FrameResource::Bloom)].name
            == "bloom temporary");
    REQUIRE(!graph.resources()[static_cast<std::size_t>(FrameResource::GBufferNormal)].imported);
    REQUIRE(!graph.resources()[static_cast<std::size_t>(FrameResource::Bloom)].imported);
    const auto plan = graph.compile();
    REQUIRE(plan.passes.size() == 9);
    for (std::uint32_t i = 0; i < 9; ++i)
        REQUIRE(plan.passes[i].id == i);
    REQUIRE(graph.passes()[0].name == "shadow");
    REQUIRE(graph.passes()[2].name == "deferred lighting");
    REQUIRE(graph.passes()[3].name == "forward translucency");
    REQUIRE(graph.passes()[4].name == "bloom extraction");
    REQUIRE(graph.passes()[5].name == "exposure metering");
    REQUIRE(graph.passes()[6].name == "HDR tonemapping");
    REQUIRE(graph.passes()[7].name == "scene color composite");
    REQUIRE(graph.passes()[8].name == "HUD/Present");
    const auto& final_pass = graph.passes().back();
    REQUIRE(final_pass.reads.size() == 1);
    REQUIRE(final_pass.writes.size() == 1);
    REQUIRE(final_pass.reads.front() == final_pass.writes.front());
    using northstarcpp::render::ResourceAccess;
    const auto normal=static_cast<northstarcpp::render::ResourceId>(FrameResource::GBufferNormal);
    const auto bloom=static_cast<northstarcpp::render::ResourceId>(FrameResource::Bloom);
    REQUIRE(plan.lifetimes[normal].second < plan.lifetimes[bloom].first);
    // Four actual transitions: shadow depth->SRV and the three
    // G-buffer attachments Undefined->RTV. Imported depth/scene already
    // begin in the required states and must not emit redundant barriers.
    REQUIRE(plan.passes[1].transitions.size() == 4);
    REQUIRE(std::any_of(plan.passes[2].transitions.begin(),
                       plan.passes[2].transitions.end(),[normal](auto const& t) {
        return t.resource==normal && t.before==ResourceAccess::ColorAttachment &&
               t.after==ResourceAccess::ShaderRead;
    }));
    // Subpass resources must transition according to declarative usages.
    const auto exposure=static_cast<northstarcpp::render::ResourceId>(FrameResource::Exposure);
    const auto tone=static_cast<northstarcpp::render::ResourceId>(FrameResource::ToneMapped);
    REQUIRE(std::any_of(plan.passes[5].transitions.begin(),
        plan.passes[5].transitions.end(),[exposure](auto const& t) {
            return t.resource==exposure &&
                   t.after==ResourceAccess::ColorAttachment;
        }));
    REQUIRE(std::any_of(plan.passes[6].transitions.begin(),
        plan.passes[6].transitions.end(),[tone](auto const& t) {
            return t.resource==tone &&
                   t.after==ResourceAccess::ColorAttachment;
        }));
    REQUIRE(std::any_of(plan.passes[7].transitions.begin(),
        plan.passes[7].transitions.end(),[tone](auto const& t) {
            return t.resource==tone && t.after==ResourceAccess::ShaderRead;
        }));
    // All backend-owned GPU resources have valid scheduled lifetimes.
    for (std::size_t i=0;i<plan.lifetimes.size();++i) {
        // GBuffer v2 auxiliary is registered but not used in default mode.
        if(i==static_cast<std::size_t>(FrameResource::GBufferAuxiliary)) {
            REQUIRE(plan.lifetimes[i].first==-1);
            REQUIRE(plan.lifetimes[i].second==-1);
            continue;
        }
        const auto& lifetime=plan.lifetimes[i];
        REQUIRE(lifetime.first >= 0);
        REQUIRE(lifetime.second >= lifetime.first);
    }
}
