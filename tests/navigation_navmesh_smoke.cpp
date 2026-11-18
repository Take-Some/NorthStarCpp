#include <northstarcpp/navigation_navmesh.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp;
using namespace northstarcpp::navigation;
namespace {
void require(bool okay,char const* message) {
    if(!okay)throw std::runtime_error(message);
}
}
int main() {
    RuntimeNavMesh nav;
    auto floor=[](float x,float z,float)->std::optional<NavSurface> {
        // 2 metre thick barrier bisects the direct line. A* MUST find
        // a route through the walkable northern/southern passage.
        if(std::abs(x)<1.f && std::abs(z)<2.5f)return std::nullopt;
        return NavSurface{0.f,{0.f,1.f,0.f}};
    };
    require(nav.begin_build({-4.f,0.f,0.f},{4.f,0.f,0.f}),
        "Failed to begin NavMesh tile rasterization");
    std::size_t slices=0;
    while(!nav.advance_build(floor,12)) {
        if(++slices>1000)throw std::runtime_error("Incremental nav build hung");
        require(!nav.ready(),"NavMesh marked ready with remaining cells");
    }
    require(nav.ready() && nav.node_count()>100 && slices>1,
        "NavMesh incremental rasterization failed");
    auto result=nav.route({-4.f,0.f,0.f},{4.f,0.f,0.f});
    require(result.reached_goal() && result.waypoints.size()>=2,
        "A* failed to route around the static barrier");
    bool detoured=false;
    for(auto const& step:result.waypoints) {
        if(std::abs(step.z)>2.f)detoured=true;
        require(!(std::abs(step.x)<.9f && std::abs(step.z)<2.4f),
            "Path cut through obstacle");
    }
    require(detoured,"NavMesh returned a direct wall-crossing path");

    // Another barrier: sampled centers look walkable, but the capsule
    // sweep forbids crossing a narrow fence between those centers.
    RuntimeNavMesh thin;
    require(thin.build({-3.f,0.f,0.f},{3.f,0.f,0.f},
        [](float,float,float)->std::optional<NavSurface> {
            return NavSurface{0.f,{0.f,1.f,0.f}};
        }),"Flat-floor NavMesh build failed");
    auto blockedEdge=[](Vector3 a,Vector3 b) {
        return a.x<.6f && b.x>=.6f && std::abs(a.z)<1.5f &&
            std::abs(b.z)<1.5f;
    };
    auto fence=thin.route({-3.f,0.f,0.f},{3.f,0.f,0.f},blockedEdge);
    require(fence.reached_goal(),"Sweep-constrained A* did not find passage");

    // Two floors are vertically coincident but disconnected; do not
    // silently teleport between them without a walkable ramp/link.
    RuntimeNavMesh stacked;
    require(stacked.build({-2.f,0.f,0.f},{2.f,3.5f,0.f},
        [](float,float,float probe)->std::optional<NavSurface> {
            return NavSurface{probe>1.75f?3.5f:0.f,{0.f,1.f,0.f}};
        }),"Multilevel NavMesh build failed");
    require(stacked.route({-2.f,0.f,0.f},{2.f,3.5f,0.f}).status==
            NavRoute::Status::Blocked,
        "Multilevel floors were accidentally connected through air");

    // A climbable graded staircase/ramp with per-cell height changes
    // below max_step must connect both ends.
    RuntimeNavMesh ramp;
    auto climbing=[](float x,float,float)->std::optional<NavSurface> {
        return NavSurface{std::clamp((x+3.f)*.25f,0.f,1.5f),
                          {0.f,.9701425f,-.2425356f}};
    };
    require(ramp.build({-3.f,0.f,0.f},{3.f,1.5f,0.f},climbing),
            "Ramp build failed");
    require(ramp.route({-3.f,0.f,0.f},{3.f,1.5f,0.f}).reached_goal(),
            "Ramp failed graded height route");

    // Far goals are addressed in bounded partial tiles. Never claim
    // completion until the actor physically reaches the final tile.
    RuntimeNavMesh distant;
    require(distant.build({0.f,0.f,0.f},{120.f,0.f,0.f},
        [](float,float,float)->std::optional<NavSurface> {
            return NavSurface{0.f,{0.f,1.f,0.f}};
        }),"Partial tile build failed");
    auto partial=distant.route({0.f,0.f,0.f},{120.f,0.f,0.f});
    require(partial.status==NavRoute::Status::Partial &&
            partial.can_advance() && distant.partial_coverage(),
            "Far target should have produced a bounded partial path");
    std::cout<<"NavMesh incremental raster + A* + obstacle + vertical separation + "
             <<"ramp + partial tile: PASS\n";
}
