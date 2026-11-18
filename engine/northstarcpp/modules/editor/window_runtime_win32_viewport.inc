// NorthStarCpp window_runtime_win32_viewport.inc -- included in class App; do not compile separately.
    LRESULT viewport_message(HWND handle,UINT msg,WPARAM wp,LPARAM lp)
    {
        // The GPU swapchain is a child HWND. It receives pointer messages,
        // whereas gameplay input uses the main HWND. Only explicit interactive
        // UI nodes intercept these messages, and never during FPS capture.
        if(!editor && graphics && !fps_captured) {
            float const mx=static_cast<float>(GET_X_LPARAM(lp));
            float const my=static_cast<float>(GET_Y_LPARAM(lp));
            if(msg==WM_LBUTTONDOWN) {
                if(graphics->ui_hit_test(mx,my)) {
                    ui_mouse_down=true;
                    SetFocus(handle);
                    SetCapture(handle);
                    graphics->ui_pointer(mx,my,1);
                    return 0;
                }
                // A click on the viewport outside interactive UI returns
                // focus to gameplay after an OS activation or an alt-tab.
                // Consume that click so it cannot also fire a weapon.
                if ((fps || scripted_first_person) && !script_paused &&
                    !fps_menu_visible && game_has_foreground()) {
                    restore_gameplay_input_focus();
                    if (fps_captured) return 0;
                }
            } else if(msg==WM_MOUSEMOVE) {
                graphics->ui_pointer(mx,my,2);
                if(ui_mouse_down)return 0;
            } else if(msg==WM_LBUTTONUP && ui_mouse_down) {
                graphics->ui_pointer(mx,my,0);
                ui_mouse_down=false;
                if(GetCapture()==handle)ReleaseCapture();
                return 0;
            } else if(msg==WM_KEYDOWN && wp==VK_BACK) {
                if(graphics->ui_key(8,true))return 0;
            } else if(msg==WM_CHAR) {
                // WM_CHAR is decoded by Win32. Do not synthesize characters
                // from virtual keys: this preserves keyboard layout semantics.
                wchar_t code=static_cast<wchar_t>(wp);
                if(code>=0xd800 && code<=0xdfff)return 0;
                char utf8[8]{};
                int const size=WideCharToMultiByte(CP_UTF8,0,&code,1,
                    utf8,static_cast<int>(sizeof(utf8)),nullptr,nullptr);
                if(size>0 && graphics->ui_key(0,true,
                       std::string_view(utf8,static_cast<std::size_t>(size))))
                    return 0;
            }
        }
        // The GPU viewport is a child HWND. It may own keyboard focus after
        // a UI click while RAWINPUT mouse deltas still reach the main Player
        // HWND. Keep both paths in the *same* engine.input provider instead
        // of losing WASD when the child has focus. UI focus wins: an editable
        // UI widget must never trigger movement or gameplay shortcuts.
        if(!editor && input && (fps || scripted_first_person)) {
            if(msg==WM_SETFOCUS) {
                if(game_has_foreground())
                    input->feed_message(WM_SETFOCUS,0,0);
            } else if(msg==WM_KILLFOCUS) {
                input->feed_message(WM_KILLFOCUS,0,0);
            } else if(msg==WM_KEYDOWN || msg==WM_KEYUP ||
                      msg==WM_SYSKEYDOWN || msg==WM_SYSKEYUP) {
                if(!game_has_foreground())
                    return DefWindowProcW(handle,msg,wp,lp);
                // Key-down and key-up are both routed to the UI input
                // service, so held keys cannot leak into the game.
                if(graphics && graphics->ui_key(
                        static_cast<std::uint32_t>(wp),
                        msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN))
                    return 0;
                if(!script_paused && !fps_menu_visible &&
                   !runtime_state_.pie_paused && !runtime_state_.pie_ejected) {
                    input->feed_message(msg,static_cast<uintptr_t>(wp),
                                        static_cast<intptr_t>(lp));
                    return 0;
                }
            }
        }
        if(!editor || !surface)return DefWindowProcW(handle,msg,wp,lp);
        if(msg==WM_SETCURSOR && LOWORD(lp)==HTCLIENT) {
            SetCursor(LoadCursorW(nullptr,gizmo_dragging?IDC_SIZEALL:IDC_CROSS));
            return TRUE;
        }
        if (msg==WM_MOUSEWHEEL && !preview) {
            float const steps=static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp))/
                static_cast<float>(WHEEL_DELTA);
            editor_camera.dolly(steps*std::max(0.4f,camera_move_speed));
            return 0;
        }
        if (msg==WM_RBUTTONDOWN && !preview) {
            SetFocus(handle);
            camera_looking=true;
            camera_pointer={float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            SetCapture(handle);
            return 0;
        }
        if (msg==WM_MOUSEMOVE && camera_looking && (wp&MK_RBUTTON)) {
            ScreenPoint const pt{float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            editor_camera.look(pt.x-camera_pointer.x,pt.y-camera_pointer.y);
            camera_pointer=pt;
            return 0;
        }
        if (msg==WM_RBUTTONUP && camera_looking) {
            camera_looking=false;
            if(GetCapture()==handle)ReleaseCapture();
            return 0;
        }
        if(msg==WM_KEYDOWN && !preview) {
            if(wp==VK_ESCAPE && gizmo_dragging) {
                gizmo_dragging=false;gizmo_scene.reset();
                if(GetCapture()==handle)ReleaseCapture();
                return 0;
            }
            if(wp=='F' && valid_selection()) {
                auto const object=document.object(selected);
                editor_camera.frame(object.position,
                    std::clamp(object.size*6.f,6.f,150.f));
                return 0;
            }
            if(!camera_looking && (wp=='W' || wp=='E' || wp=='R')) {
                gizmo_mode=wp=='W' ? editor_tools::GizmoMode::translate :
                    wp=='E' ? editor_tools::GizmoMode::rotate :
                              editor_tools::GizmoMode::scale;
                update_gizmo_buttons();
                return 0;
            }
        }
        if(msg==WM_CAPTURECHANGED) {
            camera_looking=false;
            if(gizmo_dragging) {
                gizmo_dragging=false;gizmo_scene.reset();
            }
            return 0;
        }
        if(msg==WM_LBUTTONDOWN && !preview) {
            SetFocus(handle);
            ScreenPoint const pt{float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            auto const scene=document.snapshot();
            auto const hit=pick_gizmo_axis(scene,pt);
            if(hit>=0) {
                gizmo_baseline=scene;
                gizmo_drag_axis=hit;
                gizmo_start=pt;
                gizmo_dragging=true;
                gizmo_drag_amount=0.f;
                gizmo_scene.reset();
                SetCapture(handle);
                return 0;
            }
            auto const actor=pick_actor(scene,pt);
            if(actor>=0) {
                select_index(static_cast<std::size_t>(actor));
                refresh();
            }
            return 0;
        }
        if(msg==WM_MOUSEMOVE && gizmo_dragging && (wp&MK_LBUTTON)) {
            auto const idx=current_selection_index();
            if(idx>=gizmo_baseline.objects.size())return 0;
            ScreenPoint const pt{float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            auto const& actor=gizmo_baseline.objects[idx];
            auto const direction=editor_tools::gizmo_axis(actor,
                static_cast<editor_tools::GizmoAxis>(gizmo_drag_axis),
                gizmo_settings.local);
            auto const origin=project_world(actor.position,actor.position);
            auto const handle_length=active_gizmo(gizmo_baseline).length;
            Vector3 const endpoint{actor.position.x+direction.x*handle_length,
                actor.position.y+direction.y*handle_length,
                actor.position.z+direction.z*handle_length};
            auto const tip=project_world(endpoint,actor.position);
            if(!origin || !tip)return 0;
            if(gizmo_mode==editor_tools::GizmoMode::rotate) {
                constexpr float to_degrees=57.29577951308232f;
                float const start=std::atan2(gizmo_start.y-origin->y,gizmo_start.x-origin->x);
                float const now=std::atan2(pt.y-origin->y,pt.x-origin->x);
                float degrees=(now-start)*to_degrees;
                if(degrees>180.f)degrees-=360.f;
                if(degrees< -180.f)degrees+=360.f;
                gizmo_drag_amount=degrees;
            } else {
                float const vx=tip->x-origin->x,vy=tip->y-origin->y;
                float const square=std::max(64.f,vx*vx+vy*vy);
                float const offset=((pt.x-gizmo_start.x)*vx+
                    (pt.y-gizmo_start.y)*vy)/square*handle_length;
                gizmo_drag_amount=gizmo_mode==editor_tools::GizmoMode::scale ?
                    offset*.35f : offset;
            }
            gizmo_scene=gizmo_baseline;
            editor_tools::apply_gizmo(*gizmo_scene,idx,gizmo_mode,
                static_cast<editor_tools::GizmoAxis>(gizmo_drag_axis),
                gizmo_drag_amount,gizmo_settings);
            return 0;
        }
        if(msg==WM_LBUTTONUP && gizmo_dragging) {
            gizmo_dragging=false;
            if(GetCapture()==handle)ReleaseCapture();
            if(gizmo_scene && gizmo_drag_amount!=0.f) {
                auto scene=std::move(*gizmo_scene);
                gizmo_scene.reset();
                commit_scene(std::move(scene),current_selection_index());
            } else gizmo_scene.reset();
            return 0;
        }
        return DefWindowProcW(handle,msg,wp,lp);
    }

    static LRESULT CALLBACK procedure(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (!app)
            return DefWindowProcW(handle, msg, wparam, lparam);
        try {
            return app->message(handle, msg, wparam, lparam);
        } catch (std::exception const& error) {
            if ((msg == WM_COMMAND || msg == WM_CLOSE) && !app->config.smoke) {
                app->report(error.what());
                return 0;
            }
            // A Win32 MessageBox runs a nested message pump. If a frame
            // throws, stop dispatching WM_TIMER before opening the dialog:
            // otherwise the same failing frame can recursively open dozens
            // of modal errors and leave the Player unresponsive.
            app->runtime_faulted = true;
            KillTimer(handle, frame_timer);
            app->exit_status = 1;
            if(app->startup_report_ && !app->startup_report_->completed()) {
                app->startup_report_->failure("window.message",error.what());
                app->startup_report_->complete("FAILED");
            }
            app->report(error.what());
            DestroyWindow(handle);
            return 0;
        }
    }
