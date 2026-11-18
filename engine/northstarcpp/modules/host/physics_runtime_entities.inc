// Runtime physics entity creation, rotation, and teardown.
// Included by physics.cpp after PhysicsScene::State.
std::size_t PhysicsScene::spawn_runtime(SceneObject const& object)
{
    if (!state->runtime_api)
        throw std::runtime_error("Active physics provider lacks runtime-body extension");
    if (object.name.empty() || object.name.size() >= NORTHSTARCPP_ECS_NAME_BYTES ||
        !object.node.id || object.node.cell.size() > 128 ||
        !std::isfinite(object.position.x) || !std::isfinite(object.position.y) ||
        !std::isfinite(object.position.z) || !std::isfinite(object.size) ||
        !std::isfinite(object.mass) || object.size <= 0.f || object.size > 1000.f ||
        object.mass < 0.f || object.mass > 1000000.f ||
        std::max({std::abs(object.position.x),std::abs(object.position.y),
                  std::abs(object.position.z)}) > 1000000.f ||
        state->snapshot.objects.size() >= max_scene_objects)
        throw std::invalid_argument("Invalid runtime scene entity");
    if (state->used_ids.find(object.node.id)!=state->used_ids.end())
        throw std::invalid_argument("Duplicate runtime entity stable ID");
    if (!object.node.collision && !state->collider_api)
        throw std::runtime_error("Noncolliding runtime visuals require collider provider ABI");
    // Allocate host snapshot storage first. On a provider failure, roll back.
    auto const index = state->snapshot.objects.size();
    state->snapshot.objects.push_back(object);
    try {
        state->poses.push_back({});
        state->resident_mask.push_back(1);
        state->runtime_alive.push_back(1);
    } catch (...) {
        state->snapshot.objects.pop_back();
        if (state->poses.size() > index) state->poses.pop_back();
        if (state->resident_mask.size() > index) state->resident_mask.pop_back();
        if (state->runtime_alive.size() > index-state->authored_count)
            state->runtime_alive.pop_back();
        throw;
    }
    NorthStarRigidBody const body{
        object.position.x, object.position.y, object.position.z,
        object.size, object.mass, 0u};
    uint32_t result = UINT32_MAX;
    if (!state->runtime_api->add_body(state->world, &body, &result)) {
        state->snapshot.objects.pop_back();
        state->poses.pop_back();
        state->resident_mask.pop_back();
        state->runtime_alive.pop_back();
        throw state->module.error("Cannot spawn runtime physics body");
    }
    if (result != index)
        throw std::runtime_error("Runtime physics provider violated stable scene index contract");
    if (!object.node.collision) {
        NorthStarPhysicsCollider const disabled{0u,0u,object.size,object.size,
            object.size,0.f,0.f,0.f};
        if(!state->collider_api->configure(state->world,result,&disabled)) {
            (void)state->runtime_api->remove_body(state->world,result);
            state->snapshot.objects.pop_back();
            state->poses.pop_back();
            state->resident_mask.pop_back();
            state->runtime_alive.pop_back();
            throw state->module.error("Unable to disable runtime visual collision");
        }
    }
    state->used_ids.insert(object.node.id);
    // Initialize only the new pose instead of reading every provider body.
    // PhysicsScene::step updates all dynamic poses normally on the next tick.
    state->poses[index] = {object.position.x, object.position.y, object.position.z,
        {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}};
    return index;
}

void PhysicsScene::rotate_authored_visual(
    std::size_t index,Vector3 euler_degrees) {
    if(index>=state->authored_count)
        throw std::out_of_range("Authored visual index required");
    auto& object=state->snapshot.objects[index];
    // Procedurally animated NPCs have static, yaw-symmetric capsule bodies.
    // Rotate only the authored *visual* facing, never the physics collider;
    // all other colliding authored objects retain the original restriction.
    bool const npc_facing=std::any_of(state->snapshot.components.begin(),
        state->snapshot.components.end(),[&](SceneComponentBlob const& component) {
            return component.entity_id==object.node.id &&
                   component.type=="engine.model.procedural_humanoid" &&
                   component.version==1 && component.payload=="locomotion_v1";
        });
    if(object.mass!=0.f || (object.node.collision && !npc_facing) || !object.node.id)
        throw std::invalid_argument("Authored visual must be static without collision unless a humanoid NPC");
    bool const skeletal=std::any_of(state->snapshot.components.begin(),
        state->snapshot.components.end(),[&](SceneComponentBlob const& component) {
            return component.entity_id==object.node.id &&
                   component.type=="northstar.gameplay.skeletal_mesh";
        });
    // Permit authored render-only YDR/YDD/mesh visuals to rotate. Static
    // collision shapes and dynamic bodies are blocked by the guard above.
    if(!skeletal && object.node.asset.empty())
        throw std::invalid_argument("Authored visual needs a mesh asset or SkeletalMesh component");
    if(!std::isfinite(euler_degrees.x)||!std::isfinite(euler_degrees.y)||
       !std::isfinite(euler_degrees.z)||
       std::max({std::abs(euler_degrees.x),std::abs(euler_degrees.y),
                 std::abs(euler_degrees.z)})>360000.f)
        throw std::invalid_argument("Invalid authored visual rotation");
    object.node.rotation=euler_degrees;
    state->authored_snapshot.objects[index].node.rotation=euler_degrees;
}
void PhysicsScene::rotate_runtime(std::size_t index, Vector3 euler_degrees) {
    // The Editor/Project API uses the same rotation entry point for the
    // authored visual; reject nonvisual authored actors in the guard below.
    if(index<state->authored_count) {
        rotate_authored_visual(index,euler_degrees);
        return;
    }
    if(!runtime_alive(index) || state->snapshot.objects[index].mass!=0.f ||
       state->snapshot.objects[index].node.collision)
        throw std::invalid_argument("Rotation requires a live visual-only runtime entity");
    if(!std::isfinite(euler_degrees.x) || !std::isfinite(euler_degrees.y) ||
       !std::isfinite(euler_degrees.z) ||
       std::max({std::abs(euler_degrees.x),std::abs(euler_degrees.y),
                 std::abs(euler_degrees.z)})>360000.f)
        throw std::invalid_argument("Invalid visual rotation");
    state->snapshot.objects[index].node.rotation=euler_degrees;
}

void PhysicsScene::destroy_runtime(std::size_t index)
{
    if (!runtime_alive(index))
        throw std::out_of_range("Runtime scene entity is stale or authored");
    auto const dead_id=state->snapshot.objects[index].node.id;
    // Never leave PlayerController bound to a provider tombstone.
    if(state->controller.controlled_entity()==dead_id)unpossess();
    if (!state->runtime_api->remove_body(state->world,static_cast<uint32_t>(index)))
        throw state->module.error("Cannot destroy runtime physics body");
    state->runtime_alive[index-state->authored_count] = 0;
    if(state->controller.has_view_target_override() &&
       state->controller.view_target()==dead_id)
        state->controller.reset_view_target();
    // Keep the slot but never render or query it: authored indices must remain
    // stable, including during World Partition updates.
    auto& dead = state->snapshot.objects[index];
    dead.node.visible = false;
    dead.node.collision = false;
    dead.node.asset.clear();
}
