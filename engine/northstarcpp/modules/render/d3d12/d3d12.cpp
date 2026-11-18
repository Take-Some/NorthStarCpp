// NorthStarCpp D3D12 engine.render provider — stable C ABI 5.
#include "../../shared/frame_memory_counters.hpp"
#include "renderer_private.hpp"
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_device_api.h>
#include <northstarcpp/render_pick_api.h>
namespace {
using namespace northstarcpp::module_provider;
using Renderer = northstarcpp::render::d3d12::detail::RendererState;
void* create(void* native_window,uint32_t software) {
    Renderer* result=nullptr;
    guarded([&]{result=new Renderer(static_cast<HWND>(native_window),software!=0);});
    return result;
}
void destroy(void* ptr){delete static_cast<Renderer*>(ptr);}
int draw(void* ptr,const NorthStarRenderFrame* frame) {
    return guarded([&]{if(!ptr||!frame)throw std::invalid_argument("Null D3D12 renderer/frame");
        static_cast<Renderer*>(ptr)->draw(*frame);});
}
int set_sky(void* ptr,const NorthStarSky* sky) {
    return guarded([&]{if(!ptr)throw std::invalid_argument("Null D3D12 skydome renderer");
        static_cast<Renderer*>(ptr)->set_sky(sky);});
}
int set_sky_uniforms(void* ptr,const NorthStarGpuSkyUniforms* data) {
    return guarded([&] {
        if(!ptr)throw std::invalid_argument("Null D3D12 GPU Sky renderer");
        static_cast<Renderer*>(ptr)->set_gpu_sky_uniforms(data);
    });
}
const NorthStarRenderSkyApi sky_gpu_api{
    NORTHSTARCPP_RENDER_SKY_ABI,sizeof(NorthStarRenderSkyApi),set_sky_uniforms};
int set_triangles(void* ptr,const NorthStarRenderTriangleVertex* data,UINT count) {
    return guarded([&]{if(!ptr)throw std::invalid_argument("Null D3D12 renderer");
        static_cast<Renderer*>(ptr)->set_triangles(data,count);});
}
int set_material_batches(void* renderer,const NorthStarMaterialBatch* batches,UINT count) {
    return guarded([&]{
        if(!renderer)throw std::invalid_argument("Null D3D12 material renderer");
        static_cast<Renderer*>(renderer)->set_material_batches(batches,count);
    });
}
const NorthStarRenderMaterialsApi materials_api{
    NORTHSTARCPP_RENDER_MATERIALS_ABI,set_material_batches};
int set_material_batches_v2(void* renderer,
    NorthStarMaterialBatchV2 const* batches,UINT count) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Null D3D12 graph renderer");
        static_cast<Renderer*>(renderer)->set_material_batches_v2(batches,count);
    });
}
const NorthStarRenderMaterialsApiV2 materials_api_v2{
    NORTHSTARCPP_RENDER_MATERIALS_V2_ABI,set_material_batches_v2};
int rigid_upload(void* instance,NorthStarRigidMesh const* mesh) {
    return guarded([&]{
        if(!instance||!mesh)throw std::invalid_argument("Rigid mesh missing");
        static_cast<Renderer*>(instance)->upload_rigid_mesh(*mesh);
    });
}
int rigid_clear(void* instance) {
    return guarded([&]{
        if(!instance)throw std::invalid_argument("Rigid renderer missing");
        static_cast<Renderer*>(instance)->clear_rigid_meshes();
    });
}
int rigid_instances(void* instance,
                    NorthStarRigidInstance const* draws,std::uint32_t count) {
    return guarded([&]{
        if(!instance)throw std::invalid_argument("Rigid renderer missing");
        static_cast<Renderer*>(instance)->set_rigid_instances(draws,count);
    });
}
int rigid_stats(void* instance,NorthStarRigidInstanceStats* result) {
    return guarded([&]{
        if(!instance||!result||result->struct_size<sizeof(*result))
            throw std::invalid_argument("Rigid stats buffer invalid");
        static_cast<Renderer const*>(instance)->read_rigid_stats(*result);
    });
}
int rigid_release_meshes(void* instance,const std::uint64_t* keys,
                        std::uint32_t count) {
    return guarded([&]{
        if(!instance)throw std::invalid_argument("Rigid GPU residency missing");
        static_cast<Renderer*>(instance)->release_rigid_meshes(keys,count);
    });
}
const NorthStarRenderRigidResidencyApi rigid_residency_api{
    NORTHSTARCPP_RENDER_RIGID_RESIDENCY_ABI,
    sizeof(NorthStarRenderRigidResidencyApi),rigid_release_meshes};
