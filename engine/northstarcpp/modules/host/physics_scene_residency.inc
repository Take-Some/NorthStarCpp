// Collision residency and vehicle protection, included by physics.cpp inside namespace northstarcpp.
std::size_t PhysicsScene::sync_scene_residency(
    SceneWorld const& scene_world, Vector3 protected_position)
{
    // The ECS provider is independent of the physics collision residency
    // extension. An older physics DLL retains safe, eager collision fallback.
    if (!std::isfinite(protected_position.x) ||
        !std::isfinite(protected_position.y) ||
        !std::isfinite(protected_position.z))
        throw std::invalid_argument("Invalid protected position for collision streaming");
    auto const& objects=state->snapshot.objects;
    auto const& authored=scene_world.source().objects;
    if (objects.size()<authored.size() ||
        state->authored_count!=authored.size() ||
        objects.size()!=state->resident_mask.size())
        throw std::runtime_error("Collision streaming source indices do not match physics world");
    auto& vehicle_guards=state->vehicle_residency_guards;
    vehicle_guards.clear();
    vehicle_guards.reserve(state->native_vehicles.size());
    for(auto id:state->native_vehicles) {
        NorthStarPhysicsVehiclePose pose{};pose.size=sizeof(pose);
        if(!state->vehicle_api || !state->vehicle_api->pose(state->world,id,&pose))
            throw state->module.error("Vehicle streaming pose unavailable");
        vehicle_guards.push_back(physics_detail::VehicleResidencyGuard::from_pose(pose));
    }
    std::size_t changed=0;
    for (std::size_t i=0;i<authored.size();++i) {
        auto const& object=objects[i];
        auto const& node=authored[i].node;
        if (node.id!=object.node.id)
            throw std::runtime_error("Collision streaming stable ID mismatch");
        // Never detach dynamic traffic/NPCs or their player controller.
        if (scene_pawn::is_pawn(state->snapshot,object) || object.mass>0.f ||
            state->native_vehicles.count(static_cast<std::uint32_t>(i)))
            continue;
        // Preserve static world surfaces with no sector assignment (terrain,
        // persistent roads and other collision assets).
        if (node.cell.empty() && node.collision)
            continue;
        auto const dx=object.position.x-protected_position.x;
        auto const dy=object.position.y-protected_position.y;
        auto const dz=object.position.z-protected_position.z;
        auto const protect=std::max(32.f,object.size*2.f);
        bool const nearby=dx*dx+dy*dy+dz*dz <= protect*protect;
        bool vehicle_nearby=false;
        for(auto const& guard:vehicle_guards) {
            if(guard.contains(object.position.x,object.position.y,
                              object.position.z,object.size)) {
                vehicle_nearby=true;
                break;
            }
        }
        bool const desired=node.collision &&
            (nearby || vehicle_nearby || scene_world.object_resident(i));
        if (state->resident_mask[i] == static_cast<uint8_t>(desired))
            continue;
        if (state->residency_api) {
            if (!state->residency_api->set_static_resident(
                    state->world,static_cast<uint32_t>(i),desired ? 1u : 0u))
                throw state->module.error("Cannot change static collision residency");
            state->resident_mask[i]=static_cast<uint8_t>(desired);
            ++changed;
        }
    }
    (void)state->ecs.reconcile_scene_residency(state->authored_snapshot,scene_world);
    return changed;
}
