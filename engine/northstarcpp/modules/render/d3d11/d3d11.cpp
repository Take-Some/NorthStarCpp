// D3D11 engine.render provider ABI remains version 5.
#include "renderer_private.hpp"
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_device_api.h>
#include <wrl/client.h>
namespace {
using namespace northstarcpp::module_provider;
using Renderer = northstarcpp::render::d3d11::detail::RendererState;
void* create(void* window, uint32_t software)
{
    Renderer* result = nullptr;
    guarded([&] { result = new Renderer(static_cast<HWND>(window), software != 0); });
    return result;
}

void destroy(void* renderer) { delete static_cast<Renderer*>(renderer); }

int draw(void* renderer, NorthStarRenderFrame const* frame)
{
    return guarded([&] {
        if (!renderer || !frame)
            throw std::runtime_error("Missing renderer or frame");
        static_cast<Renderer*>(renderer)->draw(*frame);
    });
}

int set_sky(void* renderer, NorthStarSky const* sky)
{
    return guarded([&] {
        if (!renderer)
            throw std::runtime_error("Missing skydome renderer");
        static_cast<Renderer*>(renderer)->set_sky(sky);
    });
}

int read_memory_stats(void* renderer,NorthStarRenderMemoryStats* result)
{
    return guarded([&] {
        if (!renderer || !result)
            throw std::invalid_argument("Invalid D3D11 memory stats query");
        *result=static_cast<Renderer const*>(renderer)->memory_stats();
    });
}
int set_authored_materials(void* renderer,
                           NorthStarMaterialBatch const* batches,std::uint32_t count)
{
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Missing GPU material renderer");
        static_cast<Renderer*>(renderer)->set_material_batches(batches,count);
    });
}
const NorthStarRenderMaterialsApi materials_api{
    NORTHSTARCPP_RENDER_MATERIALS_ABI,set_authored_materials};
int set_authored_materials_v2(void* renderer,
    NorthStarMaterialBatchV2 const* batches,std::uint32_t count)
{
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Missing Material Graph renderer");
        static_cast<Renderer*>(renderer)->set_material_batches_v2(batches,count);
    });
}
const NorthStarRenderMaterialsApiV2 materials_api_v2{
    NORTHSTARCPP_RENDER_MATERIALS_V2_ABI,set_authored_materials_v2};
int set_native_gpu_skinning(void* renderer,
    NorthStarSkinnedMaterialBatch const* batches,std::uint32_t count)
{
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Missing native GPU skin renderer");
        static_cast<Renderer*>(renderer)->set_skinned_material_batches(batches,count);
    });
}
const NorthStarRenderSkinnedMaterialsApi skinned_materials_api{
    NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI,set_native_gpu_skinning};
int set_render_triangles(void* renderer,
                         NorthStarRenderTriangleVertex const* vertices,std::uint32_t count)
{
    return guarded([&] {
        if (!renderer) throw std::invalid_argument("Missing geometry renderer");
        static_cast<Renderer*>(renderer)->set_triangles(vertices,count);
    });
}
int set_world_surface_texture(void* renderer,
                              NorthStarSurfaceTexture const* image) {
    return guarded([&] {
        if(!renderer || !image)throw std::invalid_argument("Missing surface texture");
        static_cast<Renderer*>(renderer)->set_surface_texture(*image);
    });
}
int set_world_surface_cubes(void* renderer,
                            std::uint32_t const* ids,std::uint32_t count) {
    return guarded([&] {
        if(!renderer)throw std::invalid_argument("Missing surface renderer");
        static_cast<Renderer*>(renderer)->set_cube_surfaces(ids,count);
    });
}
const NorthStarRenderSurfacesApi surfaces_api{
    NORTHSTARCPP_RENDER_SURFACES_ABI,
    set_world_surface_texture,set_world_surface_cubes};
const NorthStarRenderTrianglesApi triangles_api{
    NORTHSTARCPP_RENDER_TRIANGLES_ABI,set_render_triangles};
const NorthStarRenderMemoryApi memory_api{
    NORTHSTARCPP_RENDER_MEMORY_ABI,read_memory_stats};
int set_character_model(void* renderer, NorthStarCharacterPart const* parts, std::uint32_t count)
{
    return guarded([&] {
        if (!renderer) throw std::invalid_argument("Missing character renderer");
        static_cast<Renderer*>(renderer)->set_character(parts,count);
    });
}
int set_character_pose(void* renderer, NorthStarCharacterVertex const* data, std::uint32_t count)
{
    return guarded([&] {
        if (!renderer) throw std::invalid_argument("Missing character renderer");
        static_cast<Renderer*>(renderer)->set_character_pose(data,count);
    });
}
const NorthStarRenderCharacterApi character_api{
    NORTHSTARCPP_RENDER_CHARACTER_ABI, set_character_model, set_character_pose};
