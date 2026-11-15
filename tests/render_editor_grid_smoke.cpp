// Dedicated WARP/Hardware analytic editor-grid GPU acceptance.
// Uses real DXGI backbuffer readback in both D3D11 and D3D12 providers.
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_editor_grid_api.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <chrono>
static void require(bool ok,char const* message) {
    if(!ok)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc<2||argc>3)return 2;
    bool const hardware=argc==3 && std::strcmp(argv[2],"--hardware")==0;
    if(argc==3&&!hardware)return 2;
    HMODULE library=nullptr;
    HWND window=nullptr;
    void* renderer=nullptr;
    NorthStarRenderApi const* renderer_api=nullptr;
    NorthStarModule const* module=nullptr;
    auto const class_name=L"NorthStarEditorAnalyticGridSmoke";
    auto const instance=GetModuleHandleW(nullptr);
    auto detail=[&]() -> std::string {
        return module&&module->last_error()?
            module->last_error():"No provider diagnostic";
    };
    try {
        library=LoadLibraryW(std::filesystem::u8path(argv[1]).wstring().c_str());
        require(library!=nullptr,"Missing render provider DLL");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(library,"northstar_module_query"));
        require(query!=nullptr,"Missing module query");
        module=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
                     NORTHSTARCPP_RENDER_ABI);
        require(module && module->api_size>=sizeof(NorthStarRenderApi),
                "Missing render ABI");
        renderer_api=static_cast<NorthStarRenderApi const*>(module->api);
        auto extension=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,
            NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI);
        require(extension&&extension->api_size==sizeof(NorthStarRenderEditorGridApi),
                "Missing independently replaceable grid extension ABI");
        auto const* grid=static_cast<NorthStarRenderEditorGridApi const*>(
            extension->api);
        require(grid->struct_size>=sizeof(*grid) &&
                grid->abi_version==NORTHSTARCPP_RENDER_EDITOR_GRID_ABI &&
                grid->set_grid &&
                (grid->anti_aliasing_modes&3u)==3u,
                "Grid provider lacks derivative AA/temporal reprojection");
        auto get_grid=reinterpret_cast<NorthStarRenderGetEditorGridApi>(
            GetProcAddress(library,"northstar_render_get_editor_grid_api"));
        require(get_grid && get_grid(999)==nullptr &&
                get_grid(NORTHSTARCPP_RENDER_EDITOR_GRID_ABI)==grid,
                "Grid native export/version negotiation mismatch");
        WNDCLASSW wc{};
        wc.lpfnWndProc=DefWindowProcW;
        wc.hInstance=instance;wc.lpszClassName=class_name;
        require(RegisterClassW(&wc)!=0,"Cannot register grid QA window");
        window=CreateWindowW(class_name,L"Grid TAA GPU QA",WS_OVERLAPPEDWINDOW,
                             120,120,360,270,nullptr,nullptr,instance,nullptr);
        require(window!=nullptr,"Cannot open grid QA window");
        ShowWindow(window,SW_SHOW);
        renderer=renderer_api->create(window,hardware?0u:1u);
        if(!renderer && hardware) {
            std::puts("Hardware GPU grid unavailable: skipped");
            DestroyWindow(window);window=nullptr;
            UnregisterClassW(class_name,instance);
            FreeLibrary(library);library=nullptr;
            return 77;
        }
        if(!renderer)throw std::runtime_error("Create GPU grid: "+detail());
        NorthStarRenderEditorGridSettings options{};
        options.abi_version=NORTHSTARCPP_RENDER_EDITOR_GRID_ABI;
        options.enabled=1;
        options.minor_step=2.f;
        options.opacity=1.f;
        options.fade_start=15.f;
        options.fade_end=160.f;
        auto invalid=options;
        invalid.fade_start=invalid.fade_end;
        require(!grid->set_grid(renderer,&invalid),
                "Invalid grid fade range silently accepted");
        if(!grid->set_grid(renderer,&options))
            throw std::runtime_error("Enable grid: "+detail());
        NorthStarRenderFrame frame{};
        frame.camera_override=1;
        frame.camera_y=8.f;
        frame.camera_z=-12.f;
        frame.camera_pitch=-.65f;
        frame.camera_fov_y_degrees=60.f;
        frame.camera_near_clip=.1f;
        frame.camera_far_clip=500.f;
        frame.selected=-1;
        frame.verify_pixels=7;
        auto start=std::chrono::steady_clock::now();
        auto draw=[&](char const* stage) {
            if(!renderer_api->draw(renderer,&frame))
                throw std::runtime_error(std::string(stage)+": "+detail());
        };
        draw("First grid frame");
        for(unsigned index=0;index<8;++index) {
            frame.camera_x+=.04f;
            frame.camera_z+=.02f;
            frame.camera_yaw+=.0015f;
            frame.verify_pixels=index%4==0?7u:0u;
            draw("Temporal jitter/reprojection frame");
        }
        frame.camera_x=8000.f;
        frame.camera_z=6000.f;
        frame.verify_pixels=7;
        draw("Large camera cut");
        SetWindowPos(window,nullptr,100,100,430,335,
                     SWP_NOZORDER|SWP_NOACTIVATE);
        draw("Resize resets history");
        require(grid->set_grid(renderer,nullptr)!=0,
                "Cannot disable editor grid");
        frame.verify_pixels=8;
        draw("Grid disabled");
        // Re-enable and verify history does not leak stale pixels.
        require(grid->set_grid(renderer,&options)!=0,
                "Cannot reenable grid");
        frame.verify_pixels=7;
        draw("Grid re-enabled after history reset");
        auto const elapsed=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        std::printf("Analytic grid %s %s TAA/resize/cut/toggle PASS, 13 frames, %.2f ms\n",
                    module->provider,hardware?"hardware":"WARP",elapsed);
        renderer_api->destroy(renderer);renderer=nullptr;
        DestroyWindow(window);window=nullptr;
        UnregisterClassW(class_name,instance);
        FreeLibrary(library);
        return 0;
    }catch(std::exception const& e) {
        std::fprintf(stderr,"Native analytic grid GPU QA failed: %s\n",e.what());
        if(renderer && renderer_api)renderer_api->destroy(renderer);
        if(window)DestroyWindow(window);
        if(library)FreeLibrary(library);
        return 1;
    }
}
