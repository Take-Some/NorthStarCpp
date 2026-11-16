#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/player_animation_controller.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include "../engine/northstarcpp/modules/editor/editor_prefab.hpp"
#include "../engine/northstarcpp/modules/editor/editor_visual_prefab.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,char const* error) {
    if(!condition)throw std::runtime_error(error);
}
}
int main(int argc,char** argv) {
    using namespace northstarcpp;
    namespace player=player_blueprint;
    if(argc==3 && std::string(argv[1])=="--check-demo-visual") {
        auto scene=load_scene(argv[2]);
        auto layout=player::layout(scene,1);
        // Historical demo Pawn has no complete Blueprint mesh hierarchy;
        // explicitly resolve the scene-authored SkeletalMesh by its own ID.
        auto mesh=std::find_if(scene.objects.begin(),scene.objects.end(),
            [](SceneObject const& object) {
                return object.name=="SkeletalMesh" && object.node.parent==1;
            });
        check(mesh!=scene.objects.end(),"Authored SkeletalMesh missing");
        auto settings=player::mesh(scene,mesh->node.id);
        check(!settings.prefab.empty(),"Visual prefab reference missing");
        auto const count=scene.objects.size(), stable=mesh->node.id;
        auto root=std::filesystem::path(argv[2]).parent_path();
        auto source=editor::visual_prefab::load(root,settings.prefab);
        auto composed=scene;
        editor::visual_prefab::apply(composed,stable,source);
        check(scene.objects.size()==count,"Authoring scene was mutated");
        auto index=gameplay::entity_index(composed,stable);
        check(index.has_value() &&
              composed.objects[*index].node.asset==
                  "characters/abby/models/abby.ydd" &&
              !composed.objects[*index].node.collision,
              "Prefab visual composition is missing native Abby geometry");
        std::cout<<"PASS: Abby native visual prefab resolved; Pawn ID 1 and "
                 <<"SkeletalMesh ID "<<stable<<" retained\n";
        return 0;
    }
    if(argc==3 && std::string(argv[1])=="--author-visual") {
        auto scene=load_scene(argv[2]);
        auto pawn=std::find_if(scene.objects.begin(),scene.objects.end(),
            [](SceneObject const& e){return e.node.id==1;});
        if(pawn==scene.objects.end())
            throw std::runtime_error("Demo Pawn is missing");
        for(auto const& o:scene.objects)
            if(o.name=="SkeletalMesh")
                throw std::runtime_error("Demo already owns SkeletalMesh");
        std::uint64_t maxid=0;
        for(auto const& o:scene.objects)maxid=std::max(maxid,o.node.id);
        SceneObject visual;
        visual.node.id=maxid+1;
        visual.node.parent=1;
        visual.node.visible=true;
        visual.node.collision=false;
        visual.node.asset="characters/abby/models/abby.ydd";
        visual.name="SkeletalMesh";
        visual.position=pawn->position;
        visual.size=1.f;
        scene.objects.push_back(visual);
        player::Mesh mesh;
        mesh.offset={0.f,0.f,0.f};
        mesh.hide_in_first_person=false;
        mesh.prefab="prefabs/abby.nprefab";
        player::set_mesh(scene,visual.node.id,mesh);
        validate_scene(scene);
        save_scene(scene,argv[2]);
        std::cout<<"Native authored SkeletalMesh id="<<visual.node.id<<"\n";
        return 0;
    }
    Scene scene;
    scene.version=3;
    scene.name="Player Blueprint Smoke";
    auto created=player::create(scene,"Hero",{12,0,15},true);
    check(scene.objects.size()==5,"Expected 5 separate entities");
    check(created.controller && created.pawn && created.motor &&
          created.camera && created.mesh &&
          created.controller!=created.pawn &&
          created.motor!=created.pawn && created.motor!=created.camera,
          "Controller, Motor and Camera need unique Scene Entity IDs");
    auto layout=player::layout(scene,created.pawn);
    check(layout&&layout->controller==created.controller&&
          layout->camera==created.camera&&layout->mesh==created.mesh &&
          layout->motor==created.motor,
          "Player hierarchy not discoverable");
    auto motorIndex=gameplay::entity_index(scene,created.motor).value();
    auto cameraIndex=gameplay::entity_index(scene,created.camera).value();
    auto meshIndex=gameplay::entity_index(scene,created.mesh).value();
    check(scene.objects[motorIndex].node.parent==created.pawn &&
          scene.objects[cameraIndex].node.parent==created.pawn &&
          scene.objects[meshIndex].node.parent==created.pawn,
          "Motor/Camera/Mesh not parented under Pawn");
    check(!scene.objects[motorIndex].node.collision &&
          !scene.objects[cameraIndex].node.collision &&
          !scene.objects[meshIndex].node.collision,
          "Motor/Camera/Mesh must not create duplicate physics shapes");
    check(!player_blueprint::find(scene,created.pawn,
            player_character::motor_type) &&
          player_blueprint::find(scene,created.motor,
            player_character::motor_type),
          "Motor tuning must be stored on its own entity, not Pawn");
    gameplay::PlayerController controller;
    check(controller.auto_possess(scene),"Controller failed to auto possess");
    check(controller.controlled_entity()==created.pawn,"Controller owns wrong Pawn");
    controller.unpossess();
    check(!controller.possessed(),"Controller failed Unpossess");
    controller.possess(scene,created.pawn);
    check(controller.possessed(),"Controller failed re-possess");

    auto tune=gameplay::motor_settings(scene,created.pawn);
    check(tune.run_speed>tune.walk_speed &&
          tune.jump_speed>0.f,"Character Motor metadata missing");
    tune.walk_speed=6.25f;
    player_blueprint::set_motor(scene,created.motor,tune);
    check(gameplay::motor_settings(scene,created.pawn).walk_speed==6.25f,
          "Independent CharacterMotor settings not applied");
    Scene legacyMotor=scene;
    for(auto& component:legacyMotor.components)
        if(component.entity_id==created.motor &&
           component.type==player_character::motor_type)
            component.entity_id=created.pawn;
    check(gameplay::motor_settings(legacyMotor,created.pawn).walk_speed==6.25f,
          "Legacy Pawn-owned CharacterMotor compatibility broken");
    auto collision=collision_authoring::lookup(scene,created.pawn);
    check(collision && collision->shape==collision_authoring::Shape::Capsule &&
          collision->object_type==collision_authoring::ObjectType::Pawn,
          "Capsule Collider not attached to Pawn");
    auto rig=gameplay::camera_rig(scene,created.pawn);
    check(rig.eye_height>0.f && rig.fov_degrees>=50.f,
          "Camera Entity rig failed to resolve through Pawn");

    player::Camera newCamera;
    newCamera.mode=player::ViewMode::ThirdPerson;
    newCamera.spring_arm_length=3.7f;
    newCamera.shoulder=.35f;
    player::set_camera(scene,created.camera,newCamera);
    auto restoredCamera=player::camera(scene,created.camera);
    check(restoredCamera.mode==player::ViewMode::ThirdPerson &&
          restoredCamera.spring_arm_length==3.7f,"Camera setup round-trip failed");

    player::Mesh visual;
    visual.skeleton="rigs/hero.skeleton.json";
    visual.material="materials/hero.ymat";
    visual.prefab="prefabs/hero.nprefab";
    visual.offset={.1f,-.7f,.15f};
    player::set_mesh(scene,created.mesh,visual);
    scene.objects[meshIndex].node.asset="models/hero.ydd";
    scene.objects[meshIndex].node.visible=true;
    player::Animation animation;
    animation.idle="animations/hero_idle.ycd";
    animation.walk="animations/hero_walk.ycd";
    animation.run="animations/hero_run.ycd";
    animation.jump="animations/hero_jump.ycd";
    animation.fall="animations/hero_fall.ycd";
    player::set_animation(scene,created.mesh,animation);
    check(player::pick_animation(animation,0,true,false)==player::AnimationState::Idle &&
          player::pick_animation(animation,2,true,false)==player::AnimationState::Walk &&
          player::pick_animation(animation,8,true,false)==player::AnimationState::Run &&
          player::pick_animation(animation,8,false,true)==player::AnimationState::Jump &&
          player::pick_animation(animation,2,false,false)==player::AnimationState::Fall,
          "Animation State Machine transitions not deterministic");
    check(player::clip(animation,player::AnimationState::Run)==animation.run,
          "Animation Controller failed to select configured YCD clip");
    player_animation::Controller animator(animation);
    animator.advance(.1f,2.f,true,false);
    check(animator.sample().state==player::AnimationState::Walk &&
          animator.sample().blend_alpha<1.f,
          "Animation Controller did not transition with blending");
    animator.advance(.2f,8.f,true,false);
    check(animator.sample().state==player::AnimationState::Run &&
          animator.sample().clip==animation.run &&
          animator.sample().previous_clip==animation.walk,
          "Animation graph did not select the run clip with previous blend");
    animator.advance(.1f,8.f,false,true);
    check(animator.sample().state==player::AnimationState::Jump,
          "Animation Controller jump state missing");
    animator.advance(.1f,8.f,false,false);
    check(animator.sample().state==player::AnimationState::Fall,
          "Animation Controller falling state missing");

    Scene sourceVisual;sourceVisual.version=3;sourceVisual.name="Visual";
    SceneObject rootVisual;
    rootVisual.node.id=77;
    rootVisual.name="Hero Visual";
    rootVisual.node.asset="characters/hero/models/hero.ydd";
    sourceVisual.objects.push_back(rootVisual);
    auto preview=scene;
    editor::visual_prefab::apply(preview,created.mesh,sourceVisual);
    check(preview.objects.size()==scene.objects.size() &&
          preview.objects[meshIndex].node.id==created.mesh &&
          preview.objects[meshIndex].node.asset==
              "characters/hero/models/hero.ydd" &&
          scene.objects[meshIndex].node.asset=="models/hero.ydd" &&
          !preview.objects[meshIndex].node.collision,
          "Visual prefab must preserve SkeletalMesh ID, no extra collider");
    // Generic Entity (not Pawn/SkeletalMesh) retains gameplay components,
    // while additional prefab visual children map back to the authoring actor.
    Scene generic;generic.version=3;generic.name="Generic Visual";
    SceneObject prop;
    prop.node.id=201;
    prop.name="Physics Prop";
    prop.node.asset="models/proxy.ydd";
    prop.node.collision=true;
    prop.node.script="scripts/prop.js";
    prop.mass=5.f;
    generic.objects.push_back(prop);
    editor::visual_prefab::bind(generic,201,"prefabs/hero.nprefab");
    check(editor::visual_prefab::reference(generic,201)==
          "prefabs/hero.nprefab" &&
          editor::visual_prefab::links(generic).size()==1,
          "Any Scene Entity must accept a visual prefab");
    SceneObject visor;
    visor.node.id=78;
    visor.node.parent=77;
    visor.name="Extra Visual";
    visor.node.asset="characters/hero/models/visor.ydd";
    visor.node.collision=true;
    visor.node.script="scripts/untrusted.js";
    sourceVisual.objects.push_back(visor);
    auto composed=generic;
    editor::visual_prefab::apply(composed,201,sourceVisual);
    check(composed.objects.size()==2 &&
          composed.objects[0].node.id==201 &&
          composed.objects[0].mass==5.f &&
          composed.objects[0].node.collision &&
          composed.objects[0].node.script=="scripts/prop.js" &&
          composed.objects[1].node.parent==201 &&
          !composed.objects[1].node.collision &&
          composed.objects[1].node.script.empty() &&
          editor::visual_prefab::authoring_index(generic,composed,1)==0 &&
          editor::visual_prefab::authoring_index(generic,composed,0)==0,
          "Visual-only composition must retain physics and map mesh picking");
    auto genericRoundTrip=parse_scene_binary(
        serialize_scene_binary(generic));
    check(editor::visual_prefab::reference(genericRoundTrip,201)==
          "prefabs/hero.nprefab",
          "Generic Visual Prefab reference lost in NEF8");
    editor::visual_prefab::bind(generic,201,"");
    check(editor::visual_prefab::reference(generic,201).empty(),
          "Removing generic Visual Prefab binding failed");

    Scene legacyVisual=scene;
    for(auto& component:legacyVisual.components)
        if(component.entity_id==created.mesh &&
           component.type==player::mesh_type)
            component.payload="0 -0.9 0 1 \"\" \"\"";
    check(player::mesh(legacyVisual,created.mesh).prefab.empty(),
          "Legacy SkeletalMesh payload must remain readable");
    validate_scene(scene);
    auto roundTrip=parse_scene_v3(serialize_scene_v3(scene));
    auto loaded=player::layout(roundTrip,created.pawn);
    check(loaded&&loaded->controller==created.controller&&
          loaded->camera==created.camera&&loaded->mesh==created.mesh &&
          loaded->motor==created.motor,
          "Player Blueprint hierarchy lost in Scene V3");
    check(player::mesh(roundTrip,created.mesh).material==visual.material &&
          player::mesh(roundTrip,created.mesh).prefab==visual.prefab &&
          player::animation(roundTrip,created.mesh).walk==animation.walk,
          "Player Mesh/Animation components lost in Scene V3");
    gameplay::PlayerController restoredController;
    check(restoredController.auto_possess(roundTrip) &&
          restoredController.controlled_entity()==created.pawn,
          "Possession lost after Scene V3 serialisation");
    auto binary=parse_scene_binary(serialize_scene_binary(roundTrip));
    check(player::layout(binary,created.pawn).has_value(),
          "Player Blueprint lost in NEF8 Scene binary");

    // Prefab root is Pawn; children survive ID remap, and possession is
    // automatic on the instanced Pawn even without standalone Controller.
    std::size_t pawnIndex=gameplay::entity_index(scene,created.pawn).value();
    auto prefab=editor_tools::capture_prefab(scene,pawnIndex);
    check(prefab.objects.size()==4,
          "Pawn prefab should contain Pawn/Motor/Camera/Mesh");
    Scene level;level.version=3;level.name="Instanced Player";
    auto root=editor_tools::instantiate_prefab(level,prefab,{21,0,0});
    auto instanced=player::layout(level,level.objects.at(root).node.id);
    check(instanced && instanced->motor && instanced->camera &&
          instanced->mesh,"Prefab instance lost motor, camera or skeletal mesh");
    check(player::animation(level,instanced->mesh).run==animation.run,
          "Prefab instance lost animation graph");
    check(level.objects.at(root).node.id!=created.pawn,
          "Prefab assigned source stable ID");
    gameplay::PlayerController instanceController;
    check(instanceController.auto_possess(level),
          "Instanced Pawn not auto-possessed");

    Scene npc;
    npc.version=3;
    auto npcIds=player::create(npc,"NPC",{0,0,0},false);
    auto npcLayout=player::layout(npc,npcIds.pawn);
    check(npcLayout && npcLayout->controller==npcIds.controller,
          "Nonlocal PlayerController lost its Pawn binding");
    gameplay::PlayerController noLocalController;
    check(!noLocalController.auto_possess(npc),
          "Non-local Pawn incorrectly auto-possessed");

    std::cout<<"Player Blueprint: Controller/Pawn/Camera/Mesh/Animation, "
             <<"Scene V3/NEF8, Prefab, Possess = PASS\n";
    return 0;
}