const NorthStarRenderRigidInstancesApi rigid_api{
    NORTHSTARCPP_RENDER_RIGID_INSTANCES_ABI,
    sizeof(NorthStarRenderRigidInstancesApi),
    rigid_upload,rigid_clear,rigid_instances,rigid_stats};
int set_temporal_instances(void* instance,
    const NorthStarRenderTemporalInstance* items,std::uint32_t count) {
    return guarded([&] {
        if(!instance)throw std::invalid_argument("D3D12 temporal renderer missing");
        static_cast<Renderer*>(instance)->set_temporal_instances(items,count);
    });
}
const NorthStarRenderTemporalInstancesApi temporal_instances_api{
    NORTHSTARCPP_RENDER_TEMPORAL_INSTANCES_ABI,set_temporal_instances};

const NorthStarRenderShaderReloadApiV2 shader_reload_v2{
    NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_ABI,
    sizeof(NorthStarRenderShaderReloadApiV2),
    [](void* instance,const char* const* refs,std::uint32_t count)->int {
        return guarded([&] {
            if(!instance)throw std::invalid_argument("Null D3D12 shader renderer");
            static_cast<Renderer*>(instance)->set_hot_material_references(refs,count);
        });
    },
    [](void* instance,const char* ref,std::uint32_t stage,std::uint32_t format,
       const std::uint8_t* bytes,std::uint32_t count)->int {
        return guarded([&] {
            if(!instance)throw std::invalid_argument("Null DXIL shader renderer");
            static_cast<Renderer*>(instance)->queue_hot_shader(
                ref,stage,format,bytes,count);
        });
    },
    [](void* instance,const char* ref,NorthStarShaderReloadStatus* status)->int {
        return guarded([&] {
            if(!instance||!status||
               !static_cast<Renderer*>(instance)->poll_hot_shader(ref,*status))
                throw std::invalid_argument("Invalid DXIL shader reload status");
        });
    }};
int set_film_look(void* renderer,NorthStarRenderPostFxSettings const* settings) {
    return guarded([&] {
        if(!renderer || !settings)
            throw std::invalid_argument("D3D12 Film Look requires renderer and settings");
        static_cast<Renderer*>(renderer)->set_film_look(*settings);
    });
}
int get_film_look(void* renderer,NorthStarRenderPostFxSettings* settings) {
    return guarded([&] {
        if(!renderer || !settings ||
           settings->struct_size<sizeof(NorthStarRenderPostFxSettings))
            throw std::invalid_argument("D3D12 Film Look settings buffer too small");
        *settings=static_cast<Renderer*>(renderer)->get_film_look();
    });
}
const NorthStarRenderPostFxApi postfx_api{
    NORTHSTARCPP_RENDER_POSTFX_ABI,sizeof(NorthStarRenderPostFxApi),
    set_film_look,get_film_look};
