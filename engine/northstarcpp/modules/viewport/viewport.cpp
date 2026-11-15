// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include "viewport.hpp"
#include <northstarcpp/scene_world.hpp>

#include <northstarcpp/physics.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/procedural_environment.hpp>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_lights_api.h>
#include "../render/shared/render_light_components.hpp"
#include <northstarcpp/material.hpp>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_character_api.h>
#include <northstarcpp/character.hpp>
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/geometry_scene.hpp>
#include <northstarcpp/geometry_service.hpp>
#include <northstarcpp/assets.hpp>
#include "module_loader.hpp"

#include <array>
#include <utility>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <optional>
#include <chrono>

namespace northstarcpp {

namespace {
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
    std::string active_animation;
    float seconds = 0;
};
}

struct Viewport::State {
    State(HWND window, bool software)
        : window_handle(window), software_renderer(software)
    {
        // A project-selected provider must never silently fall back to
        // another backend: unsupported ABI/features must remain visible.
        std::array<char,128> selected{};
        auto const selection_length=GetEnvironmentVariableA(
            "NORTHSTARCPP_RENDER_PROVIDER",selected.data(),
            static_cast<DWORD>(selected.size()));
        if(selection_length>=selected.size())
            throw std::runtime_error("Selected renderer provider ID is too long");
        std::vector<std::string> providers;
        if(selection_length)providers.emplace_back(selected.data());
        else providers={"d3d12","d3d11"}; // Existing automatic fallback.
        for(std::size_t i=0;i<providers.size();++i) {
            try {
                auto candidate=std::make_unique<Module>(
                    NORTHSTARCPP_CAPABILITY_RENDER,providers[i]);
                auto const* candidate_api=
                    candidate->query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
                void* instance=candidate_api->create(window, software?1u:0u);
                if(!instance)throw candidate->error("Cannot create viewport");
                module=std::move(candidate);
                api=candidate_api;
                renderer=instance;
                break;
            }catch(std::exception const&) {
                if(i+1==providers.size())throw;
            }
        }
        triangle_api = module->query_extension<NorthStarRenderTrianglesApi>(
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI);
        if (!triangle_api) {
            auto const query = module->symbol<NorthStarRenderGetTrianglesApi>(
                "northstar_render_get_triangles_api");
            auto const* candidate = query ? query(NORTHSTARCPP_RENDER_TRIANGLES_ABI) : nullptr;
            if (candidate && valid_api(*candidate))
                triangle_api = candidate;
        }
        surface_api=module->query_extension<NorthStarRenderSurfacesApi>(
            NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI);
        material_api=module->query_extension<NorthStarRenderMaterialsApi>(
            NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI);
        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        skinned_material_api=module->query_extension<NorthStarRenderSkinnedMaterialsApi>(
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI);
        lights_api=module->query_extension<NorthStarRenderLightsApi>(
            NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI);
        try {
            cubes.reserve(max_scene_objects);
            surface_ids.reserve(max_scene_objects);
        } catch (...) {
            api->destroy(renderer);
            throw;
        }
    }

    ~State() { if (renderer) api->destroy(renderer); }

    void fallback_to_d3d11() {
        if (module && module->provider()!="d3d12") return;
        std::array<char,8> selected{};
        if(GetEnvironmentVariableA("NORTHSTARCPP_RENDER_PROVIDER",
             selected.data(),static_cast<DWORD>(selected.size()))>0)
            throw std::runtime_error(
                "Selected renderer lacks character support; explicit provider fallback disabled");
        auto next_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_RENDER,"d3d11");
        auto next_api=next_module->query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
        // DXGI only permits one active flip-model swapchain per HWND.
        // Release the D3D12 swapchain before creating the replacement.
        if(renderer)api->destroy(renderer);
        renderer=nullptr;
        auto next_renderer=next_api->create(window_handle,software_renderer?1u:0u);
        if(!next_renderer)throw next_module->error("Cannot initialize D3D11 fallback");
        renderer=next_renderer;api=next_api;module=std::move(next_module);
        triangle_api=module->query_extension<NorthStarRenderTrianglesApi>(
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI);
        if(!triangle_api) {
            auto query=module->symbol<NorthStarRenderGetTrianglesApi>(
                "northstar_render_get_triangles_api");
            auto candidate=query?query(NORTHSTARCPP_RENDER_TRIANGLES_ABI):nullptr;
            if(candidate&&valid_api(*candidate))triangle_api=candidate;
        }
        surface_api=module->query_extension<NorthStarRenderSurfacesApi>(
            NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI);
        material_api=module->query_extension<NorthStarRenderMaterialsApi>(
            NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI);
        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        skinned_material_api=module->query_extension<NorthStarRenderSkinnedMaterialsApi>(
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI);
        lights_api=module->query_extension<NorthStarRenderLightsApi>(
            NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI);
    }
    HWND window_handle=nullptr;
    bool software_renderer=false;
    std::unique_ptr<Module> module;
    NorthStarRenderApi const* api = nullptr;
    std::vector<NorthStarRenderCube> cubes;
    std::vector<std::uint32_t> surface_ids;
    NorthStarRenderSurfacesApi const* surface_api = nullptr;
    NorthStarRenderMaterialsApi const* material_api = nullptr;
    NorthStarRenderMaterialsApiV2 const* material_api_v2=nullptr;
    NorthStarRenderSkinnedMaterialsApi const* skinned_material_api=nullptr;
    NorthStarRenderLightsApi const* lights_api=nullptr;
    bool warned_missing_light_provider=false;
    std::unordered_map<std::string,MaterialDefinition> cached_materials;
    // Failed resources are quarantined for this VFS mount, not retried in
    // every frame. Changing mounted asset sources clears the quarantine.
    std::unordered_set<std::string> invalid_materials;
    std::unordered_set<std::string> invalid_meshes;
    void warn_asset(char const* type,std::string const& path,
                    std::exception const& error) const {
        std::fprintf(stderr,"[WARN] [engine.assets] Skipping invalid %s '%s': %s\n",
            type,path.c_str(),error.what());
    }
    std::optional<shared_assets::DrawableResources> retained_sky;
    bool world_surfaces_ready = false;
    NorthStarRenderCharacterApi const* character_api = nullptr;
    std::unique_ptr<Character> character;
    std::unique_ptr<AssetStore> project_assets;
    std::filesystem::path source_project_root;
    std::unordered_map<std::string,MeshAsset> cached_meshes;
    // Reuse original YDD eight-weight influences, YMT bind skeleton and
    // decoded YCD clips. GPU providers receive a CPU-deformed PBR mesh.
    struct NativeRig {
        Skeleton skeleton;
        std::vector<SkinWeights> weights;
        std::unordered_map<std::string,SkeletalClip> clips;
        std::unordered_set<std::string> invalid_clips;
    };
    std::unordered_map<std::string,NativeRig> cached_native_skins;
    struct NativePlayback {
        std::string clip_reference;
        float seconds=0.f;
    };
    std::unordered_map<std::uint64_t,NativePlayback> native_playback;
    std::chrono::steady_clock::time_point last_native_frame{};
    std::unordered_map<std::string,GeometryData> cached_geometry;
    std::unique_ptr<GeometryService> geometry_service;
    std::vector<NorthStarRenderTriangleVertex> mesh_vertices;
    NorthStarRenderTrianglesApi const* triangle_api = nullptr;
    void* renderer = nullptr;
};

Viewport::Viewport(HWND window, bool software) : state(std::make_unique<State>(window, software)) {}
Viewport::~Viewport() = default;

std::string Viewport::renderer_provider_name() const
{
    return state->module->provider();
}

std::string Viewport::renderer_provider_version() const
{
    return state->module->version();
}

