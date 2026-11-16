#include <northstarcpp/ui.hpp>
#include <northstarcpp/ui_cpu_compositor.hpp>
#include "module_loader.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iostream>
#include <vector>

namespace {
struct Events {
    std::vector<std::string> names;
    int reentrant = -1;
    const NorthStarUIApi* api = nullptr;
    void* context = nullptr;
};
void callback(void* p, const NorthStarUIEvent* event) {
    auto& state=*static_cast<Events*>(p);
    state.names.emplace_back(event->name.data, event->name.length);
    state.reentrant = state.api->pump_events(state.context);
}
void check(int32_t status) {
    if (status != NORTHSTAR_UI_OK) throw std::runtime_error("UI provider status: " + std::to_string(status));
}
}
int main() {
    northstarcpp::Module module{NORTHSTARCPP_CAPABILITY_UI};
    const auto& api=*module.query<NorthStarUIApi>(NORTHSTARCPP_UI_ABI);
    assert(module.provider() == "native");
    void* context=nullptr;
    const NorthStarUIConfig cfg{sizeof(cfg),0,nullptr};
    check(api.create_context(&cfg,&context));
    assert(context);
    const auto root=northstarcpp::ui::element(NORTHSTAR_UI_ROOT,0,"menu");
    NorthStarUIHandle root_id=0, button_id=0;
    check(api.create_element(context,&root,&root_id));
    const auto button=northstarcpp::ui::element(NORTHSTAR_UI_BUTTON,root_id,"start");
    check(api.create_element(context,&button,&button_id));
    assert(button_id && button_id!=root_id);
    const auto value=northstarcpp::ui::text("Hello UI");
    check(api.set_property(context,button_id,northstarcpp::ui::string("text"),&value));
    uint64_t required=0;
    check(api.get_property_json(context,button_id,northstarcpp::ui::string("text"),nullptr,0,&required));
    std::vector<char> buffer(required);
    check(api.get_property_json(context,button_id,northstarcpp::ui::string("text"),
                                buffer.data(),buffer.size(),&required));
    assert(std::strcmp(buffer.data(),"\"Hello UI\"")==0);
    Events observed;
    observed.api=&api;
    observed.context=context;
    check(api.set_event_callback(context,callback,&observed));
    NorthStarUIEvent event{};
    event.size=sizeof(event);
    event.kind=NORTHSTAR_UI_EVENT_CLICK;
    event.target=button_id;
    event.name=northstarcpp::ui::string("clicked");
    event.value=northstarcpp::ui::boolean(true);
    check(api.dispatch_event(context,&event));
    check(api.pump_events(context));
    assert(observed.names.size()==1 && observed.names[0]=="clicked");
    assert(observed.reentrant==NORTHSTAR_UI_INVALID_ARGUMENT);
    check(api.resize(context,1280,720,1.5f));
    NorthStarUITexture texture{};
    texture.size=sizeof(texture);
    assert(api.render(context,&texture)==NORTHSTAR_UI_UNSUPPORTED);
    assert(texture.backend==0 && texture.width==1280 && texture.height==720);
    // Optional ABI 2: deterministic CPU rendering and pointer hit-testing.
    const auto* raster=module.query_extension<NorthStarUIRasterApi>(
        NORTHSTARCPP_UI_RASTER_ABI);
    assert(raster && raster->size>=sizeof(*raster));
    const auto background=NorthStarUIValue{sizeof(NorthStarUIValue),
        NORTHSTAR_UI_VALUE_COLOR_RGBA8,0,0x224466ff,0,{nullptr,0}};
    check(api.set_property(context,button_id,northstarcpp::ui::string("background"),
                            &background));
    std::vector<unsigned char> rgba(1280u*720u*4u);
    check(raster->draw_rgba(context,rgba.data(),1280u*4u,1280,720));
    NorthStarUIRect bounds{};
    check(raster->get_rect(context,button_id,&bounds));
    assert(bounds.width>0 && bounds.height>0);
    // UTF-8 source is preserved as one Unicode code point per placeholder.
    auto unicode=northstarcpp::ui::text("Привет UI");
    check(api.set_property(context,button_id,northstarcpp::ui::string("text"),&unicode));
    check(raster->draw_rgba(context,rgba.data(),1280u*4u,1280,720));
    check(api.set_property(context,button_id,northstarcpp::ui::string("text"),&value));
    check(raster->draw_rgba(context,rgba.data(),1280u*4u,1280,720));
    assert(bounds.width>0 && bounds.height>0);
    auto px=[&](uint32_t x,uint32_t y,uint32_t channel) {
        return rgba[(static_cast<size_t>(y)*1280+x)*4+channel];
    };
    assert(px(static_cast<uint32_t>(bounds.x+2),static_cast<uint32_t>(bounds.y+2),0)==0x22);
    assert(px(static_cast<uint32_t>(bounds.x+2),static_cast<uint32_t>(bounds.y+2),1)==0x44);
    // Opt-in textured neon skin: hover and press must change raster pixels,
    // while the legacy plain button above retains its exact old colors.
    auto neon=northstarcpp::ui::text("neon");
    check(api.set_property(context,button_id,
        northstarcpp::ui::string("style"),&neon));
    auto primary=northstarcpp::ui::boolean(true);
    check(api.set_property(context,button_id,
        northstarcpp::ui::string("primary"),&primary));
    // ABI 4: button-only RGBA includes the text and true alpha contour.
    auto const* layers=module.query_extension<NorthStarUIRasterLayersApi>(
        NORTHSTARCPP_UI_RASTER_LAYERS_MODULE_ABI);
    assert(layers && layers->size>=sizeof(*layers));
    std::vector<unsigned char> buttonOnly(rgba.size());
    std::vector<unsigned char> withoutButton(rgba.size());
    check(raster->draw_rgba(context,rgba.data(),1280u*4u,1280,720));
    check(layers->draw_filtered_rgba(context,buttonOnly.data(),
        1280u*4u,1280,720,button_id,2));
    check(layers->draw_filtered_rgba(context,withoutButton.data(),
        1280u*4u,1280,720,button_id,1));
    assert(std::equal(rgba.begin(),rgba.end(),buttonOnly.begin()));
    assert(std::all_of(withoutButton.begin(),withoutButton.end(),
        [](unsigned char c){return c==0;}));
    const auto cutCorner=(static_cast<std::size_t>(bounds.y)*1280u+
        static_cast<std::size_t>(bounds.x+bounds.width-1))*4+3;
    assert(buttonOnly[cutCorner]==0);
    auto sample=[&]() {
        check(raster->draw_rgba(context,rgba.data(),1280u*4u,1280,720));
        return px(static_cast<uint32_t>(bounds.x+40),
                  static_cast<uint32_t>(bounds.y+2),0);
    };
    check(raster->pointer(context,1200,700,2));
    auto idlePixel=sample();
    // The upper-right chamfer must be truly cut away; the neighboring
    // surface remains opaque, rather than drawing a rectangular button.
    const auto chamferRight=static_cast<uint32_t>(bounds.x+bounds.width-1);
    const auto top=static_cast<uint32_t>(bounds.y);
    assert(px(chamferRight,top,3)==0 && "Cyberpunk top-right corner is cut out");
    assert(px(chamferRight-12,top,3)>0 && "Button face reaches the top bevel");
    check(raster->pointer(context,bounds.x+5,bounds.y+5,2));
    auto hoverPixel=sample();
    assert(hoverPixel!=idlePixel &&
           "Hover must alter the procedural neon button texture");
    check(raster->pointer(context,bounds.x+5,bounds.y+5,1));
    auto pressedPixel=sample();
    assert(pressedPixel!=hoverPixel &&
           "Pressed buttons must have a distinct visual state");
    check(raster->pointer(context,bounds.x+5,bounds.y+5,0));
    check(api.pump_events(context));
    // Initial "clicked" event, then native hover-entry/press/click events.
    assert(observed.names.size()==4);
    assert(observed.names[0]=="clicked");
    assert(observed.names[1]=="pointer");
    assert(observed.names[2]=="press");
    assert(observed.names[3]=="click");
    check(raster->pointer(context,1200,700,2));
    check(api.pump_events(context));
    assert(observed.names.size()==5 && observed.names.back()=="pointer");
    // Layout V2: grid, anchors, constraints and percentages.
    auto gridDesc=northstarcpp::ui::element(NORTHSTAR_UI_PANEL,root_id,"grid");
    NorthStarUIHandle grid=0,first=0,second=0;
    check(api.create_element(context,&gridDesc,&grid));
    auto setNum=[&](NorthStarUIHandle id,const char* key,double value) {
        auto v=northstarcpp::ui::number(value);
        check(api.set_property(context,id,northstarcpp::ui::string(key),&v));
    };
    auto setText=[&](NorthStarUIHandle id,const char* key,const char* value) {
        auto v=northstarcpp::ui::text(value);
        check(api.set_property(context,id,northstarcpp::ui::string(key),&v));
    };
    setNum(grid,"width",420);
    setNum(grid,"height",200);
    setText(grid,"layout","grid");
    setNum(grid,"columns",2);
    auto child=northstarcpp::ui::element(NORTHSTAR_UI_BUTTON,grid,"first");
    check(api.create_element(context,&child,&first));
    child=northstarcpp::ui::element(NORTHSTAR_UI_BUTTON,grid,"second");
    check(api.create_element(context,&child,&second));
    NorthStarUIRect firstRect{},secondRect{},gridRect{};
    check(raster->get_rect(context,grid,&gridRect));
    check(raster->get_rect(context,first,&firstRect));
    check(raster->get_rect(context,second,&secondRect));
    assert(secondRect.x>firstRect.x && firstRect.y==secondRect.y);
    setNum(first,"width_percent",50);
    check(raster->get_rect(context,first,&firstRect));
    assert(firstRect.width>0 && firstRect.width<secondRect.width);
    auto right=northstarcpp::ui::boolean(true);
    check(api.set_property(context,second,northstarcpp::ui::string("anchor_right"),&right));
    check(raster->get_rect(context,second,&secondRect));
    assert(secondRect.x+secondRect.width<=gridRect.x+gridRect.width);
    setNum(grid,"scroll_y",8);
    check(raster->get_rect(context,first,&firstRect));
    assert(firstRect.y>=gridRect.y);
    // ABI 3: focused text input, UTF-8, composition commit and wheel.
    auto const* inputApi=module.query_extension<NorthStarUIInputApi>(
        NORTHSTARCPP_UI_INPUT_ABI);
    assert(inputApi && inputApi->size>=sizeof(*inputApi));
    auto inputDesc=northstarcpp::ui::element(NORTHSTAR_UI_INPUT,root_id,"chat");
    NorthStarUIHandle inputHandle=0;
    check(api.create_element(context,&inputDesc,&inputHandle));
    NorthStarUIRect inputBounds{};
    check(raster->get_rect(context,inputHandle,&inputBounds));
    check(raster->pointer(context,inputBounds.x+3,inputBounds.y+3,1));
    check(raster->pointer(context,inputBounds.x+3,inputBounds.y+3,0));
    NorthStarUIHandle focusedHandle=0;
    check(inputApi->focused(context,&focusedHandle));
    assert(focusedHandle==inputHandle);
    auto typed=northstarcpp::ui::string("Привет");
    check(inputApi->keyboard(context,0,1,typed));
    auto committed=northstarcpp::ui::string(" мир");
    check(inputApi->composition(context,northstarcpp::ui::string("preview"),0));
    check(inputApi->composition(context,committed,1));
    uint64_t n=0;
    check(api.get_property_json(context,inputHandle,northstarcpp::ui::string("text"),nullptr,0,&n));
    std::vector<char> editValue(n);
    check(api.get_property_json(context,inputHandle,northstarcpp::ui::string("text"),
                                editValue.data(),editValue.size(),&n));
    assert(std::string(editValue.data())==std::string("\"Привет мир\""));
    check(inputApi->keyboard(context,8,1,northstarcpp::ui::string("")));
    check(inputApi->wheel(context,0,12));
    check(api.pump_events(context));
    assert(!observed.names.empty() && observed.names.back()=="change");
    check(api.destroy_element(context,root_id));
    check(inputApi->focused(context,&focusedHandle));
    assert(focusedHandle==0);
    assert(raster->get_rect(context,button_id,&bounds)==NORTHSTAR_UI_NOT_FOUND);
    assert(api.dispatch_event(context,&event)==NORTHSTAR_UI_NOT_FOUND);
    check(api.destroy_context(context));
    // GPU compositor reference: opaque scene + straight-alpha UI.
    uint8_t scene[8]={20,40,60,255,10,20,30,255};
    const uint8_t overlay[8]={220,140,60,128,255,255,255,0};
    assert(northstarcpp::ui::composite_opaque_rgba(
        {scene,2,1,8},{overlay,2,1,8}));
    assert(scene[0]>=119 && scene[0]<=121);
    assert(scene[1]>=89 && scene[1]<=91);
    assert(scene[2]==60 && scene[3]==255);
    assert(scene[4]==10 && scene[5]==20 && scene[6]==30 && scene[7]==255);
    assert(!northstarcpp::ui::composite_opaque_rgba(
        {scene,2,1,7},{overlay,2,1,8}));
    // 1000 CPU raster frames: diagnostics only, not Vulkan/D3D12 timings.
    void* perfContext=nullptr;
    check(api.create_context(&cfg,&perfContext));
    auto perfRoot=northstarcpp::ui::element(NORTHSTAR_UI_ROOT,0,"perf");
    NorthStarUIHandle perfId=0,perfText=0;
    check(api.create_element(perfContext,&perfRoot,&perfId));
    auto perfLabel=northstarcpp::ui::element(NORTHSTAR_UI_TEXT,perfId,"labels");
    check(api.create_element(perfContext,&perfLabel,&perfText));
    auto perfValue=northstarcpp::ui::text("Привет NorthStar 123");
    check(api.set_property(perfContext,perfText,northstarcpp::ui::string("text"),&perfValue));
    check(api.resize(perfContext,320,180,1.f));
    std::vector<uint8_t> framebuffer(320u*180u*4u);
    std::vector<double> samples;
    samples.reserve(1000);
    for(int frame=0;frame<1000;++frame) {
        const auto start=std::chrono::steady_clock::now();
        check(raster->draw_rgba(perfContext,framebuffer.data(),320u*4u,320u,180u));
        const auto end=std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double,std::milli>(end-start).count());
    }
    std::sort(samples.begin(),samples.end());
    std::cout<<"UI CPU raster (320x180, 1000 frames): p50="
             <<samples[499]<<"ms p95="<<samples[949]
             <<"ms p99="<<samples[989]<<"ms\\n";
    check(api.destroy_context(perfContext));
    return 0;
}
