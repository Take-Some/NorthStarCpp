// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Scene v3 compatibility and independent streaming/spatial regression suite.

#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_world.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace northstarcpp;

namespace {
void check(bool value, char const* message)
{
    if (!value) throw std::runtime_error(message);
}
template <class Task>
void rejects(Task task, char const* message)
{
    bool rejected=false;
    try {task();}
    catch (std::exception const&) {rejected=true;}
    check(rejected,message);
}
Scene build()
{
    Scene scene;
    scene.version=3;
    scene.name="Night City \xE2\x80\x94 Expanded World";
    scene.streaming={150.f,230.f,1.25f};
    scene.layers={{"Default",true},{"World",true},{"Debug",false}};
    scene.cells={
        {"Watson",{-40.f,-10.f,-40.f},{40.f,20.f,40.f},false},
        {"Westbrook",{900.f,-10.f,-40.f},{980.f,20.f,40.f},false},
    };
    scene.volumes={
        {"Streaming Trigger",SceneVolume::Shape::sphere,{0.f,0.f,0.f},
            {0.f,0.f,0.f},18.f,"streaming_source",true},
        {"Interior A",SceneVolume::Shape::box,{-10.f,-5.f,-10.f},
            {10.f,10.f,10.f},0.f,"interior",true},
    };
    scene.portals={
        {"Lobby Door","Lobby","Hall",{-2.f,0.f,-2.f},{2.f,3.f,2.f},true},
        {"Hall Exterior","Hall","Exterior",{-2.f,0.f,-6.f},{2.f,3.f,-2.f},false},
    };
    SceneObject player;
    player.name="Player";
    player.player=true;
    player.mass=2.f;
    player.node.id=1;
    player.node.layer="World";
    player.node.cell="Watson";
    player.node.room="Lobby";
    player.node.asset="characters/player.ydd";

    SceneObject child;
    child.name="Furniture";
    child.position={3.f,0.f,1.f};
    child.node.id=2;
    child.node.parent=1;
    child.node.layer="World";
    child.node.cell="Watson";
    child.node.room="Hall";
    child.node.asset="props/table.ydd";
    child.node.script="scripts/behaviors/furniture.js";
    child.node.scale={1.f,2.f,1.f};
    child.node.rotation={0.f,45.f,0.f};
    child.node.lod_far=75.f;

    SceneObject distant;
    distant.name="Tower";
    distant.position={920.f,0.f,0.f};
    distant.node.id=3;
    distant.node.layer="World";
    distant.node.cell="Westbrook";
    distant.node.asset="buildings/tower.ydd";

    SceneObject hidden;
    hidden.name="Editor Helper";
    hidden.position={0.f,1.f,0.f};
    hidden.node.id=4;
    hidden.node.layer="Debug";
    hidden.node.visible=true;
    hidden.node.cell="Watson";

    SceneObject persistent;
    persistent.name="Persistent Floor";
    persistent.position={0.f,-6.f,0.f};
    persistent.node.id=5;
    persistent.node.layer="Default";
    persistent.node.collision=true;
    scene.objects={player,child,distant,hidden,persistent};
    validate_scene(scene);
    return scene;
}
}
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try {
        auto const root=fs::u8path(argv[1]);
        fs::create_directories(root);
        auto original=build();
        auto const bytes=serialize_scene_v3(original);
        check(bytes.rfind("NorthStarCpp.Scene 3\n",0)==0,
              "v3 header incorrect");
        auto parsed=parse_scene_data(bytes);
        check(parsed.version==3 && parsed.name==original.name,
              "v3 metadata was not parsed");
        check(parsed.objects.size()==original.objects.size() &&
              parsed.cells.size()==2 && parsed.portals.size()==2 &&
              parsed.volumes.size()==2 && parsed.layers.size()==3,
              "v3 structural sections were not preserved");
        check(parsed.objects[1].node.parent==1 &&
              parsed.objects[1].node.scale.y==2.f &&
              parsed.objects[1].node.rotation.y==45.f &&
              parsed.objects[1].node.asset=="props/table.ydd" &&
              parsed.objects[1].node.script=="scripts/behaviors/furniture.js",
              "v3 entity hierarchy/asset/transform lost");
        auto const destination=root/"Expanded.nscn";
        save_scene(parsed,destination);
        auto reopened=load_scene(destination);
        check(serialize_scene_v3(reopened)==bytes,
              "v3 roundtrip is not deterministic");

        SceneWorld world(reopened);
        world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"Lobby");
        check(world.cell_loaded("Watson") && !world.cell_loaded("Westbrook") &&
              world.loaded_cell_count()==1, "near cell residence incorrect");
        check(world.object_visible(0) && world.object_visible(1) &&
              !world.object_visible(2) && !world.object_visible(3),
              "layer, parent, portal, and cell visibility incorrect");
        check(world.object_resident(4) && world.object_visible(4)==false,
              "interior portal failed to exclude exterior persistent visuals");
        auto near=world.query_sphere({0.f,0.f,0.f},6.f);
        check(std::find(near.begin(),near.end(),0)!=near.end() &&
              std::find(near.begin(),near.end(),2)==near.end(),
              "spatial sphere index returned wrong candidates");
        auto box=world.query_box({900.f,-5.f,-5.f},{940.f,5.f,5.f});
        check(box.size()==1 && box[0]==2,"spatial box index failed");
        check(world.volume_contains("Streaming Trigger",{0.f,0.f,0.f}) &&
              world.volume_contains("Interior A",{1.f,1.f,1.f}) &&
              !world.volume_contains("Interior A",{300.f,0.f,0.f}),
              "scene volume query incorrect");
        auto deps=world.asset_dependencies();
        check(deps.size()==2 && deps[0]=="characters/player.ydd" &&
              deps[1]=="props/table.ydd",
              "active virtual asset dependencies were not prioritized");

        world.set_portal_open("Hall Exterior",true);
        world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"Lobby");
        check(world.object_visible(4),"open portal failed to reveal exterior");
        world.set_portal_open("Hall Exterior",false);
        world.set_layer_visible("World",false);
        world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"Lobby");
        check(world.object_visible(0) && !world.object_visible(1),
              "layer mask did not preserve player residency");

        world.set_layer_visible("World",true);
        world.update({0.f,0.f,0.f},{800.f,0.f,0.f},"Lobby");
        check(world.cell_loaded("Westbrook"),"velocity lookahead did not prefetch cell");
        world.update({920.f,0.f,0.f},{0.f,0.f,0.f},"Exterior");
        check(!world.cell_loaded("Watson") &&
               world.cell_loaded("Westbrook") &&
               world.object_visible(2), "fast travel failed to exchange cell residency");
        world.pin_cell("Watson",true);
        world.update({920.f,0.f,0.f});
        check(world.cell_loaded("Watson"),"explicit streaming pin ignored");
        world.pin_cell("Watson",false);
        world.update({920.f,0.f,0.f});
        check(!world.cell_loaded("Watson"),"unpin did not allow unload");
        check(world.update_generation()>=7,"streaming generation not advanced");

        {
            auto broken=original;
            broken.objects[1].node.parent=3;
            broken.objects[2].node.parent=2;
            rejects([&]{validate_scene(broken);},"hierarchy cycle accepted");
        }
        {
            auto broken=original;
            broken.objects[2].node.id=2;
            rejects([&]{validate_scene(broken);},"duplicate stable ID accepted");
        }
        {
            auto broken=original;
            broken.objects[2].node.asset="../escape.mesh";
            rejects([&]{validate_scene(broken);},"unsafe virtual path accepted");
        }
        {
            auto broken=original;
            broken.cells[0].minimum.x=200;
            rejects([&]{validate_scene(broken);},"invalid cell AABB accepted");
        }
        {
            auto broken=original;
            broken.streaming.unload_radius=100.f;
            rejects([&]{validate_scene(broken);},"invalid streaming hysteresis accepted");
        }
        {
            auto broken=original;
            broken.objects[1].node.script="../escape.js";
            rejects([&]{validate_scene(broken);},"unsafe script path accepted");
        }
        rejects([&]{parse_scene_data(bytes+"script 2 \"scripts/duplicate.js\"\n");},
                "duplicate script component accepted");
        rejects([&]{parse_scene_data(bytes+"script 999 \"scripts/missing.js\"\n");},
                "orphan script component accepted");
        rejects([&]{parse_scene_data(bytes+"unknown \"evil\"\n");},
                "unknown v3 record accepted");
        rejects([&]{parse_scene_data("NorthStarCpp.Scene 3\nentity -1 0 \"X\"\n");},
                "negative stable ID accepted");
        {
            // Scene files authored before streaming budgets existed must
            // continue to open without explicit conversion.
            auto old_v3=bytes;
            auto const start=old_v3.find("stream_budget ");
            check(start!=std::string::npos,"v3 writer omitted streaming budgets");
            old_v3.erase(start,old_v3.find('\n',start)-start+1);
            auto previous=parse_scene_data(old_v3);
            check(previous.version==3 &&
                  previous.streaming.max_cell_loads_per_update==64 &&
                  previous.streaming.max_cell_unloads_per_update==64,
                  "existing v3 scene without budgets became incompatible");
            auto invalid=original;
            invalid.streaming.max_cell_loads_per_update=0;
            rejects([&]{validate_scene(invalid);},"zero stream budget accepted");
        }
        {
            // Moving NPCs and vehicles must update LOD decisions and spatial
            // queries without rebuilding the source ECS entity order.
            SceneWorld dynamic_world(original);
            auto live=original;
            dynamic_world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"",&live);
            auto const initial_near=dynamic_world.query_sphere({3.f,0.f,1.f},4.f);
            check(dynamic_world.object_visible(1) &&
                  std::find(initial_near.begin(),initial_near.end(),1)!=initial_near.end(),
                  "authored child initially missing from the spatial index");
            live.objects[1].position={350.f,0.f,0.f};
            dynamic_world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"",&live);
            auto const moved_near=dynamic_world.query_sphere({350.f,0.f,0.f},3.f);
            check(!dynamic_world.object_visible(1) &&
                  std::find(moved_near.begin(),moved_near.end(),1)!=moved_near.end(),
                  "moving entity did not update its spatial bucket and LOD");
            auto far_index=dynamic_world.query_box({345.f,-2.f,-3.f},
                                                   {354.f,2.f,3.f});
            check(std::find(far_index.begin(),far_index.end(),1)!=far_index.end(),
                  "moving entity is absent from box query");
            // The authored tower changes sector at runtime without changing
            // its stable ID or serialized Watson/Westbrook ownership.
            live.objects[2].position={10.f,0.f,0.f};
            dynamic_world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"",&live);
            check(dynamic_world.effective_cell_for_object(2)=="Watson" &&
                  dynamic_world.object_resident(2) &&
                  dynamic_world.object_visible(2),
                  "moving object disappeared on a streaming cell boundary");
            live.objects[2].position={920.f,0.f,0.f};
            dynamic_world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"",&live);
            check(dynamic_world.effective_cell_for_object(2)=="Westbrook" &&
                  !dynamic_world.object_resident(2),
                  "moving object failed to re-enter its distant cell");
            std::swap(live.objects[1],live.objects[2]);
            rejects([&]{
                dynamic_world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"",&live);
            },"reordered live scene was accepted");
            auto last_valid=dynamic_world.query_sphere({350.f,0.f,0.f},3.f);
            check(std::find(last_valid.begin(),last_valid.end(),1)!=last_valid.end(),
                  "invalid dynamic update corrupted the spatial index");
            live=original;
            dynamic_world.update({0.f,0.f,0.f},{0.f,0.f,0.f},"",&live);
            check(dynamic_world.object_visible(1) &&
                  dynamic_world.source().objects[1].position.x==3.f,
                  "dynamic spatial index did not restore authored position");
        }
        {
            // Stress the priority queue without ECS or a concrete physics
            // provider. Pins load immediately, ordinary loads are capped.
            Scene scheduled;
            scheduled.version=3;
            scheduled.name="Streaming Priority Workload";
            scheduled.streaming.load_radius=1000.f;
            scheduled.streaming.unload_radius=1200.f;
            scheduled.streaming.max_cell_loads_per_update=1;
            scheduled.streaming.max_cell_unloads_per_update=1;
            auto const scheduled_bytes=serialize_scene_v3(scheduled);
            auto const reloaded_budget=parse_scene_data(scheduled_bytes);
            check(reloaded_budget.streaming.max_cell_loads_per_update==1 &&
                  reloaded_budget.streaming.max_cell_unloads_per_update==1,
                  "custom streaming work budgets did not round-trip");
            scheduled.cells={
                {"NearA",{-5.f,-5.f,-5.f},{5.f,5.f,5.f},false},
                {"NearB",{40.f,-5.f,-5.f},{50.f,5.f,5.f},false},
                {"NearC",{80.f,-5.f,-5.f},{90.f,5.f,5.f},false},
                {"NearD",{120.f,-5.f,-5.f},{130.f,5.f,5.f},false},
                {"Forced",{810.f,-5.f,-5.f},{820.f,5.f,5.f},true}
            };
            SceneWorld planner(scheduled);
            planner.update({0.f,0.f,0.f});
            check(planner.loaded_cell_count()==2 &&
                  planner.cell_loaded("NearA") &&
                  planner.cell_loaded("Forced") &&
                  planner.streaming_diagnostics().pending_loads==3,
                  "prioritized cell load budget or mandatory cell failed");
            check(planner.cell_transitions().size()==2 &&
                  planner.cell_transitions()[0].loaded &&
                  planner.cell_transitions()[0].forced,
                  "forced load must be first and reported");
            planner.update({0.f,0.f,0.f});
            check(planner.cell_loaded("NearB") &&
                  planner.streaming_diagnostics().pending_loads==2,
                  "second-nearest cell was not prioritized");
            planner.update({0.f,0.f,0.f});
            planner.update({0.f,0.f,0.f});
            check(planner.loaded_cell_count()==5 &&
                  planner.streaming_diagnostics().pending_loads==0,
                  "streaming pending work did not converge");
            planner.update({5000.f,0.f,0.f});
            check(planner.loaded_cell_count()==4 &&
                  planner.cell_loaded("Forced") &&
                  planner.streaming_diagnostics().pending_unloads==3,
                  "unload budget should release only one non-forced cell");
            planner.update({5000.f,0.f,0.f});
            planner.update({5000.f,0.f,0.f});
            planner.update({5000.f,0.f,0.f});
            check(planner.loaded_cell_count()==1 &&
                  planner.cell_loaded("Forced") &&
                  planner.streaming_diagnostics().pending_unloads==0,
                  "mandatory cell evicted or unload work failed to converge");
            planner.pin_cell("NearA",true);
            planner.update({5000.f,0.f,0.f});
            check(planner.cell_loaded("NearA") &&
                  planner.cell_transitions().front().forced,
                  "explicit pinned cell should bypass the regular load budget");
            check(planner.streaming_diagnostics().loaded_cells==2 &&
                  planner.streaming_diagnostics().generation>=9,
                  "scene diagnostic counters do not match actual residency");
        }
        {
            // Coarse distance bands use a 64m and 256m proxy hierarchy.
            // Stable scene indices remain intact, and no player/dynamic
            // object may enter the HLOD static cluster.
            Scene scene;
            scene.version=3;
            scene.name="Two-tier HLOD Geometry";
            scene.streaming={1000.f,1400.f,1.f};
            scene.cells={{"SectorA",{-10.f,-10.f,-10.f},{140.f,25.f,50.f},false}};
            for (std::uint64_t i=0;i<12;++i) {
                SceneObject object;
                object.name="Static facade";
                object.node.id=i+1;
                object.node.cell="SectorA";
                object.position={static_cast<float>(i%6)*12.f,0.f,
                                 static_cast<float>(i/6)*12.f};
                object.mass=0.f;
                object.color=0x607080u;
                scene.objects.push_back(object);
            }
            validate_scene(scene);
            SceneWorld hlod(scene);
            check(hlod.hlod_cluster_count()>=2,"HLOD hierarchy not generated");
            hlod.update({0.f,0.f,0.f});
            check(hlod.hlod_proxies().empty() &&
                  hlod.visible_indices().size()==12,
                  "near HLOD replaced detailed geometry");
            hlod.update({180.f,0.f,0.f});
            check(!hlod.hlod_proxies().empty() &&
                  hlod.hlod_proxies().front().level==1 &&
                  hlod.hlod_proxies().front().member_count==12,
                  "midrange HLOD did not select level 1 proxy");
            hlod.update({450.f,0.f,0.f});
            check(hlod.hlod_proxies().size()==1 &&
                  hlod.hlod_proxies()[0].level==2 &&
                  hlod.hlod_proxies()[0].member_count==12 &&
                  hlod.visible_indices().empty(),
                  "longrange HLOD failed to replace distant static bodies");
            check(hlod.source().objects.size()==12 &&
                  hlod.resident_indices().size()==12,
                  "HLOD incorrectly removed authored physics/ECS data");
            hlod.set_layer_visible("Default",false);
            hlod.update({450.f,0.f,0.f});
            check(hlod.hlod_proxies().empty(),
                  "HLOD leaked objects through an invisible layer");
        }
        {
            auto legacy=make_startup_scene();
            auto const file=root/"Legacy.nscn";
            save_scene(legacy,file);
            auto reopened_legacy=load_scene(file);
            if (!(reopened_legacy.version==3 && reopened_legacy.objects.size()==4 &&
                  reopened_legacy.objects[0].node.id==1)) {
                std::cerr << "Legacy actual version=" << reopened_legacy.version
                          << " count=" << reopened_legacy.objects.size()
                          << " first_id=" << (reopened_legacy.objects.empty() ? 0 :
                             reopened_legacy.objects[0].node.id) << '\n';
                throw std::runtime_error("legacy scene compatibility broken");
            }
        }
        fs::remove_all(root);
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "SceneWorld V3 smoke: " << error.what() << '\n';
        return 1;
    }
}
