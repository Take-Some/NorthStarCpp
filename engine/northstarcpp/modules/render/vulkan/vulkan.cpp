// NorthStarCpp Vulkan GPU provider, experimental Win32 baseline.
// Implementation is isolated from ABI 5 and remains non-default.
#include "../../shared/frame_memory_counters.hpp"
#include "renderer_private.hpp"
#include "module_provider.hpp"
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_lights_api.h>
#include <northstarcpp/render_pick_api.h>
#include <stdexcept>
#include <cstdint>
using northstarcpp::render::vulkan::Renderer;
using namespace northstarcpp::module_provider;

namespace {
void* create(void* window,std::uint32_t software) {
    Renderer* object=nullptr;
    guarded([&]{object=new Renderer(static_cast<HWND>(window),software!=0);});
    return object;
}
void destroy(void* ptr) {delete static_cast<Renderer*>(ptr);}
int draw(void* ptr,const NorthStarRenderFrame* frame) {
    return guarded([&] {
        if(!ptr||!frame)throw std::invalid_argument("Vulkan renderer/frame null");
        static_cast<Renderer*>(ptr)->draw(*frame);
    });
}
int sky(void* ptr,const NorthStarSky* data) {
    return guarded([&] {
        if(!ptr)throw std::invalid_argument("Vulkan renderer null");
        static_cast<Renderer*>(ptr)->set_sky(data);
    });
}
int triangles(void* ptr,const NorthStarRenderTriangleVertex* data,std::uint32_t count) {
    return guarded([&] {
        if(!ptr)throw std::invalid_argument("Vulkan renderer null");
        static_cast<Renderer*>(ptr)->set_triangles(data,count);
    });
}
int set_materials(void* instance,
    const NorthStarMaterialBatch* batches,std::uint32_t count) {
    return guarded([&] {
        if(!instance)throw std::invalid_argument("Vulkan material renderer missing");
        static_cast<Renderer*>(instance)->set_material_batches(batches,count);
    });
}
const NorthStarRenderMaterialsApi materials_api{
    NORTHSTARCPP_RENDER_MATERIALS_ABI,set_materials};
int set_materials_v2(void* instance,
    const NorthStarMaterialBatchV2* batches,std::uint32_t count) {
    return guarded([&] {
        if(!instance)throw std::invalid_argument("Vulkan graph renderer missing");
        static_cast<Renderer*>(instance)->set_material_batches_v2(batches,count);
    });
}
const NorthStarRenderMaterialsApiV2 materials_v2_api{
    NORTHSTARCPP_RENDER_MATERIALS_V2_ABI,set_materials_v2};
int set_skinned_materials(void* instance,
    const NorthStarSkinnedMaterialBatch* batches,std::uint32_t count) {
    return guarded([&] {
        if(!instance)throw std::invalid_argument("Vulkan GPU skin renderer missing");
        static_cast<Renderer*>(instance)->set_skinned_material_batches(batches,count);
    });
}
const NorthStarRenderSkinnedMaterialsApi skinning_api{
    NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI,set_skinned_materials};
int set_surface_texture(void* instance,const NorthStarSurfaceTexture* source) {
    return guarded([&]{
        if(!instance||!source)
            throw std::invalid_argument("Missing Vulkan world BC texture");
        static_cast<Renderer*>(instance)->set_world_texture(*source);
    });
}
int set_surface_selectors(void* instance,const std::uint32_t* ids,
                          std::uint32_t count) {
    return guarded([&]{
        if(!instance)
            throw std::invalid_argument("Missing Vulkan world surface renderer");
        static_cast<Renderer*>(instance)->set_cube_surfaces(ids,count);
    });
}
const NorthStarRenderSurfacesApi surfaces_api{
    NORTHSTARCPP_RENDER_SURFACES_ABI,
    set_surface_texture,set_surface_selectors};



int set_lights(void* instance,const NorthStarRenderLight* lights,
               std::uint32_t count) {
    return guarded([&] {
        if(!instance)throw std::invalid_argument("Vulkan light renderer missing");
        static_cast<Renderer*>(instance)->set_lights(lights,count);
    });
}
const NorthStarRenderLightsApi lights_api{
    NORTHSTARCPP_RENDER_LIGHTS_ABI,set_lights};

int read_timing(void* instance,NorthStarRenderTimingStats* stats) {
    return guarded([&] {
        if(!instance||!stats)throw std::invalid_argument("Invalid Vulkan timing query");
        if(!static_cast<Renderer*>(instance)->timing(*stats))
            throw std::runtime_error("Vulkan GPU timestamps unavailable");
    });
}
int read_device(void* instance,NorthStarRenderDeviceInfo* result) {
    return guarded([&] {
        if(!instance||!result)throw std::invalid_argument("Invalid Vulkan GPU query");
        if(!static_cast<Renderer*>(instance)->device_info(*result))
            throw std::runtime_error("Vulkan physical device unavailable");
    });
}
const NorthStarRenderDeviceApi device_api{
    NORTHSTARCPP_RENDER_DEVICE_ABI,read_device};
const NorthStarRenderTimingApi timing_api{NORTHSTARCPP_RENDER_TIMING_ABI,read_timing};
int set_ui_rgba(void* renderer,const std::uint8_t* pixels,
                std::uint32_t width,std::uint32_t height,std::uint32_t stride) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Null Vulkan UI renderer");
        static_cast<Renderer*>(renderer)->set_ui_rgba(pixels,width,height,stride);
    });
}
const NorthStarRenderUiApi ui_api{NORTHSTARCPP_RENDER_UI_ABI,
    sizeof(NorthStarRenderUiApi),set_ui_rgba};
