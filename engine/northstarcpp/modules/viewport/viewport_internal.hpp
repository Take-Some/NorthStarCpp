#pragma once
#include <northstarcpp/asset_roots.hpp>
// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include "viewport.hpp"
#include <northstarcpp/sky_environment.hpp>
#include "../shared/frame_profiler.hpp"
#include <northstarcpp/collision_wireframe.hpp>
#include "viewport_collision_wireframe.hpp"
#include "viewport_material_batches.hpp"
#include "viewport_frame_scratch.hpp"
#include "viewport_weapon_fx_sprites.hpp"
#include "viewport_mesh_residency.hpp"
#include "viewport_hlod_geometry.hpp"
#include "viewport_model_preloader.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <sstream>
#include "viewport_frame_builder.hpp"
#include "viewport_frame_submission.hpp"
#include "viewport_procedural_geometry.hpp"
#include "viewport_skin_cache.hpp"
#include "viewport_skeletal_animation.hpp"
#include "viewport_npc_procedural.hpp"
#include "viewport_skeletal_basis.hpp"
#include "editor_character_marker.hpp"
#include "editor_grid.hpp"
#include <northstarcpp/scene_world.hpp>

#include <northstarcpp/physics.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/fps_weapon_effects.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/procedural_environment.hpp>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/ui.hpp>
#include <northstarcpp/render_editor_grid_api.h>
#include <northstarcpp/render_pick_api.h>
#include <northstarcpp/render_capture_api.h>
#include <northstarcpp/render_sky_api.h>
#include <northstarcpp/render_feature_requirements.hpp>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_rigid_instances_api.h>
#include <northstarcpp/render_rigid_residency_api.h>
#include <northstarcpp/render_shader_reload_api.h>
#include <northstarcpp/render_postfx_api.h>
#include <northstarcpp/render_lights_api.h>
#include "../render/shared/render_light_components.hpp"
#include <northstarcpp/material.hpp>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_character_api.h>
#include <northstarcpp/character.hpp>
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/geometry_scene.hpp>
#include <northstarcpp/geometry_service.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/animation_graph.hpp>
#include <northstarcpp/procedural_ik.hpp>
#include <northstarcpp/grounded_locomotion.hpp>
#include <northstarcpp/posture_ik.hpp>
#include "module_loader.hpp"

#include <array>
#include <utility>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <optional>
#include <functional>
#include <sstream>
#include <chrono>

namespace northstarcpp {

namespace viewport_detail {
// Browser passes a validated comma-delimited list of strict project
// requirements to the Editor/Viewport child. A directly launched Editor can
// also supply it manually; unknown or duplicated feature identifiers fail.
inline std::vector<std::string> renderer_requirements_from_environment()
{
    std::array<char,4096> buffer{};
    auto const bytes=GetEnvironmentVariableA(
        "NORTHSTARCPP_RENDER_REQUIRED_FEATURES",
        buffer.data(),static_cast<DWORD>(buffer.size()));
    if(bytes>=buffer.size())
        throw std::runtime_error("GPU feature requirements environment is too long");
    std::vector<std::string> requirements;
    if(!bytes)return requirements;
    std::string value(buffer.data(),bytes);
    for(std::size_t start=0;start<value.size();) {
        auto end=value.find(',',start);
        if(end==std::string::npos)end=value.size();
        requirements.push_back(value.substr(start,end-start));
        start=end+1;
    }
    render::validate_feature_requirements(requirements);
    return requirements;
}
inline void require_provider_render_features(
    Module const& provider,const std::vector<std::string>& required)
{
    if(required.empty())return;
    std::uint64_t apis=0;
    if(provider.query_extension<NorthStarRenderTrianglesApi>(
        NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_COLORED_GEOMETRY;
    if(provider.query_extension<NorthStarRenderMaterialsApi>(
        NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_TEXTURED_MATERIALS;
    if(provider.query_extension<NorthStarRenderMaterialsApiV2>(
        NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_MATERIAL_GRAPH;
    if(provider.query_extension<NorthStarRenderSkinnedMaterialsApi>(
        NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_GPU_SKINNING;
    if(provider.query_extension<NorthStarRenderSurfacesApi>(
        NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_WORLD_SURFACES;
    if(provider.query_extension<NorthStarRenderCharacterApi>(
        NORTHSTARCPP_RENDER_CHARACTER_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_CHARACTER;
    if(provider.query_extension<NorthStarRenderLightsApi>(
        NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_DYNAMIC_LIGHTS;
    if(provider.query_extension<NorthStarRenderTimingApi>(
        NORTHSTARCPP_RENDER_TIMING_MODULE_ABI))
        apis|=NS_RENDER_FEATURE_GPU_TIMING;
    auto features=provider.query_extension<NorthStarRenderFeaturesApi>(
        NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI);
    auto missing=render::missing_feature_requirements(
        required,apis,features?features->implemented_features:0,features!=nullptr);
    if(!missing.empty()) {
        std::string message="Render provider '"+provider.provider()+
            "' does not satisfy mandatory project GPU features:";
        for(auto const& id:missing)message+=" "+id;
        throw std::runtime_error(message);
    }
}
struct Character {
    Character(AssetStore const& assets, CharacterAssetSpec const& spec)
        : skeleton(load_skeleton(assets,spec.skeleton)),
          model(load_character_model(assets,spec.model,skeleton)),
          pose(skeleton),
          skeleton_to_mesh(math::invert4x3(model.mesh_to_skeleton))
    {
        if (spec.animations.empty())
            throw std::invalid_argument("A skeletal character requires animation clips");
        for (auto const& resource : spec.animations) {
            auto loaded=load_skeletal_clips(assets,resource.source,
                {resource.clip},skeleton);
            if (loaded.size()!=1 || !clips.emplace(resource.name,
                std::move(loaded.front())).second)
                throw std::invalid_argument("Duplicate or missing skeletal animation");
        }
        for (auto const& part : model.parts)
            vertex_count += part.mesh.vertices.size();
        vertices.resize(vertex_count);
        // The skeleton mapping and palette storage are stable across frames.
        // Reuse these buffers for every pose instead of allocating per tick.
        matrices.reserve(skeleton.joints.size());
    }

    Skeleton skeleton;
    CharacterModel model;
    SkeletonPose pose;
    math::float4x4 skeleton_to_mesh;
    std::vector<math::float4x4> matrices;
    std::unordered_map<std::string,SkeletalClip> clips;
    std::size_t vertex_count = 0;
    std::vector<NorthStarCharacterVertex> vertices;
    // Retained across first-person/third-person geometry mode switches.
    std::vector<CharacterAppearance> appearance;
    std::vector<std::vector<std::uint32_t>> arm_indices;
    std::string active_animation;
    float seconds = 0;
};
} // namespace viewport_detail

using viewport_detail::Character;
using viewport_detail::renderer_requirements_from_environment;
using viewport_detail::require_provider_render_features;

#include "viewport_state.inc"
} // namespace northstarcpp
