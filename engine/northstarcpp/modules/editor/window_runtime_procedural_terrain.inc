// Project-configured procedural terrain, shared GPU/physics residency.
// Included as App member functions by window.cpp, never as a stand-alone TU.
    void update_procedural_terrain(float seconds) {
        if(config.archive_path.empty() && !file_path.empty()) {
            auto const config_file=fs::absolute(file_path).parent_path()/
                "assets/streaming/procedural_terrain.json";
            if(!terrain_config_checked || terrain_config_path!=config_file) {
                terrain_config_checked=true;
                terrain_config_path=config_file;
                terrain_config_enabled=false;
                terrain_physics_bridge.reset();
                terrain_async_streamer.reset();
                terrain_contact_provider.reset();
                terrain_editor_streamer.reset();
                terrain_visual_chunks.clear();
                terrain_attached_preview=nullptr;
                if(fs::is_regular_file(config_file)) {
                    boost::property_tree::ptree source;
                    boost::property_tree::read_json(config_file.string(),source);
                    auto const enabled=source.get<bool>("enabled",false);
                    if(enabled) {
                        terrain_settings.seed=source.get<std::uint32_t>(
                            "seed",20261117u);
                        terrain_settings.width_m=source.get<double>(
                            "chunk_width_m",64.);
                        terrain_settings.base_height_m=source.get<double>(
                            "base_height_m",-1.2);
                        terrain_settings.vertical_scale_m=source.get<double>(
                            "vertical_scale_m",4.);
                        terrain_settings.radius=source.get<std::uint32_t>(
                            "radius_chunks",4u);
                        terrain_settings.quads_per_axis=source.get<std::uint32_t>(
                            "render_quads",32u);
                        terrain_settings.collision_quads=source.get<std::uint32_t>(
                            "collision_quads",16u);
                        terrain_settings.max_lod=source.get<std::uint32_t>(
                            "max_lod",3u);
                        terrain_settings.validate();
                        terrain_contact_provider=std::make_unique<world::TerrainProvider>(terrain_settings);
                        terrain_follow_entity=source.get<std::string>(
                            "follow_entity","");
                        terrain_material=source.get<std::string>(
                            "material","materials/rustsea.ymat@ground_dry");
                        if(terrain_material.size()<6 ||
                           terrain_material.find("..")!=std::string::npos)
                            throw std::runtime_error(
                                "Procedural terrain requires safe project YMAT");
                        terrain_config_enabled=true;
                        std::fprintf(stderr,
                            "[INFO] [terrain.streaming] enabled seed=%u width=%.1f radius=%u relief=%.1f\n",
                            terrain_settings.seed,terrain_settings.width_m,
                            terrain_settings.radius,terrain_settings.vertical_scale_m);
                    }
                }
            }
        }
        if(!terrain_config_enabled)return;
        if(!preview) {
            // Editor uses render-only streaming around its camera, without
            // altering a physics world until Play.
            if(terrain_attached_preview) {
                terrain_attached_preview=nullptr;
                terrain_physics_bridge.reset();
                terrain_async_streamer.reset();
                terrain_visual_chunks.clear();
            }
            if(!terrain_editor_streamer) {
                terrain_editor_streamer=
                    std::make_unique<world::TerrainStreamer>(terrain_settings);
                terrain_visual_chunks.clear();
            }
            auto const& camera=editor_camera.pose.position;
            auto delta=terrain_editor_streamer->update(
                {camera.x,camera.y,camera.z});
            for(auto const& key:delta.evicted)
                terrain_visual_chunks.erase(key);
            for(auto& chunk:delta.uploaded)
                terrain_visual_chunks.insert_or_assign(chunk.key,std::move(chunk));
            return;
        }
        if(terrain_editor_streamer) {
            terrain_editor_streamer.reset();
            terrain_visual_chunks.clear();
        }
        if(terrain_attached_preview!=preview.get()) {
            terrain_physics_bridge.reset();
            terrain_async_streamer.reset();
            terrain_visual_chunks.clear();
            if(!preview->supports_streamed_terrain())
                throw std::runtime_error(
                    "Selected physics provider lacks streamed terrain ABI");
            terrain_physics_bridge=std::make_unique<TerrainPhysicsBridge>(
                terrain_settings);
            terrain_async_streamer=std::make_unique<world::TerrainAsyncStreamer>(
                terrain_settings);
            terrain_attached_preview=preview.get();
            terrain_last_focus={};
            terrain_have_focus=false;
            terrain_follow_index=std::numeric_limits<std::size_t>::max();
            auto const& objs=preview->scene().objects;
            for(std::size_t index=0;index<objs.size();++index)
                if(objs[index].name==terrain_follow_entity) {
                    terrain_follow_index=index;
                    break;
                }
            if(terrain_follow_index==std::numeric_limits<std::size_t>::max())
                throw std::runtime_error(
                    "Streamed terrain follow_entity not found in scene: "+
                    terrain_follow_entity);
        }
        auto const& objects=preview->scene().objects;
        if(terrain_follow_index>=objects.size())
            throw std::runtime_error("Streamed terrain lost tracked city root");
        auto const& pos=objects[terrain_follow_index].position;
        world::Vec3d focus{pos.x,pos.y,pos.z};
        world::Vec3d velocity{};
        if(terrain_have_focus && seconds>1.e-5f) {
            velocity.x=std::clamp(
                (focus.x-terrain_last_focus.x)/seconds,-70.,70.);
            velocity.z=std::clamp(
                (focus.z-terrain_last_focus.z)/seconds,-70.,70.);
        }
        // Gameplay/physics authoritative. The renderer only receives copies
        // of the same TerrainProvider-generated chunks returned by the bridge.
        // Poll without blocking the frame. Only the initial chunk ring is
        // generated inline; later transitions run on a background worker.
        auto generated=terrain_async_streamer->update(focus,velocity);
        if(!generated)return;
        auto& delta=*generated;
        terrain_physics_bridge->apply_delta(*preview,delta);
        for(auto const& key:delta.evicted)
            terrain_visual_chunks.erase(key);
        for(auto& part:delta.uploaded)
            terrain_visual_chunks.insert_or_assign(part.key,std::move(part));
        terrain_last_focus=focus;
        terrain_have_focus=true;
        if(!delta.uploaded.empty()||!delta.evicted.empty())
            std::fprintf(stderr,
                "[INFO] [terrain.streaming] generation=%llu loaded=%zu evicted=%zu resident=%zu at=%.1f,%.1f\n",
                static_cast<unsigned long long>(delta.generation),
                delta.uploaded.size(),delta.evicted.size(),
                terrain_visual_chunks.size(),focus.x,focus.z);
        if(terrain_visual_chunks.size()>
           (2u*terrain_settings.radius+1u)*(2u*terrain_settings.radius+1u))
            throw std::runtime_error("Terrain GPU residency exceeds budget");
    }

    // Runtime-init smoke can exercise the real App->PhysicsScene->Jolt
    // bridge without creating a window or a GPU device.