const NorthStarRenderUiEffectsApi ui_effects_api{
    NORTHSTARCPP_RENDER_UI_EFFECTS_ABI,
    sizeof(NorthStarRenderUiEffectsApi),
    [](void* renderer,const NorthStarRenderUiHoverFx* fx)->int {
        return guarded([&] {
            if(!renderer)throw std::invalid_argument("Null Vulkan UI renderer");
            static_cast<Renderer*>(renderer)->set_ui_hover_fx(fx);
        });
    }};
const NorthStarRenderUiShadersApi ui_shaders_api{
    NORTHSTARCPP_RENDER_UI_SHADERS_ABI,sizeof(NorthStarRenderUiShadersApi),
    nullptr,
    [](void* renderer,const uint32_t* words,uint32_t count)->int {
        return guarded([&] {
            if(!renderer || (!words && count) || count>262144u)
                throw std::invalid_argument("Invalid project UI SPIR-V");
            if(words && (count<5 || words[0]!=0x07230203u))
                throw std::invalid_argument("Project UI fragment is not SPIR-V");
            static_cast<Renderer*>(renderer)->set_ui_fragment_spirv(words,count);
        });
    }};
int read_cube_id(void* instance,std::uint32_t x,std::uint32_t y,
                 std::int32_t* result) {
    return guarded([&] {
        if(!instance || !result)
            throw std::invalid_argument("Invalid Vulkan GPU ID picking request");
        *result=static_cast<Renderer*>(instance)->pick_cube(x,y);
    });
}
int set_pick_world(void* ptr,NorthStarRenderPickVertex const* vertices,
                   std::uint32_t count) {
    return guarded([&] {if(!ptr)throw std::invalid_argument("Missing Vulkan picker");
        static_cast<Renderer*>(ptr)->set_pick_triangles(vertices,count);});
}
int set_pick_cube_ids(void* ptr,std::uint64_t const* ids,std::uint32_t count) {
    return guarded([&] {if(!ptr)throw std::invalid_argument("Missing Vulkan picker");
        static_cast<Renderer*>(ptr)->set_pick_cubes(ids,count);});
}
int read_pick_entity(void* ptr,std::uint32_t x,std::uint32_t y,
                     std::uint64_t* entity) {
    return guarded([&] {if(!ptr||!entity)throw std::invalid_argument("Invalid Vulkan picker");
        *entity=static_cast<Renderer*>(ptr)->pick_entity(x,y);});
}
int async_pick_enqueue(void* renderer,NorthStarRenderPickAsyncRequest const* request) {
    return guarded([&] {
        if(!renderer||!request)
            throw std::invalid_argument("Missing Vulkan async pick request");
        if(!static_cast<Renderer*>(renderer)->enqueue_async_pick(*request))
            throw std::runtime_error("Async Vulkan pick ring busy");
    });
}
int async_pick_poll(void* renderer,std::uint64_t id,
                    NorthStarRenderPickAsyncResult* result) {
    return guarded([&] {
        if(!renderer||!result)
            throw std::invalid_argument("Missing Vulkan async pick result");
        *result=static_cast<Renderer*>(renderer)->poll_async_pick(id);
    });
}
int async_pick_invalidate(void* renderer,std::uint64_t minGeneration) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Missing Vulkan async picker");
        static_cast<Renderer*>(renderer)->invalidate_async_pick(minGeneration);
    });
}
const NorthStarRenderPickAsyncApi pick_async_api{
    NORTHSTARCPP_RENDER_PICK_ASYNC_ABI,sizeof(NorthStarRenderPickAsyncApi),
    async_pick_enqueue,async_pick_poll,async_pick_invalidate};
