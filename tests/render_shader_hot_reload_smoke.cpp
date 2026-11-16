// Real Windows hardware GPU test for native Vulkan shader reload.
// Usage: northstarcpp_render_shader_hot_reload_smoke <vulkan-provider.dll> <green-frag.spv>
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_shader_reload_api.h>
#include <northstarcpp/render_materials_api.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
void require(bool ok,std::string const& message) {
    if(!ok)throw std::runtime_error(message);
}
template<class T>
T const* query_api(NorthStarModuleQuery fn,std::uint32_t moduleAbi,
                    std::uint32_t tableAbi) {
    auto const* m=fn(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,moduleAbi);
    if(!m || m->api_size<sizeof(T) || !m->api)return nullptr;
    auto const* api=static_cast<T const*>(m->api);
    return api->abi_version==tableAbi?api:nullptr;
}
}
int main(int argc,char** argv) {
    if(argc!=3) {std::cerr<<"Expected <DLL> <SPIRV>\n";return 2;}
    HMODULE dll=nullptr;
    HWND hwnd=nullptr;
    NorthStarRenderApi const* api=nullptr;
    void* renderer=nullptr;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    constexpr wchar_t const* name=L"NorthStarShaderReloadAcceptance";
    bool registered=false;
    auto cleanup=[&] {
        if(renderer && api && api->destroy)api->destroy(renderer);
        renderer=nullptr;
        if(hwnd)DestroyWindow(hwnd);
        hwnd=nullptr;
        if(registered)UnregisterClassW(name,instance);
        if(dll)FreeLibrary(dll);
        dll=nullptr;
    };
    try {
        std::ifstream input(std::filesystem::u8path(argv[2]),std::ios::binary);
        require(bool(input),"Cannot read test SPIR-V");
        std::vector<std::uint8_t> data(
            (std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        require(data.size()>24 && data.size()%4==0,
                "Test SPIR-V size invalid");
        std::vector<std::uint32_t> spirv(data.size()/4);
        std::memcpy(spirv.data(),data.data(),data.size());
        dll=LoadLibraryW(std::filesystem::u8path(argv[1]).wstring().c_str());
        require(dll!=nullptr,"Cannot load Vulkan provider");
        auto fn=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(dll,"northstar_module_query"));
        require(fn!=nullptr,"Provider lacks module query");
        auto const* base=fn(NORTHSTARCPP_MODULE_ABI,
                             NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI);
        require(base && base->api,"Missing Render ABI 5");
        api=static_cast<NorthStarRenderApi const*>(base->api);
        auto* mat=query_api<NorthStarRenderMaterialsApi>(fn,
            NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI,NORTHSTARCPP_RENDER_MATERIALS_ABI);
        auto* hot=query_api<NorthStarRenderShaderReloadApi>(fn,
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI,
            NORTHSTARCPP_RENDER_SHADER_RELOAD_ABI);
        require(mat && mat->set_batches,"Missing material API");
        require(hot && hot->struct_size>=sizeof(*hot) &&
                hot->set_material_references && hot->queue_fragment_spirv && hot->poll,
                "Missing Shader Reload ABI v1");
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;
        wc.hInstance=instance;wc.lpszClassName=name;
        require(RegisterClassW(&wc)!=0,"Cannot register GPU test window");
        registered=true;
        hwnd=CreateWindowW(name,L"Shader Reload GPU Smoke",WS_OVERLAPPEDWINDOW,
            200,200,480,360,nullptr,nullptr,instance,nullptr);
        require(hwnd!=nullptr,"Cannot create test HWND");
        ShowWindow(hwnd,SW_SHOW);
        renderer=api->create(hwnd,0);
        require(renderer!=nullptr,"Cannot create hardware renderer");
        NorthStarMaterialVertex quad[6]{
            {-2,-2,3,0,0,-1,0,1},{2,-2,3,0,0,-1,1,1},
            {-2,2,3,0,0,-1,0,0},{2,-2,3,0,0,-1,1,1},
            {2,2,3,0,0,-1,1,0},{-2,2,3,0,0,-1,0,0}
        };
        NorthStarMaterialBatch batch{};
        batch.vertices=quad;batch.vertex_count=6;
        batch.unlit=1;batch.roughness=.7f;
        batch.base_color[0]=batch.base_color[1]=
            batch.base_color[2]=batch.base_color[3]=1.f;
        require(mat->set_batches(renderer,&batch,1),"Material batch rejected");
        char const* keys[]{"assets/materials/live_test.ymat@Green"};
        require(hot->set_material_references(renderer,keys,1),
                "Material reference association rejected");
        NorthStarRenderFrame frame{};
        frame.camera_fov_y_degrees=70.f;
        frame.camera_near_clip=.1f;frame.camera_far_clip=250.f;
        frame.selected=-1;
        require(api->draw(renderer,&frame),"Initial GPU frame failed");

        // Bad SPIR-V must leave the old working pipeline intact.
        std::uint32_t bad[]{0x00000000,0,0,0,0,0};
        require(!hot->queue_fragment_spirv(renderer,keys[0],bad,6),
                "Invalid SPIR-V unexpectedly accepted");
        require(api->draw(renderer,&frame),"GPU failed after rejected SPIR-V");
        require(!hot->queue_fragment_spirv(renderer,"unknown.material",spirv.data(),
                                           std::uint32_t(spirv.size())),
                "Unbound material unexpectedly accepted");
        require(hot->queue_fragment_spirv(renderer,keys[0],spirv.data(),
                                          std::uint32_t(spirv.size())),
                "Valid SPIR-V rejected");

        NorthStarShaderReloadStatus status{};
        status.struct_size=sizeof(status);
        require(hot->poll(renderer,keys[0],&status),"Initial shader status failed");
        const auto wanted=status.revision;
        require(wanted>0 && status.state!=NS_SHADER_RELOAD_UNKNOWN,
                "Shader did not get a revision");
        auto const deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(20);
        bool ready=false;
        while(std::chrono::steady_clock::now()<deadline) {
            require(api->draw(renderer,&frame),
                    std::string("GPU frame failed while hot building: ")+
                    (base->last_error()?base->last_error():""));
            status.struct_size=sizeof(status);
            require(hot->poll(renderer,keys[0],&status),"Shader status lost");
            if(status.state==NS_SHADER_RELOAD_READY) {
                ready=true;break;
            }
            require(status.state!=NS_SHADER_RELOAD_FAILED,
                    std::string("PSO compilation failed: ")+status.diagnostic);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(ready && status.installed_revision==wanted,
                "New PSO was not installed before deadline");
        frame.verify_pixels=3; // Vulkan readback must be GREEN, not old PBR.
        require(api->draw(renderer,&frame),
                std::string("Hot-loaded green pixel verification failed: ")+
                (base->last_error()?base->last_error():""));
        frame.verify_pixels=0;
        for(int i=0;i<15;++i)
            require(api->draw(renderer,&frame),"Frame stability after hot swap failed");
        // A second save must install a new revision without destroying the
        // current working shader while its replacement is compiled.
        require(hot->queue_fragment_spirv(renderer,keys[0],spirv.data(),
                                          std::uint32_t(spirv.size())),
                "Second revision rejected");
        status.struct_size=sizeof(status);
        require(hot->poll(renderer,keys[0],&status) &&
                status.revision>wanted,"Second revision not issued");
        auto const second=status.revision;
        auto const secondDeadline=std::chrono::steady_clock::now()+
                                  std::chrono::seconds(20);
        bool secondReady=false;
        while(std::chrono::steady_clock::now()<secondDeadline) {
            require(api->draw(renderer,&frame),
                    "GPU frame lost while replacing an installed PSO");
            status.struct_size=sizeof(status);
            require(hot->poll(renderer,keys[0],&status),"Revision status lost");
            if(status.state==NS_SHADER_RELOAD_READY &&
               status.installed_revision==second) {
                secondReady=true;break;
            }
            require(status.state!=NS_SHADER_RELOAD_FAILED,
                    std::string("Replacement pipeline failed: ")+status.diagnostic);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(secondReady,"Second PSO never installed");
        frame.verify_pixels=3;
        require(api->draw(renderer,&frame),"Second GPU PSO produced wrong output");
        frame.verify_pixels=0;
        // Trigger swapchain recreation to validate GPU-safe retirement and
        // automatic recompilation of retained shader source.
        SetWindowPos(hwnd,nullptr,0,0,600,440,SWP_NOMOVE|SWP_NOZORDER);
        require(api->draw(renderer,&frame),"Resize interrupted GPU renderer");
        const auto resizeDeadline=std::chrono::steady_clock::now()+
                                  std::chrono::seconds(20);
        bool restored=false;
        while(std::chrono::steady_clock::now()<resizeDeadline) {
            require(api->draw(renderer,&frame),"Frame lost after swapchain resize");
            status.struct_size=sizeof(status);
            require(hot->poll(renderer,keys[0],&status),"Resize lost hot shader status");
            if(status.state==NS_SHADER_RELOAD_READY &&
               status.installed_revision==second){restored=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(restored,"Hot shader failed to recover after swapchain recreation");
        frame.verify_pixels=3;
        require(api->draw(renderer,&frame),"Restored GPU shader differs after resize");
        std::cout<<"Vulkan Shader Reload hardware PASS (revisions "<<wanted
                 <<" -> "<<second<<", GPU readback, resize recovery, no lost frames)\n";
        cleanup();return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Shader Reload hardware FAIL: "<<e.what()<<"\n";
        cleanup();return 1;
    }
}
