// ABI-validated provider diagnostics shared by game tooling and Qt Editor.
// The implementation delegates to Module; it never duplicates plugin discovery.
#include <northstarcpp/provider_inspector.hpp>
#include "module_loader.hpp"
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/ui_plugin.h>
#include <northstarcpp/vehicle_plugin.h>
#include <exception>
#include <utility>

namespace northstarcpp {
namespace {
template<class Api>
ProviderResolution inspect(char const* capability, std::uint32_t abi,
                           char const* requested_provider = "") {
    ProviderResolution status;
    status.capability = capability;
    status.api_abi = abi;
    status.requested_provider = requested_provider;
    try {
        Module module(capability, requested_provider);
        (void)module.query<Api>(abi);
        status.provider = module.provider();
        status.version = module.version();
        status.source = module.source();
        status.external = module.is_external();
        status.available = true;
    } catch(std::exception const& error) {
        status.error = error.what();
    }
    return status;
}
} // namespace

std::vector<ProviderResolution> inspect_engine_providers() {
    std::vector<ProviderResolution> providers;
    providers.reserve(15);
    providers.push_back(inspect<NorthStarGeometryApi>(
        NORTHSTARCPP_CAPABILITY_GEOMETRY, NORTHSTARCPP_GEOMETRY_ABI));
    providers.push_back(inspect<NorthStarCollisionApi>(
        NORTHSTARCPP_CAPABILITY_COLLISION, NORTHSTARCPP_COLLISION_ABI));
    providers.push_back(inspect<NorthStarTimeApi>(
        NORTHSTARCPP_CAPABILITY_TIME, NORTHSTARCPP_TIME_ABI));
    providers.push_back(inspect<NorthStarPerlinApi>(
        NORTHSTARCPP_CAPABILITY_PERLIN, NORTHSTARCPP_PERLIN_ABI));
    providers.push_back(inspect<NorthStarAssetsApi>(
        NORTHSTARCPP_CAPABILITY_ASSETS, NORTHSTARCPP_ASSETS_ABI));
    providers.push_back(inspect<NorthStarLoggerApi>(
        NORTHSTARCPP_CAPABILITY_LOGGER, NORTHSTARCPP_LOGGER_ABI));
    providers.push_back(inspect<NsEcsApi>(
        NORTHSTARCPP_CAPABILITY_ECS, NORTHSTARCPP_ECS_API_VERSION));
    providers.push_back(inspect<NorthStarPhysicsApi>(
        NORTHSTARCPP_CAPABILITY_PHYSICS, NORTHSTARCPP_PHYSICS_ABI));
    providers.push_back(inspect<NorthStarVehicleApi>(
        NORTHSTARCPP_CAPABILITY_VEHICLE, NORTHSTARCPP_VEHICLE_ABI));
    providers.push_back(inspect<NorthStarAudioApi>(
        NORTHSTARCPP_CAPABILITY_AUDIO, NORTHSTARCPP_AUDIO_ABI));
    providers.push_back(inspect<NorthStarInputApi>(
        NORTHSTARCPP_CAPABILITY_INPUT, NORTHSTARCPP_INPUT_ABI));
    providers.push_back(inspect<NorthStarScriptingApi>(
        NORTHSTARCPP_CAPABILITY_SCRIPTING, NORTHSTARCPP_SCRIPTING_ABI));
    providers.push_back(inspect<NorthStarUIApi>(
        NORTHSTARCPP_CAPABILITY_UI, NORTHSTARCPP_UI_ABI));
#ifdef _WIN32
    // Two distinct render-provider requests: the primary D3D12 and D3D11
    // compatibility backend. The active viewport may already own an instance.
    providers.push_back(inspect<NorthStarRenderApi>(
        NORTHSTARCPP_CAPABILITY_RENDER, NORTHSTARCPP_RENDER_ABI, "d3d12"));
    providers.push_back(inspect<NorthStarRenderApi>(
        NORTHSTARCPP_CAPABILITY_RENDER, NORTHSTARCPP_RENDER_ABI, "d3d11"));
#endif
    return providers;
}
} // namespace northstarcpp
