// NorthStar native camera motion, cut, collision and timing regression.
// Standalone engine-owned implementation inspired by generic camera concepts;
// no external game or proprietary runtime source is compiled or embedded.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_track.hpp>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace northstarcpp;
namespace {
void require(bool condition, char const* description) {
    if(!condition)throw std::runtime_error(description);
}
bool near(float a,float b,float eps=.003f) {
    return std::abs(a-b)<=eps;
}
CameraTarget player() {
    CameraTarget t;
    t.eye={0.f,1.6f,0.f};
    t.ignore_object=std::numeric_limits<std::size_t>::max();
    return t;
}
}

int main() {
    try {
        CameraSettings spring;
        spring.third_person_distance=4.f;
        spring.third_person_height=0.f;
        spring.shoulder_offset=0.f;
        spring.follow_smoothing=.12f;
        spring.follow_spring_frequency_hz=2.6f;
        spring.third_person_lookahead_seconds=0.f;
        CameraTarget t=player();
        auto integrate=[&](int frames,float dt) {
            CameraDirector camera(spring);
            camera.update(0.f,t);
            camera.set_mode(CameraMode::third_person,0.f);
            CameraPose pose{};
            for(int frame=0;frame<frames;++frame)
                pose=camera.update(dt,t);
            return pose;
        };
        auto p30=integrate(30,1.f/30.f);
        auto p60=integrate(60,1.f/60.f);
        auto p120=integrate(120,1.f/120.f);
        require(near(p30.position.z,p60.position.z,.001f) &&
                near(p60.position.z,p120.position.z,.001f),
                "Critically damped camera follow differs by frame rate");
        require(p120.position.z< -3.95f && p120.position.z>=-4.01f,
                "Critically damped spring did not converge to target");

        CameraSettings look_settings=spring;
        look_settings.follow_spring_frequency_hz=0.f;
        look_settings.follow_smoothing=0.f;
        look_settings.third_person_lookahead_seconds=.2f;
        look_settings.third_person_lookahead_max=1.0f;
        look_settings.lookahead_damping_seconds=.3f;
        CameraDirector look(look_settings);
        look.set_mode(CameraMode::third_person,0.f);
        t.velocity={10.f,0.f,0.f};
        auto leading=look.update(1.f/60.f,t);
        require(near(leading.position.x,1.f,.001f),
                "Camera predictive look-ahead does not honor bounded lead");
        t.velocity={0.f,0.f,0.f};
        auto settling=look.update(1.f/60.f,t);
        require(settling.position.x>0.f &&
                settling.position.x<leading.position.x,
                "Look-ahead snapped instead of damping after velocity drop");
        for(int frame=0;frame<180;++frame)
            settling=look.update(1.f/60.f,t);
        require(std::abs(settling.position.x)<.002f,
                "Look-ahead offset never returned to rest");

        CameraSettings obstructed_settings=spring;
        obstructed_settings.follow_spring_frequency_hz=0.f;
        obstructed_settings.follow_smoothing=0.f;
        obstructed_settings.collision_radius=0.f;
        obstructed_settings.collision_padding=.01f;
        obstructed_settings.collision_restore_seconds=.3f;
        CameraDirector occlusion(obstructed_settings);
        occlusion.set_mode(CameraMode::third_person,0.f);
        t=player();
        Scene scene;
        scene.objects.push_back({"Wall",{0.f,1.f,-1.5f},2.f,
                                  0xffffff,false,0.f});
        auto blocked=occlusion.update(1.f/60.f,t,&scene);
        require(blocked.position.z> -2.f && blocked.position.z<=0.f,
                "Camera did not pull in front of obstacle");
        scene.objects.clear();
        auto initial_release=occlusion.update(1.f/60.f,t,&scene);
        require(initial_release.position.z < blocked.position.z &&
                initial_release.position.z >-3.9f,
                "Camera collision distance restored abruptly without hysteresis");
        for(int frame=0;frame<180;++frame)
            initial_release=occlusion.update(1.f/60.f,t,&scene);
        require(near(initial_release.position.z,-4.f,.002f),
                "Camera collision hysteresis did not recover full radius");
        scene.objects.push_back({"Wall",{0.f,1.f,-1.5f},2.f,
                                  0xffffff,false,0.f});
        auto blocked_again=occlusion.update(1.f/60.f,t,&scene);
        require(blocked_again.position.z> -2.f,
                "Camera did not retract on the same frame as a new obstruction");

        Scene scene_v3=scene;
        scene_v3.version=3;
        scene_v3.objects.front().node.id=1;
        scene_v3.objects.front().node.collision=false;
        CameraDirector ignored(obstructed_settings);
        ignored.set_mode(CameraMode::third_person,0.f);
        auto no_collider=ignored.update(1.f/60.f,t,&scene_v3);
        require(near(no_collider.position.z,-4.f),
                "Non-colliding Scene V3 actor incorrectly occluded camera");
        scene_v3.objects.front().node.collision=true;
        scene_v3.objects.front().node.visible=false;
        ignored.cut();
        auto invisible=ignored.update(1.f/60.f,t,&scene_v3);
        require(near(invisible.position.z,-4.f),
                "Invisible Scene V3 actor incorrectly occluded camera");
        scene_v3.objects.front().node.visible=true;
        auto visible=ignored.update(1.f/60.f,t,&scene_v3);
        require(visible.position.z>-2.f,
                "Visible collidable Scene V3 actor did not obstruct camera");

        CameraKeyframe first,second;
        first.seconds=0.f;
        first.pose.position={1.f,3.f,5.f};
        second.seconds=2.f;
        second.pose.position={7.f,3.f,5.f};
        CameraTrack track;
        track.set_keyframes({first,second});
        CameraDirector cinematic;
        cinematic.set_track(std::move(track));
        cinematic.set_mode(CameraMode::cinematic,0.f);
        cinematic.seek_track(1.f);
        cinematic.cut();
        require(near(cinematic.track_time(),1.f),
                "A camera cut unexpectedly rewound the cinematic track");
        auto shot=cinematic.update(0.f,t);
        require(near(shot.position.x,4.f,.002f),
                "Camera cut did not immediately adopt the selected cinematic frame");

        CameraDirector fps(spring);
        t=player();
        t.yaw=1.2f;
        t.pitch=.25f;
        fps.set_mode(CameraMode::first_person,0.f);
        auto aim=fps.update(1.f/60.f,t);
        require(near(aim.yaw,t.yaw,.00001f) &&
                near(aim.pitch,t.pitch,.00001f),
                "Spring settings introduced lag in authoritative first-person aiming");

        auto parsed=parse_camera_settings(
            R"({"schema":1,"smoothing":{"spring_frequency_hz":2.6,"lookahead_seconds":0.12},"third_person":{"lookahead_seconds":0.17,"lookahead_max":1.5},"collision":{"restore_seconds":0.11}})");
        require(near(parsed.follow_spring_frequency_hz,2.6f) &&
                near(parsed.third_person_lookahead_seconds,.17f) &&
                near(parsed.collision_restore_seconds,.11f),
                "Optional camera tuning parameters were not parsed");
        try {
            (void)parse_camera_settings(
                R"({"schema":1,"smoothing":{"spring_frequency_hz":100}})");
            throw std::runtime_error("Invalid camera spring frequency accepted");
        }catch(std::invalid_argument const&) {}
        try {
            (void)parse_camera_settings(
                R"({"schema":1,"collision":{"restore_seconds":-1}})");
            throw std::runtime_error("Negative collision recovery accepted");
        }catch(std::invalid_argument const&) {}

        CameraDirector continuous(spring);
        continuous.set_mode(CameraMode::third_person,0.f);
        t=player();
        for(int frame=0;frame<1000;++frame) {
            float const seconds=float(frame)/60.f;
            t.eye={std::sin(seconds)*5.f,1.6f,
                   std::cos(seconds*.7f)*8.f};
            t.velocity={std::cos(seconds)*5.f,0.f,
                        -std::sin(seconds*.7f)*5.6f};
            auto result=continuous.update(1.f/60.f,t);
            require(std::isfinite(result.position.x) &&
                    std::isfinite(result.position.y) &&
                    std::isfinite(result.position.z) &&
                    std::isfinite(result.yaw) &&
                    std::isfinite(result.fov_y_degrees),
                    "Camera motion lost finite state in 1000-frame regression");
        }
        // CPU-only camera microprofile in a large scene. The editor fallback
        // broadphase must discard far actors before the AABB narrowphase.
        // This does not replace the renderer's GPU frame-time profile.
        Scene crowded;
        crowded.version=3;
        crowded.objects.reserve(12000);
        for(int index=0;index<12000;++index) {
            SceneObject actor;
            actor.node.id=static_cast<std::uint64_t>(index)+1;
            actor.position={200.f+float(index%160),
                            float(index%7),200.f+float(index/160)};
            crowded.objects.push_back(std::move(actor));
        }
        CameraSettings profile_settings=spring;
        profile_settings.follow_smoothing=0.f;
        profile_settings.follow_spring_frequency_hz=0.f;
        CameraDirector profiler(profile_settings);
        profiler.set_mode(CameraMode::third_person,0.f);
        t=player();
        std::vector<double> frame_us;
        frame_us.reserve(1000);
        for(int frame=0;frame<1000;++frame) {
            auto const start=std::chrono::steady_clock::now();
            auto pose=profiler.update(1.f/60.f,t,&crowded);
            auto const finish=std::chrono::steady_clock::now();
            require(std::abs(pose.position.z+4.f)<.001f,
                    "Fallback camera broadphase incorrectly hit far actor");
            frame_us.push_back(std::chrono::duration<double,std::micro>(
                finish-start).count());
        }
        std::sort(frame_us.begin(),frame_us.end());
        std::cout<<"PROFILE: 12000 far actors, camera fallback 1000 frames; "
                 <<"p50="<<frame_us[500]<<" us, p95="<<frame_us[950]
                 <<" us, p99="<<frame_us[990]<<" us (CPU only)\n";
        std::cout<<"PASS: camera spring 30/60/120 FPS, predictive look-ahead, "
                    "occlusion hysteresis, V3 collision filtering, cinematic cuts, "
                    "FPS aim, VFS tuning and 1000 frames\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Camera motion regression FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
