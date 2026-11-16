// Independently authored camera Scene Entity and rig tuning.
#pragma once
#include <northstarcpp/player_blueprint_camera.hpp>
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/player_blueprint_core.hpp>
#include <stdexcept>
namespace northstarcpp::gameplay {
struct CharacterCameraRig {
    float eye_height=1.45f,fov_degrees=75.f;
    float min_pitch=-85.f,max_pitch=85.f;
};
inline CharacterCameraRig camera_rig(Scene const& scene,
    std::uint64_t id) {
    CharacterCameraRig value;
    // A camera is an independent child Entity, like CameraComponent in UE.
    // Old Pawn-owned rigs remain readable until the scene is reauthored.
    std::uint64_t rig_entity=id;
    for(auto const& object:scene.objects)
        if(object.node.parent==id &&
           player_blueprint::find(scene,object.node.id,
                                  player_blueprint::camera_attachment_type)) {
            rig_entity=object.node.id;
            break;
        }
    if(auto const* c=player_blueprint::find(
        scene,rig_entity,player_character::camera_type)) {
        auto const& x=*c;
        if(!player_character::detail::camera_valid(nullptr,x.version,
            x.payload.data(),x.payload.size()))
            throw std::invalid_argument("Invalid character camera rig");
        auto const* bytes=x.payload.data();
        value.eye_height=player_character::detail::getfloat(bytes,0);
        value.fov_degrees=player_character::detail::getfloat(bytes,4);
        value.min_pitch=player_character::detail::getfloat(bytes,8);
        value.max_pitch=player_character::detail::getfloat(bytes,12);
    }
    return value;
}
// World-space camera anchor is read from the independent Camera Entity.
// Legacy Pawn-owned camera rigs continue to derive their eye point.
inline Vector3 camera_anchor(Scene const& scene,std::uint64_t pawn,
                             float legacy_eye_height) {
    for(auto const& child:scene.objects)
        if(child.node.parent==pawn &&
           player_blueprint::find(scene,child.node.id,
                 player_blueprint::camera_attachment_type) &&
           player_blueprint::find(scene,child.node.id,
                 player_character::camera_type))
            return child.position;
    for(auto const& object:scene.objects)
        if(object.node.id==pawn)
            return {object.position.x,object.position.y+legacy_eye_height,
                    object.position.z};
    throw std::invalid_argument("Camera target Pawn missing");
}
} // namespace northstarcpp::gameplay