void Viewport::load_character(AssetStore const& assets, CharacterAssetSpec const& spec)
{
    auto& s = *state;
    s.character_api = s.module->query_extension<NorthStarRenderCharacterApi>(
        NORTHSTARCPP_RENDER_CHARACTER_MODULE_ABI);
    if (!s.character_api && s.module->provider() == "d3d12") {
        s.fallback_to_d3d11();
        s.character_api = s.module->query_extension<NorthStarRenderCharacterApi>(
            NORTHSTARCPP_RENDER_CHARACTER_MODULE_ABI);
    }
    if (!s.character_api)
        throw std::runtime_error("Selected engine.render provider lacks the character ABI");
    auto actor = std::make_unique<Character>(assets, spec);
    auto appearance = load_character_appearance(assets,actor->model,
        spec.material,spec.textures);
    std::vector<std::vector<NorthStarSurfaceMip>> mips(appearance.size());
    std::vector<std::vector<NorthStarCharacterVertex>> initial(appearance.size());
    std::vector<NorthStarCharacterPart> parts;
    for (std::size_t i = 0; i < appearance.size(); ++i) {
        auto const& mesh = actor->model.parts[i].mesh;
        auto const& image = appearance[i].texture;
        for (auto const& mip : image.mips)
            mips[i].push_back({mip.width,mip.height,static_cast<std::uint32_t>(mip.offset),
                static_cast<std::uint32_t>(mip.size)});
        for (auto const& v : mesh.vertices)
            initial[i].push_back({{v.position[0],v.position[1],v.position[2]},
                {v.normal[0],v.normal[1],v.normal[2]},{v.uv[0],v.uv[1]}});
        parts.push_back({initial[i].data(),static_cast<std::uint32_t>(mesh.vertices.size()),
            mesh.indices.data(),static_cast<std::uint32_t>(mesh.indices.size()),
            {0,image.format_id,image.srgb ? 1u : 0u,static_cast<std::uint32_t>(mips[i].size()),
            mips[i].data(),image.data.data(),static_cast<std::uint32_t>(image.data.size())},
            appearance[i].cutoff});
    }
    if (!s.character_api->set_model(s.renderer,parts.data(),static_cast<std::uint32_t>(parts.size())))
        throw s.module->error("Cannot upload textured character");
    s.character = std::move(actor);
}

void Viewport::pose_character(Vector3 foot, float heading, float dt,
                              std::string const& animation)
{
    auto& s = *state;
    if (!s.character) return;
    auto& actor = *s.character;
    auto const it=actor.clips.find(animation);
    if(it==actor.clips.end())
        throw std::invalid_argument("Skeletal animation not declared by the project: "+animation);
    if(actor.active_animation!=animation) {
        actor.active_animation=animation;
        actor.seconds=0.f;
    }
    actor.seconds+=std::clamp(dt,0.f,0.1f);
    actor.pose.sample(actor.skeleton,it->second,actor.seconds,true);
    using namespace math;
    auto const to_skeleton = actor.model.mesh_to_skeleton;
    // Imported mesh forward is +Z. This rotation maps a project-specified
    // heading (yaw 0 => +Z) into the row-vector world transform.
    auto const to_world = actor.skeleton_to_mesh * create_rotation_y(-heading) *
        create_translation({foot.x,foot.y,foot.z});
    auto& matrices = actor.matrices;
    matrices.clear();
    for (auto const& joint : actor.pose.palette())
        matrices.push_back(to_skeleton * joint * to_world);
    std::size_t dest = 0;
    for (auto const& part : actor.model.parts) {
        for (std::size_t i = 0; i < part.skin.size(); ++i) {
            auto const& v = part.mesh.vertices[i];
            auto const& skin = part.skin[i];
            float3 position{0,0,0}, normal{0,0,0};
            for (unsigned j = 0; j < 8; ++j) {
                auto const weight = skin.weights[j];
                if (weight == 0.f) continue;
                auto const& matrix = matrices[skin.joints[j]];
                position += matrix.transform_position({v.position[0],v.position[1],v.position[2]}) * weight;
                normal += matrix.transform_direction({v.normal[0],v.normal[1],v.normal[2]}) * weight;
            }
            actor.vertices[dest++] = {{position.x,position.y,position.z},
                {normal.x,normal.y,normal.z},{v.uv[0],v.uv[1]}};
        }
    }
    if (!s.character_api->set_pose(s.renderer,actor.vertices.data(),
            static_cast<std::uint32_t>(actor.vertices.size())))
        throw s.module->error("Cannot upload character pose");
}

void Viewport::hide_character()
{
    auto& s = *state;
    if (s.character && !s.character_api->set_pose(s.renderer,nullptr,0))
        throw s.module->error("Cannot hide character mesh");
}

void Viewport::clear_character()
{
    hide_character();
    state->character.reset();
}

void Viewport::set_asset_sources(std::filesystem::path const& root,
                                 std::filesystem::path const& archive,
                                 std::filesystem::path const& archive_data,
                                 std::filesystem::path const& shared_root)
{
    namespace fs = std::filesystem;
    auto common=shared_root;
    if (common.empty()) {
        auto search_ancestors=[](fs::path from)->fs::path {
            for (auto dir=std::move(from); !dir.empty();) {
                auto const candidate=dir/"sharedAssets";
                if (fs::is_directory(candidate)) return candidate;
                auto const parent=dir.parent_path();
                if (parent==dir) break;
                dir=parent;
            }
            return {};
        };
        if (!root.empty()) common=search_ancestors(fs::absolute(root));
        if (common.empty()) {
            std::array<wchar_t,32768> executable{};
            auto length=GetModuleFileNameW(nullptr,executable.data(),
                static_cast<DWORD>(executable.size()));
            if (length && length<executable.size())
                common=search_ancestors(fs::path(executable.data()).parent_path());
        }
    }
    auto assets=std::make_unique<AssetStore>();
    if (!archive.empty()) {
        if (!common.empty()) assets->mount_shared_assets(common);
        assets->mount_archive("",archive,archive_data.empty()?archive:archive_data);
    } else if (!root.empty()) {
        assets->mount_project_assets(root,common);
    }
    state->project_assets=std::move(assets);
    state->source_project_root=archive.empty() ? std::filesystem::absolute(root) :
        std::filesystem::path{};
    state->cached_meshes.clear();
    state->cached_native_skins.clear();
    state->native_playback.clear();
    state->last_native_frame={};
    state->cached_materials.clear();
    state->invalid_meshes.clear();
    state->invalid_materials.clear();
    state->mesh_vertices.clear();
    state->world_surfaces_ready=false;
    // Optional authored terrain dictionary normalized from original YTD.
    // Original malformed bytes remain unchanged.
    constexpr char dictionary[]="assets/textures/world_surfaces_runtime.ytd";
    bool available=false;
    try {available=state->project_assets->size(dictionary)>0;}
    catch(std::exception const&) {available=false;}
    if(available) {
        // Asset decoding is optional; renderer/provider initialization is
        // not. Validate all source mips BEFORE submitting any GPU textures,
        // so malformed YTD cannot leave half of a surface atlas installed.
        static constexpr char const* names[9]{
            "ground_sand","ground_sand_normal","ground_sand_roughness",
            "rock_moss","rock_moss_normal","rock_moss_roughness",
            "terrain_forest_floor","terrain_forest_floor_normal",
            "terrain_forest_floor_roughness"};
        std::optional<std::vector<shared_assets::Texture>> decoded;
        try {
            auto textures=shared_assets::load_textures(*state->project_assets,dictionary);
            if(textures.size()!=9)
                throw std::runtime_error("Expected 9 original world surface maps");
            for(std::uint32_t i=0;i<9;++i) {
                auto it=std::find_if(textures.begin(),textures.end(),
                    [&](auto const& texture){return texture.name==names[i];});
                if(it==textures.end() || it->mips.empty() ||
                    it->format_id!=(i%3==0?102:105) ||
                    it->srgb!=(i%3==0))
                    throw std::runtime_error("World surface texture format/name mismatch");
                if(it->data.size()>UINT32_MAX)
                    throw std::runtime_error("World surface texture exceeds GPU ABI");
                for(auto const& mip:it->mips) {
                    if(mip.offset>UINT32_MAX || mip.size>UINT32_MAX ||
                       mip.offset>it->data.size() ||
                       mip.size>it->data.size()-mip.offset)
                        throw std::runtime_error("Invalid world surface texture mip");
                }
            }
            decoded=std::move(textures);
        } catch(std::exception const& error) {
            state->warn_asset("world texture dictionary",dictionary,error);
        }
        if(decoded) {
            // Keep renderer ABI/provider errors visible as runtime faults.
            // Only source decode/validation above is recoverable.
            if(!state->surface_api && state->module->provider()=="d3d12")
                state->fallback_to_d3d11();
            if(!state->surface_api)
                throw std::runtime_error(
                    "Selected engine.render provider lacks world-surface extension");
            for(std::uint32_t i=0;i<9;++i) {
                auto const& textures=*decoded;
                auto it=std::find_if(textures.begin(),textures.end(),
                    [&](auto const& texture){return texture.name==names[i];});
                std::vector<NorthStarSurfaceMip> mips;
                mips.reserve(it->mips.size());
                for(auto const& mip:it->mips)
                    mips.push_back({mip.width,mip.height,
                        static_cast<std::uint32_t>(mip.offset),
                        static_cast<std::uint32_t>(mip.size)});
                NorthStarSurfaceTexture texture{
                    i,it->format_id,it->srgb?1u:0u,
                    static_cast<std::uint32_t>(mips.size()),
                    mips.data(),it->data.data(),
                    static_cast<std::uint32_t>(it->data.size())};
                if(!state->surface_api->set_texture(state->renderer,&texture))
                    throw state->module->error("GPU world texture upload failed");
            }
            state->world_surfaces_ready=true;
        }
    }
}


