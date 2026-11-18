// NPC Controller to Jolt Motor integration.
    // Explicit game-AI movement runs on the physics owner thread. NPCs are
    // separate Jolt dynamic bodies; no extra ECS entity is created or teleported.
    void update_ai_motors(float seconds) {
        if(!preview || seconds<=0.f || !preview->supports_ai_motor())return;
        auto const& scene=preview->scene();
        // One shared raster budget for ALL NPCs, not N unbounded queries.
        std::size_t nav_build_budget=72;
        // Snapshot moving bodies once per tick for bounded crowd steering.
        std::vector<navigation::CrowdObstacle> crowd_obstacles;
        crowd_obstacles.reserve(48);
        for(std::size_t i=0;i<scene.objects.size();++i) {
            auto const& body=scene.objects[i];
            const bool vehicle=script_vehicle_native.count(
                static_cast<std::uint32_t>(i))!=0;
            if((body.mass<=0.f && !vehicle)||!body.node.id)continue;
            navigation::CrowdObstacle entry;
            entry.id=body.node.id;
            entry.position=body.position;
            entry.radius=std::clamp(body.size*.44f,.35f,vehicle?3.2f:1.1f);
            entry.cooperative=script_ai_controllers.count(body.node.id)!=0;
            if(auto other=script_ai_controllers.find(body.node.id);
               other!=script_ai_controllers.end() &&
               other->second.have_previous) {
                auto const& previous=other->second.previous_position;
                entry.velocity={
                    (body.position.x-previous.x)/seconds,0.f,
                    (body.position.z-previous.z)/seconds};
            } else if(vehicle) {
                try {
                    auto const pose=preview->native_vehicle_pose(i);
                    entry.position={pose.x,pose.y,pose.z};
                    entry.velocity={pose.linear_velocity[0],0.f,
                                    pose.linear_velocity[2]};
                } catch(std::exception const&) {continue;}
            }
            const auto measured=std::hypot(
                entry.velocity.x,entry.velocity.z);
            if(!std::isfinite(measured))continue;
            if(measured>45.f) {
                entry.velocity.x*=45.f/measured;
                entry.velocity.z*=45.f/measured;
            }
            crowd_obstacles.push_back(entry);
        }
        auto& possession_events=npc_controller_event_scratch;
        possession_events.clear();
        auto collect_events=[&](std::uint64_t id,gameplay::AIController& controller) {
            while(auto e=controller.poll_possession_event())
                possession_events.emplace_back(id,*e);
        };
        for(auto it=script_ai_controllers.begin();it!=script_ai_controllers.end();) {
            auto const pawn=it->first;
            auto& npc=it->second;
            npc.controller.reconcile(scene);
            // Player takeover ends the NPC controller lifetime, not the Pawn.
            if(pawn==preview->controlled_entity() && npc.controller.possessed())
                npc.controller.unpossess();
            collect_events(pawn,npc.controller);
            if(!npc.controller.possessed() || pawn==preview->controlled_entity()) {
                if(npc.motor_active && pawn!=preview->controlled_entity() &&
                   gameplay::entity_index(scene,pawn)) {
                    try {preview->clear_ai_move_intent(pawn);}
                    catch(std::exception const& error) {
                        std::fprintf(stderr,"[WARN] [engine.ai] Stale motor: %s\n",error.what());
                    }
                }
                it=script_ai_controllers.erase(it);
                continue;
            }
            auto const index=gameplay::entity_index(scene,pawn);
            auto intent=npc.controller.next_intent(scene);
            if(!index || intent.arrived) {
                if(npc.motor_active)preview->clear_ai_move_intent(pawn);
                npc.motor_active=false;
                ++it;
                continue;
            }
            const auto& position=scene.objects[*index].position;
            const auto walk=gameplay::motor_settings(scene,pawn).walk_speed;
            const float speed=std::clamp(walk,.1f,12.f);
            npc.replanning_delay=std::max(0.f,npc.replanning_delay-seconds);
            if(!npc.navmesh && npc.path.empty() &&
               npc.replanning_delay<=0.f && !npc.route_failed) {
                navigation::NavSettings settings;
                settings.cell_size=.95f;
                settings.padding=4.f;
                settings.max_side=44;
                settings.max_step=.48f;
                npc.navmesh.emplace(settings);
                if(!npc.navmesh->begin_build(position,npc.controller.goal())) {
                    npc.navmesh.reset();
                    npc.route_failed=true;
                    npc.replanning_delay=1.5f;
                }
            }
            if(npc.navmesh && !npc.navmesh->ready() && nav_build_budget) {
                const auto budget=std::min<std::size_t>(nav_build_budget,24);
                nav_build_budget-=budget;
                auto sample=[&](float px,float pz,float probe)
                    ->std::optional<navigation::NavSurface> {
                    constexpr float up=1.3f,down=2.0f;
                    auto hit=preview->raycast_channel(
                        {px,probe+up,pz},{px,probe-down,pz},2u,false,*index);
                    if(!hit || hit->response!=2u || hit->normal.y<.70f ||
                       !std::isfinite(hit->fraction))
                        return std::nullopt;
                    // Moving NPCs/vehicles are dynamic avoidance obstacles,
                    // not floors baked into the current navigation tile.
                    if(hit->object_index<scene.objects.size() &&
                       scene.objects[hit->object_index].mass>0.f)
                        return std::nullopt;
                    return navigation::NavSurface{
                        probe+up-(up+down)*hit->fraction,hit->normal};
                };
                try {npc.navmesh->advance_build(sample,budget);}
                catch(std::exception const& error) {
                    std::fprintf(stderr,
                        "[WARN] [engine.ai] NavMesh raster failed: %s\n",error.what());
                    npc.navmesh.reset();
                    npc.route_failed=true;
                    npc.replanning_delay=1.5f;
                }
            }
            if(npc.navmesh && npc.navmesh->ready()) {
                auto route=npc.navmesh->route(
                    position,npc.controller.goal(),
                    [&](Vector3 from,Vector3 to) {
                        auto hit=preview->sweep_sphere(from,to,.27f,*index);
                        return hit && hit->response==2u && hit->fraction<.92f;
                    });
                npc.path=std::move(route.waypoints);
                npc.path_cursor=0;
                npc.route_partial=route.status==navigation::NavRoute::Status::Partial;
                npc.route_failed=!route.can_advance();
                if(npc.route_failed)npc.replanning_delay=1.5f;
                npc.navmesh.reset();
            }
            // A missing/unwalkable floor is a safe stop, not a fallback to
            // blind forward movement or per-frame Pawn teleports.
            if(npc.path.empty()) {
                if(npc.motor_active)preview->clear_ai_move_intent(pawn);
                npc.motor_active=false;
                if(npc.route_failed && npc.replanning_delay<=0.f)
                    npc.route_failed=false;
                ++it;
                continue;
            }
            while(npc.path_cursor<npc.path.size()) {
                auto const& waypoint=npc.path[npc.path_cursor];
                if(std::hypot(waypoint.x-position.x,waypoint.z-position.z)>.6f)
                    break;
                ++npc.path_cursor;
            }
            if(npc.path_cursor>=npc.path.size()) {
                npc.path.clear();
                npc.path_cursor=0;
                npc.navmesh.reset();
                if(npc.motor_active)preview->clear_ai_move_intent(pawn);
                npc.motor_active=false;
                ++it;
                continue;
            }
            auto const& waypoint=npc.path[npc.path_cursor];
            const float remaining=std::hypot(waypoint.x-position.x,
                                             waypoint.z-position.z);
            if(remaining>.001f) {
                intent.direction.x=(waypoint.x-position.x)/remaining;
                intent.direction.z=(waypoint.z-position.z)/remaining;
            } else intent.direction={0.f,0.f,0.f};
            // A short collision sweep detects blocking geometry without
            // changing the Pawn's physics transform or collision channels.
            const Vector3 eye{position.x,position.y+.70f,position.z};
            const float distance=std::min(1.2f,std::max(.5f,speed*.35f));
            auto const blocked=[&](float x,float z) {
                auto const hit=preview->sweep_sphere(eye,
                    {eye.x+x*distance,eye.y,eye.z+z*distance},.22f,*index);
                return hit && hit->response==2u && hit->fraction<.90f;
            };
            auto const crowd=navigation::steer_crowd(
                pawn,position,intent.direction,speed,.42f,crowd_obstacles);
            auto x=crowd.direction.x,z=crowd.direction.z;
            float motor_speed=speed*crowd.velocity_scale;
            if(blocked(x,z)) {
                // Local steering around convex obstacles. A polygon NavMesh
                // remains a separate provider and is NOT inferred here.
                const bool leftOpen=!blocked(-z,x);
                const bool rightOpen=!blocked(z,-x);
                if(leftOpen){const float temp=x;x=-z;z=temp;}
                else if(rightOpen){const float temp=x;x=z;z=-temp;}
                else x=z=0.f; // collision stop, never tunnel or teleport
            }
            preview->set_ai_move_intent(pawn,x,z,motor_speed);
            npc.motor_active=true;
            ++it;
        }
        // Emit only after iterators are released: callbacks may create NPCs.
        if(script_runtime && !script_suspended) {
            for(auto const& [owner,e]:possession_events) {
                auto name=e.kind==gameplay::PossessionEventKind::OnPossess?
                    "ai.OnPossess":"ai.OnUnPossess";
                auto payload=std::string("{\"controllerPawn\":")+
                    script_json_quote(std::to_string(owner))+
                    ",\"pawn\":"+script_json_quote(std::to_string(e.pawn))+
                    ",\"otherPawn\":"+script_json_quote(std::to_string(e.other_pawn))+
                    ",\"revision\":"+std::to_string(e.revision)+"}";
                try {script_runtime->emit_event(name,payload);}
                catch(std::exception const& error) {
                    std::fprintf(stderr,"[WARN] [engine.ai] %s: %s\n",
                        name,error.what());
                }
            }
        }
    }
    // Temporary render-only yaw. Jolt retains the upright capsule's
    // original physical orientation and authoritative collision response.
    void apply_ai_visual_facing(Scene& visual) const {
        for(auto const& [pawn,npc]:script_ai_controllers) {
            if(!npc.have_visual_yaw || !std::isfinite(npc.visual_yaw))
                continue;
            std::string payload(sizeof(float),'\0');
            std::memcpy(payload.data(),&npc.visual_yaw,sizeof(float));
            visual.components.push_back(
                {pawn,"engine.model.visual_yaw",1,std::move(payload)});
        }
    }
    void update_ai_animations(float seconds) {
        if(!preview || seconds<=0.f)return;
        auto const& scene=preview->scene();
        for(auto& [pawn,npc]:script_ai_controllers) {
            auto index=gameplay::entity_index(scene,pawn);
            if(!index)continue;
            auto const& at=scene.objects[*index].position;
            float speed=0.f;
            if(npc.have_previous)
                speed=std::hypot(at.x-npc.previous_position.x,
                                 at.z-npc.previous_position.z)/seconds;
            if(npc.motor_active && npc.have_previous && speed<.09f)
                npc.stall_seconds+=std::min(seconds,.10f);
            else if(speed>.18f || !npc.motor_active)
                npc.stall_seconds=0.f;
            if(npc.stall_seconds>1.4f) {
                npc.path.clear(); // dynamic obstacle or changed scenery: replan
                npc.path_cursor=0;
                npc.navmesh.reset();
                npc.route_failed=false;
                npc.replanning_delay=.20f;
                npc.stall_seconds=0.f;
            }
            if(npc.have_previous && speed>.12f && speed<20.f) {
                const float dx=at.x-npc.previous_position.x;
                const float dz=at.z-npc.previous_position.z;
                const float desired=std::atan2(dx,dz);
                if(!npc.have_visual_yaw) {
                    npc.visual_yaw=desired;
                    npc.have_visual_yaw=true;
                } else {
                    constexpr float two_pi=6.2831853071795864769f;
                    const float delta=std::remainder(
                        desired-npc.visual_yaw,two_pi);
                    const float weight=1.f-std::exp(
                        -std::min(seconds,.12f)*9.f);
                    npc.visual_yaw=std::remainder(
                        npc.visual_yaw+delta*weight,two_pi);
                }
            }
            npc.previous_position=at;
            npc.have_previous=true;
            if(!npc.animation)continue;
            bool grounded=at.y>=-.1f && at.y<.14f;
            auto ground=preview->sweep_sphere(
                {at.x,at.y+.25f,at.z},{at.x,at.y-.35f,at.z},.12f,*index);
            grounded|=ground && ground->normal.y>.55f;
            npc.animation->advance(std::min(seconds,.25f),speed,grounded,false);
            if(npc.mesh_id) {
                auto const& clip=npc.animation->sample().clip;
                if(!clip.empty())script_model_animations.insert_or_assign(npc.mesh_id,clip);
            }
        }
    }
