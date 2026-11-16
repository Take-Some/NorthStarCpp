// CameraComponent-to-CameraDirector bridge; no renderer-specific dependencies.
#pragma once
#include <northstarcpp/camera.hpp>
#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/player_controller.hpp>
#include <algorithm>
namespace northstarcpp::gameplay {
inline bool configure_player_camera(CameraDirector& director,
                                    Scene const& scene,std::uint64_t pawn_id) {
    auto hierarchy=player_blueprint::layout(scene,pawn_id);
    if(!hierarchy||!hierarchy->camera)return false;
    auto const attachment=player_blueprint::camera(scene,hierarchy->camera);
    auto const rig=camera_rig(scene,pawn_id);
    auto settings=director.settings();
    settings.default_fov_y=std::clamp(rig.fov_degrees,25.f,120.f);
    settings.third_person_distance=attachment.spring_arm_length>=.5f?
        attachment.spring_arm_length:4.f;
    settings.shoulder_offset=attachment.shoulder;
    settings.follow_smoothing=attachment.lag;
    director.configure(settings);
    director.set_mode(attachment.mode==player_blueprint::ViewMode::ThirdPerson?
                      CameraMode::third_person:CameraMode::first_person,0.f);
    return true;
}
} // namespace northstarcpp::gameplay