void Viewport::set_sky(shared_assets::DrawableResources const& resources)
{
    // Preserve authored sky resources across an optional render-provider
    // fallback when material-capable geometry first enters the scene.
    state->retained_sky=resources;
    if (resources.drawable.meshes.size() != 1 || resources.materials.empty())
        throw std::runtime_error("Skydome requires one authored mesh and its YMAT material");
    auto const& mesh = resources.drawable.meshes.front();
    auto const& material = resources.materials.front().asset;
    auto const binding = std::find_if(material.textures.begin(), material.textures.end(),
        [](shared_assets::TextureBinding const& tex) {return tex.slot == "base_color";});
    if (binding == material.textures.end())
        throw std::runtime_error("Skydome material has no base_color texture");
    auto const found = std::find_if(resources.textures.begin(), resources.textures.end(),
        [&](auto const& item) {return item.reference == binding->reference;});
    if (found == resources.textures.end())
        throw std::runtime_error("Skydome base_color texture is unresolved");
    auto const& texture = found->asset;
    if (texture.format_id != 1 || texture.mips.empty() ||
        texture.mips.front().size != static_cast<std::size_t>(texture.mips.front().width) *
            texture.mips.front().height * 4)
        throw std::runtime_error("Skydome requires native RGBA8 base_color mip");
    auto const& mip = texture.mips.front();
    if (mip.offset > texture.data.size() ||
        mip.size > texture.data.size() - mip.offset)
        throw std::runtime_error("Invalid skydome texture mip data");
    std::vector<NorthStarSkyVertex> verts;
    verts.reserve(mesh.vertices.size());
    for (auto const& value : mesh.vertices)
        verts.push_back({value.position[0], value.position[1], value.position[2],
            value.uv[0], value.uv[1]});
    float tint[3]{1.f,1.f,1.f};
    for (auto const& param : material.parameters)
        if (param.name == "base_color" && param.values.size() >= 3)
            for (std::size_t axis = 0; axis < 3; ++axis) tint[axis] = param.values[axis];
    // Only the specific Perlin noise texture is procedural. Other skydome
    // materials continue using their decoded native YTD base-color data.
    std::vector<std::uint8_t> procedural_clouds;
    auto const* pixels=texture.data.data() + mip.offset;
    auto width=mip.width, height=mip.height;
    if (binding->reference == "textures/skydome.ytd@baseperlinnoise3channel") {
        CloudNoiseSettings settings;
        settings.seed=2026;
        settings.width=width;
        settings.height=height;
        procedural_clouds=make_perlin_cloud_rgba(settings);
        pixels=procedural_clouds.data();
    }
    NorthStarSky const sky{verts.data(), static_cast<std::uint32_t>(verts.size()),
        mesh.indices.data(), static_cast<std::uint32_t>(mesh.indices.size()),
        pixels, width, height,
        tint[0],tint[1],tint[2]};
    if (!state->api->set_sky(state->renderer, &sky)) {
        if(state->module->provider()!="d3d12")
            throw state->module->error("Cannot initialize texture-mapped skydome");
        state->fallback_to_d3d11();
        if(!state->api->set_sky(state->renderer,&sky))
            throw state->module->error("Cannot initialize D3D11 skydome fallback");
    }
}


// Optional per-entity authoring component uses one of the native YTD
// albedo entry names. Unspecified map proxies derive their surface from
// existing scene layer/actor names without changing collision or transforms.
static std::uint32_t world_surface_category(std::string name) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if(name=="rock_moss" || name.find("rock")!=std::string::npos ||
       name.find("cliff")!=std::string::npos ||
       name.find("perimeter")!=std::string::npos)return 2;
    if(name=="terrain_forest_floor" ||
       name.find("forest")!=std::string::npos ||
       name.find("grass")!=std::string::npos)return 3;
    if(name=="ground_sand" || name.find("sand")!=std::string::npos ||
       name.find("terrain")!=std::string::npos ||
       name.find("ground")!=std::string::npos ||
       name.find("deck")!=std::string::npos ||
       name.find("platform")!=std::string::npos)return 1;
    return 0;
}

