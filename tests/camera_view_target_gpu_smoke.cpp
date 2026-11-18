// GPU end-to-end camera view-target handoff, without a mocked renderer.
// Produces two actual Win32 swapchain framebuffer captures and compares pixels.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/player_camera_runtime.hpp>
#include <northstarcpp/scene_camera_actor.hpp>
#include "viewport.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::uint32_t> capture(HWND hwnd,int width,int height) {
    HDC source=GetDC(hwnd);
    if(!source)throw std::runtime_error("Cannot read GPU viewport HWND");
    HDC memory=CreateCompatibleDC(source);
    HBITMAP bitmap=CreateCompatibleBitmap(source,width,height);
    if(!memory||!bitmap) {
        if(bitmap)DeleteObject(bitmap);
        if(memory)DeleteDC(memory);
        ReleaseDC(hwnd,source);
        throw std::runtime_error("Win32 framebuffer allocation failed");
    }
    HGDIOBJ previous=SelectObject(memory,bitmap);
    BOOL const copied=BitBlt(memory,0,0,width,height,source,0,0,SRCCOPY);
    std::vector<std::uint32_t> pixels(std::size_t(width)*std::size_t(height));
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32;
    info.bmiHeader.biCompression=BI_RGB;
    int rows=copied?GetDIBits(memory,bitmap,0,UINT(height),pixels.data(),&info,
                            DIB_RGB_COLORS):0;
    SelectObject(memory,previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(hwnd,source);
    if(rows!=height)throw std::runtime_error("GPU framebuffer GDI readback failed");
    return pixels;
}
void require(bool condition,char const* message) {
    if(!condition)throw std::runtime_error(message);
}
}
int main() {
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance=instance;
    wc.lpfnWndProc=DefWindowProcW;
    wc.lpszClassName=L"NorthStarViewTargetGpuQA";
    if(!RegisterClassW(&wc))return 2;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,
        L"NorthStar View Target GPU",WS_OVERLAPPEDWINDOW,
        100,100,960,540,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 3;
    ShowWindow(hwnd,SW_SHOW);
    UpdateWindow(hwnd);
    int status=0;
    try {
        northstarcpp::Scene scene;
        scene.version=3;
        scene.name="Camera View Target real-GPU QA";
        const auto character=northstarcpp::player_blueprint::create(
            scene,"QA Pawn",{0.f,0.f,-2.f},true);
        northstarcpp::SceneObject view;
        view.node.id=98765001;
        view.name="View Target";
        view.node.visible=true;
        view.node.collision=false;
        view.position={8.f,2.f,0.f};
        view.node.rotation.y=-80.f;
        scene.objects.push_back(view);
        scene.components.push_back({view.node.id,
            northstarcpp::scene_camera::type,
            northstarcpp::scene_camera::version,
            northstarcpp::scene_camera::encode({62.f,.08f,400.f,0,0})});
        // Reproducible 3D geometry makes the two camera views visually distinct.
        for(int i=0;i<4;++i) {
            northstarcpp::SceneObject object;
            object.node.id=98765100u+unsigned(i);
            object.name="View Target Test Geometry";
            object.node.collision=false;
            object.position={float(i)*3.f-4.5f,.15f,5.f+float(i)*2.f};
            object.size=1.8f+float(i)*.45f;
            object.color=i%2?0x4aa9ef:0xe5a45b;
            scene.objects.push_back(object);
        }
        northstarcpp::PhysicsScene physics(scene);
        require(physics.controlled_entity()==character.pawn,
            "Player not possessed at initialization");
        auto const stablePawn=physics.controlled_entity();
        northstarcpp::CameraDirector director;
        require(northstarcpp::gameplay::configure_player_camera(
            director,physics.scene(),stablePawn),
            "Pawn camera rig not configured");
        northstarcpp::CameraTarget target;
        target.eye={0.f,1.7f,-2.f};
        target.yaw=0.f;
        director.update(0.f,target,&physics.scene(),&physics);
        northstarcpp::Viewport viewport(hwnd,false);
        auto gpuCapture=[&]() {
            auto frame=viewport.read_scene_frame_capture();
            if(!frame || !frame->width || !frame->height ||
               frame->rgba.size()!=std::size_t(frame->width)*frame->height*4u)
                throw std::runtime_error("GPU provider RGBA capture unavailable");
            std::vector<std::uint32_t> pixels;
            pixels.reserve(frame->rgba.size()/4);
            for(std::size_t i=0;i<frame->rgba.size();i+=4)
                pixels.push_back(std::uint32_t(frame->rgba[i])|
                    (std::uint32_t(frame->rgba[i+1])<<8)|
                    (std::uint32_t(frame->rgba[i+2])<<16));
            return pixels;
        };
        std::vector<std::uint32_t> pawnImage,viewImage,revertedImage;
        northstarcpp::CameraPose pawnPose{},viewPose{},revertedPose{};
        std::vector<double> gpu_test_draw_wall_ms;
        gpu_test_draw_wall_ms.reserve(115);
        for(int frame=0;frame<115;++frame) {
            auto const dt=1.f/60.f;
            if(frame==35) {
                physics.set_view_target(view.node.id);
                require(physics.controlled_entity()==stablePawn,
                    "View Target switched physical Pawn possession");
                director.set_mode(northstarcpp::CameraMode::free,.35f);
                northstarcpp::CameraPose spectator;
                spectator.position=view.position;
                spectator.yaw=view.node.rotation.y*.01745329252f;
                spectator.pitch=0.f;
                spectator.fov_y_degrees=62.f;
                director.set_free_pose(spectator);
            }
            if(frame==80) {
                physics.reset_view_target();
                require(physics.view_target()==stablePawn,
                    "View Target failed to restore Pawn camera");
                require(northstarcpp::gameplay::configure_player_camera(
                    director,physics.scene(),stablePawn,.35f),
                    "Pawn camera rig did not reconfigure after reset");
            }
            auto const pose=director.update(dt,target,&physics.scene(),&physics);
            if(frame==30||frame==70||frame==110)
                require(viewport.request_scene_frame_capture(),
                    "GPU provider capture ABI unavailable");
            auto const start_draw=std::chrono::steady_clock::now();
            viewport.draw(physics.scene(),-2,true,nullptr,nullptr,false,
                          &pose,nullptr,false);
            gpu_test_draw_wall_ms.push_back(
                std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start_draw).count());
            if(frame==30){pawnPose=pose;pawnImage=gpuCapture();}
            if(frame==70){viewPose=pose;viewImage=gpuCapture();}
            if(frame==110){revertedPose=pose;revertedImage=gpuCapture();}
        }
        // These values measure CPU wall time around draw submission, not GPU timestamps.
        std::sort(gpu_test_draw_wall_ms.begin(),gpu_test_draw_wall_ms.end());
        auto percentile=[&](double p) {
            auto const n=gpu_test_draw_wall_ms.size();
            auto const i=std::min(n-1,static_cast<std::size_t>(std::ceil(p*double(n)))-1);
            return gpu_test_draw_wall_ms[i];
        };
        std::cout<<"Camera draw submission CPU wall p95="<<percentile(.95)
                 <<" ms p99="<<percentile(.99)<<" ms n="
                 <<gpu_test_draw_wall_ms.size()<<"\n";
        require(physics.controlled_entity()==stablePawn,
                "View Target changed Pawn possession");
        require(std::abs(viewPose.position.x-pawnPose.position.x)>2.f,
                "GPU view target did not move the active camera");
        require(std::abs(revertedPose.position.x-pawnPose.position.x)<2.f,
                "GPU view target did not return to Pawn");
        auto changed=[&](std::vector<std::uint32_t> const& a,
                         std::vector<std::uint32_t> const& b){
            if(a.size()!=b.size())return std::size_t{0};
            std::size_t count=0;
            for(std::size_t i=0;i<a.size();i+=4)
                if((a[i]&0x00ffffff)!=(b[i]&0x00ffffff))++count;
            return count;
        };
        const auto distinct=changed(pawnImage,viewImage);
        const auto back=changed(viewImage,revertedImage);
        std::cout<<"GPU capture diagnostic provider="
                 <<viewport.renderer_provider_name()
                 <<" changes="<<distinct<<" return="<<back
                 <<" firstPixels="<<std::hex<<pawnImage[0]<<"/"
                 <<viewImage[0]<<"/"<<revertedImage[0]<<std::dec
                 <<" pawnX="<<pawnPose.position.x<<" viewX="
                 <<viewPose.position.x<<" returnX="<<revertedPose.position.x<<"\n";
        require(distinct>200 && back>200,
            "Native GPU frames did not respond to camera View Target transition");
        std::cout<<"GPU ViewTarget provider="<<viewport.renderer_provider_name()
                 <<" sample_pixel_changes="<<distinct<<" return_changes="<<back
                 <<" pawn_unchanged=true result=PASS\n";
    } catch(std::exception const& error) {
        std::cerr<<"GPU ViewTarget QA failed: "<<error.what()<<"\n";
        status=12;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName,instance);
    return status;
}
