// NorthStarCpp Vulkan GPU provider, experimental Win32 baseline.
// Implementation is isolated from ABI 5 and remains non-default.
#include "renderer_private.hpp"
#include "module_provider.hpp"
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_lights_api.h>
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
