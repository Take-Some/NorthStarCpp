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
    REQUIRE(allocations.peak_live_bytes<=allocations.committed_bytes);
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
}
