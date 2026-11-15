// Real Win32 Vulkan GPU provider smoke, ABI 5, GPU readback and resizing.
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_timing_api.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#define REQUIRE(value) do {if(!(value))throw std::runtime_error(#value);} while(false)
int main(int argc,char** argv) {
    if(argc<2||argc>4)return 2;
    bool stress=false,gpu_profile=false;
    for(int i=2;i<argc;++i) {
        if(std::strcmp(argv[i],"--stress")==0)stress=true;
        else if(std::strcmp(argv[i],"--gpu-profile")==0)gpu_profile=true;
        else return 2;
    }
    if(gpu_profile&&!stress)return 2;
    HMODULE dll=nullptr;
    HWND window=nullptr;
    const NorthStarModule* module=nullptr;
    const NorthStarRenderApi* render=nullptr;
    const NorthStarRenderTrianglesApi* geometry=nullptr;
    const NorthStarRenderTimingApi* timing=nullptr;
    void* backend=nullptr;
    const wchar_t* cls=L"NorthStarVulkanSmokeWindow";
    auto cleanup=[&] {
        if(backend&&render)render->destroy(backend);
        backend=nullptr;
        if(window)DestroyWindow(window);
        window=nullptr;
        UnregisterClassW(cls,GetModuleHandleW(nullptr));
        if(dll)FreeLibrary(dll);
        dll=nullptr;
    };
    try {
        dll=LoadLibraryW(std::filesystem::u8path(argv[1]).wstring().c_str());
        REQUIRE(dll!=nullptr);
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(dll,"northstar_module_query"));
        REQUIRE(query!=nullptr);
        module=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI);
        REQUIRE(module!=nullptr);
        REQUIRE(module->api_size==sizeof(NorthStarRenderApi));
        REQUIRE(std::strcmp(module->provider,"vulkan")==0);
        REQUIRE(query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI+1)==nullptr);
        render=static_cast<const NorthStarRenderApi*>(module->api);
        REQUIRE(render&&render->create&&render->draw&&render->destroy&&render->set_sky);
        auto query_triangles=reinterpret_cast<NorthStarRenderGetTrianglesApi>(
            GetProcAddress(dll,"northstar_render_get_triangles_api"));
        REQUIRE(query_triangles!=nullptr);
        geometry=query_triangles(NORTHSTARCPP_RENDER_TRIANGLES_ABI);
        REQUIRE(geometry&&geometry->set_triangles);
        REQUIRE(query_triangles(999)==nullptr);
        auto query_timing=reinterpret_cast<NorthStarRenderGetTimingApi>(
            GetProcAddress(dll,"northstar_render_get_timing_api"));
        REQUIRE(query_timing&&query_timing(999)==nullptr);
        timing=query_timing(NORTHSTARCPP_RENDER_TIMING_ABI);
        REQUIRE(timing&&timing->read);
        REQUIRE(query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            NORTHSTARCPP_RENDER_TIMING_MODULE_ABI)!=nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc=DefWindowProcW;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=cls;
        REQUIRE(RegisterClassW(&wc)!=0);
        window=CreateWindowW(cls,L"Vulkan render QA",WS_OVERLAPPEDWINDOW,
            100,100,640,480,nullptr,nullptr,wc.hInstance,nullptr);
        REQUIRE(window!=nullptr);
        ShowWindow(window,SW_SHOW);
        backend=render->create(window,0);
        if(!backend) {
            auto reason=module->last_error?module->last_error():"unknown";
            std::fprintf(stderr,"Vulkan GPU provider unavailable: %s\n",reason);
            // Only skip a genuinely absent GPU/feature; any other failure
            // is a regression and MUST fail rather than be silently hidden.
            std::string why=reason?reason:"";
            const bool unsupported=why.find("unavailable")!=std::string::npos||
                why.find("No Vulkan physical devices")!=std::string::npos;
            cleanup();
            return unsupported?77:1;
        }
        NorthStarRenderTriangleVertex v[]={
            {-1.5f,-1.1f,5.f,0xff4444u},
            { 1.5f,-1.1f,5.f,0x44ff44u},
            { 0.f, 1.3f,5.f,0x4444ffu}
        };
        REQUIRE(!geometry->set_triangles(backend,v,2));
        REQUIRE(geometry->set_triangles(backend,v,3));
        v[0].x=9999.f; // source lifetime must not affect GPU.
        NorthStarRenderFrame frame{};
        frame.camera_fov_y_degrees=70.f;
        frame.camera_near_clip=.1f;
        frame.camera_far_clip=250.f;
        frame.verify_pixels=1;
        REQUIRE(render->draw(backend,&frame));
        NorthStarRenderCube cube{};
        cube.z=7.f;cube.size=1.7f;cube.rgb=0xffee55u;
        cube.rotation[0]=cube.rotation[4]=cube.rotation[8]=1.f;
        frame.cubes=&cube;frame.count=1;
        REQUIRE(render->draw(backend,&frame));
        frame.verify_pixels=2;
        REQUIRE(!render->draw(backend,&frame)); // Explicitly unsupported UV QA.
        frame.verify_pixels=1;
        REQUIRE(render->set_sky(backend,nullptr));
        SetWindowPos(window,nullptr,100,100,460,320,SWP_NOZORDER|SWP_NOACTIVATE);
        REQUIRE(render->draw(backend,&frame)); // Resize + recreate swapchain.
        if(stress) {
            frame.verify_pixels=0;
            std::vector<double> samples;
            samples.reserve(1000);
            for(unsigned i=0;i<1000;++i) {
                frame.camera_yaw=0.12f*static_cast<float>(i%20)/20.f;
                REQUIRE(render->draw(backend,&frame));
                if(gpu_profile) {
                    NorthStarRenderTimingStats gpu{};
                    REQUIRE(timing->read(backend,&gpu));
                    REQUIRE(gpu.valid&&gpu.gpu_timestamp_frequency_hz>0);
                    REQUIRE(std::isfinite(gpu.total_gpu_ms)&&gpu.total_gpu_ms>=0);
                    REQUIRE(std::abs(gpu.total_gpu_ms-
                        (gpu.opaque_gpu_ms+gpu.deferred_gpu_ms+
                         gpu.postfx_gpu_ms+gpu.hud_gpu_ms))<.001);
                    samples.push_back(gpu.total_gpu_ms);
                }
            }
            if(gpu_profile) {
                REQUIRE(samples.size()==1000);
                std::sort(samples.begin(),samples.end());
                std::printf("Vulkan 1000 GPU frames: p50 %.4f p95 %.4f p99 %.4f ms\n",
                    samples[499],samples[949],samples[989]);
            }
        }
        REQUIRE(geometry->set_triangles(backend,nullptr,0));
        frame.cubes=nullptr;frame.count=0;frame.verify_pixels=1;
        REQUIRE(render->draw(backend,&frame)); // Pure surface clear/readback.
        cleanup();
        std::puts(stress?"Vulkan 1000-frame GPU stress passed":
                         "Vulkan real GPU present/geometry/resize/readback passed");
        return 0;
    }catch(std::exception const& e) {
        std::fprintf(stderr,"Vulkan smoke failure: %s; provider: %s\n",e.what(),
            module&&module->last_error?module->last_error():"unknown");
        cleanup();return 1;
    }
}