int read_memory(void* renderer,NorthStarRenderMemoryStats* result) {
    return guarded([&] {
        if(!renderer||!result)throw std::invalid_argument("Invalid D3D12 memory query");
        *result=static_cast<Renderer const*>(renderer)->memory_stats();
    });
}
const NorthStarRenderMemoryApi memory_api{NORTHSTARCPP_RENDER_MEMORY_ABI,read_memory};
int read_timing(void* renderer,NorthStarRenderTimingStats* result) {
    return guarded([&] {
        if(!renderer||!result)throw std::invalid_argument("Invalid D3D12 timing query");
        *result=static_cast<Renderer*>(renderer)->timing_stats();
    });
}
const NorthStarRenderTimingApi timing_api{NORTHSTARCPP_RENDER_TIMING_ABI,read_timing};
int set_dynamic_lights(void* renderer,NorthStarRenderLight const* lights,UINT count) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Null D3D12 light renderer");
        static_cast<Renderer*>(renderer)->set_lights(lights,count);
    });
}
const NorthStarRenderLightsApi lights_api{NORTHSTARCPP_RENDER_LIGHTS_ABI,set_dynamic_lights};
int set_editor_grid(void* renderer,
                    NorthStarRenderEditorGridSettings const* settings) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Missing D3D12 grid renderer");
        static_cast<Renderer*>(renderer)->set_editor_grid(settings);
    });
}
const NorthStarRenderEditorGridApi editor_grid_api{
    NORTHSTARCPP_RENDER_EDITOR_GRID_ABI,sizeof(NorthStarRenderEditorGridApi),
    set_editor_grid,3u};
int set_ui_rgba(void* renderer,const uint8_t* rgba,uint32_t w,
                uint32_t h,uint32_t stride) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Null D3D12 UI renderer");
        static_cast<Renderer*>(renderer)->set_ui_rgba(rgba,w,h,stride);
    });
}
const NorthStarRenderUiApi ui_api{NORTHSTARCPP_RENDER_UI_ABI,
    sizeof(NorthStarRenderUiApi),set_ui_rgba};
const NorthStarRenderUiEffectsApi ui_effects_api{
    NORTHSTARCPP_RENDER_UI_EFFECTS_ABI,
    sizeof(NorthStarRenderUiEffectsApi),
    [](void* renderer,const NorthStarRenderUiHoverFx* fx)->int {
        return guarded([&] {
            if(!renderer)throw std::invalid_argument("Null D3D12 UI renderer");
            static_cast<Renderer*>(renderer)->ui_hover_fx=
                fx?*fx:NorthStarRenderUiHoverFx{};
        });
    }};
