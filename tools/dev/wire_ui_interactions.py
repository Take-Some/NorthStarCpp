"""Integrate backend-neutral UI input/events with Player's V8 loop."""
from pathlib import Path
R=Path(__file__).resolve().parents[2]
def modify(p, edits):
    f=R/p
    s=f.read_text(encoding="utf-8-sig")
    for old,new in edits:
        n=s.count(old)
        if n!=1: raise RuntimeError(f"{p}: expected one occurrence; got {n}: {old[:100]!r}")
        s=s.replace(old,new,1)
    f.write_text(s,encoding="utf-8",newline="\n")
    print("patched",p)

modify("engine/northstarcpp/modules/viewport/ui_document.hpp",[
('struct UiDocument {\n', '''// UI events are provider-neutral: they reference script element IDs, not
// native pointer addresses or gameplay-specific callbacks.
struct UiInteractionEvent {
    std::string id;
    std::string name;
    std::uint32_t kind=0;
    std::uint32_t value_kind=0;
    double number=0;
    bool boolean=false;
    std::string text;
};
struct UiDocument {
''')
])
modify("engine/northstarcpp/modules/viewport/viewport.hpp",[
('    void set_ui_document(UiDocument document);\n', '''    void set_ui_document(UiDocument document);
    // UI input is dispatched only on the Player's UI/window thread.
    // Returning true means an interactive node consumed the pointer.
    bool ui_hit_test(float x,float y) const;
    bool ui_pointer(float x,float y,std::uint32_t phase);
    bool ui_key(std::uint32_t key,bool down,std::string_view utf8 = {});
    bool ui_wheel(float delta_x,float delta_y);
    std::vector<UiInteractionEvent> take_ui_events();
'''),
('#include <string>\n', '#include <string>\n#include <string_view>\n')
])
modify("engine/northstarcpp/modules/viewport/viewport.cpp",[
('                ui_handles.clear();\n                ui_applied_document={};',
 '''                ui_handles.clear();
                ui_names_by_handle.clear();
                ui_events.clear();
                ui_applied_document={};'''),
('            ui_raster=ui_module->query_extension<NorthStarUIRasterApi>(\n                NORTHSTARCPP_UI_RASTER_ABI);',
 '''            ui_raster=ui_module->query_extension<NorthStarUIRasterApi>(
                NORTHSTARCPP_UI_RASTER_ABI);
            ui_input_api=ui_module->query_extension<NorthStarUIInputApi>(
                NORTHSTARCPP_UI_INPUT_ABI);'''),
('                ui_handles.clear();\n            }\n            NorthStarUIConfig cfg{',
 '''                ui_handles.clear();
                ui_names_by_handle.clear();
                ui_events.clear();
            }
            NorthStarUIConfig cfg{'''),
('            if(ui_api->create_context(&cfg,&ui_context)!=NORTHSTAR_UI_OK)\n                throw ui_module->error("Cannot create UI context");',
 '''            if(ui_api->create_context(&cfg,&ui_context)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create UI context");
            // Provider callbacks are synchronous during pump_events().
            // Buffer the event; script callbacks run later, never reentrantly
            // inside the C ABI UI provider.
            if(ui_api->set_event_callback(ui_context,
                    [](void* owner,const NorthStarUIEvent* event) {
                        if(!owner||!event)return;
                        auto& state=*static_cast<State*>(owner);
                        try {
                            if(state.ui_events.size()>=1024)return;
                            auto it=state.ui_names_by_handle.find(event->target);
                            if(it==state.ui_names_by_handle.end())return;
                            UiInteractionEvent next;
                            next.id=it->second;
                            next.kind=event->kind;
                            next.value_kind=event->value.kind;
                            next.number=event->value.number;
                            next.boolean=event->value.boolean!=0;
                            if(event->name.data && event->name.length<=64)
                                next.name.assign(event->name.data,
                                                 static_cast<size_t>(event->name.length));
                            if(event->value.kind==NORTHSTAR_UI_VALUE_STRING &&
                               event->value.string.data &&
                               event->value.string.length<=4096)
                                next.text.assign(event->value.string.data,
                                                 static_cast<size_t>(event->value.string.length));
                            state.ui_events.push_back(std::move(next));
                        } catch(...) {
                            // Never throw through an external provider callback.
                        }
                    },this)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot register UI event sink");'''),
('                ui_handles.emplace(node.id,handle);',
 '''                ui_handles.emplace(node.id,handle);
                ui_names_by_handle.emplace(handle,node.id);'''),
('    NorthStarUIRasterApi const* ui_raster = nullptr;',
 '''    NorthStarUIRasterApi const* ui_raster = nullptr;
    NorthStarUIInputApi const* ui_input_api = nullptr;'''),
('    std::unordered_map<std::string,NorthStarUIHandle> ui_handles;',
 '''    std::unordered_map<std::string,NorthStarUIHandle> ui_handles;
    std::unordered_map<NorthStarUIHandle,std::string> ui_names_by_handle;
    std::vector<UiInteractionEvent> ui_events;'''),
('void Viewport::hide_character()\n', '''bool Viewport::ui_hit_test(float x,float y) const {
    const auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_raster||
       !std::isfinite(x)||!std::isfinite(y))return false;
    // Only interactive nodes intercept gameplay input. Panels/text do not
    // block camera movement, shooting or world-space picking.
    for(const auto& node:s.ui_applied_document.nodes) {
        if(node.kind!=NORTHSTAR_UI_BUTTON && node.kind!=NORTHSTAR_UI_INPUT)
            continue;
        auto handle=s.ui_handles.find(node.id);
        if(handle==s.ui_handles.end())continue;
        NorthStarUIRect rect{};
        if(s.ui_raster->get_rect(s.ui_context,handle->second,&rect)!=NORTHSTAR_UI_OK)
            continue;
        if(x>=rect.x && y>=rect.y && x<rect.x+rect.width &&
           y<rect.y+rect.height)return true;
    }
    return false;
}

bool Viewport::ui_pointer(float x,float y,std::uint32_t phase) {
    auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_raster)return false;
    const bool handled=ui_hit_test(x,y);
    if(s.ui_raster->pointer(s.ui_context,x,y,phase)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI pointer dispatch failed");
    if(s.ui_api->pump_events(s.ui_context)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI event pump failed");
    return handled;
}

bool Viewport::ui_key(std::uint32_t key,bool down,std::string_view utf8) {
    auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_input_api)return false;
    NorthStarUIHandle focused=0;
    if(s.ui_input_api->focused(s.ui_context,&focused)!=NORTHSTAR_UI_OK ||
       !focused)return false;
    if(s.ui_input_api->keyboard(s.ui_context,key,down?1u:0u,ui::string(utf8))
       !=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI keyboard dispatch failed");
    if(s.ui_api->pump_events(s.ui_context)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI event pump failed");
    s.ui_dirty=true;
    return true;
}

bool Viewport::ui_wheel(float dx,float dy) {
    auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_input_api)return false;
    if(s.ui_input_api->wheel(s.ui_context,dx,dy)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI wheel dispatch failed");
    s.ui_dirty=true;
    return true;
}

std::vector<UiInteractionEvent> Viewport::take_ui_events() {
    auto output=std::move(state->ui_events);
    state->ui_events.clear();
    return output;
}

void Viewport::hide_character()
''')
])
