#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/player_camera_runtime.hpp>
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
    player_blueprint::Camera thirdPerson;
    thirdPerson.mode=player_blueprint::ViewMode::ThirdPerson;
    thirdPerson.spring_arm_length=5.f;
    thirdPerson.shoulder=.75f;
    player_blueprint::set_camera(authored,ids.camera,thirdPerson);
    PhysicsScene world(authored);
    auto startupEvent=world.poll_controller_event();
    if(!startupEvent ||
       startupEvent->kind!=gameplay::PossessionEventKind::OnPossess ||
       startupEvent->pawn!=ids.pawn || startupEvent->revision!=1 ||
       world.poll_controller_event())
        throw std::runtime_error("Auto possession must emit one OnPossess");
    if(world.controlled_entity()!=ids.pawn)
        throw std::runtime_error("PlayerController did not possess the Pawn");
    if(world.view_target()!=ids.pawn)
        throw std::runtime_error("Camera did not default to possessed Pawn");
    world.set_view_target(ids.camera);
    if(world.view_target()!=ids.camera ||
       world.view_target_index()!=gameplay::entity_index(authored,ids.camera) ||
       world.controlled_entity()!=ids.pawn)
        throw std::runtime_error("PhysicsScene View Target changed Pawn possession");
    world.reset_view_target();
    if(world.view_target()!=ids.pawn)
        throw std::runtime_error("PhysicsScene failed to restore Pawn camera");
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
    CameraDirector director;
    if(!gameplay::configure_player_camera(director,world.scene(),world.controlled_entity()) ||
       director.mode()!=CameraMode::third_person ||
       director.settings().third_person_distance!=5.f ||
       director.settings().shoulder_offset!=.75f)
        throw std::runtime_error("Possessed Pawn camera could not configure CameraDirector");
    world.set_view_target(ids.camera);
    if(!gameplay::configure_view_target_camera(director,world.scene(),world.view_target()) ||
       director.settings().third_person_distance!=5.f ||
       world.controlled_entity()!=ids.pawn)
        throw std::runtime_error("Camera target changed Pawn possession");
    world.reset_view_target();
    if(!gameplay::configure_view_target_camera(director,world.scene(),world.view_target()) ||
       world.view_target()!=ids.pawn)
        throw std::runtime_error("View target did not restore Pawn camera");
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
    // Runtime camera Entity deletion must not leave View Target attached to
    // a tombstoned physics index or accidentally unpossess the live Pawn.
    SceneObject spectator;
    spectator.name="RuntimeSpectator";
    spectator.node.id=45001;
    spectator.position={50.f,2.f,40.f};
    spectator.size=.25f;
    spectator.mass=0.f;
    spectator.node.collision=false;
    auto const runtime_camera=world.spawn_runtime(spectator);
    world.set_view_target(spectator.node.id);
    world.destroy_runtime(runtime_camera);
    if(world.view_target()!=ids.pawn || world.controlled_entity()!=ids.pawn)
        throw std::runtime_error("Deleting runtime View Target lost Pawn ownership");
    bool deadCameraRejected=false;
    try {world.set_view_target(spectator.node.id);}
    catch(std::invalid_argument const&) {deadCameraRejected=true;}
    if(!deadCameraRejected)
        throw std::runtime_error("Destroyed runtime Camera Entity was accepted");
    world.unpossess();
    auto removedEvent=world.poll_controller_event();
    if(!removedEvent ||
       removedEvent->kind!=gameplay::PossessionEventKind::OnUnPossess ||
       removedEvent->pawn!=ids.pawn || removedEvent->revision!=2)
        throw std::runtime_error("Jolt UnPossess event missing");
    if(world.player_animation())
        throw std::runtime_error("Animation still bound after Unpossess");
    world.possess(ids.pawn);
    auto rebound=world.poll_controller_event();
    if(!rebound ||
       rebound->kind!=gameplay::PossessionEventKind::OnPossess ||
       rebound->pawn!=ids.pawn || rebound->revision!=3)
        throw std::runtime_error("Jolt OnPossess missing after rebind");
    world.possess(ids.pawn);
    if(world.poll_controller_event())
        throw std::runtime_error("Duplicate Jolt Possess generated event");
    if(!world.player_animation())
        throw std::runtime_error("Animation not rebound after Possess");
    // Clip-driven movement enters the selected Jolt CharacterMotor as
    // collision-resolved velocity, not an unvalidated Scene teleport.
    Scene constrained;constrained.version=3;
    constrained.name="Root Motion Collision QA";
    auto subject=player_blueprint::create(constrained,
        "RootMotionPawn",{0.f,0.f,0.f},true);
    SceneObject wall;
    wall.node.id=987654;
    wall.position={0.f,0.f,2.4f};
    wall.size=1.4f;
    wall.mass=0.f;
    constrained.objects.push_back(wall);
    PhysicsScene blocking(constrained);
    if(!blocking.supports_root_motion())
        throw std::runtime_error("Jolt provider lacks Root Motion extension");
    if(blocking.enqueue_root_motion(987654,{0.f,0.f,.1f},1.f/60.f)||
       blocking.enqueue_root_motion(subject.mesh,{0.f,.06f,0.f},1.f/60.f))
        throw std::runtime_error("Root Motion accepted wrong entity/vertical teleport");
    auto const avatar=gameplay::entity_index(blocking.scene(),subject.pawn).value();
    auto const initial_z=blocking.scene().objects[avatar].position.z;
    for(int tick=0;tick<80;++tick) {
        if(!blocking.enqueue_root_motion(subject.mesh,
            {0.f,0.f,.05f},1.f/60.f))
            throw std::runtime_error("Valid Root Motion rejected tick="+std::to_string(tick));
        blocking.step(1.f/60.f);
    }
    auto const resolved_z=blocking.scene().objects[avatar].position.z;
    std::cout<<"Jolt Root Motion world z="<<resolved_z
             <<" start="<<initial_z<<" collisionWallZ=2.4\\n";
    if(resolved_z-initial_z<.20f||resolved_z>1.9f)
        throw std::runtime_error(
            "Root Motion did not advance or penetrated collision wall");
    blocking.unpossess();
    if(blocking.enqueue_root_motion(subject.mesh,{.03f,0,0},1.f/60.f))
        throw std::runtime_error("Unpossessed motor accepted Root Motion");
    // One animation delta must not be multiplied by the number of Jolt
    // fixed substeps when a frame stalls and the host steps 3 ticks.
    Scene frame_batch;frame_batch.version=3;
    frame_batch.name="Root Motion Fixed Step Conservation";
    auto batch_ids=player_blueprint::create(frame_batch,
        "SubstepPawn",{0.f,0.f,0.f},true);
    PhysicsScene one_step(frame_batch),three_steps(frame_batch);
    auto const batch_pawn=gameplay::entity_index(
        frame_batch,batch_ids.pawn).value();
    if(!one_step.enqueue_root_motion(batch_ids.mesh,{0.f,0.f,.05f},
            1.f/60.f)||
       !three_steps.enqueue_root_motion(batch_ids.mesh,{0.f,0.f,.05f},
            1.f/60.f))
        throw std::runtime_error("Substep Root Motion request refused");
    one_step.step(1.f/60.f);
    three_steps.step(3.f/60.f);
    auto const delta_once=one_step.scene().objects[batch_pawn].position.z;
    auto const delta_three=three_steps.scene().objects[batch_pawn].position.z;
    if(!(delta_once>0.f &&delta_three>=delta_once &&
         delta_three<delta_once*2.5f))
        throw std::runtime_error(
            "A stalled physics frame replayed one Root Motion delta multiple times");
    // Multiple physical Pawns: AI velocity is integrated by Jolt at the
    // fixed step without possessing the player's controller or teleporting.
    Scene ai_scene; ai_scene.version=3; ai_scene.name="Jolt Multi-Agent QA";
    auto local=player_blueprint::create(ai_scene,"Human",{0.f,0.f,-4.f},true);
    auto npc=player_blueprint::create(ai_scene,"Autonomous",{2.f,0.f,0.f},false);
    SceneObject obstacle;
    obstacle.node.id=999001;
    obstacle.name="AI Collision Wall";
    obstacle.position={4.2f,0.f,0.f};
    obstacle.size=1.4f;
    obstacle.mass=0.f;
    ai_scene.objects.push_back(obstacle);
    PhysicsScene agent_world(ai_scene);
    if(!agent_world.supports_ai_motor())
        throw std::runtime_error("Jolt provider lacks multi-agent AI motor");
    const auto npc_index=gameplay::entity_index(ai_scene,npc.pawn).value();
    const auto initial=agent_world.scene().objects[npc_index].position;
    if(agent_world.controlled_entity()!=local.pawn)
        throw std::runtime_error("AI scene took possession from human");
    bool invalid=false;
    try {agent_world.set_ai_move_intent(local.pawn,1.f,0.f,3.f);}
    catch(std::invalid_argument const&) {invalid=true;}
    if(!invalid)throw std::runtime_error("AI motor accepted currently controlled player");
    for(int tick=0;tick<120;++tick) {
        agent_world.set_ai_move_intent(npc.pawn,1.f,0.f,2.f);
        agent_world.step(1.f/60.f);
    }
    const auto moved=agent_world.scene().objects[npc_index].position;
    if(moved.x<=initial.x+.35f || moved.x>=3.4f ||
       agent_world.controlled_entity()!=local.pawn)
        throw std::runtime_error("Jolt AI failed to move or penetrated wall");
    agent_world.clear_ai_move_intent(npc.pawn);
    auto const stopped=agent_world.scene().objects[npc_index].position;
    for(int tick=0;tick<20;++tick)agent_world.step(1.f/60.f);
    if(std::abs(agent_world.scene().objects[npc_index].position.x-stopped.x)>.25f)
        throw std::runtime_error("AI stop left unbounded horizontal velocity");
    std::cout<<"Jolt AI multi-agent collision: PASS (x="<<moved.x<<")\\n";
    std::cout<<"Jolt Player Blueprint: Possess/Unpossess + "
             <<"Camera/SkeletalMesh following + Animator PASS\n";
    return 0;
}
