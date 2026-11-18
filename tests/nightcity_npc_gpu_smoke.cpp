// Real GPU acceptance: native Ellie/Isaac YDD, YMT, YCD and skinning.
// Captures renderer-owned RGBA rather than using Win32 BitBlt (which can
// return the compositor's white background on modern swapchains).
#include <windows.h>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene.hpp>
#include "viewport.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace northstarcpp;
namespace {
void require(bool x,char const* message) {
    if(!x)throw std::runtime_error(message);
}
void save_ppm(std::filesystem::path const& path,
              Viewport::CapturedFrame const& frame) {
    std::ofstream out(path,std::ios::binary);
    if(!out)throw std::runtime_error("Cannot write QA frame");
    out<<"P6\n"<<frame.width<<" "<<frame.height<<"\n255\n";
    for(std::size_t i=0;i<frame.rgba.size();i+=4)
        out.write(reinterpret_cast<char const*>(&frame.rgba[i]),3);
}
std::size_t changed(Viewport::CapturedFrame const& a,
                    Viewport::CapturedFrame const& b) {
    require(a.width==b.width && a.height==b.height &&
            a.rgba.size()==b.rgba.size(),
            "GPU scene captures have different dimensions");
    std::size_t n=0;
    for(std::size_t i=0;i<a.rgba.size();i+=4) {
        unsigned int delta=0;
        for(int channel=0;channel<3;++channel)
            delta+=std::abs(int(a.rgba[i+channel])-int(b.rgba[i+channel]));
        if(delta>24)++n;
    }
    return n;
}
}
int main(int argc,char** argv) {
    if(argc!=4 && argc!=5)return 2;
    const std::string slug=argc==5?argv[4]:"ellie";
    if(slug!="ellie" && slug!="isaac")return 2;
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;
    wc.lpszClassName=L"NorthStar NPC Actual GPU Skin QA";
    if(!RegisterClassW(&wc))return 3;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"NPC actual YMT/YCD QA",
            WS_OVERLAPPEDWINDOW,100,100,800,600,
            nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 4;
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
    try {
        Viewport viewport(hwnd,false);
        viewport.set_asset_sources(argv[1],{}, {},argv[2]);
        Scene scene;
        scene.version=3;
        scene.name="Native NPC GPU skinning acceptance";
        SceneObject actor;
        actor.name=slug=="ellie"?"NPC Ellie":"NPC Isaac";
        actor.node.id=slug=="ellie"?155u:156u;
        actor.node.visible=true;
        actor.node.collision=false;
        actor.node.asset="characters/"+slug+"/models/"+slug+".ydd";
        actor.position={0.f,0.f,0.f};
        actor.size=1.f;
        actor.color=0xffffff;
        scene.objects.push_back(actor);
        scene.components.push_back({actor.node.id,"engine.model.animation",1,
          "animations/npcs/"+slug+"_locomotion.ycd@npc-idle"});
        CameraPose eye{};
        eye.position={0.f,1.45f,-3.4f};
        eye.yaw=0.f;
        eye.pitch=0.f;
        eye.fov_y_degrees=65.f;
        Viewport::CapturedFrame idle{},walk{};
        auto draw=[&] {
            viewport.draw(scene,-2,false,nullptr,nullptr,false,&eye,
                          nullptr,false);
        };
        bool ready=false;
        for(int i=0;i<145;++i) {
            draw();
            if(viewport.asset_stats().native_meshes>0) {
                ready=true;
                if(i>8)break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        require(ready,"Actual Ellie YDD did not stream into GPU viewport");
        viewport.set_scene_animation_override(actor.node.id,
            "animations/npcs/"+slug+"_locomotion.ycd@npc-idle",1.f);
        for(int frame=0;frame<14;++frame) {
            if(frame==13)require(viewport.request_scene_frame_capture(),
                                "No native GPU capture ABI");
            draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(35));
            if(frame==13) {
                auto result=viewport.read_scene_frame_capture();
                require(result.has_value(),"GPU idle capture unavailable");
                idle=std::move(*result);
            }
        }
        viewport.set_scene_animation_override(actor.node.id,
            "animations/npcs/"+slug+"_locomotion.ycd@npc-walk",1.8f);
        auto out=std::filesystem::path(argv[3]);
        std::filesystem::create_directories(out);
        std::vector<std::pair<int,Viewport::CapturedFrame>> gaitFrames;
        for(int frame=0;frame<54;++frame) {
            const bool capture=frame==6||frame==16||frame==27||
                               frame==38||frame==53;
            if(capture)require(viewport.request_scene_frame_capture(),
                              "No native GPU capture ABI");
            draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(35));
            if(capture) {
                auto result=viewport.read_scene_frame_capture();
                require(result.has_value(),"GPU walk phase capture unavailable");
                save_ppm(out/(slug+"_walk_"+std::to_string(frame)+".ppm"),*result);
                gaitFrames.emplace_back(frame,std::move(*result));
            }
        }
        require(gaitFrames.size()==5,"Missing gait phase capture");
        walk=gaitFrames.back().second;
        for(std::size_t j=1;j<gaitFrames.size();++j) {
            auto const moved=changed(gaitFrames[j-1].second,
                                      gaitFrames[j].second);
            std::cout<<slug<<" GPU gait "<<gaitFrames[j-1].first
                     <<"->"<<gaitFrames[j].first<<" pixelChanges="
                     <<moved<<"\n";
            // Reject the old immutable-GPU-mesh bug (about 300-550 pixels).
            require(moved>800,
                "CPU skinned NPC animation frozen in immutable GPU mesh cache");
        }
        std::filesystem::create_directories(out);
        save_ppm(out/(slug+"_idle.ppm"),idle);
        save_ppm(out/(slug+"_walk.ppm"),walk);
        auto const difference=changed(idle,walk);
        std::cout<<"Native "<<slug<<" GPU YCD provider="
            <<viewport.renderer_provider_name()
            <<" changed_pixels="<<difference
            <<" resolution="<<idle.width<<"x"<<idle.height
            <<" native_meshes="<<viewport.asset_stats().native_meshes<<"\n";
        require(difference>1800,
                "Real NPC GPU images do not show animated pose change");
        std::cout<<"Native "<<slug<<" YCD GPU acceptance: PASS\n";
    } catch(std::exception const& e) {
        std::cerr<<"Native NPC GPU test FAIL: "<<e.what()<<"\n";
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName,instance);
        return 5;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName,instance);
    return 0;
}
