// NorthStarCpp Scene V3 prefabs: extraction, instancing, validation, serialization.
#include "editor_prefab.hpp"
#include "editor_tools.hpp"
#include <northstarcpp/scene.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_hulls.hpp>
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
    source.objects[2].node.collision=false;
    collision_authoring::Collider custom;
    custom.shape=collision_authoring::Shape::Capsule;
    custom.dimensions={1.f,2.5f,1.f};
    custom.offset={.25f,0.f,-.1f};
    custom.responses[1]=collision_authoring::Response::Overlap;
    custom.responses[4]=collision_authoring::Response::Ignore;
    custom.responses[7]=collision_authoring::Response::Overlap;
    custom.preset="DoorSensor";
    collision_authoring::assign(source,source.objects[2].node.id,custom);
    collision_mesh::Mesh triangle;
    triangle.source="Models/door.obj";
    triangle.xyz={0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f};
    triangle.indices={0,1,2};
    source.objects[2].node.asset=triangle.source;
    collision_mesh::assign(source,source.objects[2].node.id,triangle);
    collision_cooker::Hull tetra;
    tetra.xyz={0,0,0,1,0,0,0,1,0,0,0,1};
    tetra.indices={0,1,2,0,3,1,0,2,3,1,3,2};
    collision_hulls::assign(source,source.objects[2].node.id,
        collision_hulls::Set{triangle.source,4,{tetra}});
    validate_scene(source);

    auto prefab=capture_prefab(source,1);
    if(prefab.components.size()!=3 ||
       !collision_hulls::lookup(prefab,prefab.objects[1].node.id) ||
       !collision_mesh::lookup(prefab,prefab.objects[1].node.id) ||
       !collision_authoring::lookup(prefab,prefab.objects[1].node.id) ||
       prefab.objects[1].node.collision)
        return 21;
    auto textRoundTrip=parse_scene_v3(serialize_scene_v3(prefab));
    auto restored=collision_authoring::lookup(
        textRoundTrip,textRoundTrip.objects[1].node.id);
    if(!restored || restored->shape!=collision_authoring::Shape::Capsule ||
       restored->dimensions.y!=2.5f || restored->offset.x!=.25f ||
       restored->responses[1]!=collision_authoring::Response::Overlap ||
       restored->responses[4]!=collision_authoring::Response::Ignore ||
       restored->responses[7]!=collision_authoring::Response::Overlap ||
       restored->preset!="DoorSensor" ||
       !collision_hulls::lookup(textRoundTrip,textRoundTrip.objects[1].node.id))
        return 22;
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

    auto instanceCollider1=collision_authoring::lookup(
        source,source.objects[i1+1].node.id);
    auto instanceCollider2=collision_authoring::lookup(
        source,source.objects[i2+1].node.id);
    if(!collision_hulls::lookup(source,source.objects[i1+1].node.id) ||
       !collision_hulls::lookup(source,source.objects[i2+1].node.id) ||
       !collision_mesh::lookup(source,source.objects[i1+1].node.id) ||
       !collision_mesh::lookup(source,source.objects[i2+1].node.id) ||
       !instanceCollider1 || !instanceCollider2 ||
       instanceCollider1->shape!=collision_authoring::Shape::Capsule ||
       instanceCollider2->dimensions.y!=2.5f ||
       source.objects[i2+1].node.collision)
        return 23;

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
    // Engine sharedAssets should provide reusable weapon templates to
    // every mounted project, with legacy transforms kept intact.
    auto const shared=std::filesystem::path(__FILE__).parent_path()
        .parent_path()/"engine/northstarcpp/resources/sharedAssets";
    struct WeaponCase {
        char const* prefab;
        char const* mesh;
        float size;
        float mass;
        float y;
        bool collides;
    };
    WeaponCase const weapon_cases[]={
        {"prefabs/weapon/pistol.nprefab","models/weapon/pistol/pistol.ydd",
            1.f,.65f,0.f,true},
        {"prefabs/weapon/pistol_shell.nprefab",
            "models/weapon/pistol/pistol_shell.ydd",.05f,.006f,0.f,true},
        {"prefabs/weapon/rifle_shell.nprefab",
            "models/weapon/pistol/pistol_shell.ydd",.062f,.011f,0.f,true}
    };
    for(auto const& variant:weapon_cases) {
        if(!std::filesystem::is_regular_file(shared/variant.prefab) ||
           !std::filesystem::is_regular_file(shared/variant.mesh))
            return 30;
        auto const weapon=load_scene(shared/variant.prefab);
        validate_scene(weapon);
        if(weapon.version!=3 ||weapon.objects.size()!=1 ||
           weapon.objects[0].node.parent!=0 ||
           weapon.objects[0].node.asset!=variant.mesh ||
           weapon.objects[0].node.collision!=variant.collides ||
           std::abs(weapon.objects[0].size-variant.size)>1.e-5f ||
           std::abs(weapon.objects[0].mass-variant.mass)>1.e-5f ||
           std::abs(weapon.objects[0].position.y-variant.y)>1.e-5f ||
           weapon.objects[0].node.scale.x!=1.f ||
           weapon.objects[0].node.scale.y!=1.f ||
           weapon.objects[0].node.scale.z!=1.f)
            return 31;
        if(std::string(variant.prefab).find("shell")!=std::string::npos &&
           weapon.objects[0].node.layer!="Weapon debris")
            return 32;
    }

    // Resolve canonical weapon prefabs through the actual VFS overlay in
    // each existing project; project-local historical pistol files must
    // not shadow the new engine-owned weapon/ namespace.
    auto const repository=std::filesystem::path(__FILE__).parent_path()
        .parent_path();
    for(char const* project:{"AbbyPistolDemo","KaylaNightCity",
                             "NorthStarFirstPersonPlatform"}) {
        AssetStore mounted;
        mounted.mount_project_assets(repository/"Projects"/project,shared);
        for(auto const& variant:weapon_cases) {
            // The canonical weapon subtree must resolve in every project.
            auto const bytes=mounted.read(variant.prefab);
            auto const resolved=parse_scene_data(
                std::string(bytes.begin(),bytes.end()));
            validate_scene(resolved);
            if(resolved.objects.size()!=1 ||
               resolved.objects[0].node.asset!=variant.mesh ||
               std::abs(resolved.objects[0].mass-variant.mass)>1.e-5f)
                return 33;
        }
    }
    return 0;
}
