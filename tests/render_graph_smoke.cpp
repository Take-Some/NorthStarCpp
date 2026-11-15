#include "../engine/northstarcpp/modules/render/shared/render_graph.hpp"
#include <stdexcept>
#define REQUIRE(condition) do { if(!(condition)) \
    throw std::runtime_error("Render graph smoke failed: " #condition); } while(false)
#include <stdexcept>
int main() {
    using namespace northstarcpp::render;
    RenderGraph g;
    auto depth = g.add_resource("depth");
    auto gbuffer = g.add_resource("gbuffer");
    auto swap = g.add_resource("swapchain", true);
    auto shadow = g.add_resource("shadows");
    auto a = g.add_pass({"depth", {}, {depth}, {}});
    auto b = g.add_pass({"shadows", {}, {shadow}, {}});
    auto c = g.add_pass({"gbuffer", {depth}, {gbuffer}, {a}});
    auto d = g.add_pass({"lighting", {gbuffer, shadow}, {swap}, {b,c}});
    auto e = g.add_pass({"ui", {swap}, {swap}, {d}});
    auto plan = g.compile();
    REQUIRE(plan.passes.size() == 5);
    REQUIRE(plan.passes[0].id == a && plan.passes[1].id == b);
    REQUIRE(plan.passes[2].id == c && plan.passes[3].id == d && plan.passes[4].id == e);
    REQUIRE(plan.lifetimes[depth].first == 0 && plan.lifetimes[depth].second == 2);
    REQUIRE(!plan.passes[4].barriers.empty());
    bool rejected = false;
    try {
        RenderGraph bad;
        auto transient = bad.add_resource("uninitialized");
        bad.add_pass({"invalid", {transient}, {}, {}});
        (void)bad.compile();
    } catch (std::logic_error const&) { rejected = true; }
    REQUIRE(rejected);
    // Typed usages must not claim a write from read-only declarations.
    rejected=false;
    try {
        RenderGraph invalid_usage;
        auto texture=invalid_usage.add_resource("texture",true);
        invalid_usage.add_pass({"read",{texture},{},{},
            {{texture,ResourceAccess::ColorAttachment}}});
    } catch(const std::invalid_argument&) { rejected=true; }
    REQUIRE(rejected);
    rejected=false;
    try {
        RenderGraph invalid_initial;
        (void)invalid_initial.add_resource("transient",false,ResourceAccess::ShaderRead);
    } catch(const std::invalid_argument&) { rejected=true; }
    REQUIRE(rejected);
    return 0;
}
