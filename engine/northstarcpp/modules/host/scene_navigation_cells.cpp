// Coarse WorldPartition connectivity over verified resident NEF8 cells.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_navigation_cells.hpp>
#include <northstarcpp/scene_world.hpp>
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <unordered_map>

namespace northstarcpp {
namespace {
bool adjacent(SceneCell const& a,SceneCell const& b) {
    // Bounding-box contact/overlap is only a sector-level connectivity hint.
    // No claim of unobstructed pedestrian or vehicle navigation is made.
    constexpr float epsilon=0.01f;
    auto separated=[&](float amin,float amax,float bmin,float bmax){
        return amax+epsilon<bmin || bmax+epsilon<amin;
    };
    return !separated(a.minimum.x,a.maximum.x,b.minimum.x,b.maximum.x) &&
           !separated(a.minimum.y,a.maximum.y,b.minimum.y,b.maximum.y) &&
           !separated(a.minimum.z,a.maximum.z,b.minimum.z,b.maximum.z);
}
}
SceneNavigationCells::SceneNavigationCells(Scene const& scene)
    : authored_(scene.cells) {
    validate_scene(scene);
}
void SceneNavigationCells::sync(SceneWorld const& world) {
    if(world.source().cells.size()!=authored_.size())
        throw std::runtime_error("Navigation source cell topology changed");
    for(std::size_t i=0;i<authored_.size();++i) {
        auto const& cell=authored_[i];
        if(world.source().cells[i].name!=cell.name)
            throw std::runtime_error("Navigation cell stable name changed");
        if(world.cell_loaded(cell.name) && world.cell_payload_ready(cell.name))
            resident_.insert_or_assign(cell.name,cell);
        else resident_.erase(cell.name);
    }
}
bool SceneNavigationCells::resident(std::string const& name) const {
    return resident_.count(name)!=0;
}
std::vector<std::string> SceneNavigationCells::route(
    std::string const& from,std::string const& to) const {
    if(!resident(from) || !resident(to))return {};
    std::deque<std::string> queue{from};
    std::unordered_map<std::string,std::string> predecessor{{from,{}}};
    while(!queue.empty()) {
        auto const node=queue.front();
        queue.pop_front();
        if(node==to)break;
        auto const& area=resident_.at(node);
        // Scene authoring order gives deterministic ties without storing
        // dense O(N^2) adjacency for large partitioned worlds.
        for(auto const& candidate:authored_) {
            if(!resident(candidate.name) || predecessor.count(candidate.name) ||
               !adjacent(area,candidate))
                continue;
            predecessor.emplace(candidate.name,node);
            queue.push_back(candidate.name);
        }
    }
    if(!predecessor.count(to))return {};
    std::vector<std::string> reversed;
    for(auto current=to;!current.empty();current=predecessor.at(current))
        reversed.push_back(current);
    std::reverse(reversed.begin(),reversed.end());
    return reversed;
}
} // namespace northstarcpp
