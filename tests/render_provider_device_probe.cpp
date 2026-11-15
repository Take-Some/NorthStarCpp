// NorthStarCpp device-level renderer diagnostics.
// Runs out of process: a GPU driver fault cannot terminate Project Browser.
// Inputs: exact provider DLL path, optional --full for texture & Material Graph.
// Exit status distinguishes GPU failure (1), ABI mismatch (2), and success (0).
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_lights_api.h>
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
        auto const* device_api=extension<NorthStarRenderDeviceApi>(query,
            NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI,NORTHSTARCPP_RENDER_DEVICE_ABI);
        result.textures_supported=materials&&materials->set_batches;
        result.graph_supported=graph_api&&graph_api->set_batches_v2;
        result.timing_supported=timing_api&&timing_api->read;
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
                result.graph=true;
            }
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
