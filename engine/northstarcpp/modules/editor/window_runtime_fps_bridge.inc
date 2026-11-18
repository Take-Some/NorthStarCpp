    void update_fps_title()
    {
        if (!fps) return;
        if (config.explore) {
            SetWindowTextW(window,
                fps_menu_visible ? L"NorthStar First Person Platform | PAUSED"
                : L"NorthStar First Person Platform | WASD Move | Mouse Look | Space Jump | E Push");
            return;
        }
        std::wstring title = L"NorthStar FPS Arena | " +
            std::to_wstring(fps->hits()) + L"/" + std::to_wstring(fps->targets()) +
            L" hits | HP " + std::to_wstring(fps->health()) +
            L" | Ammo " + std::to_wstring(fps->ammunition()) + L"/" +
            std::to_wstring(fps->reserve_ammunition()) +
            (fps->reloading() ? L" | RELOADING" : L"") +
            (fps_menu_visible ? (fps->alive() ? L" | PAUSED" : L" | GAME OVER") : L"");
        SetWindowTextW(window, title.c_str());
    }

    // Standalone FPS UI follows the same VFS override hierarchy as scripted
    // games. No Win32 child buttons or parallel UI rasterizer are created.
    void create_fps_menu()
    {
        fps_ui_assets_=std::make_unique<AssetStore>();
        fps_ui_assets_->mount_runtime_archives();
        auto const root=file_path.empty()?fs::path{}:script_project_root(file_path);
        auto const common=discover_shared_assets(config,root);
        if(!root.empty() && config.archive_path.empty())
            fps_ui_assets_->mount_project_assets(root,common);
        else if(!common.empty())
            fps_ui_assets_->mount_shared_assets(common);
        if(!config.archive_path.empty())
            fps_ui_assets_->mount_archive("",config.archive_path,
                config.archive_data.empty()?config.archive_path:config.archive_data);
        reload_fps_menu_asset();
        if(config.archive_path.empty() && !root.empty()) {
            auto const override=root/"assets"/"ui"/"fps_pause.nui";
            auto const shared=common/"ui"/"fps_pause.nui";
            fps_ui_source_=fs::is_regular_file(override)?override:shared;
            std::error_code ec;
            fps_ui_stamp_=fs::last_write_time(fps_ui_source_,ec);
            if(ec)fps_ui_stamp_=fs::file_time_type::min();
        }
    }

    void reload_fps_menu_asset() {
        if(!fps_ui_assets_)throw std::runtime_error("FPS UI VFS not initialized");
        const auto size=fps_ui_assets_->size("ui/fps_pause.nui");
        if(!size || size>262144)
            throw std::length_error("FPS .nui must be 1..262144 bytes");
        auto bytes=fps_ui_assets_->read("ui/fps_pause.nui");
        if(bytes.size()!=size)
            throw std::runtime_error("Incomplete FPS .nui read");
        RECT client{};
        if(window)GetClientRect(window,&client);
        auto width=static_cast<unsigned>(std::max<LONG>(0,client.right-client.left));
        auto candidate=fps_nui::decode(
            std::string_view(reinterpret_cast<char const*>(bytes.data()),bytes.size()),
            "fpspause",width);
        fps_ui_document_=std::move(candidate);
        if(fps_menu_visible && graphics)
            set_fps_menu_document();
    }

    void set_fps_menu_document() {
        if(!graphics)return;
        if(!fps_menu_visible) {
            graphics->set_ui_document(UiDocument{});
            return;
        }
        auto live=fps_ui_document_;
        fps_nui::text(live,"fpspause.caption",
            !fps->alive() ? "GAME OVER / RESTART" :
            fps->won() ? "ARENA COMPLETE / RESTART" :
            config.explore ? "FIRST PERSON PLATFORM / PAUSED" :
                             "NORTHSTAR FPS / PAUSED");
        // All visible elements originate from the versioned .nui document.
        graphics->set_ui_document(std::move(live));
    }

    bool handle_fps_ui_action(std::string_view event_id) {
        if(!fps || !fps_menu_visible)return false;
        if(event_id=="fpspause.resume") {
            if(fps->alive() && !fps->won())show_fps_menu(false);
        } else if(event_id=="fpspause.restart") {
            restart_fps();
        } else if(event_id=="fpspause.exit") {
            // Dispatch after the present frame, never destroy the HWND while
            // Viewport::take_ui_events() is being iterated.
            PostMessageW(window,WM_CLOSE,0,0);
        } else return false;
        return true;
    }

    void show_fps_menu(bool show)
    {
        if(!fps || fps_menu_visible==show)return;
        fps_menu_visible=show;
        if(show)release_fps_cursor();
        set_fps_menu_document();
        if(GetForegroundWindow()==window) {
            SetFocus(window);
            if(!show)capture_fps_cursor();
        }
        frame_clock.start();
        update_fps_title();
    }

    void apply_script_character(Scene const& scene, float dt)
    {
        if(!graphics) return;
        if(script_character_dirty) {
            script_character_renderable=false;
            if(script_character) {
                try {
                    if(!script_assets)
                        throw std::runtime_error("No mounted character asset store");
                    graphics->load_character(*script_assets,*script_character);
                    script_character_renderable=true;
                } catch(std::exception const& e) {
                    std::fprintf(stderr,
                        "[WARN] [engine.assets] Skipping invalid scripted character: %s\n",
                        e.what());
                    // Invalid character visuals must not stop scene updates.
                    // Keep the descriptor for script pose() validation.
                    script_character_pose.reset();
                    graphics->clear_character();
                }
            } else {
                graphics->clear_character();
            }
            script_character_dirty=false;
        }
        if(!script_character || !script_character_renderable) return;
        if(!script_character_pose) {
            graphics->hide_character();
            return;
        }
        auto const& pose=*script_character_pose;
        if(pose.scene_index>=scene.objects.size()) {
            std::fprintf(stderr,
                "[WARN] [engine.assets] Skipping stale character pose index %zu\n",
                pose.scene_index);
            script_character_pose.reset();
            graphics->hide_character();
            return;
        }
        try {
            if(pose.first_person_arms && fps_camera) {
                graphics->pose_character(scene.objects[pose.scene_index].position,
                    fps_pose.yaw,dt,pose.animation,true,
                    fps_pose.position,fps_pose.pitch);
            } else {
                graphics->pose_character(scene.objects[pose.scene_index].position,
                    pose.heading,dt,pose.animation);
            }
        } catch(std::exception const& e) {
            std::fprintf(stderr,
                "[WARN] [engine.assets] Skipping invalid character animation: %s\n",
                e.what());
            script_character_pose.reset();
            graphics->hide_character();
        }
    }