// The game owns the complete HLSL UI implementation. Validate both stages
// before installing; invalid project code never destroys the generic fallback.
const NorthStarRenderUiShadersApi ui_shaders_api{
    NORTHSTARCPP_RENDER_UI_SHADERS_ABI,sizeof(NorthStarRenderUiShadersApi),
    [](void* renderer,const char* code,uint32_t length)->int {
        return guarded([&] {
            if(!renderer || (!code && length) || length>262144u)
                throw std::invalid_argument("Invalid project UI HLSL");
            auto* state=static_cast<Renderer*>(renderer);
            std::string source=code?std::string(code,code+length):std::string{};
            if(source.find('\0')!=std::string::npos)
                throw std::invalid_argument("Embedded NUL in UI HLSL");
            if(!source.empty()) {
                for(auto stage:{"vs","ps"}) {
                    Microsoft::WRL::ComPtr<ID3DBlob> compiled,error;
                    HRESULT result=D3DCompile(source.data(),source.size(),nullptr,
                        nullptr,nullptr,stage,
                        std::strcmp(stage,"vs")==0?"vs_5_0":"ps_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&compiled,&error);
                    if(FAILED(result)) {
                        std::string detail=error?
                            std::string(static_cast<const char*>(error->GetBufferPointer()),
                                        error->GetBufferSize()):"compiler failed";
                        throw std::runtime_error("Project UI HLSL: "+detail);
                    }
                }
            }
            if(source!=state->ui_shader_hlsl) {
                state->ui_shader_hlsl=std::move(source);
                state->ui_pso.Reset();
            }
        });
    },nullptr};
int read_cube_id(void* renderer,std::uint32_t x,std::uint32_t y,
                 std::int32_t* index) {
    return guarded([&] {
        if(!renderer || !index)
            throw std::invalid_argument("Invalid D3D12 GPU pick arguments");
        *index=static_cast<Renderer*>(renderer)->pick_cube(x,y);
    });
}
int pick_world_v2(void* renderer,NorthStarRenderPickVertex const* points,
                  std::uint32_t count) {
    return guarded([&]{if(!renderer)throw std::invalid_argument("D3D12 pick renderer missing");
        static_cast<Renderer*>(renderer)->set_pick_triangles(points,count);});
}
int pick_cubes_v2(void* renderer,std::uint64_t const* ids,std::uint32_t count) {
    return guarded([&]{if(!renderer)throw std::invalid_argument("D3D12 pick renderer missing");
        static_cast<Renderer*>(renderer)->set_pick_cubes(ids,count);});
}
int pick_read_v2(void* renderer,std::uint32_t x,std::uint32_t y,
                 std::uint64_t* out) {
    return guarded([&]{if(!renderer||!out)throw std::invalid_argument("D3D12 pick result missing");
        *out=static_cast<Renderer*>(renderer)->pick_entity(x,y);});
}
int async_pick_enqueue(void* renderer,NorthStarRenderPickAsyncRequest const* request) {
    return guarded([&] {
        if(!renderer || !request)
            throw std::invalid_argument("Missing async GPU picking request");
        if(!static_cast<Renderer*>(renderer)->enqueue_async_pick(*request))
            throw std::runtime_error("Async GPU pick ring busy; retry after fence");
    });
}
int async_pick_poll(void* renderer,std::uint64_t id,
                    NorthStarRenderPickAsyncResult* result) {
    return guarded([&] {
        if(!renderer || !result)
            throw std::invalid_argument("Missing async GPU picking result");
        *result=static_cast<Renderer*>(renderer)->poll_async_pick(id);
    });
}
int async_pick_invalidate(void* renderer,std::uint64_t minGeneration) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Missing async GPU picker");
        static_cast<Renderer*>(renderer)->invalidate_async_pick(minGeneration);
    });
}
const NorthStarRenderPickAsyncApi pick_async_api{
    NORTHSTARCPP_RENDER_PICK_ASYNC_ABI,sizeof(NorthStarRenderPickAsyncApi),
    async_pick_enqueue,async_pick_poll,async_pick_invalidate};
const NorthStarRenderPickApiV2 pick_api_v2{
    NORTHSTARCPP_RENDER_PICK_V2_ABI,sizeof(NorthStarRenderPickApiV2),
    pick_world_v2,pick_cubes_v2,pick_read_v2};
const NorthStarRenderPickApi pick_api{
    NORTHSTARCPP_RENDER_PICK_ABI,sizeof(NorthStarRenderPickApi),read_cube_id};
const NorthStarRenderApi api{create,destroy,draw,set_sky};
const NorthStarRenderTrianglesApi triangles_api{
    NORTHSTARCPP_RENDER_TRIANGLES_ABI,set_triangles};
