// Gameplay subsystem implementation; included once after PhysicsScene::State.
void PhysicsScene::possess(std::uint64_t entity_id) {
    gameplay::PlayerController proposed=state->controller;
    if(!proposed.possess(state->snapshot,entity_id))return;
    auto const index=proposed.controlled_index(state->snapshot);
    if(!index || *index>UINT32_MAX)
        throw std::out_of_range("Controlled Pawn has no provider body");
    auto const tune=gameplay::motor_settings(state->snapshot,entity_id);
    NorthStarPhysicsCharacterMotor motor{tune.walk_speed,tune.run_speed,
        tune.jump_speed,tune.acceleration,tune.air_control,tune.gravity_scale};
    auto const outcome=gameplay::physics_bridge::transfer(
        state->character_api,state->ai_motor_api,state->world,
        std::uint32_t(*index),motor,true);
    if(outcome!=gameplay::physics_bridge::Result::Ok)
        throw state->module.error(gameplay::physics_bridge::message(outcome));
    {
        std::lock_guard<std::mutex> lock(state->root_motion_mutex);
        state->queued_root_motion={0.f,0.f,0.f};
        state->queued_root_seconds=0.f;
    }
    state->controller=proposed;
    state->initialize_player_animation();
}
void PhysicsScene::unpossess() {
    if(!state->controller.possessed())return;
    auto const outcome=gameplay::physics_bridge::release(
        state->character_api,state->world);
    if(outcome!=gameplay::physics_bridge::Result::Ok)
        throw state->module.error(gameplay::physics_bridge::message(outcome));
    {
        std::lock_guard<std::mutex> lock(state->root_motion_mutex);
        state->queued_root_motion={0.f,0.f,0.f};
        state->queued_root_seconds=0.f;
    }
    state->controller.unpossess();
    state->initialize_player_animation();
}
std::optional<gameplay::PossessionEvent> PhysicsScene::poll_controller_event() noexcept {
    return state->controller.poll_possession_event();
}
std::uint64_t PhysicsScene::dropped_controller_events() const noexcept {
    return state->controller.dropped_possession_events();
}
std::uint64_t PhysicsScene::controlled_entity() const noexcept {
    return state->controller.controlled_entity();
}
std::optional<std::size_t> PhysicsScene::controlled_index() const noexcept {
    return state->controller.controlled_index(state->snapshot);
}
void PhysicsScene::set_view_target(std::uint64_t entity_id) {
    if(entity_id) {
        auto const index=gameplay::entity_index(state->snapshot,entity_id);
        if(!index || (*index>=state->authored_count && !runtime_alive(*index)))
            throw std::invalid_argument("Camera View Target is missing or destroyed");
    }
    state->controller.set_view_target(state->snapshot,entity_id);
}
void PhysicsScene::reset_view_target() noexcept {
    state->controller.reset_view_target();
}
std::uint64_t PhysicsScene::view_target() const noexcept {
    return state->controller.view_target();
}
std::optional<std::size_t> PhysicsScene::view_target_index() const noexcept {
    return state->controller.view_target_index(state->snapshot);
}
