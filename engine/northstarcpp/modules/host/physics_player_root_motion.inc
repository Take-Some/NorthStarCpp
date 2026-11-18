// Gameplay subsystem implementation; included once after PhysicsScene::State.
bool PhysicsScene::supports_root_motion() const noexcept {
    return state->root_motion_api &&state->root_motion_api->submit;
}
bool PhysicsScene::enqueue_root_motion(std::uint64_t animated_entity,
    Vector3 world_delta,float seconds) const {
    if(!supports_root_motion()||!animated_entity||
       !state->controller.controlled_entity()||
       (animated_entity!=state->controller.controlled_entity() &&
        animated_entity!=state->animation_mesh_id)||
       !std::isfinite(world_delta.x)||!std::isfinite(world_delta.y)||
       !std::isfinite(world_delta.z)||!std::isfinite(seconds)||
       seconds<=0.f||seconds>.25f||
       std::abs(world_delta.y)>.00001f ||
       std::hypot(world_delta.x,world_delta.z)>seconds*12.f)
        return false;
    std::lock_guard<std::mutex> lock(state->root_motion_mutex);
    auto const next=state->queued_root_motion+world_delta;
    if(std::hypot(next.x,next.z)>.6f||
       state->queued_root_seconds+seconds>.25f)return false;
    state->queued_root_motion=next;
    state->queued_root_seconds+=seconds;
    return true;
}
