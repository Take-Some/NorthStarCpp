// Gameplay subsystem implementation; included once after PhysicsScene::State.
bool PhysicsScene::supports_ai_motor() const noexcept {
    return state->ai_motor_api && state->ai_motor_api->set_intent &&
           state->ai_motor_api->clear_intent;
}
void PhysicsScene::set_ai_move_intent(std::uint64_t pawn_id,
    float input_x,float input_z,float speed) {
    if(!supports_ai_motor())throw std::runtime_error("Physics provider lacks AI motor");
    if(!gameplay::valid_controller_pawn(state->snapshot,pawn_id) ||
       pawn_id==controlled_entity() ||
       !std::isfinite(input_x)||!std::isfinite(input_z)||
       !std::isfinite(speed)||speed<0.f||speed>12.f ||
       std::hypot(input_x,input_z)>1.001f)
        throw std::invalid_argument("Invalid AI motor command");
    auto index=gameplay::entity_index(state->snapshot,pawn_id);
    if(!index || *index>UINT32_MAX ||
       !state->ai_motor_api->set_intent(state->world,std::uint32_t(*index),
                                        input_x,input_z,speed))
        throw state->module.error("AI motor intent rejected");
}
void PhysicsScene::clear_ai_move_intent(std::uint64_t pawn_id) {
    if(!supports_ai_motor())throw std::runtime_error("Physics provider lacks AI motor");
    auto index=gameplay::entity_index(state->snapshot,pawn_id);
    if(!index || *index>UINT32_MAX ||
       !state->ai_motor_api->clear_intent(state->world,std::uint32_t(*index)))
        throw state->module.error("AI motor stop rejected");
}
void PhysicsScene::set_sprinting(bool active) {
    if(!state->character_api || !state->character_api->set_sprinting ||
       !state->character_api->set_sprinting(state->world,active?1u:0u))
        throw state->module.error("Physics provider lacks Character sprint control");
}
std::optional<PlayerAnimationFrame> PhysicsScene::player_animation() const {
    if(!state->player_animator || !state->animation_pawn_id)
        return std::nullopt;
    auto const& sample=state->player_animator->sample();
    return PlayerAnimationFrame{
        state->animation_pawn_id,state->animation_mesh_id,
        player_animation::name(sample.state),sample.clip,
        sample.previous_clip,sample.blend_alpha,sample.playback_seconds,
        sample.grounded};
}
