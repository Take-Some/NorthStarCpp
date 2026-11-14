// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// D3D11 provider memory residency test using real WARP rendering.

#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/module.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition,char const* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc,char** argv)
{
    if (argc!=2) return 2;
    HMODULE library=nullptr;
    HWND window=nullptr;
    void* renderer=nullptr;
    NorthStarRenderApi const* api=nullptr;
    try {
        library=LoadLibraryW(std::filesystem::u8path(argv[1]).wstring().c_str());
        require(library!=nullptr,"Cannot load D3D11 render provider");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(library,"northstar_module_query"));
        require(query!=nullptr,"Render provider descriptor is missing");
        auto const* module=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI);
        require(module && module->api_size>=sizeof(NorthStarRenderApi),
                "Render provider has incompatible ABI");
        api=static_cast<NorthStarRenderApi const*>(module->api);
        auto extension=reinterpret_cast<NorthStarRenderGetMemoryApi>(
            GetProcAddress(library,"northstar_render_get_memory_api"));
        require(extension && extension(2)==nullptr,
                "Missing or incompatible render memory extension");
        auto const* stats_api=extension(NORTHSTARCPP_RENDER_MEMORY_ABI);
        require(stats_api && stats_api->read &&
                stats_api->abi_version==NORTHSTARCPP_RENDER_MEMORY_ABI,
                "Invalid memory reporting interface");

        HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSW klass{};
        klass.hInstance=instance;
        klass.lpfnWndProc=DefWindowProcW;
        klass.lpszClassName=L"NorthStarRenderResidencySmoke";
        require(RegisterClassW(&klass)!=0,"Cannot register WARP test window");
        window=CreateWindowExW(0,klass.lpszClassName,L"Render memory smoke",
            WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,instance,nullptr);
        require(window!=nullptr,"Cannot create WARP test window");
        renderer=api->create(window,1);
        require(renderer!=nullptr,"Cannot create D3D11 WARP renderer");
        auto get_geometry=reinterpret_cast<NorthStarRenderGetTrianglesApi>(
            GetProcAddress(library,"northstar_render_get_triangles_api"));
        require(get_geometry && get_geometry(2)==nullptr,
                "Render triangle extension ABI negotiation failed");
        auto const* geometry=get_geometry(NORTHSTARCPP_RENDER_TRIANGLES_ABI);
        require(geometry && geometry->abi_version==NORTHSTARCPP_RENDER_TRIANGLES_ABI &&
                geometry->set_triangles,
                "Missing scene triangle rendering API");
        NorthStarRenderTriangleVertex triangle[]={
            {-1.f,-1.f,4.f,0xff6644u},{1.f,-1.f,4.f,0xff6644u},
            {0.f,1.f,4.f,0xff6644u}
        };
        require(geometry->set_triangles(renderer,triangle,2)==0,
                "Accepted a non-triangulated render command");
        require(geometry->set_triangles(renderer,triangle,3)!=0,
                "Cannot submit mesh triangles");
        triangle[0].x=99999999.f; // Provider must already own a copy.
        NorthStarRenderFrame geometry_frame{};
        geometry_frame.first_person=1;
        geometry_frame.verify_pixels=1; // GPU readback must contain real mesh pixels.
        require(api->draw(renderer,&geometry_frame)!=0,
                "D3D11 could not render provider-owned mesh triangles");
        require(geometry->set_triangles(renderer,nullptr,0)!=0,
                "Cannot clear scene geometry");


        NorthStarRenderMemoryStats stats{};
        require(stats_api->read(renderer,&stats)!=0 &&
                stats.world_vertex_capacity==4096,
                "Renderer did not start with compact GPU vertex allocation");

        std::vector<NorthStarRenderCube> cubes(300);
        for (std::size_t i=0;i<cubes.size();++i) {
            auto& cube=cubes[i];
            cube.x=static_cast<float>(i%20)*0.10f;
            cube.y=0.f;
            cube.z=3.f+static_cast<float>(i/20)*0.10f;
            cube.size=1.f;
            cube.rgb=0x91aabc;
            cube.rotation[0]=cube.rotation[4]=cube.rotation[8]=1.f;
        }
        NorthStarRenderFrame frame{};
        frame.cubes=cubes.data();
        frame.count=static_cast<std::uint32_t>(cubes.size());
        frame.first_person=1;
        require(api->draw(renderer,&frame)!=0,"Cannot draw large WARP frame");
        require(stats_api->read(renderer,&stats)!=0 &&
                stats.world_vertex_capacity>4096,
                "D3D11 allocation failed to grow for a large scene");
        auto const expanded=stats.vertex_buffer_bytes;

        frame.count=0;
        frame.cubes=nullptr;
        for (int i=0;i<125;++i)
            require(api->draw(renderer,&frame)!=0,
                    "Small-scene GPU residency frame failed");
        require(stats_api->read(renderer,&stats)!=0 &&
                stats.world_vertex_capacity==4096 &&
                stats.vertex_buffer_bytes<expanded &&
                stats.vertex_buffer_reallocations>=2,
                "GPU vertex allocation was not released after scene unload");

        NorthStarSkyVertex vertices[]{
            {-1.f,-1.f,1.f,0.f,0.f},{1.f,-1.f,1.f,1.f,0.f},
            {0.f,1.f,1.f,0.5f,1.f},
        };
        std::uint32_t indices[]{0,1,2};
        std::uint8_t texels[]{
            100,120,140,255, 120,140,160,255,
            150,170,190,255, 180,200,220,255,
        };
        NorthStarSky sky{vertices,3,indices,3,texels,2,2,1.f,1.f,1.f};
        require(api->set_sky(renderer,&sky)!=0,"Cannot upload test sky");
        require(stats_api->read(renderer,&stats)!=0 &&
                stats.sky_geometry_and_texture_bytes>0,
                "Sky resource GPU residency not recorded");
        require(api->set_sky(renderer,nullptr)!=0,
                "Cannot release D3D11 sky resources");
        require(stats_api->read(renderer,&stats)!=0 &&
                stats.sky_geometry_and_texture_bytes==0,
                "Sky GPU memory remained resident after release");

        api->destroy(renderer); renderer=nullptr;
        DestroyWindow(window);window=nullptr;
        FreeLibrary(library);library=nullptr;
        std::cout<<"D3D11 WARP GPU residency test passed\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr<<"D3D11 GPU residency failed: "<<error.what()<<'\n';
        if (renderer && api) api->destroy(renderer);
        if (window) DestroyWindow(window);
        if (library) FreeLibrary(library);
        return 1;
    }
}
