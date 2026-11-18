#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/ai_controller.hpp>
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/player_animation_controller.hpp>
#include <northstarcpp/input_actions.hpp>
#include <northstarcpp/input_actions_xml.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool result,char const* message) {
    if(!result)throw std::runtime_error(message);
}
int main() {
    using namespace northstarcpp;
    Scene scene; scene.version=3; scene.name="Gameplay Framework QA";
    SceneObject player;player.node.id=101;player.name="Local Pawn";
    SceneObject npc;npc.node.id=102;npc.name="NPC Pawn";
    SceneObject camera;camera.node.id=103;camera.name="Camera";
    scene.objects={player,npc,camera};
    auto addPawn=[&](std::uint64_t id) {
        std::string payload;
        player_character::detail::push32(payload,1);
        player_character::detail::push32(payload,0);
        player_character::detail::push32(payload,1);
        scene.components.push_back({id,player_character::pawn_type,1,payload});
    };
    addPawn(101);addPawn(102);
    // The same controller survives possession transfers. Transition order
    // is OnPossess(A), OnUnPossess(A), OnPossess(B), with one revision per handoff.
    gameplay::PlayerController transferred;
    require(transferred.possess(scene,101) &&
            transferred.possess(scene,102) &&
            !transferred.possess(scene,102),
            "Stable Pawn transfer was not idempotent");
    auto e1=transferred.poll_possession_event();
    auto e2=transferred.poll_possession_event();
    auto e3=transferred.poll_possession_event();
    require(e1 && e2 && e3 &&
            e1->kind==gameplay::PossessionEventKind::OnPossess &&
            e1->pawn==101 && e1->revision==1 &&
            e2->kind==gameplay::PossessionEventKind::OnUnPossess &&
            e2->pawn==101 && e2->other_pawn==102 && e2->revision==2 &&
            e3->kind==gameplay::PossessionEventKind::OnPossess &&
            e3->pawn==102 && e3->other_pawn==101 && e3->revision==2 &&
            !transferred.poll_possession_event() &&
            transferred.possession_revision()==2,
            "Possession transfer events were unordered or duplicated");
    gameplay::PlayerController local;
    require(!local.possessed() && local.possession_revision()==0,
            "New controller must not possess a Pawn by default");
    bool invalidPawnRejected=false;
    try {local.possess(scene,103);} // Camera is an Entity, but not a Pawn.
    catch(std::invalid_argument const&) {invalidPawnRejected=true;}
    require(invalidPawnRejected && !local.possessed() &&
            local.possession_revision()==0,
            "Invalid Pawn must not mutate controller state");
    local.possess(scene,101);
    auto const first_revision=local.possession_revision();
    local.possess(scene,101);
    require(first_revision==1 && local.possession_revision()==first_revision,
            "Duplicate possession must be idempotent");
    local.set_view_target(scene,103);
    require(local.controlled_entity()==101 && local.view_target()==103,
            "View Target changed controlled Pawn");
    gameplay::AIController ai;
    ai.possess(scene,102);
    require(ai.move_to({3.f,0.f,4.f},.2f) &&
            !ai.move_to({3.f,0.f,4.f},.2f),
            "Repeated AI target must preserve NavMesh progress");
    require(ai.move_to({4.f,0.f,4.f},.2f) &&
            ai.move_to({3.f,0.f,4.f},.2f),
            "Changing AI target must trigger route invalidation");
    auto move=ai.next_intent(scene);
    require(!move.arrived && std::abs(move.distance-5.f)<.001f &&
            std::abs(move.direction.x-.6f)<.001f &&
            std::abs(move.direction.z-.8f)<.001f,
            "AIController movement intent is incorrect");
    scene.objects[1].position={3.f,0.f,4.f};
    require(ai.next_intent(scene).arrived && !ai.has_goal(),
            "AIController did not arrive at its destination");
    scene.components.pop_back();
    ai.reconcile(scene);
    transferred.reconcile(scene);
    auto deleted=transferred.poll_possession_event();
    require(deleted &&
            deleted->kind==gameplay::PossessionEventKind::OnUnPossess &&
            deleted->pawn==102 && deleted->other_pawn==0 &&
            deleted->revision==3 && !transferred.possessed(),
            "Destroyed Pawn failed to dispatch OnUnPossess");
    require(!ai.possessed() && ai.possession_revision()==2,
            "AIController retained missing Pawn component or stale revision");
    require(local.controlled_entity()==101 && local.view_target()==103,
            "AI Pawn destruction unexpectedly detached the PlayerController");
    local.unpossess();
    require(!local.possessed() && local.view_target()==103 &&
            local.possession_revision()==2,
            "Unpossess must retain the independent cinematic View Target");
    auto localStart=local.poll_possession_event();
    auto localStop=local.poll_possession_event();
    require(localStart && localStop &&
            localStart->kind==gameplay::PossessionEventKind::OnPossess &&
            localStop->kind==gameplay::PossessionEventKind::OnUnPossess &&
            localStop->pawn==101 && localStop->revision==2,
            "Local controller events lost when camera override was retained");
    local.reset_view_target();
    require(!local.view_target() && !local.has_view_target_override(),
            "Reset View Target left stale cinematic ownership");
    std::array<std::uint8_t,512> pressed{};
    gameplay::InputActionMap map;
    pressed['W']=1;
    require(map.action_down("MoveForward",pressed.data(),pressed.size()),
            "Gameplay context was not active");
    map.set_context_enabled("Vehicle",true);
    require(map.action_down("Accelerate",pressed.data(),pressed.size()) &&
           !map.action_down("MoveForward",pressed.data(),pressed.size()),
           "Higher-priority Vehicle context did not consume key");
    map.set_context_enabled("Vehicle",false);
    map.bind("Gameplay","MoveForward",'I');
    require(!map.action_down("MoveForward",pressed.data(),pressed.size()),
            "Old gameplay key survived remap");
    pressed['I']=1;
    require(map.action_down("MoveForward",pressed.data(),pressed.size()),
            "Remapped input action is not active");
    require(!map.action_down("MoveForward",pressed.data(),pressed.size(),true),
            "UI capture leaked gameplay movement");
    pressed[27]=1;
    require(map.action_down("Cancel",pressed.data(),pressed.size(),true) &&
           !map.action_down("Pause",pressed.data(),pressed.size(),true),
           "Modal UI did not capture Escape");
    auto const xml=gameplay::serialize_input_actions(map);
    auto restored=gameplay::parse_input_actions(xml);
    require(restored.context_enabled("Gameplay") &&
           !restored.context_enabled("Vehicle") &&
           restored.action_down("MoveForward",pressed.data(),pressed.size()) &&
           !restored.action_down("MoveForward",pressed.data(),pressed.size(),true),
           "Project XML input action roundtrip lost contexts or remap");
    bool xmlRejected=false;
    try {gameplay::parse_input_actions("<InputActions version='99'/>");}
    catch(std::exception const&) {xmlRejected=true;}
    require(xmlRejected,"Unsupported InputActions XML version was accepted");
    bool rejected=false;
    try{map.bind("Gameplay","Jump",'I');}
    catch(std::invalid_argument const&){rejected=true;}
    require(rejected,"Conflicting action binding was accepted");
    player_blueprint::Animation clips;
    clips.idle="idle.ycd";clips.walk="walk.ycd";clips.run="run.ycd";
    clips.blend_seconds=.15f;
    player_animation::Controller anim(clips);
    anim.advance(.2f,2.f,true,false);
    auto const previous=anim.sample().clip;
    anim.advance(.016f,2.f,false,true);
    require(!previous.empty() && anim.sample().clip==previous &&
            anim.sample().state==player_blueprint::AnimationState::Walk,
            "Unbound Jump switched to bind pose");
    std::cout<<"Gameplay Framework: Input Context / AI Control / Camera Target / Animation continuity PASS\n";
}
