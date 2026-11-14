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
        northstarcpp::FpsArena game(scene);
        northstarcpp::CameraDirector camera;
        northstarcpp::CameraTarget target;
        target.eye=game.eye(scene);
        target.ignore_object=game.player_index();
        auto pose=camera.update(0.016f,target,&scene);
        pose.fov_y_degrees=70.f;
        northstarcpp::Viewport output(hwnd,true); // WARP: no GPU required
        // Exercise the real skydome YDD/YMAT/YTD materials through the GPU shader.
        if (argc == 2) {
            northstarcpp::AssetManager assets;
            assets.mount_shared_assets(argv[1]);
            output.set_sky(assets.load_drawable_resources("models/skydome.ydd@skydome_high"));
        }
        output.draw(scene,-2,true,nullptr,&game,false,&pose);
        pose.roll=0.12f;
        pose.fov_y_degrees=100.f;
        output.draw(scene,-2,true,nullptr,&game,false,&pose);
    } catch (std::exception const& e) {
        std::fprintf(stderr,"Camera/D3D11 smoke: %s\n",e.what());
        code=12;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName,instance);
    return code;
}
