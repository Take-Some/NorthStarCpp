// Camera Director: deterministic source priorities, transitions and VFS presets.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/assets.hpp>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace northstarcpp;
namespace {
void check(bool value,char const* reason) {
    if(!value)throw std::runtime_error(reason);
}
bool near(float a,float b,float tolerance=.003f) {
    return std::abs(a-b)<=tolerance;
}
CameraShot shot(std::string name,int priority,float x,float blend=0.f) {
    CameraShot result;
    result.source=std::move(name);
    result.priority=priority;
    result.blend_seconds=blend;
    result.pose.position={x,3.f,-6.f};
    result.pose.yaw=.2f;
    result.pose.pitch=-.12f;
    result.pose.fov_y_degrees=65.f;
    return result;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Camera preset VFS test requires repository root");
        CameraDirector director;
        CameraTarget target;
        target.eye={0.f,1.6f,0.f};
        target.ignore_object=0;
        director.update(0.f,target);
        director.push_shot(shot("ui.preview",10,14.f));
        check(director.shot_count()==1,"First shot was not registered");
        auto pose=director.update(.016f,target);
        check(director.active_shot()=="ui.preview" &&
              near(pose.position.x,14.f) &&
              near(pose.fov_y_degrees,65.f),
              "Cinema shot did not override active gameplay camera");
        director.push_shot(shot("scene.cutscene",20,30.f));
        pose=director.update(.016f,target);
        check(director.active_shot()=="scene.cutscene" &&
              near(pose.position.x,30.f),
              "Camera director did not select higher priority shot");
        director.push_shot(shot("vehicle.script",20,44.f));
        pose=director.update(.016f,target);
        check(director.active_shot()=="vehicle.script" &&
              near(pose.position.x,44.f),
              "Priority ties did not pick most recently registered request");
        director.push_shot(shot("scene.cutscene",20,28.f));
        check(director.shot_count()==3,
              "Repeated source pushed duplicate camera shots");
        pose=director.update(.016f,target);
        check(director.active_shot()=="scene.cutscene" &&
              near(pose.position.x,28.f),
              "Re-registering source did not refresh shot priority tie order");
        director.clear_shot("scene.cutscene");
        pose=director.update(.016f,target);
        check(director.active_shot()=="vehicle.script" &&
              near(pose.position.x,44.f),
              "Clearing winner did not fall back to next request");
        director.clear_shot("vehicle.script");
        pose=director.update(.016f,target);
        check(director.active_shot()=="ui.preview" &&
              near(pose.position.x,14.f),
              "Clearing higher priority requests lost lower priority shot");
        director.clear_shots();
        pose=director.update(.016f,target);
        check(director.active_shot().empty() &&
              director.shot_count()==0 &&
              near(pose.position.x,0.f) && near(pose.position.y,1.6f),
              "Game FPS camera did not resume precisely after cinematic override");

        CameraSettings smooth;
        smooth.follow_smoothing=0.f;
        CameraDirector blending(smooth);
        blending.set_mode(CameraMode::third_person,0.f);
        auto gameplay=blending.update(0.f,target);
        blending.push_shot(shot("cinema.slow",50,23.f,.4f));
        auto halfway=blending.update(.2f,target);
        check(halfway.position.x>gameplay.position.x &&
              halfway.position.x<23.f,
              "Cinematic shot transition ignored its authored blend");
        for(int i=0;i<10;++i)
            halfway=blending.update(.05f,target);
        check(near(halfway.position.x,23.f),
              "Cinematic shot blend did not settle to target");

        CameraKeyframe first,second;
        first.seconds=0.f; first.pose.position={0.f,3.f,0.f};
        second.seconds=2.f;second.pose.position={10.f,3.f,0.f};
        CameraTrack track;
        track.set_keyframes({first,second});
        director.set_track(std::move(track));
        director.set_mode(CameraMode::cinematic,0.f);
        director.seek_track(1.f);
        director.push_shot(shot("story.priority",75,55.f));
        auto frozen=director.update(.1f,target);
        check(near(frozen.position.x,55.f) &&
              near(director.track_time(),1.f),
              "Priority shot must not rewind or inadvertently advance cinematic track");
        director.clear_shot("story.priority");
        auto resumed=director.update(.1f,target);
        check(near(director.track_time(),1.1f) &&
              near(resumed.position.x,5.5f,.02f),
              "Cutscene timeline did not resume after priority release");

        std::vector<CameraShot> originals{
            shot("cinema.wide",10,15.f,.5f),
            shot("cinema.close",30,21.f,.2f)
        };
        auto json=serialize_camera_presets(originals);
        auto decoded=parse_camera_presets(json);
        check(decoded.size()==2 &&
              decoded[0].source=="cinema.wide" &&
              decoded[1].priority==30 &&
              near(decoded[1].pose.position.x,21.f) &&
              near(decoded[0].pose.yaw,.2f,.00001f),
              "Camera preset JSON roundtrip changed source or pose");
        // Exact same JSON contract consumed by the game through engine.assets.
        namespace fs=std::filesystem;
        AssetManager assets;
        auto content=fs::u8path(argv[1])/
            "Projects/NorthStarFirstPersonPlatform/assets";
        assets.mount_directory("",content,100);
        auto authored=load_camera_presets(assets);
        check(authored.size()==2 &&
              authored[0].source=="cinematic.intro_wide" &&
              authored[1].source=="cinematic.inspect",
              "Project Camera Presets were not read via active VFS");
        auto rejects=[&](std::vector<CameraShot> input) {
            try {serialize_camera_presets(input);}
            catch(std::exception const&) {return true;}
            return false;
        };
        check(rejects({shot("bad/source",0,0.f)}),
              "Invalid camera source accepted");
        check(rejects({shot("valid",101,0.f)}),
              "Out-of-range cinematic priority accepted");
        check(rejects({shot("dup",0,0.f),shot("dup",1,1.f)}),
              "Duplicate camera presets accepted");
        try {
            (void)parse_camera_presets(
                R"({"schema":1,"presets":[{"source":"x","position":{"x":4,"y":2,"z":1},"rotation":{"yaw_degrees":0,"pitch_degrees":130}}]})");
            throw std::runtime_error("Invalid camera pitch parsed");
        }catch(std::invalid_argument const&) {}
        director.push_shot(shot("source.reset",15,4.f));
        director.reset();
        check(director.shot_count()==0 && director.active_shot().empty(),
              "Reset retained camera source ownership after scene stop");
        std::cout<<"PASS: prioritized cinematic cameras, deterministic ties, "
                 <<"blended transitions, stable tracks and preset JSON roundtrip\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<"Camera director priority regression: "<<error.what()<<"\n";
        return 1;
    }
}