const NorthStarRenderPickApiV2 pick_api_v2{
    NORTHSTARCPP_RENDER_PICK_V2_ABI,sizeof(NorthStarRenderPickApiV2),
    set_pick_world,set_pick_cube_ids,read_pick_entity};
const NorthStarRenderPickApi pick_api{
    NORTHSTARCPP_RENDER_PICK_ABI,sizeof(NorthStarRenderPickApi),read_cube_id};
int shader_bind_materials(void* instance,const char* const* refs,std::uint32_t count) {
    return guarded([&] {
        if(!instance)throw std::invalid_argument("Null Vulkan material shader binding");
        static_cast<Renderer*>(instance)->set_material_references(refs,count);
    });
}
int shader_queue_fragment(void* instance,const char* ref,
                          const std::uint32_t* spirv,std::uint32_t words) {
    return guarded([&] {
        if(!instance)throw std::invalid_argument("Null Vulkan shader reload renderer");
        static_cast<Renderer*>(instance)->queue_material_fragment_spirv(ref,spirv,words);
    });
}
int shader_status(void* instance,const char* ref,NorthStarShaderReloadStatus* status) {
    return guarded([&] {
        if(!instance||!status ||
           !static_cast<Renderer*>(instance)->shader_reload_status(ref,*status))
            throw std::invalid_argument("Invalid Vulkan shader reload status request");
    });
}
int shader_queue_stage(void* instance,const char* ref,std::uint32_t stage,
                       std::uint32_t format,const std::uint8_t* bytes,
                       std::uint32_t size) {
    return guarded([&] {
        if(!instance || format!=NS_SHADER_BINARY_SPIRV || !bytes ||
           size<24 || size%4 || size>NORTHSTARCPP_SHADER_RELOAD_MAX_BINARY_BYTES)
            throw std::invalid_argument("Vulkan hot reload requires SPIR-V");
        std::vector<std::uint32_t> words(size/4);
        std::memcpy(words.data(),bytes,size);
        static_cast<Renderer*>(instance)->queue_material_stage_spirv(
            ref,stage,words.data(),static_cast<std::uint32_t>(words.size()));
    });
}
int set_postfx_look(void* instance,NorthStarRenderPostFxSettings const* value) {
    return guarded([&] {
        if(!instance||!value)
            throw std::invalid_argument("Missing Vulkan filmic look parameters");
        static_cast<Renderer*>(instance)->set_film_look(*value);
    });
}
int get_postfx_look(void* instance,NorthStarRenderPostFxSettings* result) {
    return guarded([&] {
        if(!instance||!result||result->struct_size<sizeof(*result))
            throw std::invalid_argument("Vulkan PostFX output buffer too small");
        *result=static_cast<Renderer*>(instance)->get_film_look();
    });
}
const NorthStarRenderPostFxApi postfx_api{
    NORTHSTARCPP_RENDER_POSTFX_ABI,sizeof(NorthStarRenderPostFxApi),
    set_postfx_look,get_postfx_look};
const NorthStarRenderShaderReloadApiV2 shader_reload_v2{
    NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_ABI,
    sizeof(NorthStarRenderShaderReloadApiV2),
    shader_bind_materials,shader_queue_stage,shader_status};
const NorthStarRenderShaderReloadApi shader_reload_api{
    NORTHSTARCPP_RENDER_SHADER_RELOAD_ABI,sizeof(NorthStarRenderShaderReloadApi),
    shader_bind_materials,shader_queue_fragment,shader_status};
const NorthStarRenderApi render_api{create,destroy,draw,sky};
const NorthStarRenderTrianglesApi triangles_api{
    NORTHSTARCPP_RENDER_TRIANGLES_ABI,triangles};
