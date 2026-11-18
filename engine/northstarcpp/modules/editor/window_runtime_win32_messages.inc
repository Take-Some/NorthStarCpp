// NorthStarCpp window_runtime_win32_messages.inc -- included in class App; do not compile separately.
    LRESULT message(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        // Versioned, editor-only cross-process controls. Processed on the
        // Player window thread, so the renderer/physics state is never mutated
        // concurrently with a frame. SendMessageTimeout returns 1 only after
        // the requested command has been applied.
        if(config.embed_parent && !editor) {
            static UINT const pause=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::pause);
            static UINT const resume=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::resume);
            static UINT const step=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::step);
            static UINT const eject=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::eject);
            static UINT const possess=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::possess);
            if(pause && msg==pause) {
                runtime_state_.pie_stepper.reset();
                runtime_state_.pie_paused=true;
                release_fps_cursor();
                return 1;
            }
            if(resume && msg==resume) {
                runtime_state_.pie_stepper.reset();
                runtime_state_.pie_paused=false;
                frame_clock.start(); // do not integrate time spent paused
                if(!runtime_state_.pie_ejected)capture_fps_cursor();
                return 1;
            }
            if(step && msg==step) {
                // Only acknowledge a genuine queued frame. Multiple rapid
                // clicks before the next render are coalesced to one tick.
                return runtime_state_.pie_stepper.request(runtime_state_.pie_paused,script_paused)?1:0;
            }
            if(eject && msg==eject) {
                runtime_state_.pie_ejected=true;
                release_fps_cursor();
                return 1;
            }
            if(possess && msg==possess) {
                runtime_state_.pie_ejected=false;
                if(!runtime_state_.pie_paused)capture_fps_cursor();
                return 1;
            }
        }
        if(config.embed_parent && msg==WM_KEYDOWN &&
           !(lparam&(1LL<<30))) {
            bool const shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            wchar_t const* request=nullptr;
            if(wparam==VK_F7)
                request=shift?::northstarcpp::editor::pie_protocol::resume_request:
                              ::northstarcpp::editor::pie_protocol::pause_request;
            else if(wparam==VK_F9)
                request=::northstarcpp::editor::pie_protocol::step_request;
            else if(wparam==VK_F8)
                request=::northstarcpp::editor::pie_protocol::eject_request;
            else if(wparam==VK_F6 && shift)
                request=::northstarcpp::editor::pie_protocol::exit_request;
            if(request) {
                UINT const notify=RegisterWindowMessageW(request);
                if(notify)PostMessageW(config.embed_parent,notify,0,0);
                return 0;
            }
        }
        if(config.embed_parent && msg==WM_KEYDOWN &&
           wparam==VK_ESCAPE && !(lparam&(1LL<<30))) {
            // Escape always exits Play In Editor, not an in-game pause
            // menu. Send the request to the owning Qt Editor HWND before
            // any game script, UI controller or provider consumes the key.
            release_fps_cursor();
            UINT const notify=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::exit_request);
            if(!notify || !PostMessageW(config.embed_parent,notify,0,0))
                PostMessageW(window,WM_CLOSE,0,0);
            return 0;
        }
        if (input)
            input->feed_message(msg, static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam));
        // The Qt overlay initially owns focus. A click in the embedded
        // native game window transfers input focus and enables relative mouse.
        if (config.embed_parent && msg==WM_LBUTTONDOWN &&
            (fps || scripted_first_person) && !runtime_state_.pie_paused && !runtime_state_.pie_ejected &&
            game_has_foreground()) {
            if (GetFocus()!=window) SetFocus(window);
            capture_fps_cursor();
        }
        switch (msg) {
        case WM_ACTIVATEAPP:
            if (fps || scripted_first_person) {
                if (!wparam) release_fps_cursor();
                else if (!fps_menu_visible && !runtime_state_.pie_paused && !runtime_state_.pie_ejected)
                    restore_gameplay_input_focus();
            }
            break;
        case WM_KILLFOCUS:
            if (fps || scripted_first_person) release_fps_cursor();
            break;
        case WM_SETFOCUS:
            if ((fps || scripted_first_person) && !runtime_state_.pie_paused && !runtime_state_.pie_ejected)
                capture_fps_cursor();
            break;
        case WM_GETMINMAXINFO: {
            auto* bounds = reinterpret_cast<MINMAXINFO*>(lparam);
            bounds->ptMinTrackSize.x = editor ? 1060 : 480;
            bounds->ptMinTrackSize.y = editor ? 610 : 460;
            return 0;
        }
        case WM_SIZE:
            if (surface && wparam != SIZE_MINIMIZED)
                layout();
            if(fps && fps_menu_visible && fps_ui_assets_ &&
               wparam!=SIZE_MINIMIZED) {
                try{reload_fps_menu_asset();}
                catch(std::exception const& error){
                    std::fprintf(stderr,
                        "[WARN] [engine.ui] FPS XML resize: %s\n",error.what());
                }
            }
            if ((fps || scripted_first_person) && fps_captured) {
                release_fps_cursor();
                capture_fps_cursor();
            }
            return 0;
        case WM_NOTIFY:
            if (editor && content_browser &&
                content_browser->on_notify(reinterpret_cast<NMHDR const*>(lparam)))
                return 0;
            break;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_SETCURSOR:
            if (editor && content_browser && content_browser->dragging()) {
                if (msg==WM_MOUSEMOVE || msg==WM_SETCURSOR) {
                    SetCursor(LoadCursorW(nullptr,IDC_HAND));
                    return TRUE;
                }
                if (msg==WM_LBUTTONUP) {
                    auto dragged=content_browser->finish_drag();
                    if(dragged && !preview) {
                        POINT pt{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
                        drop_project_asset(*dragged,pt);
                    }
                    return 0;
                }
            }
            if (editor && handle_splitter(msg,wparam,lparam))
                return TRUE;
            break;
        case WM_COMMAND:
            if (editor) {
                if (content_browser &&
                    content_browser->on_command(LOWORD(wparam),HIWORD(wparam)))
                    return 0;
                command(LOWORD(wparam), HIWORD(wparam));
                return 0;
            }
            break;
        case WM_APP+27:
            if (scripted_game && preview && !editor) {
                stop_project_scripts();
                preview=std::make_unique<PhysicsScene>(document.snapshot());
                script_yaw=script_pitch=0.f;
                if(fps_camera) fps_camera->reset();
                update_script_camera(0.f);
                initialize_project_scripts();
                restore_gameplay_input_focus();
            }
            return 0;
        case WM_APP+28:
            if (!editor && !requested_project_scene_.empty()) {
                auto path=requested_project_scene_;
                requested_project_scene_.clear();
                try { switch_project_scene(path); }
                catch (std::exception const& error) {
                    std::fprintf(stderr,
                        "[ERROR] [engine.session] scene transition: %s\n",error.what());
                    report(std::string("Cannot switch project scene: ")+error.what());
                }
            }
            return 0;
        case WM_TIMER:
            if (editor && content_browser)
                content_browser->poll_thumbnails();
            if (wparam == frame_timer && !runtime_faulted &&
                !error_dialog_active && !config.smoke && !config.check_scene &&
                !config.character_smoke && graphics && !IsIconic(window)) {
                if(startup_report_ && !startup_report_->completed()) {
                    bootstrap_stage("frame.first", [&] {frame();});
                    startup_report_->complete(startup_report_->warning_count()?
                        "READY_WITH_WARNINGS":"READY");
                } else frame();
            }
            return 0;
        case smoke_message:
            if (config.character_smoke) {
                if(!script_character || !script_character_pose)
                    throw std::runtime_error("Project did not configure a skeletal character");
                auto const& scene=preview->scene();
                auto const* camera=fps_camera ? &fps_pose : nullptr;
                for(unsigned i=0;i<8;++i) {
                    apply_script_character(scene,0.05f);
                    graphics->draw(scene,-2,false,preview.get(),nullptr,false,camera,
                        scene_streaming.world(),false);
                }
                std::puts("Project-defined textured skeletal draw: PASS");
            } else if (config.fps_ui_smoke) {
                if (!fps || !graphics || fps_ui_document_.nodes.empty())
                    throw std::runtime_error("FPS menu was not initialized");
                show_fps_menu(true);
                if (!fps_menu_visible)
                    throw std::runtime_error("FPS menu did not pause");
                Scene visual = preview->scene();
                fps->tint(visual);
                graphics->draw(visual,-2,true,preview.get(),fps.get(),true);
                if(!handle_fps_ui_action("fpspause.resume"))
                    throw std::runtime_error("FPS Resume action missing");
                if (fps_menu_visible)
                    throw std::runtime_error("FPS Resume did not unpause");
                show_fps_menu(true);
                if(!handle_fps_ui_action("fpspause.restart"))
                    throw std::runtime_error("FPS Restart action missing");
                if (fps_menu_visible || !fps->alive() || fps->ammunition() != fps->magazine_size())
                    throw std::runtime_error("FPS Restart did not reset state");
            } else if (!config.transition_scene.empty()) {
                // Reproduce menu -> game transitions without desktop input.
                // The real project scripts, Jolt, V8 and GPU are exercised.
                if(!script_runtime || !preview || !graphics)
                    throw std::runtime_error("Transition smoke requires a live project runtime");
                std::fprintf(stderr,
                    "[INFO] [engine.session] qa transition begin target=%s\\n",
                    config.transition_scene.u8string().c_str());
                switch_project_scene(config.transition_scene);
                if(!script_runtime || !preview)
                    throw std::runtime_error("Transition smoke lost Jolt/V8 runtime");
                frame();
                std::puts("Project scene transition + first gameplay GPU frame: PASS");
            } else if (config.check_scene) {
                if (asset_zones)
                    asset_zones->wait();
                if (!preview || !graphics)
                    throw std::runtime_error("Cannot initialize scene preview");
                update_procedural_terrain(1.f/60.f);
                if (fps) {
                    Scene visual = preview->scene();
                    fps->tint(visual);
                    graphics->draw(visual,-2,true,preview.get(),fps.get(),false,
                        fps_camera?&fps_pose:nullptr,scene_streaming.world(),
                        true,false,nullptr,nullptr,
                        procedural_terrain_render_chunks(),terrain_material);
                } else {
                    graphics->draw(preview->scene(),-2,true,preview.get(),nullptr,false,
                        fps_camera?&fps_pose:nullptr,scene_streaming.world(),
                        true,false,nullptr,nullptr,
                        procedural_terrain_render_chunks(),terrain_material);
                }
            } else {
                smoke();
            }
            DestroyWindow(handle);
            return 0;
        case WM_CLOSE:
            if (!editor || confirm_discard()) {
                if(editor)persist_workspace();
                DestroyWindow(handle);
            }
            return 0;
        case WM_DESTROY:
            release_fps_cursor();
            KillTimer(handle, frame_timer);
            input.reset();
            graphics.reset();
            PostQuitMessage(exit_status);
            return 0;
        }
        return DefWindowProcW(handle, msg, wparam, lparam);
    }
