// CameraComponent-to-CameraDirector bridge; no renderer-specific dependencies.
#pragma once
#include <northstarcpp/camera.hpp>
#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/player_controller.hpp>
#include <algorithm>
namespace northstarcpp::gameplay {
inline bool configure_player_camera(CameraDirector& director,
                                    Scene const& scene,std::uint64_t pawn_id,
                                    float transition_seconds=0.f) {
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
                      CameraMode::third_person:CameraMode::first_person,transition_seconds);
    return true;
}

// Applies camera-rig policy for a Pawn or its independently-authored camera
// child. This never changes the Controller's possessed Pawn or physics world.
inline bool configure_view_target_camera(CameraDirector& director,
    Scene const& scene,std::uint64_t view_entity,float transition_seconds=0.f) {
    if(!view_entity) return false;
    if(valid_controller_pawn(scene,view_entity))
        return configure_player_camera(director,scene,view_entity,transition_seconds);
    for(auto const& object:scene.objects) {
        if(object.node.id!=view_entity)continue;
        if(!object.node.parent ||
           !valid_controller_pawn(scene,object.node.parent) ||
           !player_blueprint::find(scene,view_entity,
               player_blueprint::camera_attachment_type) ||
           !player_blueprint::find(scene,view_entity,
               player_character::camera_type))
            return false;
        // Camera-only targets reuse the owning Pawn's character rig, while
        // the camera's independently authored transform remains the view anchor.
        return configure_player_camera(
            director,scene,object.node.parent,transition_seconds);
    }
    return false;
}
} // namespace northstarcpp::gameplay
