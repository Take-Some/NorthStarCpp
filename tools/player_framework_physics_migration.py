from pathlib import Path
R=Path(__file__).resolve().parents[1]
def edit(file,changes):
 p=R/file;s=p.read_text(encoding="utf-8-sig")
 for old,new,n in changes:
  k=s.count(old)
  if k!=n:raise ValueError(f"{file}: match {k} vs {n}: {old[:80]}")
  s=s.replace(old,new)
 p.write_text(s,encoding="utf-8",newline="")
 print("updated",file)
n="engine/northstarcpp/modules/"
edit(n+"api/include/northstarcpp/physics_plugin.h",[
 ("    uint32_t player;","    uint32_t initially_controlled; // physics ABI 4 compatibility, not SceneObject ownership",1),
])
edit(n+"api/include/northstarcpp/physics.hpp",[
 ('    void jump();','    void jump();\n    // Canonical PlayerController -> provider handoff; stable Scene V3 IDs.\n    void possess(std::uint64_t entity_id);\n    void unpossess();\n    std::uint64_t controlled_entity() const noexcept;\n    std::optional<std::size_t> controlled_index() const noexcept;\n    void set_sprinting(bool active);',1),
])
edit(n+"host/physics.cpp",[
 ('#include <northstarcpp/physics.hpp>','#include <northstarcpp/physics.hpp>\n#include <northstarcpp/player_controller.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('        ecs.load(snapshot);','        controller.auto_possess(snapshot);\n        ecs.load(snapshot);',1),
 ('        vehicle_api = module.query_extension<NorthStarPhysicsVehicleApi>(',
  '        character_api = module.query_extension<NorthStarPhysicsCharacterApi>(\n            NORTHSTARCPP_PHYSICS_CHARACTER_MODULE_ABI);\n        vehicle_api = module.query_extension<NorthStarPhysicsVehicleApi>(',1),
 ('                object.size, object.mass, object.player ? 1u : 0u});',
  '                object.size,object.mass,\n                controller.controlled_entity()==object.node.id ? 1u : 0u});',1),
 ('            read_poses();','            if(controller.possessed()) {\n                if(!character_api || !character_api->possess_body ||\n                   !character_api->configure_motor || !character_api->set_sprinting)\n                    throw std::runtime_error("Selected physics provider lacks Pawn possession API");\n                auto const index=controller.controlled_index(snapshot);\n                if(!index)throw std::runtime_error("Controller Pawn not in physics world");\n                auto const tune=gameplay::motor_settings(snapshot,controller.controlled_entity());\n                NorthStarPhysicsCharacterMotor motor{tune.walk_speed,tune.run_speed,\n                    tune.jump_speed,tune.acceleration,tune.air_control,tune.gravity_scale};\n                if(!character_api->configure_motor(world,std::uint32_t(*index),&motor) ||\n                   !character_api->possess_body(world,std::uint32_t(*index)))\n                    throw module.error("Failed to initialize Jolt Pawn");\n            }\n            read_poses();',1),
 ('    NorthStarPhysicsVehicleApi const* vehicle_api = nullptr;',
  '    NorthStarPhysicsVehicleApi const* vehicle_api = nullptr;\n    NorthStarPhysicsCharacterApi const* character_api = nullptr;\n    gameplay::PlayerController controller;',1),
 ('    if (object.player || object.name.empty()',
  '    if (object.name.empty()',1),
 ('if (object.player || object.mass>0.f ||',
  'if (scene_pawn::is_pawn(state->snapshot,object) || object.mass>0.f ||',1),
 ('objects[index].mass<=0.f || objects[index].player ||',
  'objects[index].mass<=0.f || scene_pawn::is_pawn(state->snapshot,objects[index]) ||',1),
 ('state->snapshot.objects[index].player ||',
  'scene_pawn::is_pawn(state->snapshot,state->snapshot.objects[index]) ||',1),
 ('void PhysicsScene::step(float seconds, float input_x, float input_z)',
  '''void PhysicsScene::possess(std::uint64_t entity_id) {
    if(!state->character_api || !state->character_api->possess_body ||
       !state->character_api->configure_motor)
        throw std::runtime_error("Physics provider lacks Pawn control extension");
    gameplay::PlayerController proposed=state->controller;
    proposed.possess(state->snapshot,entity_id);
    auto const index=proposed.controlled_index(state->snapshot);
    if(!index || *index>UINT32_MAX)
        throw std::out_of_range("Controlled entity has no provider body");
    auto const tune=gameplay::motor_settings(state->snapshot,entity_id);
    NorthStarPhysicsCharacterMotor motor{tune.walk_speed,tune.run_speed,
        tune.jump_speed,tune.acceleration,tune.air_control,tune.gravity_scale};
    if(!state->character_api->configure_motor(state->world,std::uint32_t(*index),&motor) ||
       !state->character_api->possess_body(state->world,std::uint32_t(*index)))
        throw state->module.error("Cannot possess physics Pawn");
    state->controller=proposed;
}
void PhysicsScene::unpossess() {
    if(!state->character_api || !state->character_api->possess_body ||
       !state->character_api->possess_body(state->world,UINT32_MAX))
        throw state->module.error("Cannot unpossess physics Pawn");
    state->controller.unpossess();
}
std::uint64_t PhysicsScene::controlled_entity() const noexcept {
    return state->controller.controlled_entity();
}
std::optional<std::size_t> PhysicsScene::controlled_index() const noexcept {
    return state->controller.controlled_index(state->snapshot);
}
void PhysicsScene::set_sprinting(bool active) {
    if(!state->character_api || !state->character_api->set_sprinting ||
       !state->character_api->set_sprinting(state->world,active?1u:0u))
        throw state->module.error("Physics provider lacks Character sprint control");
}
void PhysicsScene::step(float seconds, float input_x, float input_z)''',1),
])
edit(n+"physics/jolt/jolt.cpp",[
 ('input.player','input.initially_controlled',4),
])
edit(n+"physics/bullet/bullet.cpp",[
 ('object.player','object.initially_controlled',2),
])
print("Physics ABI field migrated, provider-neutral possession wired")
