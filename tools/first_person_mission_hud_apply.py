from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=R/"engine/northstarcpp/modules/viewport/viewport.cpp"
s=p.read_text(encoding="utf-8-sig")
a=s.index("    void prepare_ui_overlay(bool gameplay) {")
b=s.index("    void fallback_to_d3d11() {",a)
replacement=r'''    void prepare_ui_overlay(bool gameplay) {
        const char* enabled=std::getenv("NORTHSTARCPP_UI_DEMO");
        const bool mission=gameplay && mission_hud.visible;
        const bool demo=!mission && gameplay && enabled && std::strcmp(enabled,"1")==0;
        if(!mission&&!demo) {
            if(ui_render_api && ui_active &&
               !ui_render_api->set_rgba(renderer,nullptr,0,0,0))
                throw module->error("Cannot clear runtime UI");
            ui_active=false;
            return;
        }
        if(!ui_render_api)
            ui_render_api=module->query_extension<NorthStarRenderUiApi>(
                NORTHSTARCPP_RENDER_UI_MODULE_ABI);
        if(!ui_render_api)
            throw std::runtime_error(
                "Active renderer lacks post-tonemap gameplay HUD compositor ABI");
        if(ui_context && ui_context_mission!=mission) {
            ui_api->destroy_context(ui_context);
            ui_context=nullptr;
            ui_label_handles.fill(0);
        }
        if(!ui_module) {
            ui_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_UI);
            ui_api=ui_module->query<NorthStarUIApi>(NORTHSTARCPP_UI_ABI);
            ui_raster=ui_module->query_extension<NorthStarUIRasterApi>(
                NORTHSTARCPP_UI_RASTER_ABI);
            if(!ui_raster)
                throw std::runtime_error("Active engine.ui provider cannot rasterize HUD");
        }
        auto set=[&](NorthStarUIHandle handle,const char* key,
                     NorthStarUIValue const& value) {
            if(ui_api->set_property(ui_context,handle,ui::string(key),&value)!=
               NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot set gameplay HUD property");
        };
        auto rgba=[](std::uint32_t value) {
            return NorthStarUIValue{sizeof(NorthStarUIValue),
                NORTHSTAR_UI_VALUE_COLOR_RGBA8,0,value,0,{}};
        };
        if(!ui_context) {
            NorthStarUIConfig config{sizeof(NorthStarUIConfig),0,nullptr};
            if(ui_api->create_context(&config,&ui_context)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create game HUD context");
            ui_context_mission=mission;
            NorthStarUIHandle root=0,panel=0;
            auto create=[&](std::uint32_t kind,NorthStarUIHandle parent,
                            const char* name) {
                auto d=ui::element(kind,parent,name);
                NorthStarUIHandle handle=0;
                if(ui_api->create_element(ui_context,&d,&handle)!=NORTHSTAR_UI_OK)
                    throw ui_module->error("Cannot create gameplay HUD element");
                return handle;
            };
            root=create(NORTHSTAR_UI_ROOT,0,"hudRoot");
            panel=create(NORTHSTAR_UI_PANEL,root,"missionPanel");
            set(root,"padding",ui::number(18));
            set(panel,"width",ui::number(mission?540:320));
            set(panel,"height",ui::number(mission?191:65));
            set(panel,"padding",ui::number(mission?14:8));
            set(panel,"gap",ui::number(3));
            set(panel,"background",rgba(0x0b1423d9));
            if(mission) {
                static const char* names[]={
                    "brand","stage","objective","progress","timer","controls"
                };
                const int heights[]={22,34,37,26,26,23};
                const int scales[]={2,3,2,2,2,1};
                const std::uint32_t colors[]={
                    0xf6c753ff,0xf5f8ffff,0xd3e4faff,
                    0xf6c753ff,0xa8ccdeff,0x77b2c8ff
                };
                for(std::size_t i=0;i<ui_label_handles.size();++i) {
                    auto handle=create(NORTHSTAR_UI_TEXT,panel,names[i]);
                    ui_label_handles[i]=handle;
                    set(handle,"height",ui::number(heights[i]));
                    set(handle,"font_scale",ui::number(scales[i]));
                    set(handle,"color",rgba(colors[i]));
                }
            } else {
                auto label=create(NORTHSTAR_UI_TEXT,panel,"demoStatus");
                set(label,"text",ui::text("NORTHSTAR UI NATIVE"));
            }
            ui_dirty=true;
            ui_last_width=ui_last_height=0;
        }
        if(mission && ui_dirty) {
            const auto& hud=mission_hud;
            const auto stage=std::min(hud.stage,hud.total);
            const auto percentage=hud.total
                ?std::min(100u,(stage*100u)/hud.total):0u;
            const std::string brand="NORTHSTAR / FIRST PERSON PROTOCOL";
            const std::string heading=hud.completed
                ?"COURSE COMPLETE"
                :std::to_string(stage)+" / "+std::to_string(hud.total)+
                  "   "+hud.label;
            const std::string progress="PROGRESS  "+std::to_string(percentage)+"%";
            const int elapsed=static_cast<int>(std::max(0.f,hud.elapsed));
            const std::string timer="TIME  "+std::to_string(elapsed/60)+":"+
                (elapsed%60<10?"0":"")+std::to_string(elapsed%60)+
                "     TARGET  "+std::to_string(static_cast<int>(
                    std::max(0.f,hud.distance)))+" M";
            const std::array<std::string,6> values={
                brand,heading,hud.objective,progress,timer,
                hud.completed ? "R RESTART    F10 EXIT" :
                "WASD MOVE    SHIFT RUN    SPACE JUMP    E USE"
            };
            for(std::size_t i=0;i<values.size();++i)
                set(ui_label_handles[i],"text",ui::text(values[i]));
            ui_dirty=false;
        }
        RECT rect{};
        if(!GetClientRect(window_handle,&rect) ||
           rect.right<=rect.left||rect.bottom<=rect.top)return;
        const auto width=static_cast<std::uint32_t>(rect.right-rect.left);
        const auto height=static_cast<std::uint32_t>(rect.bottom-rect.top);
        if(width>4096||height>4096 ||
           static_cast<uint64_t>(width)*height*4>64ull*1024*1024)return;
        if(ui_active && ui_last_width==width && ui_last_height==height &&
           !ui_dirty && mission && mission_hud_revision==ui_uploaded_revision)
            return;
        if(ui_api->resize(ui_context,width,height,1.f)!=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot resize runtime gameplay HUD");
        ui_pixels.resize(static_cast<size_t>(width)*height*4);
        if(ui_raster->draw_rgba(ui_context,ui_pixels.data(),width*4,width,height)
            !=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot rasterize gameplay HUD");
        if(!ui_render_api->set_rgba(renderer,ui_pixels.data(),width,height,width*4))
            throw module->error("Cannot submit gameplay HUD to GPU");
        ui_uploaded_revision=mission_hud_revision;
        ui_last_width=width;
        ui_last_height=height;
        ui_active=true;
    }
'''
s=s[:a]+replacement+s[b:]
start='    std::vector<std::uint8_t> ui_pixels;'
assert s.count(start)==1
s=s.replace(start,start+'''
    ViewportMissionHud mission_hud{};
    std::array<NorthStarUIHandle,6> ui_label_handles{};
    std::uint64_t mission_hud_revision=0;
    std::uint64_t ui_uploaded_revision=0;
    std::uint32_t ui_last_width=0,ui_last_height=0;
    bool ui_dirty=true,ui_active=false,ui_context_mission=false;
''',1)
start='        state->prepare_ui_overlay(fps!=nullptr);'
assert s.count(start)==1
s=s.replace(start,'        state->prepare_ui_overlay(fps!=nullptr || state->mission_hud.visible);',1)
start='void Viewport::hide_character()'
assert s.count(start)==1
s=s.replace(start,'''void Viewport::set_mission_hud(ViewportMissionHud hud) {
    if(hud.visible &&
       (!hud.total || hud.stage>hud.total ||
        !std::isfinite(hud.distance) || hud.distance<0.f ||
        !std::isfinite(hud.elapsed) || hud.elapsed<0.f ||
        hud.label.size()>64 || hud.objective.size()>120))
        throw std::invalid_argument("Invalid gameplay mission HUD state");
    auto& old=state->mission_hud;
    if(old.visible==hud.visible && old.stage==hud.stage &&
       old.total==hud.total && old.completed==hud.completed &&
       static_cast<int>(old.distance)==static_cast<int>(hud.distance) &&
       static_cast<int>(old.elapsed)==static_cast<int>(hud.elapsed) &&
       old.label==hud.label && old.objective==hud.objective)
        return;
    old=std::move(hud);
    state->ui_dirty=true;
    ++state->mission_hud_revision;
}

'''+start,1)
p.write_text(s,encoding="utf-8",newline="")
print("patched Viewport gameplay mission HUD")
