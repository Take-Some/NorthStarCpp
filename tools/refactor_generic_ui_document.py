#!/usr/bin/env python3
"""Replace hard-coded gameplay HUD with provider-neutral engine.ui document."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
vp=R/"engine/northstarcpp/modules/viewport/viewport.cpp"
header=R/"engine/northstarcpp/modules/viewport/viewport.hpp"

def change(path, changes):
    s=path.read_text(encoding="utf-8-sig")
    for old,new,count in changes:
        n=s.count(old)
        if n!=count:
            raise RuntimeError(f"{path.name}: expected {count} occurrences of {old[:70]!r}, got {n}")
        s=s.replace(old,new)
    path.write_text(s,encoding="utf-8",newline="")

change(header,[
    ('#include <northstarcpp/scene.hpp>',
     '#include <northstarcpp/scene.hpp>\n#include "ui_document.hpp"',1),
    ('''// Authored gameplay state forwarded to engine.ui for a post-tonemap HUD.
struct ViewportMissionHud {
    bool visible=false;
    std::uint32_t stage=0;
    std::uint32_t total=0;
    float distance=0.f;
    float elapsed=0.f;
    bool completed=false;
    std::string label;
    std::string objective;
};

''',"",1),
    ("    void set_mission_hud(ViewportMissionHud hud);",
     "    void set_ui_document(UiDocument document);",1)
])
s=vp.read_text(encoding="utf-8-sig")
a=s.index("    void prepare_ui_overlay(bool gameplay) {")
b=s.index("    void fallback_to_d3d11() {",a)
replacement=r'''    void prepare_ui_overlay(bool gameplay) {
        // A generic UI document authored by scripts, never a mission-specific
        // structure. The optional demo uses the same element-tree contract.
        UiDocument demo;
        auto const* document=&ui_document;
        const char* enabled=std::getenv("NORTHSTARCPP_UI_DEMO");
        if(ui_document.nodes.empty() && gameplay && enabled &&
           std::strcmp(enabled,"1")==0) {
            UiDocumentNode panel;
            panel.id="demoPanel";
            panel.kind=NORTHSTAR_UI_PANEL;
            panel.properties={
                {"width",{NORTHSTAR_UI_VALUE_NUMBER,320}},
                {"height",{NORTHSTAR_UI_VALUE_NUMBER,65}},
                {"background",{NORTHSTAR_UI_VALUE_COLOR_RGBA8,0,0x0b1423d9}}
            };
            UiDocumentNode label;
            label.id="demoText";
            label.parent="demoPanel";
            label.kind=NORTHSTAR_UI_TEXT;
            UiPropertyValue text;
            text.kind=NORTHSTAR_UI_VALUE_STRING;
            text.text="NORTHSTAR UI NATIVE";
            label.properties={{"text",std::move(text)}};
            demo.nodes={std::move(panel),std::move(label)};
            document=&demo;
        }
        if(document->nodes.empty()) {
            if(ui_render_api && ui_active &&
               !ui_render_api->set_rgba(renderer,nullptr,0,0,0))
                throw module->error("Cannot clear UI composition");
            ui_active=false;
            if(ui_context&&ui_api) {
                ui_api->destroy_context(ui_context);
                ui_context=nullptr;
                ui_handles.clear();
                ui_applied_document={};
            }
            return;
        }
        if(!ui_render_api)
            ui_render_api=module->query_extension<NorthStarRenderUiApi>(
                NORTHSTARCPP_RENDER_UI_MODULE_ABI);
        if(!ui_render_api)
            throw std::runtime_error(
                "Active engine.render provider has no GPU UI compositor");
        if(!ui_module) {
            ui_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_UI);
            ui_api=ui_module->query<NorthStarUIApi>(NORTHSTARCPP_UI_ABI);
            ui_raster=ui_module->query_extension<NorthStarUIRasterApi>(
                NORTHSTARCPP_UI_RASTER_ABI);
            if(!ui_raster)
                throw std::runtime_error("Active engine.ui cannot rasterize");
        }
        auto set=[&](NorthStarUIHandle handle,
                     std::pair<std::string,UiPropertyValue> const& property) {
            auto const& input=property.second;
            NorthStarUIValue value{};
            switch(input.kind) {
            case NORTHSTAR_UI_VALUE_NUMBER: value=ui::number(input.number);break;
            case NORTHSTAR_UI_VALUE_BOOL: value=ui::boolean(input.boolean);break;
            case NORTHSTAR_UI_VALUE_STRING: value=ui::text(input.text);break;
            case NORTHSTAR_UI_VALUE_COLOR_RGBA8:
                value={sizeof(NorthStarUIValue),NORTHSTAR_UI_VALUE_COLOR_RGBA8,
                    0,input.rgba,0,{}};break;
            default:throw std::invalid_argument("Invalid UI property value type");
            }
            if(ui_api->set_property(ui_context,handle,ui::string(property.first),
                &value)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot apply UI property");
        };
        bool same_structure=ui_context &&
            document->nodes.size()==ui_applied_document.nodes.size();
        if(same_structure) {
            for(std::size_t i=0;i<document->nodes.size();++i) {
                const auto& current=document->nodes[i];
                const auto& previous=ui_applied_document.nodes[i];
                if(current.id!=previous.id || current.parent!=previous.parent ||
                   current.kind!=previous.kind ||
                   current.properties.size()!=previous.properties.size()) {
                    same_structure=false;break;
                }
                for(std::size_t j=0;j<current.properties.size();++j)
                    if(current.properties[j].first!=previous.properties[j].first ||
                       current.properties[j].second.kind!=
                       previous.properties[j].second.kind) {
                        same_structure=false;break;
                    }
                if(!same_structure)break;
            }
        }
        if(!same_structure) {
            if(ui_context) {
                ui_api->destroy_context(ui_context);
                ui_context=nullptr;
                ui_handles.clear();
            }
            NorthStarUIConfig cfg{sizeof(NorthStarUIConfig),0,nullptr};
            if(ui_api->create_context(&cfg,&ui_context)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create UI context");
            auto root=ui::element(NORTHSTAR_UI_ROOT,0,"screen");
            NorthStarUIHandle root_handle=0;
            if(ui_api->create_element(ui_context,&root,&root_handle)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create UI root");
            ui_handles.emplace("",root_handle);
            for(auto const& node:document->nodes) {
                auto parent=ui_handles.find(node.parent);
                if(parent==ui_handles.end())
                    throw std::invalid_argument("UI node parent must precede child");
                auto descriptor=ui::element(node.kind,parent->second,node.id);
                NorthStarUIHandle handle=0;
                if(ui_api->create_element(ui_context,&descriptor,&handle)!=NORTHSTAR_UI_OK)
                    throw ui_module->error("Cannot create UI node");
                ui_handles.emplace(node.id,handle);
            }
            ui_last_width=ui_last_height=0;
        }
        if(!same_structure || !(*document==ui_applied_document)) {
            for(std::size_t i=0;i<document->nodes.size();++i) {
                const auto& node=document->nodes[i];
                const auto& previous=same_structure?
                    ui_applied_document.nodes[i]:node;
                const auto handle=ui_handles.at(node.id);
                for(std::size_t j=0;j<node.properties.size();++j)
                    if(!same_structure ||
                       !(node.properties[j].second==previous.properties[j].second))
                        set(handle,node.properties[j]);
            }
            ui_applied_document=*document;
            ui_dirty=true;
        }
        RECT rect{};
        if(!GetClientRect(window_handle,&rect) ||
           rect.right<=rect.left||rect.bottom<=rect.top)return;
        const auto width=static_cast<std::uint32_t>(rect.right-rect.left);
        const auto height=static_cast<std::uint32_t>(rect.bottom-rect.top);
        if(width>4096||height>4096 ||
           static_cast<std::uint64_t>(width)*height*4>64ull*1024*1024)return;
        if(ui_active && ui_last_width==width && ui_last_height==height &&
           !ui_dirty)return;
        if(ui_api->resize(ui_context,width,height,1.f)!=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot resize UI context");
        ui_pixels.resize(static_cast<std::size_t>(width)*height*4);
        if(ui_raster->draw_rgba(ui_context,ui_pixels.data(),width*4,width,height)
            !=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot rasterize UI document");
        if(!ui_render_api->set_rgba(renderer,ui_pixels.data(),width,height,width*4))
            throw module->error("Cannot upload GPU UI document");
        ui_last_width=width;
        ui_last_height=height;
        ui_active=true;
        ui_dirty=false;
    }
'''
s=s[:a]+replacement+s[b:]
start=s.index("    ViewportMissionHud mission_hud{};")
end=s.index("    std::vector<NorthStarRenderCube> cubes;",start)
s=s[:start]+'''    UiDocument ui_document{};
    UiDocument ui_applied_document{};
    std::unordered_map<std::string,NorthStarUIHandle> ui_handles;
    std::uint32_t ui_last_width=0,ui_last_height=0;
    bool ui_dirty=true,ui_active=false;

'''+s[end:]
start=s.index("void Viewport::set_mission_hud(ViewportMissionHud hud) {")
end=s.index("void Viewport::hide_character()",start)
s=s[:start]+'''void Viewport::set_ui_document(UiDocument document) {
    if(!(state->ui_document==document)) {
        state->ui_document=std::move(document);
        state->ui_dirty=true;
    }
}

'''+s[end:]
old="state->prepare_ui_overlay(fps!=nullptr || state->mission_hud.visible);"
assert old in s
s=s.replace(old,"state->prepare_ui_overlay(fps!=nullptr || !state->ui_document.nodes.empty());",1)
# Never duplicate includes during iterative refactor.
s=s.replace('#include <northstarcpp/scene_pawn.hpp>\n#include <northstarcpp/scene_pawn.hpp>',
            '#include <northstarcpp/scene_pawn.hpp>')
vp.write_text(s,encoding="utf-8",newline="")
print("Viewport now renders generic script UI documents (no gameplay-specific data).")