void Viewport::draw(Scene const& scene, int selected, bool verify_pixels, PhysicsScene const* physics,
                    FpsArena const* fps, bool paused, CameraPose const* camera_pose,
                    SceneWorld const* scene_world, bool show_fps_overlay,
                    bool show_streaming_debug, ViewportGizmo const* gizmo)
{
    // One animation clock per Viewport; paused scenes freeze their pose.
    auto const now=std::chrono::steady_clock::now();
    float delta=0.f;
    if(state->last_native_frame!=std::chrono::steady_clock::time_point{})
        delta=std::clamp(std::chrono::duration<float>(
            now-state->last_native_frame).count(),0.f,0.1f);
    state->last_native_frame=now;
    if(paused)delta=0.f;
    std::unordered_map<std::uint64_t,std::string> animation_bindings;
    std::unordered_set<std::uint64_t> active_entities;
    active_entities.reserve(scene.objects.size());
    for(auto const& entity:scene.objects)
        if(entity.node.id)active_entities.insert(entity.node.id);
    for(auto it=state->native_playback.begin();
        it!=state->native_playback.end();) {
        if(!active_entities.count(it->first))
            it=state->native_playback.erase(it);
        else ++it;
    }
    for(auto const& component:scene.components) {
        if(component.entity_id && component.version==1 &&
           component.type=="engine.model.animation" &&
           component.payload.size()<=512 && !component.payload.empty())
            animation_bindings[component.entity_id]=component.payload;
    }
    for(auto it=state->native_playback.begin();
        it!=state->native_playback.end();) {
        if(!animation_bindings.count(it->first))
            it=state->native_playback.erase(it);
        else ++it;
    }
    auto& cubes = state->cubes;
    cubes.clear();
    auto& surface_ids = state->surface_ids;
    surface_ids.clear();
    std::unordered_map<std::uint64_t,std::uint32_t> surface_overrides;
    if(state->world_surfaces_ready) {
        for(auto const& component:scene.components) {
            if(component.type=="engine.world.surface" &&
               component.version==1 && component.entity_id!=0 &&
               component.payload.size()<=48) {
                std::string const name(component.payload.begin(),
                                       component.payload.end());
                // Plugin authoring uses exact dictionary entry names.
                // Keep heuristic name matching only for legacy scene actors.
                auto const id=name=="ground_sand"?1u:
                    name=="rock_moss"?2u:
                    name=="terrain_forest_floor"?3u:0u;
                if(id)surface_overrides[component.entity_id]=id;
            }
        }
    }
    auto& mesh_vertices=state->mesh_vertices;
    mesh_vertices.clear();
    // Authored object materials remain a separate triangle stream with
    // preserved normals/UVs. The renderer may be replaced by a plugin
    // exporting the same optional versioned materials ABI.
    std::unordered_map<std::uint64_t,std::string> assigned_materials;
    for(auto const& component:scene.components) {
        if(component.type!="northstarcpp.editor.material" ||
           component.version!=1 || !component.entity_id || component.payload.empty())
            continue;
        if(component.payload.size()>1024)
            throw std::runtime_error("Oversized scene material reference");
        assigned_materials[component.entity_id]=component.payload;
    }
    // Native drawable sections carry their own YMAT references; they must
    // be rendered through UV/PBR batches even without an authored override.
    bool has_native_materials=false;
    if(state->project_assets) for(auto const& object:scene.objects) {
        auto path=object.node.asset.substr(0,object.node.asset.find('@'));
        if(path.size()>=4 &&
           (path.compare(path.size()-4,4,".ydd")==0 ||
            path.compare(path.size()-4,4,".ydr")==0)) {
            has_native_materials=true;
            break;
        }
    }
    if((!assigned_materials.empty() || has_native_materials) &&
       !state->material_api && !state->material_api_v2) {
        if(state->module->provider()=="d3d12") {
            // D3D12 retains its native skydome/HUD path for scenes without
            // authored PBR assets. Until its optional material extension
            // is present, use D3D11 rather than drawing UV-less proxies.
            state->fallback_to_d3d11();
            if(state->retained_sky) {
                auto original_sky=*state->retained_sky;
                set_sky(original_sky);
            }
        }
        if(!state->material_api && !state->material_api_v2)
            throw std::runtime_error(
                "Active engine.render provider lacks native UV/PBR materials ABI");
    }
    std::vector<std::pair<std::string,std::vector<NorthStarMaterialVertex>>> material_draws;
    struct PendingNativeSkinDraw {
        std::string material_reference;
        std::vector<NorthStarSkinMaterialVertex> vertices;
        std::vector<float> palette; // composed world-space row-major bones
    };
    std::vector<PendingNativeSkinDraw> native_skin_draws;
    std::unordered_map<std::string,std::size_t> material_indices;
    auto material_vertices=[&](std::string const& ref)
        ->std::vector<NorthStarMaterialVertex>& {
        auto [it,created]=material_indices.emplace(ref,material_draws.size());
        if(created) {
            if(material_draws.size()>=NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES)
                throw std::runtime_error("Too many distinct material batches");
            material_draws.emplace_back(ref,std::vector<NorthStarMaterialVertex>{});
        }
        return material_draws[it->second].second;
    };
    auto transformed=[&](Vector3 v,NorthStarRenderCube const& cube,
                         Vector3 const& origin) {
        return Vector3{
            origin.x+cube.rotation[0]*v.x+cube.rotation[1]*v.y+cube.rotation[2]*v.z,
            origin.y+cube.rotation[3]*v.x+cube.rotation[4]*v.y+cube.rotation[5]*v.z,
            origin.z+cube.rotation[6]*v.x+cube.rotation[7]*v.y+cube.rotation[8]*v.z};
    };
    auto normal_direction=[&](Vector3 v,NorthStarRenderCube const& c) {
        Vector3 n{c.rotation[0]*v.x+c.rotation[1]*v.y+c.rotation[2]*v.z,
                  c.rotation[3]*v.x+c.rotation[4]*v.y+c.rotation[5]*v.z,
                  c.rotation[6]*v.x+c.rotation[7]*v.y+c.rotation[8]*v.z};
        auto const len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        return len>1.e-6f?Vector3{n.x/len,n.y/len,n.z/len}:
              Vector3{0.f,1.f,0.f};
    };
    auto push_material=[&](std::vector<NorthStarMaterialVertex>& dst,
                           Vector3 position,Vector3 normal,float u,float v) {
        if(dst.size()>=NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)
            throw std::runtime_error("Material mesh vertex limit exceeded");
        dst.push_back({position.x,position.y,position.z,
                       normal.x,normal.y,normal.z,u,v});
    };
    auto emit_procedural=[&](std::vector<NorthStarMaterialVertex>& dst,
                             GeometryData const& data,SceneObject const& object,
                             NorthStarRenderCube const& cube) {
        if(data.indices.size()%3)
            throw std::invalid_argument("Invalid procedural triangle topology");
        for(std::size_t k=0;k<data.indices.size();k+=3) {
            Vector3 local[3],world[3];
            for(unsigned v=0;v<3;++v) {
                auto const off=std::size_t(data.indices[k+v])*3;
                if(off+2>=data.xyz.size())
                    throw std::invalid_argument("Invalid procedural mesh index");
                local[v]={data.xyz[off],data.xyz[off+1],data.xyz[off+2]};
                world[v]=transformed({local[v].x*object.size,
                    local[v].y*object.size,local[v].z*object.size},cube,
                    object.position);
            }
            auto const u=Vector3{world[1].x-world[0].x,
                world[1].y-world[0].y,world[1].z-world[0].z};
            auto const v=Vector3{world[2].x-world[0].x,
                world[2].y-world[0].y,world[2].z-world[0].z};
            auto const cross=Vector3{u.y*v.z-u.z*v.y,
                u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
            auto const len=std::sqrt(cross.x*cross.x+cross.y*cross.y+cross.z*cross.z);
            auto const n=len>1.e-6f?Vector3{cross.x/len,cross.y/len,cross.z/len}:
                Vector3{0.f,1.f,0.f};
            for(unsigned idx=0;idx<3;++idx) {
                // Procedural geometry has positions only. Construct stable
                // planar face UVs; authored OBJ UVs below are never replaced.
                auto const& p=local[idx];
                float tu,tv;
                if(std::abs(n.y)>=std::max(std::abs(n.x),std::abs(n.z)))
                    {tu=p.x;tv=p.z;}
                else if(std::abs(n.x)>std::abs(n.z))
                    {tu=p.z;tv=p.y;}
                else {tu=p.x;tv=p.y;}
                push_material(dst,world[idx],n,tu,tv);
            }
        }
    };

    // Components are serialized by stable authored ID. This lookup is
    // shared by both the Qt GPU Editor and Player frame composition.
    std::unordered_map<std::uint64_t,SceneComponentBlob const*> procedural;
    for(auto const& component:scene.components)
        if(component.type==geometry_primitive_component && component.entity_id)
            procedural.emplace(component.entity_id,&component);
    auto focus = Vector3(0.f, 0.f, 0.f);
    int selected_render_index = -1;
    bool const streaming = scene_world && scene_world->update_generation() > 0 &&
        scene_world->source().objects.size() <= scene.objects.size();
    auto const authored_count = physics ? physics->authored_object_count() :
        (scene_world ? scene_world->source().objects.size() : scene.objects.size());
    if (streaming) {
        // Per-scene CPU mesh storage must obey verified cell residency.
        // The D3D11 provider uploads the currently submitted triangle list
        // each frame; it does not own persistent per-entity OBJ GPU handles.
        // Drop cached OBJ meshes not used by the current visible frame, while
        // retaining the selected item in editor authoring mode.
        std::unordered_set<std::string> active_meshes;
        auto include_mesh=[&](std::size_t i) {
            auto const& path=scene.objects[i].node.asset;
            auto const selector=path.find('@');
            auto const extension=path.substr(0,selector);
            if(extension.size()>=4 &&
                (extension.compare(extension.size()-4,4,".obj")==0 ||
                 extension.compare(extension.size()-4,4,".ydd")==0 ||
                 extension.compare(extension.size()-4,4,".ydr")==0))
                active_meshes.insert(path);
        };
        for(auto const i:scene_world->visible_indices())include_mesh(i);
        // Spawned objects are persistent runtime entities, not authored cells.
        for(std::size_t i=authored_count;i<scene.objects.size();++i)
            if(scene.objects[i].node.visible) include_mesh(i);
        if(selected>=0 && static_cast<std::size_t>(selected)<scene.objects.size())
            include_mesh(static_cast<std::size_t>(selected));
        for(auto it=state->cached_meshes.begin();it!=state->cached_meshes.end();) {
            if(!active_meshes.count(it->first)) {
                state->cached_native_skins.erase(it->first);
                it=state->cached_meshes.erase(it);
            } else ++it;
        }
    }
    for (std::size_t i = 0; i < scene.objects.size(); ++i) {
        // Keep the selected editor entity editable even when HLOD substitutes
        // its authored draw; the proxy is still allowed behind selection.
        if (streaming && i<authored_count && !scene_world->object_visible(i) &&
            (selected<0 || i!=static_cast<std::size_t>(selected)))
            continue;
        auto const& object = scene.objects[i];
        if (i>=authored_count && !object.node.visible) continue;
        if ((fps || (camera_pose && selected==-2)) && object.player) continue;
        auto const& position = object.position;
        NorthStarRenderCube cube{position.x, position.y, position.z, object.size, object.color,
            {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}};
        if (physics) {
            auto const rotation = physics->orientation(i);
            for (unsigned axis = 0; axis < 3; ++axis)
                for (unsigned component = 0; component < 3; ++component)
                    cube.rotation[axis * 3 + component] = rotation.elements[axis][component];
        }
        if (scene.version == 3) {
            // Static authored transforms use Euler XYZ. Dynamic bodies retain
            // their provider-authoritative physics orientation.
            if (!physics || object.mass == 0.f) {
                constexpr float radians = 0.01745329251994329577f;
                float const x = object.node.rotation.x * radians;
                float const y = object.node.rotation.y * radians;
                float const z = object.node.rotation.z * radians;
                float const sx = std::sin(x), cx = std::cos(x);
                float const sy = std::sin(y), cy = std::cos(y);
                float const sz = std::sin(z), cz = std::cos(z);
                float const basis[9] = {
                    cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
                    sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
                    -sy, cy * sx, cy * cx
                };
                std::copy(std::begin(basis), std::end(basis),
                          std::begin(cube.rotation));
            }
            float const scale[3] = {object.node.scale.x, object.node.scale.y,
                                    object.node.scale.z};
            for (unsigned row = 0; row != 3; ++row)
                for (unsigned column = 0; column != 3; ++column)
                    cube.rotation[row * 3 + column] *= scale[column];
        }
        auto const material_it=assigned_materials.find(object.node.id);
        bool const has_material=material_it!=assigned_materials.end();
        // First-party foundation geometry, authored from the active Qt
        // Editor palette. The GPU triangle stream is shared with game runtime
        // and remains behind WorldPartition's verified visibility check.
        auto const native_shape=procedural.find(object.node.id);
        if(state->triangle_api && native_shape!=procedural.end() &&
           object.node.asset.empty()) {
            auto const& component=*native_shape->second;
            auto found=state->cached_geometry.find(component.payload);
            if(found==state->cached_geometry.end()) {
                NorthStarGeometryRequest request{};
                if(!decode_geometry_primitive_component(component,request))
                    throw std::runtime_error("Unsupported engine.geometry component");
                if(!state->geometry_service)
                    state->geometry_service=std::make_unique<GeometryService>();
                if(state->cached_geometry.size()>=128)
                    state->cached_geometry.clear();
                auto geometry=state->geometry_service->generate(request);
                found=state->cached_geometry.emplace(component.payload,
                                                       std::move(geometry)).first;
            }
            auto const& geometry=found->second;
            if(has_material) {
                auto& batch=material_vertices(material_it->second);
                emit_procedural(batch,geometry,object,cube);
                if(static_cast<int>(i)==selected || (selected==-2 && object.player))
                    focus=position;
                continue;
            }
            if(geometry.indices.size()%3!=0 ||
               mesh_vertices.size()+geometry.indices.size()>300000)
                throw std::runtime_error("Scene procedural geometry triangle budget exceeded");
            std::uint32_t const color=static_cast<int>(i)==selected ?
                0xffd474u : object.color;
            for(auto const vertex:geometry.indices) {
                auto const offset=static_cast<std::size_t>(vertex)*3u;
                float const x=geometry.xyz[offset]*object.size;
                float const y=geometry.xyz[offset+1u]*object.size;
                float const z=geometry.xyz[offset+2u]*object.size;
                mesh_vertices.push_back({
                    object.position.x+cube.rotation[0]*x+cube.rotation[1]*y+
                        cube.rotation[2]*z,
                    object.position.y+cube.rotation[3]*x+cube.rotation[4]*y+
                        cube.rotation[5]*z,
                    object.position.z+cube.rotation[6]*x+cube.rotation[7]*y+
                        cube.rotation[8]*z,
                    color});
            }
            if(static_cast<int>(i)==selected || (selected==-2 && object.player))
                focus=position;
            continue;
        }
        // The project-native YDD/YDR and optional OBJ formats share the same
        // provider-neutral VFS geometry path and renderer triangle ABI.
        // A render provider without the extension retains cube fallback.
        auto const& resource_path=object.node.asset;
        auto const selector=resource_path.find('@');
        auto const extpath=resource_path.substr(0,selector);
        bool const is_scene_mesh=extpath.size()>4 &&
            (extpath.compare(extpath.size()-4,4,".obj")==0 ||
             extpath.compare(extpath.size()-4,4,".ydd")==0 ||
             extpath.compare(extpath.size()-4,4,".ydr")==0);
        if (state->triangle_api && state->project_assets && is_scene_mesh) {
            if(state->invalid_meshes.count(object.node.asset))continue;
            auto found=state->cached_meshes.find(object.node.asset);
            if (found==state->cached_meshes.end()) {
                try {
                // Directory-backed projects must not follow symlink/junction
                // redirections when loading authored mesh assets.
                if (!state->source_project_root.empty()) {
                    auto relative=std::filesystem::u8path(object.node.asset);
                    if(relative.is_absolute())
                        throw std::invalid_argument("Invalid absolute scene OBJ reference");
                    auto cursor=state->source_project_root;
                    if(!std::filesystem::exists(cursor/relative))
                        cursor/="assets";
                    for (auto const& part:relative) {
                        if (part=="." || part==".." || part.is_absolute())
                            throw std::invalid_argument("Unsafe mesh virtual path");
                        cursor/=part;
                        if (std::filesystem::is_symlink(cursor))
                            throw std::invalid_argument("Redirected OBJ mesh asset");
                    }
                }
                if(extpath.compare(extpath.size()-4,4,".ydd")==0 ||
                   extpath.compare(extpath.size()-4,4,".ydr")==0) {
                    auto native=load_native_model_resource(
                        *state->project_assets,object.node.asset);
                    for(auto& entry:native.materials)
                        state->cached_materials.try_emplace(
                            entry.first,std::move(entry.second));
                    if(native.has_skin()) {
                        State::NativeRig rig{};
                        rig.skeleton=std::move(*native.skeleton);
                        rig.weights=std::move(native.vertex_skin);
                        state->cached_native_skins.try_emplace(
                            object.node.asset,std::move(rig));
                    }
                    found=state->cached_meshes.emplace(
                        object.node.asset,std::move(native.mesh)).first;
                } else {
                    auto model=load_scene_mesh(
                        *state->project_assets,object.node.asset);
                    found=state->cached_meshes.emplace(
                        object.node.asset,std::move(model)).first;
                }
                } catch(std::exception const& error) {
                    if(state->invalid_meshes.insert(object.node.asset).second)
                        state->warn_asset("mesh",object.node.asset,error);
                    continue; // Only this authored mesh is omitted.
                }
            }
            // Optional scene v1 component payload:
            // animations/weapon/pistol/military-pistol.ycd@p-fire
            // Only this entity animates; the shared native model is immutable.
            MeshAsset animated_mesh;
            MeshAsset const* selected_mesh=&found->second;
            bool gpu_skin=false;
            std::vector<float> gpu_skin_palette;
            auto const animation=animation_bindings.find(object.node.id);
            auto rig_it=state->cached_native_skins.find(object.node.asset);
            if(animation!=animation_bindings.end() &&
               rig_it!=state->cached_native_skins.end()) {
                auto& rig=rig_it->second;
                auto& play=state->native_playback[object.node.id];
                if(play.clip_reference!=animation->second) {
                    play.clip_reference=animation->second;
                    play.seconds=0.f;
                } else play.seconds+=delta;
                try {
                    auto const& ref=play.clip_reference;
                    auto const at=ref.rfind('@');
                    if(at==std::string::npos || at<4 || at+1>=ref.size() ||
                       ref.find("..")!=std::string::npos ||
                       ref.find('\\')!=std::string::npos ||
                       ref.find(':')!=std::string::npos ||
                       ref.find('\0')!=std::string::npos ||
                       ref.substr(at-4,4)!=".ycd")
                        throw std::invalid_argument("Invalid native YCD clip reference");
                    if(!rig.invalid_clips.count(ref)) {
                        auto clip=rig.clips.find(ref);
                        if(clip==rig.clips.end()) {
                            auto loaded=load_skeletal_clips(*state->project_assets,
                                ref.substr(0,at),{ref.substr(at+1)},rig.skeleton);
                            clip=rig.clips.emplace(ref,std::move(loaded.front())).first;
                        }
                        SkeletonPose pose(rig.skeleton);
                        pose.sample(rig.skeleton,clip->second,play.seconds,false);
                        // GPU only for opaque materials supported by v1.
                        // The CPU path remains mandatory for custom graphs,
                        // transparent assets or external render providers.
                        bool compatible=state->skinned_material_api &&
                            rig.skeleton.joints.size()<=NORTHSTARCPP_SKIN_MAX_JOINTS &&
                            rig.weights.size()==found->second.vertices.size() &&
                            !found->second.sections.empty() &&
                            native_skin_draws.size()+found->second.sections.size()<=
                                NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES;
                        if(compatible) {
                            for(auto const& section:found->second.sections) {
                                auto const& material_ref=has_material ?
                                    material_it->second : section.material_reference;
                                auto mat=state->cached_materials.find(material_ref);
                                if(material_ref.empty() ||
                                   mat==state->cached_materials.end() ||
                                   !mat->second.graph_shader_defines.empty() ||
                                   mat->second.blend_mode!=0 ||
                                   mat->second.opacity!=1.f) {
                                    compatible=false;
                                    break;
                                }
                            }
                        }
                        if(compatible) {
                            // Matrices use row vectors: authored bone pose
                            // multiplied by the same object transform used
                            // for the static PBR geometry submission.
                            math::float4x4 world{};
                            world.elements[0][0]=cube.rotation[0]*object.size;
                            world.elements[0][1]=cube.rotation[3]*object.size;
                            world.elements[0][2]=cube.rotation[6]*object.size;
                            world.elements[1][0]=cube.rotation[1]*object.size;
                            world.elements[1][1]=cube.rotation[4]*object.size;
                            world.elements[1][2]=cube.rotation[7]*object.size;
                            world.elements[2][0]=cube.rotation[2]*object.size;
                            world.elements[2][1]=cube.rotation[5]*object.size;
                            world.elements[2][2]=cube.rotation[8]*object.size;
                            world.elements[3][0]=object.position.x;
                            world.elements[3][1]=object.position.y;
                            world.elements[3][2]=object.position.z;
                            world.elements[3][3]=1.f;
                            gpu_skin_palette.reserve(pose.palette().size()*16);
                            for(auto const& joint:pose.palette()) {
                                auto const combined=joint*world;
                                for(unsigned row=0;row<4;++row)
                                    for(unsigned col=0;col<4;++col)
                                        gpu_skin_palette.push_back(
                                            combined.elements[row][col]);
                            }
                            gpu_skin=true;
                        } else {
                            animated_mesh=skin_native_mesh_cpu(found->second,
                                rig.skeleton,rig.weights,pose.palette());
                            selected_mesh=&animated_mesh;
                        }
                    }
                } catch(std::exception const& error) {
                    if(rig.invalid_clips.insert(play.clip_reference).second)
                        state->warn_asset("YCD animation",play.clip_reference,error);
                }
            }
            auto const& mesh=*selected_mesh;
            // Preserve each native MeshPart's YMAT slot and UV topology.
            // The explicit scene material (if any) overrides every part.
            auto submit_range=[&](std::size_t begin,std::size_t count,
                                  std::string const& material) {
                if(begin>mesh.indices.size() || count>mesh.indices.size()-begin ||
                   count%3!=0)
                    throw std::invalid_argument("Invalid scene mesh section range");
                if(gpu_skin) {
                    PendingNativeSkinDraw draw{};
                    draw.material_reference=material;
                    draw.palette=gpu_skin_palette;
                    draw.vertices.reserve(count);
                    auto const& weights=rig_it->second.weights;
                    for(std::size_t k=begin;k<begin+count;++k) {
                        auto index=mesh.indices[k];
                        auto const& v=mesh.vertices.at(index);
                        auto const& influence=weights.at(index);
                        NorthStarSkinMaterialVertex point{};
                        point.x=v.position.x;
                        point.y=v.position.y;
                        point.z=v.position.z;
                        point.nx=v.normal.x;
                        point.ny=v.normal.y;
                        point.nz=v.normal.z;
                        point.u=v.u;point.v=v.v;
                        for(unsigned j=0;j<8;++j) {
                            point.joints[j]=influence.joints[j];
                            point.weights[j]=influence.weights[j];
                        }
                        draw.vertices.push_back(point);
                    }
                    native_skin_draws.push_back(std::move(draw));
                    return;
                }
                if(!material.empty()) {
                    if(!state->material_api && !state->material_api_v2)
                        throw std::runtime_error(
                            "Native YDD/YDR requires render Materials ABI v1 or v2");
                    auto& batch=material_vertices(material);
                    for(std::size_t k=begin;k<begin+count;++k) {
                        auto const& v=mesh.vertices.at(mesh.indices[k]);
                        auto const point=transformed({
                            v.position.x*object.size,v.position.y*object.size,
                            v.position.z*object.size},cube,object.position);
                        auto const normal=normal_direction(v.normal,cube);
                        push_material(batch,point,normal,v.u,v.v);
                    }
                } else {
                    if(mesh_vertices.size()+count>300000)
                        throw std::runtime_error("Scene triangle budget exceeded");
                    auto const color=static_cast<int>(i)==selected ?
                        0xffd474u : object.color;
                    for(std::size_t k=begin;k<begin+count;++k) {
                        auto const& v=mesh.vertices.at(mesh.indices[k]);
                        auto const world=transformed({
                            v.position.x*object.size,v.position.y*object.size,
                            v.position.z*object.size},cube,object.position);
                        mesh_vertices.push_back({world.x,world.y,world.z,color});
                    }
                }
            };
            if(!mesh.sections.empty()) {
                for(auto const& section:mesh.sections)
                    submit_range(section.first_index,section.index_count,
                        has_material ? material_it->second :
                            section.material_reference);
            } else {
                submit_range(0,mesh.indices.size(),
                    has_material ? material_it->second : std::string{});
            }
            if (static_cast<int>(i)==selected || (selected==-2 && object.player))
                focus=position;
            continue;
        }
        if(has_material) {
            // The default scene cube also participates in the exact same
            // material pipeline. Its six faces have independent 0..1 UVs.
            // This is a real GPU material draw, not an assigned image on
            // the old color-only cube proxy.
            static constexpr float faces[6][4][3]{
                {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
                {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},
                {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
                {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
                {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},
                {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}}
            };
            static const Vector3 normals[6]{{0,0,1},{0,0,-1},
                {0,1,0},{0,-1,0},{1,0,0},{-1,0,0}};
            static constexpr unsigned triangles[6]{0,1,2,0,2,3};
            static constexpr float uvs[4][2]{{0,1},{1,1},{1,0},{0,0}};
            auto& batch=material_vertices(material_it->second);
            auto const middle=Vector3{
                object.position.x,object.position.y+object.size*.5f,
                object.position.z};
            for(unsigned face=0;face<6;++face) {
                auto const normal=normal_direction(normals[face],cube);
                for(auto const index:triangles) {
                    auto const& v=faces[face][index];
                    auto const point=transformed({
                        v[0]*object.size*.5f,v[1]*object.size*.5f,
                        v[2]*object.size*.5f},cube,middle);
                    push_material(batch,point,normal,uvs[index][0],uvs[index][1]);
                }
            }
            if(static_cast<int>(i)==selected ||
               (selected==-2 && object.player))focus=position;
            continue;
        }
        if (static_cast<int>(i) == selected) {
            selected_render_index = static_cast<int>(cubes.size());
            focus = position;
        } else if (selected == -2 && object.player) {
            focus = position;
        }
        if(state->world_surfaces_ready) {
            auto const over=surface_overrides.find(object.node.id);
            auto const id=over!=surface_overrides.end()?over->second:
                world_surface_category(object.node.layer.empty()?object.name:
                    object.node.layer+" "+object.name);
            surface_ids.push_back(id);
            if(id)cube.rgb=0xffffffu; // preserve source surface albedo
        }
        cubes.push_back(cube);
    }

    if (fps) focus = fps->eye(scene);
    if (camera_pose) focus = camera_pose->position;

    // Two-level runtime HLOD: one conservative AABB proxy replaces multiple
    // distant static draw calls. Original physics/ECS objects are untouched.
    if (streaming) {
        for (auto const& proxy:scene_world->hlod_proxies()) {
            if (cubes.size()>=max_scene_objects) break;
            NorthStarRenderCube lod{};
            lod.x=proxy.center.x;
            lod.y=proxy.center.y-0.5f; // provider adds size*0.5 to Y
            lod.z=proxy.center.z;
            lod.size=1.f;
            lod.rgb=proxy.color;
            lod.rotation[0]=std::max(0.04f,proxy.half_extent.x*2.f);
            lod.rotation[4]=std::max(0.04f,proxy.half_extent.y*2.f);
            lod.rotation[8]=std::max(0.04f,proxy.half_extent.z*2.f);
            cubes.push_back(lod);
        }
    }
    // In-editor streaming visualization. Render 3D cell AABB edges through
    // the same GPU provider, without exposing debug-specific ABI pointers.
    // Limit work and draw distances to protect the frame budget.
    if (streaming && show_streaming_debug && !fps) {
        constexpr std::size_t max_cells=256;
        std::size_t emitted=0;
        auto edge=[&](Vector3 a,Vector3 b,std::uint32_t color) {
            if (cubes.size()>=max_scene_objects) return;
            NorthStarRenderCube line{};
            line.x=(a.x+b.x)*0.5f;
            line.y=(a.y+b.y)*0.5f-0.5f;
            line.z=(a.z+b.z)*0.5f;
            line.size=1.f;
            line.rgb=color;
            line.rotation[0]=std::max(0.09f,std::abs(b.x-a.x));
            line.rotation[4]=std::max(0.09f,std::abs(b.y-a.y));
            line.rotation[8]=std::max(0.09f,std::abs(b.z-a.z));
            cubes.push_back(line);
        };
        for (auto const& cell:scene_world->source().cells) {
            if (emitted>=max_cells || cubes.size()+12>=max_scene_objects) break;
            auto const cx=(cell.minimum.x+cell.maximum.x)*0.5f;
            auto const cz=(cell.minimum.z+cell.maximum.z)*0.5f;
            auto const dx=cx-focus.x;
            auto const dz=cz-focus.z;
            if (dx*dx+dz*dz>600.f*600.f) continue;
            ++emitted;
            std::uint32_t color=cell.always_loaded ? 0x729bdfu :
                scene_world->cell_loaded(cell.name) ? 0x51c68fu : 0xb66d75u;
            auto const x0=cell.minimum.x,x1=cell.maximum.x;
            auto const y0=cell.minimum.y,y1=cell.maximum.y;
            auto const z0=cell.minimum.z,z1=cell.maximum.z;
            edge({x0,y0,z0},{x1,y0,z0},color);
            edge({x0,y0,z1},{x1,y0,z1},color);
            edge({x0,y1,z0},{x1,y1,z0},color);
            edge({x0,y1,z1},{x1,y1,z1},color);
            edge({x0,y0,z0},{x0,y1,z0},color);
            edge({x0,y0,z1},{x0,y1,z1},color);
            edge({x1,y0,z0},{x1,y1,z0},color);
            edge({x1,y0,z1},{x1,y1,z1},color);
            edge({x0,y0,z0},{x0,y0,z1},color);
            edge({x1,y0,z0},{x1,y0,z1},color);
            edge({x0,y1,z0},{x0,y1,z1},color);
            edge({x1,y1,z0},{x1,y1,z1},color);
        }
    }

    // Editor transform overlay: colored axis handles and rotation rings. These
    // are transient GPU draw commands, not scene/ECS objects or physics bodies.
    if (gizmo && selected >= 0 &&
        static_cast<std::size_t>(selected) < scene.objects.size() && !fps) {
        auto const center=scene.objects[static_cast<std::size_t>(selected)].position;
        auto const add=[](Vector3 a, Vector3 b)->Vector3 {
            return {a.x+b.x,a.y+b.y,a.z+b.z};
        };
        auto const mul=[](Vector3 a,float t)->Vector3 {
            return {a.x*t,a.y*t,a.z*t};
        };
        auto const cross=[](Vector3 a,Vector3 b)->Vector3 {
            return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        };
        auto const normalize=[](Vector3 v)->Vector3 {
            float const length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
            if (length<1e-5f) return {1.f,0.f,0.f};
            return {v.x/length,v.y/length,v.z/length};
        };
        auto segment=[&](Vector3 a,Vector3 b,std::uint32_t rgb,float thickness) {
            if (cubes.size()>=max_scene_objects) return;
            auto const tangent=normalize({b.x-a.x,b.y-a.y,b.z-a.z});
            auto const side=normalize(cross(tangent,std::abs(tangent.y)>.9f
                ? Vector3{0.f,0.f,1.f} : Vector3{0.f,1.f,0.f}));
            auto const up=cross(tangent,side);
            float const length=std::sqrt((b.x-a.x)*(b.x-a.x)+
                (b.y-a.y)*(b.y-a.y)+(b.z-a.z)*(b.z-a.z));
            NorthStarRenderCube line{};
            line.x=(a.x+b.x)*.5f;
            line.y=(a.y+b.y)*.5f-.5f;
            line.z=(a.z+b.z)*.5f;
            line.size=1.f;
            line.rgb=rgb;
            line.rotation[0]=tangent.x*length; line.rotation[3]=tangent.y*length; line.rotation[6]=tangent.z*length;
            line.rotation[1]=side.x*thickness; line.rotation[4]=side.y*thickness; line.rotation[7]=side.z*thickness;
            line.rotation[2]=up.x*thickness; line.rotation[5]=up.y*thickness; line.rotation[8]=up.z*thickness;
            cubes.push_back(line);
        };
        std::uint32_t const colors[3]{0xf46b73u,0x5ce4a1u,0x64a8ffu};
        for (unsigned axis=0;axis<3;++axis) {
            auto const direction=gizmo->axes[axis];
            auto const color=gizmo->active_axis==static_cast<int>(axis) ?
                0xffd66bu : colors[axis];
            if (gizmo->mode==1) {
                auto const u=normalize(cross(direction,
                    std::abs(direction.y)>.9f ? Vector3{0.f,0.f,1.f} : Vector3{0.f,1.f,0.f}));
                auto const v=cross(direction,u);
                constexpr float tau=6.283185307179586f;
                constexpr int segments=40;
                for (int i=0;i<segments && cubes.size()<max_scene_objects;++i) {
                    float const t0=tau*float(i)/float(segments);
                    float const t1=tau*float(i+1)/float(segments);
                    auto point=[&](float angle) {
                        return add(center,add(mul(u,gizmo->length*.8333333f*std::cos(angle)),
                            mul(v,gizmo->length*.8333333f*std::sin(angle))));
                    };
                    segment(point(t0),point(t1),color,.045f);
                }
            } else {
                segment(center,add(center,mul(direction,gizmo->length)),color,
                    std::max(.04f,gizmo->length*.035f));
                if (gizmo->mode==2 && cubes.size()<max_scene_objects) {
                    auto const tip=add(center,mul(direction,gizmo->length));
                    NorthStarRenderCube handle{};
                    handle.x=tip.x; handle.y=tip.y-gizmo->length*.13f; handle.z=tip.z;
                    handle.size=std::max(.1f,gizmo->length*.26f); handle.rgb=color;
                    handle.rotation[0]=handle.rotation[4]=handle.rotation[8]=1.f;
                    cubes.push_back(handle);
                }
            }
        }
    }

    // The host resolves material resources through VFS; GPU plugins only
    // see copied UV geometry, factors and bounded RGBA8 textures.
    std::vector<NorthStarMaterialBatch> gpu_materials;
    gpu_materials.reserve(material_draws.size());
    std::vector<NorthStarMaterialBatchV2> gpu_materials_v2;
    gpu_materials_v2.reserve(material_draws.size());
    bool needs_material_v2=false;
    std::size_t material_vertex_total=0;
    for(auto const& entry:material_draws) {
        auto const& reference=entry.first;
        auto const& geometry=entry.second;
        if(geometry.empty() || state->invalid_materials.count(reference))continue;
        if(!state->project_assets) {
            if(state->invalid_materials.insert(reference).second)
                std::fprintf(stderr,
                    "[WARN] [engine.assets] Skipping material '%s': no VFS sources mounted\n",
                    reference.c_str());
            continue;
        }
        auto found=state->cached_materials.find(reference);
        if(found==state->cached_materials.end()) {
            try {
                auto value=load_material_definition(*state->project_assets,reference);
                found=state->cached_materials.emplace(reference,std::move(value)).first;
            } catch(std::exception const& error) {
                if(state->invalid_materials.insert(reference).second)
                    state->warn_asset("material",reference,error);
                continue; // No placeholder material: skip only this batch.
            }
        }
        material_vertex_total+=geometry.size();
        if(material_vertex_total>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)
            throw std::runtime_error("Material geometry exceeds GPU batch budget");
        auto const& material=found->second;
        NorthStarMaterialBatch batch{};
        batch.vertices=geometry.data();
        batch.vertex_count=static_cast<std::uint32_t>(geometry.size());
        std::copy(material.base_color.begin(),material.base_color.end(),
                  std::begin(batch.base_color));
        batch.metallic=material.metallic;
        batch.roughness=material.roughness;
        batch.unlit=material.unlit?1u:0u;
        for(unsigned slot=0;slot<NORTHSTARCPP_RENDER_MATERIAL_TEXTURE_SLOTS;++slot) {
            auto const& texture=material.textures[slot];
            if(!texture.rgba8.empty())
                batch.textures[slot]={texture.rgba8.data(),texture.width,
                    texture.height,static_cast<std::uint32_t>(texture.rgba8.size())};
        }
        gpu_materials.push_back(batch);
        NorthStarMaterialBatchV2 variant{};
        variant.material=batch;
        variant.graph_hlsl_defines=material.graph_shader_defines.empty()?nullptr:
            material.graph_shader_defines.c_str();
        variant.graph_hlsl_bytes=static_cast<std::uint32_t>(
            material.graph_shader_defines.size());
        variant.blend_mode=material.blend_mode;
        variant.alpha_cutoff=material.alpha_cutoff;
        variant.opacity=material.opacity;
        needs_material_v2|=(variant.graph_hlsl_bytes!=0 ||
            variant.blend_mode!=0 || variant.opacity!=1.f);
        gpu_materials_v2.push_back(variant);
    }
    // GPU skinning owns original YDD vertex+influence bytes per draw.
    // The GPU render provider copies all submitted buffers, including the
    // palette and textures, so these frame-local vectors are safe.
    std::vector<NorthStarSkinnedMaterialBatch> gpu_skinned_batches;
    gpu_skinned_batches.reserve(native_skin_draws.size());
    for(auto const& entry:native_skin_draws) {
        auto const& material=state->cached_materials.at(entry.material_reference);
        NorthStarMaterialBatch base{};
        std::copy(material.base_color.begin(),material.base_color.end(),
                  std::begin(base.base_color));
        base.roughness=material.roughness;
        base.metallic=material.metallic;
        base.unlit=material.unlit?1u:0u;
        for(unsigned slot=0;slot<NORTHSTARCPP_RENDER_MATERIAL_TEXTURE_SLOTS;++slot) {
            auto const& texture=material.textures[slot];
            if(!texture.rgba8.empty())
                base.textures[slot]={texture.rgba8.data(),texture.width,
                    texture.height,static_cast<std::uint32_t>(texture.rgba8.size())};
        }
        NorthStarSkinnedMaterialBatch batch{};
        batch.material.material=base;
        batch.material.opacity=1.f;
        batch.material.alpha_cutoff=material.alpha_cutoff;
        batch.vertices=entry.vertices.data();
        batch.vertex_count=static_cast<std::uint32_t>(entry.vertices.size());
        batch.palette=entry.palette.data();
        batch.joint_count=static_cast<std::uint32_t>(entry.palette.size()/16);
        gpu_skinned_batches.push_back(batch);
    }
    NorthStarRenderFrame const frame{cubes.data(), static_cast<uint32_t>(cubes.size()),
        fps ? -1 : selected_render_index,
        focus.x, focus.y, focus.z, verify_pixels ? 1u : 0u,
        fps ? 1u : 0u,
        camera_pose ? camera_pose->yaw : (fps ? fps->yaw() : 0.f),
        camera_pose ? camera_pose->pitch : (fps ? fps->pitch() : 0.f),
        fps ? fps->recoil_amount() : 0.f,
        fps ? static_cast<uint32_t>(fps->health()) : 0u,
        fps && show_fps_overlay ? static_cast<uint32_t>(fps->maximum_health()) : 0u,
        fps ? static_cast<uint32_t>(fps->ammunition()) : 0u,
        fps ? static_cast<uint32_t>(fps->magazine_size()) : 0u,
        paused ? 1u : 0u,
        camera_pose ? 1u : 0u,
        camera_pose ? camera_pose->roll : 0.f,
        camera_pose ? camera_pose->fov_y_degrees : 45.f,
        camera_pose ? camera_pose->near_plane : 0.1f,
        camera_pose ? camera_pose->far_plane :
            (scene.version>=3 ? 1500.f : 250.f)};
    auto const scene_lights=northstarcpp::render::scene_render_lights(scene);
    auto submit=[&] {
        if(state->lights_api) {
            if(!state->lights_api->set_lights(state->renderer,
                scene_lights.empty()?nullptr:scene_lights.data(),
                static_cast<std::uint32_t>(scene_lights.size())))
                throw state->module->error("Cannot submit dynamic scene lights");
        } else if(!scene_lights.empty()&&!state->warned_missing_light_provider) {
            std::fprintf(stderr,"[WARN] [engine.render] %zu scene lights ignored: "
                "render provider lacks optional dynamic-light ABI\n",scene_lights.size());
            state->warned_missing_light_provider=true;
        }
        if(state->surface_api && state->world_surfaces_ready) {
            surface_ids.resize(cubes.size(),0u); // debug/HLOD/gizmo neutral
            if(!state->surface_api->set_cube_surfaces(state->renderer,
                surface_ids.data(),static_cast<std::uint32_t>(surface_ids.size())))
                throw state->module->error("Cannot submit world surface selectors");
        }
        if(state->triangle_api &&
           !state->triangle_api->set_triangles(state->renderer,
               mesh_vertices.empty()?nullptr:mesh_vertices.data(),
               static_cast<std::uint32_t>(mesh_vertices.size())))
            throw state->module->error("Cannot submit scene mesh triangles");
        if(state->skinned_material_api &&
           !state->skinned_material_api->set_batches(state->renderer,
               gpu_skinned_batches.empty()?nullptr:gpu_skinned_batches.data(),
               static_cast<std::uint32_t>(gpu_skinned_batches.size())))
            throw state->module->error("Cannot upload native GPU skin batches");
        if(needs_material_v2 && !state->material_api_v2)
            throw std::runtime_error(
                "Render provider has no Material Graph/Instance ABI v2");
        if(state->material_api_v2) {
            if(!state->material_api_v2->set_batches_v2(state->renderer,
                gpu_materials_v2.empty()?nullptr:gpu_materials_v2.data(),
                static_cast<std::uint32_t>(gpu_materials_v2.size())))
                throw state->module->error("Cannot upload authored Material Graph batches");
        }else if(state->material_api &&
                 !state->material_api->set_batches(state->renderer,
                    gpu_materials.empty()?nullptr:gpu_materials.data(),
                    static_cast<std::uint32_t>(gpu_materials.size())))
            throw state->module->error("Cannot upload authored GPU materials");
        return state->api->draw(state->renderer,&frame)!=0;
    };
    if(!submit()) {
        if(state->module->provider()!="d3d12")
            throw state->module->error("Cannot render frame");
        state->fallback_to_d3d11();
        if(!submit())throw state->module->error("Cannot render frame after D3D12 fallback");
    }
}

} // namespace northstarcpp
