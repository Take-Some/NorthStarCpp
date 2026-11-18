// NorthStarCpp window_runtime_frame_loop.inc -- included in class App; do not compile separately.
    // Notify gameplay scripts only after controller/physics state is committed.
    void publish_player_controller_events() {
        if(!preview)return;
        while(auto e=preview->poll_controller_event()) {
            if(!script_runtime || script_suspended)continue;
            auto name=e->kind==gameplay::PossessionEventKind::OnPossess?
                "player.OnPossess":"player.OnUnPossess";
            auto payload=std::string("{\"pawn\":")+
                script_json_quote(std::to_string(e->pawn))+
                ",\"otherPawn\":"+script_json_quote(std::to_string(e->other_pawn))+
                ",\"revision\":"+std::to_string(e->revision)+"}";
            try {script_runtime->emit_event(name,payload);}
            catch(std::exception const& ex) {
                std::fprintf(stderr,"[WARN] [engine.player] %s: %s\n",name,ex.what());
            }
        }
        auto const lost=preview->dropped_controller_events();
        if(lost>reported_controller_event_overruns_) {
            reported_controller_event_overruns_=lost;
            std::fprintf(stderr,
                "[WARN] [engine.player] Controller events overwritten: %llu\n",
                static_cast<unsigned long long>(lost));
        }
    }

    void publish_player_motion_events(float dt) {
        if(!preview ||dt<=0.f) {
            if(!preview)player_action_tracker_.reset();
            return;
        }
        auto const index=preview->controlled_index();
        if(!index || *index>=preview->scene().objects.size()) {
            player_action_tracker_.reset();
            return;
        }
        auto const& pos=preview->scene().objects[*index].position;
        bool grounded=false;
        if(auto const animation=preview->player_animation())
            grounded=animation->grounded;
        else {
            auto const hit=preview->sweep_sphere(
                {pos.x,pos.y+.22f,pos.z},
                {pos.x,pos.y-.35f,pos.z},.10f,*index);
            grounded=(hit && hit->normal.y>=.55f)||
                (pos.y>=-.10f && pos.y<=.13f);
        }
        auto transitions=player_action_tracker_.step(
            preview->controlled_entity(),pos.x,pos.y,pos.z,grounded,dt);
        if(!script_runtime ||script_suspended)return;
        for(auto const& event:transitions) {
            auto const& m=event.motion;
            auto payload=std::string("{\"pawn\":")+
                script_json_quote(std::to_string(m.pawn))+
                ",\"state\":"+script_json_quote(m.state)+
                ",\"previous\":"+script_json_quote(event.previous_state)+
                ",\"horizontalSpeed\":"+std::to_string(m.horizontal_speed)+
                ",\"verticalSpeed\":"+std::to_string(m.vertical_speed)+
                ",\"fallDistance\":"+std::to_string(m.fall_distance)+
                ",\"impactSpeed\":"+std::to_string(m.impact_speed)+
                ",\"grounded\":"+(m.grounded?"true":"false")+
                ",\"moving\":"+(m.moving?"true":"false")+
                ",\"action\":"+script_json_quote(std::string(event.name+7))+"}";
            try {
                script_runtime->emit_event(event.name,payload);
                // One catch-all channel for modular effects, audio and UI.
                script_runtime->emit_event("player.action",payload);
            }catch(std::exception const& err) {
                std::fprintf(stderr,
                    "[WARN] [engine.player] Event %s callback failed: %s\n",
                    event.name,err.what());
            }
        }
    }

    void frame()
    {
        // The sampled frame interval is always raw. Simulation is still
        // clamped for physics stability, independently from profiling.
        // Deterministic QA stop after exactly the requested completed frames.
        // Disabled unless explicitly requested; cannot end normal gameplay.
        static bool const profile_autostop = [] {
            auto const* value = std::getenv("NORTHSTARCPP_PROFILE_AUTOSTOP");
            return value && value[0] == '1';
        }();
        if (profile_autostop && frame_profiler && !frame_profiler->active()) {
            PostQuitMessage(0);
            return;
        }
        auto const raw_seconds = frame_clock.restart_seconds();
        diagnostics::FrameProfileScope frame_profile(frame_profiler.get(), raw_seconds);
        // GPU timing can synchronize the driver. Keep it OFF in production
        // and in ordinary CPU profiling runs to avoid distorting frame time.
        auto sample_gpu_frame = [&] {
            static bool const gpu_profile = [] {
                auto const* value = std::getenv("NORTHSTARCPP_PROFILE_GPU_TIMESTAMPS");
                return value && value[0] == '1';
            }();
            if (!gpu_profile || !frame_profiler || !frame_profiler->active() ||
                !graphics) return;
            if (auto const gpu = graphics->gpu_frame_timing())
                frame_profile.record_gpu_timing(gpu->total_gpu_ms);
        };
        // The scheduler owns frame clamping and the PIE single-step policy.
        auto const slice=runtime_frame::advance(runtime_state_.pie_stepper,
            static_cast<float>(raw_seconds),
            fps != nullptr || scripted_first_person,runtime_state_.pie_paused,script_paused);
        float seconds=slice.seconds;
        if(script_weapon_fx)script_weapon_fx->advance(seconds);

        // Keep rendering, message dispatch and async asset streaming alive while
        // freezing simulation time. Restarting FrameClock each render prevents a
        // catch-up physics step on Resume.

        bool const simulation_paused=slice.paused;

        auto const state = input->poll();
        script_mouse_dx=state.mouse_dx;
        script_mouse_dy=state.mouse_dy;
        script_wheel_vertical=state.focused ? state.wheel_vertical : 0;
        std::copy_n(std::begin(state.mouse_buttons),script_mouse_buttons.size(),
                    script_mouse_buttons.begin());
        std::copy_n(std::begin(state.gamepads),script_gamepads.size(),
                    script_gamepads.begin());
        // In Play In Editor the Qt Editor, not its embedded Player HWND,
        // is the foreground top-level window. Never discard valid relative
        // mouse input just because the Player is embedded.
        if (scripted_first_person && (!game_has_foreground() ||
            !state.focused || (!fps_captured && !editor))) {
            script_mouse_dx=script_mouse_dy=0.f;
        }
        if (scripted_game) {
            script_move_x=script_move_z=0.f; // no stale movement from prior frames
        }
        frame_profile.measure(diagnostics::FrameStage::scripting, [&] {
        // Drain provider events on the game thread even if no script runtime
        // exists. This is the native fallback consumer for FPS pause .nui.
        auto ui_events=graphics?graphics->take_ui_events():
            std::vector<UiInteractionEvent>{};
        if(fps && !config.packaged_game &&
           config.archive_path.empty() && !fps_ui_source_.empty() &&
           Clock::now()-fps_ui_probe_last_>=std::chrono::milliseconds(500)) {
            fps_ui_probe_last_=Clock::now();
            std::error_code io_error;
            auto next=fs::last_write_time(fps_ui_source_,io_error);
            if(!io_error && next!=fps_ui_stamp_) {
                try {
                    reload_fps_menu_asset();
                    fps_ui_stamp_=next;
                    std::fprintf(stderr,
                        "[INFO] [engine.ui] Reloaded %s\\n",
                        fps_ui_source_.u8string().c_str());
                }catch(std::exception const& err) {
                    // Keep the last valid document. Never fail a running game
                    // because an in-progress authoring save was malformed.
                    std::fprintf(stderr,
                        "[WARN] [engine.ui] Invalid FPS .nui: %s\\n",err.what());
                }
            }
        }
        if(fps && fps_menu_visible) {
            for(auto const& event:ui_events) {
                if(event.kind==NORTHSTAR_UI_EVENT_CLICK &&
                   event.name=="click" && handle_fps_ui_action(event.id))
                    break;
            }
        }
        if (script_runtime) {
            // Never call into V8 from an external UI provider callback.
            // Drain the provider's bounded event batch on the game thread
            // before the regular update event.
            if(graphics && !script_suspended) try {
                auto& payload=ui_event_payload_scratch;
                for(auto const& event:ui_events) {
                    if(event.id.size()>96 || event.name.size()>64)continue;
                    payload.clear();
                    // The per-App buffer retains capacity across UI events and
                    // frames, avoiding a chain of short-lived concatenations.
                    payload.reserve(96+event.id.size()+event.name.size()+event.text.size());
                    payload.append("{\"id\":");
                    payload.append(script_json_quote(event.id));
                    payload.append(",\"name\":");
                    payload.append(script_json_quote(event.name));
                    payload.append(",\"kind\":");
                    payload.append(std::to_string(event.kind));
                    payload.append(",\"value\":");
                    switch(event.value_kind) {
                    case NORTHSTAR_UI_VALUE_BOOL:
                        payload.append(event.boolean?"true":"false");break;
                    case NORTHSTAR_UI_VALUE_NUMBER:
                        if(std::isfinite(event.number))
                            payload.append(std::to_string(event.number));
                        else payload.append("null");
                        break;
                    case NORTHSTAR_UI_VALUE_STRING:
                        payload.append(script_json_quote(event.text));
                        break;
                    default:payload.append("null");break;
                    }
                    payload.push_back('}');
                    script_runtime->emit_event("ui.event",payload);
                }
            } catch(std::exception const& error) {
                std::fprintf(stderr,
                    "[WARN] [engine.ui] UI dispatch error: %s\n",error.what());
                script_suspended=true;
            }
            std::copy_n(std::begin(state.keys),script_keys.size(),script_keys.begin());
            auto const scan_now=Clock::now();
            if (!config.packaged_game &&
                scan_now-script_probe_last>=std::chrono::milliseconds(500)) {
                script_probe_last=scan_now;
                if (scripts_changed()) reload_project_scripts();
            }
            // UI activation is independent of physics pause. Keep it ticking
            // so a short press/release effect finishes before project callbacks
            // navigate away. Use RAW render delta: simulation delta is zero
            // whenever the player or title scene is paused.
            if (!script_suspended) try {
                auto const ui_dt=std::clamp(
                    static_cast<double>(raw_seconds),0.0,0.10);
                script_runtime->emit_event("ui.update",std::to_string(ui_dt));
            } catch(const std::exception& e) {
                std::fprintf(stderr,
                    "[WARN] Project UI tick failed: %s\n",e.what());
                script_suspended=true;
            }
            if (!script_suspended && !simulation_paused) try {
                // PhysicsScene is the live transform source. Query/has only use
                // ECS component presence, so avoid O(N) ECS writes every frame.
                script_runtime->emit_event("update",std::to_string(seconds));
                notify_script_entities(*script_runtime,"entity.update",seconds);
            } catch (std::exception const& e) {
                OutputDebugStringA(e.what());
                std::fprintf(stderr,"Project script update failed: %s\n",e.what());
                script_suspended=true;
            }
        }
        });
        // One engine-owned sky clock advances only with gameplay simulation.
        // The public state updates every frame; GPU texture reuploads are
        // rate-limited because the current native Sky ABI owns a texture.
        if(sky_environment_.settings().enabled &&
           !simulation_paused && !script_paused) {
            const auto sky_dt=std::clamp(double(seconds),0.,.25);
            sky_environment_.advance(sky_dt);
            sky_visual_elapsed_+=sky_dt;
            if(sky_visual_elapsed_>=5.) {
                sky_visual_elapsed_=0.;
                sky_visual_dirty_=true;
            }
        }
        // GPU Sky providers receive a fixed-size uniform every frame;
        // CPU fallback still uses the coarse 5-second texture refresh.
        if(graphics && (sky_visual_dirty_ ||
           (sky_environment_.settings().enabled && graphics->supports_gpu_sky()))) {
            sky_visual_dirty_=false;
            try {
                graphics->set_sky_environment(sky_environment_);
            } catch(std::exception const& error) {
                std::fprintf(stderr,"[WARN] [engine.sky] Sky presentation: %s\n",
                    error.what());
            }
        }
        frame_profile.measure(diagnostics::FrameStage::animation, [&] {
        if (script_animations && !script_suspended && !simulation_paused) {
            try { advance_scene_animations(seconds); }
            catch(std::exception const& error) {
                OutputDebugStringA(error.what());
                std::fprintf(stderr,"Project animation update failed: %s\n",error.what());
                script_suspended=true;
            }
        }
        });
        auto const down = [&state](unsigned code) {
            return code < NORTHSTARCPP_INPUT_MAX_KEYS && state.keys[code] != 0;
        };
        auto const action_down = [this,&state](std::string_view action) {
            return script_input_actions_.action_down(
                action,state.keys,NORTHSTARCPP_INPUT_MAX_KEYS,
                script_paused || fps_menu_visible || runtime_state_.pie_ejected);
        };
        bool const foreground = game_has_foreground() && state.focused;
        float x = 0.f, z = 0.f;
        if (fps) {
            bool const pause_key = (fps_menu_visible?action_down("Cancel"):
                action_down("Pause")) ||
                std::any_of(std::begin(state.gamepads),std::end(state.gamepads),
                    [](auto const& pad) {
                        return pad.connected && (pad.buttons & NORTHSTAR_INPUT_PAD_START);
                    });
            if (pause_key && !fps_previous_menu && foreground &&
                (!fps_menu_visible || (fps->alive() && !fps->won())))
                show_fps_menu(!fps_menu_visible);
            fps_previous_menu = pause_key;
        }

        if (preview && foreground && !scripted_game &&
            !simulation_paused && !runtime_state_.pie_ejected &&
            (!fps || !fps_menu_visible)) {
            x = float(int(action_down("MoveRight"))-
                      int(action_down("MoveLeft")));
            z = float(int(action_down("MoveForward"))-
                      int(action_down("MoveBackward")));
            float look_x = 0.f, look_y = 0.f;
            bool trigger = false, pad_jump = false, pad_reload = false;
            bool stop = !fps && down(NORTHSTAR_INPUT_KEY_ESCAPE);
            for (auto const& pad : state.gamepads) {
                if (!pad.connected) continue;
                stop = stop || (!fps && (pad.buttons & NORTHSTAR_INPUT_PAD_BACK) != 0);
                x = std::clamp(x + pad.left_x +
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_RIGHT)) -
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_LEFT)), -1.f, 1.f);
                z = std::clamp(z + pad.left_y +
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_UP)) -
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_DOWN)), -1.f, 1.f);
                look_x = pad.right_x;
                look_y = pad.right_y;
                trigger = pad.right_trigger > 0.35f;
                pad_jump = !!(pad.buttons & NORTHSTAR_INPUT_PAD_A);
                pad_reload = !!(pad.buttons & NORTHSTAR_INPUT_PAD_X);
                break;
            }
            if (stop) {
                if (editor) command(play_scene, BN_CLICKED);
                else { DestroyWindow(window); return; }
                x = z = 0.f;
            }
            if (fps && fps_captured && fps->alive()) {
                fps->look(float(state.mouse_dx),float(state.mouse_dy),
                          look_x,look_y,seconds);
                bool const jumping = action_down("Jump") || pad_jump;
                if (jumping && !fps_previous_jump)
                    preview->jump();
                fps_previous_jump = jumping;
                if (config.explore) {
                    bool const use=action_down("Interact") || pad_reload;
                    if(use && !explore_previous_use) {
                        auto const origin=fps->eye(preview->scene());
                        auto const direction=fps->direction();
                        auto const end=Vector3{origin.x+direction.x*3.6f,
                                               origin.y+direction.y*3.6f,
                                               origin.z+direction.z*3.6f};
                        auto hit=preview->sweep_sphere(origin,end,0.28f,fps->player_index());
                        if(hit && hit->object_index!=UINT32_MAX &&
                           hit->object_index < preview->scene().objects.size()) {
                            auto const& body=preview->scene().objects[hit->object_index];
                            if(body.mass>0.f && !scene_pawn::is_pawn(preview->scene(),body))
                                preview->apply_impulse(hit->object_index,
                                    {direction.x*9.f,direction.y*9.f+1.2f,direction.z*9.f});
                        }
                    }
                    explore_previous_use=use;
                }
                bool const reload = !config.explore && (down('R') || pad_reload);
                if (reload && !fps_previous_reload && fps->begin_reload())
                    play_fps_clip(fps_reload);
                fps_previous_reload = reload;
            }
        }
        if (!preview) {
            if (editor && camera_looking) {
                float const speed=std::clamp(seconds,0.f,.05f)*camera_move_speed*
                    ((GetKeyState(VK_SHIFT)&0x8000)?4.f:1.f);
                auto down_key=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};
                float const sideways=float(down_key('D'))-float(down_key('A'));
                float const vertical=float(down_key('E'))-float(down_key('Q'));
                float const forward=float(down_key('W'))-float(down_key('S'));
                editor_camera.move(sideways*speed,vertical*speed,forward*speed);
            }
            std::optional<Scene> authored;
            if(scene_streaming.world() || !gizmo_scene)authored.emplace(document.snapshot());
            frame_profile.measure(diagnostics::FrameStage::scene_streaming,[&] {
                if(authored)update_scene_world(*authored,seconds);
                update_procedural_terrain(seconds);
            });
            auto const& active_scene=gizmo_scene?*gizmo_scene:*authored;
            auto const overlay=editor ? std::optional<ViewportGizmo>(active_gizmo(active_scene)) :
                std::optional<ViewportGizmo>{};
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(active_scene, selected_index(), false,
                           nullptr,nullptr,false,editor ? &editor_camera.pose : nullptr,
                           (preview && terrain_config_enabled &&
                            terrain_follow_entity=="CITYROOT Bastion"
                            ?nullptr:scene_streaming.world()),true,show_streaming_overlay,
                           overlay ? &*overlay : nullptr,nullptr,
                           procedural_terrain_render_chunks(),terrain_material); });
            sample_gpu_frame();
            return;
        }
        publish_player_controller_events();
        // Terrain streaming must precede Jolt stepping. Do this on the
        // simulation owner thread; renderer consumes the resulting cache.
        if(!simulation_paused && !script_paused)
            frame_profile.measure(diagnostics::FrameStage::scene_streaming,[&] {
                update_procedural_terrain(seconds);
            });
        if (scripted_first_person) {
            if (!simulation_paused && !script_suspended)
                frame_profile.measure(diagnostics::FrameStage::physics, [&] {
                    update_ai_motors(seconds);
                    preview->step(seconds,script_move_x,script_move_z);
                    update_ai_animations(seconds);
                    publish_player_motion_events(seconds);
                    advance_vehicles(seconds);
                    // Same-frame vehicle passenger sync: once Jolt has moved
                    // CityRoot, refresh scene parts from the *new* pose
                    // BEFORE sampling the camera and building render geometry.
                    // A zero-delta event changes no game time or input edges.
                    if(terrain_config_enabled &&
                       terrain_follow_entity=="CITYROOT Bastion" &&
                       script_runtime)
                        script_runtime->emit_event("update","0");
                });
            update_script_camera(script_paused ? 0.f : seconds);
            update_cinematic_postfx();
            vehicle_visual_scene_scratch=preview->scene();
            Scene& visual=vehicle_visual_scene_scratch;
            frame_profile.measure(diagnostics::FrameStage::animation,[&] {
                apply_vehicle_visuals(visual);
                sync_vehicle_seat_ik(visual);
            });
            for (auto const& [index,color]:script_object_colors)
                if (index<visual.objects.size()) visual.objects[index].color=color;
            apply_ai_visual_facing(visual);
            apply_scene_animation_visuals(visual);
            for(auto const& [id,payload]:script_model_visual_modes)
                visual.components.push_back(
                    {id,"engine.model.visual_mode",1,payload});
            for(auto const& [id,ref]:script_model_animations) {
                auto found=std::find_if(visual.components.begin(),
                    visual.components.end(),[&](auto const& component) {
                        return component.entity_id==id &&
                            component.type=="engine.model.animation" &&
                            component.version==1;
                    });
                if(found!=visual.components.end())found->payload=ref;
                else visual.components.push_back(
                    {id,"engine.model.animation",1,ref});
            }
            for(auto const& [id,payload]:script_model_bone_attachments)
                visual.components.push_back({id,"engine.model.bone_attachment",1,payload});
            frame_profile.measure(diagnostics::FrameStage::scene_streaming, [&] { update_scene_world(seconds); });
            apply_script_character(visual,script_paused ? 0.f : seconds);
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(visual,-2,false,preview.get(),nullptr,script_paused,
                           fps_camera ? &fps_pose : nullptr,(preview && terrain_config_enabled &&
                            terrain_follow_entity=="CITYROOT Bastion"
                            ?nullptr:scene_streaming.world()),false,
                           false,nullptr,script_weapon_fx.get(),
                           procedural_terrain_render_chunks(),terrain_material); });
        } else if (fps) {
            if (!fps_menu_visible && !simulation_paused && !runtime_state_.pie_ejected &&
                foreground && fps->alive()) {
                fps->advance(seconds,preview->scene());
                if(fps_weapon_fx)fps_weapon_fx->advance(seconds);
                for (auto const index : fps->moving_indices())
                    preview->move_static(index, fps->moving_position(index));
                auto const m = fps->movement(x,z);
                frame_profile.measure(diagnostics::FrameStage::physics, [&] { preview->step(seconds,m.x,m.z); });
                publish_player_motion_events(seconds);
                if (!config.explore && (state.mouse_buttons[0] ||
                    std::any_of(std::begin(state.gamepads),std::end(state.gamepads),
                        [](auto const& p) {return p.connected && p.right_trigger > 0.35f;})))
                {
                    auto const before = fps->shots();
                    auto const shot_eye=fps->eye(preview->scene());
                    auto const shot_aim=fps->direction();
                    fps->fire(preview->scene());
                    if (fps->shots() != before) {
                        if(fps_weapon_fx) {
                            // Fire() consumes ammunition even when the ray
                            // misses a target; particles never spawn brass here.
                            fps_weapon_fx->emit_shot(
                                shot_eye,shot_aim);
                        }
                        if(fps_camera)fps_camera->shake(
                            state.mouse_buttons[1]?.012f:.022f,
                            state.mouse_buttons[1]?.11f:.16f);
                        play_fps_clip(fps_shot);
                    }
                }
                if (!fps->alive() || fps->won()) show_fps_menu(true);
            }
            update_fps_camera(fps_menu_visible ? 0.f : seconds,
                              foreground && !fps_menu_visible && state.mouse_buttons[1]);
            update_cinematic_postfx();
            Scene visual = preview->scene();
            fps->tint(visual);
            frame_profile.measure(diagnostics::FrameStage::scene_streaming, [&] { update_scene_world(seconds); });
            apply_script_character(visual,fps_menu_visible ? 0.f : seconds);
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(visual,-2,false,preview.get(),fps.get(),fps_menu_visible,
                           fps_camera ? &fps_pose : nullptr,(preview && terrain_config_enabled &&
                            terrain_follow_entity=="CITYROOT Bastion"
                            ?nullptr:scene_streaming.world()),
                           !config.explore,false,nullptr,
                           scripted_game ? script_weapon_fx.get() : fps_weapon_fx.get(),
                           procedural_terrain_render_chunks(),terrain_material); });
            update_fps_title();
        } else {
            if(!simulation_paused)
                frame_profile.measure(diagnostics::FrameStage::physics, [&] {
                    update_ai_motors(seconds);
                    preview->step(seconds,scripted_game ? script_move_x : x,
                                  scripted_game ? script_move_z : z);
                    update_ai_animations(seconds);
                });
            if(!simulation_paused && !script_suspended)
                publish_player_motion_events(seconds);
            if(!simulation_paused && !script_suspended)
                frame_profile.measure(diagnostics::FrameStage::physics,[&] {
                    advance_vehicles(seconds);
                });
            frame_profile.measure(diagnostics::FrameStage::scene_streaming, [&] { update_scene_world(seconds); });
            vehicle_visual_scene_scratch=preview->scene();
            Scene& visual=vehicle_visual_scene_scratch;
            frame_profile.measure(diagnostics::FrameStage::animation,[&] {
                apply_vehicle_visuals(visual);
                sync_vehicle_seat_ik(visual);
            });
            for (auto const& [index,color]:script_object_colors)
                if (index<visual.objects.size()) visual.objects[index].color=color;
            apply_ai_visual_facing(visual);
            apply_scene_animation_visuals(visual);
            apply_script_character(visual,seconds);
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(visual,-2,false,preview.get(),
                           nullptr,false,nullptr,(preview && terrain_config_enabled &&
                            terrain_follow_entity=="CITYROOT Bastion"
                            ?nullptr:scene_streaming.world()),
                           true,show_streaming_overlay && editor,
                           nullptr, script_weapon_fx.get(),
                           procedural_terrain_render_chunks(),terrain_material); });
        }
        sample_gpu_frame();
        frame_profile.measure(diagnostics::FrameStage::asset_streaming, [&] { update_asset_zones(); });
    }
