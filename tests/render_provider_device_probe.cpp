// NorthStarCpp device-level renderer diagnostics.
// Runs out of process: a GPU driver fault cannot terminate Project Browser.
// Inputs: exact provider DLL path, optional --full for texture & Material Graph.
// Exit status distinguishes GPU failure (1), ABI mismatch (2), and success (0).
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_lights_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_device_api.h>
#include <northstarcpp/provider_identity.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

std::string json_escape(std::string const& s) {
    static char const digits[]="0123456789abcdef";
    std::string out;
    for(unsigned char c:s) {
        if(c=='"')out+="\\\"";
        else if(c=='\\')out+="\\\\";
        else if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else if(c=='\t')out+="\\t";
        else if(c<0x20) {
            out+="\\u00";
            out+=digits[c>>4];out+=digits[c&15];
        } else out+=static_cast<char>(c);
    }
    return out;
}
void require(bool ok,std::string const& msg) {
    if(!ok)throw std::runtime_error(msg);
}
template<typename Api>
Api const* extension(NorthStarModuleQuery query,std::uint32_t version,
                     std::uint32_t table_version) {
    auto desc=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,version);
    if(!desc||desc->size<sizeof(NorthStarModule) ||
       desc->host_abi!=NORTHSTARCPP_MODULE_ABI || desc->api_version!=version ||
       desc->api_size<sizeof(Api) || !desc->api || !desc->capability ||
       std::strcmp(desc->capability,NORTHSTARCPP_CAPABILITY_RENDER)!=0)
        return nullptr;
    auto api=static_cast<Api const*>(desc->api);
    return api->abi_version==table_version?api:nullptr;
}
struct Results {
    std::string provider,version,error,gpu_name;
    std::uint32_t vendor_id=0,device_id=0,device_backend=0,api_version=0;
    bool identity=false,software=false;
    bool created=false,geometry=false,texture=false,graph=false,timing=false;
    bool textures_supported=false,graph_supported=false,timing_supported=false;
    bool lights_supported=false,light_gpu_verified=false;
    bool skin_supported=false,skin_gpu_verified=false;
    bool surfaces_supported=false,surfaces_gpu_verified=false;
    bool full=false;
    double gpu_ms=0;
    std::uint64_t advertised=0;
};
void print(Results const& data) {
    auto b=[](bool v){return v?"true":"false";};
    // Single bounded JSON response. Never print driver logs to stdout.
    std::cout<<"{\"schema\":1,\"status\":\""<<(data.error.empty()?"passed":"failed")
        <<"\",\"provider\":\""<<json_escape(data.provider)<<"\""
        <<",\"version\":\""<<json_escape(data.version)<<"\""
        <<",\"error\":\""<<json_escape(data.error)<<"\""
        <<",\"device_identity_verified\":"<<b(data.identity)
        <<",\"adapter_name\":\""<<json_escape(data.gpu_name)<<"\""
        <<",\"vendor_id\":"<<data.vendor_id
        <<",\"device_id\":"<<data.device_id
        <<",\"device_backend\":"<<data.device_backend
        <<",\"device_api_version\":"<<data.api_version
        <<",\"software_adapter\":"<<b(data.software)
        <<",\"device_initialized\":"<<b(data.created)
        <<",\"geometry_gpu_readback\":"<<b(data.geometry)
        <<",\"texture_api_available\":"<<b(data.textures_supported)
        <<",\"texture_gpu_readback\":"<<b(data.texture)
        <<",\"graph_api_available\":"<<b(data.graph_supported)
        <<",\"graph_gpu_readback\":"<<b(data.graph)
        <<",\"lights_api_available\":"<<b(data.lights_supported)
        <<",\"light_gpu_readback\":"<<b(data.light_gpu_verified)
        <<",\"skin_api_available\":"<<b(data.skin_supported)
        <<",\"skin_gpu_readback\":"<<b(data.skin_gpu_verified)
        <<",\"surfaces_api_available\":"<<b(data.surfaces_supported)
        <<",\"surfaces_gpu_readback\":"<<b(data.surfaces_gpu_verified)
        <<",\"timing_api_available\":"<<b(data.timing_supported)
        <<",\"gpu_timestamp_valid\":"<<b(data.timing)
        <<",\"gpu_frame_ms\":"<<data.gpu_ms
        <<",\"full_test\":"<<b(data.full)
        <<",\"declared_feature_mask\":"<<data.advertised<<"}\n";
    std::cout.flush();
}
}
int main(int argc,char** argv) {
    Results result;
    result.full=argc==3 && std::strcmp(argv[2],"--full")==0;
    if(argc!=2 && !(argc==3&&result.full)) {
        result.error="Usage: render_provider_gpu_probe <exact-dll-path> [--full]";
        print(result);return 2;
    }
    HMODULE dll=nullptr;
    HWND window=nullptr;
    NorthStarRenderApi const* api=nullptr;
    NorthStarModule const* base=nullptr;
    void* renderer=nullptr;
    wchar_t const* window_class=L"NorthStarRenderDeviceProbe";
    auto const instance=GetModuleHandleW(nullptr);
    bool registered=false;
    auto close=[&] {
        if(renderer && api && api->destroy)api->destroy(renderer);
        renderer=nullptr;
        if(window)DestroyWindow(window);
        window=nullptr;
        if(registered)UnregisterClassW(window_class,instance);
        registered=false;
        if(dll)FreeLibrary(dll);
        dll=nullptr;
    };
    try {
        auto const path=std::filesystem::u8path(argv[1]);
        require(std::filesystem::is_regular_file(path),"Provider DLL does not exist");
        dll=LoadLibraryW(path.wstring().c_str());
        require(dll!=nullptr,"Windows failed to load provider DLL (code "+
            std::to_string(GetLastError())+")");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(dll,"northstar_module_query"));
        require(query!=nullptr,"Missing northstar_module_query");
        base=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
                   NORTHSTARCPP_RENDER_ABI);
        require(base && base->size>=sizeof(NorthStarModule) &&
                base->host_abi==NORTHSTARCPP_MODULE_ABI &&
                base->api_version==NORTHSTARCPP_RENDER_ABI &&
                base->api_size>=sizeof(NorthStarRenderApi) &&
                base->api && base->provider && base->version && base->last_error,
                "Renderer ABI 5 mismatch");
        result.provider=northstarcpp::canonical_provider_id(base->provider,
                              NORTHSTARCPP_CAPABILITY_RENDER);
        result.version=base->version;
        api=static_cast<NorthStarRenderApi const*>(base->api);
        require(api->create && api->destroy && api->draw && api->set_sky,
                "Incomplete Render ABI 5 table");
        auto const* feature=extension<NorthStarRenderFeaturesApi>(query,
            NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI,
            NORTHSTARCPP_RENDER_FEATURES_ABI);
        if(feature && feature->struct_size>=sizeof(NorthStarRenderFeaturesApi))
            result.advertised=feature->implemented_features;
        auto const* triangles=extension<NorthStarRenderTrianglesApi>(query,
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI,
            NORTHSTARCPP_RENDER_TRIANGLES_ABI);
        auto const* materials=extension<NorthStarRenderMaterialsApi>(query,
            NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI,
            NORTHSTARCPP_RENDER_MATERIALS_ABI);
        auto const* graph_api=extension<NorthStarRenderMaterialsApiV2>(query,
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI,
            NORTHSTARCPP_RENDER_MATERIALS_V2_ABI);
        auto const* timing_api=extension<NorthStarRenderTimingApi>(query,
            NORTHSTARCPP_RENDER_TIMING_MODULE_ABI,NORTHSTARCPP_RENDER_TIMING_ABI);
        auto const* lights_api=extension<NorthStarRenderLightsApi>(query,
            NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI,NORTHSTARCPP_RENDER_LIGHTS_ABI);
        auto const* skin_api=extension<NorthStarRenderSkinnedMaterialsApi>(query,
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI,
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI);
        auto const* surface_api=extension<NorthStarRenderSurfacesApi>(query,
            NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI,
            NORTHSTARCPP_RENDER_SURFACES_ABI);
        auto const* device_api=extension<NorthStarRenderDeviceApi>(query,
            NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI,NORTHSTARCPP_RENDER_DEVICE_ABI);
        result.textures_supported=materials&&materials->set_batches;
        result.graph_supported=graph_api&&graph_api->set_batches_v2;
        result.timing_supported=timing_api&&timing_api->read;
        result.lights_supported=lights_api&&lights_api->set_lights;
        result.skin_supported=skin_api&&skin_api->set_batches;
        result.surfaces_supported=surface_api&&surface_api->set_texture&&
                                  surface_api->set_cube_surfaces;
        WNDCLASSW wc{};
        wc.lpfnWndProc=DefWindowProcW;
        wc.hInstance=instance;
        wc.lpszClassName=window_class;
        require(RegisterClassW(&wc)!=0,"Cannot register native test window");
        registered=true;
        window=CreateWindowW(window_class,L"NorthStar GPU Device Probe",
            WS_OVERLAPPEDWINDOW,100,100,512,384,nullptr,nullptr,instance,nullptr);
        require(window!=nullptr,"Cannot create renderer test HWND");
        ShowWindow(window,SW_SHOW);
        renderer=api->create(window,0u); // Strict hardware, not WARP/software.
        require(renderer!=nullptr,std::string("Hardware GPU initialization failed: ")+
             (base->last_error()?base->last_error():"unknown error"));
        result.created=true;
        // Identity comes from the active renderer instance, NOT an unrelated
        // adapter enumeration that might choose a different physical GPU.
        if(device_api&&device_api->read) {
            NorthStarRenderDeviceInfo actual{};
            if(device_api->read(renderer,&actual)&&
               actual.abi_version==NORTHSTARCPP_RENDER_DEVICE_ABI &&
               actual.struct_size>=sizeof(actual)) {
                actual.adapter_name_utf8[sizeof(actual.adapter_name_utf8)-1]=0;
                result.identity=true;
                result.gpu_name=actual.adapter_name_utf8;
                result.vendor_id=actual.vendor_id;
                result.device_id=actual.device_id;
                result.device_backend=actual.backend;
                result.api_version=actual.api_version;
                result.software=actual.software!=0;
                require(!result.software,"Provider selected a software adapter "
                       "despite the requested hardware renderer");
            }
        }

        // Verify a *real GPU frame*, not just a successful vkCreateDevice or
        // D3D device creation. verify_pixels=1 performs provider-owned GPU
        // texture readback and rejects missing geometry/invalid output.
        NorthStarRenderCube cube{};
        cube.x=0;cube.y=0;cube.z=7.f;cube.size=1.7f;cube.rgb=0xffcc77u;
        cube.rotation[0]=cube.rotation[4]=cube.rotation[8]=1.f;
        NorthStarRenderFrame frame{};
        frame.cubes=&cube;frame.count=1;frame.selected=-1;
        frame.camera_fov_y_degrees=70.f;
        frame.camera_near_clip=.1f;frame.camera_far_clip=250.f;
        frame.verify_pixels=1;
        if(triangles && triangles->set_triangles) {
            NorthStarRenderTriangleVertex verts[]{
                {-1.5f,-1.1f,5.f,0xff4444u},
                {1.5f,-1.1f,5.f,0x44ff44u},
                {0.f,1.3f,5.f,0x4444ffu}
            };
            require(triangles->set_triangles(renderer,verts,3),
                    "Uploading GPU triangle geometry failed");
        }
        require(api->draw(renderer,&frame),std::string("GPU frame/readback failed: ")+
                (base->last_error()?base->last_error():"unknown"));
        result.geometry=true;
        if(result.timing_supported) {
            NorthStarRenderTimingStats gpu{};
            if(timing_api->read(renderer,&gpu) && gpu.valid &&
               std::isfinite(gpu.total_gpu_ms)&&gpu.total_gpu_ms>0) {
                result.timing=true;result.gpu_ms=gpu.total_gpu_ms;
            }
        }
        if(result.full && result.textures_supported) {
            // Exercise actual textured GPU sampling using the same two-color
            // pixel readback as the D3D11/D3D12 material smoke acceptance.
            if(triangles && triangles->set_triangles)
                require(triangles->set_triangles(renderer,nullptr,0),
                        "Cannot clear triangles before material readback");
            NorthStarMaterialVertex quad[6]{
                {-2,-2,3,0,0,-1,0,1},{2,-2,3,0,0,-1,1,1},
                {-2,2,3,0,0,-1,0,0},{2,-2,3,0,0,-1,1,1},
                {2,2,3,0,0,-1,1,0},{-2,2,3,0,0,-1,0,0}};
            std::array<std::uint8_t,8*8*4> pixels{};
            auto fill_texels=[&] {
                for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
                    auto const at=static_cast<std::size_t>(y*8+x)*4;
                    pixels[at]=x<4?255:0;
                    pixels[at+1]=x>=4?255:0;
                    pixels[at+2]=0;
                    pixels[at+3]=255;
                }
            };
            fill_texels();
            NorthStarMaterialBatch batch{};
            batch.vertices=quad;batch.vertex_count=6;
            batch.base_color[0]=batch.base_color[1]=
                batch.base_color[2]=batch.base_color[3]=1.f;
            batch.roughness=.7f;batch.metallic=.1f;batch.unlit=1u;
            batch.textures[0]={pixels.data(),8,8,
                static_cast<std::uint32_t>(pixels.size())};
            require(materials->set_batches(renderer,&batch,1),
                    "GPU provider refused textured material");
            pixels.fill(0); // GPU provider must own a copy of texture bytes.
            frame.cubes=nullptr;frame.count=0;frame.first_person=1;
            frame.camera_override=1;frame.camera_fov_y_degrees=80.f;
            frame.camera_far_clip=100.f;
            frame.verify_pixels=2;
            require(api->draw(renderer,&frame),std::string(
                "Textured GPU UV readback failed: ")+
                (base->last_error()?base->last_error():"unknown"));
            result.texture=true;
            if(result.lights_supported &&
               (result.provider=="vulkan"||result.provider=="d3d11")) {
                NorthStarRenderLight invalid{};
                invalid.position_range[3]=-1.f;
                if(lights_api->set_lights(renderer,&invalid,1))
                    throw std::runtime_error(
                        "Vulkan dynamic lights accepted invalid radius");
                NorthStarRenderLight light{};
                light.position_range[0]=0.f;
                light.position_range[1]=0.f;
                light.position_range[2]=0.f;
                light.position_range[3]=16.f;
                light.color_intensity[0]=1.f;
                light.color_intensity[1]=1.f;
                light.color_intensity[2]=1.f;
                light.color_intensity[3]=28.f;
                light.direction_cone[3]=-1.f; // Point light.
                require(lights_api->set_lights(renderer,&light,1),
                        "Vulkan light upload failed");
                pixels.fill(255);
                batch.unlit=0;
                batch.roughness=1.f;batch.metallic=0.f;
                require(materials->set_batches(renderer,&batch,1),
                        "Vulkan PBR material for lighting failed");
                pixels.fill(0);
                frame.verify_pixels=5;
                require(api->draw(renderer,&frame),
                        std::string("Vulkan point light GPU readback failed: ")+
                        (base->last_error()?base->last_error():"unknown"));
                result.light_gpu_verified=true;
                require(lights_api->set_lights(renderer,nullptr,0),
                        "Vulkan dynamic lights cleanup failed");
                // Restore the unlit material for subsequent graph checks.
                batch.unlit=1;
            }

            if(result.graph_supported) {
                fill_texels();
                static constexpr char green_graph[]=
                    "#define NS_GRAPH_BASE(p) (float4(0,1,0,1))\n";
                NorthStarMaterialBatchV2 variant{};
                variant.material=batch;variant.graph_hlsl_defines=green_graph;
                variant.graph_hlsl_bytes=sizeof(green_graph)-1;
                variant.opacity=1.f;variant.alpha_cutoff=.5f;
                require(graph_api->set_batches_v2(renderer,&variant,1),
                        "Material Graph v2 GPU upload failed");
                pixels.fill(0);
                frame.verify_pixels=3;
                auto const deadline=std::chrono::steady_clock::now()+16s;
                bool graph_ready=false;
                while(std::chrono::steady_clock::now()<deadline) {
                    if(api->draw(renderer,&frame)) {
                        graph_ready=true;break;
                    }
                    auto const* message=base->last_error();
                    auto const error=std::string(message?message:"");
                    if(error.find("Generated Material Graph GPU color mismatch")==
                            std::string::npos &&
                       error.find("D3D12 programmable Material Graph GPU tint was not applied")==
                            std::string::npos)
                        throw std::runtime_error("Material Graph GPU failure: "+error);
                    std::this_thread::sleep_for(15ms);
                }
                require(graph_ready,"Material Graph GPU pipeline did not become ready");
                if(result.provider=="vulkan") {
                    // Confirm both new Vulkan PSO states, not just ABI presence.
                    variant.blend_mode=1;
                    variant.alpha_cutoff=.5f;
                    fill_texels(); // Host-owned texture was intentionally freed/zeroed.
                    require(graph_api->set_batches_v2(renderer,&variant,1),
                        "Vulkan masked Material Graph variant rejected");
                    pixels.fill(0);
                    if(!api->draw(renderer,&frame))
                        throw std::runtime_error(std::string(
                            "Vulkan masked graph pixels incorrect: ")+
                            (base->last_error()?base->last_error():"unknown"));
                    variant.blend_mode=2;
                    variant.opacity=.85f;
                    fill_texels();
                    require(graph_api->set_batches_v2(renderer,&variant,1),
                        "Vulkan translucent Material Graph variant rejected");
                    pixels.fill(0);
                    if(!api->draw(renderer,&frame))
                        throw std::runtime_error(std::string(
                            "Vulkan translucent graph pixels incorrect: ")+
                            (base->last_error()?base->last_error():"unknown"));
                    variant.opacity=-.1f;
                    require(!graph_api->set_batches_v2(renderer,&variant,1),
                        "Vulkan accepted invalid Material Graph opacity");
                    variant.opacity=1.f;
                    static constexpr char unsafe_graph[]=
                        "#define UNVALIDATED_USER_SHADER(p) (float4(0,1,0,1))\\n";
                    variant.graph_hlsl_defines=unsafe_graph;
                    variant.graph_hlsl_bytes=sizeof(unsafe_graph)-1;
                    require(!graph_api->set_batches_v2(renderer,&variant,1),
                        "Vulkan accepted shader directives outside graph whitelist");
                }
                result.graph=true;
            }
            if(result.skin_supported &&
               (result.provider=="vulkan"||result.provider=="d3d11")) {
                // Original 8-weight ABI. Identity palette confirms no CPU
                // expansion; GPU must execute the skinned vertex path.
                std::array<NorthStarSkinMaterialVertex,6> skin_vertices{};
                for(unsigned i=0;i<6;++i) {
                    skin_vertices[i].x=quad[i].x;
                    skin_vertices[i].y=quad[i].y;
                    skin_vertices[i].z=quad[i].z;
                    skin_vertices[i].nx=quad[i].nx;
                    skin_vertices[i].ny=quad[i].ny;
                    skin_vertices[i].nz=quad[i].nz;
                    skin_vertices[i].u=quad[i].u;
                    skin_vertices[i].v=quad[i].v;
                    skin_vertices[i].joints[0]=0u;
                    skin_vertices[i].weights[0]=1.f;
                }
                std::array<float,16> palette{};
                palette[0]=palette[5]=palette[10]=palette[15]=1.f;
                NorthStarSkinnedMaterialBatch skinned{};
                fill_texels();
                skinned.material.material=batch;
                skinned.material.material.unlit=1u;
                skinned.material.material.vertices=nullptr;
                skinned.material.material.vertex_count=0;
                skinned.material.blend_mode=0;
                skinned.material.opacity=1.f;
                skinned.material.alpha_cutoff=.5f;
                skinned.vertices=skin_vertices.data();
                skinned.vertex_count=6;
                skinned.palette=palette.data();
                skinned.joint_count=1;
                require(materials->set_batches(renderer,nullptr,0),
                    "Cannot clear static PBR before native GPU skin test");
                require(skin_api->set_batches(renderer,&skinned,1),
                    "Provider rejected original 8-weight GPU skin geometry");
                pixels.fill(0);
                frame.verify_pixels=2;
                if(!api->draw(renderer,&frame))
                    throw std::runtime_error(
                        std::string("Native GPU skinned UV readback failed: ")+
                        (base->last_error()?base->last_error():"unknown"));
                // Verify palette actually moves geometry out of the
                // frustum; a provider that merely draws original vertices
                // cannot pass this GPU-side deformation check.
                palette[12]=100.f;
                require(skin_api->set_batches(renderer,&skinned,1),
                    "GPU skin rejected translated bone palette");
                frame.verify_pixels=6;
                if(!api->draw(renderer,&frame))
                    throw std::runtime_error(std::string(
                        "Native GPU bone translation readback failed: ")+
                        (base->last_error()?base->last_error():"unknown"));
                skin_vertices[0].weights[0]=-1.f;
                require(!skin_api->set_batches(renderer,&skinned,1),
                    "GPU skin provider accepted invalid negative bone weight");
                result.skin_gpu_verified=true;
                require(skin_api->set_batches(renderer,nullptr,0),
                        "Provider rejected native GPU skin clear");
            }
        }
        if(result.full && result.provider=="vulkan" &&
           result.surfaces_supported) {
            // 9 genuine compressed GPU textures, no RGBA8 conversion.
            // BC1 solid red/green/blue for sand/rock/forest and BC5
            // neutral normals/roughness, 8x8 compressed block layout.
            std::array<NorthStarSurfaceMip,1> mips{{{8u,8u,0u,0u}}};
            std::array<std::uint8_t,32> bc1{};
            std::array<std::uint8_t,64> bc5{};
            for(unsigned slot=0;slot<9;++slot) {
                const auto albedo=slot%3==0;
                const auto bytes_per_block=albedo?8u:16u;
                mips[0].bytes=4u*bytes_per_block;
                const auto color=slot<3?0xf800u:slot<6?0x07e0u:0x001fu;
                if(albedo) {
                    for(unsigned block=0;block<4;++block) {
                        bc1[block*8]=static_cast<std::uint8_t>(color&255u);
                        bc1[block*8+1]=static_cast<std::uint8_t>(color>>8);
                        bc1[block*8+2]=bc1[block*8];
                        bc1[block*8+3]=bc1[block*8+1];
                    }
                }else for(unsigned block=0;block<4;++block) {
                    bc5[block*16]=128u;bc5[block*16+1]=128u;
                    bc5[block*16+8]=128u;bc5[block*16+9]=128u;
                }
                NorthStarSurfaceTexture surface{};
                surface.slot=slot;surface.format_id=albedo?102:105;
                surface.srgb=albedo?1u:0u;
                surface.mip_count=1;
                surface.mips=mips.data();
                surface.data=albedo?bc1.data():bc5.data();
                surface.data_bytes=mips[0].bytes;
                if(!surface_api->set_texture(renderer,&surface))
                    throw std::runtime_error(std::string(
                        "Vulkan native BC surface upload failed: ")+
                        (base->last_error()?base->last_error():"unknown"));
            }
            // The host may release/deallocate all compressed source data
            // immediately after the set_texture() call. Only GPU-owned BC
            // textures can still pass the pixel tests after this mutation.
            bc1.fill(0);bc5.fill(0);
            NorthStarSurfaceMip bad_mip{8u,8u,0u,1u};
            NorthStarSurfaceTexture invalid_bc{};
            invalid_bc.slot=0;
            invalid_bc.format_id=102;invalid_bc.srgb=1;
            invalid_bc.mip_count=1;invalid_bc.mips=&bad_mip;
            invalid_bc.data=bc1.data();invalid_bc.data_bytes=32;
            require(!surface_api->set_texture(renderer,&invalid_bc),
                "Vulkan accepted a malformed BC1 mip payload");
            // Force all existing static/skinned batches clear.
            if(skin_api)require(skin_api->set_batches(renderer,nullptr,0),
                "Failed to clear skinned geometry for World Surface GPU test");
            if(materials)require(materials->set_batches(renderer,nullptr,0),
                "Failed to clear authored geometry for World Surface GPU test");
            NorthStarRenderCube world_cube{};
            world_cube.z=7.f;
            world_cube.size=4.f;
            world_cube.rgb=0xffffffu;
            world_cube.rotation[0]=world_cube.rotation[4]=world_cube.rotation[8]=1.f;
            frame.cubes=&world_cube;
            frame.count=1;
            frame.verify_pixels=7;
            for(std::uint32_t index=1;index<=3;++index) {
                if(!surface_api->set_cube_surfaces(renderer,&index,1))
                    throw std::runtime_error("Vulkan surface selector upload failed");
                if(!api->draw(renderer,&frame))
                    throw std::runtime_error(
                        std::string("Vulkan BC1/BC5 GPU selector readback failed: ")+
                        (base->last_error()?base->last_error():"unknown"));
            }
            result.surfaces_gpu_verified=true;
            require(surface_api->set_cube_surfaces(renderer,nullptr,0),
                "Vulkan world surface selectors clear failed");
        }
        close();
        print(result);return 0;
    } catch(std::exception const& error) {
        result.error=error.what();
        std::fprintf(stderr,"GPU Device Probe failed: %s\n",result.error.c_str());
        close();
        print(result);return 1;
    }
}
