// Direct GPU acceptance: native D3D11 material ABI + UV texture sampling.
// Uses WARP and real CopyResource/Map readback (frame.verify_pixels == 2).
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_lights_api.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

static void require(bool condition,char const* message) {
    if(!condition)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3)return 2;
    bool const hardware=argc==3 && std::strcmp(argv[2],"--hardware")==0;
    if(argc==3&&!hardware)return 2;
    HMODULE library=nullptr;
    HWND window=nullptr;
    void* renderer=nullptr;
    NorthStarRenderApi const* api=nullptr;
    NorthStarModule const* module=nullptr;
    wchar_t const* type=L"NorthStarMaterialWarpSmoke";
    HINSTANCE const instance=GetModuleHandleW(nullptr);
    try {
        library=LoadLibraryW(std::filesystem::u8path(argv[1]).wstring().c_str());
        require(library!=nullptr,"Cannot load direct D3D11 renderer");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(library,"northstar_module_query"));
        require(query!=nullptr,"Missing versioned module query");
        module=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI);
        require(module&&module->api_size>=sizeof(NorthStarRenderApi),
            "Legacy render ABI no longer supported");
        api=static_cast<NorthStarRenderApi const*>(module->api);
        auto get_materials=reinterpret_cast<NorthStarRenderGetMaterialsApi>(
            GetProcAddress(library,"northstar_render_get_materials_api"));
        require(get_materials&&get_materials(999)==nullptr,
            "Material ABI negotiation did not reject unsupported version");
        auto material=get_materials(NORTHSTARCPP_RENDER_MATERIALS_ABI);
        require(material && material->abi_version==NORTHSTARCPP_RENDER_MATERIALS_ABI &&
                material->set_batches,"Native material extension missing");
        auto get_v2=reinterpret_cast<NorthStarRenderGetMaterialsApiV2>(
            GetProcAddress(library,"northstar_render_get_materials_api_v2"));
        require(get_v2&&get_v2(999)==nullptr,"Graph ABI negotiation rejected incorrectly");
        auto const* material_v2=get_v2(NORTHSTARCPP_RENDER_MATERIALS_V2_ABI);
        require(material_v2&&material_v2->set_batches_v2,
                "GPU provider lacks programmable Material Graph API v2");

        auto extension=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI);
        require(extension&&extension->api_size==sizeof(NorthStarRenderMaterialsApi),
            "Material extension not replaceable through host module query");
        WNDCLASSW wc{};
        wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=type;
        require(RegisterClassW(&wc)!=0,"Register material acceptance window failed");
        window=CreateWindowW(type,L"NorthStar UV/PBR acceptance",
            WS_OVERLAPPEDWINDOW,100,100,512,384,nullptr,nullptr,instance,nullptr);
        require(window!=nullptr,"Create material acceptance window failed");
        ShowWindow(window,SW_SHOW);
        renderer=api->create(window,hardware?0u:1u);
        if(!renderer&&hardware) {
            std::puts("Hardware render adapter unavailable: test skipped");
            DestroyWindow(window);window=nullptr;
            UnregisterClassW(type,instance);
            FreeLibrary(library);library=nullptr;
            return 77;
        }
        require(renderer!=nullptr,
            module->last_error() ? module->last_error() :
            "GPU renderer initialization failed");
        // D3D12-only replaceable dynamic lighting contract.
        auto query_lights=reinterpret_cast<NorthStarRenderGetLightsApi>(
            GetProcAddress(library,"northstar_render_get_lights_api"));
        NorthStarRenderLightsApi const* light_api=nullptr;
        if(std::strcmp(module->provider,"d3d12")==0) {
            require(query_lights&&query_lights(999)==nullptr,
                "Dynamic light ABI does not validate version");
            light_api=query_lights(NORTHSTARCPP_RENDER_LIGHTS_ABI);
            require(light_api && light_api->set_lights,
                "Native D3D12 dynamic lighting extension missing");
            auto const* light_module=query(NORTHSTARCPP_MODULE_ABI,
                NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI);
            require(light_module && light_module->api_size==sizeof(NorthStarRenderLightsApi),
                "Dynamic lights not replaceable through module query");
            NorthStarRenderLight bad{};
            bad.position_range[3]=-1.f;
            bad.color_intensity[0]=1.f;
            bad.color_intensity[1]=1.f;
            bad.color_intensity[2]=1.f;
            bad.color_intensity[3]=1.f;
            bad.direction_cone[3]=-1.f;
            require(!light_api->set_lights(renderer,&bad,1),
                "Renderer accepted a negative light radius");
            NorthStarRenderLight lights[2]{
                {{0,2,3,12},{1,1,1,1.5f},{0,-1,0,-1}},
                {{1.5f,3,3,9},{.4f,.7f,1,.5f},{0,-1,0,.6f}}};
            require(light_api->set_lights(renderer,lights,2),
                module->last_error()?module->last_error():"Cannot upload two scene lights");
            lights[0].position_range[0]=10000.f; // must copy; not borrow.
        }
        // Big rectangle at Z=3 with real (0..1) UVs facing the camera.
        NorthStarMaterialVertex quad[6]{
            {-2,-2,3,0,0,-1,0,1},{2,-2,3,0,0,-1,1,1},
            {-2,2,3,0,0,-1,0,0},{2,-2,3,0,0,-1,1,1},
            {2,2,3,0,0,-1,1,0},{-2,2,3,0,0,-1,0,0}};
        std::array<std::uint8_t,8*8*4> pixels{};
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            std::size_t index=std::size_t(y*8+x)*4;
            pixels[index+0]=x<4?255:0;
            pixels[index+1]=x>=4?255:0;
            pixels[index+2]=0;
            pixels[index+3]=255;
        }
        NorthStarMaterialBatch batch{};
        batch.vertices=quad;batch.vertex_count=6;
        batch.base_color[0]=batch.base_color[1]=
            batch.base_color[2]=batch.base_color[3]=1.f;
        batch.roughness=.7f;batch.metallic=.1f;batch.unlit=1u;
        batch.textures[0]={pixels.data(),8,8,static_cast<std::uint32_t>(pixels.size())};
        NorthStarMaterialBatch invalid=batch;
        invalid.vertex_count=5;
        require(!material->set_batches(renderer,&invalid,1),
            "GPU material provider accepted malformed triangle topology");
        require(material->set_batches(renderer,&batch,1),
            module->last_error()?module->last_error():"Cannot upload authored material");
        // API guarantees ownership; the shader must continue sampling
        // original GPU texture after the source buffer has been changed.
        pixels.fill(0);
        NorthStarRenderFrame frame{};
        frame.first_person=1;frame.camera_override=1;
        frame.camera_fov_y_degrees=80.f;
        frame.camera_near_clip=.1f;frame.camera_far_clip=100.f;
        frame.verify_pixels=2; // two distinct UV colors required by GPU readback
        require(api->draw(renderer,&frame),
            module->last_error()?module->last_error():"GPU material UV readback failed");
        // Exercise actual GGX deferred/forward lighting, not just the unlit
        // material shortcut. Both APIs must preserve UV sampling and depth.
        for(auto& vertex:quad) {
            vertex.nx=0.f;vertex.ny=1.f;vertex.nz=0.f;
        }
        batch.unlit=0u;
        batch.metallic=0.f;
        batch.roughness=.8f;
        // Reconstruct the authored texture: source pixels were deliberately
        // erased after the first upload to test ownership semantics.
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            std::size_t index=std::size_t(y*8+x)*4;
            pixels[index+0]=x<4?255:0;
            pixels[index+1]=x>=4?255:0;
            pixels[index+2]=0;
            pixels[index+3]=255;
        }
        require(material->set_batches(renderer,&batch,1),
            module->last_error()?module->last_error():"Cannot upload lit PBR material");
        pixels.fill(0);
        frame.verify_pixels=2;
        require(api->draw(renderer,&frame),
            module->last_error()?module->last_error():"Lit PBR GPU readback failed");
        // Repeated deferred and forward frames must safely reuse the GPU
        // resources, upload allocations, SRV descriptors and depth transitions.
        frame.verify_pixels=0;
        for(unsigned repetition=0;repetition<64;++repetition)
            require(api->draw(renderer,&frame),
                module->last_error()?module->last_error():"Repeated lit material frame failed");
        frame.verify_pixels=2;
        // Resize after both deferred material targets and depth have been used.
        SetWindowPos(window,nullptr,100,100,440,330,SWP_NOZORDER|SWP_NOACTIVATE);
        require(api->draw(renderer,&frame),
            module->last_error()?module->last_error():"PBR draw after resize failed");
        // D3D12 exposes actual GPU timings; D3D11 is allowed to omit this
        // optional extension while preserving the same material ABI.
        auto get_timing=reinterpret_cast<NorthStarRenderGetTimingApi>(
            GetProcAddress(library,"northstar_render_get_timing_api"));
        if(get_timing) {
            require(get_timing(999u)==nullptr,"Timing version negotiation failed");
            auto timing_api=get_timing(NORTHSTARCPP_RENDER_TIMING_ABI);
            require(timing_api&&timing_api->read,"Missing timing implementation");
            NorthStarRenderTimingStats timings{};
            require(timing_api->read(renderer,&timings) && timings.valid,
                "Material frame GPU timestamps are unavailable");
            require(std::isfinite(timings.deferred_gpu_ms) &&
                    timings.deferred_gpu_ms>0.0,
                "Material frame did not execute the deferred lighting pass");
            std::printf("Lit material GPU timestamps ms: total %.4f deferred %.4f postfx %.4f\n",
                timings.total_gpu_ms,timings.deferred_gpu_ms,timings.postfx_gpu_ms);
        }
        static constexpr char green_graph[]=
            "#define NS_GRAPH_BASE(p) (float4(0,1,0,1))\n";
        NorthStarMaterialBatchV2 variant{};
        variant.material=batch;
        // Verify the graph's green multiplication independently of the
        // lighting/shadow intensity already exercised above by lit PBR.
        variant.material.unlit=1u;
        variant.graph_hlsl_defines=green_graph;
        variant.graph_hlsl_bytes=sizeof(green_graph)-1;
        variant.opacity=1.f;
        variant.alpha_cutoff=.5f;
        // The previous v1 upload deliberately cleared host pixels to prove
        // GPU ownership. Restore the source before the NEW v2 upload; each
        // upload must supply valid bytes, independent of prior GPU copies.
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            std::size_t const at=std::size_t(y*8+x)*4;
            pixels[at+0]=x<4?255:0;
            pixels[at+1]=x>=4?255:0;
            pixels[at+2]=0;
            pixels[at+3]=255;
        }
        require(material_v2->set_batches_v2(renderer,&variant,1),
            module->last_error()?module->last_error():"Cannot compile graph variant");
        pixels.fill(0); // v2 must also copy all bytes into provider-owned storage.
        frame.verify_pixels=3;
        require(api->draw(renderer,&frame),
            module->last_error()?module->last_error():"Material Graph GPU readback failed");
        frame.verify_pixels=1;
        if(light_api)
            require(light_api->set_lights(renderer,nullptr,0),
                "Cannot clear dynamic scene lights");
        require(material->set_batches(renderer,nullptr,0),
            "Cannot clear authored material batches");
        // Native GPU Skinning is an optional D3D11 renderer extension.
        auto const get_skin=reinterpret_cast<
            NorthStarRenderSkinnedMaterialsApi const* (*)(uint32_t)>(
            GetProcAddress(library,"northstar_render_get_skinning_api"));
        if(get_skin) {
            require(get_skin(999u)==nullptr,
                "GPU skin ABI accepted unknown version");
            auto const* skin=get_skin(NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI);
            require(skin&&skin->set_batches,"GPU skin ABI lacks upload");
            NorthStarSkinMaterialVertex vertices[6]{};
            for(unsigned i=0;i<6;++i) {
                vertices[i].x=quad[i].x;
                vertices[i].y=quad[i].y;
                vertices[i].z=quad[i].z;
                vertices[i].nx=quad[i].nx;
                vertices[i].ny=quad[i].ny;
                vertices[i].nz=quad[i].nz;
                vertices[i].u=quad[i].u;
                vertices[i].v=quad[i].v;
                vertices[i].joints[0]=0;
                vertices[i].weights[0]=1.f;
            }
            float palette[16]{};
            palette[0]=palette[5]=palette[10]=palette[15]=1.f;
            for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
                std::size_t at=std::size_t(y*8+x)*4;
                pixels[at+0]=x<4?255:0;
                pixels[at+1]=x>=4?255:0;
                pixels[at+2]=0;pixels[at+3]=255;
            }
            NorthStarSkinnedMaterialBatch skinned{};
            skinned.material.material=batch;
            skinned.material.material.unlit=1u;
            skinned.material.opacity=1.f;
            skinned.vertices=vertices;
            skinned.vertex_count=6;
            skinned.palette=palette;
            skinned.joint_count=1;
            require(skin->set_batches(renderer,&skinned,1),
                module->last_error()?module->last_error():
                "Cannot upload original GPU skinned vertices");
            pixels.fill(0);
            frame.verify_pixels=2;
            std::fprintf(stderr,"GPU SKIN: initial bind pose\n");
            require(api->draw(renderer,&frame),
                module->last_error()?module->last_error():
                "Skinned GPU shader failed UV readback");
            std::fprintf(stderr,"GPU SKIN: translated pose\n");
            // Reupload a genuine texture, not the deliberately zeroed
            // ownership-test buffer: the missing pixels must be caused by
            // the GPU vertex translation alone.
            for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
                std::size_t at=std::size_t(y*8+x)*4;
                pixels[at+0]=x<4?255:0;
                pixels[at+1]=x>=4?255:0;
                pixels[at+2]=0;pixels[at+3]=255;
            }
            palette[12]=1000.f; // move the full quad off screen using bones
            require(skin->set_batches(renderer,&skinned,1),
                "Cannot upload translated native skin palette");
            require(!api->draw(renderer,&frame),
                "Native GPU skinning shader ignored bone translation");
            std::fprintf(stderr,"GPU SKIN: restored bind pose\n");
            palette[12]=0.f;
            for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
                std::size_t at=std::size_t(y*8+x)*4;
                pixels[at+0]=x<4?255:0;
                pixels[at+1]=x>=4?255:0;
                pixels[at+2]=0;pixels[at+3]=255;
            }
            require(skin->set_batches(renderer,&skinned,1),
                "Cannot restore native bind pose");
            require(api->draw(renderer,&frame),
                module->last_error()?module->last_error():
                "Native GPU skinning failed bind-pose restoration");
            require(skin->set_batches(renderer,nullptr,0),
                "Cannot clear native GPU skinned materials");
            frame.verify_pixels=1;
        }

        std::puts(hardware?
            "Hardware native PBR/UV GPU texture readback PASS":
            "WARP native PBR/UV GPU texture readback PASS");
        api->destroy(renderer);renderer=nullptr;
        DestroyWindow(window);window=nullptr;
        UnregisterClassW(type,instance);
        FreeLibrary(library);
        return 0;
    } catch(std::exception const& error) {
        std::fprintf(stderr,"D3D11 material smoke failed: %s\n",error.what());
        if(renderer&&api)api->destroy(renderer);
        if(window)DestroyWindow(window);
        if(library)FreeLibrary(library);
        return 1;
    }
}
