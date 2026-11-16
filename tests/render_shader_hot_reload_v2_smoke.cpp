#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_shader_reload_api.h>
#include <northstarcpp/render_timing_api.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
static void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
static std::vector<unsigned char> read(const char* name) {
    std::ifstream f(std::filesystem::u8path(name),std::ios::binary);
    require(bool(f),"Missing SPIR-V fixture");
    std::vector<unsigned char> result((std::istreambuf_iterator<char>(f)),{});
    require(result.size()>24 && result.size()%4==0,"Invalid fixture size");
    return result;
}
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    bool const deferred=std::strcmp(argv[4],"deferred")==0;
    if(!deferred && std::strcmp(argv[4],"forward")!=0)return 2;
    if(deferred)SetEnvironmentVariableA("NORTHSTARCPP_ENABLE_VULKAN_DEFERRED","1");
    auto const vertex=read(argv[2]),pixel=read(argv[3]);
    HMODULE dll=nullptr;HWND window=nullptr;
    void* renderer=nullptr;NorthStarRenderApi const* api=nullptr;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    static constexpr wchar_t name[]=L"NorthStarShaderReloadV2Smoke";
    bool registered=false;
    auto cleanup=[&]{
        if(renderer && api)api->destroy(renderer);
        if(window)DestroyWindow(window);
        if(registered)UnregisterClassW(name,instance);
        if(dll)FreeLibrary(dll);
    };
    try {
        dll=LoadLibraryW(std::filesystem::u8path(argv[1]).wstring().c_str());
        require(dll!=nullptr,"Cannot open Vulkan provider");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(dll,"northstar_module_query"));
        require(query!=nullptr,"Missing module query");
        auto base=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
                        NORTHSTARCPP_RENDER_ABI);
        require(base && base->api,"Missing render ABI");
        api=static_cast<NorthStarRenderApi const*>(base->api);
        auto m=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
                     NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        auto v=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
                     NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI);
        require(m&&v&&m->api&&v->api,"Missing optional material/shader ABI");
        auto time_module=query(NORTHSTARCPP_MODULE_ABI,
                               NORTHSTARCPP_CAPABILITY_RENDER,
                               NORTHSTARCPP_RENDER_TIMING_MODULE_ABI);
        require(time_module && time_module->api,"GPU timing API missing");
        auto timing=static_cast<NorthStarRenderTimingApi const*>(time_module->api);
        auto materials=static_cast<NorthStarRenderMaterialsApiV2 const*>(m->api);
        auto hot=static_cast<NorthStarRenderShaderReloadApiV2 const*>(v->api);
        require(hot->abi_version==NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_ABI,
                "Wrong Shader Reload API version");
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;
        cls.hInstance=instance;cls.lpszClassName=name;
        require(RegisterClassW(&cls)!=0,"Window class failed");
        registered=true;
        window=CreateWindowW(name,name,WS_OVERLAPPEDWINDOW,20,20,512,384,
                             nullptr,nullptr,instance,nullptr);
        require(window!=nullptr,"Window creation failed");
        ShowWindow(window,SW_SHOW);
        renderer=api->create(window,0);
        require(renderer!=nullptr,"GPU renderer initialization failed");
        NorthStarMaterialVertex points[6]{
            {-2,-2,3,0,0,-1,0,1},{2,-2,3,0,0,-1,1,1},
            {-2,2,3,0,0,-1,0,0},{2,-2,3,0,0,-1,1,1},
            {2,2,3,0,0,-1,1,0},{-2,2,3,0,0,-1,0,0}};
        NorthStarMaterialBatchV2 batch{};
        batch.material.vertices=points;batch.material.vertex_count=6;
        for(float& f:batch.material.base_color)f=1.f;
        batch.material.roughness=.5f;batch.material.unlit=1;
        batch.blend_mode=deferred?0:2;batch.opacity=1.f;batch.alpha_cutoff=.5f;
        require(materials->set_batches_v2(renderer,&batch,1),"Material upload failed");
        char const* keys[]{"assets/materials/live.ymat@ShaderV2"};
        require(hot->set_material_references(renderer,keys,1),"Material key binding failed");
        auto submit=[&](std::uint32_t stage,const auto& bytes) {
            require(hot->queue_shader_binary(renderer,keys[0],stage,
                    NS_SHADER_BINARY_SPIRV,bytes.data(),
                    static_cast<std::uint32_t>(bytes.size()))!=0,
                    "SPIR-V stage was rejected");
        };
        require(!hot->queue_shader_binary(renderer,keys[0],99,
                NS_SHADER_BINARY_SPIRV,vertex.data(),
                static_cast<std::uint32_t>(vertex.size())),
                "Invalid shader stage was accepted");
        submit(NS_SHADER_PROGRAM_VERTEX,vertex);
        submit(deferred?NS_SHADER_PROGRAM_FRAGMENT_GBUFFER:
                        NS_SHADER_PROGRAM_FRAGMENT_FORWARD,pixel);
        NorthStarRenderFrame frame{};
        frame.selected=-1;frame.camera_fov_y_degrees=70;
        frame.camera_near_clip=.1f;frame.camera_far_clip=250;
        bool ready=false;
        for(unsigned i=0;i<350;++i) {
            require(api->draw(renderer,&frame),"GPU frame during shader compile failed");
            NorthStarShaderReloadStatus status{};
            status.struct_size=sizeof(status);
            require(hot->poll(renderer,keys[0],&status),"Shader status unavailable");
            require(status.state!=NS_SHADER_RELOAD_FAILED,
                    "Asynchronous shader program build failed");
            if(status.state==NS_SHADER_RELOAD_READY &&
               status.installed_revision==status.revision) {
                require(status.revision>=2,"Missing vertex+fragment revisions");
                ready=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(ready,"Shader v2 stage PSO did not install");
        frame.verify_pixels=3;
        require(api->draw(renderer,&frame),"Hot shader GPU readback failed");
        frame.verify_pixels=0;
        std::vector<double> cpu_times,gpu_times;
        cpu_times.reserve(1000);gpu_times.reserve(1000);
        unsigned ready_frames=0,requested=2;
        for(unsigned i=0;i<1000;++i) {
            if(i && i%125==0) {
                submit(NS_SHADER_PROGRAM_VERTEX,vertex);
                submit(deferred?NS_SHADER_PROGRAM_FRAGMENT_GBUFFER:
                                NS_SHADER_PROGRAM_FRAGMENT_FORWARD,pixel);
                requested+=2;
            }
            auto start=std::chrono::steady_clock::now();
            require(api->draw(renderer,&frame),"GPU frame lost during 1000-frame hot reload");
            auto end=std::chrono::steady_clock::now();
            cpu_times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
            NorthStarRenderTimingStats gpu{};
            if(timing->read(renderer,&gpu) && gpu.valid &&
               gpu.total_gpu_ms>0 && std::isfinite(gpu.total_gpu_ms))
                gpu_times.push_back(gpu.total_gpu_ms);
            NorthStarShaderReloadStatus current{};
            current.struct_size=sizeof(current);
            require(hot->poll(renderer,keys[0],&current),"Shader status poll failed");
            require(current.state!=NS_SHADER_RELOAD_FAILED,"PSO compilation failed during stress");
            if(current.state==NS_SHADER_RELOAD_READY &&
               current.installed_revision==current.revision)++ready_frames;
        }
        NorthStarShaderReloadStatus final{};
        final.struct_size=sizeof(final);
        require(hot->poll(renderer,keys[0],&final) &&
                final.state==NS_SHADER_RELOAD_READY &&
                final.installed_revision==final.revision &&
                final.installed_revision>=requested,
                "Last shader revision was not installed");
        require(gpu_times.size()>=950,"Too few GPU timestamp samples");
        auto pct=[](std::vector<double> values,double percentile){
            std::sort(values.begin(),values.end());
            auto pos=(values.size()-1)*percentile;
            auto lo=static_cast<std::size_t>(pos);
            auto hi=std::min(lo+1,values.size()-1);
            return values[lo]+(values[hi]-values[lo])*(pos-lo);
        };
        std::cout<<"{\"status\":\"passed\",\"provider\":\"vulkan\",\"mode\":\""
                 <<(deferred?"deferred":"forward")<<"\",\"frames\":1000"
                 <<",\"gpu_samples\":"<<gpu_times.size()
                 <<",\"last_revision\":"<<final.installed_revision
                 <<",\"ready_frames\":"<<ready_frames
                 <<",\"cpu_p50_ms\":"<<pct(cpu_times,.5)
                 <<",\"cpu_p95_ms\":"<<pct(cpu_times,.95)
                 <<",\"cpu_p99_ms\":"<<pct(cpu_times,.99)
                 <<",\"gpu_p50_ms\":"<<pct(gpu_times,.5)
                 <<",\"gpu_p95_ms\":"<<pct(gpu_times,.95)
                 <<",\"gpu_p99_ms\":"<<pct(gpu_times,.99)
                 <<"}\n";
        cleanup();return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Vulkan Shader Reload v2 FAIL: "<<e.what()<<"\n";
        cleanup();return 1;
    }
}