int read_actual_gpu(void* renderer,NorthStarRenderDeviceInfo* result) {
    return guarded([&] {
        if(!renderer||!result)throw std::invalid_argument("Invalid GPU device query");
        auto* state=static_cast<Renderer*>(renderer);
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        auto const luid=state->device->GetAdapterLuid();
        if(FAILED(state->factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter))))
            throw std::runtime_error("D3D12 active adapter LUID lookup failed");
        DXGI_ADAPTER_DESC1 desc{};
        if(FAILED(adapter->GetDesc1(&desc)))
            throw std::runtime_error("D3D12 GetDesc1 failed");
        *result={};
        result->abi_version=NORTHSTARCPP_RENDER_DEVICE_ABI;
        result->struct_size=sizeof(*result);
        result->backend=NORTHSTARCPP_RENDER_DEVICE_D3D12;
        result->software=(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)?1u:0u;
        result->vendor_id=desc.VendorId;result->device_id=desc.DeviceId;
        D3D_FEATURE_LEVEL levels[]{
            D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS supported{
            static_cast<UINT>(std::size(levels)),levels,
            D3D_FEATURE_LEVEL_11_0};
        result->api_version=SUCCEEDED(state->device->CheckFeatureSupport(
            D3D12_FEATURE_FEATURE_LEVELS,&supported,sizeof(supported)))?
            static_cast<std::uint32_t>(supported.MaxSupportedFeatureLevel):0u;
        if(WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,
            result->adapter_name_utf8,sizeof(result->adapter_name_utf8),
            nullptr,nullptr)==0)
            throw std::runtime_error("D3D12 adapter name conversion failed");
    });
}
const NorthStarRenderDeviceApi device_api{
    NORTHSTARCPP_RENDER_DEVICE_ABI,read_actual_gpu};
const NorthStarRenderFeaturesApi features_api{
    NORTHSTARCPP_RENDER_FEATURES_ABI,sizeof(NorthStarRenderFeaturesApi),
    NS_RENDER_FEATURE_DEPTH_TEST | NS_RENDER_FEATURE_COLORED_GEOMETRY |
    NS_RENDER_FEATURE_TEXTURED_MATERIALS | NS_RENDER_FEATURE_MATERIAL_GRAPH |
    NS_RENDER_FEATURE_DYNAMIC_LIGHTS | NS_RENDER_FEATURE_GBUFFER |
    NS_RENDER_FEATURE_DEFERRED | NS_RENDER_FEATURE_CSM |
    NS_RENDER_FEATURE_PCSS | NS_RENDER_FEATURE_SSAO |
    NS_RENDER_FEATURE_HDR | NS_RENDER_FEATURE_BLOOM |
    NS_RENDER_FEATURE_EXPOSURE | NS_RENDER_FEATURE_TONEMAP |
    NS_RENDER_FEATURE_FXAA | NS_RENDER_FEATURE_GPU_TIMING |
    NS_RENDER_FEATURE_GPU_TRANSIENT_ALIASING};
