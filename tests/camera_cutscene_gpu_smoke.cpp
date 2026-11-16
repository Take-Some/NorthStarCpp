// Actual native GPU cutscene / filmic exposure acceptance (D3D12, Vulkan).
// Uses the same CameraDirector, Viewport and provider PostFX ABI as Player.
// No proprietary runtime code or mock renderer.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/render_postfx_api.h>
#include <northstarcpp/scene.hpp>
#include "viewport.hpp"
#include "qa_scene_fixture.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    if(argc<2)return 2;
    WNDCLASSW wc{};
    wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpfnWndProc=DefWindowProcW;
    wc.lpszClassName=L"NorthStarCutsceneRealGPUQA";
    if(!RegisterClassW(&wc))return 3;
    auto* hwnd=CreateWindowExW(0,wc.lpszClassName,
        L"NorthStar Cinematic GPU QA",WS_OVERLAPPEDWINDOW,
        100,100,960,540,nullptr,nullptr,wc.hInstance,nullptr);
    if(!hwnd)return 4;
    ShowWindow(hwnd,SW_SHOW);
    UpdateWindow(hwnd);
    int code=0;
    try {
        std::ifstream stream(argv[1],std::ios::binary);
        if(!stream)throw std::runtime_error("Cutscene fixture not found");
        std::string data{std::istreambuf_iterator<char>(stream),
                         std::istreambuf_iterator<char>()};
        auto scene_asset=northstarcpp::parse_camera_cutscene(data);
        int frames=420;
        if(argc>2)frames=std::clamp(std::atoi(argv[2]),1,1200);
        northstarcpp::CameraDirector director;
        northstarcpp::CameraTarget target;
        target.eye={0.f,1.6f,0.f};
        director.update(0.f,target);
        director.play_cutscene(std::move(scene_asset),frames>420,0.f,0.f);
        northstarcpp::Scene scene;
        northstarcpp::qa::append(scene,
            {"Center",{0.f,0.f,4.f},2.f,0xfcc884,false,0.f});
        northstarcpp::qa::append(scene,
            {"Left",{-4.f,0.f,6.f},3.f,0x79a9f0,false,0.f});
        northstarcpp::qa::append(scene,
            {"Right",{5.f,0.f,9.f},4.f,0x80d1a0,false,0.f});
        // Explicit renderer: do not silently substitute D3D11 or WARP.
        // Only device initialization may opt out on machines without GPU.
        std::unique_ptr<northstarcpp::Viewport> viewport;
        try {viewport=std::make_unique<northstarcpp::Viewport>(hwnd,false);}
        catch(std::exception const& error) {
            std::cerr<<"SKIP: native GPU provider unavailable: "<<error.what()<<'\n';
            code=77;
        }
        if(viewport) {
            if(!viewport->supports_postfx())
                throw std::runtime_error("Provider missing film look extension");
            auto baseline=viewport->postfx_settings();
            if(!baseline)
                throw std::runtime_error("Unable to read native GPU film look");
            std::vector<double> frame_ms;
            std::vector<double> gpu_ms,gpu_postfx_ms;
            frame_ms.reserve(std::size_t(frames));
            gpu_ms.reserve(std::size_t(frames));
            gpu_postfx_ms.reserve(std::size_t(frames));
            std::size_t lens_changes=0,shots=0;
            std::string previous_shot;
            for(int frame=0;frame<frames;++frame) {
                auto t0=std::chrono::steady_clock::now();
                float const dt=1.f/60.f;
                auto pose=director.update(dt,target,&scene);
                auto const current=director.cutscene_shot();
                if(current!=previous_shot && !current.empty()) {
                    ++shots;previous_shot=current;
                }
                if(director.cutscene_active()) {
                    auto settings=*baseline;
                    settings.exposure_ev=std::clamp(
                        baseline->exposure_ev+director.cinematic_lens().exposure_ev,
                        -5.f,5.f);
                    viewport->set_postfx_settings(settings);
                    auto actual=viewport->postfx_settings();
                    if(!actual || std::abs(actual->exposure_ev-settings.exposure_ev)>.002f)
                        throw std::runtime_error("GPU film exposure update not roundtripped");
                    ++lens_changes;
                } else {
                    viewport->set_postfx_settings(*baseline);
                }
                viewport->draw(scene,-2,true,nullptr,nullptr,false,
                               &pose,nullptr,false);
                auto elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-t0).count();
                frame_ms.push_back(elapsed);
                // Synchronous QA-only GPU timestamps. They are *not* included
                // in the CPU submit distribution above.
                if(auto timing=viewport->gpu_frame_timing()) {
                    gpu_ms.push_back(timing->total_gpu_ms);
                    gpu_postfx_ms.push_back(timing->postfx_gpu_ms);
                }
            }
            viewport->set_postfx_settings(*baseline);
            if(shots<3 || lens_changes<120)
                throw std::runtime_error("GPU scene missed cutscene shots or lens frames");
            if(frames>=1100 && gpu_ms.size()<1000)
                throw std::runtime_error(
                    "Cinematic stress test requires >=1000 real GPU timestamp samples");
            std::sort(frame_ms.begin(),frame_ms.end());
            std::sort(gpu_ms.begin(),gpu_ms.end());
            std::sort(gpu_postfx_ms.begin(),gpu_postfx_ms.end());
            auto quantile=[](std::vector<double> const& samples,double q) {
                if(samples.empty())return -1.;
                std::size_t i=std::size_t((samples.size()-1)*q+.5);
                return samples[std::min(i,samples.size()-1)];
            };
            auto at=[&](double q){return quantile(frame_ms,q);};
            auto actual=viewport->postfx_settings();
            if(!actual || std::abs(actual->exposure_ev-baseline->exposure_ev)>.001f)
                throw std::runtime_error("Film look not restored");
            std::cout<<"GPU cutscene provider="<<viewport->renderer_provider_name()
                     <<" frames="<<frames<<" shots="<<shots
                     <<" lens_updates="<<lens_changes
                     <<" CPU+submit p50="<<at(.5)<<"ms"
                     <<" p95="<<at(.95)<<"ms"
                     <<" p99="<<at(.99)<<"ms"
                     <<" GPU_samples="<<gpu_ms.size()
                     <<" GPU_p50="<<quantile(gpu_ms,.5)<<"ms"
                     <<" GPU_p95="<<quantile(gpu_ms,.95)<<"ms"
                     <<" GPU_p99="<<quantile(gpu_ms,.99)<<"ms"
                     <<" PostFX_GPU_p95="<<quantile(gpu_postfx_ms,.95)<<"ms"
                     <<" result=PASS\n";
        }
    }catch(std::exception const& e) {
        std::cerr<<"GPU cutscene QA failed: "<<e.what()<<'\n';
        code=12;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName,wc.hInstance);
    return code;
}
