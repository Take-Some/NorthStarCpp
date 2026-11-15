// NorthStarCpp Vulkan GPU provider, experimental Win32 baseline.
// Implementation is isolated from ABI 5 and remains non-default.
#include "renderer_private.hpp"
#include "module_provider.hpp"
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_features_api.h>
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
const NorthStarRenderApi render_api{create,destroy,draw,sky};
const NorthStarRenderTrianglesApi triangles_api{
    NORTHSTARCPP_RENDER_TRIANGLES_ABI,triangles};
constexpr const char* provider_version="0.1.0";
const NorthStarRenderFeaturesApi features_api{
    NORTHSTARCPP_RENDER_FEATURES_ABI,sizeof(NorthStarRenderFeaturesApi),
    NS_RENDER_FEATURE_COLORED_GEOMETRY | NS_RENDER_FEATURE_GPU_TIMING};

const NorthStarModule module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"vulkan",provider_version,
    NORTHSTARCPP_RENDER_ABI,sizeof(render_api),&render_api,last_error};
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
    if(version==NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI)
        return match(device_module,host,capability,version);
    if(version==NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI)
        return match(features_module,host,capability,version);
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

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderTimingApi*
northstar_render_get_timing_api(std::uint32_t version) {
    return version==NORTHSTARCPP_RENDER_TIMING_ABI?&timing_api:nullptr;
}