#include "window_runtime_ai_controllers.inc"

    void update_fps_camera(float dt, bool aiming)
    {
        if (!fps_camera || !fps || !preview) return;
        fps_camera->set_zoom(aiming ? 1.f : 0.f);
        CameraTarget target;
        target.eye = fps->eye(preview->scene());
        target.yaw = fps->yaw();
        target.pitch = fps->pitch();
        target.heading = target.yaw;
        target.ignore_object = fps->player_index();
        fps_pose = fps_camera->update(dt, target, &preview->scene(), preview.get());
    }

    void initialize_authored_player_camera() {
        auto const pawn=preview->controlled_entity();
        if(!gameplay::configure_player_camera(*fps_camera,preview->scene(),pawn))
            fps_camera->set_mode(CameraMode::third_person,0.f);
        update_script_camera(0.f);
    }

    void update_script_camera(float dt) {
        if (!scripted_first_person || !preview || !fps_camera) return;
        auto const& scene=preview->scene();
        auto const controlled=preview->controlled_index();
        auto const viewIndex=preview->view_target_index();
        if(!viewIndex || *viewIndex>=scene.objects.size())return;
        const auto pawnId=preview->controlled_entity();
        const auto viewId=preview->view_target();
        const bool overridden=viewId!=pawnId;
        CameraTarget target{};
        target.ignore_object=controlled.value_or(std::size_t(-1));
        target.yaw=script_yaw;
        target.pitch=script_pitch;
        target.heading=script_yaw;
        if(controlled && *controlled<scene.objects.size()) {
            target.eye=gameplay::camera_anchor(scene,pawnId,script_eye_height);
        } else {
            target.eye=scene.objects[*viewIndex].position;
        }
        // ViewTarget is an independent authored entity. Only the view changes:
        // the Jolt motor, input route and possessed Pawn are never rebound.
        if(!camera_view_target_initialized || viewId!=camera_view_target_last) {
            const bool changed=camera_view_target_initialized;
            const auto previous=camera_view_target_last;
            camera_view_target_last=viewId;
            camera_view_target_initialized=true;
            camera_view_target_blend_remaining=changed?.35f:0.f;
            if(overridden) {
                fps_camera->set_mode(CameraMode::free,changed?.35f:0.f);
            } else if(pawnId) {
                (void)gameplay::configure_player_camera(
                    *fps_camera,scene,pawnId,changed?.35f:0.f);
            }
            if(changed && script_runtime && !script_suspended) {
                const auto payload=std::string("{\"from\":")+
                    script_json_quote(std::to_string(previous))+
                    ",\"to\":"+script_json_quote(std::to_string(viewId))+"}";
                try {script_runtime->emit_event("camera.viewTargetStarted",payload);}
                catch(std::exception const& error) {
                    std::fprintf(stderr,"[WARN] [camera.viewTargetStarted] %s\\n",error.what());
                }
            }
        }
        if(overridden) {
            auto const& selected=scene.objects[*viewIndex];
            constexpr float radians=.017453292519943295f;
            CameraPose pose{};
            pose.position=selected.position;
            pose.yaw=selected.node.rotation.y*radians;
            pose.pitch=selected.node.rotation.x*radians;
            pose.roll=selected.node.rotation.z*radians;
            pose.fov_y_degrees=fps_camera->settings().default_fov_y;
            pose.near_plane=fps_camera->settings().near_plane;
            pose.far_plane=fps_camera->settings().far_plane;
            for(auto const& c:scene.components) {
                if(c.entity_id!=viewId || c.type!=scene_camera::type ||
                   c.version!=scene_camera::version)continue;
                if(auto const settings=scene_camera::decode(c.payload)) {
                    pose.fov_y_degrees=settings->fov;
                    pose.near_plane=settings->near_plane;
                    pose.far_plane=settings->far_plane;
                }
                break;
            }
            fps_camera->set_free_pose(pose);
        }
        fps_pose=fps_camera->update(dt,target,&scene,preview.get());
        if(camera_view_target_blend_remaining>0.f && dt>0.f) {
            camera_view_target_blend_remaining=
                std::max(0.f,camera_view_target_blend_remaining-dt);
            if(camera_view_target_blend_remaining==0.f &&
               script_runtime && !script_suspended) {
                const auto payload=std::string("{\"target\":")+
                    script_json_quote(std::to_string(viewId))+"}";
                try {script_runtime->emit_event("camera.viewTargetFinished",payload);}
                catch(std::exception const& error) {
                    std::fprintf(stderr,"[WARN] [camera.viewTargetFinished] %s\\n",error.what());
                }
            }
        }
        if(script_voice_audio && script_voice_audio->supports_voices()) {
            try {
                const float cy=std::cos(fps_pose.yaw);
                const float sy=std::sin(fps_pose.yaw);
                const float cp=std::cos(fps_pose.pitch);
                const float sp=std::sin(fps_pose.pitch);
                const NorthStarAudioListener listener{
                    sizeof(NorthStarAudioListener),
                    {fps_pose.position.x,fps_pose.position.y,fps_pose.position.z},
                    {sy*cp,sp,cy*cp},
                    {0.f,1.f,0.f}};
                script_voice_audio->set_listener(listener);
            } catch(std::exception const& error) {
                std::fprintf(stderr,"[WARN] [engine.audio] listener: %s\n",
                    error.what());
            }
        }
        // Camera/animation/audio/dialogue/gameplay cue tracks share the same
        // director clock. Deliver one bounded batch on the V8 game thread,
        // not from a worker or renderer callback. Authored project scripts
        // decide how each cue maps to sounds/animations/subtitles.
        auto cues=fps_camera->drain_cutscene_cues_json();
        if(script_runtime && !script_suspended && cues!="[]") {
            try {script_runtime->emit_event("camera.cues",cues);}
            catch(std::exception const& error) {
                std::fprintf(stderr,"[WARN] [camera.cues] %s\n",error.what());
                script_suspended=true;
            }
        }
    }

    void update_cinematic_postfx() {
        if(!graphics || !fps_camera || cinematic_postfx_unavailable_)return;
        try {
            if(fps_camera->cutscene_lens_active()) {
                if(!graphics->supports_postfx()) {
                    cinematic_postfx_unavailable_=true;
                    std::fprintf(stderr,
                        "[WARN] [camera.lens] Render provider has no Filmic PostFX ABI\n");
                    return;
                }
                if(!cinematic_postfx_original_)
                    cinematic_postfx_original_=graphics->postfx_settings();
                if(!cinematic_postfx_original_)return;
                float const exposure=std::clamp(
                    cinematic_postfx_original_->exposure_ev+
                    fps_camera->cutscene_exposure_ev(),-5.f,5.f);
                if(std::abs(exposure-cinematic_postfx_applied_ev_)<0.001f)return;
                auto settings=*cinematic_postfx_original_;
                settings.exposure_ev=exposure;
                graphics->set_postfx_settings(settings);
                cinematic_postfx_applied_ev_=exposure;
            } else if(cinematic_postfx_original_) {
                graphics->set_postfx_settings(*cinematic_postfx_original_);
                cinematic_postfx_original_.reset();
                cinematic_postfx_applied_ev_=1000.f;
            }
        }catch(std::exception const& error) {
            std::fprintf(stderr,"[WARN] [camera.lens] %s\n",error.what());
            cinematic_postfx_unavailable_=true;
            cinematic_postfx_original_.reset();
        }
    }

    // Generic transition; the game owns scene names and scripting policy.
    // Called only after the script event completes, on the Player thread.
    void switch_project_scene(fs::path const& requested)
    {
        if(editor || !scripted_game || !preview || file_path.empty())
            throw std::runtime_error("Scene transition requires a running game");
        if(requested.empty() || requested.is_absolute() ||
           requested.has_root_name() || requested.extension()!=L".nscn")
            throw std::invalid_argument("Scene path must be relative .nscn");
        for(auto const& part:requested)
            if(part=="." || part==".." || part.empty())
                throw std::invalid_argument("Scene path traversal is forbidden");

        fs::path next_path=requested;
        Scene next_scene;
        if(config.archive_path.empty()) {
            auto const root=fs::canonical(project_sky::find_root(file_path));
            auto const candidate=fs::canonical(root/requested);
            auto const relative=fs::relative(candidate,root);
            if(relative.empty() || relative.is_absolute())
                throw std::invalid_argument("Scene path is outside the project");
            for(auto const& part:relative)
                if(part=="..")
                    throw std::invalid_argument("Scene path escapes the project");
            if(!fs::is_regular_file(candidate))
                throw std::invalid_argument("Requested scene is not a file");
            next_path=candidate;
            next_scene=read_document(next_path);
        } else {
            AssetManager assets;
            assets.mount_archive("",config.archive_path,
                config.archive_data.empty()?config.archive_path:config.archive_data);
            next_scene=*assets.request_scene(requested.generic_u8string()).get();
        }
        if(next_path==file_path)return;
        auto prepared=std::make_unique<PhysicsScene>(next_scene);
        auto const prior_file=file_path;
        auto const prior_scene=document.snapshot();
        auto const prior_sky=sky_environment_;
        auto const prior_sky_mode=sky_mode_;
        auto const prior_sky_disabled=sky_scene_disabled_;
        auto const prior_sky_drawable=sky_drawable_reference_;
        stop_project_scripts();
        auto old_physics=std::move(preview);
        auto const prior_camera_settings=fps_camera_settings;
        auto const prior_pose=fps_pose;
        auto const prior_yaw=script_yaw,prior_pitch=script_pitch,prior_eye_height=script_eye_height;
        std::unique_ptr<CameraDirector> prior_camera;
        try {
            file_path=std::move(next_path);
            document.load(std::move(next_scene));
            preview=std::move(prepared);
            script_yaw=script_pitch=0.f;
            camera_view_target_initialized=false;
            camera_view_target_blend_remaining=0.f;
            if(scripted_first_person && preview->controlled_entity()) {
                auto const rig=gameplay::camera_rig(preview->scene(),
                    preview->controlled_entity());
                script_eye_height=rig.eye_height;
                fps_camera_settings.default_fov_y=std::clamp(rig.fov_degrees,25.f,120.f);
            }
            if(fps_camera) {
                prior_camera=std::move(fps_camera);
                fps_camera=std::make_unique<CameraDirector>(fps_camera_settings);
                initialize_authored_player_camera();
            }
            rebuild_scene_world();
            load_scene_sky();
            initialize_project_scripts();
            frame_clock.start();
            restore_gameplay_input_focus();
            std::fprintf(stderr,"[INFO] [engine.session] scene loaded: %s\n",
                file_path.u8string().c_str());
        } catch(...) {
            auto failure=std::current_exception();
            try {
                stop_project_scripts();
                file_path=prior_file;
                document.load(prior_scene);
                preview=std::move(old_physics);
                if(prior_camera)fps_camera=std::move(prior_camera);
                fps_camera_settings=prior_camera_settings;
                fps_pose=prior_pose;
                script_yaw=prior_yaw;
                script_pitch=prior_pitch;
                script_eye_height=prior_eye_height;
                rebuild_scene_world();
                sky_environment_=prior_sky;
                sky_mode_=prior_sky_mode;
                sky_scene_disabled_=prior_sky_disabled;
                sky_drawable_reference_=prior_sky_drawable;
                sky_visual_elapsed_=0;
                if(graphics)apply_scene_sky();
                initialize_project_scripts();
                frame_clock.start();
            } catch(std::exception const& rollback_error) {
                std::fprintf(stderr,"[ERROR] [engine.session] rollback: %s\n",
                    rollback_error.what());
            }
            std::rethrow_exception(failure);
        }
    }

    void restart_fps()
    {
        if (!fps) return;
        Scene const initial = document.snapshot();
        auto reset_physics = std::make_unique<PhysicsScene>(initial);
        auto reset_game = std::make_unique<FpsArena>(initial,
            fps_tuning, config.explore);
        preview = std::move(reset_physics);
        fps = std::move(reset_game);
        fps_weapon_fx=std::make_unique<FpsWeaponEffects>();
        if (fps_camera) fps_camera->reset();
        update_fps_camera(0.f, false);
        fps_previous_jump = fps_previous_reload = fps_previous_menu = false;
        explore_previous_use = false;
        fps_menu_visible = true;
        show_fps_menu(false);
        update_fps_title();
    }

    // A child Player HWND never becomes the OS foreground window while
    // Play In Editor is active: Windows reports the Qt Editor top-level HWND.
    // Keep the same foreground policy for cursor capture and script input.
    bool game_has_foreground() const
    {
        auto const foreground=GetForegroundWindow();
        return foreground && (foreground==window ||
            (config.embed_parent && IsWindow(config.embed_parent) &&
             foreground==GetAncestor(config.embed_parent,GA_ROOT)));
    }

    void capture_fps_cursor()
    {
        bool const active=game_has_foreground();
        if (editor || (!fps && !scripted_first_person) || fps_captured ||
            fps_menu_visible || script_paused || runtime_state_.pie_paused || runtime_state_.pie_ejected ||
            !window || !active) return;
        RECT client{};
        GetClientRect(window, &client);
        POINT top{client.left, client.top}, bottom{client.right, client.bottom};
        ClientToScreen(window, &top);
        ClientToScreen(window, &bottom);
        RECT bounds{top.x, top.y, bottom.x, bottom.y};
        if (!ClipCursor(&bounds)) return;
        SetCapture(window);
        ShowCursor(FALSE);
        fps_captured = true;
    }

    void release_fps_cursor()
    {
        if (!fps_captured) return;
        ClipCursor(nullptr);
        if (GetCapture() == window) ReleaseCapture();
        ShowCursor(TRUE);
        fps_captured = false;
    }

    // UI clicks focus the GPU child viewport, causing WM_KILLFOCUS on the
    // main Player HWND. Restore actual keyboard focus, not just cursor clip.
    // Do not steal the foreground from unrelated applications.
    void restore_gameplay_input_focus()
    {
        if (editor || (!fps && !scripted_first_person) || !window ||
            fps_menu_visible || script_paused || runtime_state_.pie_paused || runtime_state_.pie_ejected ||
            !game_has_foreground()) return;
        if (GetFocus() != window) {
            SetFocus(window); // WM_SETFOCUS re-enables the native input provider.
            if (GetFocus() != window) return;
        }
        capture_fps_cursor();
    }
