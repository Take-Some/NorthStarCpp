    void load_script_files(ScriptRuntime& runtime) {
        if(!script_assets || !preview)
            throw std::runtime_error("Script asset VFS is not mounted");
        script_bootstrap::Options options;
        options.project_root=script_source_root;
        options.archive_index=config.archive_path;
        options.archive_data=config.archive_data;
        options.load_project_entry=script_entry_exists;
        options.validate_project_path=[this](fs::path const& relative) {
            if(config.archive_path.empty())
                (void)validated_script_file(script_source_root,relative);
            else
                validate_script_virtual_path(relative);
        };
        auto const report=script_bootstrap::load(
            runtime,*script_assets,options,script_logger.get());
        auto const entity_start=Clock::now();
        std::unordered_set<std::string> unique;
        // Scenes may contain thousands of actors but only a few unique
        // script paths; avoid reserving the whole scene size.
        unique.reserve(std::min<std::size_t>(128,preview->scene().objects.size()));
        std::uint64_t entity_bytes=0;
        auto log_entity=[this](LogLevel level,std::string const& message) noexcept {
            if(!script_logger)return;
            try {script_logger->write(level,"engine.scripting.bootstrap",message);}
            catch(...) {}
        };
        for(auto const& object:preview->scene().objects) {
            if(object.node.script.empty() ||
               !unique.insert(object.node.script).second)
                continue;
            auto const started=Clock::now();
            try {
                auto const relative=fs::u8path(object.node.script);
                if(config.archive_path.empty())
                    (void)validated_script_file(script_source_root,relative);
                else
                    validate_script_virtual_path(relative);
                auto const bytes=script_assets->read(object.node.script);
                if(bytes.size()>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                    throw std::length_error("Entity script source exceeds 1 MiB");
                // One allocation instead of copying bytes to a temporary
                // string and allocating again during JS scope wrapping.
                std::string wrapped;
                wrapped.reserve(bytes.size()+20);
                wrapped.append("(function(){\n");
                if(!bytes.empty())
                    wrapped.append(reinterpret_cast<char const*>(bytes.data()),bytes.size());
                wrapped.append("\n})();");
                runtime.evaluate(wrapped,object.node.script);
                entity_bytes+=bytes.size();
                log_entity(LogLevel::debug,
                    "phase=entity status=pass path="+object.node.script+
                    " bytes="+std::to_string(bytes.size())+
                    " duration_us="+std::to_string(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            Clock::now()-started).count()));
            }catch(std::exception const& error) {
                log_entity(LogLevel::error,
                    "phase=entity status=fail path="+object.node.script+
                    " reason="+error.what());
                throw;
            }
        }
        log_entity(LogLevel::info,
            "phase=ready status=pass embedded="+std::to_string(report.embedded)+
            " overrides="+std::to_string(report.project_overrides)+
            " entity_scripts="+std::to_string(unique.size())+
            " entity_bytes="+std::to_string(entity_bytes)+
            " total_source_bytes="+std::to_string(report.source_bytes+entity_bytes)+
            " entity_duration_us="+std::to_string(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now()-entity_start).count()));
    }

        void notify_script_entities(ScriptRuntime& runtime,char const* event,float dt=-1.f) {
        if (!preview) return;
        auto const& scene=preview->scene();
        // Batch JavaScript event dispatch rather than recompiling an individual
        // V8 script for every resident entity every frame. Each batch is
        // bounded independently of scene size and still uses the neutral ABI.
        std::string batch="[";
        std::size_t count=0;
        auto flush=[&] {
            if (!count) return;
            batch+="]";
            // Keep payload bounded and execute callbacks inside the JS realm,
            // but avoid one new Realm.eval compilation for every entity batch.
            runtime.emit_event("northstar._entityBatch",
                "{\"event\":"+script_json_quote(event)+",\"items\":"+batch+"}");
            batch="[";
            count=0;
        };
        for (std::size_t i=0;i<scene.objects.size();++i) {
            auto const& object=scene.objects[i];
            if (object.node.script.empty() ||
                (dt>=0.f && scene_streaming.world() && !scene_streaming.world()->object_resident(i))) continue;
            if (count) batch+=',';
            batch+=script_object_payload(object,i,dt);
            ++count;
            if (count==256 || batch.size()>=65536) flush();
        }
        flush();
    }

    // Watch only source projects; packaged game scripts are immutable runtime
    // assets, while editor/project source scripts are hot-reloadable.
    bool scripts_changed() {
        if (config.packaged_game || config.archive_path.empty()==false ||
            script_source_root.empty()) return false;
        bool changed=false;
        for (auto& [relative,stamp]:script_file_stamps) {
            std::error_code ec;
            auto next=fs::last_write_time(script_source_root/relative,ec);
            if (ec) next=fs::file_time_type::min();
            if (next!=stamp) { stamp=next; changed=true; }
        }
        return changed;
    }

    void reload_project_scripts() {
        if (!script_runtime) return;
        auto const reload_started=Clock::now();
        // Loading a candidate JS context may invoke engine.animation.play().
        // Preserve the previous animation state so failed hot reload cannot
        // destroy the active gameplay simulation.
        auto old_animations=script_animations
            ? std::make_unique<SceneAnimationPlayer>(*script_animations)
            : nullptr;
        auto old_visuals=script_animation_visuals;
        auto const old_character=script_character;
        auto const old_character_pose=script_character_pose;
        auto const old_character_dirty=script_character_dirty;
        auto const old_character_renderable=script_character_renderable;
        auto const previous_context_epoch=script_context_epoch;
        try {
            script_entry_exists=fs::is_regular_file(script_source_root/"scripts"/"main.js");
            ScriptingOptions scriptOptions;
            if(auto const* timeout=std::getenv("NORTHSTARCPP_QA_SCRIPT_TIMEOUT_MS")) {
                auto const value=std::strtoul(timeout,nullptr,10);
                if(value>=1000ul && value<=60000ul)
                    scriptOptions.timeout_ms=static_cast<std::uint32_t>(value);
            }
            auto fresh=std::make_unique<ScriptRuntime>(scriptOptions);
            configure_script_bindings(*fresh);
            load_script_files(*fresh);
            fresh->emit_event("start");
            notify_script_entities(*fresh,"entity.start");
            auto new_animations=script_animations
                ? std::make_unique<SceneAnimationPlayer>(*script_animations)
                : nullptr;
            // The old context's stop callback can clear native character state.
            // Capture the candidate's resources and pose before stopping the old
            // context, then restore them for the next render frame.
            auto new_character=script_character;
            auto new_character_pose=script_character_pose;
            try {
                notify_script_entities(*script_runtime,"entity.stop");
                script_runtime->emit_event("stop");
            } catch (...) { /* old scripts cannot veto a successful reload */ }
            cleanup_script_spawns(active_script_context_epoch);
            if(auto it=script_binding_leases.find(active_script_context_epoch);
               it!=script_binding_leases.end()) {
                it->second->revoke();
                script_binding_leases.erase(it);
            }
            active_script_context_epoch=script_context_epoch;
            script_character=std::move(new_character);
            script_character_pose=std::move(new_character_pose);
            script_character_renderable=false;
            script_character_dirty=true;
            if (new_animations) *script_animations=std::move(*new_animations);
            script_animation_samples.clear();
            script_animation_visuals.clear();
            script_runtime.swap(fresh);
            script_suspended=false;
            if (script_logger)
                script_logger->info("project.script",
                    "phase=hot_reload status=pass duration_us="+std::to_string(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            Clock::now()-reload_started).count()));
        } catch (std::exception const& e) {
            // Provider construction can fail before a new script epoch exists.
            // Preserve the previous active context on that failure path.
            if(script_context_epoch!=previous_context_epoch) {
                cleanup_script_spawns(script_context_epoch);
                if(auto it=script_binding_leases.find(script_context_epoch);
                   it!=script_binding_leases.end()) {
                    it->second->revoke();
                    script_binding_leases.erase(it);
                }
            }
            if (old_animations && script_animations)
                *script_animations=std::move(*old_animations);
            script_animation_visuals=std::move(old_visuals);
            script_character=old_character;
            script_character_pose=old_character_pose;
            script_character_dirty=old_character_dirty;
            script_character_renderable=old_character_renderable;
            // A failed edit must not destroy the currently running context.
            if(script_logger) {
                try {script_logger->error("project.script",
                    "phase=hot_reload status=reject duration_us="+std::to_string(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            Clock::now()-reload_started).count())+
                    " reason="+e.what());}catch(...){}
            }
            OutputDebugStringA(e.what());
            std::fprintf(stderr,"Project script reload rejected: %s\n",e.what());
        }
    }

    void stop_project_scripts() {
        if(preview && preview->supports_ai_motor())
            for(auto const& [pawn,npc]:script_ai_controllers)
                if(npc.motor_active)
                    try {preview->clear_ai_move_intent(pawn);}
                    catch(std::exception const&) {}
        for(auto const& [pawn,npc]:script_ai_controllers)
            if(npc.mesh_id)script_model_animations.erase(npc.mesh_id);
        script_ai_controllers.clear();
        script_audio_cache.clear();
        script_nac_cache.clear();
        script_voice_audio.reset();
        script_object_colors.clear();
        script_character.reset();
        script_character_pose.reset();
        script_character_renderable=false;
        script_character_dirty=false;
        if(graphics) {
            try { graphics->clear_character(); }
            catch(std::exception const& error) {
                std::fprintf(stderr,"Character cleanup failed: %s\n",error.what());
            }
        }
        script_paused=false;
        script_move_x=script_move_z=0.f;
        if (script_runtime) {
            try {
                notify_script_entities(*script_runtime,"entity.stop");
                script_runtime->emit_event("stop");
            } catch (std::exception const&) {}
        }
        cleanup_script_spawns(0);
        // Stop event has already completed; all callbacks now become stale.
        for(auto& [epoch,lease]:script_binding_leases) {
            (void)epoch;
            lease->revoke();
        }
        script_binding_leases.clear();
        ++script_scene_generation;
        active_script_context_epoch=0;
        script_runtime.reset();
        player_action_tracker_.reset(); // No stale airborne state across Play sessions.
        script_animations.reset();
        script_vehicle_rigs.clear();
        script_vehicle_occupant_seats.clear();
        script_vehicle_visual_indices.clear();
        script_vehicle_door_visual_indices.clear();
        script_vehicle_visual_followers.clear();
        script_vehicle_targets.clear();
        script_vehicle_native.clear();
        script_vehicle.reset();
        vehicle_before_scratch.clear();
        vehicle_after_scratch.clear();
        vehicle_draw_states_scratch.clear();
        vehicle_visual_scene_scratch=Scene{};
        script_animation_samples.clear();
        script_animation_visuals.clear();
        script_ecs.reset();
        script_assets.reset();
        script_entity_indices.clear();
        script_handle_ids.clear();
        script_file_stamps.clear();
        script_source_root.clear();
        script_entry_exists=false;
        script_suspended=false;
    }

    void load_project_input_actions() {
        script_input_actions_=gameplay::InputActionMap{};
        if(!script_assets)return;
        try {
            auto const bytes=script_assets->size("input/actions.xml");
            if(!bytes)return; // no authored map; use defaults
            if(bytes>65536)throw std::length_error("InputActions asset exceeds 64 KiB");
            auto const raw=script_assets->read("input/actions.xml");
            if(raw.size()!=bytes)throw std::runtime_error("Incomplete input binding asset");
            script_input_actions_=gameplay::parse_input_actions(std::string(
                reinterpret_cast<char const*>(raw.data()),raw.size()));
            std::fprintf(stderr,"[INFO] [engine.input] loaded input/actions.xml\n");
        } catch(std::runtime_error const& error) {
            // Absent input/actions.xml is normal for older projects.
            if(std::string(error.what()).find("Asset not found: input/actions.xml")!=
               std::string::npos)return;
            std::fprintf(stderr,"[WARN] [engine.input] Using default bindings: %s\n",
                error.what());
        } catch(std::exception const& error) {
            std::fprintf(stderr,"[WARN] [engine.input] Ignoring invalid bindings: %s\n",
                error.what());
        }
    }

    void save_project_input_actions() {
        if(config.packaged_game || !config.archive_path.empty() || file_path.empty())
            throw std::runtime_error("Input rebinding persistence requires a writable project");
        auto const root=script_project_root(file_path);
        const auto folder=root/"assets"/"input";
        const auto target=folder/"actions.xml";
        const auto tmp=folder/"actions.xml.tmp";
        if(fs::is_symlink(folder) || fs::is_symlink(target) || fs::is_symlink(tmp))
            throw std::runtime_error("Input action bindings cannot use symbolic links");
        fs::create_directories(folder);
        const auto data=gameplay::serialize_input_actions(script_input_actions_);
        if(data.size()>65536)throw std::length_error("InputActions XML exceeds 64 KiB");
        try {
            std::ofstream output(tmp,std::ios::binary|std::ios::trunc);
            if(!output)throw std::runtime_error("Cannot open InputActions temp file");
            output.write(data.data(),std::streamsize(data.size()));
            output.flush();
            if(!output)throw std::runtime_error("InputActions temp write failed");
            output.close();
#ifdef _WIN32
            if(!MoveFileExW(tmp.c_str(),target.c_str(),
                MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot atomically replace project input actions");
#else
            fs::rename(tmp,target);
#endif
        } catch(...) {
            std::error_code ignored;
            fs::remove(tmp,ignored);
            throw;
        }
    }

    void initialize_project_scripts() {
        if (!preview || file_path.empty()) return;
        // A new scene invalidates every previous ScriptRuntime lease, even if
        // initialization was requested without an explicit stop message.
        for(auto& [epoch,lease]:script_binding_leases) {
            (void)epoch;
            lease->revoke();
        }
        script_binding_leases.clear();
        bool const archived=!config.archive_path.empty();
        auto const base=archived ? fs::path{} : script_project_root(file_path);
        // The virtual path scripts/... belongs to assets/scripts/... on disk.
        // Older project/scripts directories remain loadable until migration.
        auto const canonical_scripts=base/"assets"/"scripts";
        auto const scripts_source=fs::is_directory(canonical_scripts)?
            base/"assets":base;
        script_assets=std::make_unique<AssetStore>();
        auto const common=discover_shared_assets(config,base);
        // Embedded engine API is available even when no external sharedAssets
        // directory is mounted (standalone exported games).
        if (archived) {
            script_assets->mount_runtime_archives();
            if(!common.empty())script_assets->mount_shared_assets(common);
            script_assets->mount_archive("",config.archive_path,
                config.archive_data.empty() ? config.archive_path : config.archive_data);
            // Missing optional main.js is not a failure; attached entity scripts
            // are still allowed to start without a global entrypoint.
            try {
                auto const bytes=script_assets->size("scripts/main.js");
                if (bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                    throw std::length_error("Archived main.js exceeds 1 MiB");
                script_entry_exists=true;
            } catch (std::runtime_error const&) {
                script_entry_exists=false;
            }
        } else {
            auto const entry=scripts_source/"scripts"/"main.js";
            script_entry_exists=fs::is_regular_file(entry);
            if (fs::is_symlink(scripts_source/"scripts") ||
                (script_entry_exists && fs::is_symlink(entry)))
                throw std::runtime_error("Project script paths may not be symbolic links");
            script_assets->mount_project_assets(base,common);
            if (!fps_assets && fs::is_directory(base/"assets")) {
                fps_assets=std::make_unique<AssetManager>();
                fps_assets->mount_project_assets(base,common);
            }
        }
        load_project_input_actions();
        auto const& scene=preview->scene();
        bool attached=false;
        for (auto const& object:scene.objects)
            if (!object.node.script.empty()) attached=true;
        if (!script_entry_exists && !attached && !fps_camera) {
            // A scene without any game camera or attached scripts does not
            // need a persistent V8 instance. All camera-enabled games still
            // receive the engine default root script.
            script_assets.reset();
            return;
        }
        script_animations=std::make_unique<SceneAnimationPlayer>();
        script_vehicle=std::make_unique<VehicleService>();
        std::fprintf(stderr,"[INFO] [engine.vehicle] selected=%s source=%s\n",
            script_vehicle->provider_name().c_str(),script_vehicle->provider_source().c_str());
        // Even if initialize is called without an explicit stop, a new
        // scene cannot inherit stale ECS IDs from the previous scene.
        ++script_scene_generation;
        script_entity_indices.clear();
        script_handle_ids.clear();
        script_ecs=std::make_unique<EcsWorld>();
        script_ecs->load(scene);
        next_script_spawn_id=0;
        script_spawn_handles.clear();
        script_spawn_owners.clear();
        script_model_visual_modes.clear();
        for (std::size_t i=0;i<scene.objects.size();++i)
            if (scene.objects[i].node.id) {
                auto stable=scene.objects[i].node.id;
                script_entity_indices.emplace(stable,i);
                script_handle_ids.emplace(script_ecs->at(i),stable);
            }
        if (!archived) {
            script_source_root=scripts_source;
            auto const entry=scripts_source/"scripts"/"main.js";
            script_file_stamps.emplace(fs::path("scripts/main.js"),
                script_entry_exists ? fs::last_write_time(entry) : fs::file_time_type::min());
            // Track ALL project root script overrides, including file creation
            // and deletion. The hot-reload swap keeps the current context when
            // a malformed author script fails to initialize.
            static constexpr std::array<std::string_view,26> roots={{
                "input/Keys.js", "input/GamepadButtons.js",
                "scene/Entity.js", "scene/Prefab.js",
                "ui/UI.js", "UI.js",
                "character/Character.js", "Character.js",
                "player/playerConfig.js", "playerConfig.js",
                "player/playerActor.js", "playerActor.js",
                "player/playerSession.js", "playerSession.js",
                "camera/playerCamera.js", "playerCamera.js",
                "camera/playerLook.js", "playerLook.js",
                "player/playerMovement.js", "playerMovement.js",
                "character/footsteps.js", "playerFootsteps.js",
                "character/playerCharacter.js", "playerCharacter.js",
                "player/playerInteraction.js", "playerInteraction.js"
            }};
            for(auto name:roots) {
                auto const path=fs::path("scripts")/fs::u8path(name);
                auto const absolute=scripts_source/path;
                if(fs::is_symlink(absolute))
                    throw std::runtime_error("Project root script override cannot be a symbolic link");
                script_file_stamps.emplace(path,fs::is_regular_file(absolute) ?
                    fs::last_write_time(absolute):fs::file_time_type::min());
            }
        }
        for (auto const& object:scene.objects)
            if (!object.node.script.empty()) {
                auto const rel=fs::u8path(object.node.script);
                validate_script_virtual_path(rel);
                if (archived) {
                    auto const bytes=script_assets->size(object.node.script);
                    if (bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                        throw std::length_error("Archived entity script exceeds 1 MiB");
                } else {
                    auto const actual=validated_script_file(scripts_source,rel);
                    script_file_stamps.emplace(rel,fs::last_write_time(actual));
                }
            }
        try {
            auto const logging=application_logger_configuration("NorthStarCpp-ProjectScripts");
            script_logging_allowed=logging.enabled;
            if (logging.enabled)
                script_logger=std::make_unique<Logger>(logging.options);
        } catch(std::exception const& e) { OutputDebugStringA(e.what()); }
        ScriptingOptions scriptOptions;
        if(auto const* timeout=std::getenv("NORTHSTARCPP_QA_SCRIPT_TIMEOUT_MS")) {
            auto const value=std::strtoul(timeout,nullptr,10);
            if(value>=1000ul && value<=60000ul)
                scriptOptions.timeout_ms=static_cast<std::uint32_t>(value);
        }
        auto fresh=std::make_unique<ScriptRuntime>(scriptOptions);
        configure_script_bindings(*fresh);
        load_script_files(*fresh);
        fresh->emit_event("start");
        notify_script_entities(*fresh,"entity.start");
        script_runtime=std::move(fresh);
        active_script_context_epoch=script_context_epoch;
    }

    void advance_scene_animations(float dt) {
        if (!script_animations || !preview) return;
        if (!script_paused && !script_suspended) {
            auto const delta=std::clamp(dt,0.f,0.25f);
            script_animations->advance_into(delta,script_animation_samples);
            for (auto const& sample:script_animation_samples) {
                if (sample.scene_index>=preview->scene().objects.size())
                    throw std::runtime_error("Animation references stale scene object");
                // Timeline continues offscreen, but no physical relocation or
                // render override is submitted for a partition-unloaded cell.
                if (scene_streaming.world() && !scene_streaming.world()->object_resident(sample.scene_index))
                    continue;
                auto const& obj=preview->scene().objects[sample.scene_index];
                if (obj.node.id!=sample.entity_id || obj.mass!=0.f || scene_pawn::is_pawn(preview->scene(),obj))
                    throw std::runtime_error("Animated scene object topology changed");
                if (std::abs(obj.position.x-sample.position.x)>0.00001f ||
                    std::abs(obj.position.y-sample.position.y)>0.00001f ||
                    std::abs(obj.position.z-sample.position.z)>0.00001f) {
                    preview->move_static(sample.scene_index,sample.position);
                    if (script_ecs && sample.scene_index<script_ecs->size()) {
                        NsPosition next{sample.position.x,sample.position.y,sample.position.z};
                        script_ecs->set_component(script_ecs->at(sample.scene_index),
                                                  NS_COMPONENT_POSITION,&next,sizeof(next));
                    }
                }
                // Visual rotation and scale do not change collision shapes until
                // the physics-provider transform/shape ABI is extended.
                script_animation_visuals.insert_or_assign(sample.scene_index,sample);
            }
        }
    }

    void apply_scene_animation_visuals(Scene& visual) const {
        for (auto const& entry:script_animation_visuals) {
            auto const index=entry.first;
            if (index>=visual.objects.size()) continue;
            auto& object=visual.objects[index];
            object.node.rotation=entry.second.rotation;
            object.node.scale=entry.second.scale;
        }
    }
