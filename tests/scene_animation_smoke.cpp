#include "qa_scene_fixture.hpp"
// NorthStarCpp — scene animation contract and gameplay-independent playback
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_animation.hpp>
#include <cmath>
#include <functional>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>

using namespace northstarcpp;
namespace {
void check(bool ok,char const* message) {
    if(!ok) throw std::runtime_error(message);
}
bool near(float x,float y,float tolerance=0.0001f) {
    return std::abs(x-y)<=tolerance;
}
void rejects(std::function<void()> const& f,char const* reason) {
    bool rejected=false;
    try {f();} catch(std::exception const&) {rejected=true;}
    check(rejected,reason);
}
std::string valid_clip=R"JSON({
  "schema":1,
  "type":"scene-transform",
  "interpolation":"linear",
  "keyframes":[
    {"time":0,"translation":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},
    {"time":2,"translation":[4,2,-2],"rotation":[0,90,0],"scale":[2,1,1]}
  ]
})JSON";
Scene test_scene() {
    Scene scene;
    scene.version=3;
    SceneObject actor;
    actor.name="Animated elevator";
    actor.node.id=41;
    actor.position={10.f,3.f,-5.f};
    actor.node.rotation={0.f,15.f,0.f};
    actor.node.scale={1.f,2.f,3.f};
    actor.mass=0.f;
    scene.objects.push_back(actor);
    return scene;
}
}
int main(int argc,char** argv) {
    try {
        auto clip=parse_scene_animation_clip(valid_clip);
        check(clip.keyframe_count()==2 && near(clip.duration(),2.f),"Clip metadata");
        auto pose=clip.sample(1.f);
        check(near(pose.translation.x,2.f) &&
              near(pose.translation.y,1.f) && near(pose.translation.z,-1.f),
              "Linear position interpolation");
        check(near(pose.rotation.y,45.f) && near(pose.scale.x,1.5f),
              "Rotation and scale interpolation");
        check(near(clip.sample(200.f).translation.x,4.f),
              "Sample after duration must clamp");

        auto stepped=valid_clip;
        auto const pos=stepped.find("\"linear\"");
        stepped.replace(pos,8,"\"step\"");
        auto step_clip=parse_scene_animation_clip(stepped);
        check(near(step_clip.sample(1.f).translation.x,0.f) &&
              near(step_clip.sample(2.f).translation.x,4.f),
              "Step interpolation");

        auto eased=valid_clip;
        eased.replace(pos,8,"\"fermi-dirac\"");
        auto ease_clip=parse_scene_animation_clip(eased);
        check(ease_clip.interpolation()==SceneAnimationInterpolation::fermi_dirac &&
              ease_clip.sample(0.5f).translation.x < clip.sample(0.5f).translation.x &&
              near(ease_clip.sample(1.f).translation.x,2.f) &&
              ease_clip.sample(1.5f).translation.x > clip.sample(1.5f).translation.x &&
              near(ease_clip.sample(2.f).translation.x,4.f),
              "Foundation Fermi-Dirac transition");

        auto scene=test_scene();
        SceneAnimationPlayer player;
        player.play(scene,41,clip,false,1.f);
        check(player.count()==1 && player.active(41),"Play clip");
        for (int frame=0;frame<3;++frame) player.advance(0.25f);
        auto first=player.advance(0.25f);
        check(first.size()==1 && first[0].entity_id==41 &&
              first[0].scene_index==0 && near(first[0].position.x,12.f) &&
              near(first[0].position.y,4.f) &&
              near(first[0].rotation.y,60.f) &&
              near(first[0].scale.x,1.5f) && near(first[0].scale.y,2.f),
              "Animation transforms are relative to authored base");
        player.pause(41,true);
        auto paused=player.advance(0.2f);
        check(near(paused[0].position.x,12.f),"Paused animation must not advance");
        player.pause(41,false);
        player.seek(41,2.f);
        auto final=player.advance(0.1f);
        check(near(final[0].position.x,14.f) && player.status(41).finished,
              "Final non-looped pose is retained");

        player.play(scene,41,clip,true,2.f);
        auto looping=player.advance(0.25f);
        check(near(looping[0].position.x,11.f),"Playback speed");
        for(int i=0;i<8;++i) player.advance(0.25f);
        check(!player.status(41).finished && player.status(41).time<2.f,
              "Loop playback wraps safely");
        player.stop(41);
        check(!player.active(41) && player.advance(0.f).empty(),
              "Stop releases animation instance");

        // Pre-sorted timeline and caller-owned samples: no frame-by-frame
        // sort, output vector allocation, or unstable order after hot replace.
        auto many=test_scene();
        auto second=many.objects.front(),third=many.objects.front();
        second.node.id=42;
        third.node.id=43;
        many.objects.push_back(second);
        many.objects.push_back(third);
        SceneAnimationPlayer batch;
        batch.play(many,43,clip);
        batch.play(many,41,clip);
        batch.play(many,42,clip);
        std::vector<SceneAnimationSample> reused;
        batch.advance_into(.1f,reused);
        check(reused.size()==3 && reused[0].entity_id==41 &&
              reused[1].entity_id==42 && reused[2].entity_id==43,
              "Scene animation ordering must follow scene indices");
        auto const* stable=reused.data();
        for(int i=0;i<100;++i)batch.advance_into(.01f,reused);
        check(reused.data()==stable,
              "Scene animation samples reallocated during steady playback");
        batch.play(many,41,clip);
        batch.stop(42);
        batch.advance_into(0.f,reused);
        check(reused.size()==2 && reused[0].entity_id==41 &&
              reused[1].entity_id==43,
              "Hot-replace and stop must maintain sorted animation order");
        batch.clear();
        batch.advance_into(0.f,reused);
        check(reused.empty(),"Clear must release all ordered animation state");

        rejects([&] {player.play(scene,42,clip);},"Unknown entity accepted");
        rejects([&] {player.play(scene,41,clip,false,0.f);},"Zero speed accepted");
        rejects([&] {player.advance(0.5f);},"Excessive frame delta accepted");
        rejects([&] {clip.sample(std::nanf(""));},"Invalid sample accepted");
        auto dynamic=test_scene();
        dynamic.objects[0].mass=2.f;
        rejects([&] {player.play(dynamic,41,clip);},"Dynamic physics body accepted");
        auto player_scene=test_scene();
        scene_pawn::migrate_imported_local(player_scene,41);
        rejects([&] {player.play(player_scene,41,clip);},"Player accepted");
        rejects([&] {
            auto invalid=valid_clip;
            invalid.replace(invalid.find("\"schema\":1"),10,"\"schema\":2");
            parse_scene_animation_clip(invalid);
        },"Invalid schema accepted");
        rejects([&] {
            auto invalid=valid_clip;
            invalid.replace(invalid.find("\"time\":2"),8,"\"time\":0");
            parse_scene_animation_clip(invalid);
        },"Duplicate times accepted");
        rejects([&] {
            auto invalid=valid_clip;
            invalid.replace(invalid.find("\"scale\":[2,1,1]"),15,"\"scale\":[-1,1,1]");
            parse_scene_animation_clip(invalid);
        },"Negative scale accepted");
        rejects([&] {parse_scene_animation_clip(std::string(1024*1024+1,'x'));},
                "Oversized clip accepted");
        if (argc==3) {
            auto authored=load_scene(argv[1]);
            check(authored.version==3 && authored.objects.size()>=3,
                  "Animation Lab scene V3 cannot be loaded");
            std::ifstream file(argv[2],std::ios::binary);
            check(file.good(),"Animation Lab clip missing");
            auto source=std::string(std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>());
            auto authored_clip=parse_scene_animation_clip(source);
            SceneAnimationPlayer demo;
            demo.play(authored,3,authored_clip,true,1.f);
            auto animated=demo.advance(0.25f);
            check(animated.size()==1 && animated.front().entity_id==3 &&
                  animated.front().position.y>authored.objects[2].position.y,
                  "Animation Lab scene/clip integration failed");
        }
        std::cout<<"Scene transform animation PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
