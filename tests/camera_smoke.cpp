#include "qa_scene_fixture.hpp"
// CameraDirector invariants and behavior regression tests.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_track.hpp>
#include <cmath>
#include <stdexcept>
#include <iostream>
using namespace northstarcpp;
static bool approx(float a,float b,float eps=0.002f){return std::abs(a-b)<eps;}
int main() {
    char const* stage="initial";
    try {
        CameraTarget t;
        t.eye={0,1.6f,0};t.yaw=0.f;t.pitch=0.f;t.ignore_object=0;
        Scene scene;
        qa::append(scene,{"Player",{0,0,0},1.5f,0xffffff,true,2.f});
        CameraDirector camera;
        auto check_defaults=[&](char const* step) {
            auto const& settings=camera.settings();
            if(std::abs(settings.third_person_height-.7f)>.0001f ||
               std::abs(settings.third_person_distance-4.f)>.0001f) {
                std::cerr<<"Camera settings corrupt at "<<step
                         <<" height="<<settings.third_person_height
                         <<" distance="<<settings.third_person_distance<<"\n";
                return false;
            }
            return true;
        };
        if(!check_defaults("constructor"))return 96;
        auto p=camera.update(0.016f,t,&scene);
        if(!check_defaults("first person"))return 96;
        if (!approx(p.position.y,1.6f) || !approx(p.yaw,0.f) ||
            !approx(p.fov_y_degrees,75.f))return 1;
        stage="zoom-in";
        camera.set_zoom(1.f);
        p=camera.update(.016f,t,&scene);
        if (!(p.fov_y_degrees>55.f && p.fov_y_degrees<75.f))return 2;
        for(int i=0;i<120;++i) {
            p=camera.update(.016f,t,&scene);
            if(!check_defaults("zoom loop"))return 96;
        }
        if (!approx(p.fov_y_degrees,55.f))return 2;
        camera.set_zoom(0.f);
        stage="third-person-collision";
        if(!check_defaults("before third person"))return 96;
        camera.set_mode(CameraMode::third_person,0.f);
        if(!check_defaults("mode transition"))return 96;
        p=camera.update(.25f,t,&scene);
        if (p.position.z>-0.3f || p.position.z<-4.5f)return 3;
        qa::append(scene,{"Wall",{0,1,-2},1.5f,0x888888,false,0.f});
        camera.reset();
        p=camera.update(.016f,t,&scene);
        // Occlusion constrains the camera in front of the wall.
        if (p.position.z<-1.1f || p.position.z>0.f)return 4;
        stage="vehicle";
        camera.set_mode(CameraMode::vehicle_chase,0.f);
        scene.objects.pop_back();
        camera.reset();
        p=camera.update(.016f,t,&scene);
        if (!std::isfinite(p.position.x) || p.position.z>-6.f)return 5;
        stage="orbit";
        camera.set_mode(CameraMode::orbit,0.f);
        camera.reset();
        auto o1=camera.update(.016f,t,&scene);
        auto o2=camera.update(.2f,t,&scene);
        if (approx(o1.position.x,o2.position.x,0.001f))return 6;
        CameraPose cp;cp.position={3,4,5};cp.yaw=0.5f;cp.fov_y_degrees=80;
        stage="free";
        camera.set_free_pose(cp);
        camera.set_mode(CameraMode::free,0.f);
        camera.reset();
        p=camera.update(.016f,t,&scene);
        if (!approx(p.position.x,3.f) || !approx(p.fov_y_degrees,80.f))return 7;
        stage="cinematic";
        camera.set_cinematic_pose(cp);
        camera.set_mode(CameraMode::cinematic,0.f);
        camera.reset();
        p=camera.update(.016f,t,&scene);
        if (!approx(p.position.z,5.f))return 8;
        camera.shake(.15f,.1f);
        auto shaken=camera.update(.03f,t,&scene);
        if (approx(shaken.position.y,p.position.y,0.0001f))return 9;
        for(int i=0;i<10;i++) camera.update(.03f,t,&scene);
        auto stabilized=camera.update(.03f,t,&scene);
        if (!approx(stabilized.position.y,p.position.y))return 10;
        CameraKeyframe a,b,c;
        a.seconds=0.f;a.pose.position={0.f,3.f,0.f};
        b.seconds=2.f;b.pose.position={10.f,3.f,0.f};
        c.seconds=4.f;c.pose.position={20.f,3.f,0.f};
        CameraTrack track;
        track.set_keyframes({a,b,c});
        auto halfway=track.sample(1.f);
        if (!approx(halfway.position.x,5.f) ||
            !approx(track.duration(),4.f) || track.size()!=3) return 16;
        if (!approx(track.sample(99.f).position.x,20.f)) return 17;
        stage="track";
        camera.set_track(std::move(track),true);
        camera.set_mode(CameraMode::cinematic,0.f);
        camera.reset();
        camera.update(0.25f,t,&scene);
        if (!approx(camera.track_time(),0.25f)) return 18;
        camera.seek_track(2.f);
        camera.set_track_playing(false);
        p=camera.update(0.f,t,&scene);
        if (!approx(p.position.x,10.f)) return 19;
        camera.set_track_playing(true);
        camera.seek_track(3.90f);
        camera.update(.2f,t,&scene);
        if (!approx(camera.track_time(),.10f,.001f)) return 20;
        try {
            CameraTrack invalid;
            auto duplicate=b;
            invalid.set_keyframes({a,duplicate,duplicate});
            return 21;
        } catch(std::invalid_argument const&){}
        // Vehicle dynamics: speed responsive FOV and orbit distance,
        // turn banking and reverse camera positioning.
        stage="vehicle-dynamics";
        CameraSettings vehicle_settings;
        vehicle_settings.follow_smoothing=0.f;
        CameraDirector vehicle(vehicle_settings);
        vehicle.set_mode(CameraMode::vehicle_chase,0.f);
        t.velocity={0.f,0.f,20.f};
        t.steering=1.f;
        auto forward=vehicle.update(.016f,t,&scene);
        if(forward.fov_y_degrees<=75.f || forward.roll>=0.f ||
           forward.position.z>=-1.f) return 22;
        vehicle.reset();
        t.reversing=true;
        auto backward=vehicle.update(.016f,t,&scene);
        if(backward.position.z<=0.f) return 23;
        t.reversing=false;
        t.steering=0.f;
        t.velocity={0,0,0};

        // Cinematic recording, file serializer round-trip and playback.
        stage="replay";
        CameraReplayRecorder recorder;
        recorder.start(0.02f);
        CameraPose shot=cp;
        recorder.record(0.f,shot);
        shot.position.x=4.f;
        recorder.record(.025f,shot);
        shot.position.x=5.f;
        recorder.record(.025f,shot);
        auto saved=recorder.finish();
        if(saved.size()!=3 || recorder.recording())return 24;
        auto const encoded=serialize_camera_track(saved);
        auto decoded=parse_camera_track(encoded);
        if(decoded.size()!=3 || !approx(decoded.sample(.025f).position.x,4.f))
            return 25;
        camera.clear_track();
        camera.set_track(std::move(decoded),false);
        camera.set_track_playback_rate(2.f);
        camera.set_mode(CameraMode::cinematic,0.f);
        camera.reset();
        camera.update(.02f,t,&scene);
        if(!approx(camera.track_time(),.04f))return 26;
        try { camera.set_track_playback_rate(0.f);return 27; }
        catch(std::invalid_argument const&){}
        stage="attachment";
        CameraDirector socket;
        t.attachment.enabled=true;
        t.attachment.world_position={1.f,2.f,3.f};
        t.attachment.yaw=.45f;
        t.attachment.pitch=.12f;
        t.attachment.roll=-.04f;
        t.attachment.weight=1.f;
        auto socket_pose=socket.update(.016f,t,&scene);
        if (!approx(socket_pose.position.x,1.f) ||
            !approx(socket_pose.position.y,2.f) ||
            !approx(socket_pose.yaw,.45f) ||
            !approx(socket_pose.roll,-.04f))return 28;
        t.attachment.weight=0.f;
        socket_pose=socket.update(.016f,t,&scene);
        if (!approx(socket_pose.position.x,t.eye.x) ||
            !approx(socket_pose.position.y,t.eye.y))return 29;
        t.attachment.weight=2.f;
        try {socket.update(.016f,t,&scene);return 30;}
        catch(std::invalid_argument const&){}
        t.attachment.enabled=false;

        // Native transition must animate dollying both into and out of
        // third person. Wheel distance changes must use follow smoothing.
        stage="third-person-transition";
        CameraSettings smooth_settings;
        CameraDirector smooth(smooth_settings);
        CameraTarget person;
        person.eye={0.f,1.6f,0.f};
        person.ignore_object=0;
        smooth.update(.016f,person,&scene);
        smooth.set_mode(CameraMode::third_person,.45f);
        auto approaching=smooth.update(.016f,person,&scene);
        if (!(approaching.position.z<0.f && approaching.position.z>-4.f))
            return 31;
        for (int i=0;i<60;++i) approaching=smooth.update(.016f,person,&scene);
        if (!approx(approaching.position.z,-4.f,.03f)) {
            std::cerr<<"Third-person convergence z="<<approaching.position.z<<"\n";
            return 32;
        }
        smooth.set_mode(CameraMode::first_person,.45f);
        auto zooming_in=smooth.update(.016f,person,&scene);
        if (!(zooming_in.position.z<0.f && zooming_in.position.z>-4.f))
            return 33;
        for (int i=0;i<60;++i) zooming_in=smooth.update(.016f,person,&scene);
        if (!approx(zooming_in.position.z,0.f,.03f)) return 34;
        smooth.set_mode(CameraMode::third_person,0.f);
        for (int i=0;i<60;++i) smooth.update(.016f,person,&scene);
        smooth_settings.third_person_distance=8.f;
        smooth.configure(smooth_settings);
        auto zooming_out=smooth.update(.016f,person,&scene);
        if (!(zooming_out.position.z<-4.f && zooming_out.position.z>-8.f))
            return 35;
        for (int i=0;i<60;++i) zooming_out=smooth.update(.016f,person,&scene);
        if (!approx(zooming_out.position.z,-8.f,.03f)) return 36;

        try{camera.update(-0.1f,t,&scene);return 11;}
        catch(std::invalid_argument const&){}
        try{camera.set_zoom(2.f);return 12;}
        catch(std::invalid_argument const&){}
        try{CameraSettings invalid;invalid.third_person_distance=-1.f;camera.configure(invalid);return 13;}
        catch(std::invalid_argument const&){}
        std::cout<<"Camera system smoke passed\n";
        return 0;
    }catch(std::exception const& e){
        std::cerr<<"camera_smoke stage="<<stage<<": "<<e.what()<<'\n';
        return 42;
    }
}