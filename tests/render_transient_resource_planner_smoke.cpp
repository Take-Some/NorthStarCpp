#include "../engine/northstarcpp/modules/render/shared/transient_resource_planner.hpp"
#include <stdexcept>
#define REQUIRE(condition) do { if(!(condition)) \
    throw std::runtime_error("Runtime acceptance failed: " #condition); } while(false)
int main() {
    using namespace northstarcpp::render;
    RenderGraph graph;
    auto a=graph.add_resource("a");
    auto b=graph.add_resource("b");
    auto c=graph.add_resource("c");
    auto d=graph.add_resource("different");
    auto imported=graph.add_resource("swap",true);
    auto p0=graph.add_pass({"begin",{}, {a,b},{}});
    auto p1=graph.add_pass({"consume a",{a},{c},{p0}});
    auto p2=graph.add_pass({"consume b",{b},{d},{p1}});
    graph.add_pass({"last",{c,d},{imported},{p2}});
    auto plan=graph.compile();
    // a lives through p1 and d begins at p2: compatible size/format
    // permits same physical slot, but b and d cannot be reused in p2.
    auto allocations=plan_transient_resources(graph,plan,{
        {a,4096,256,1},{b,4096,256,1},{c,8192,256,2},
        {d,4096,256,1}});
    REQUIRE(allocations.resource_to_slot[a]==allocations.resource_to_slot[d]);
    REQUIRE(allocations.resource_to_slot[b]!=allocations.resource_to_slot[d]);
    REQUIRE(allocations.resource_to_slot[c]!=allocations.resource_to_slot[a]);
    REQUIRE(allocations.resource_to_slot[imported]==-1);
    REQUIRE(allocations.slots.size()==3);
    REQUIRE(allocations.committed_bytes==16384);
    REQUIRE(allocations.peak_live_bytes==16384); // p1 and p2 retain three physical slots.
    bool rejected=false;
    try{(void)plan_transient_resources(graph,plan,{{imported,4096,256,1}});}
    catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);
    rejected=false;
    try{(void)plan_transient_resources(graph,plan,{{a,512,3,1}});}
    catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);
    rejected=false;
    try{(void)plan_transient_resources(graph,plan,{{a,4096,256,1},{a,4096,256,1}});}
    catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);
    // Strictly non-overlapping reuse only. A and B both occupy p0.
    REQUIRE(allocations.resource_to_slot[a]!=allocations.resource_to_slot[b]);
    RenderGraph handoff;
    const auto first=handoff.add_resource("first");
    const auto next=handoff.add_resource("next");
    const auto other=handoff.add_resource("other");
    handoff.add_pass({"start",{}, {first},{}});
    handoff.add_pass({"handoff",{}, {next,other},{}});
    const auto handoff_plan=plan_transient_resources(handoff,handoff.compile(),{
        {first,8192,256,7},{next,8192,256,7},{other,2048,256,8}});
    REQUIRE(handoff_plan.resource_to_slot[first]==handoff_plan.resource_to_slot[next]);
    REQUIRE(handoff_plan.committed_bytes==10240);
    REQUIRE(handoff_plan.peak_live_bytes==10240);
    auto invalid_execution=handoff.compile();
    invalid_execution.lifetimes[next].second=100;
    rejected=false;
    try{(void)plan_transient_resources(handoff,invalid_execution,{{next,8192,256,7}});}
    catch(const std::invalid_argument&){rejected=true;}
    REQUIRE(rejected);
}
