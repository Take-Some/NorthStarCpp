// Validation of provider tables before selection, shared by all host clients.
#pragma once
#include <northstarcpp/assets_plugin.h>
#include <northstarcpp/audio_plugin.h>
#include <northstarcpp/ecs_api.h>
#include <northstarcpp/input_plugin.h>
#include <northstarcpp/logger_plugin.h>
#include <northstarcpp/perlin_plugin.h>
#include <northstarcpp/ui_plugin.h>
#include <northstarcpp/geometry_plugin.h>
#include <northstarcpp/collision_plugin.h>
#include <northstarcpp/time_plugin.h>
#include <northstarcpp/physics_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_lights_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_editor_grid_api.h>
#include <northstarcpp/render_character_api.h>
#include <northstarcpp/script_service_plugin.h>
#include <northstarcpp/scripting_plugin.h>

namespace northstarcpp {

inline bool valid_api(NsEcsApi const& api)
{
    return api.abi_version == NORTHSTARCPP_ECS_API_VERSION &&
        api.struct_size >= sizeof(api) && api.provider && api.last_error &&
        api.create_world && api.destroy_world && api.register_component &&
        api.create_entity && api.destroy_entity && api.is_alive && api.count &&
        api.at && api.set && api.get && api.has && api.remove && api.query && api.progress;
}

inline bool valid_api(NorthStarPhysicsApi const& api)
{
    // Older ABI 4 providers may omit impulse support. The host checks it at use.
    return api.create && api.destroy && api.step && api.read_poses && api.jump &&
        api.move_static && api.sweep_sphere && api.add_static_mesh;
}

inline bool valid_api(NorthStarPhysicsRuntimeApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_RUNTIME_ABI &&
           api.add_body && api.remove_body;
}

inline bool valid_api(NorthStarPhysicsResidencyApi const& api)
{
    return api.abi_version == NORTHSTARCPP_PHYSICS_RESIDENCY_ABI &&
        api.set_static_resident && api.is_static_resident;
}

inline bool valid_api(NorthStarRenderEditorGridApi const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_EDITOR_GRID_ABI &&
           api.struct_size>=sizeof(NorthStarRenderEditorGridApi) &&
           api.set_grid && (api.anti_aliasing_modes&1u);
}

inline bool valid_api(NorthStarRenderApi const& api)
{
    return api.create && api.destroy && api.draw && api.set_sky;
}

inline bool valid_api(NorthStarRenderUiApi const& api)
{
    return api.abi_version==NORTHSTARCPP_RENDER_UI_ABI &&
           api.struct_size>=sizeof(api) && api.set_rgba;
}

inline bool valid_api(NorthStarRenderTrianglesApi const& api)
{
    return api.abi_version == NORTHSTARCPP_RENDER_TRIANGLES_ABI && api.set_triangles;
}

inline bool valid_api(NorthStarRenderCharacterApi const& api)
{
    return api.abi_version == NORTHSTARCPP_RENDER_CHARACTER_ABI && api.set_model && api.set_pose;
}

inline bool valid_api(NorthStarRenderMaterialsApi const& api)
{
    return api.abi_version==NORTHSTARCPP_RENDER_MATERIALS_ABI && api.set_batches;
}

inline bool valid_api(NorthStarRenderMaterialsApiV2 const& api)
{
    return api.abi_version==NORTHSTARCPP_RENDER_MATERIALS_V2_ABI &&
           api.set_batches_v2;
}

inline bool valid_api(NorthStarRenderSkinnedMaterialsApi const& api)
{
    return api.abi_version==NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI &&
           api.set_batches;
}


inline bool valid_api(NorthStarRenderFeaturesApi const& api)
{
    return api.abi_version==NORTHSTARCPP_RENDER_FEATURES_ABI &&
        api.struct_size>=sizeof(NorthStarRenderFeaturesApi);
}

// Optional GPU timestamp contract: reject incomplete function tables.
inline bool valid_api(NorthStarRenderTimingApi const& api)
{
    return api.abi_version == NORTHSTARCPP_RENDER_TIMING_ABI && api.read;
}

// Optional dynamic lighting provider introduced by Render Graph integration.
inline bool valid_api(NorthStarRenderLightsApi const& api)
{
    return api.abi_version==NORTHSTARCPP_RENDER_LIGHTS_ABI && api.set_lights;
}

inline bool valid_api(NorthStarRenderSurfacesApi const& api)
{
    return api.abi_version == NORTHSTARCPP_RENDER_SURFACES_ABI &&
        api.set_texture && api.set_cube_surfaces;
}

inline bool valid_api(NorthStarAudioApi const& api)
{
    return api.create && api.destroy && api.play && api.stop;
}

inline bool valid_api(NorthStarInputApi const& api)
{
    return api.create && api.destroy && api.feed_message && api.poll && api.vibrate;
}

inline bool valid_api(NorthStarAssetsApi const& api)
{
    return api.create && api.destroy && api.mount_directory && api.unmount_directory &&
        api.file_size && api.read;
}

inline bool valid_api(NorthStarAssetsArchiveApi const& api)
{
    return api.mount_archive && api.unmount_archive;
}

inline bool valid_api(NorthStarAssetsRangeApi const& api)
{
    return api.read_range != nullptr;
}

inline bool valid_api(NorthStarLoggerApi const& api)
{
    return api.create && api.destroy && api.write && api.flush && api.set_minimum_level;
}

inline bool valid_api(NorthStarGeometryApi const& api)
{
    return api.generate != nullptr;
}

inline bool valid_api(NorthStarCollisionApi const& api)
{
    return api.ray_aabb != nullptr && api.ray_triangles != nullptr;
}

inline bool valid_api(NorthStarTimeApi const& api)
{
    return api.read_counter && api.read_frequency;
}

inline bool valid_api(NorthStarPerlinApi const& api)
{
    return api.create && api.destroy && api.sample1 && api.sample2 && api.sample3 &&
        api.fractal1 && api.fractal2 && api.fractal3 && api.fill2;
}

inline bool valid_api(NorthStarUIApi const& api)
{
    return api.size >= sizeof(api) && api.create_context && api.destroy_context &&
        api.create_element && api.destroy_element && api.set_property &&
        api.get_property_json && api.set_event_callback && api.dispatch_event &&
        api.pump_events && api.resize && api.render;
}

inline bool valid_api(NorthStarUIRasterApi const& api)
{
    return api.size >= sizeof(api) && api.flags == 0 &&
           api.draw_rgba && api.get_rect && api.pointer;
}

inline bool valid_api(NorthStarUIInputApi const& api)
{
    return api.size >= sizeof(api) && api.flags == 0 &&
           api.keyboard && api.composition && api.wheel && api.focused;
}

inline bool valid_api(NorthStarScriptingApi const& api)
{
    return api.create && api.destroy && api.evaluate && api.reset && api.set_host_call;
}

inline bool valid_api(NorthStarScriptServiceApi const& api)
{
    return api.create && api.destroy && api.supports && api.invoke;
}

} // namespace northstarcpp
