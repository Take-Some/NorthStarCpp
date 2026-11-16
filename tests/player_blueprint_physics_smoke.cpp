#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/player_animation_controller.hpp>
#include <northstarcpp/physics.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
int main() {
    using namespace northstarcpp;
    Scene authored;authored.version=3;authored.name="Player Runtime Attachments";
    auto ids=player_blueprint::create(authored,"Player",{9.f,0.f,9.f},true);
    player_blueprint::Animation graph;
    graph.idle="animations/idle.ycd";
    graph.walk="animations/walk.ycd";
    graph.run="animations/run.ycd";
    graph.jump="animations/jump.ycd";
    graph.fall="animations/fall.ycd";
    player_blueprint::set_animation(authored,ids.mesh,graph);
    // Authored Camera Entity owns its attachment offset, not a hardcoded
    // Pawn+eye-height position. Motor is a separate following Scene Entity.
    auto authoredCamera=gameplay::entity_index(authored,ids.camera).value();
    authored.objects[authoredCamera].position.x+=.35f;
    authored.objects[authoredCamera].position.z+=.20f;
    PhysicsScene world(authored);
    if(world.controlled_entity()!=ids.pawn)
        throw std::runtime_error("PlayerController did not possess the Pawn");
    auto const pawn=gameplay::entity_index(world.scene(),ids.pawn).value();
    auto const motor=gameplay::entity_index(world.scene(),ids.motor).value();
    auto const camera=gameplay::entity_index(world.scene(),ids.camera).value();
    auto const mesh=gameplay::entity_index(world.scene(),ids.mesh).value();
    for(int i=0;i<35;++i)
        world.step(1.f/60.f,1.f,0.f);
    auto const& bodies=world.scene().objects;
    auto const& position=bodies[pawn].position;
    auto const& motorPosition=bodies[motor].position;
    auto const& cameraPosition=bodies[camera].position;
    auto const& meshPosition=bodies[mesh].position;
    auto cameraEye=gameplay::camera_anchor(world.scene(),ids.pawn,1.45f);
    if(std::abs(cameraEye.x-cameraPosition.x)>.001f ||
       std::abs(cameraEye.y-cameraPosition.y)>.001f)
        throw std::runtime_error("Camera anchor ignored Camera Entity transform");
    auto offset=player_blueprint::mesh(world.scene(),ids.mesh).offset;
    if(std::abs(motorPosition.x-position.x)>.001f||
       std::abs(motorPosition.y-position.y)>.001f||
       std::abs(motorPosition.z-position.z)>.001f||
       std::abs(cameraPosition.x-position.x-.35f)>.001f||
       std::abs(cameraPosition.z-position.z-.20f)>.001f||
       std::abs(cameraPosition.y-position.y-1.65f)>.001f||
       std::abs(meshPosition.x-position.x-offset.x)>.001f||
       std::abs(meshPosition.y-position.y-offset.y)>.001f||
       std::abs(meshPosition.z-position.z-offset.z)>.001f)
        throw std::runtime_error("SkeletalMesh/Camera did not follow physics Pawn");
    auto sample=world.player_animation();
    if(!sample || sample->mesh_id!=ids.mesh ||sample->pawn_id!=ids.pawn||
       sample->state.empty())
        throw std::runtime_error("Player Animation runtime did not update");
    world.unpossess();
    if(world.player_animation())
        throw std::runtime_error("Animation still bound after Unpossess");
    world.possess(ids.pawn);
    if(!world.player_animation())
        throw std::runtime_error("Animation not rebound after Possess");
    std::cout<<"Jolt Player Blueprint: Possess/Unpossess + "
             <<"Camera/SkeletalMesh following + Animator PASS\n";
    return 0;
}
