#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <stdexcept>
#include <string>
namespace northstarcpp::window_app_internal {
void collect_camera_available_script_binding(CameraScriptBindings& bindings, CameraAvailableStateView view) {
    if(!view.available) throw std::invalid_argument("Invalid camera ScriptRuntime view");
        bindings.add("available",[view](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty())
                throw std::invalid_argument("camera.available()");
            return view.available() ? std::string("true") : std::string("false");
        });
}

void collect_camera_runtime_script_bindings(
    CameraScriptBindings& bindings, CameraDomainCapabilities service) {
    if(!service.camera_slot || !service.assets_slot || !service.first_person ||
       !service.yaw || !service.pitch)
        throw std::invalid_argument("Invalid camera capabilities");
        bindings.add("setMode",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if (args.size()<1 || args.size()>2 || !service.camera())
                throw std::invalid_argument("camera.setMode(name,transitionSeconds=0.35)");
            static const std::map<std::string,CameraMode> modes={
                {"first_person",CameraMode::first_person},
                {"third_person",CameraMode::third_person},
                {"vehicle_chase",CameraMode::vehicle_chase},
                {"orbit",CameraMode::orbit},
                {"free",CameraMode::free},
                {"cinematic",CameraMode::cinematic}};
            auto const mode=modes.find(args[0]);
            if (mode==modes.end()) throw std::invalid_argument("Unknown camera mode");
            auto const seconds=args.size()==2 ? runtime_script_contracts::script_number(args[1]) : 0.35;
            service.camera()->set_mode(mode->second,static_cast<float>(seconds));
            return std::string("null");
        });
        bindings.add("mode",[service](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty() || !service.camera())
                throw std::invalid_argument("camera.mode()");
            switch (service.camera()->mode()) {
            case CameraMode::first_person: return runtime_project_files::script_json_quote("first_person");
            case CameraMode::third_person: return runtime_project_files::script_json_quote("third_person");
            case CameraMode::vehicle_chase: return runtime_project_files::script_json_quote("vehicle_chase");
            case CameraMode::orbit: return runtime_project_files::script_json_quote("orbit");
            case CameraMode::free: return runtime_project_files::script_json_quote("free");
            case CameraMode::cinematic: return runtime_project_files::script_json_quote("cinematic");
            }
            throw std::runtime_error("Invalid camera mode");
        });
        bindings.add("setFollowDistance",
            [service](std::string_view json) {
                auto const args=runtime_script_contracts::script_args(json);
                if (args.size()!=1 || !service.camera())
                    throw std::invalid_argument("camera.setFollowDistance(metres)");
                auto settings=service.camera()->settings();
                settings.third_person_distance=static_cast<float>(runtime_script_contracts::script_number(args[0]));
                service.camera()->configure(settings);
                return std::string("null");
            });
        bindings.add("followDistance",[service](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty() || !service.camera())
                throw std::invalid_argument("camera.followDistance()");
            return std::to_string(service.camera()->settings().third_person_distance);
        });
        bindings.add("setAngles",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=2 || !*service.first_person)
                throw std::invalid_argument("camera.setAngles(yaw,pitch)");
            auto yaw=runtime_script_contracts::script_number(args[0]),pitch=runtime_script_contracts::script_number(args[1]);
            if (std::abs(yaw)>1000000 || std::abs(pitch)>1.55)
                throw std::invalid_argument("Camera angles outside bounds");
            *service.yaw=static_cast<float>(std::remainder(yaw,6.283185307179586));
            *service.pitch=static_cast<float>(pitch);
            return std::string("null");
        });
        bindings.add("angles",[service](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty()) throw std::invalid_argument("camera.angles()");
            return std::string("{\"yaw\":")+std::to_string(*service.yaw)+
                ",\"pitch\":"+std::to_string(*service.pitch)+"}";
        });
        bindings.add("cut",[service](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty() || !service.camera())
                throw std::invalid_argument("camera.cut()");
            // Explicit teleport / shot boundary. Keep active cinematic
            // track time, session state, and plugin-owned resources intact.
            service.camera()->cut();
            return std::string("null");
        });
        bindings.add("pushShot",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=10 || !service.camera())
                throw std::invalid_argument(
                    "camera.pushShot(source,x,y,z,yaw,pitch,roll,fov,priority,blend)");
            CameraShot shot;
            shot.source=args[0];
            shot.pose.position={
                static_cast<float>(runtime_script_contracts::script_number(args[1])),
                static_cast<float>(runtime_script_contracts::script_number(args[2])),
                static_cast<float>(runtime_script_contracts::script_number(args[3]))};
            shot.pose.yaw=static_cast<float>(runtime_script_contracts::script_number(args[4]));
            shot.pose.pitch=static_cast<float>(runtime_script_contracts::script_number(args[5]));
            shot.pose.roll=static_cast<float>(runtime_script_contracts::script_number(args[6]));
            shot.pose.fov_y_degrees=static_cast<float>(runtime_script_contracts::script_number(args[7]));
            shot.pose.near_plane=service.camera()->settings().near_plane;
            shot.pose.far_plane=service.camera()->settings().far_plane;
            auto const priority=runtime_script_contracts::script_number(args[8]);
            if(priority<-100. || priority>100. ||
               std::floor(priority)!=priority)
                throw std::invalid_argument("Camera shot priority must be an integer -100..100");
            shot.priority=static_cast<int>(priority);
            shot.blend_seconds=static_cast<float>(runtime_script_contracts::script_number(args[9]));
            service.camera()->push_shot(std::move(shot));
            return std::string("null");
        });
        bindings.add("clearShot",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.camera())
                throw std::invalid_argument("camera.clearShot(source)");
            service.camera()->clear_shot(args[0]);
            return std::string("null");
        });
        bindings.add("clearShots",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.camera())
                throw std::invalid_argument("camera.clearShots()");
            service.camera()->clear_shots();
            return std::string("null");
        });
        bindings.add("activeShot",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.camera())
                throw std::invalid_argument("camera.activeShot()");
            return runtime_project_files::script_json_quote(service.camera()->active_shot());
        });
        bindings.add("applyPreset",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.camera() || !service.assets())
                throw std::invalid_argument("camera.applyPreset(presetId) requires project VFS");
            auto const presets=load_camera_presets(*service.assets());
            auto const it=std::find_if(presets.begin(),presets.end(),
                [&](CameraShot const& preset) {
                    return preset.source==args[0];
                });
            if(it==presets.end())
                throw std::invalid_argument("Camera preset source not found");
            service.camera()->push_shot(*it);
            return std::string("null");
        });
        bindings.add("shake",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=2 || !service.camera())
                throw std::invalid_argument("camera.shake(amplitude,seconds)");
            auto a=runtime_script_contracts::script_number(args[0]),t=runtime_script_contracts::script_number(args[1]);
            if (a<0 || a>1 || t<0 || t>10) throw std::invalid_argument("Camera shake limits");
            service.camera()->shake(static_cast<float>(a),static_cast<float>(t));
            if(std::getenv("NORTHSTARCPP_QA_WEAPON_TRACE"))
                std::fprintf(stderr,
                    "[WEAPON FX TRACE] camera recoil amplitude=%.4f duration=%.3f\n",a,t);
            return std::string("null");
        });
        bindings.add("recoil",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=3 || !service.camera())
                throw std::invalid_argument(
                    "camera.recoil(pitchRadians,yawRadians,seconds)");
            service.camera()->recoil(
                static_cast<float>(runtime_script_contracts::script_number(args[0])),
                static_cast<float>(runtime_script_contracts::script_number(args[1])),
                static_cast<float>(runtime_script_contracts::script_number(args[2])));
            if(std::getenv("NORTHSTARCPP_QA_WEAPON_TRACE"))
                std::fprintf(stderr,
                    "[WEAPON FX TRACE] angular recoil pitch=%s yaw=%s\n",
                    args[0].c_str(),args[1].c_str());
            return std::string("null");
        });
        bindings.add("playCutscene",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.empty() || args.size()>4 || !service.camera() || !service.assets())
                throw std::invalid_argument(
                    "camera.playCutscene(vfsPath,loop=false,entry=0.55,exit=0.55)");
            const bool loop=args.size()>1 && args[1]=="true";
            if(args.size()>1 && args[1]!="true" && args[1]!="false")
                throw std::invalid_argument("Cutscene loop must be boolean");
            auto const entry=args.size()>2?runtime_script_contracts::script_number(args[2]):0.55;
            auto const exit=args.size()>3?runtime_script_contracts::script_number(args[3]):0.55;
            // VFS resolves this through the currently mounted project/provider.
            auto scene=load_camera_cutscene(*service.assets(),args[0]);
            service.camera()->play_cutscene(std::move(scene),loop,
                static_cast<float>(entry),static_cast<float>(exit));
            return std::string("null");
        });
        bindings.add("stopCutscene",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()>1 || !service.camera())
                throw std::invalid_argument("camera.stopCutscene(exit=0.55)");
            auto const exit=args.empty()?0.55:runtime_script_contracts::script_number(args[0]);
            service.camera()->stop_cutscene(static_cast<float>(exit));
            return std::string("null");
        });
        bindings.add("pauseCutscene",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.camera() ||
               (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("camera.pauseCutscene(paused)");
            service.camera()->pause_cutscene(args[0]=="true");
            return std::string("null");
        });
        bindings.add("seekCutscene",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.camera())
                throw std::invalid_argument("camera.seekCutscene(seconds)");
            service.camera()->seek_cutscene(static_cast<float>(runtime_script_contracts::script_number(args[0])));
            return std::string("null");
        });
        bindings.add("cutsceneState",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.camera())
                throw std::invalid_argument("camera.cutsceneState()");
            return std::string("{\"active\":")+
                (service.camera()->cutscene_active()?"true":"false")+
                ",\"time\":"+std::to_string(service.camera()->cutscene_time())+
                ",\"shot\":"+runtime_project_files::script_json_quote(service.camera()->cutscene_shot())+"}";
        });
        bindings.add("lensState",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.camera())
                throw std::invalid_argument("camera.lensState()");
            auto const lens=service.camera()->cinematic_lens();
            return std::string("{\"active\":")+
                (service.camera()->cutscene_lens_active()?"true":"false")+
                ",\"focusMetres\":"+std::to_string(lens.focus_metres)+
                ",\"apertureF\":"+std::to_string(lens.aperture_f)+
                ",\"exposureEV\":"+std::to_string(lens.exposure_ev)+
                ",\"motionBlur\":"+std::to_string(lens.motion_blur)+
                ",\"letterbox\":"+std::to_string(lens.letterbox)+
                ",\"dofStrength\":"+std::to_string(lens.dof_strength)+"}";
        });
        bindings.add("playTrack",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.empty() || args.size()>3 || !service.camera() || !service.assets())
                throw std::invalid_argument("camera.playTrack(vfsPath,loop=false,blend=0.5)");
            if(args.size()>1 && args[1]!="true" && args[1]!="false")
                throw std::invalid_argument("Track loop must be boolean");
            auto const blend=args.size()>2?runtime_script_contracts::script_number(args[2]):0.5;
            service.camera()->set_track(load_camera_track(*service.assets(),args[0]),
                args.size()>1 && args[1]=="true");
            service.camera()->set_mode(CameraMode::cinematic,static_cast<float>(blend));
            return std::string("null");
        });
        bindings.add("setZoom",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=1 || !service.camera()) throw std::invalid_argument("camera.setZoom(level)");
            auto zoom=runtime_script_contracts::script_number(args[0]);
            if (zoom<0 || zoom>1) throw std::invalid_argument("Zoom must be 0..1");
            service.camera()->set_zoom(static_cast<float>(zoom));
            return std::string("null");
        });

}

void register_camera_script_bindings(ScriptRuntime& runtime,
                                   CameraScriptBindings&& handlers) {
    std::move(handlers).bind_all(runtime);
}
} // namespace northstarcpp::window_app_internal
