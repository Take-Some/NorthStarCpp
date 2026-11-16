// 16-channel filtered ray/sphere queries. Included by host/physics.cpp
// inside namespace northstarcpp, after PhysicsScene::State definition.
std::optional<PhysicsCastHit> PhysicsScene::sweep_sphere_channel(
    Vector3 start,Vector3 end,float radius,std::uint32_t channel,
    bool include_overlaps,std::size_t ignore_index) const {
    if(!state->channels_api)
        throw std::runtime_error("Physics provider lacks channel-aware sweeps");
    if(channel>=16||!std::isfinite(radius)||radius<=0.f||radius>2.f)
        throw std::invalid_argument("Invalid channel sphere cast");
    for(float v:{start.x,start.y,start.z,end.x,end.y,end.z})
        if(!std::isfinite(v))throw std::invalid_argument("Invalid cast coordinates");
    if(ignore_index!=std::size_t(-1)&&ignore_index>=state->snapshot.objects.size())
        throw std::out_of_range("Invalid ignored body");
    auto ignored=ignore_index==std::size_t(-1)?UINT32_MAX:
        std::uint32_t(ignore_index);
    NorthStarPhysicsCastHit hit{};
    if(!state->channels_api->sweep_sphere(state->world,
        start.x,start.y,start.z,end.x,end.y,end.z,radius,
        channel,include_overlaps?6u:4u,ignored,&hit))
        throw state->module.error("Channel sphere cast failed");
    if(!hit.hit)return std::nullopt;
    if(!std::isfinite(hit.fraction)||hit.fraction<0.f||hit.fraction>1.f)
        throw std::runtime_error("Invalid channel cast fraction");
    std::uint32_t response=2;
    if(hit.object_index<state->snapshot.objects.size())
        response=std::uint32_t(collision_authoring::for_object(
            state->snapshot,state->snapshot.objects[hit.object_index]).responses[channel]);
    return PhysicsCastHit{hit.fraction,{hit.normal_x,hit.normal_y,hit.normal_z},
        hit.object_index,0u,UINT32_MAX,response};
}
std::optional<PhysicsCastHit> PhysicsScene::raycast_channel(
    Vector3 start,Vector3 end,std::uint32_t channel,
    bool include_overlaps,std::size_t ignore_index) const {
    if(!state->channels_api)
        throw std::runtime_error("Physics provider lacks channel-aware raycast");
    if(channel>=16)throw std::invalid_argument("Invalid ray channel");
    for(float v:{start.x,start.y,start.z,end.x,end.y,end.z})
        if(!std::isfinite(v))throw std::invalid_argument("Invalid ray coordinates");
    if(ignore_index!=std::size_t(-1)&&ignore_index>=state->snapshot.objects.size())
        throw std::out_of_range("Invalid ignored body");
    auto ignored=ignore_index==std::size_t(-1)?UINT32_MAX:
        std::uint32_t(ignore_index);
    NorthStarPhysicsCastHit hit{};
    if(!state->channels_api->raycast(state->world,
        start.x,start.y,start.z,end.x,end.y,end.z,
        channel,include_overlaps?6u:4u,ignored,&hit))
        throw state->module.error("Channel raycast failed");
    if(!hit.hit)return std::nullopt;
    if(!std::isfinite(hit.fraction)||hit.fraction<0.f||hit.fraction>1.f)
        throw std::runtime_error("Invalid raycast fraction");
    std::uint32_t response=2;
    if(hit.object_index<state->snapshot.objects.size())
        response=std::uint32_t(collision_authoring::for_object(
            state->snapshot,state->snapshot.objects[hit.object_index]).responses[channel]);
    return PhysicsCastHit{hit.fraction,{hit.normal_x,hit.normal_y,hit.normal_z},
        hit.object_index,0u,UINT32_MAX,response};
}
