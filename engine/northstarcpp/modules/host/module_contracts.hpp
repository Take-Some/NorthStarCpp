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
#include <northstarcpp/physics_vehicle_plugin.h>
#include <northstarcpp/vehicle_plugin.h>
#include <northstarcpp/vehicle_dynamics_plugin.h>
#include <northstarcpp/vehicle_cabin_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_shader_reload_api.h>
#include <northstarcpp/render_postfx_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_lights_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_frame_memory_api.h>
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_editor_grid_api.h>
#include <northstarcpp/render_pick_api.h>
#include <northstarcpp/render_character_api.h>
#include <northstarcpp/script_service_plugin.h>
#include <northstarcpp/scripting_plugin.h>

namespace northstarcpp {
inline bool valid_api(NorthStarRenderPostFxApi const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_POSTFX_ABI &&
           api.struct_size>=sizeof(NorthStarRenderPostFxApi) &&
           api.set_settings && api.get_settings;
}
inline bool valid_api(NorthStarRenderShaderReloadApiV2 const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_ABI &&
           api.struct_size>=sizeof(NorthStarRenderShaderReloadApiV2) &&
           api.set_material_references && api.queue_shader_binary && api.poll;
}
inline bool valid_api(NorthStarRenderShaderReloadApi const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_SHADER_RELOAD_ABI &&
           api.struct_size>=sizeof(NorthStarRenderShaderReloadApi) &&
           api.set_material_references && api.queue_fragment_spirv && api.poll;
}


inline bool valid_api(NsEcsApi const& api)
{
    return api.abi_version == NORTHSTARCPP_ECS_API_VERSION &&
        api.struct_size >= sizeof(api) && api.provider && api.last_error &&
        api.create_world && api.destroy_world && api.register_component &&
        api.create_entity && api.destroy_entity && api.is_alive && api.count &&
        api.at && api.set && api.get && api.has && api.remove && api.query && api.progress;
}

inline bool valid_api(NorthStarVehicleCabinApi const& api)
{
    return api.abi_version==NORTHSTARCPP_VEHICLE_CABIN_ABI &&
        api.size>=sizeof(NorthStarVehicleCabinApi) &&
        api.configure_doors && api.set_door && api.door_state && api.door_count;
}
inline bool valid_api(NorthStarVehicleDynamicsApi const& api)
{
    return api.abi_version==NORTHSTARCPP_VEHICLE_DYNAMICS_ABI &&
        api.struct_size>=sizeof(NorthStarVehicleDynamicsApi) &&
        api.configure && api.wheel_count && api.wheel_state &&
        api.seat_count && api.seat_state && api.occupy_seat &&
        api.release_seat && api.ground_contact && api.motion_state &&
        api.resolve_collision;
}

inline bool valid_api(NorthStarVehicleApi const& api)
{
    return api.create_world && api.destroy_world && api.attach && api.detach &&
        api.controls && api.waypoint && api.step && api.read && api.at && api.count;
}

inline bool valid_api(NorthStarPhysicsVehicleApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_VEHICLE_ABI &&
        api.size>=sizeof(NorthStarPhysicsVehicleApi) &&
        api.create && api.destroy && api.controls && api.pose &&
        api.wheel && api.wheel_count;
}
inline bool valid_api(NorthStarPhysicsApi const& api)
{
    // Older ABI 4 providers may omit impulse support. The host checks it at use.
    return api.create && api.destroy && api.step && api.read_poses && api.jump &&
        api.move_static && api.sweep_sphere && api.add_static_mesh;
}

inline bool valid_api(NorthStarPhysicsCharacterApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_CHARACTER_ABI &&
        api.possess_body && api.set_sprinting && api.configure_motor;
}
inline bool valid_api(NorthStarPhysicsColliderApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_COLLIDER_ABI &&
        api.configure!=nullptr;
}

inline bool valid_api(NorthStarPhysicsMeshApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_MESH_ABI &&
           api.configure_mesh && api.set_object_type && api.object_type;
}

inline bool valid_api(NorthStarPhysicsSurfaceApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_SURFACE_ABI &&
        api.set_triangle_materials && api.add_static_mesh_materials &&
        api.sweep_sphere_surface;
}
inline bool valid_api(NorthStarPhysicsCookedHullsApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_COOKED_HULLS_ABI &&
           api.configure_hulls;
}
inline bool valid_api(NorthStarPhysicsChannelsApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_CHANNELS_ABI &&
           api.configure && api.query && api.sweep_sphere && api.raycast;
}
inline bool valid_api(NorthStarPhysicsResponseApi const& api)
{
    return api.abi_version==NORTHSTARCPP_PHYSICS_RESPONSE_ABI &&
           api.configure && api.query && api.drain_events;
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

inline bool valid_api(NorthStarRenderPickApi const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_PICK_ABI &&
           api.struct_size>=sizeof(api) && api.read_cube_id;
}
inline bool valid_api(NorthStarRenderPickAsyncApi const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_PICK_ASYNC_ABI &&
           api.struct_size>=sizeof(api) && api.enqueue &&
           api.poll && api.invalidate_before;
}
inline bool valid_api(NorthStarRenderPickApiV2 const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_PICK_V2_ABI &&
           api.struct_size>=sizeof(api) && api.set_world_triangles &&
           api.set_cube_entities && api.read_entity_id;
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

inline bool valid_api(NorthStarRenderUiShadersApi const& api) {
    return api.abi_version==NORTHSTARCPP_RENDER_UI_SHADERS_ABI &&
           api.struct_size>=sizeof(api) && (api.set_hlsl || api.set_spirv);
}

inline bool valid_api(NorthStarRenderUiEffectsApi const& api)
{
    return api.abi_version==NORTHSTARCPP_RENDER_UI_EFFECTS_ABI &&
           api.struct_size>=sizeof(api) && api.set_hover_fx;
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
inline bool valid_api(NorthStarRenderFrameMemoryApi const& api)
{
    return api.abi_version == NORTHSTARCPP_RENDER_FRAME_MEMORY_ABI && api.exchange;
}

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

inline bool valid_api(NorthStarAudioVoicesApi const& api)
{
    return api.abi_version == NORTHSTARCPP_AUDIO_VOICES_ABI &&
        api.struct_size >= sizeof(api) && api.play_voice && api.stop_voice &&
        api.set_voice_gain && api.set_voice_position && api.set_listener &&
        api.update;
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

inline bool valid_api(NorthStarUIRasterLayersApi const& api) {
    return api.size>=sizeof(api) && api.flags==0 && api.draw_filtered_rgba;
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
