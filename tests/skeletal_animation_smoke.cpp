#include <northstarcpp/skeletal_animation.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace northstarcpp;

static bool close(float a,float b) {return std::abs(a-b)<0.0002f;}
template<typename Fn> static void must_reject(Fn&& fn) {
    bool rejected=false;
    try {fn();}catch(std::exception const&) {rejected=true;}
    if(!rejected) throw std::runtime_error("Invalid skeleton or skin data was accepted");
}
int main() {
    try {
        auto rig=parse_skeleton_rig(R"({
            "schema":1,"type":"skeleton",
            "bones":[
                {"name":"root","parent":-1},
                {"name":"arm","parent":0,"translation":[1,0,0]}
            ]
        })");
        if(rig.bone_count()!=2)throw std::runtime_error("Skeleton hierarchy lost");
        auto rest=rig.skinning_palette({},0.f);
        auto rest_position=rest[1].transform_position({1,2,3});
        if(!close(rest_position.x,1) || !close(rest_position.y,2))
            throw std::runtime_error("Bind palette is not identity");
        SceneAnimationClip child_track;
        SceneAnimationKeyframe from{},to{};
        to.time=1.f;
        to.pose.translation={2.f,0.f,0.f};
        child_track.set_keyframes({from,to});
        auto moved=rig.skinning_palette({SceneAnimationClip{},child_track},0.5f);
        auto packed=pack_skinning_palette(moved);
        if(packed.size()!=32 || !close(moved[1].transform_position({1,2,3}).x,2))
            throw std::runtime_error("Animated child bone palette is wrong");
        SkinnedVertex one{};
        one.vertex.position={1.f,2.f,3.f};
        one.vertex.normal={0.f,1.f,0.f};
        one.skin.bone={1,0,0,0};
        one.skin.weight={1.f,0.f,0.f,0.f};
        auto mesh=skin_mesh_cpu({one,one,one},{0,1,2},moved);
        if(mesh.vertices.size()!=3 || !close(mesh.vertices[0].position.x,2.f) ||
           !close(mesh.vertices[0].normal.y,1.f))
            throw std::runtime_error("Reference CPU skinning mismatched palette");
        must_reject([] {
            SkeletonRig invalid;
            invalid.set_bones({{"root",1,{}}});
        });
        must_reject([] {
            SkeletonRig invalid;
            invalid.set_bones({{"root",-1,{}},{"root",0,{}}});
        });
        must_reject([&] {
            auto invalid=one;
            invalid.skin.bone[0]=2;
            skin_mesh_cpu({invalid,invalid,invalid},{0,1,2},moved);
        });
        must_reject([&] {
            auto invalid=one;
            invalid.skin.weight={0.5f,0.f,0.f,0.f};
            skin_mesh_cpu({invalid,invalid,invalid},{0,1,2},moved);
        });
        must_reject([&] {rig.skinning_palette({},-1.f);});
        must_reject([&] {pack_skinning_palette({});});
        std::cout<<"Skeletal rig, bind pose, animation palette, CPU skinning PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"Skeletal animation regression: "<<e.what()<<'\n';
        return 1;
    }
}
