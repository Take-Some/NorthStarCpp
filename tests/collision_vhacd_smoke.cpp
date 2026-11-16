#include <northstarcpp/collision_cooker.hpp>
#include <northstarcpp/collision_hulls.hpp>
#include <northstarcpp/collision_cook_cache.hpp>
#include <filesystem>
#include <atomic>
#include <northstarcpp/collision_authoring.hpp>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    using namespace northstarcpp;
    std::vector<float> xyz;
    std::vector<std::uint32_t> indices;
    auto add_cube=[&](float offset) {
        auto const base=static_cast<std::uint32_t>(xyz.size()/3);
        const float points[]{
            0,0,0, 1,0,0, 0,1,0, 1,1,0,
            0,0,1, 1,0,1, 0,1,1, 1,1,1};
        for(int i=0;i<24;++i)
            xyz.push_back(points[i]+(i%3==0?offset:0.f));
        const std::uint32_t triangles[]{
            0,2,3,0,3,1,4,5,7,4,7,6,0,1,5,0,5,4,
            2,6,7,2,7,3,0,4,6,0,6,2,1,3,7,1,7,5};
        for(auto index:triangles)indices.push_back(base+index);
    };
    add_cube(0);add_cube(3.5f);
    collision_cooker::Options settings;
    settings.max_hulls=2;
    settings.resolution=20000;
    auto hulls=collision_cooker::cook(xyz,indices,settings);
    if(hulls.empty()||hulls.size()>2)
        throw std::runtime_error("V-HACD returned invalid hull count");
    collision_hulls::Set set{"models/double_cube.obj",2,hulls};
    auto bytes=collision_hulls::encode(set);
    auto restored=collision_hulls::decode(bytes);
    if(!restored||restored->hulls.size()!=hulls.size()||
       restored->source!=set.source||restored->splits_num!=2)
        throw std::runtime_error("Cooked hull serialization failed");
    for(auto const& hull:restored->hulls)
        if(hull.xyz.size()<12||hull.indices.empty()||
           hull.xyz.size()%3||hull.indices.size()%3)
            throw std::runtime_error("V-HACD produced invalid topology");
    collision_mesh::Mesh source;
    source.source="models/double_cube.obj";
    source.xyz=xyz;
    source.indices=indices;
    auto const temp=std::filesystem::temp_directory_path()/
        "northstar_collision_ddc_smoke";
    std::atomic<int> progress{0};
    auto first=collision_cook_cache::load_or_cook(
        source,2,temp,20000,&progress);
    if(first.hit || progress.load()!=100)
        throw std::runtime_error("First collision DDC request must cook");
    progress=0;
    auto second=collision_cook_cache::load_or_cook(
        source,2,temp,20000,&progress);
    if(!second.hit || second.hulls.hulls.size()!=first.hulls.hulls.size() ||
       progress.load()!=100)
        throw std::runtime_error("Collision DDC cache hit failed");
    auto other=source;other.xyz[0]+=0.01f;
    if(collision_cook_cache::key(other,2,20000)==
       collision_cook_cache::key(source,2,20000))
        throw std::runtime_error("Collision DDC key missed source change");
    std::filesystem::remove_all(temp);
    std::cout<<"V-HACD hulls="<<hulls.size()
             <<" persisted_bytes="<<bytes.size()
             <<" hull_serialization=PASS\n";
    return 0;
}
