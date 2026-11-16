#include "../engine/northstarcpp/modules/render/shared/render_graph.hpp"
#include <stdexcept>
#include <string>
#define REQUIRE(condition) do { if(!(condition)) \
    throw std::runtime_error("Render graph smoke failed: " #condition); } while(false)
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
    RenderGraph hazards;
    auto color=hazards.add_resource("color",true,ResourceAccess::Present);
    auto depth_target=hazards.add_resource("depth target",true,ResourceAccess::DepthRead);
    auto unused=hazards.add_resource("unused");
    hazards.add_pass({"sample",{depth_target,color},{},{},
        {{depth_target,ResourceAccess::DepthRead},{color,ResourceAccess::ShaderRead}}});
    hazards.add_pass({"update",{color,color},{depth_target,color,color},{},
        {{depth_target,ResourceAccess::DepthWrite},{color,ResourceAccess::ColorAttachment}}});
    hazards.add_pass({"present",{color},{},{},{{color,ResourceAccess::Present}}});
    auto compiled=hazards.compile();
    REQUIRE((compiled.passes[1].barriers == std::vector<ResourceId>{color,depth_target}));
    REQUIRE((compiled.passes[2].barriers == std::vector<ResourceId>{color}));
    REQUIRE((compiled.lifetimes[unused] == std::pair<int,int>{-1,-1}));
    REQUIRE(compiled.passes[0].transitions.size()==1);
    REQUIRE(compiled.passes[0].transitions[0].before==ResourceAccess::Present);
    REQUIRE(compiled.passes[1].transitions.size()==2);
    REQUIRE(compiled.passes[1].transitions[0].resource==depth_target);
    REQUIRE(compiled.passes[1].transitions[0].before==ResourceAccess::DepthRead);
    REQUIRE(compiled.passes[2].transitions[0].before==ResourceAccess::ColorAttachment);

    rejected=false;
    try {
        RenderGraph read_modify;
        auto target=read_modify.add_resource("transient");
        read_modify.add_pass({"invalid first use",{target},{target},{}});
        (void)read_modify.compile();
    } catch(const std::logic_error&) { rejected=true; }
    REQUIRE(rejected);
    // A large graph with one producer, thousands of read-only consumers and
    // a final overwrite must not generate redundant read->read barriers.
    RenderGraph stress;
    auto history=stress.add_resource("history");
    auto first=stress.add_pass({"write",{},{history},{}});
    constexpr unsigned consumers=2048;
    for(unsigned i=0;i<consumers;++i)
        stress.add_pass({"read"+std::to_string(i),{history},{},{first}});
    auto last=stress.add_pass({"overwrite",{},{history},{}});
    auto large=stress.compile();
    REQUIRE(large.passes.size()==consumers+2);
    REQUIRE(large.passes[0].id==first);
    REQUIRE(large.passes.back().id==last);
    REQUIRE(large.passes[1].barriers.size()==1);
    for(std::size_t i=2;i<consumers+1;++i)
        REQUIRE(large.passes[i].barriers.empty());
    REQUIRE(large.passes.back().barriers.size()==1);
    REQUIRE(large.lifetimes[history].first==0 &&
            large.lifetimes[history].second==int(consumers+1));
    auto same=stress.compile();
    for(std::size_t i=0;i<large.passes.size();++i)
        REQUIRE(large.passes[i].id==same.passes[i].id &&
                large.passes[i].barriers==same.passes[i].barriers);
    return 0;
}
