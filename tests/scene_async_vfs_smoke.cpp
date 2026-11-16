// NEF8 index-cache/VFS/async SceneWorld residency smoke.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/assets.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <northstarcpp/scene_world.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace northstarcpp;
namespace fs=std::filesystem;

static Scene demo() {
    Scene world;
    world.version=3;
    world.name="Async partitioned world";
    world.streaming={35.f,60.f,0.f,2,2};
    world.cells.push_back({"Alpha",{-40.f,-5.f,-40.f},{0.f,30.f,0.f},false});
    world.cells.push_back({"Beta",{350.f,-5.f,350.f},{430.f,30.f,430.f},false});
    SceneObject persistent;
    persistent.name="World Base";
    persistent.node.id=10;
    world.objects.push_back(persistent);
    SceneObject a;
    a.name="Alpha Resident";
    a.node.id=20;
    a.node.cell="Alpha";
    a.position={-20.f,0.f,-20.f};
    world.objects.push_back(a);
    SceneObject b;
    b.name="Beta Resident";
    b.node.id=30;
    b.node.cell="Beta";
    b.position={390.f,0.f,390.f};
    world.objects.push_back(b);
    validate_scene(world);
    return world;
}
static bool poll(SceneCellAsyncResidency& file,SceneCellAsyncResidency& vfs,
                 SceneWorld& world,Vector3 camera,std::string const& cell,
                 std::string const& absent) {
    const auto selection_passes=world.streaming_diagnostics().cell_selection_passes;
    for(int i=0;i<300;++i) {
        world.update(camera);
        if(world.streaming_diagnostics().cell_selection_passes!=selection_passes ||
           !world.cell_transitions().empty())
            return false;
        file.apply(world);
        vfs.apply(world);
        if(file.loaded("") && vfs.loaded("") &&
           file.loaded(cell) && vfs.loaded(cell) &&
           !file.loaded(absent) && !vfs.loaded(absent))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
int main(int argc,char** argv) {
    if(argc!=2)return 99;
    try {
        fs::path folder=fs::u8path(argv[1]);
        fs::create_directories(folder);
        auto const path=folder/"async.nscn";
        auto authored=demo();
        save_scene(authored,path);
        AssetStore assets;
        assets.mount_directory("game",folder);
        if(!assets.native_range_reads())
            throw std::runtime_error("Directory VFS did not provide native range ABI");
        SceneChunkReader file(path);
        SceneChunkReader vfs(assets,"game/async.nscn");
        auto index=file.chunks();
        if(index.size()!=3 || vfs.chunks().size()!=3 ||
           index[1].cell!="Alpha" || index[2].cell!="Beta" ||
           !vfs.native_range_reads() || vfs.load("Alpha").size()!=1)
            return 1;
        SceneWorld world(authored);
        SceneCellAsyncResidency disk_resident(path,2);
        SceneCellAsyncResidency vfs_resident(assets,"game/async.nscn",2);
        world.update({-20.f,0.f,-20.f});
        if(!poll(disk_resident,vfs_resident,world,{-20.f,0.f,-20.f},"Alpha","Beta"))
            return 2;
        world.update({390.f,0.f,390.f});
        if(!poll(disk_resident,vfs_resident,world,{390.f,0.f,390.f},"Beta","Alpha"))
            return 3;
        if(disk_resident.records("Beta")->front().entity_id!=30 ||
           vfs_resident.records("Beta")->front().entity_id!=30)
            return 4;
        if(!vfs_resident.error("Alpha").empty() ||
           !disk_resident.error("Beta").empty())
            return 5;
        fs::remove(path);
        return 0;
    }catch(std::exception const& error){
        std::cerr<<error.what()<<'\n';
        return 10;
    }
}
