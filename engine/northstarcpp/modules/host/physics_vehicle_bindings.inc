// Native vehicle controls, queries, and pose translation.
// Included by physics.cpp after PhysicsScene::State.
bool PhysicsScene::supports_native_vehicle() const {
    return state->vehicle_api!=nullptr;
}
void PhysicsScene::create_native_vehicle(std::size_t index,
    NorthStarVehicleRig const& rig,float yaw) {
    if(!state->vehicle_api)
        throw std::runtime_error("Selected engine.physics provider lacks Jolt native vehicle extension");
    if(index>=state->snapshot.objects.size() ||
       !state->snapshot.objects[index].node.collision ||
       scene_pawn::is_pawn(state->snapshot,state->snapshot.objects[index]) ||
       state->snapshot.objects[index].mass!=0.f)
        throw std::invalid_argument("Native vehicle must own a collidable, static authored entity");
    if(!state->vehicle_api->create(state->world,
        static_cast<std::uint32_t>(index),&rig,yaw))
        throw state->module.error("Jolt cannot construct native vehicle");
    state->resident_mask.at(index)=1;
    state->native_vehicles.insert(static_cast<std::uint32_t>(index));
}
void PhysicsScene::destroy_native_vehicle(std::size_t index) {
    if(!state->vehicle_api || index>=state->snapshot.objects.size() ||
       !state->vehicle_api->destroy(state->world,
        static_cast<std::uint32_t>(index)))
        throw state->module.error("Cannot destroy Jolt native vehicle");
    state->native_vehicles.erase(static_cast<std::uint32_t>(index));
}
void PhysicsScene::native_vehicle_controls(std::size_t index,float throttle,
    float steering,float brake,float handbrake) {
    if(!state->vehicle_api ||
       !state->vehicle_api->controls(state->world,
            static_cast<std::uint32_t>(index),throttle,steering,brake,handbrake))
        throw state->module.error("Native Jolt vehicle controls failed");
}
NorthStarPhysicsVehiclePose PhysicsScene::native_vehicle_pose(std::size_t index) const {
    NorthStarPhysicsVehiclePose out{};out.size=sizeof(out);
    if(!state->vehicle_api ||
       !state->vehicle_api->pose(state->world,
            static_cast<std::uint32_t>(index),&out) ||
       out.size<sizeof(out))
        throw state->module.error("Native Jolt vehicle pose unavailable");
    return out;
}
NorthStarVehicleWheelState PhysicsScene::native_vehicle_wheel(std::size_t index,
    std::uint32_t wheel) const {
    NorthStarVehicleWheelState out{};out.size=sizeof(out);
    if(!state->vehicle_api ||
       !state->vehicle_api->wheel(state->world,
            static_cast<std::uint32_t>(index),wheel,&out) ||
       out.size<sizeof(out))
        throw state->module.error("Native Jolt wheel telemetry unavailable");
    return out;
}
std::uint32_t PhysicsScene::native_vehicle_wheel_count(std::size_t index) const {
    return state->vehicle_api?
        state->vehicle_api->wheel_count(state->world,static_cast<std::uint32_t>(index)):0u;
}
