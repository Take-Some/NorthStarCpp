// Direct native D3D12 ABI, skydome, HUD, readback and stress test.
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_timing_api.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc<2||argc>5)return 2;
    bool hardware=false,stress=false,gpu_profile=false;
    for(int i=2;i<argc;++i) {
        if(!std::strcmp(argv[i],"--hardware"))hardware=true;
        else if(!std::strcmp(argv[i],"--stress"))stress=true;
        else if(!std::strcmp(argv[i],"--gpu-profile"))gpu_profile=true;
        else return 2;
    }
    if(gpu_profile&&!stress)return 2;
    HMODULE dll=nullptr;
    HWND window=nullptr;
    void* renderer=nullptr;
    const NorthStarRenderApi* api=nullptr;
    const NorthStarModule* module=nullptr;
    const wchar_t* class_name=L"NorthStarD3D12Smoke";
    auto error=[&](){return std::string(module&&module->last_error()?
        module->last_error():"");};
    try {
        dll=LoadLibraryW(std::filesystem::u8path(argv[1]).wstring().c_str());
        require(dll!=nullptr,"Cannot load D3D12 module");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(dll,"northstar_module_query"));
        require(query!=nullptr,"Missing module descriptor");
        module=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI);
        require(module&&module->api_size>=sizeof(NorthStarRenderApi),
            "Render API ABI mismatch");
        require(std::strcmp(module->provider,"d3d12")==0,"Not native D3D12 provider");
        api=static_cast<const NorthStarRenderApi*>(module->api);
        require(api&&api->create&&api->destroy&&api->draw&&api->set_sky,
            "Incomplete render API");
        auto query_mesh=reinterpret_cast<NorthStarRenderGetTrianglesApi>(
            GetProcAddress(dll,"northstar_render_get_triangles_api"));
        require(query_mesh&&query_mesh(2)==nullptr,"Triangle API version mismatch");
        auto mesh_api=query_mesh(NORTHSTARCPP_RENDER_TRIANGLES_ABI);
        require(mesh_api&&mesh_api->set_triangles,"Missing triangle extension");
        auto query_memory=reinterpret_cast<NorthStarRenderGetMemoryApi>(
            GetProcAddress(dll,"northstar_render_get_memory_api"));
        require(query_memory&&query_memory(2)==nullptr,"Memory ABI negotiation failed");
        auto memory_api=query_memory(NORTHSTARCPP_RENDER_MEMORY_ABI);
        require(memory_api&&memory_api->read,"Missing D3D12 GPU memory extension");
        auto get_timing=reinterpret_cast<NorthStarRenderGetTimingApi>(
            GetProcAddress(dll,"northstar_render_get_timing_api"));
        require(get_timing&&get_timing(999)==nullptr,
            "D3D12 timing ABI negotiation failed");
        auto timing_api=get_timing(NORTHSTARCPP_RENDER_TIMING_ABI);
        require(timing_api&&timing_api->read,"Missing D3D12 timing extension");
        auto timing_extension=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_TIMING_MODULE_ABI);
        require(timing_extension&&timing_extension->api_size==sizeof(NorthStarRenderTimingApi),
            "D3D12 timing is not available through plugin module query");
        WNDCLASSW wc{};
        wc.lpfnWndProc=DefWindowProcW;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=class_name;
        require(RegisterClassW(&wc)!=0,"RegisterClassW failed");
        window=CreateWindowW(class_name,L"D3D12 acceptance",WS_OVERLAPPEDWINDOW,
            100,100,640,480,nullptr,nullptr,wc.hInstance,nullptr);
        require(window!=nullptr,"CreateWindowW failed");
        ShowWindow(window,SW_SHOW);
        renderer=api->create(window,hardware?0:1);
        if(!renderer&&hardware) {
            std::fprintf(stderr,"D3D12 hardware unavailable: %s\n",error().c_str());
            DestroyWindow(window);
            UnregisterClassW(class_name,wc.hInstance);
            FreeLibrary(dll);
            return 77; // CTest SKIP_RETURN_CODE
        }
        require(renderer!=nullptr,"Cannot initialize D3D12 WARP");
        NorthStarRenderTriangleVertex mesh[]={
            {-1.f,-1.f,5.f,0xff5555u},{1.f,-1.f,5.f,0x55ff55u},
            {0.f,1.f,5.f,0x5555ffu}};
        require(!mesh_api->set_triangles(renderer,mesh,2),"Accepted invalid triangle topology");
        require(mesh_api->set_triangles(renderer,mesh,3),"Cannot copy world mesh");
        mesh[0].x=1000000.f;
        NorthStarRenderCube cube{};
        cube.x=0.f;cube.y=0.f;cube.z=7.f;cube.size=1.7f;cube.rgb=0xffcc77u;
        cube.rotation[0]=cube.rotation[4]=cube.rotation[8]=1.f;
        NorthStarRenderFrame frame{};
        frame.cubes=&cube;frame.count=1;frame.selected=-1;
        frame.first_person=1;frame.camera_fov_y_degrees=70.f;
        frame.camera_near_clip=.1f;frame.camera_far_clip=250.f;
        frame.player_health=75;frame.player_max_health=100;
        frame.ammunition=8;frame.magazine_capacity=16;
        frame.weapon_recoil=.6f;frame.verify_pixels=1;
        auto draw_checked=[&](const char* stage){
            if(!api->draw(renderer,&frame))
                throw std::runtime_error(std::string(stage)+": "+error());
        };
        draw_checked("Native geometry/HUD/readback");
        NorthStarRenderTimingStats timing{};
        require(timing_api->read(renderer,&timing),"D3D12 timing readback failed");
        require(timing.valid && timing.gpu_timestamp_frequency_hz &&
                std::isfinite(timing.total_gpu_ms) && timing.total_gpu_ms>0.0,
                "D3D12 GPU timestamps invalid");
        double segments=timing.opaque_gpu_ms+timing.deferred_gpu_ms+
                        timing.postfx_gpu_ms+timing.hud_gpu_ms;
        require(std::abs(timing.total_gpu_ms-segments)<0.001,
                "D3D12 GPU timestamp pass breakdown does not add up");
        std::printf("D3D12 GPU timestamp ms: total %.4f opaque %.4f deferred %.4f postfx %.4f hud %.4f\n",
                    timing.total_gpu_ms,timing.opaque_gpu_ms,timing.deferred_gpu_ms,
                    timing.postfx_gpu_ms,timing.hud_gpu_ms);
        // HUD-only validation: no scene geometry or sky may mask a missing HUD.
        frame.cubes=nullptr;frame.count=0;
        require(mesh_api->set_triangles(renderer,nullptr,0),"Cannot clear geometry");
        draw_checked("Native FPS HUD only");
        frame.cubes=&cube;frame.count=1;
        require(mesh_api->set_triangles(renderer,mesh,3),"Cannot restore mesh");
        NorthStarRenderMemoryStats stats{};
        require(memory_api->read(renderer,&stats)&&stats.world_vertex_capacity>0,
                "Invalid D3D12 memory stats");
        NorthStarSkyVertex verts[]={
            {-3.f,-3.f,2.f,0.f,0.f},{3.f,-3.f,2.f,1.f,0.f},
            {3.f,3.f,2.f,1.f,1.f},{-3.f,3.f,2.f,0.f,1.f}};
        uint32_t indices[]={0,1,2,0,2,3};
        std::array<uint8_t,64> pixels{};pixels.fill(255);
        NorthStarSky sky{verts,4,indices,6,pixels.data(),4,4,1.f,1.f,1.f};
        if(!api->set_sky(renderer,&sky))
            throw std::runtime_error("Native D3D12 skydome upload: "+error());
        pixels.fill(0); // Verify ownership after upload.
        require(memory_api->read(renderer,&stats)&&
                stats.sky_geometry_and_texture_bytes>0,
                "Native skydome memory not reported");
        // Sky-only validation: coverage must come from the skydome pass.
        frame.cubes=nullptr;frame.count=0;frame.player_max_health=0;
        require(mesh_api->set_triangles(renderer,nullptr,0),"Cannot clear scene triangles");
        draw_checked("Native skydome only");
        frame.cubes=&cube;frame.count=1;frame.player_max_health=100;
        frame.overlay_paused=1;
        draw_checked("Native skydome and paused HUD readback");
        require(mesh_api->set_triangles(renderer,nullptr,0),"Cannot clear geometry");
        SetWindowPos(window,nullptr,100,100,400,300,SWP_NOZORDER|SWP_NOACTIVATE);
        draw_checked("D3D12 swapchain resize");
        frame.overlay_paused=0;
        if(stress) {
            SetWindowPos(window,nullptr,100,100,176,160,SWP_NOZORDER|SWP_NOACTIVATE);
            std::vector<double> samples;samples.reserve(1000);
            std::vector<double> gpu_total,gpu_opaque,gpu_deferred,gpu_postfx,gpu_hud;
            if(gpu_profile) {
                gpu_total.reserve(1000);gpu_opaque.reserve(1000);
                gpu_deferred.reserve(1000);gpu_postfx.reserve(1000);
                gpu_hud.reserve(1000);
            }
            for(unsigned n=0;n<1000;++n) {
                frame.camera_yaw=.025f*std::sin(float(n)*.03f);
                frame.weapon_recoil=float(n%25)/25.f;
                frame.verify_pixels=n%125==0?1u:0u;
                auto start=std::chrono::steady_clock::now();
                draw_checked("D3D12 stress");
                samples.push_back(std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count());
                if(gpu_profile) {
                    NorthStarRenderTimingStats gpu{};
                    require(timing_api->read(renderer,&gpu) && gpu.valid &&
                        gpu.gpu_timestamp_frequency_hz && std::isfinite(gpu.total_gpu_ms) &&
                        gpu.total_gpu_ms>=0.0,
                        "1000-frame GPU profiling returned an invalid timestamp");
                    double total_parts=gpu.opaque_gpu_ms+gpu.deferred_gpu_ms+
                        gpu.postfx_gpu_ms+gpu.hud_gpu_ms;
                    require(std::abs(gpu.total_gpu_ms-total_parts)<0.001,
                        "GPU timestamp frame subpasses do not sum to total");
                    gpu_total.push_back(gpu.total_gpu_ms);
                    gpu_opaque.push_back(gpu.opaque_gpu_ms);
                    gpu_deferred.push_back(gpu.deferred_gpu_ms);
                    gpu_postfx.push_back(gpu.postfx_gpu_ms);
                    gpu_hud.push_back(gpu.hud_gpu_ms);
                }
            }
            std::sort(samples.begin(),samples.end());
            std::printf("D3D12 %s 1000 frames CPU+Present ms p50 %.3f p95 %.3f p99 %.3f\n",
                hardware?"hardware":"WARP",samples[499],samples[949],samples[989]);
            if(gpu_profile) {
                auto report=[&](const char* name,std::vector<double>& values) {
                    require(values.size()==1000,"GPU frame profile has missing samples");
                    std::sort(values.begin(),values.end());
                    std::printf("D3D12 GPU profile %s: p50 %.4f p95 %.4f p99 %.4f ms\n",
                        name,values[499],values[949],values[989]);
                };
                report("total",gpu_total);
                report("opaque",gpu_opaque);
                report("deferred",gpu_deferred);
                report("postfx",gpu_postfx);
                report("hud",gpu_hud);
            }
        }
        require(api->set_sky(renderer,nullptr)!=0,"Cannot clear skydome");
        require(memory_api->read(renderer,&stats)&&
                stats.sky_geometry_and_texture_bytes==0,
                "Skydome resource memory retained after clear");
        frame.verify_pixels=1;
        draw_checked("World draw after skydome release");
        frame.cubes=nullptr;frame.count=0;frame.player_max_health=0;
        require(!api->draw(renderer,&frame),"Empty frame passed GPU readback validation");
        api->destroy(renderer);renderer=nullptr;
        DestroyWindow(window);window=nullptr;
        UnregisterClassW(class_name,wc.hInstance);
        FreeLibrary(dll);dll=nullptr;
        std::printf("D3D12 %s native render acceptance passed\n",hardware?"hardware":"WARP");
        return 0;
    }catch(std::exception const& e) {
        std::fprintf(stderr,"D3D12 test failure: %s [%s]\n",e.what(),error().c_str());
        if(renderer&&api)api->destroy(renderer);
        if(window)DestroyWindow(window);
        if(dll)FreeLibrary(dll);
        return 1;
    }
}
