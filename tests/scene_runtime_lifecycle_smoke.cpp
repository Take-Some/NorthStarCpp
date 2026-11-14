// Scene V3 verified NEF8 payload -> live ECS / Bullet/Jolt residency regression.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_world.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <filesystem>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <string>
#include <iostream>

using namespace northstarcpp;
static Scene make_world()
{
    Scene scene;
    scene.version=3;
    scene.name="NEF8 Live Resource Life Cycle";
    scene.streaming={25.f,40.f,0.f,2,2};
    scene.cells={
        {"Near",{-25.f,-10.f,-25.f},{25.f,20.f,25.f},false},
        {"Far",{100.f,-10.f,-25.f},{150.f,20.f,25.f},false}
    };
    SceneObject player;
    player.name="Driver";
    player.node.id=1;
    player.node.cell="Near";
    player.player=true;
    player.mass=1.f;
    player.position={0.f,0.f,0.f};
    SceneObject near;
    near.name="Near Static";
    near.node.id=2;
    near.node.cell="Near";
    near.position={10.f,1.f,0.f};
    SceneObject far=near;
    far.name="Far Static";
    far.node.id=3;
    far.node.cell="Far";
    far.position={125.f,1.f,0.f};
    SceneObject persistent=near;
    persistent.name="Persistent Terrain";
    persistent.node.id=4;
    persistent.node.cell.clear();
    persistent.position={50.f,-10.f,0.f};
    SceneObject dynamic=near;
    dynamic.name="Moving NPC";
    dynamic.node.id=5;
    dynamic.mass=5.f;
    dynamic.position={18.f,0.f,0.f};
    scene.objects={player,near,far,persistent,dynamic};
    validate_scene(scene);
    return scene;
}
static void require(bool good,char const* why) {
    if(!good)throw std::runtime_error(why);
}
static void exercise_verified_disk_payload(std::filesystem::path const& path)
{
    namespace fs=std::filesystem;
    auto source=make_world();
    save_scene(source,path);
    SceneWorld world(source);
    PhysicsScene runtime(source);
    SceneCellAsyncResidency disk(path,2);
    world.enable_payload_gate(true);
    auto tick=[&](Vector3 camera) {
        world.update(camera);
        disk.apply(world);
        for (auto const& cell:source.cells)
            world.set_cell_payload_ready(cell.name,
                !disk.has_chunk(cell.name) || disk.loaded(cell.name));
        (void)runtime.sync_scene_residency(world,camera);
    };
    auto wait_for=[&](Vector3 camera,std::string const& target,
                      std::size_t expected) {
        for(int i=0;i<400;++i) {
            tick(camera);
            if(world.object_resident(expected) && disk.loaded(target) &&
               runtime.ecs_resident_entity_count()==4)return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("NEF8 async block did not instantiate live ECS");
    };
    tick({0.f,0.f,0.f});
    require(runtime.ecs_resident_entity_count()==3,
        "Unverified on-disk NEF8 block instantiated static ECS");
    wait_for({0.f,0.f,0.f},"Near",1);
    tick({125.f,0.f,0.f});
    require(runtime.ecs_resident_entity_count()==3,
        "Fast-travel without sector payload failed to unload ECS");
    wait_for({125.f,0.f,0.f},"Far",2);
    tick({0.f,0.f,0.f});
    wait_for({0.f,0.f,0.f},"Near",1);
    require(!disk.loaded("Far"),
            "Inactive disk sector remained decoded after fast travel");
    std::error_code error;
    fs::remove(path,error);
}

int main(int argc,char** argv)
{
    try {
        if(argc==2)exercise_verified_disk_payload(
            std::filesystem::u8path(argv[1]));
        auto source=make_world();
        SceneWorld world(source);
        PhysicsScene runtime(source);
        require(runtime.supports_scene_residency(),
                "Physics provider lacks static residency contract");
        require(runtime.ecs_resident_entity_count()==5,"Initial ECS contract changed");
        world.enable_payload_gate(true);
        // The logical load is an I/O request, not evidence of a verified block.
        world.update({0.f,0.f,0.f});
        require(world.cell_loaded("Near") && !world.cell_payload_ready("Near"),
                "Logical cell incorrectly implies payload readiness");
        runtime.sync_scene_residency(world,{0.f,0.f,0.f});
        require(runtime.ecs_resident_entity_count()==3,
                "Unverified streaming cell still has live ECS entities");

        world.set_cell_payload_ready("Near",true);
        world.update({0.f,0.f,0.f});
        runtime.sync_scene_residency(world,{0.f,0.f,0.f});
        require(runtime.ecs_resident_entity_count()==4,
                "Verified near-cell ECS entity was not instantiated");
        runtime.step(0.016f);
        require(runtime.ecs_resident_entity_count()==4,
                "Sparse ECS position sync failed after physics step");

        // Fast travel into a target before its NEF8 block finishes. Physics
        // may keep a protective nearby collider, but ECS cannot instantiate.
        world.update({125.f,0.f,0.f});
        runtime.sync_scene_residency(world,{125.f,0.f,0.f});
        require(runtime.ecs_resident_entity_count()==3 &&
                !world.object_resident(2),
                "Unverified destination resurrected a streaming ECS entity");
        world.set_cell_payload_ready("Far",true);
        world.update({125.f,0.f,0.f});
        runtime.sync_scene_residency(world,{125.f,0.f,0.f});
        require(runtime.ecs_resident_entity_count()==4 &&
                world.object_resident(2),
                "Fast travel did not construct destination entity");
        runtime.step(0.016f);

        // Repeated unload/reload cycles must not leak ECS handles, destroy
        // active dynamic traffic, or change authored physics object indices.
        for(int i=0;i<16;++i) {
            bool near_cell=(i%2)==0;
            auto destination=near_cell ? Vector3{0.f,0.f,0.f} :
                                         Vector3{125.f,0.f,0.f};
            world.update(destination);
            runtime.sync_scene_residency(world,destination);
            require(runtime.ecs_resident_entity_count()==4,
                    "Fast-travel ECS count drift or handle leak");
            runtime.step(0.016f);
            require(runtime.scene().objects[2].node.id==3 &&
                    runtime.scene().objects[4].node.id==5,
                    "Fast travel altered stable authored entity IDs");
        }
        world.set_cell_payload_ready("Near",false);
        world.set_cell_payload_ready("Far",false);
        world.update({0.f,0.f,0.f});
        runtime.sync_scene_residency(world,{0.f,0.f,0.f});
        require(runtime.ecs_resident_entity_count()==3,
                "Closing verified blocks did not unload static ECS");
        bool rejected=false;
        try{world.set_cell_payload_ready("Unknown",true);}
        catch(std::invalid_argument const&){rejected=true;}
        require(rejected,"Unknown payload cell accepted");
        return 0;
    } catch(std::exception const& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
