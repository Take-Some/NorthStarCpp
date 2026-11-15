// NorthStarCpp D3D12 engine.render provider — stable C ABI 5.
#include "renderer_private.hpp"
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_device_api.h>
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
const NorthStarModule module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",NORTHSTARCPP_RENDER_ABI,
    sizeof(api),&api,last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(uint32_t host,const char* capability,uint32_t version) {
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
