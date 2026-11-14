// NorthStarCpp Scene V3 prefabs: extraction, instancing, validation, serialization.
#include "editor_prefab.hpp"
#include "editor_tools.hpp"
#include <northstarcpp/scene.hpp>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

int main(int argc, char** argv) {
    if(argc!=2) return 99;
    using namespace northstarcpp;
    using namespace northstarcpp::editor_tools;
    Scene source;
    source.objects.resize(4);
    source.objects[0].name="Unrelated";
    source.objects[1].name="Workshop";
    source.objects[1].position={25.f,3.f,40.f};
    source.objects[2].name="Door";
    source.objects[2].position={27.f,3.f,40.f};
    source.objects[3].name="Handle";
    source.objects[3].position={27.f,4.f,40.f};
    promote_to_v3(source);
    source.objects[2].node.parent=source.objects[1].node.id;
    source.objects[3].node.parent=source.objects[2].node.id;
    source.layers.push_back({"Details",false});
    source.objects[2].node.layer="Details";
    source.cells.push_back({"Area",{20.f,0.f,30.f},{40.f,20.f,50.f},false});
    source.objects[1].node.cell="Area";
    source.objects[2].node.cell="Area";
    source.objects[3].node.cell="Area";
    source.objects[2].node.script="scripts/door.js";
    source.objects[1].node.asset="buildings/workshop.ydd";
    validate_scene(source);

    auto prefab=capture_prefab(source,1);
    if(prefab.version!=3 || prefab.objects.size()!=3 ||
       prefab.objects[0].node.parent!=0 || prefab.objects[0].position.x!=0.f ||
       prefab.objects[1].position.x!=2.f || prefab.objects[2].position.y!=1.f ||
       prefab.layers.size()!=1 || prefab.cells.size()!=1 ||
       prefab.cells[0].minimum.x!=-5.f ||
       source.objects.size()!=4) return 1;

    std::filesystem::path const file=std::filesystem::absolute(argv[1]);
    save_scene(prefab,file);
    auto persisted=load_scene(file);
    std::error_code ignored;
    std::filesystem::remove(file,ignored);
    if(persisted.version!=3 || persisted.objects.size()!=3) return 20;
    auto const first_parent=source.objects[0].node.id;
    auto i1=instantiate_prefab(source,persisted,{100.f,0.f,200.f},first_parent);
    auto i2=instantiate_prefab(source,persisted,{200.f,0.f,300.f});
    if(source.objects.size()!=10 || i1!=4 || i2!=7 ||
       source.objects[i1].node.parent!=first_parent ||
       source.objects[i2].node.parent!=0 ||
       source.objects[i1].position.x!=100.f ||
       source.objects[i1+1].position.x!=102.f ||
       source.objects[i1+2].position.y!=1.f ||
       source.objects[i1+1].node.script!="scripts/door.js" ||
       source.cells.size()!=3 ||
       source.objects[i1].node.cell==source.objects[i2].node.cell) return 2;

    std::unordered_set<std::uint64_t> ids;
    for(auto const& e:source.objects) {
        if(!ids.insert(e.node.id).second) return 3;
    }
    if(source.objects[i1+1].node.parent!=source.objects[i1].node.id ||
       source.objects[i2+2].node.parent!=source.objects[i2+1].node.id)
        return 4;
    auto final_scene=parse_scene_v3(serialize_scene_v3(source));
    if(final_scene.objects.size()!=10 || final_scene.cells.size()!=3)
        return 5;
    auto const before=serialize_scene_v3(source);

    bool rejected=false;
    try { instantiate_prefab(source,persisted,{0.f,0.f,0.f},999999); }
    catch (std::invalid_argument const&) { rejected=true; }
    if(!rejected || serialize_scene_v3(source)!=before) return 6;

    Scene bad=persisted;
    bad.objects[1].node.parent=0;
    rejected=false;
    try { instantiate_prefab(source,bad,{0.f,0.f,0.f}); }
    catch (std::invalid_argument const&) { rejected=true; }
    if(!rejected || serialize_scene_v3(source)!=before) return 7;

    rejected=false;
    try { instantiate_prefab(source,persisted,
        {std::numeric_limits<float>::quiet_NaN(),0.f,0.f}); }
    catch (std::invalid_argument const&) { rejected=true; }
    if(!rejected || serialize_scene_v3(source)!=before) return 8;

    rejected=false;
    try { instantiate_prefab(source,persisted,{900000.f,0.f,0.f}); }
    catch (std::invalid_argument const&) { rejected=true; }
    if(!rejected || serialize_scene_v3(source)!=before) return 9;
    return 0;
}