public:
    void verify_procedural_terrain_startup() {
        if(!preview)return;
        update_procedural_terrain(1.f/60.f);
        if(terrain_config_enabled) {
            auto const side=2u*terrain_settings.radius+1u;
            auto const expected=side*side;
            if(terrain_visual_chunks.size()!=expected ||
               preview->terrain_resident_count()!=expected)
                throw std::runtime_error(
                    "Procedural terrain runtime CPU/GPU residency mismatch");
            if(terrain_follow_entity=="CITYROOT Bastion" &&
               (!script_vehicle || script_vehicle->count()!=2))
                throw std::runtime_error(
                    "Sea of Rust: two CityRoot vehicles did not register during script start");
            if(auto const* qa=std::getenv("NORTHSTARCPP_RUSTSEA_DRIVE_QA");
               terrain_follow_entity=="CITYROOT Bastion" &&
               qa && qa[0]=='1') {
                // A production-like fixed-step integration test: script
                // controls -> Jolt massive dynamic chassis -> kinematic
                // deck followers -> grounded Pawn. Do not perform this loop
                // in a normal interactive game startup.
                auto const first_pose=preview->native_vehicle_pose(
                    static_cast<std::uint32_t>(terrain_follow_index));
                std::size_t enemy_index=static_cast<std::size_t>(-1);
                for(std::size_t i=0;i<preview->scene().objects.size();++i)
                    if(preview->scene().objects[i].name=="CITYROOT Rust Wren"){
                        enemy_index=i;break;
                    }
                if(enemy_index==static_cast<std::size_t>(-1))
                    throw std::runtime_error(
                        "Rust Sea: missing physical Rust Wren CityRoot");
                auto const enemy_start=preview->native_vehicle_pose(
                    static_cast<std::uint32_t>(enemy_index));
                auto const pawn_index=preview->controlled_index();
                float initial_pawn_z=0.f;
                if(pawn_index)
                    initial_pawn_z=preview->scene().objects[*pawn_index].position.z;
                const int qa_seconds=std::clamp(
                    std::getenv("NORTHSTARCPP_RUSTSEA_QA_SECONDS")?
                    std::atoi(std::getenv("NORTHSTARCPP_RUSTSEA_QA_SECONDS")):8,
                    8,240);
                const int qa_ticks=qa_seconds*60;
                float max_relative_error=0.f;
                float max_height=first_pose.y;
                for(int tick=0;tick<qa_ticks;++tick) {
                    script_runtime->emit_event("update","0.0166666667");
                    preview->step(1.f/60.f,0.f,0.f);
                    advance_vehicles(1.f/60.f);
                    // Mirror the real frame's after-physics world transform.
                    script_runtime->emit_event("update","0");
                    update_procedural_terrain(1.f/60.f);
                    if(tick%300==299 || tick+1==qa_ticks) {
                        auto const current=preview->native_vehicle_pose(
                            static_cast<std::uint32_t>(terrain_follow_index));
                        float pawn_z=0.f;
                        if(pawn_index) {
                            pawn_z=preview->scene().objects[*pawn_index].position.z;
                            max_relative_error=std::max(max_relative_error,
                                std::abs((pawn_z-initial_pawn_z)-
                                (current.z-first_pose.z)));
                        }
                        max_height=std::max(max_height,current.y);
                        std::fprintf(stderr,
                            "[INFO] [rustsea.longdrive] second=%d city_z=%.2f city_y=%.2f pawn_z=%.2f relative_error=%.3f resident=%zu\n",
                            (tick+1)/60,current.z,current.y,pawn_z,
                            max_relative_error,terrain_visual_chunks.size());
                    }
                }
                auto const last_pose=preview->native_vehicle_pose(
                    static_cast<std::uint32_t>(terrain_follow_index));
                auto const travelled=last_pose.z-first_pose.z;
                auto const enemy_end=preview->native_vehicle_pose(
                    static_cast<std::uint32_t>(enemy_index));
                auto const enemy_travelled=enemy_end.z-enemy_start.z;
                float deck_z=0.f,deck_y=0.f;
                for(auto const& item:preview->scene().objects)
                    if(item.name=="BASTION Deck -3 -12") {
                        deck_z=item.position.z;deck_y=item.position.y;
                        break;
                    }
                if(std::abs(deck_z-(last_pose.z-12.f))>.04f)
                    throw std::runtime_error(
                        "Sea of Rust: visual deck is one frame behind physics");
                std::fprintf(stderr,
                    "[INFO] [rustsea.drive.debug] heading=%.5f x=%.3f y=%.3f z=%.3f deck_z=%.3f deck_y=%.3f\n",
                    std::atan2(last_pose.rotation[6],last_pose.rotation[8]),
                    last_pose.x,last_pose.y,last_pose.z,deck_z,deck_y);
                const float pawn_delta=pawn_index?
                    preview->scene().objects[*pawn_index].position.z-initial_pawn_z:0.f;
                std::fprintf(stderr,
                    "[INFO] [rustsea.drive.qa] city_delta_z=%.3f pawn_delta_z=%.3f chassis_mass_kg=25000000\n",
                    travelled,pawn_delta);
                if(qa_seconds>=30 &&
                   (max_relative_error>2.f || max_height>first_pose.y+4.f))
                    throw std::runtime_error(
                        "Sea of Rust: long drive carrier jitter or city disappeared");
                if(!std::isfinite(travelled)||travelled<1.f ||
                   !std::isfinite(last_pose.y) ||
                   std::abs(last_pose.y-first_pose.y)>2.f)
                    throw std::runtime_error(
                        "Sea of Rust: physical Bastion stopped or bounced off terrain");
                std::fprintf(stderr,
                    "[INFO] [rustsea.enemy.qa] dz=%.4f dy=%.4f y=%.3f vz=%.3f vy=%.3f\\n",
                    enemy_travelled,enemy_end.y-enemy_start.y,enemy_end.y,
                    enemy_end.linear_velocity[2],enemy_end.linear_velocity[1]);
                if(!std::isfinite(enemy_travelled)||enemy_travelled<.5f ||
                   !std::isfinite(enemy_end.y)||
                   std::abs(enemy_end.y-enemy_start.y)>3.f)
                    throw std::runtime_error(
                        "Sea of Rust: Rust Wren did not drive stably");
                if(!pawn_index ||
                   std::abs(pawn_delta-travelled)>1.5f)
                    throw std::runtime_error(
                        "Sea of Rust: dynamic deck lost physical Pawn contact");
                std::fprintf(stderr,
                    "[INFO] [rustsea.drive.qa] enemy_delta_z=%.3f enemy_delta_y=%.3f contact=stable\n",
                    enemy_travelled,enemy_end.y-enemy_start.y);
            }
            auto const& first=terrain_visual_chunks.begin()->second;
            if(first.render.indices.empty()||first.collision.indices.empty())
                throw std::runtime_error(
                    "Procedural terrain lacks render or Jolt geometry");
        }
    }
private:
    std::map<world::ChunkKey,world::TerrainChunk> const*
        procedural_terrain_render_chunks() const noexcept {
        return terrain_visual_chunks.empty()?nullptr:&terrain_visual_chunks;
    }