const NorthStarRenderApi api{create, destroy, draw, set_sky};
int read_actual_gpu(void* renderer,NorthStarRenderDeviceInfo* result) {
    return guarded([&] {
        if(!renderer||!result)throw std::invalid_argument("Invalid GPU device query");
        auto* state=static_cast<Renderer*>(renderer);
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        if(FAILED(state->device.get()->QueryInterface(IID_PPV_ARGS(&dxgi))))
            throw std::runtime_error("D3D11 device has no IDXGIDevice");
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if(FAILED(dxgi->GetAdapter(&adapter)))
            throw std::runtime_error("D3D11 cannot resolve active adapter");
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
        if(FAILED(adapter.As(&adapter1)))
            throw std::runtime_error("D3D11 cannot identify adapter type");
        DXGI_ADAPTER_DESC1 desc{};
        if(FAILED(adapter1->GetDesc1(&desc)))
            throw std::runtime_error("D3D11 GetDesc1 failed");
        *result={};
        result->abi_version=NORTHSTARCPP_RENDER_DEVICE_ABI;
        result->struct_size=sizeof(*result);
        result->backend=NORTHSTARCPP_RENDER_DEVICE_D3D11;
        result->software=(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)?1u:0u;
        result->vendor_id=desc.VendorId;result->device_id=desc.DeviceId;
        result->api_version=static_cast<std::uint32_t>(state->device->GetFeatureLevel());
        if(WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,
            result->adapter_name_utf8,sizeof(result->adapter_name_utf8),
            nullptr,nullptr)==0)
            throw std::runtime_error("D3D11 adapter name conversion failed");
    });
}
const NorthStarRenderDeviceApi device_api{
    NORTHSTARCPP_RENDER_DEVICE_ABI,read_actual_gpu};
const NorthStarRenderFeaturesApi features_api{
    NORTHSTARCPP_RENDER_FEATURES_ABI,sizeof(NorthStarRenderFeaturesApi),
    NS_RENDER_FEATURE_COLORED_GEOMETRY |
    NS_RENDER_FEATURE_TEXTURED_MATERIALS | NS_RENDER_FEATURE_MATERIAL_GRAPH |
    NS_RENDER_FEATURE_GPU_SKINNING | NS_RENDER_FEATURE_WORLD_SURFACES |
    NS_RENDER_FEATURE_CHARACTER};
const NorthStarModule module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER, "Direct3D 11", "5.0.0", NORTHSTARCPP_RENDER_ABI,
    sizeof(api), &api, last_error};

} // namespace

extern "C" const NorthStarModule* northstar_module_query(uint32_t host,
    char const* capability, uint32_t version)
{
    if(version==NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI) {
        static const NorthStarModule ext{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "Direct3D 11","5.0.0",
            NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI,sizeof(features_api),
            &features_api,last_error};
        return match(ext,host,capability,version);
    }

    if (version == NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "Direct3D 11","5.5.0",NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI,
            sizeof(skinned_materials_api),&skinned_materials_api,last_error};
        return match(extension,host,capability,version);
    }
    if (version == NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "Direct3D 11","5.4.0",NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI,
            sizeof(materials_api_v2),&materials_api_v2,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI) {
        static const NorthStarModule info{
            sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,"Direct3D 11","5.0.0",
            NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI,
            sizeof(device_api),&device_api,last_error};
        return match(info,host,capability,version);
    }
    if (version == NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "Direct3D 11","5.3.0",NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI,
            sizeof(materials_api),&materials_api,last_error};
        return match(extension,host,capability,version);
    }
    if (version == NORTHSTARCPP_RENDER_CHARACTER_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "Direct3D 11","5.2.0",NORTHSTARCPP_RENDER_CHARACTER_MODULE_ABI,
            sizeof(character_api),&character_api,last_error};
        return match(extension,host,capability,version);
    }
    if (version == NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,
            "Direct3D 11","5.1.0",NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI,
            sizeof(surfaces_api),&surfaces_api,last_error};
        return match(extension,host,capability,version);
    }
    if (version == NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER, "Direct3D 11", "5.0.0",
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI, sizeof(triangles_api), &triangles_api, last_error};
        return match(extension, host, capability, version);
    }
    if (version == NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER, "Direct3D 11", "5.0.0",
            NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI, sizeof(memory_api), &memory_api, last_error};
        return match(extension, host, capability, version);
    }
    return match(module, host, capability, version);
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarRenderTrianglesApi const* northstar_render_get_triangles_api(uint32_t version)
{
    return version==NORTHSTARCPP_RENDER_TRIANGLES_ABI ? &triangles_api : nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarRenderMemoryApi const* northstar_render_get_memory_api(uint32_t version)
{
    return version==NORTHSTARCPP_RENDER_MEMORY_ABI ? &memory_api : nullptr;
}


extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarRenderMaterialsApi const* northstar_render_get_materials_api(uint32_t version)
{
    return version==NORTHSTARCPP_RENDER_MATERIALS_ABI?&materials_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarRenderMaterialsApiV2 const* northstar_render_get_materials_api_v2(
    uint32_t version)
{
    return version==NORTHSTARCPP_RENDER_MATERIALS_V2_ABI?&materials_api_v2:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarRenderSkinnedMaterialsApi const* northstar_render_get_skinning_api(
    uint32_t version)
{
    return version==NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI?
        &skinned_materials_api:nullptr;
}
