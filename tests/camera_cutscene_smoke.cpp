#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_track.hpp>
#include <cmath>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace northstarcpp;
namespace {
void check(bool result,const char* message) {
    if(!result)throw std::runtime_error(message);
}
bool near(float a,float b,float tolerance=.008f) {
    return std::abs(a-b)<=tolerance;
}
CameraTrack track(float first,float last) {
    CameraKeyframe a,b;
    a.seconds=0.f;
    a.pose.position={first,2.f,0.f};
    b.seconds=2.f;
    b.pose.position={last,2.f,0.f};
    CameraTrack result;
    result.set_keyframes({a,b});
    return result;
}
CameraCutscene make() {
    CameraCutscene result;
    result.set_shots({
        {"establish",2.f,0.f,track(0.f,4.f)},
        {"detail",2.f,1.f,track(10.f,14.f)},
        {"reaction",2.f,.5f,track(30.f,34.f)}});
    result.set_cues({
        {0.f,"actors","animation","abby","play","idle"},
        {2.5f,"audio","audio","world","play","ambience.aac"},
        {3.f,"dialogue","dialogue","abby","say","Hello"},
        {6.f,"story","event","quest","finish","intro"}
    });
    result.set_lens_keys({
        {0.f,{5.f,4.f,0.f,0.f,0.f,0.f}},
        {6.f,{1.4f,1.8f,2.f,.4f,.18f,.7f}}
    });
    return result;
}
}
int main(int argc,char** argv) {
    try {
        auto c=make();
        check(c.size()==3 && near(c.duration(),6.f),"duration/shot count");
        check(c.cues().size()==4 && c.cues_between(-1.f,0.f).size()==1 &&
              c.cues_between(2.f,3.f).size()==2 &&
              c.cues_between(3.f,4.f).empty(),
              "event tracks use stable global time intervals");
        check(near(c.lens_at(3.f).exposure_ev,1.f) &&
              near(c.lens_at(3.f).focus_metres,3.2f),
              "lens keyframes interpolate exposure and focus");
        auto serialized=serialize_camera_cutscene(c);
        auto roundtrip=parse_camera_cutscene(serialized);
        check(roundtrip.cues().size()==4 &&
              roundtrip.lens_keys().size()==2 &&
              roundtrip.cues()[0].value=="idle" &&
              near(roundtrip.lens_at(3.f).exposure_ev,1.f),
              "multitrack JSON roundtrip preserves cues and lens");
        check(c.sample(0.f).shot=="establish" &&
              near(c.sample(0.f).pose.position.x,0.f),"first shot");
        check(c.sample(2.f).shot=="detail" &&
              near(c.sample(2.f).pose.position.x,4.f),"shot boundary no pop");
        auto halfway=c.sample(2.5f);
        check(halfway.shot=="detail" &&
              halfway.pose.position.x>4.f &&
              halfway.pose.position.x<12.f,"quintic shot transition");
        check(near(c.sample(3.f).pose.position.x,12.f),"second shot after blend");
        check(near(c.sample(4.f).pose.position.x,14.f),"second shot final");
        check(near(c.sample(6.f).pose.position.x,34.f),"cutscene final pose");
        try {
            CameraCutscene invalid;
            invalid.set_shots({{"a",2.f,1.f,track(0,1)}});
            throw std::runtime_error("First shot accepted nonzero transition");
        }catch(std::invalid_argument const&) {}
        try {
            CameraCutscene invalid;
            invalid.set_shots({{"a",1.f,0.f,track(0,1)}});
            throw std::runtime_error("Track outside shot duration accepted");
        }catch(std::invalid_argument const&) {}
        const auto parsed=parse_camera_cutscene(R"({
            "schema":1,
            "space":"target",
            "shots":[
              {"name":"wide","duration":2.0,"transition_seconds":0,
               "track":{"schema":1,"keyframes":[
                 {"time":0,"position":[0,2,0],"yaw":0,"pitch":0},
                 {"time":2,"position":[4,2,0],"yaw":0.2,"pitch":0}]}},
              {"name":"close","duration":2.0,"transition_seconds":0.5,
               "track":{"schema":1,"keyframes":[
                 {"time":0,"position":[4,2,0],"yaw":0.2,"pitch":0},
                 {"time":2,"position":[7,2,0],"yaw":0.4,"pitch":0}]}}
            ]})");
        check(parsed.size()==2 && near(parsed.duration(),4.f) &&
              parsed.space()==CameraCutsceneSpace::target,
              "JSON cutscene parser and local coordinate space");
        if(argc>1) {
            std::ifstream source(argv[1],std::ios::binary);
            check(bool(source),"bundled engine cinematic example not found");
            std::string json((std::istreambuf_iterator<char>(source)),
                             std::istreambuf_iterator<char>());
            auto const fixture=parse_camera_cutscene(json);
            check(fixture.size()==3 && near(fixture.duration(),7.f),
                  "engine cutscene NSP asset parses");
            check(fixture.sample(2.5f).shot=="closeup",
                  "engine asset shot change incorrect");
        }
        // Reusable cutscenes may be authored in local actor coordinates.
        // The scene origin must remain frozen when the character moves.
        CameraTarget placed;
        placed.eye={100.f,1.6f,200.f};
        placed.yaw=1.57079632679f;
        CameraDirector attached;
        attached.update(0.f,placed);
        auto relative=make();
        relative.set_space(CameraCutsceneSpace::target);
        attached.play_cutscene(std::move(relative),false,0.f,0.f);
        auto target_frame=attached.update(0.f,placed);
        check(near(target_frame.position.x,100.f) &&
              near(target_frame.position.y,3.6f) &&
              near(target_frame.yaw,placed.yaw),
              "cutscene target-space anchor and heading");
        placed.eye={102.f,1.6f,199.f};
        placed.yaw=-0.2f;
        auto target_fixed=attached.update(0.f,placed);
        check(near(target_fixed.position.x,100.f) &&
              near(target_fixed.position.y,3.6f) &&
              near(target_fixed.yaw,1.57079632679f),
              "camera origin must not follow the moving actor");

        CameraSettings settings;
        settings.follow_smoothing=0.f;
        CameraDirector director(settings);
        CameraTarget t;
        t.eye={0.f,1.6f,0.f};
        auto fps=director.update(0.f,t);
        check(near(fps.position.y,1.6f),"FPS initial position");
        director.set_mode(CameraMode::third_person,.45f);
        auto first=director.update(.02f,t);
        check(first.position.z<0.f && first.position.z>-4.f,
              "FPS/TPS view transition moves smoothly");
        for(int i=0;i<60;++i)director.update(.02f,t);
        director.play_cutscene(make(),false,.55f,.65f);
        check(director.cutscene_active(),"play cutscene");
        auto intro=director.update(.05f,t);
        check(intro.position.x>=0.f && intro.position.x<1.f,
              "cutscene entry blend interpolates");
        auto first_cues=director.drain_cutscene_cues_json();
        check(first_cues.find("idle")!=std::string::npos &&
              first_cues.find("animation")!=std::string::npos &&
              director.drain_cutscene_cues_json()=="[]",
              "t=0 animation cue delivered exactly once");
        director.pause_cutscene(true);
        auto paused_time=director.cutscene_time();
        director.update(.2f,t);
        check(near(paused_time,director.cutscene_time()) &&
              director.drain_cutscene_cues_json()=="[]",
              "paused cutscene neither advances nor re-emits events");
        director.pause_cutscene(false);
        director.seek_cutscene(3.f);
        auto middle=director.update(0.f,t);
        check(near(middle.position.x,12.f) &&
              director.drain_cutscene_cues_json()=="[]",
              "seek adopts the shot without replaying skipped audio/dialogue");
        check(near(director.cinematic_lens().exposure_ev,1.f),
              "director exposes interpolated GPU exposure");
        check(director.cutscene_shot()=="detail","active cutscene shot name");
        director.seek_cutscene(5.95f);
        director.update(.05f,t);
        check(director.cutscene_active() &&
              near(director.cutscene_time(),6.f) &&
              director.drain_cutscene_cues_json().find("intro")!=std::string::npos,
              "last frame and end event visible before gameplay restoration");
        auto returned=director.update(.02f,t);
        check(!director.cutscene_active() &&
              director.mode()==CameraMode::third_person,
              "automatic gameplay camera restoration");
        check(std::isfinite(returned.position.x),"finite exit blend");
        for(int i=0;i<80;++i)returned=director.update(.02f,t);
        check(near(returned.position.z,-4.f,.025f),"third-person camera restored");
        director.play_cutscene(make(),true,0.f,0.f);
        director.update(.02f,t);
        director.seek_cutscene(5.9f);
        director.update(.2f,t);
        auto loop_cues=director.drain_cutscene_cues_json();
        check(loop_cues.find("idle")!=std::string::npos &&
              loop_cues.find("intro")!=std::string::npos,
              "loop boundary delivers both final and first-frame cues");
        check(director.cutscene_active() &&
              director.cutscene_time()<.2f,"loop wraps shot timeline");
        director.stop_cutscene(0.f);
        check(!director.cutscene_active(),"manual cutscene stop");
        // This is a genuine TPS -> FPS test: after ending the cutscene,
        // restore the gameplay follow rig before switching modes.
        CameraPose third{};
        for(int i=0;i<80;++i)third=director.update(.016f,t);
        check(near(third.position.z,-4.f,.025f),
              "third-person gameplay restored after manual stop");
        director.set_mode(CameraMode::first_person,.3f);
        auto exiting=director.update(.016f,t);
        check(exiting.position.z<0.f &&
              exiting.position.z>-4.f,"third-to-first smooth transition");
        for(int i=0;i<80;++i)exiting=director.update(.016f,t);
        check(near(exiting.position.z,0.f,.001f) &&
              near(exiting.yaw,t.yaw),"FPS aim restore after transition");

        std::cout<<"NorthStar multi-shot cutscenes, VFS JSON parser, FPS/TPS "
                 <<"transitions, pause/seek/loop/return: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Camera cutscene QA failed: "<<e.what()<<"\n";
        return 1;
    }
}
