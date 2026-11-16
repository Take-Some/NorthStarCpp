// Inline Window host adapter: V8-to-native engine.vehicle and PhysicsScene contacts.
// Intentionally no vehicle force calculation in this file.
void configure_vehicle_bindings(ScriptRuntime& runtime) {
    runtime.bind("engine.vehicle","attach",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=3 || !preview || !script_vehicle)
            throw std::invalid_argument("vehicle.attach(index,wheelbase,maxSpeed)");
        auto i=script_index(a[0]);
        auto const& objects=preview->scene().objects;
        if(i>=objects.size()||i>=16384||scene_pawn::is_pawn(preview->scene(),objects[i])||objects[i].mass!=0)
            throw std::invalid_argument("Vehicle requires static non-player entity");
        auto const& o=objects[i];
        NorthStarVehicleDefinition d{};
        d.size=sizeof(d);d.entity_index=static_cast<std::uint32_t>(i);
        d.position[0]=o.position.x;d.position[1]=o.position.y;d.position[2]=o.position.z;
        d.heading=o.node.rotation.y*0.017453292519943295f;
        d.wheelbase=float(script_number(a[1]));d.maximum_speed=float(script_number(a[2]));
        d.acceleration=2.4f;d.braking=5.5f;d.maximum_steering=.51f;d.rolling_drag=.08f;
        // Existing attached vehicle may belong to the previous V8 context
        // after project scripts hot reload; reset its commands and seat state.
        if(script_vehicle_native.erase(d.entity_index))
            preview->destroy_native_vehicle(d.entity_index);
        script_vehicle_targets.erase(d.entity_index);
        try {script_vehicle->detach(d.entity_index);}catch(std::exception const&){}
        script_vehicle_rigs.erase(d.entity_index);
        script_vehicle_visual_indices.erase(d.entity_index);
        script_vehicle_door_visual_indices.erase(d.entity_index);
        script_vehicle_visual_followers.erase(d.entity_index);
        script_vehicle->attach(d);
        return std::string("true");
    });
    runtime.bind("engine.vehicle","configureRig",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=2||!script_vehicle||!script_assets)
            throw std::invalid_argument("vehicle.configureRig(index,vehicle_xml)");
        auto id=script_index(a[0]);
        auto const& path=a[1];
        if(path.size()<20 || path.size()>160 ||
           path.compare(0,9,"vehicles/")!=0 ||
           path.substr(path.size()-9)!=".nvehicle" ||
           path.find("..")!=std::string::npos ||
           path.find(':')!=std::string::npos ||
           path.find('\\')!=std::string::npos ||
           path.find("//")!=std::string::npos)
            throw std::invalid_argument("Vehicle rig must be vehicles/*.nvehicle");
        if(script_assets->size(path)>131072)
            throw std::invalid_argument("Vehicle rig XML is larger than 128 KiB");
        auto bytes=script_assets->read(path);
        auto rig=parse_vehicle_rig_xml(
            std::string_view(reinterpret_cast<char const*>(bytes.data()),bytes.size()));
        auto const entity=static_cast<std::uint32_t>(id);
        script_vehicle->configure(entity,rig.view());
        if(rig.contact_mode=="physics") {
            if(!preview||!preview->supports_native_vehicle())
                throw std::runtime_error(
                    "contact_mode=physics requires provider-native Jolt VehicleConstraint");
            if(script_vehicle_native.erase(entity))
                preview->destroy_native_vehicle(entity);
            auto const base=script_vehicle->state(entity);
            preview->create_native_vehicle(entity,rig.view(),base.heading);
            script_vehicle_native.insert(entity);
        } else if(script_vehicle_native.erase(entity)) {
            preview->destroy_native_vehicle(entity);
        }
        std::fprintf(stderr,"[INFO] [engine.vehicle] rig=%s wheels=%zu seats=%zu contact=%s\n",
            path.c_str(),rig.wheels.size(),rig.seats.size(),rig.contact_mode.c_str());
        // Resolve named scene nodes once, not every rendered frame. Unbound
        // nodes are allowed while their original YDR assets await conversion.
        std::vector<std::uint32_t> visuals;
        visuals.reserve(rig.wheel_visual_nodes.size());
        for(auto const& name:rig.wheel_visual_nodes) {
            std::uint32_t resolved=UINT32_MAX;
            if(!name.empty() && preview) {
                auto const& nodes=preview->scene().objects;
                auto found=std::find_if(nodes.begin(),nodes.end(),
                    [&name](auto const& obj){return obj.name==name;});
                if(found!=nodes.end() && !found->node.collision &&
                   !scene_pawn::is_pawn(preview->scene(),*found) && found->mass==0.f)
                    resolved=static_cast<std::uint32_t>(found-nodes.begin());
            }
            visuals.push_back(resolved);
        }
        script_vehicle_visual_indices.insert_or_assign(
            static_cast<std::uint32_t>(id),std::move(visuals));
        if(!rig.doors.empty()) {
            if(!script_vehicle->supports_cabin())
                throw std::runtime_error("Selected vehicle provider cannot animate doors");
            script_vehicle->configure_doors(static_cast<std::uint32_t>(id),rig.doors);
        }
        std::vector<std::uint32_t> door_bindings;
        door_bindings.reserve(rig.door_visual_nodes.size());
        for(auto const& name:rig.door_visual_nodes) {
            std::uint32_t resolved=UINT32_MAX;
            if(!name.empty() && preview) {
                auto const& nodes=preview->scene().objects;
                auto found=std::find_if(nodes.begin(),nodes.end(),
                    [&name](auto const& o){return o.name==name;});
                if(found!=nodes.end()&&!found->node.collision&&!scene_pawn::is_pawn(preview->scene(),*found)&&
                   found->mass==0.f)
                    resolved=static_cast<std::uint32_t>(found-nodes.begin());
            }
            door_bindings.push_back(resolved);
        }
        script_vehicle_door_visual_indices.insert_or_assign(
            static_cast<std::uint32_t>(id),std::move(door_bindings));
        // Resolve visual-only descendants once when a rig is configured.
        // The authored hierarchy is generic: a composite vehicle can place
        // body parts under its chassis, rim parts under wheel visuals, etc.
        // No project- or MT28-specific filenames are embedded in the engine.
        std::vector<VehicleVisualFollower> followers;
        {
            auto const& nodes=preview->scene().objects;
            std::unordered_map<std::uint64_t,std::uint32_t> by_id;
            by_id.reserve(nodes.size());
            for(std::uint32_t n=0;n<nodes.size();++n)
                if(nodes[n].node.id)by_id.emplace(nodes[n].node.id,n);
            std::unordered_set<std::uint64_t> moving_parents{
                nodes.at(id).node.id};
            for(auto const index:script_vehicle_visual_indices.at(
                    static_cast<std::uint32_t>(id)))
                if(index!=UINT32_MAX && index<nodes.size())
                    moving_parents.insert(nodes[index].node.id);
            for(auto const index:script_vehicle_door_visual_indices.at(
                    static_cast<std::uint32_t>(id)))
                if(index!=UINT32_MAX && index<nodes.size())
                    moving_parents.insert(nodes[index].node.id);
            for(std::uint32_t n=0;n<nodes.size();++n) {
                auto const& child=nodes[n];
                if(child.node.parent==0 || child.node.asset.empty() ||
                   !moving_parents.count(child.node.parent) ||
                   child.node.collision || child.mass!=0.f ||
                   scene_pawn::is_pawn(preview->scene(),child))
                    continue;
                auto parent_it=by_id.find(child.node.parent);
                if(parent_it==by_id.end() || parent_it->second==n)continue;
                auto const& parent=nodes[parent_it->second];
                followers.push_back({n,parent_it->second,
                    {child.position.x-parent.position.x,
                     child.position.y-parent.position.y,
                     child.position.z-parent.position.z},
                    {child.node.rotation.x-parent.node.rotation.x,
                     child.node.rotation.y-parent.node.rotation.y,
                     child.node.rotation.z-parent.node.rotation.z}});
                if(followers.size()>512)
                    throw std::length_error("Vehicle visual hierarchy too large");
            }
        }
        script_vehicle_visual_followers.insert_or_assign(
            static_cast<std::uint32_t>(id),std::move(followers));
        script_vehicle_rigs.insert_or_assign(static_cast<std::uint32_t>(id),std::move(rig));
        return std::string("true");
    });
    runtime.bind("engine.vehicle","controls",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=4||!script_vehicle)
            throw std::invalid_argument("vehicle.controls(index,gas,steer,brake)");
        script_vehicle_targets.erase(static_cast<std::uint32_t>(script_index(a[0])));
        script_vehicle->control(static_cast<std::uint32_t>(script_index(a[0])),
            float(script_number(a[1])),float(script_number(a[2])),float(script_number(a[3])));
        return std::string("true");
    });
    runtime.bind("engine.vehicle","waypoint",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=5||!script_vehicle)
            throw std::invalid_argument("vehicle.waypoint(index,x,z,speed,radius)");
        auto const entity=static_cast<std::uint32_t>(script_index(a[0]));
        float const x=float(script_number(a[1])),z=float(script_number(a[2]));
        float const speed=float(script_number(a[3])),radius=float(script_number(a[4]));
        script_vehicle->waypoint(entity,x,z,speed,radius);
        script_vehicle_targets.insert_or_assign(entity,VehicleTarget{x,z,speed,radius});
        return std::string("true");
    });
    runtime.bind("engine.vehicle","state",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=1||!script_vehicle)
            throw std::invalid_argument("vehicle.state(index)");
        auto v=script_vehicle->state(static_cast<std::uint32_t>(script_index(a[0])));
        if(script_vehicle_native.count(v.entity_index)) {
            auto const pose=preview->native_vehicle_pose(v.entity_index);
            v.position[0]=pose.x;v.position[1]=pose.y;v.position[2]=pose.z;
            v.heading=std::atan2(pose.rotation[6],pose.rotation[8]);
            v.speed=pose.linear_velocity[0]*pose.rotation[6]+
                    pose.linear_velocity[1]*pose.rotation[7]+
                    pose.linear_velocity[2]*pose.rotation[8];
            v.waypoint_active=script_vehicle_targets.count(v.entity_index)?1u:0u;
        }
        return std::string("{\"index\":")+std::to_string(v.entity_index)+
            ",\"x\":"+std::to_string(v.position[0])+
            ",\"y\":"+std::to_string(v.position[1])+
            ",\"z\":"+std::to_string(v.position[2])+
            ",\"heading\":"+std::to_string(v.heading)+
            ",\"speed\":"+std::to_string(v.speed)+
            ",\"waypointActive\":"+(v.waypoint_active?"true":"false")+"}";
    });
    runtime.bind("engine.vehicle","motion",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=1||!script_vehicle)
            throw std::invalid_argument("vehicle.motion(index)");
        auto const id=static_cast<std::uint32_t>(script_index(a[0]));
        auto v=script_vehicle->motion(id);
        if(script_vehicle_native.count(id)) {
            auto const pose=preview->native_vehicle_pose(id);
            v.forward_speed=pose.linear_velocity[0]*pose.rotation[6]+
                pose.linear_velocity[1]*pose.rotation[7]+
                pose.linear_velocity[2]*pose.rotation[8];
            v.lateral_speed=pose.linear_velocity[0]*pose.rotation[0]+
                pose.linear_velocity[1]*pose.rotation[1]+
                pose.linear_velocity[2]*pose.rotation[2];
            v.yaw_rate=pose.angular_velocity[1];
            v.grounded_wheels=0;
            v.longitudinal_slip=0.f;v.lateral_slip=0.f;
            auto const wheels=preview->native_vehicle_wheel_count(id);
            for(std::uint32_t n=0;n<wheels;++n) {
                auto const wheel=preview->native_vehicle_wheel(id,n);
                v.grounded_wheels+=wheel.in_contact;
                v.longitudinal_slip=std::max(v.longitudinal_slip,std::abs(wheel.slip_ratio));
                v.lateral_slip=std::max(v.lateral_slip,std::abs(wheel.slip_angle));
            }
        }
        return std::string("{\"forwardSpeed\":")+std::to_string(v.forward_speed)+
            ",\"lateralSpeed\":"+std::to_string(v.lateral_speed)+
            ",\"yawRate\":"+std::to_string(v.yaw_rate)+
            ",\"longitudinalSlip\":"+std::to_string(v.longitudinal_slip)+
            ",\"lateralSlip\":"+std::to_string(v.lateral_slip)+
            ",\"distance\":"+std::to_string(v.travelled_distance)+
            ",\"groundedWheels\":"+std::to_string(v.grounded_wheels)+"}";
    });
    runtime.bind("engine.vehicle","wheelState",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=2||!script_vehicle)
            throw std::invalid_argument("vehicle.wheelState(index,wheel)");
        auto const id=static_cast<std::uint32_t>(script_index(a[0]));
        auto const n=static_cast<std::uint32_t>(script_index(a[1]));
        auto v=script_vehicle_native.count(id)?
            preview->native_vehicle_wheel(id,n):script_vehicle->wheel(id,n);
        return std::string("{\"steerAngle\":")+std::to_string(v.steer_angle)+
            ",\"spinAngle\":"+std::to_string(v.spin_angle)+
            ",\"angularSpeed\":"+std::to_string(v.angular_speed)+
            ",\"compression\":"+std::to_string(v.compression)+
            ",\"normalForce\":"+std::to_string(v.normal_force)+
            ",\"slipRatio\":"+std::to_string(v.slip_ratio)+
            ",\"slipAngle\":"+std::to_string(v.slip_angle)+
            ",\"inContact\":"+(v.in_contact?"true":"false")+"}";
    });
    runtime.bind("engine.vehicle","wheelPose",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=2||!script_vehicle)
            throw std::invalid_argument("vehicle.wheelPose(index,wheel)");
        auto const id=static_cast<std::uint32_t>(script_index(a[0]));
        auto const index=static_cast<std::uint32_t>(script_index(a[1]));
        auto const& rig=script_vehicle_rigs.at(id);
        auto const& mount=rig.wheels.at(index);
        auto const dynamic=script_vehicle_native.count(id)!=0;
        auto const wheel=dynamic?
            preview->native_vehicle_wheel(id,index):script_vehicle->wheel(id,index);
        auto const state=script_vehicle->state(id);
        float x=0.f,y=0.f,z=0.f;
        float yaw=state.heading;
        float suspension=mount.rest_length-wheel.compression;
        if(dynamic) {
            auto const pose=preview->native_vehicle_pose(id);
            auto const half=preview->scene().objects.at(id).size*.5f;
            suspension=mount.rest_length+mount.travel*.5f-wheel.compression;
            auto const local_x=mount.mount[0];
            auto const local_y=mount.mount[1]-suspension;
            auto const local_z=mount.mount[2];
            x=pose.x+local_x*pose.rotation[0]+
                local_y*pose.rotation[3]+local_z*pose.rotation[6];
            y=pose.y+half+local_x*pose.rotation[1]+
                local_y*pose.rotation[4]+local_z*pose.rotation[7];
            z=pose.z+local_x*pose.rotation[2]+
                local_y*pose.rotation[5]+local_z*pose.rotation[8];
            yaw=std::atan2(pose.rotation[6],pose.rotation[8]);
        } else {
            float const sn=std::sin(yaw),cs=std::cos(yaw);
            x=state.position[0]+mount.mount[0]*cs+mount.mount[2]*sn;
            z=state.position[2]+mount.mount[2]*cs-mount.mount[0]*sn;
            y=state.position[1]+mount.mount[1]-suspension;
        }
        return std::string("{\"x\":")+std::to_string(x)+
            ",\"y\":"+std::to_string(y)+
            ",\"z\":"+std::to_string(z)+
            ",\"heading\":"+std::to_string(yaw+wheel.steer_angle)+
            ",\"spin\":"+std::to_string(wheel.spin_angle)+
            ",\"compression\":"+std::to_string(wheel.compression)+
            ",\"contact\":"+(wheel.in_contact?"true":"false")+"}";
    });
    runtime.bind("engine.vehicle","wheelCount",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=1||!script_vehicle)throw std::invalid_argument("vehicle.wheelCount(index)");
        return std::to_string(script_vehicle->wheel_count(
            static_cast<std::uint32_t>(script_index(a[0]))));
    });
    runtime.bind("engine.vehicle","seatCount",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=1||!script_vehicle)throw std::invalid_argument("vehicle.seatCount(index)");
        return std::to_string(script_vehicle->seat_count(
            static_cast<std::uint32_t>(script_index(a[0]))));
    });
    runtime.bind("engine.vehicle","seatState",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=2||!script_vehicle)
            throw std::invalid_argument("vehicle.seatState(index,seat)");
        auto const id=static_cast<std::uint32_t>(script_index(a[0]));
        auto const n=static_cast<std::uint32_t>(script_index(a[1]));
        auto v=script_vehicle->seat(id,n);
        if(script_vehicle_native.count(id)) {
            auto const pose=preview->native_vehicle_pose(id);
            auto const& local=script_vehicle_rigs.at(id).seats.at(n);
            auto const& xyz=local.position;
            auto const half=preview->scene().objects.at(id).size*.5f;
            float const local_y=xyz[1]-half;
            v.world_position[0]=pose.x+xyz[0]*pose.rotation[0]+
                local_y*pose.rotation[3]+xyz[2]*pose.rotation[6];
            v.world_position[1]=pose.y+half+xyz[0]*pose.rotation[1]+
                local_y*pose.rotation[4]+xyz[2]*pose.rotation[7];
            v.world_position[2]=pose.z+xyz[0]*pose.rotation[2]+
                local_y*pose.rotation[5]+xyz[2]*pose.rotation[8];
            v.world_yaw=std::atan2(pose.rotation[6],pose.rotation[8])+local.yaw;
        }
        return std::string("{\"index\":")+std::to_string(v.index)+
            ",\"role\":"+std::to_string(v.role)+
            ",\"occupant\":"+script_json_quote(std::to_string(v.occupant))+
            ",\"x\":"+std::to_string(v.world_position[0])+
            ",\"y\":"+std::to_string(v.world_position[1])+
            ",\"z\":"+std::to_string(v.world_position[2])+
            ",\"yaw\":"+std::to_string(v.world_yaw)+"}";
    });
    runtime.bind("engine.vehicle","occupy",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=3||!script_vehicle)
            throw std::invalid_argument("vehicle.occupy(index,seat,entityStableId)");
        auto const occupant=static_cast<std::uint64_t>(script_index(a[2]));
        if(!script_entity_indices.count(occupant))
            throw std::invalid_argument("Seat occupant is not a live scene entity");
        script_vehicle->occupy(static_cast<std::uint32_t>(script_index(a[0])),
            static_cast<std::uint32_t>(script_index(a[1])),occupant);
        return std::string("true");
    });
    runtime.bind("engine.vehicle","release",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=3||!script_vehicle)
            throw std::invalid_argument("vehicle.release(index,seat,entityStableId)");
        script_vehicle->release(static_cast<std::uint32_t>(script_index(a[0])),
            static_cast<std::uint32_t>(script_index(a[1])),
            static_cast<std::uint64_t>(script_index(a[2])));
        return std::string("true");
    });
    runtime.bind("engine.vehicle","setDoor",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=3||!script_vehicle)
            throw std::invalid_argument("vehicle.setDoor(index,door,open0or1)");
        auto const id=static_cast<std::uint32_t>(script_index(a[0]));
        auto const door=static_cast<std::uint32_t>(script_index(a[1]));
        int const request=static_cast<int>(script_number(a[2]));
        if(request!=0&&request!=1)
            throw std::invalid_argument("Vehicle door open must be 0 or 1");
        if(request!=0 && script_vehicle_native.count(id)) {
            auto const pose=preview->native_vehicle_pose(id);
            float const linear=std::hypot(pose.linear_velocity[0],
                std::hypot(pose.linear_velocity[1],pose.linear_velocity[2]));
            if(linear>.5f)
                throw std::runtime_error("Cannot open vehicle doors while driving");
        }
        script_vehicle->set_door(id,door,request!=0);
        return std::string("true");
    });
    runtime.bind("engine.vehicle","doorCount",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=1||!script_vehicle)
            throw std::invalid_argument("vehicle.doorCount(index)");
        return std::to_string(script_vehicle->door_count(
            static_cast<std::uint32_t>(script_index(a[0]))));
    });
    runtime.bind("engine.vehicle","doorState",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=2||!script_vehicle)
            throw std::invalid_argument("vehicle.doorState(index,door)");
        auto const id=static_cast<std::uint32_t>(script_index(a[0]));
        auto const ordinal=static_cast<std::uint32_t>(script_index(a[1]));
        auto const state=script_vehicle->door(id,ordinal);
        return std::string("{\"index\":")+std::to_string(state.index)+
            ",\"fraction\":"+std::to_string(state.fraction)+
            ",\"target\":"+std::to_string(state.target_fraction)+
            ",\"angle\":"+std::to_string(state.angle)+"}";
    });
    runtime.bind("engine.vehicle","detach",[this](std::string_view json) {
        auto a=script_args(json);
        if(a.size()!=1||!script_vehicle)
            throw std::invalid_argument("vehicle.detach(index)");
        auto const id=static_cast<std::uint32_t>(script_index(a[0]));
        if(script_vehicle_native.erase(id))
            preview->destroy_native_vehicle(id);
        script_vehicle->detach(id);
        script_vehicle_rigs.erase(id);
        script_vehicle_visual_indices.erase(id);
        script_vehicle_door_visual_indices.erase(id);
        script_vehicle_visual_followers.erase(id);
        script_vehicle_targets.erase(id);
        return std::string("true");
    });
    runtime.bind("engine.vehicle","provider",[this](std::string_view json) {
        if(!script_args(json).empty()||!script_vehicle)
            throw std::invalid_argument("vehicle.provider()");
        return script_json_quote(script_vehicle->provider_name());
    });
    runtime.bind("engine.vehicle","count",[this](std::string_view json) {
        if(!script_args(json).empty()||!script_vehicle)
            throw std::invalid_argument("vehicle.count()");
        return std::to_string(script_vehicle->count());
    });
}
// Optional PhysicsScene wheel/road sensing. The flat fallback is retained
// for prototype scenes whose original road collision has not been imported.
void sample_vehicle_contacts(NorthStarVehicleState const& state) {
    auto const id=state.entity_index;
    auto it=script_vehicle_rigs.find(id);
    if(it==script_vehicle_rigs.end()||it->second.contact_mode!="physics"||
       !script_vehicle||!preview)return;
    auto const& rig=it->second;
    for(std::size_t n=0;n<rig.wheels.size();++n) {
        auto const& w=rig.wheels[n];
        float const sn=std::sin(state.heading),cs=std::cos(state.heading);
        float const wx=state.position[0]+w.mount[0]*cs+w.mount[2]*sn;
        float const wz=state.position[2]+w.mount[2]*cs-w.mount[0]*sn;
        float const sy=state.position[1]+w.mount[1]+0.10f;
        float const depth=w.radius+w.rest_length+w.travel+0.20f;
        float const radius=std::max(0.12f,w.radius*.7f);
        auto hit=preview->sweep_sphere({wx,sy,wz},
            {wx,sy-depth,wz},radius,id);
        bool const found=hit&&hit->normal.y>.45f;
        float const ground=found?
            sy-depth*hit->fraction-radius:sy-depth;
        NorthStarVehicleGroundContact contact{
            sizeof(NorthStarVehicleGroundContact),
            static_cast<std::uint32_t>(n),found?1u:0u,
            ground,rig.surface_friction};
        script_vehicle->set_contact(id,contact);
    }
}
// Swept collision rejection is not a full Jolt vehicle rigid-body constraint.
// It prevents the kinematic vehicle from tunnelling through authored obstacles.
void resolve_vehicle_obstacles(NorthStarVehicleState const& before,
    NorthStarVehicleState& after) {
    auto it=script_vehicle_rigs.find(after.entity_index);
    if(!preview||!script_vehicle||it==script_vehicle_rigs.end()||
       it->second.contact_mode!="physics")return;
    auto const id=after.entity_index;
    float const dx=after.position[0]-before.position[0];
    float const dz=after.position[2]-before.position[2];
    if(std::hypot(dx,dz)<.0001f)return;
    std::optional<PhysicsCastHit> nearest;
    for(float offset:{-2.8f,0.f,2.8f}) {
        float const x=offset*std::sin(before.heading);
        float const z=offset*std::cos(before.heading);
        auto result=preview->sweep_sphere(
            {before.position[0]+x,before.position[1]+1.0f,
                before.position[2]+z},
            {after.position[0]+x,after.position[1]+1.0f,
                after.position[2]+z},
            1.15f,id);
        if(result && std::abs(result->normal.y)<.4f &&
           std::hypot(result->normal.x,result->normal.z)>.5f &&
           (!nearest||result->fraction<nearest->fraction))nearest=result;
    }
    if(!nearest)return;
    float const fraction=std::max(0.f,nearest->fraction-.02f);
    NorthStarVehicleCollision impact{};
    impact.size=sizeof(impact);
    impact.position[0]=before.position[0]+dx*fraction;
    impact.position[1]=after.position[1];
    impact.position[2]=before.position[2]+dz*fraction;
    impact.normal_x=nearest->normal.x;
    impact.normal_z=nearest->normal.z;
    impact.restitution=0.05f;
    script_vehicle->resolve_collision(id,impact);
    after=script_vehicle->state(id);
}
void advance_vehicles(float dt) {
    if(!preview||!script_vehicle||script_paused||script_suspended||
       script_vehicle->count()==0||dt<=0.f)return;
    auto const before=script_vehicle->states();
    for(auto const& state:before)sample_vehicle_contacts(state);
    script_vehicle->step(dt);
    auto after=script_vehicle->states();
    for(auto& state:after) {
        auto const index=state.entity_index;
        if(index>=preview->scene().objects.size())continue;
        auto const& object=preview->scene().objects[index];
        if(scene_pawn::is_pawn(preview->scene(),object)||object.mass!=0)continue;
        if(script_vehicle_native.count(index)) {
            auto const pose=preview->native_vehicle_pose(index);
            float gas=state.throttle,steer=state.steering,brake=state.brake;
            auto route=script_vehicle_targets.find(index);
            if(route!=script_vehicle_targets.end()) {
                auto const target=route->second;
                float const dx=target.x-pose.x,dz=target.z-pose.z;
                float const distance=std::hypot(dx,dz);
                float const heading=std::atan2(pose.rotation[6],pose.rotation[8]);
                float const error=std::remainder(std::atan2(dx,dz)-heading,
                    6.28318530717958647692f);
                float const speed=pose.linear_velocity[0]*pose.rotation[6]+
                    pose.linear_velocity[1]*pose.rotation[7]+
                    pose.linear_velocity[2]*pose.rotation[8];
                if(distance<=target.radius && std::abs(speed)<.3f) {
                    script_vehicle_targets.erase(route);
                    gas=0.f;steer=0.f;brake=1.f;
                } else {
                    steer=std::clamp(error/.75f,-1.f,1.f);
                    float const safe_speed=std::sqrt(8.f*
                        std::max(0.f,distance-target.radius));
                    float cruise=std::min(target.speed,safe_speed);
                    // Basic forward obstacle perception: controlled braking,
                    // not a lane or detour planner. Jolt handles collisions.
                    if(speed>.15f) {
                        float const sx=pose.rotation[6],sz=pose.rotation[8];
                        float const lookahead=std::clamp(
                            12.f+2.f*speed+speed*speed/12.f,14.f,100.f);
                        float const head_x=pose.x+sx*4.8f;
                        float const head_y=pose.y+1.35f;
                        float const head_z=pose.z+sz*4.8f;
                        auto const hit=preview->sweep_sphere(
                            {head_x,head_y,head_z},
                            {head_x+sx*lookahead,head_y,head_z+sz*lookahead},
                            .65f,index);
                        if(hit && hit->normal.y<.35f) {
                            float const free_distance=
                                std::max(0.f,lookahead*hit->fraction-3.5f);
                            cruise=std::min(cruise,
                                std::sqrt(2.f*4.5f*free_distance));
                            if(free_distance<2.f)cruise=0.f;
                        }
                    }
                    gas=std::clamp((cruise-speed)*.45f,0.f,1.f);
                    brake=std::clamp((speed-cruise)*.35f,0.f,1.f);
                }
            }
            preview->native_vehicle_controls(index,gas,steer,brake);
            if(script_ecs&&index<script_ecs->size()) {
                NsPosition e{pose.x,pose.y,pose.z};
                script_ecs->set_component(script_ecs->at(index),
                    NS_COMPONENT_POSITION,&e,sizeof(e));
            }
            continue; // Jolt exclusively owns the 6DOF rigid-body pose.
        }
        auto previous=std::find_if(before.begin(),before.end(),
            [index](auto const& b){return b.entity_index==index;});
        if(previous!=before.end())resolve_vehicle_obstacles(*previous,state);
        Vector3 position{state.position[0],state.position[1],state.position[2]};
        preview->move_static(index,position);
        if(script_ecs&&index<script_ecs->size()) {
            NsPosition ecs_position{position.x,position.y,position.z};
            script_ecs->set_component(script_ecs->at(index),
                NS_COMPONENT_POSITION,&ecs_position,sizeof(ecs_position));
        }
    }
}
void apply_vehicle_visuals(Scene& scene) {
    if(!script_vehicle||script_vehicle->count()==0)return;
    constexpr float to_degrees=57.29577951308232f;
    for(auto const& state:script_vehicle->states()) {
        auto const index=state.entity_index;
        if(index>=scene.objects.size())continue;
        auto& chassis=scene.objects[index];
        auto const physical=script_vehicle_native.count(index)!=0;
        float heading=state.heading,body_pitch=0.f,body_roll=0.f;
        NorthStarPhysicsVehiclePose pose{};
        if(physical) {
            pose=preview->native_vehicle_pose(index);
            chassis.position={pose.x,pose.y,pose.z};
            body_pitch=std::asin(std::clamp(-pose.rotation[7],-1.f,1.f));
            heading=std::atan2(pose.rotation[6],pose.rotation[8]);
            body_roll=std::atan2(pose.rotation[1],pose.rotation[4]);
        }
        chassis.node.rotation.x=body_pitch*to_degrees;
        chassis.node.rotation.y=heading*to_degrees;
        chassis.node.rotation.z=body_roll*to_degrees;
        // Only the authored visible scene nodes move here. Collision and
        // dynamics are exclusively owned by Jolt (or the fallback provider).
        auto const visual=script_vehicle_visual_indices.find(index);
        auto const rig=script_vehicle_rigs.find(index);
        if(visual==script_vehicle_visual_indices.end() ||
           rig==script_vehicle_rigs.end())continue;
        auto const& binding=visual->second;
        for(std::size_t n=0;n<binding.size() && n<rig->second.wheels.size();++n) {
            auto const i=binding[n];
            if(i==UINT32_MAX || i>=scene.objects.size())continue;
            auto const& wheel_spec=rig->second.wheels[n];
            auto const wheel=physical?
                preview->native_vehicle_wheel(index,static_cast<std::uint32_t>(n)):
                script_vehicle->wheel(index,static_cast<std::uint32_t>(n));
            auto& target=scene.objects[i];
            float const lx=wheel_spec.mount[0],lz=wheel_spec.mount[2];
            float const suspension=wheel_spec.rest_length+
                wheel_spec.travel*.5f-wheel.compression;
            float const ly=wheel_spec.mount[1]-suspension;
            if(physical) {
                auto const half=preview->scene().objects.at(index).size*.5f;
                target.position={
                    pose.x+lx*pose.rotation[0]+ly*pose.rotation[3]+
                        lz*pose.rotation[6],
                    pose.y+half+lx*pose.rotation[1]+ly*pose.rotation[4]+
                        lz*pose.rotation[7],
                    pose.z+lx*pose.rotation[2]+ly*pose.rotation[5]+
                        lz*pose.rotation[8]};
            } else {
                float const sn=std::sin(heading),cs=std::cos(heading);
                target.position={state.position[0]+lx*cs+lz*sn,
                    state.position[1]+ly,state.position[2]+lz*cs-lx*sn};
            }
            target.node.rotation.x=(body_pitch+wheel.spin_angle)*to_degrees;
            target.node.rotation.y=(heading+wheel.steer_angle)*to_degrees;
            target.node.rotation.z=body_roll*to_degrees;
        }
        auto const door_nodes=script_vehicle_door_visual_indices.find(index);
        if(door_nodes!=script_vehicle_door_visual_indices.end() &&
           script_vehicle->supports_cabin()) {
            auto const& entries=door_nodes->second;
            for(std::size_t n=0;n<entries.size()&&n<rig->second.doors.size();++n) {
                auto const i=entries[n];
                if(i==UINT32_MAX||i>=scene.objects.size())continue;
                auto const& d=rig->second.doors[n];
                auto const door=script_vehicle->door(index,static_cast<std::uint32_t>(n));
                auto& target=scene.objects[i];
                float const x=d.hinge[0],y=d.hinge[1],z=d.hinge[2];
                if(physical) {
                    float const half=preview->scene().objects.at(index).size*.5f;
                    target.position={
                        pose.x+x*pose.rotation[0]+y*pose.rotation[3]+z*pose.rotation[6],
                        pose.y+half+x*pose.rotation[1]+y*pose.rotation[4]+z*pose.rotation[7],
                        pose.z+x*pose.rotation[2]+y*pose.rotation[5]+z*pose.rotation[8]};
                } else {
                    float const sn=std::sin(heading),cs=std::cos(heading);
                    target.position={state.position[0]+x*cs+z*sn,
                        state.position[1]+y,state.position[2]+z*cs-x*sn};
                }
                target.node.rotation.x=body_pitch*to_degrees;
                target.node.rotation.y=(heading+door.angle)*to_degrees;
                target.node.rotation.z=body_roll*to_degrees;
            }
        }
        // Original composite meshes follow the live parent pose without
        // spawning extra Jolt rigid bodies or per-frame asset lookups.
        auto const followers=script_vehicle_visual_followers.find(index);
        if(followers!=script_vehicle_visual_followers.end()) {
            constexpr float to_radians=0.017453292519943295f;
            for(auto const& ref:followers->second) {
                if(ref.child_index>=scene.objects.size() ||
                   ref.parent_index>=scene.objects.size())continue;
                auto const& base=scene.objects[ref.parent_index];
                auto& child=scene.objects[ref.child_index];
                Vector3 offset=ref.local_offset;
                if(offset.x!=0.f || offset.y!=0.f || offset.z!=0.f) {
                    const float ax=base.node.rotation.x*to_radians;
                    const float ay=base.node.rotation.y*to_radians;
                    const float az=base.node.rotation.z*to_radians;
                    const float sx=std::sin(ax),cx=std::cos(ax);
                    const float sy=std::sin(ay),cy=std::cos(ay);
                    const float sz=std::sin(az),cz=std::cos(az);
                    const float x0=offset.x;
                    const float y0=offset.y*cx-offset.z*sx;
                    const float z0=offset.y*sx+offset.z*cx;
                    const float x1=x0*cz-y0*sz;
                    const float y1=x0*sz+y0*cz;
                    offset={x1*cy+z0*sy,y1,z0*cy-x1*sy};
                }
                child.position={
                    base.position.x+offset.x,
                    base.position.y+offset.y,
                    base.position.z+offset.z};
                child.node.rotation={
                    base.node.rotation.x+ref.local_rotation.x,
                    base.node.rotation.y+ref.local_rotation.y,
                    base.node.rotation.z+ref.local_rotation.z};
            }
        }
    }
}