int request_scene_capture(void* renderer) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Null GPU capture renderer");
        auto& r=*static_cast<Renderer*>(renderer);
        r.capture_rgba.clear();
        r.capture_width=r.capture_height=0u;
        r.capture_pending=true;
    });
}
int read_scene_capture(void* renderer,std::uint8_t* pixels,
        std::uint32_t capacity,std::uint32_t* width,std::uint32_t* height) {
    return guarded([&] {
        if(!renderer||!width||!height)
            throw std::invalid_argument("Null GPU capture query");
        auto& r=*static_cast<Renderer*>(renderer);
        if(r.capture_rgba.empty() || r.capture_pending)
            throw std::runtime_error("GPU scene capture is not ready");
        *width=r.capture_width;
        *height=r.capture_height;
        if(!pixels && capacity==0)return;
        if(!pixels||capacity<r.capture_rgba.size())
            throw std::invalid_argument("GPU capture destination buffer too small");
        std::memcpy(pixels,r.capture_rgba.data(),r.capture_rgba.size());
    });
}
const NorthStarRenderCaptureApi capture_api{
    NORTHSTARCPP_RENDER_CAPTURE_ABI,sizeof(NorthStarRenderCaptureApi),
    request_scene_capture,read_scene_capture
};
const NorthStarModule module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",NORTHSTARCPP_RENDER_ABI,
    sizeof(api),&api,last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(uint32_t host,const char* capability,uint32_t version) {
    if(version==NORTHSTARCPP_RENDER_SKY_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_SKY_MODULE_ABI,
            sizeof(sky_gpu_api),&sky_gpu_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_CAPTURE_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_CAPTURE_MODULE_ABI,
            sizeof(capture_api),&capture_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_FRAME_MEMORY_MODULE_ABI) {
        auto const& api=northstarcpp::diagnostics::frame_memory::api;
        static const NorthStarModule extension{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_FRAME_MEMORY_MODULE_ABI,sizeof(api),&api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_TEMPORAL_INSTANCES_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_TEMPORAL_INSTANCES_MODULE_ABI,
            sizeof(temporal_instances_api),&temporal_instances_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI,
            sizeof(postfx_api),&postfx_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI,
            sizeof(shader_reload_v2),&shader_reload_v2,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI) {
        static const NorthStarModule ext{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI,sizeof(pick_async_api),
            &pick_async_api,last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI) {
        static const NorthStarModule ext{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI,sizeof(pick_api_v2),
            &pick_api_v2,last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_PICK_MODULE_ABI) {
        static const NorthStarModule ext{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_PICK_MODULE_ABI,sizeof(pick_api),&pick_api,
            last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_UI_SHADERS_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.0.0",
            NORTHSTARCPP_RENDER_UI_SHADERS_MODULE_ABI,
            sizeof(ui_shaders_api),&ui_shaders_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_UI_EFFECTS_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.0.0",
            NORTHSTARCPP_RENDER_UI_EFFECTS_MODULE_ABI,sizeof(ui_effects_api),
            &ui_effects_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_UI_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.8.0",
            NORTHSTARCPP_RENDER_UI_MODULE_ABI,sizeof(ui_api),
            &ui_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.7.0",
            NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI,sizeof(editor_grid_api),
            &editor_grid_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI) {
        static const NorthStarModule ext{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "d3d12","1.6.0",
            NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI,sizeof(features_api),
            &features_api,last_error};
        return match(ext,host,capability,version);
    }

    if(version==NORTHSTARCPP_RENDER_RIGID_RESIDENCY_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "d3d12","1.8.1",NORTHSTARCPP_RENDER_RIGID_RESIDENCY_MODULE_ABI,
            sizeof(rigid_residency_api),&rigid_residency_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "d3d12","1.8.0",NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI,
            sizeof(rigid_api),&rigid_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI,sizeof(materials_api_v2),
            &materials_api_v2,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "d3d12","1.6.0",NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI,
            sizeof(lights_api),&lights_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_TIMING_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "d3d12","1.6.0",NORTHSTARCPP_RENDER_TIMING_MODULE_ABI,
            sizeof(timing_api),&timing_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI) {
        static const NorthStarModule info{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI,
            sizeof(device_api),&device_api,last_error};
        return match(info,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",
            NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI,sizeof(materials_api),
            &materials_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.1.0",
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI,sizeof(triangles_api),
            &triangles_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.1.0",
            NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI,sizeof(memory_api),&memory_api,last_error};
        return match(extension,host,capability,version);
    }
    return match(module,host,capability,version);
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderTrianglesApi*
northstar_render_get_triangles_api(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_TRIANGLES_ABI?&triangles_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderMemoryApi*
northstar_render_get_memory_api(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_MEMORY_ABI?&memory_api:nullptr;
}


extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderLightsApi*
northstar_render_get_lights_api(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_LIGHTS_ABI?&lights_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderTimingApi*
northstar_render_get_timing_api(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_TIMING_ABI?&timing_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderMaterialsApi*
northstar_render_get_materials_api(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_MATERIALS_ABI?&materials_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderMaterialsApiV2*
northstar_render_get_materials_api_v2(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_MATERIALS_V2_ABI?&materials_api_v2:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderEditorGridApi*
northstar_render_get_editor_grid_api(uint32_t requested_version) {
    return requested_version==NORTHSTARCPP_RENDER_EDITOR_GRID_ABI?
        &editor_grid_api:nullptr;
}
