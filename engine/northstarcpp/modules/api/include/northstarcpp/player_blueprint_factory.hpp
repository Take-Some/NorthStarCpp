#pragma once
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/player_blueprint_camera.hpp>
#include <northstarcpp/player_blueprint_visual.hpp>
#include <northstarcpp/player_blueprint_animation.hpp>
#include <northstarcpp/player_blueprint_motor.hpp>

namespace northstarcpp::player_blueprint {
struct Layout {
    std::uint64_t pawn=0,controller=0,camera=0,mesh=0,motor=0;
};
inline std::optional<Layout> layout(Scene const& scene,std::uint64_t pawn) {
    if(!player_character::has_pawn(scene,pawn))return std::nullopt;
    Layout result;result.pawn=pawn;
    for(auto const& object:scene.objects) {
        auto const id=object.node.id;
        if(object.node.parent==pawn) {
            if(find(scene,id,camera_attachment_type))result.camera=id;
            if(find(scene,id,mesh_type))result.mesh=id;
            if(find(scene,id,player_character::motor_type))result.motor=id;
        }
    }
    if(auto const* controller=controller_for_pawn(scene,pawn))
        result.controller=controller->entity_id;
    return result;
}
inline Layout create(Scene& scene,std::string name,Vector3 position,
                     bool auto_possess=true) {
    if(scene.version!=3 ||name.empty()||name.size()>80||
       scene.objects.size()>max_scene_objects-5)
        throw std::invalid_argument("Player Blueprint requires a V3 scene");
    for(char c:name)if(static_cast<unsigned char>(c)<32)
        throw std::invalid_argument("Invalid Player Blueprint name");
    std::uint64_t top=0;
    for(auto const& object:scene.objects)top=std::max(top,object.node.id);
    if(top>std::numeric_limits<std::uint64_t>::max()-5)
        throw std::overflow_error("Scene entity ID capacity exhausted");
    for(float c:{position.x,position.y,position.z})
        if(!std::isfinite(c)||std::abs(c)>1000000.f)
            throw std::invalid_argument("Player position out of scene bounds");
    if(auto_possess) {
        auto const startup=scene_pawn::startup_pawn(scene);
        if(startup.id || startup.conflict!=scene_pawn::StartupConflict::None)
            throw std::invalid_argument("Scene already owns a local Player Pawn");
    }
    Scene candidate=scene;
    // Stable identity order: Controller, Pawn, Motor, Camera, SkeletalMesh.
    Layout result{top+2,top+1,top+4,top+5,top+3};
    SceneObject controller;
    controller.name=name+"Controller";
    controller.node.id=result.controller;
    controller.node.collision=false;
    controller.node.visible=false;
    controller.size=.25f;
    controller.position=position;
    SceneObject pawn;
    pawn.name=std::move(name);
    pawn.node.id=result.pawn;
    pawn.position=position;
    pawn.size=1.8f;
    pawn.color=0x56a9cbu;
    pawn.mass=80.f;
    pawn.node.collision=true;
    pawn.node.visible=false; // logical capsule, visual is the child SkeletalMesh
    SceneObject motor_object;
    motor_object.name="CharacterMotor";
    motor_object.node.id=result.motor;
    motor_object.node.parent=result.pawn;
    motor_object.node.collision=false;
    motor_object.node.visible=false;
    motor_object.position=position;
    motor_object.size=.2f;
    SceneObject camera_object;
    camera_object.name="Camera";
    camera_object.node.id=result.camera;
    camera_object.node.parent=result.pawn;
    camera_object.node.collision=false;
    camera_object.node.visible=false;
    camera_object.position={position.x,position.y+1.65f,position.z};
    camera_object.size=.15f;
    SceneObject mesh_object;
    mesh_object.name="SkeletalMesh";
    mesh_object.node.id=result.mesh;
    mesh_object.node.parent=result.pawn;
    mesh_object.node.collision=false;
    mesh_object.node.visible=false; // until a real mesh is assigned
    mesh_object.position={position.x,position.y-.9f,position.z};
    mesh_object.size=1.f;
    candidate.objects.push_back(std::move(controller));
    candidate.objects.push_back(std::move(pawn));
    candidate.objects.push_back(std::move(motor_object));
    candidate.objects.push_back(std::move(camera_object));
    candidate.objects.push_back(std::move(mesh_object));
    player_character::add_character_components(candidate,result.pawn,auto_possess);
    // Maintain backward-compatible Pawn authoring defaults while placing the
    // operational Motor and Camera components on their own Scene Entities.
    auto migrate=[&](std::uint64_t owner,char const* type) {
        std::string payload;
        for(auto it=candidate.components.begin();it!=candidate.components.end();) {
            if(it->entity_id==result.pawn && it->type==type) {
                payload=std::move(it->payload);
                it=candidate.components.erase(it);
            } else ++it;
        }
        if(payload.empty())
            throw std::runtime_error(std::string("Missing default ")+type);
        candidate.components.push_back({
            owner,type,player_character::version,std::move(payload)});
    };
    migrate(result.motor,player_character::motor_type);
    // A non-local Pawn may have no camera rig; it still owns an independent
    // camera object that can be activated by a PlayerController later.
    if(auto const* inherited=find(candidate,result.pawn,
                                 player_character::camera_type)) {
        (void)inherited;
        migrate(result.camera,player_character::camera_type);
    } else {
        std::string rig;
        for(float v:{1.65f,75.f,-85.f,85.f})
            player_character::detail::pushfloat(rig,v);
        candidate.components.push_back({
            result.camera,player_character::camera_type,
            player_character::version,std::move(rig)});
    }
    assign(candidate,result.controller,controller_type,
           serialize_controller(result.pawn,auto_possess));
    set_camera(candidate,result.camera,{});
    set_mesh(candidate,result.mesh,{});
    set_animation(candidate,result.mesh,{});
    validate_scene(candidate);
    scene=std::move(candidate);
    return result;
}
}
