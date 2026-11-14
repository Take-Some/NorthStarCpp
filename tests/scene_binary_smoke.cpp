// NorthStarCpp binary scene and legacy migration regression.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene.hpp>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
template<class F> bool throws(F&& f) {
    try { f(); } catch(std::exception const&) { return true; }
    return false;
}
}
int main(int argc,char** argv) {
    using namespace northstarcpp;
    if(argc!=3) return 99;
    Scene scene;
    scene.version=3;
    scene.name="Binary Test";
    scene.streaming={150.f,225.f,2.f,4,6};
    scene.layers.push_back({"Custom",false});
    scene.cells.push_back({"District",{0.f,-4.f,0.f},{40.f,60.f,50.f},true});
    scene.volumes.push_back({"Marker",SceneVolume::Shape::sphere,
        {3.f,2.f,1.f},{0.f,0.f,0.f},2.f,"trigger",true});
    scene.portals.push_back({"Door","RoomA","RoomB",
        {0.f,0.f,0.f},{1.f,2.f,1.f},false});
    SceneObject root;
    root.name="Root";
    root.node.id=450;
    root.node.layer="Custom";
    root.node.cell="District";
    root.node.asset="models/workshop.ydd";
    root.node.script="scripts/root.js";
    root.node.room="RoomA";
    root.node.rotation={5.f,10.f,15.f};
    root.node.scale={1.5f,2.f,0.5f};
    root.node.lod_near=10.f;
    root.node.lod_far=700.f;
    root.node.collision=false;
    root.position={11.f,20.f,-5.f};
    root.size=3.f;
    root.color=0x224466;
    root.mass=12.f;
    scene.objects.push_back(root);
    SceneObject child;
    child.name="Door";
    child.node.id=900;
    child.node.parent=450;
    child.position={12.f,20.f,-4.f};
    scene.objects.push_back(child);
    validate_scene(scene);

    auto const b=serialize_scene_binary(scene);
    if(b.size()<64 || b.substr(0,4)!="NEF8" ||
       b.find("NorthStarCpp.Scene")!=std::string::npos) return 1;
    auto decoded=parse_scene_data(b);
    if(decoded.version!=3 || decoded.objects.size()!=2 ||
       decoded.objects[0].node.script!="scripts/root.js" ||
       decoded.objects[0].node.scale.z!=0.5f ||
       decoded.objects[0].node.collision ||
       decoded.objects[1].node.parent!=450 ||
       decoded.layers[0].visible || decoded.cells[0].name!="District" ||
       decoded.volumes[0].tag!="trigger" || decoded.portals[0].open ||
       decoded.streaming.max_cell_unloads_per_update!=6)
        return 2;
    if(serialize_scene_binary(decoded)!=b) return 3;
    // Existing projects continue loading the unmodified NSCN v1 payload.
    std::ifstream fixture(argv[2],std::ios::binary);
    if(!fixture)return 13;
    std::string const old((std::istreambuf_iterator<char>(fixture)),
                          std::istreambuf_iterator<char>());
    if(old.size()<16 || old.compare(0,4,"NSCN")!=0)return 14;
    auto oldScene=parse_scene_binary(old);
    if(oldScene.version!=3 || oldScene.objects.empty())return 15;
    auto migrated=serialize_scene_binary(oldScene);
    if(migrated.compare(0,4,"NEF8")!=0 ||
       serialize_scene_binary(parse_scene_binary(migrated))!=migrated)return 16;

    // A separate old-NSCN fixture will be exercised by the compatibility test.

    std::filesystem::path const path=argv[1];
    save_scene(scene,path);
    auto disk=load_scene(path);
    if(serialize_scene_binary(disk)!=b) return 4;
    std::error_code ec;
    std::filesystem::remove(path,ec);

    // Legacy sources are import-only; saving always produces NSCN binary.
    Scene const legacy=parse_scene_data(
        "NorthStarCpp.Scene 2\n"
        "cube \"Player\" 0 0 0 1.5 46b9d8 1 1\n");
    if(legacy.version!=2) return 5;
    save_scene(legacy,path);
    auto upgraded=load_scene(path);
    if(upgraded.version!=3 || upgraded.objects.size()!=1 ||
       upgraded.objects[0].node.id!=1 || !upgraded.objects[0].player)
        return 6;
    std::filesystem::remove(path,ec);

    if(!throws([&] {parse_scene_binary(b.substr(0,b.size()-1));}))return 7;
    auto corrupt=b;
    corrupt[40]^=static_cast<char>(0xff);
    if(!throws([&] {parse_scene_binary(corrupt);}))return 8;
    auto bad_major=b;
    bad_major[4]=2;
    if(!throws([&] {parse_scene_binary(bad_major);}))return 9;
    auto bad_length=b;
    bad_length[8]=0;
    if(!throws([&] {parse_scene_binary(bad_length);}))return 10;
    auto extra=b;
    extra.push_back('\0');
    if(!throws([&] {parse_scene_binary(extra);}))return 11;
    if(!throws([&] {serialize_scene_binary(legacy);}))return 12;
    return 0;
}
