// NorthStarCpp — camera override smoke through native engine.render D3D11.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/assets.hpp>
#include "viewport.hpp"
#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    HINSTANCE const instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=instance;
    wc.lpszClassName=L"NorthStarCameraD3D11Smoke";
    if (!RegisterClassW(&wc)) return 2;
    HWND const hwnd=CreateWindowExW(0,wc.lpszClassName,L"Camera Renderer Smoke",
        WS_OVERLAPPEDWINDOW,80,80,800,600,nullptr,nullptr,instance,nullptr);
    if (!hwnd) return 3;
    int code=0;
    try {
        ShowWindow(hwnd,SW_SHOW);
        UpdateWindow(hwnd);
        northstarcpp::Scene scene;
        scene.objects.push_back({"Player",{0.f,0.f,0.f},1.5f,0xffffff,true,2.f});
        scene.objects.push_back({"Target Center",{0.f,1.f,5.f},1.5f,0x3dfe90,false,0.f});
        if(argc>=4) {
            // Real NSCN/NEF8 map remains untouched; exercise all authored
            // objects with their actual actor/layer surface associations.
            scene=northstarcpp::load_scene(std::filesystem::u8path(argv[3]));
            if(scene.objects.size()<600)
                throw std::runtime_error("FPS world texture scene is unexpectedly small");
        } else if(argc>=3) {
            scene.objects.push_back({"Deck Test",{0.f,-0.5f,5.f},8.f,0xffffff,false,0.f});
            scene.objects.push_back({"Rock Cliff",{8.f,-0.5f,10.f},6.f,0xffffff,false,0.f});
            scene.objects.push_back({"Forest Floor",{-8.f,-0.5f,10.f},6.f,0xffffff,false,0.f});
        }
        northstarcpp::FpsArena game(scene);
        northstarcpp::CameraDirector camera;
        northstarcpp::CameraTarget target;
        target.eye=game.eye(scene);
        target.ignore_object=game.player_index();
        auto pose=camera.update(0.016f,target,&scene);
        pose.fov_y_degrees=70.f;
        if(argc>=3) {
            // Frame the authored terrain directly; the default FPS camera
            // can face away from the synthetic surface QA proxies.
            pose.position={0.f,6.f,-15.f};
            pose.yaw=0.f;
            pose.pitch=-0.16f;
        }
        northstarcpp::Viewport output(hwnd,true); // WARP: no GPU required
        if(argc>=3)
            output.set_asset_sources(std::filesystem::u8path(argv[2]));
        // Exercise the real skydome YDD/YMAT/YTD materials through the GPU shader.
        if (argc == 2) {
            northstarcpp::AssetManager assets;
            assets.mount_shared_assets(argv[1]);
            output.set_sky(assets.load_drawable_resources("models/skydome.ydd@skydome_high"));
        }
        // Full-map pass validates rendering/residency without assuming
        // the authoring camera is aimed at a particular World Partition cell.
        bool const pixelVerification=argc<4;
        output.draw(scene,-2,pixelVerification,nullptr,
                    argc>=3?nullptr:&game,false,&pose);
        pose.roll=0.12f;
        pose.fov_y_degrees=100.f;
        output.draw(scene,-2,pixelVerification,nullptr,
                    argc>=3?nullptr:&game,false,&pose);
    } catch (std::exception const& e) {
        std::fprintf(stderr,"Camera/D3D11 smoke: %s\n",e.what());
        code=12;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName,instance);
    return code;
}