constexpr const char* provider_version="0.1.0";
const NorthStarRenderFeaturesApi features_api{
    NORTHSTARCPP_RENDER_FEATURES_ABI,sizeof(NorthStarRenderFeaturesApi),
    NS_RENDER_FEATURE_DEPTH_TEST | NS_RENDER_FEATURE_COLORED_GEOMETRY |
    NS_RENDER_FEATURE_TEXTURED_MATERIALS | NS_RENDER_FEATURE_MATERIAL_GRAPH |
    NS_RENDER_FEATURE_GPU_SKINNING | NS_RENDER_FEATURE_WORLD_SURFACES |
    NS_RENDER_FEATURE_DYNAMIC_LIGHTS | NS_RENDER_FEATURE_GPU_TIMING};

const NorthStarModule module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_ABI,sizeof(render_api),&render_api,last_error};
const NorthStarModule light_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI,
    sizeof(lights_api),&lights_api,last_error};
const NorthStarModule surfaces_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI,
    sizeof(surfaces_api),&surfaces_api,last_error};
const NorthStarModule skinning_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI,
    sizeof(skinning_api),&skinning_api,last_error};
const NorthStarModule material_v2_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI,
    sizeof(materials_v2_api),&materials_v2_api,last_error};
const NorthStarModule material_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI,
    sizeof(materials_api),&materials_api,last_error};
const NorthStarModule triangle_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI,
    sizeof(triangles_api),&triangles_api,last_error};
const NorthStarModule device_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI,
    sizeof(device_api),&device_api,last_error};
const NorthStarModule ui_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan","0.2.0",
    NORTHSTARCPP_RENDER_UI_MODULE_ABI,
    sizeof(ui_api),&ui_api,last_error};
const NorthStarModule features_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI,
    sizeof(features_api),&features_api,last_error};
const NorthStarModule timing_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_TIMING_MODULE_ABI,
    sizeof(timing_api),&timing_api,last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(std::uint32_t host,const char* capability,std::uint32_t version) {
    if(version==NORTHSTARCPP_RENDER_FRAME_MEMORY_MODULE_ABI) {
        auto const& api=northstarcpp::diagnostics::frame_memory::api;
        static const NorthStarModule extension{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
            NORTHSTARCPP_RENDER_FRAME_MEMORY_MODULE_ABI,sizeof(api),&api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI) {
        static const NorthStarModule ext{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
            NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI,
            sizeof(postfx_api),&postfx_api,last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI) {
        static const NorthStarModule ext{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
            NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI,
            sizeof(shader_reload_v2),&shader_reload_v2,last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI,
            sizeof(shader_reload_api),&shader_reload_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI) {
        static const NorthStarModule ext{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
            NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI,sizeof(pick_async_api),
            &pick_async_api,last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI) {
        static const NorthStarModule ext{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
            NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI,sizeof(pick_api_v2),
            &pick_api_v2,last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_PICK_MODULE_ABI) {
        static const NorthStarModule ext{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
            NORTHSTARCPP_RENDER_PICK_MODULE_ABI,sizeof(pick_api),&pick_api,
            last_error};
        return match(ext,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_UI_SHADERS_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan","1.0.0",
            NORTHSTARCPP_RENDER_UI_SHADERS_MODULE_ABI,
            sizeof(ui_shaders_api),&ui_shaders_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_UI_EFFECTS_MODULE_ABI) {
        static const NorthStarModule extension{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"vulkan","1.0.0",
            NORTHSTARCPP_RENDER_UI_EFFECTS_MODULE_ABI,sizeof(ui_effects_api),
            &ui_effects_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_UI_MODULE_ABI)
        return match(ui_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI)
        return match(device_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI)
        return match(features_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI)
        return match(light_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI)
        return match(surfaces_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI)
        return match(skinning_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI)
        return match(material_v2_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI)
        return match(material_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI)
        return match(triangle_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_TIMING_MODULE_ABI)
        return match(timing_module,host,capability,version);
    return match(module,host,capability,version);
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderTrianglesApi*
northstar_render_get_triangles_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_TRIANGLES_ABI?&triangles_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderLightsApi*
northstar_render_get_lights_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_LIGHTS_ABI?&lights_api:nullptr;
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderSurfacesApi*
northstar_render_get_surfaces_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_SURFACES_ABI?&surfaces_api:nullptr;
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderSkinnedMaterialsApi*
northstar_render_get_skinning_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI?
        &skinning_api:nullptr;
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderMaterialsApiV2*
northstar_render_get_materials_v2_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_MATERIALS_V2_ABI?&materials_v2_api:nullptr;
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderMaterialsApi*
northstar_render_get_materials_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_MATERIALS_ABI?&materials_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderTimingApi*
northstar_render_get_timing_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_TIMING_ABI?&timing_api:nullptr;
}
