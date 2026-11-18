// Real Win32 Vulkan GPU provider smoke, ABI 5, GPU readback and resizing.
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_postfx_api.h>
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
        // Occlusion test submits near/far overlapping geometry in BOTH
        // orders. Without a real depth attachment, the second test would
        // be red (far triangle drawn last), which must be rejected.
        NorthStarRenderTriangleVertex depth_triangles[]{
            {-2.f,-2.f,6.f,0xff0000u},{2.f,-2.f,6.f,0xff0000u},
            {0.f,2.f,6.f,0xff0000u},
            {-1.f,-1.f,3.f,0x00ff00u},{1.f,-1.f,3.f,0x00ff00u},
            {0.f,1.f,3.f,0x00ff00u}};
        frame.cubes=nullptr;frame.count=0;frame.verify_pixels=4;
        REQUIRE(geometry->set_triangles(backend,depth_triangles,6));
        REQUIRE(render->draw(backend,&frame)); // far then near
        for(int i=0;i<3;++i)
            std::swap(depth_triangles[i],depth_triangles[i+3]);
        REQUIRE(geometry->set_triangles(backend,depth_triangles,6));
        REQUIRE(render->draw(backend,&frame)); // near then far
        // Near-plane regression: two vertices are in front of the camera,
        // one lies behind it. Vulkan must clip the visible polygon instead
        // of dropping all three vertices (the old floor-blinking bug).
        NorthStarRenderTriangleVertex crossing[]{
            {-2.f,-2.f,2.f,0x00ff00u},
            { 2.f,-2.f,2.f,0x00ff00u},
            { 0.f, 2.f,-.5f,0x00ff00u}};
        REQUIRE(geometry->set_triangles(backend,crossing,3));
        frame.verify_pixels=9;
        REQUIRE(render->draw(backend,&frame));
        // Test exactly the authored 50x50m bottom-anchored Jolt platform.
        // A steep-down camera necessarily crosses its top face at z=0.1.
        REQUIRE(geometry->set_triangles(backend,nullptr,0));
        NorthStarRenderCube floor{};
        floor.x=0.f;floor.y=-50.f;floor.z=0.f;
        floor.size=50.f;floor.rgb=0x00ff00u;
        floor.rotation[0]=floor.rotation[4]=floor.rotation[8]=1.f;
        frame.cubes=&floor;frame.count=1;
        frame.selected=-1; // No editor-selection yellow tint on floor.
        frame.camera_y=1.6f;frame.camera_pitch=-.30f;
        REQUIRE(render->draw(backend,&frame));
        // Walk across the slab for 120 GPU frames. The center pixel must
        // never flicker back to sky, even when the near plane cuts a different
        // pair of triangle edges in consecutive frames.
        for(unsigned step=0;step<120;++step) {
            frame.camera_x=-4.f+8.f*float(step)/119.f;
            frame.camera_z=-6.f+12.f*float(step)/119.f;
            frame.camera_yaw=std::sin(float(step)*.04f)*.12f;
            REQUIRE(render->draw(backend,&frame));
        }
        frame.camera_x=frame.camera_z=frame.camera_yaw=0.f;
        frame.camera_y=0.f;frame.camera_pitch=0.f;
        REQUIRE(geometry->set_triangles(backend,v,3));
        frame.cubes=&cube;frame.count=1;
        frame.verify_pixels=2;
        REQUIRE(!render->draw(backend,&frame)); // Explicitly unsupported UV QA.
        frame.verify_pixels=1;
        REQUIRE(render->set_sky(backend,nullptr));
        SetWindowPos(window,nullptr,100,100,460,320,SWP_NOZORDER|SWP_NOACTIVATE);
        REQUIRE(render->draw(backend,&frame)); // Resize + recreate swapchain.
        // Exercise non-neutral scene Filmic grading before drawing the HUD.
        char hdr_flag[8]{};
        const bool test_hdr=GetEnvironmentVariableA(
            "NORTHSTARCPP_ENABLE_VULKAN_HDR",
            hdr_flag,sizeof(hdr_flag))>0 && hdr_flag[0]=='1';
        if(test_hdr) {
            auto* extension=query(NORTHSTARCPP_MODULE_ABI,
                NORTHSTARCPP_CAPABILITY_RENDER,
                NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI);
            REQUIRE(extension && extension->api_size>=
                    sizeof(NorthStarRenderPostFxApi));
            const auto* api=static_cast<const NorthStarRenderPostFxApi*>(
                extension->api);
            REQUIRE(api&&api->set_settings&&api->get_settings);
            NorthStarRenderPostFxSettings settings{
                sizeof(NorthStarRenderPostFxSettings),.65f,1.05f,
                .93f,.12f,1.5f,.015f,0u};
            REQUIRE(api->set_settings(backend,&settings));
            NorthStarRenderPostFxSettings roundtrip{};
            roundtrip.struct_size=sizeof(roundtrip);
            REQUIRE(api->get_settings(backend,&roundtrip));
            REQUIRE(roundtrip.chromatic_pixels==settings.chromatic_pixels);
            REQUIRE(roundtrip.exposure_ev==settings.exposure_ev);
        }
        auto* ui_module=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_UI_MODULE_ABI);
        REQUIRE(ui_module && ui_module->api_size>=sizeof(NorthStarRenderUiApi));
        const auto* ui=static_cast<const NorthStarRenderUiApi*>(ui_module->api);
        REQUIRE(ui && ui->abi_version==NORTHSTARCPP_RENDER_UI_ABI && ui->set_rgba);
        std::vector<uint8_t> overlay(4*4*4);
        for(size_t i=0;i<overlay.size();i+=4) {
            overlay[i]=255;overlay[i+1]=0;overlay[i+2]=0;overlay[i+3]=255;
        }
        REQUIRE(ui->set_rgba(backend,overlay.data(),4,4,16));
        std::fill(overlay.begin(),overlay.end(),0);
        frame.verify_pixels=8;
        REQUIRE(render->draw(backend,&frame));
        REQUIRE(ui->set_rgba(backend,nullptr,0,0,0));
        frame.verify_pixels=1;
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
