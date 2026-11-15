// NorthStarCpp D3D12 engine.render provider — stable C ABI 5.
#include "renderer_private.hpp"
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
const NorthStarRenderApi api{create,destroy,draw,set_sky};
const NorthStarRenderTrianglesApi triangles_api{
    NORTHSTARCPP_RENDER_TRIANGLES_ABI,set_triangles};
const NorthStarModule module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.6.0",NORTHSTARCPP_RENDER_ABI,
    sizeof(api),&api,last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(uint32_t host,const char* capability,uint32_t version) {
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
