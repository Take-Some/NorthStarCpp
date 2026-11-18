    HWND child(wchar_t const* type, wchar_t const* label, DWORD style, int id)
    {
        HWND const result = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (!result)
            throw std::runtime_error("Cannot create editor control");
        SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return result;
    }

    void create_editor_menu()
    {
        auto menu = CreateMenu();
        if (!menu)
            throw std::runtime_error("Cannot create editor menu");
        auto category = [menu](wchar_t const* caption) {
            auto popup=CreatePopupMenu();
            if (!popup)
                throw std::runtime_error("Cannot create editor menu group");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), caption);
            return popup;
        };
        auto command = [](HMENU popup, int id, wchar_t const* caption) {
            AppendMenuW(popup, MF_STRING, id, caption);
        };
        auto file=category(L"&File");
        command(file,new_scene,L"&New Scene");
        command(file,open_scene,L"&Open Scene...");
        command(file,save_scene_id,L"&Save Scene");
        command(file,save_as,L"Save &As...");
        AppendMenuW(file,MF_SEPARATOR,0,nullptr);
        command(file,menu_project_manager,L"Project &Manager...");

        auto edit=category(L"&Edit");
        command(edit,menu_undo,L"&Undo\tCtrl+Z");
        command(edit,menu_redo,L"&Redo\tCtrl+Y");
        AppendMenuW(edit,MF_SEPARATOR,0,nullptr);
        command(edit,add_object,L"&Add Cube");
        command(edit,menu_duplicate,L"D&uplicate\tCtrl+D");
        command(edit,menu_add_child,L"Add &Child");
        command(edit,delete_object,L"Delete &Entity");
        command(edit,menu_delete_subtree,L"Delete &Subtree");

        auto world=category(L"&World");
        command(world,menu_scene_v3,L"Enable Scene &v3");
        command(world,menu_layer,L"Create &Layer for Selection");
        command(world,menu_cell,L"Create Streaming &Cell");
        command(world,menu_volume,L"Create Trigger &Volume");
        command(world,menu_portal,L"Create &Portal");
        command(world,menu_validate_scene,L"&Validate Scene");
        command(world,menu_streaming_diagnostics,L"Streaming &Diagnostics...");
        command(world,menu_streaming_overlay,L"Show Streaming &Cells (3D)");

        auto assets=category(L"&Assets");
        command(assets,menu_bind_asset,L"&Bind Asset to Selection...");
        command(assets,menu_open_content,L"Open &Content Folder");
        command(assets,menu_inspect_assets,L"Inspect Shared &Assets");
        AppendMenuW(assets,MF_SEPARATOR,0,nullptr);
        command(assets,menu_prefab_export,L"Export Selection as &Prefab...");
        command(assets,menu_prefab_insert,L"&Instantiate Prefab...");

        auto scripting=category(L"&Scripting");
        command(scripting,menu_script_attach,L"Attach &Script Component...");
        command(scripting,menu_script_remove,L"&Remove Script Component");

        auto play=category(L"&Play");
        command(play,play_scene,L"&Play / Stop Physics");
        command(play,menu_launch_fps,L"Launch &FPS Player");

        auto view=category(L"&View");
        command(view,menu_show_content,L"Open Native &Content Browser...\tCtrl+Shift+B");
        command(view,menu_legacy_content,L"Legacy Asset Panel (Compatibility)");
        auto tools=category(L"&Tools");
        command(tools,camera_timeline,L"&Camera Timeline...");
        if (!SetMenu(window,menu))
            throw std::runtime_error("Cannot assign editor menu");
        DrawMenuBar(window);
    }

    fs::path workspace_preferences() const
    {
        std::array<wchar_t,32768> base{};
        DWORD const length=GetEnvironmentVariableW(L"LOCALAPPDATA",
            base.data(),static_cast<DWORD>(base.size()));
        if (!length || length>=base.size())return {};
        return fs::path(base.data())/L"KaylasSystems"/L"NorthStarCpp"/L"workspace.ini";
    }
    void restore_workspace()
    {
        if(config.smoke)return;
        auto const file=workspace_preferences();
        if(file.empty())return;
        auto const read=[&](wchar_t const* name,int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(
                L"Layout",name,fallback,file.c_str()));
        };
        left_width=std::clamp(read(L"Outliner",240),185,600);
        right_width=std::clamp(read(L"Details",295),240,650);
        content_height=std::clamp(read(L"ContentHeight",250),168,700);
        content_visible=read(L"LegacyContentVisible",0)!=0;
        // Compatibility-only visual regression: production defaults to Qt.
        if(GetEnvironmentVariableW(L"NORTHSTARCPP_TEST_LEGACY_CONTENT",nullptr,0)>0)
            content_visible=true;
        gizmo_mode=static_cast<editor_tools::GizmoMode>(
            std::clamp(read(L"GizmoMode",0),0,2));
        gizmo_settings.local=read(L"Local",0)!=0;
        gizmo_settings.snap=read(L"Snap",1)!=0;
        auto number=[&](wchar_t const* name,float initial)->float {
            wchar_t data[96]{};
            GetPrivateProfileStringW(L"Camera",name,L"",
                data,96,file.c_str());
            if(!data[0])return initial;
            wchar_t* end=nullptr;
            float const value=std::wcstof(data,&end);
            return end && *end==0 && std::isfinite(value) &&
                std::abs(value)<100000.f ? value : initial;
        };
        auto& pose=editor_camera.pose;
        pose.position.x=number(L"X",pose.position.x);
        pose.position.y=number(L"Y",pose.position.y);
        pose.position.z=number(L"Z",pose.position.z);
        pose.yaw=number(L"Yaw",pose.yaw);
        pose.pitch=std::clamp(number(L"Pitch",pose.pitch),-1.52f,1.52f);
        camera_move_speed=std::clamp(number(L"Speed",8.f),.5f,100.f);
    }
    void persist_workspace() const
    {
        if(config.smoke)return;
        auto const file=workspace_preferences();
        if(file.empty())return;
        std::error_code ec;
        fs::create_directories(file.parent_path(),ec);
        if(ec)return;
        auto write=[&](wchar_t const* group,wchar_t const* name,
                       std::wstring value) {
            WritePrivateProfileStringW(group,name,value.c_str(),file.c_str());
        };
        write(L"Layout",L"Outliner",std::to_wstring(left_width));
        write(L"Layout",L"Details",std::to_wstring(right_width));
        write(L"Layout",L"ContentHeight",std::to_wstring(content_height));
        write(L"Layout",L"LegacyContentVisible",std::to_wstring(content_visible?1:0));
        write(L"Layout",L"GizmoMode",std::to_wstring(static_cast<int>(gizmo_mode)));
        write(L"Layout",L"Local",std::to_wstring(gizmo_settings.local?1:0));
        write(L"Layout",L"Snap",std::to_wstring(gizmo_settings.snap?1:0));
        write(L"Camera",L"X",std::to_wstring(editor_camera.pose.position.x));
        write(L"Camera",L"Y",std::to_wstring(editor_camera.pose.position.y));
        write(L"Camera",L"Z",std::to_wstring(editor_camera.pose.position.z));
        write(L"Camera",L"Yaw",std::to_wstring(editor_camera.pose.yaw));
        write(L"Camera",L"Pitch",std::to_wstring(editor_camera.pose.pitch));
        write(L"Camera",L"Speed",std::to_wstring(camera_move_speed));
    }
    void create_editor_controls()
    {
        wchar_t const* names[]{L"New", L"Open...", L"Save", L"Save As...", L"Add Cube", L"Delete", L"Play", L"Camera..."};
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i)
            buttons[i] = child(L"BUTTON", names[i], WS_TABSTOP | BS_PUSHBUTTON, new_scene + i);
        create_editor_menu();
        inspector_tabs=child(L"COMBOBOX",L"",WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,inspector_pages);
        for (auto const* tab: {L"Object",L"Transform",L"Hierarchy / Asset",
                              L"LOD / Streaming",L"Streaming Budget"})
            SendMessageW(inspector_tabs,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(tab));
        SendMessageW(inspector_tabs,CB_SETCURSEL,0,0);
        list = child(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | WS_BORDER, object_list);
        wchar_t const* fields[]{L"Name", L"X", L"Y", L"Z", L"Size", L"Mass (kg)"};
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            labels[i] = child(L"STATIC", fields[i], 0, 0);
            edits[i] = child(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0);
            SendMessageW(edits[i], EM_SETLIMITTEXT, i == 0 ? 96 : 32, 0);
        }
        player = child(L"BUTTON", L"Auto Possess Local", WS_TABSTOP | BS_AUTOCHECKBOX, player_object);
        collision_flag = child(L"BUTTON", L"Collision", WS_TABSTOP | BS_AUTOCHECKBOX, collision_object);
        apply = child(L"BUTTON", L"Apply", WS_TABSTOP | BS_PUSHBUTTON, apply_properties);
        status = child(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0);
        outliner_title=child(L"STATIC",L"WORLD OUTLINER",SS_LEFTNOWORDWRAP,0);
        outliner_search=child(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,outliner_filter);
        SendMessageW(outliner_search,EM_SETCUEBANNER,0,
                     reinterpret_cast<LPARAM>(L"Filter actors by name..."));
        details_title=child(L"STATIC",L"DETAILS  /  PROPERTIES",SS_LEFTNOWORDWRAP,0);
        viewport_title=child(L"STATIC",L"PERSPECTIVE  /  SCENE VIEW",SS_LEFTNOWORDWRAP,0);
        content_title=child(L"STATIC",L"CONTENT BROWSER  /  PROJECT ASSETS",SS_LEFTNOWORDWRAP,0);
        wchar_t const* modes[]{L"W Move",L"E Rotate",L"R Scale"};
        int const ids[]{gizmo_move,gizmo_rotate,gizmo_scale};
        for (int i=0;i<3;++i)
            gizmo_buttons[i]=child(L"BUTTON",modes[i],WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE,ids[i]);
        local_button=child(L"BUTTON",L"Local",WS_TABSTOP|BS_AUTOCHECKBOX,gizmo_local);
        snap_button=child(L"BUTTON",L"Snap",WS_TABSTOP|BS_AUTOCHECKBOX,gizmo_snap);
        SendMessageW(snap_button,BM_SETCHECK,BST_CHECKED,0);
        step_edit=child(L"EDIT",L"1",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,gizmo_step);
        SendMessageW(step_edit,EM_SETLIMITTEXT,12,0);
        content_browser=std::make_unique<EditorContentBrowser>(window);
        bind_button=child(L"BUTTON",L"Bind to Actor",WS_TABSTOP|BS_PUSHBUTTON,content_bind);
        place_button=child(L"BUTTON",L"Place Mesh",WS_TABSTOP|BS_PUSHBUTTON,content_place);
        reveal_button=child(L"BUTTON",L"Show Folder",WS_TABSTOP|BS_PUSHBUTTON,content_reveal);
        update_gizmo_buttons();
    }

    void update_gizmo_buttons()
    {
        for (int i=0;i<3;++i)
            SendMessageW(gizmo_buttons[i],BM_SETCHECK,
                static_cast<int>(gizmo_mode)==i ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(local_button,BM_SETCHECK,
            gizmo_settings.local ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(snap_button,BM_SETCHECK,
            gizmo_settings.snap ? BST_CHECKED : BST_UNCHECKED,0);
        float const step=gizmo_mode==editor_tools::GizmoMode::translate ?
            gizmo_settings.translation_step :
            gizmo_mode==editor_tools::GizmoMode::rotate ?
                gizmo_settings.rotation_step : gizmo_settings.scale_step;
        set_number(step_edit,step);
    }

    bool handle_splitter(UINT message,WPARAM,LPARAM parameter)
    {
        RECT rect{};GetClientRect(window,&rect);
        int const width=rect.right,height=rect.bottom;
        POINT pt{};
        if (message==WM_SETCURSOR) {
            GetCursorPos(&pt);
            ScreenToClient(window,&pt);
        } else {
            pt.x=GET_X_LPARAM(parameter);pt.y=GET_Y_LPARAM(parameter);
        }
        int const left=std::clamp(left_width,185,std::max(185,width/3));
        int const right=std::clamp(right_width,240,std::max(240,width/3));
        int const content_top=height-34-
            (content_visible ? std::clamp(content_height,168,std::max(168,height/2)) : 0);
        auto hover=Splitter::none;
        if (pt.y>=73 && pt.y<height-33 &&
            std::abs(pt.x-(left+13))<=5)
            hover=Splitter::left;
        else if (pt.y>=73 && pt.y<content_top-8 &&
                 std::abs(pt.x-(width-right-13))<=5)
            hover=Splitter::right;
        else if (content_visible && pt.x>=left+18 &&
                 std::abs(pt.y-(content_top-4))<=5)
            hover=Splitter::bottom;
        if (message==WM_SETCURSOR) {
            auto const active=splitter_drag==Splitter::none?hover:splitter_drag;
            if (active==Splitter::none)return false;
            SetCursor(LoadCursorW(nullptr,active==Splitter::bottom?IDC_SIZENS:IDC_SIZEWE));
            return true;
        }
        if (message==WM_LBUTTONDOWN && hover!=Splitter::none) {
            splitter_drag=hover;
            SetCapture(window);
            return true;
        }
        if (message==WM_MOUSEMOVE && splitter_drag!=Splitter::none) {
            if (splitter_drag==Splitter::left)
                left_width=std::clamp(static_cast<int>(pt.x)-13,185,std::max(185,width/3));
            if (splitter_drag==Splitter::right)
                right_width=std::clamp(width-static_cast<int>(pt.x)-13,240,std::max(240,width/3));
            if (splitter_drag==Splitter::bottom)
                content_height=std::clamp(height-34-static_cast<int>(pt.y),168,std::max(168,height/2));
            layout();
            return true;
        }
        if (message==WM_LBUTTONUP && splitter_drag!=Splitter::none) {
            splitter_drag=Splitter::none;
            ReleaseCapture();
            persist_workspace();
            return true;
        }
        return false;
    }

    void layout()
    {
        RECT rect{};
        GetClientRect(window, &rect);
        int const width = rect.right;
        int const height = rect.bottom;
        if (!editor) {
            MoveWindow(surface, 0, 0, width, height, TRUE);

            return;
        }
        // Win32 can send WM_SIZE during child creation, before docking widgets
        // are constructed. Do not lay out the editor until all panes exist.
        if (!content_browser)
            return;
        int x=8;
        for (auto* button:buttons) {
            MoveWindow(button,x,7,72,30,TRUE);
            x+=75;
        }
        int gx=x+10;
        for (auto* button:gizmo_buttons) {
            MoveWindow(button,gx,7,73,30,TRUE);
            gx+=76;
        }
        MoveWindow(local_button,gx+2,8,58,29,TRUE);
        MoveWindow(snap_button,gx+63,8,58,29,TRUE);
        MoveWindow(step_edit,gx+127,10,69,25,TRUE);

        // Resizable three-column workspace, with a bottom asset dock.
        int const left=std::clamp(left_width,185,std::max(185,width/3));
        int const right=std::clamp(right_width,240,std::max(240,width/3));
        int const left_x=8, mid_x=left+18, right_x=width-right-8;
        int const mid_w=std::max(110,right_x-mid_x-10);
        int const content_h=content_visible ? std::clamp(content_height,168,std::max(168,height/2)) : 0;
        int const content_top=height-34-content_h;
        int const upper_bottom=content_visible ? content_top-8 : height-32;
        int const workspace_y=47, frame_y=73;
        int const upper_h=std::max(120,upper_bottom-frame_y);
        MoveWindow(outliner_title,left_x,workspace_y,left,23,TRUE);
        MoveWindow(outliner_search,left_x,frame_y,left,27,TRUE);
        MoveWindow(list,left_x,frame_y+32,left,std::max(95,height-frame_y-67),TRUE);
        MoveWindow(viewport_title,mid_x,workspace_y,mid_w,22,TRUE);
        MoveWindow(surface,mid_x,frame_y,mid_w,upper_h,TRUE);
        MoveWindow(details_title,right_x,workspace_y,right,22,TRUE);
        MoveWindow(inspector_tabs,right_x,frame_y,right,350,TRUE);
        for (int i=0;i<static_cast<int>(labels.size());++i) {
            int const y=frame_y+39+i*39;
            MoveWindow(labels[i],right_x+3,y+3,100,23,TRUE);
            MoveWindow(edits[i],right_x+108,y,std::max(70,right-111),27,TRUE);
        }
        int const actions_y=frame_y+39+6*39+6;
        MoveWindow(player,right_x+3,actions_y,100,27,TRUE);
        MoveWindow(collision_flag,right_x+105,actions_y,98,27,TRUE);
        MoveWindow(apply,right_x+right-90,actions_y+32,85,27,TRUE);
        if (content_visible) {
            int const content_x=mid_x, content_w=std::max(240,width-content_x-8);
            ShowWindow(content_title,SW_SHOW);
            MoveWindow(content_title,content_x,content_top,content_w-300,23,TRUE);
            MoveWindow(bind_button,content_x+content_w-296,content_top-2,101,26,TRUE);
            MoveWindow(place_button,content_x+content_w-190,content_top-2,90,26,TRUE);
            MoveWindow(reveal_button,content_x+content_w-95,content_top-2,95,26,TRUE);
            content_browser->layout(content_x,content_top+26,content_w,content_h-27);
        }
        content_browser->set_visible(content_visible);
        for (auto h:{bind_button,place_button,reveal_button})
            ShowWindow(h,content_visible ? SW_SHOW : SW_HIDE);
        if (!content_visible)ShowWindow(content_title,SW_HIDE);
        MoveWindow(status,8,height-26,std::max(1,width-16),21,TRUE);
    }

    int selected_index() const
    {
        auto ids = document.entities();
        auto it = std::find(ids.begin(),ids.end(),selected);
        return it == ids.end() ? -1 : static_cast<int>(it-ids.begin());
    }

    bool valid_selection() const
    {
        return selected != 0 && document.alive(selected);
    }

    void refresh_outliner(Scene const& view)
    {
        SendMessageW(list,WM_SETREDRAW,FALSE,0);
        SendMessageW(list,LB_RESETCONTENT,0,0);
        outliner_indices.clear();
        auto filter=get_text(outliner_search);
        std::transform(filter.begin(),filter.end(),filter.begin(),
            [](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        int selected_row=-1;
        int const chosen=selected_index();
        for (std::size_t i=0;i<view.objects.size();++i) {
            auto name=to_wide(editor_tools::hierarchy_label(view,i));
            if (scene_pawn::is_local(view,view.objects[i]))name+=L"  [Local Pawn]";
            if (!view.objects[i].node.script.empty())name+=L"  [JS]";
            auto match=name;
            std::transform(match.begin(),match.end(),match.begin(),
                [](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
            if (!filter.empty() && match.find(filter)==std::wstring::npos)
                continue;
            if (static_cast<int>(i)==chosen)
                selected_row=static_cast<int>(outliner_indices.size());
            outliner_indices.push_back(i);
            SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendMessageW(list,LB_SETCURSEL,selected_row,0);
        SendMessageW(list,WM_SETREDRAW,TRUE,0);
        InvalidateRect(list,nullptr,TRUE);
    }

    void refresh()
    {
        if (!editor)
            return;
        // A selection/inspector refresh is not a scene edit. Keep streaming
        // queues and pinned-cell state alive across ordinary UI updates.
        auto const view=document.snapshot();
        refresh_outliner(view);
        if (content_browser)
            content_browser->set_project(file_path);
        update_inspector();

        std::wstring title = L"NorthStarCpp Editor - ";
        title += file_path.empty() ? L"Untitled" : file_path.filename().wstring();
        if (dirty)
            title += L" *";
        SetWindowTextW(window, title.c_str());
        SetWindowTextW(buttons[play_scene - new_scene], preview ? L"Stop" : L"Play");
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
            if (i == play_scene - new_scene || i == camera_timeline - new_scene)
                continue;
            EnableWindow(buttons[i],
                !preview && (i != delete_object - new_scene || valid_selection()));
        }
        EnableWindow(list, !preview);
        std::wstring summary=preview ? L"Play mode | WASD / arrows | Esc to stop"
            : (L"Editor | ECS: "+to_wide(document.provider_name())+
                L" | Scene v"+std::to_wstring(document.snapshot().version)+
                L" | Entities: "+std::to_wstring(document.size())+
                L" | Undo: "+(history.can_undo()?L"yes":L"no"));
        if (scene_streaming.world()) {
            auto const& diagnostics=scene_streaming.world()->streaming_diagnostics();
            summary+=L" | Cells: "+std::to_wstring(diagnostics.loaded_cells)+L"/"+
                std::to_wstring(scene_streaming.world()->source().cells.size())+
                L" | Queue: "+std::to_wstring(diagnostics.pending_loads)+L" in / "+
                std::to_wstring(diagnostics.pending_unloads)+L" out"+
                L" | Visible: "+std::to_wstring(diagnostics.visible_entities);
        }
        SetWindowTextW(status,summary.c_str());
    }

    void update_inspector()
    {
        if (!editor)
            return;
        static wchar_t const* captions[5][6] = {
            {L"Name",L"X",L"Y",L"Z",L"Size",L"Mass (kg)"},
            {L"Rotation X",L"Rotation Y",L"Rotation Z",L"Scale X",L"Scale Y",L"Scale Z"},
            {L"Entity ID",L"Parent ID",L"Layer",L"Cell",L"Room",L"Asset"},
            {L"LOD Near",L"LOD Far",L"Load Radius",L"Unload",L"Lookahead",L"World Name"},
            {L"Loads / Frame",L"Unloads / Frame",L"Load Radius",L"Unload",L"Lookahead",L"World Name"}
        };
        bool const active = valid_selection();
        for(int i=0;i<6;++i) {
            SetWindowTextW(labels[i],captions[inspector_page][i]);
            EnableWindow(edits[i],active && !preview &&
                !(inspector_page==2 && i==0));
        }
        bool const is_basic=inspector_page==0;
        bool const is_metadata=inspector_page==2 || inspector_page==3;
        ShowWindow(player,is_basic||is_metadata ? SW_SHOW : SW_HIDE);
        ShowWindow(collision_flag,is_metadata ? SW_SHOW : SW_HIDE);
        EnableWindow(player,active && !preview);
        EnableWindow(collision_flag,active && !preview);
        EnableWindow(apply,active && !preview);
        EnableWindow(buttons[delete_object-new_scene],active && !preview);
        SetWindowTextW(player,is_basic ? L"Player" : L"Visible");
        if (!active) {
            for(auto* edit:edits)SetWindowTextW(edit,L"");
            SendMessageW(player,BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(collision_flag,BM_SETCHECK,BST_UNCHECKED,0);
            return;
        }
        auto const scene = document.snapshot();
        auto const object = document.object(selected);
        auto const& node = object.node;
        if (inspector_page==0) {
            SetWindowTextW(edits[0],to_wide(object.name).c_str());
            set_number(edits[1],object.position.x);
            set_number(edits[2],object.position.y);
            set_number(edits[3],object.position.z);
            set_number(edits[4],object.size);
            set_number(edits[5],object.mass);
        } else if (inspector_page==1) {
            set_number(edits[0],node.rotation.x);
            set_number(edits[1],node.rotation.y);
            set_number(edits[2],node.rotation.z);
            set_number(edits[3],node.scale.x);
            set_number(edits[4],node.scale.y);
            set_number(edits[5],node.scale.z);
        } else if (inspector_page==2) {
            SetWindowTextW(edits[0],std::to_wstring(node.id).c_str());
            SetWindowTextW(edits[1],std::to_wstring(node.parent).c_str());
            SetWindowTextW(edits[2],to_wide(node.layer).c_str());
            SetWindowTextW(edits[3],to_wide(node.cell).c_str());
            SetWindowTextW(edits[4],to_wide(node.room).c_str());
            SetWindowTextW(edits[5],to_wide(node.asset).c_str());
        } else if (inspector_page==3) {
            set_number(edits[0],node.lod_near);
            set_number(edits[1],node.lod_far);
            set_number(edits[2],scene.streaming.load_radius);
            set_number(edits[3],scene.streaming.unload_radius);
            set_number(edits[4],scene.streaming.lookahead_seconds);
            SetWindowTextW(edits[5],to_wide(scene.name).c_str());
        } else {
            SetWindowTextW(edits[0],
                std::to_wstring(scene.streaming.max_cell_loads_per_update).c_str());
            SetWindowTextW(edits[1],
                std::to_wstring(scene.streaming.max_cell_unloads_per_update).c_str());
            set_number(edits[2],scene.streaming.load_radius);
            set_number(edits[3],scene.streaming.unload_radius);
            set_number(edits[4],scene.streaming.lookahead_seconds);
            SetWindowTextW(edits[5],to_wide(scene.name).c_str());
        }
        SendMessageW(player,BM_SETCHECK,
            (is_basic ? scene_pawn::is_local(scene,object) : node.visible) ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(collision_flag,BM_SETCHECK,
            node.collision ? BST_CHECKED : BST_UNCHECKED,0);
    }

    fs::path choose_file(bool write)
    {
        std::array<wchar_t, 32768> name{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = write
            ? L"NorthStar Binary Scene (*.nscn)\0*.nscn\0\0"
            : L"NorthStar Binary Scene (*.nscn)\0*.nscn\0Legacy Text Scene (*.nscene)\0*.nscene\0\0";
        dialog.lpstrDefExt = L"nscn";
        dialog.lpstrFile = name.data();
        dialog.nMaxFile = name.size();
        dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
            (write ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        BOOL const result = write ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        if (!result && CommDlgExtendedError())
            throw std::runtime_error("Cannot open the scene file dialog");
        return result ? fs::path(name.data()) : fs::path{};
    }

    bool save(bool ask_path)
    {
        auto const target = ask_path || file_path.empty() ? choose_file(true) : file_path;
        if (target.empty())
            return false;
        if (config.new_document && !ask_path && target == config.scene_path && fs::exists(target) &&
            MessageBoxW(window, L"A scene already exists at this path. Replace it?",
                L"NorthStarCpp Editor", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
        if (target.extension()==L".nscene")
            throw std::runtime_error("Legacy .nscene is read-only. Use Save As to create a .nscn binary scene.");
        save_scene(document.snapshot(), target);
        config.new_document = false;
        file_path = target;
        dirty = false;
        if (scene_streaming.world())
            rebuild_scene_world(); // Refresh on-disk NEF8 index after Save As.
        refresh();
        return true;
    }

    bool confirm_discard()
    {
        if (!dirty)
            return true;
        auto const result = MessageBoxW(window, L"Save the changes to this scene?",
            L"NorthStarCpp Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
        return result == IDNO || (result == IDYES && save(false));
    }

    void launch_camera_timeline()
    {
        std::array<wchar_t,32768> host{};
        auto const count=GetModuleFileNameW(nullptr,host.data(),
            static_cast<DWORD>(host.size()));
        if (!count || count>=host.size())
            throw std::runtime_error("Cannot locate the camera editor executable directory");
        auto tool=fs::path(host.data()).parent_path() /
            L"northstarcpp_camera_timeline_editor.exe";
        if (!fs::is_regular_file(tool))
            tool=fs::path(host.data()).parent_path()/L"qt_tools"/
                L"northstarcpp_camera_timeline_editor.exe";
        if (!fs::is_regular_file(tool))
            throw std::runtime_error("Camera Timeline Editor is missing from bin/");
        std::wstring command_line=L"\"" + tool.wstring() + L"\"";
        if (!file_path.empty()) {
            auto const track=fs::absolute(file_path).parent_path() /
                "assets/cinematics/intro.camera.json";
            if (fs::is_regular_file(track))
                command_line+=L" \"" + track.wstring() + L"\"";
        }
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(tool.c_str(),command_line.data(),
                nullptr,nullptr,FALSE,0,nullptr,tool.parent_path().c_str(),
                &startup,&process))
            throw std::runtime_error("Unable to launch the Camera Timeline Editor");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    std::size_t current_selection_index() const
    {
        auto const index=selected_index();
        return index<0 ? std::numeric_limits<std::size_t>::max() :
            static_cast<std::size_t>(index);
    }

    void select_index(std::size_t index)
    {
        selected=index<document.size() ? document.at(index) : 0;
    }

    void commit_scene(Scene modified, std::size_t select)
    {
        validate_scene(modified);
        auto before=document.snapshot();
        auto const before_selected=current_selection_index();
        document.load(modified);
        dirty=true; // Unsaved edits must not reuse the on-disk cell index.
        rebuild_scene_world();
        history.record(std::move(before),before_selected);
        select_index(select);
        refresh();
    }

    void history_step(bool redo)
    {
        Scene restored;
        std::size_t target=0;
        auto const now=document.snapshot();
        auto const index=current_selection_index();
        bool const changed=redo
            ? history.redo(now,index,restored,target)
            : history.undo(now,index,restored,target);
        if (!changed)
            return;
        document.load(restored);
        rebuild_scene_world();
        select_index(target);
        dirty=true;
        refresh();
    }

    void launch_tool(wchar_t const* name, std::wstring arguments = {})
    {
        std::array<wchar_t,32768> exe{};
        auto const n=GetModuleFileNameW(nullptr,exe.data(),
            static_cast<DWORD>(exe.size()));
        if (!n || n>=exe.size())
            throw std::runtime_error("Cannot locate editor installation");
        auto path=fs::path(exe.data()).parent_path()/name;
        if (!fs::is_regular_file(path)) {
            // Qt tools have their own private DLL directory when co-packaged
            // with a UCRT V8 runtime; legacy flat layouts remain supported.
            path=fs::path(exe.data()).parent_path()/L"qt_tools"/name;
        }
        if (!fs::is_regular_file(path))
            throw std::runtime_error("Requested engine executable is not installed");
        auto command_line=L"\"" + path.wstring() + L"\"";
        if(!arguments.empty())
            command_line+=L" "+arguments;
        STARTUPINFOW start{};start.cb=sizeof(start);
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(path.c_str(),command_line.data(),nullptr,nullptr,
                FALSE,0,nullptr,path.parent_path().c_str(),&start,&process))
            throw std::runtime_error("Cannot launch requested engine tool");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    void open_content_folder()
    {
        if(file_path.empty())
            throw std::runtime_error("Save the scene in a project before opening content");
        auto content=fs::absolute(file_path).parent_path()/"assets";
        if(!fs::is_directory(content))
            throw std::runtime_error("This scene has no assets directory");
        if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",
             content.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
            throw std::runtime_error("Cannot open project assets folder");
    }

    void bind_selected_asset()
    {
        if(!valid_selection() || file_path.empty())
            throw std::runtime_error("Select an entity and save the scene in a project first");
        auto root=fs::absolute(file_path).parent_path()/"assets";
        if(!fs::is_directory(root))
            throw std::runtime_error("Project assets directory does not exist");
        std::array<wchar_t,32768> name{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);
        dialog.hwndOwner=window;
        dialog.lpstrFilter=L"Engine assets (*.ydd;*.ymat;*.netd;*.nscn;*.nscene)\0*.ydd;*.ymat;*.netd;*.nscn;*.nscene\0All files (*.*)\0*.*\0";
        dialog.lpstrFile=name.data();
        dialog.nMaxFile=static_cast<DWORD>(name.size());
        dialog.lpstrInitialDir=root.c_str();
        dialog.Flags=OFN_NOCHANGEDIR|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&dialog)) {
            if(CommDlgExtendedError())
                throw std::runtime_error("Cannot choose project asset");
            return;
        }
        auto absolute=fs::weakly_canonical(fs::path(name.data()));
        auto canonical_root=fs::weakly_canonical(root);
        auto relative=absolute.lexically_relative(canonical_root);
        if(relative.empty() || relative.is_absolute() ||
            *relative.begin()==fs::path(".."))
            throw std::runtime_error("Assets must be inside the project assets directory");
        auto scene=document.snapshot();
        auto const index=current_selection_index();
        editor_tools::promote_to_v3(scene);
        scene.objects[index].node.asset=relative.generic_u8string();
        commit_scene(std::move(scene),index);
    }

    fs::path project_prefab_file(bool for_export) {
        if (file_path.empty())
            throw std::runtime_error("Save the current project scene before using prefabs");
        auto const root=script_project_root(file_path)/"prefabs";
        if (fs::is_symlink(root))
            throw std::runtime_error("Project prefabs directory cannot be a symbolic link");
        if (for_export)
            fs::create_directories(root);
        else if (!fs::is_directory(root))
            throw std::runtime_error("Project has no prefabs/ directory");
        auto dir=root.wstring();
        std::array<wchar_t,32768> selected_name{};
        OPENFILENAMEW picker{};
        picker.lStructSize=sizeof(picker);
        picker.hwndOwner=window;
        picker.lpstrFile=selected_name.data();
        picker.nMaxFile=static_cast<DWORD>(selected_name.size());
        picker.lpstrInitialDir=dir.c_str();
        picker.lpstrFilter=L"NorthStar Prefabs (*.nprefab)\0*.nprefab\0\0";
        picker.lpstrDefExt=L"nprefab";
        picker.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|
            (for_export ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        bool const accepted=for_export ? GetSaveFileNameW(&picker)!=FALSE :
                                         GetOpenFileNameW(&picker)!=FALSE;
        if (!accepted) {
            if (CommDlgExtendedError())
                throw std::runtime_error("Cannot select a project prefab file");
            return {};
        }
        fs::path absolute=fs::weakly_canonical(fs::path(selected_name.data()));
        fs::path base=fs::weakly_canonical(root);
        auto relative=absolute.lexically_relative(base);
        if (relative.empty() || relative.is_absolute() || *relative.begin()==fs::path("..") ||
            absolute.extension()!=L".nprefab" || fs::is_symlink(absolute))
            throw std::runtime_error("Prefab must be a .nprefab file inside project prefabs/");
        return absolute;
    }

    void export_prefab_selection() {
        if (!valid_selection())
            throw std::runtime_error("Select a scene entity to export a prefab");
        auto prefab=editor_tools::capture_prefab(document.snapshot(),
                                                 current_selection_index());
        auto path=project_prefab_file(true);
        if (path.empty()) return;
        save_scene(prefab,path);
        // Prefab capture does not modify the authored scene or undo history.
    }

    void instantiate_project_prefab() {
        auto path=project_prefab_file(false);
        if (path.empty()) return;
        auto prefab=load_scene(path);
        auto scene=document.snapshot();
        editor_tools::promote_to_v3(scene);
        auto const selected=current_selection_index();
        std::uint64_t parent_id=0;
        Vector3 anchor{0.f,0.f,0.f};
        if (selected<scene.objects.size()) {
            auto const& source=scene.objects[selected];
            parent_id=source.node.id;
            anchor=source.position;
            anchor.x+=std::max(1.f,source.size*1.5f);
        }
        auto const index=editor_tools::instantiate_prefab(
            scene,prefab,anchor,parent_id);
        commit_scene(std::move(scene),index);
    }

    void edit_selected_script(bool remove) {
        if (!valid_selection() || file_path.empty())
            throw std::runtime_error("Select an entity in a saved project scene first");
        auto scene=document.snapshot();
        auto const index=current_selection_index();
        editor_tools::promote_to_v3(scene);
        if (remove) {
            scene.objects[index].node.script.clear();
            commit_scene(std::move(scene),index);
            return;
        }
        auto root=script_project_root(file_path)/"assets"/"scripts";
        if (!fs::is_directory(root) || fs::is_symlink(root))
            throw std::runtime_error("Create a regular assets/scripts/ project directory first");
        std::array<wchar_t,32768> name{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);
        dialog.hwndOwner=window;
        dialog.lpstrFilter=L"JavaScript files (*.js;*.mjs)\0*.js;*.mjs\0";
        dialog.lpstrFile=name.data();
        dialog.nMaxFile=static_cast<DWORD>(name.size());
        dialog.lpstrInitialDir=root.c_str();
        dialog.Flags=OFN_NOCHANGEDIR|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&dialog)) {
            if(CommDlgExtendedError())
                throw std::runtime_error("Cannot select project script");
            return;
        }
        auto absolute=fs::weakly_canonical(fs::path(name.data()));
        auto scripts=fs::weakly_canonical(root);
        auto relative=absolute.lexically_relative(scripts);
        if(relative.empty() || relative.is_absolute() ||
            *relative.begin()==fs::path("..") || fs::is_symlink(absolute))
            throw std::runtime_error("Script must be inside the project's assets/scripts directory");
        auto filename=relative.generic_u8string();
        if (!(fs::path(filename).extension()==".js" ||
              fs::path(filename).extension()==".mjs"))
            throw std::runtime_error("Entity scripts must use .js or .mjs");
        scene.objects[index].node.script="scripts/"+filename;
        commit_scene(std::move(scene),index);
    }

    void inspect_assets()
    {
        if (file_path.empty())
            throw std::runtime_error("Save your scene in a project first");
        auto const project=script_project_root(file_path);
        auto root=project/"assets";
        if (!fs::is_directory(root))
            throw std::runtime_error("Project assets folder is missing");
        AssetManager assets;
        assets.mount_project_assets(project,discover_shared_assets(config,project));
        std::size_t references=0,missing=0;
        std::wstring missing_list;
        for(auto const& object:document.snapshot().objects) {
            if(object.node.asset.empty())continue;
            ++references;
            auto const at=object.node.asset.find('@');
            auto path=object.node.asset.substr(0,at);
            try {
                (void)assets.size(path);
            } catch(std::exception const&) {
                ++missing;
                if(missing_list.size()<750)
                    missing_list+=L"\n"+to_wide(object.node.asset);
            }
        }
        std::wstring message=L"Active Asset Manager: "+to_wide(assets.provider_name())+
            L"\nBound references: "+std::to_wstring(references)+
            L"\nMissing references: "+std::to_wstring(missing);
        if(missing)
            message+=missing_list;
        MessageBoxW(window,message.c_str(),L"NorthStarCpp Asset Diagnostics",
            MB_OK|(missing?MB_ICONWARNING:MB_ICONINFORMATION));
    }

    void validate_editor_scene()
    {
        auto scene=document.snapshot();
        validate_scene(scene);
        std::wstring message=L"Scene version: "+std::to_wstring(scene.version)+
            L"\nEntities: "+std::to_wstring(scene.objects.size())+
            L"\nLayers: "+std::to_wstring(scene.layers.size())+
            L"\nStreaming cells: "+std::to_wstring(scene.cells.size())+
            L"\nVolumes: "+std::to_wstring(scene.volumes.size())+
            L"\nPortals: "+std::to_wstring(scene.portals.size());
        if(scene.version==3) {
            SceneWorld world(scene);
            auto center=valid_selection() ? document.object(selected).position : Vector3{0.f,0.f,0.f};
            world.update(center);
            message+=L"\nResident entities: "+std::to_wstring(world.resident_indices().size())+
                L"\nVisible entities: "+std::to_wstring(world.visible_indices().size());
        }
        MessageBoxW(window,message.c_str(),L"Scene Validation: PASS",
            MB_OK|MB_ICONINFORMATION);
    }

    void drop_project_asset(std::string const& reference,POINT point)
    {
        if(reference.empty()||preview)return;
        RECT viewport{};
        GetWindowRect(surface,&viewport);
        MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&viewport),2);
        RECT outliner{};
        GetWindowRect(list,&outliner);
        MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&outliner),2);
        bool const in_viewport=PtInRect(&viewport,point)!=0;
        bool const in_outliner=PtInRect(&outliner,point)!=0;
        if(!in_viewport && !in_outliner)return;
        auto scene=document.snapshot();
        editor_tools::promote_to_v3(scene);
        auto const extension=fs::u8path(reference).extension().wstring();
        if(in_outliner || extension!=L".ydd") {
            if(!valid_selection())return;
            auto const index=current_selection_index();
            scene.objects[index].node.asset=reference;
            commit_scene(std::move(scene),index);
            return;
        }
        if(scene.objects.size()>=max_scene_objects)
            throw std::runtime_error("Scene object limit reached");
        SceneObject actor;
        actor.name=fs::u8path(reference).stem().u8string().substr(0,96);
        if(actor.name.empty())actor.name="Asset";
        actor.node.asset=reference;
        std::uint64_t id=1;
        for(auto const& item:scene.objects)
            id=std::max(id,item.node.id+1);
        actor.node.id=id;
        RECT bounds{};GetClientRect(surface,&bounds);
        float const x=float(point.x-viewport.left),y=float(point.y-viewport.top);
        auto const ray=editor_camera.screen_ray(x,y,
            float(bounds.right),float(bounds.bottom));
        float t=12.f;
        if(ray.direction.y< -.0001f) {
            float const ground=-ray.origin.y/ray.direction.y;
            if(std::isfinite(ground)&&ground>0.f&&ground<20000.f)t=ground;
        }
        actor.position={ray.origin.x+ray.direction.x*t,
                        ray.origin.y+ray.direction.y*t,
                        ray.origin.z+ray.direction.z*t};
        float const step=gizmo_settings.translation_step;
        if(gizmo_settings.snap && std::isfinite(step) && step>.0001f) {
            actor.position.x=std::round(actor.position.x/step)*step;
            actor.position.y=std::round(actor.position.y/step)*step;
            actor.position.z=std::round(actor.position.z/step)*step;
        }
        scene.objects.push_back(std::move(actor));
        auto const index=scene.objects.size()-1;
        commit_scene(std::move(scene),index);
    }

    void command(int id, int notification)
    {
        if (id == object_list && notification == LBN_SELCHANGE) {
            auto const row=static_cast<int>(SendMessageW(list,LB_GETCURSEL,0,0));
            selected=row>=0 && static_cast<std::size_t>(row)<outliner_indices.size() ?
                document.at(outliner_indices[static_cast<std::size_t>(row)]) : 0;
            update_inspector();
            return;
        }
        if (id==outliner_filter && notification==EN_CHANGE) {
            refresh_outliner(document.snapshot());
            return;
        }
        if (id == inspector_pages && notification == CBN_SELCHANGE) {
            auto const page=SendMessageW(inspector_tabs,CB_GETCURSEL,0,0);
            if(page>=0 && page<5) {
                inspector_page=static_cast<int>(page);
                update_inspector();
            }
            return;
        }
        if (id==gizmo_step && notification==EN_KILLFOCUS) {
            float const step=parse_number(step_edit);
            if (!std::isfinite(step) || step<=0.f || step>10000.f)
                throw std::runtime_error("Transform snap step must be between 0 and 10000");
            if (gizmo_mode==editor_tools::GizmoMode::translate)
                gizmo_settings.translation_step=step;
            else if (gizmo_mode==editor_tools::GizmoMode::rotate)
                gizmo_settings.rotation_step=step;
            else gizmo_settings.scale_step=step;
            return;
        }
        if (notification != BN_CLICKED)
            return;
        if (id==menu_show_content) {
            // ContentBrowser is part of the unified Qt Editor, not a
            // separately launched asset browser process.
            std::wstring arguments=L"--focus-content-browser";
            if (!file_path.empty())
                arguments+=L" --scene \"" + fs::absolute(file_path).wstring() + L"\"";
            launch_tool(L"northstarcpp_editor_qt.exe",arguments);
            return;
        }
        if (id==menu_legacy_content) {
            content_visible=!content_visible;
            persist_workspace();
            CheckMenuItem(GetMenu(window),menu_legacy_content,
                MF_BYCOMMAND|(content_visible?MF_CHECKED:MF_UNCHECKED));
            layout();
            return;
        }
        if (id==gizmo_move || id==gizmo_rotate || id==gizmo_scale) {
            gizmo_mode=id==gizmo_move ? editor_tools::GizmoMode::translate :
                id==gizmo_rotate ? editor_tools::GizmoMode::rotate :
                    editor_tools::GizmoMode::scale;
            update_gizmo_buttons();
            return;
        }
        if (id==gizmo_local || id==gizmo_snap) {
            gizmo_settings.local=SendMessageW(local_button,BM_GETCHECK,0,0)==BST_CHECKED;
            gizmo_settings.snap=SendMessageW(snap_button,BM_GETCHECK,0,0)==BST_CHECKED;
            return;
        }
        if (id==content_reveal) {
            content_browser->show_selected_in_explorer();return;
        }
        if (id==content_bind || id==content_place) {
            if (preview)return;
            auto chosen=content_browser->selected_asset();
            if (!chosen)
                throw std::runtime_error("Select a file in Content Browser");
            auto scene=document.snapshot();
            editor_tools::promote_to_v3(scene);
            if (id==content_bind) {
                if (!valid_selection())
                    throw std::runtime_error("Select an actor in World Outliner first");
                auto const index=current_selection_index();
                scene.objects[index].node.asset=*chosen;
                commit_scene(std::move(scene),index);
            } else {
                auto const extension=fs::u8path(*chosen).extension().wstring();
                if (extension!=L".ydd" && extension!=L".YDD")
                    throw std::runtime_error("Place Mesh requires a .ydd drawable asset");
                if (scene.objects.size()>=max_scene_objects)
                    throw std::runtime_error("Scene entity limit reached");
                SceneObject actor;
                actor.name=fs::u8path(*chosen).stem().u8string().substr(0,96);
                if (actor.name.empty())actor.name="Asset";
                actor.node.asset=*chosen;
                std::uint64_t next=1;
                for (auto const& item:scene.objects)
                    next=std::max(next,item.node.id+1);
                actor.node.id=next;
                if (valid_selection()) {
                    auto const& selected_object=scene.objects[current_selection_index()];
                    actor.position=selected_object.position;
                    actor.position.x+=2.f;
                }
                scene.objects.push_back(std::move(actor));
                auto const new_index=scene.objects.size()-1;
                commit_scene(std::move(scene),new_index);
            }
            return;
        }
        if (id == menu_project_manager) {
            launch_tool(L"northstarcpp_project_manager.exe");
            return;
        }
        if (id == menu_validate_scene) {
            validate_editor_scene();
            return;
        }
        if (id == menu_streaming_diagnostics) {
            inspect_scene_streaming();
            return;
        }
        if (id == menu_streaming_overlay) {
            show_streaming_overlay=!show_streaming_overlay;
            auto* menu=GetMenu(window);
            if (menu)
                CheckMenuItem(menu,menu_streaming_overlay,
                    MF_BYCOMMAND | (show_streaming_overlay?MF_CHECKED:MF_UNCHECKED));
            if (surface)
                InvalidateRect(surface,nullptr,FALSE);
            return;
        }
        if (id == menu_open_content) {
            open_content_folder();
            return;
        }
        if (id == menu_inspect_assets) {
            inspect_assets();
            return;
        }
        if (id == menu_launch_fps) {
            if (file_path.empty() || dirty) {
                if (!save(false)) return;
            }
            launch_tool(L"northstarcpp_player.exe",
                L"--fps --scene \"" + file_path.wstring() + L"\"");
            return;
        }
        if (preview && id != play_scene && id != camera_timeline)
            return;
        if (id == menu_undo || id == menu_redo) {
            history_step(id == menu_redo);
            return;
        }
        if (id == menu_prefab_export) {
            export_prefab_selection();
            return;
        }
        if (id == menu_prefab_insert) {
            instantiate_project_prefab();
            return;
        }
        if (id == menu_bind_asset) {
            bind_selected_asset();
            return;
        }
        if (id == menu_script_attach || id == menu_script_remove) {
            edit_selected_script(id == menu_script_remove);
            return;
        }
        if (id == menu_scene_v3 || id == menu_duplicate || id == menu_add_child ||
            id == menu_delete_subtree || id == menu_layer || id == menu_cell ||
            id == menu_volume || id == menu_portal) {
            auto scene=document.snapshot();
            auto const index=current_selection_index();
            if (id == menu_scene_v3) {
                if (scene.version == 3) return;
                editor_tools::promote_to_v3(scene);
                commit_scene(std::move(scene),index);
                return;
            }
            if (!valid_selection())
                throw std::runtime_error("Select a scene entity before using this tool");
            auto result_index=index;
            if (id == menu_duplicate)
                result_index=editor_tools::duplicate(scene,index);
            else if (id == menu_add_child)
                result_index=editor_tools::duplicate(scene,index,true);
            else if (id == menu_delete_subtree)
                result_index=editor_tools::delete_subtree(scene,index);
            else if (id == menu_layer)
                editor_tools::create_layer(scene,index);
            else if (id == menu_cell)
                editor_tools::create_cell(scene,index);
            else if (id == menu_volume)
                editor_tools::create_volume(scene,index);
            else if (id == menu_portal)
                editor_tools::create_portal(scene,index);
            commit_scene(std::move(scene),result_index);
            return;
        }
        if (id == camera_timeline) {
            launch_camera_timeline();
            return;
        }
        if (id == play_scene) {
            if (preview) {
                stop_project_scripts();
                preview.reset();
                if (audio)
                    audio->stop();
            } else {
                preview = std::make_unique<PhysicsScene>(document.snapshot());
                if (scripted_first_person && !fps_camera) {
                    fps_camera=std::make_unique<CameraDirector>(fps_camera_settings);
                    initialize_authored_player_camera();
                }
                try {
                    if (!config.smoke) initialize_project_scripts();
                } catch (...) {
                    preview.reset();
                    stop_project_scripts();
                    throw;
                }
                if (!config.smoke)
                    play_start_sound();
            }
            frame_clock.start();
            SetFocus(window);
            refresh();
            return;
        }
        if (preview)
            return;
        switch (id) {
        case new_scene:
            if (!confirm_discard())
                return;
            document.load(Scene{});
            history.clear();
            file_path.clear();
            rebuild_scene_world();
            selected = 0;
            dirty = false;
            break;
        case open_scene: {
            if (!confirm_discard())
                return;
            auto const path = choose_file(false);
            if (path.empty())
                return;
            auto loaded = read_document(path);
            document.load(loaded);
            history.clear();
            file_path = path;
            rebuild_scene_world();
            selected = 0;
            dirty = false;
            break;
        }
        case save_scene_id:
        case save_as:
            save(id == save_as);
            return;
        case add_object: {
            if (document.size() == max_scene_objects)
                throw std::runtime_error("Scene has too many objects");
            SceneObject object;
            object.name = "Cube " + std::to_string(document.size() + 1);
            object.mass = 1.f;
            object.position.x = 1.4f * static_cast<float>(document.size() % 8);
            object.position.z = 1.4f * static_cast<float>(document.size() / 8);
            auto before=document.snapshot();
            auto const previous=current_selection_index();
            selected = document.create(object);
            rebuild_scene_world();
            history.record(std::move(before),previous);
            dirty = true;
            break;
        }
        case delete_object: {
            if (!valid_selection())
                return;
            auto scene=document.snapshot();
            auto const index=editor_tools::delete_subtree(scene,current_selection_index());
            commit_scene(std::move(scene),index);
            return;
        }
        case apply_properties:
            if (!valid_selection())
                return;
            apply_edit();
            break;
        default:
            return;
        }
        refresh();
    }

    void apply_edit()
    {
        auto changed=document.snapshot();
        auto const index=current_selection_index();
        if(index>=changed.objects.size())
            throw std::runtime_error("Selected ECS entity is no longer alive");
        editor_tools::promote_to_v3(changed);
        auto& object=changed.objects[index];

        if(inspector_page==0) {
            object.name=to_utf8(get_text(edits[0]));
            object.position={parse_number(edits[1]),parse_number(edits[2]),parse_number(edits[3])};
            object.size=parse_number(edits[4]);
            object.mass=parse_number(edits[5]);
            bool const possess=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            if(possess) {
                if(object.mass==0.f)object.mass=1.f;
                player_character::add_character_components(changed,object.node.id,false);
            }
            for(auto& component:changed.components) {
                if(component.type!=player_character::pawn_type ||
                   component.version!=player_character::version ||
                   component.payload.size()!=12)continue;
                // Change only auto-possess metadata, never destroy Pawn/Camera.
                component.payload[4]=char(possess &&
                    component.entity_id==object.node.id?1:0);
            }
        } else if(inspector_page==1) {
            object.node.rotation={parse_number(edits[0]),parse_number(edits[1]),parse_number(edits[2])};
            object.node.scale={parse_number(edits[3]),parse_number(edits[4]),parse_number(edits[5])};
        } else if(inspector_page==2) {
            auto const number=to_utf8(get_text(edits[1]));
            if(number.empty() ||
               !std::all_of(number.begin(),number.end(),
                  [](unsigned char c) { return c>='0' && c<='9'; }))
                throw std::runtime_error("Parent ID must be an unsigned integer");
            object.node.parent=std::stoull(number);
            object.node.layer=to_utf8(get_text(edits[2]));
            if(object.node.layer.empty()) object.node.layer="Default";
            object.node.cell=to_utf8(get_text(edits[3]));
            object.node.room=to_utf8(get_text(edits[4]));
            object.node.asset=to_utf8(get_text(edits[5]));
            object.node.visible=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            object.node.collision=SendMessageW(collision_flag,BM_GETCHECK,0,0)==BST_CHECKED;
            // Adding a layer is a scene transaction, not a Flecs-specific call.
            if(object.node.layer!="Default" &&
                std::none_of(changed.layers.begin(),changed.layers.end(),
                    [&](SceneLayer const& layer){return layer.name==object.node.layer;}))
                changed.layers.push_back({object.node.layer,true});
        } else if(inspector_page==3) {
            object.node.lod_near=parse_number(edits[0]);
            object.node.lod_far=parse_number(edits[1]);
            changed.streaming.load_radius=parse_number(edits[2]);
            changed.streaming.unload_radius=parse_number(edits[3]);
            changed.streaming.lookahead_seconds=parse_number(edits[4]);
            changed.name=to_utf8(get_text(edits[5]));
            object.node.visible=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            object.node.collision=SendMessageW(collision_flag,BM_GETCHECK,0,0)==BST_CHECKED;
        } else {
            auto parse_budget=[](HWND edit)->std::uint32_t {
                auto const text=to_utf8(get_text(edit));
                if (text.empty() || !std::all_of(text.begin(),text.end(),
                     [](unsigned char c){return c>='0' && c<='9';}))
                    throw std::runtime_error("Cell budget must be an unsigned integer");
                auto const number=std::stoul(text);
                if (number<1 || number>8192)
                    throw std::runtime_error("Cell budget must be between 1 and 8192");
                return static_cast<std::uint32_t>(number);
            };
            changed.streaming.max_cell_loads_per_update=parse_budget(edits[0]);
            changed.streaming.max_cell_unloads_per_update=parse_budget(edits[1]);
            changed.streaming.load_radius=parse_number(edits[2]);
            changed.streaming.unload_radius=parse_number(edits[3]);
            changed.streaming.lookahead_seconds=parse_number(edits[4]);
            changed.name=to_utf8(get_text(edits[5]));
        }
        commit_scene(std::move(changed),index);
    }
