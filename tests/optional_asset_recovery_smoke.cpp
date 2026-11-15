// Optional resource recovery: one malformed mesh/material cannot abort a frame.
// The WARP Viewport draws alongside an authentic native YMAT from sharedAssets.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/camera.hpp>
#include "viewport.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs=std::filesystem;

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    auto const shared=fs::u8path(argv[1]);
    auto const temp=fs::temp_directory_path()/
        ("northstar-invalid-assets-"+std::to_string(GetCurrentProcessId()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() {std::error_code ec;fs::remove_all(path,ec);}
    } cleanup{temp};
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc=DefWindowProcW;
    cls.hInstance=instance;
    cls.lpszClassName=L"NorthStarOptionalAssetRecoverySmoke";
    if(!RegisterClassW(&cls))return 3;
    HWND hwnd=CreateWindowExW(0,cls.lpszClassName,L"Optional asset recovery",
        WS_OVERLAPPEDWINDOW,200,100,640,400,nullptr,nullptr,instance,nullptr);
    if(!hwnd){UnregisterClassW(cls.lpszClassName,instance);return 4;}
    int code=0;
    try {
        fs::create_directories(temp/"assets"/"models");
        fs::create_directories(temp/"assets"/"materials");
        fs::create_directories(temp/"assets"/"textures");
        {
            std::ofstream out(temp/"assets"/"textures"/"world_surfaces_runtime.ytd",
                std::ios::binary);
            out<<"corrupted native YTD";
        }
        {
            std::ofstream out(temp/"assets"/"models"/"corrupt.ydd",
                std::ios::binary);
            out<<"this is not a valid NEF8 model";
        }
        {
            std::ofstream out(temp/"assets"/"materials"/"corrupt.ymat",
                std::ios::binary);
            out<<"not a valid YMAT library";
        }
        northstarcpp::Scene scene;
        scene.version=3;
        auto add=[&](char const* name,float x,std::uint64_t id,
                     std::string asset,std::string material) {
            northstarcpp::SceneObject item;
            item.name=name;
            item.node.id=id;
            item.node.asset=std::move(asset);
            item.position={x,0.f,5.f};
            item.size=2.f;
            scene.objects.push_back(std::move(item));
            if(!material.empty()) {
                northstarcpp::SceneComponentBlob binding;
                binding.entity_id=id;
                binding.type="northstarcpp.editor.material";
                binding.version=1;
                binding.payload=std::move(material);
                scene.components.push_back(std::move(binding));
            }
        };
        add("Corrupt material",-4.f,1,"","materials/corrupt.ymat");
        add("Good material",0.f,2,"","materials/skydome.ymat@sky_cloud_clear");
        add("Corrupt mesh",4.f,3,"models/corrupt.ydd@broken","");
        add("Good cube",8.f,4,"","");
        ShowWindow(hwnd,SW_SHOW);
        UpdateWindow(hwnd);
        northstarcpp::Viewport viewport(hwnd,true);
        viewport.set_asset_sources(temp,{}, {},shared);
        northstarcpp::CameraPose camera{};
        camera.position={0.f,2.f,-8.f};
        camera.yaw=0.f;
        camera.pitch=0.f;
        camera.fov_y_degrees=85.f;
        for(int i=0;i<3;++i)
            viewport.draw(scene,-1,false,nullptr,nullptr,false,&camera);
        // The broken resources are not transformed into placeholder cubes.
        // Successful draw confirms the other objects and provider stayed alive.
        std::puts("Optional malformed mesh and YMAT: survived 3 WARP frames");
    }catch(std::exception const& e) {
        std::fprintf(stderr,"Optional asset recovery failed: %s\n",e.what());
        code=8;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(cls.lpszClassName,instance);
    return code;
}
