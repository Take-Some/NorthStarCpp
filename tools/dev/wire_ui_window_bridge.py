"""Route Win32 viewport messages into engine.ui, publish UI events to V8."""
from pathlib import Path
R=Path(__file__).resolve().parents[2]
p=R/"engine/northstarcpp/modules/editor/window.cpp"
s=p.read_text(encoding="utf-8-sig")
def edit(old,new):
 global s
 n=s.count(old)
 if n!=1: raise RuntimeError(f"Expected 1 match got {n}: {old[:85]!r}")
 s=s.replace(old,new,1)

edit('''    LRESULT viewport_message(HWND handle,UINT msg,WPARAM wp,LPARAM lp)
    {
        if(!editor || !surface)return DefWindowProcW(handle,msg,wp,lp);''',
'''    LRESULT viewport_message(HWND handle,UINT msg,WPARAM wp,LPARAM lp)
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
        if(!editor || !surface)return DefWindowProcW(handle,msg,wp,lp);''')
edit('''        if (script_runtime) {
            std::copy_n(std::begin(state.keys),script_keys.size(),script_keys.begin());''',
'''        if (script_runtime) {
            // Never call into V8 from an external UI provider callback.
            // Drain the provider's bounded event batch on the game thread
            // before the regular update event.
            if(graphics && !script_suspended) try {
                for(auto const& event:graphics->take_ui_events()) {
                    if(event.id.size()>96 || event.name.size()>64)continue;
                    std::string value="null";
                    switch(event.value_kind) {
                    case NORTHSTAR_UI_VALUE_BOOL:
                        value=event.boolean?"true":"false";break;
                    case NORTHSTAR_UI_VALUE_NUMBER:
                        if(std::isfinite(event.number))
                            value=std::to_string(event.number);
                        break;
                    case NORTHSTAR_UI_VALUE_STRING:
                        value=script_json_quote(event.text);
                        break;
                    default:break;
                    }
                    std::string payload="{\\"id\\":"+script_json_quote(event.id)+
                        ",\\"name\\":"+script_json_quote(event.name)+
                        ",\\"kind\\":"+std::to_string(event.kind)+
                        ",\\"value\\":"+value+"}";
                    script_runtime->emit_event("ui.event",payload);
                }
            } catch(std::exception const& error) {
                std::fprintf(stderr,
                    "[WARN] [engine.ui] UI dispatch error: %s\\n",error.what());
                script_suspended=true;
            }
            std::copy_n(std::begin(state.keys),script_keys.size(),script_keys.begin());''')
edit('''    bool fps_captured = false, fps_menu_visible = false;''',
'''    bool ui_mouse_down = false;
    bool fps_captured = false, fps_menu_visible = false;''')
p.write_text(s,encoding="utf-8",newline="\n")
print("Win32 input routed into native provider; queued UI events dispatched via V8")
