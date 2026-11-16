// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include "viewport.hpp"
#include "../shared/frame_profiler.hpp"
#include <northstarcpp/collision_wireframe.hpp>
#include "viewport_collision_wireframe.hpp"
#include "viewport_material_batches.hpp"
#include "viewport_skin_cache.hpp"
#include "viewport_skeletal_animation.hpp"
#include "editor_character_marker.hpp"
#include "editor_grid.hpp"
#include <northstarcpp/scene_world.hpp>

#include <northstarcpp/physics.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/procedural_environment.hpp>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/ui.hpp>
#include <northstarcpp/render_editor_grid_api.h>
#include <northstarcpp/render_pick_api.h>
#include <northstarcpp/render_feature_requirements.hpp>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
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

namespace {
// Browser passes a validated comma-delimited list of strict project
// requirements to the Editor/Viewport child. A directly launched Editor can
// also supply it manually; unknown or duplicated feature identifiers fail.
std::vector<std::string> renderer_requirements_from_environment()
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
void require_provider_render_features(
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
        auto const required=renderer_requirements_from_environment();
        std::vector<std::string> providers;
        if(selection_length)providers.emplace_back(selected.data());
        else providers={"d3d12","d3d11"}; // Existing automatic fallback.
        for(std::size_t i=0;i<providers.size();++i) {
            try {
                auto candidate=std::make_unique<Module>(
                    NORTHSTARCPP_CAPABILITY_RENDER,providers[i]);
                auto const* candidate_api=
                    candidate->query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
                require_provider_render_features(*candidate,required);
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
        shader_reload_api=module->query_extension<NorthStarRenderShaderReloadApi>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI);
        shader_reload_v2=module->query_extension<NorthStarRenderShaderReloadApiV2>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI);
        postfx_api=module->query_extension<NorthStarRenderPostFxApi>(
            NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI);
        timing_api=module->query_extension<NorthStarRenderTimingApi>(
            NORTHSTARCPP_RENDER_TIMING_MODULE_ABI);
        skinned_material_api=module->query_extension<NorthStarRenderSkinnedMaterialsApi>(
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI);
        lights_api=module->query_extension<NorthStarRenderLightsApi>(
            NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI);
        editor_grid_api=module->query_extension<NorthStarRenderEditorGridApi>(
            NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI);
        pick_api=module->query_extension<NorthStarRenderPickApi>(
            NORTHSTARCPP_RENDER_PICK_MODULE_ABI);
        pick_v2_api=module->query_extension<NorthStarRenderPickApiV2>(
            NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI);
        pick_async_api=module->query_extension<NorthStarRenderPickAsyncApi>(
            NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI);
        try {
            cubes.reserve(max_scene_objects);
            surface_ids.reserve(max_scene_objects);
        } catch (...) {
            api->destroy(renderer);
            throw;
        }
    }

    ~State() {
        if(ui_context&&ui_api)ui_api->destroy_context(ui_context);
        if(renderer)api->destroy(renderer);
    }
    void prepare_ui_overlay(bool gameplay) {
        // A generic UI document authored by scripts, never a mission-specific
        // structure. The optional demo uses the same element-tree contract.
        UiDocument demo;
        auto const* document=&ui_document;
        const char* enabled=std::getenv("NORTHSTARCPP_UI_DEMO");
        if(ui_document.nodes.empty() && gameplay && enabled &&
           std::strcmp(enabled,"1")==0) {
            UiDocumentNode panel;
            panel.id="demoPanel";
            panel.kind=NORTHSTAR_UI_PANEL;
            panel.properties={
                {"width",{NORTHSTAR_UI_VALUE_NUMBER,320}},
                {"height",{NORTHSTAR_UI_VALUE_NUMBER,65}},
                {"background",{NORTHSTAR_UI_VALUE_COLOR_RGBA8,0,0x0b1423d9}}
            };
            UiDocumentNode label;
            label.id="demoText";
            label.parent="demoPanel";
            label.kind=NORTHSTAR_UI_TEXT;
            UiPropertyValue text;
            text.kind=NORTHSTAR_UI_VALUE_STRING;
            text.text="NORTHSTAR UI NATIVE";
            label.properties={{"text",std::move(text)}};
            demo.nodes={std::move(panel),std::move(label)};
            document=&demo;
        }
        if(document->nodes.empty()) {
            ui_fx_target_id.clear();
            ui_fx_id.clear();
            ui_fx_hover=false;
            ui_fx_pressed=false;
            ui_fx_press_burst=false;
            if(ui_effects_api && ui_effects_api->set_hover_fx)
                ui_effects_api->set_hover_fx(renderer,nullptr);
            if(ui_render_api && ui_active &&
               !ui_render_api->set_rgba(renderer,nullptr,0,0,0))
                throw module->error("Cannot clear UI composition");
            ui_active=false;
            if(ui_context&&ui_api) {
                ui_api->destroy_context(ui_context);
                ui_context=nullptr;
                ui_handles.clear();
                ui_names_by_handle.clear();
                ui_events.clear();
                ui_applied_document={};
            }
            return;
        }
        if(!ui_render_api) {
            ui_render_api=module->query_extension<NorthStarRenderUiApi>(
                NORTHSTARCPP_RENDER_UI_MODULE_ABI);
            ui_effects_api=module->query_extension<NorthStarRenderUiEffectsApi>(
                NORTHSTARCPP_RENDER_UI_EFFECTS_MODULE_ABI);
            ui_shaders_api=module->query_extension<NorthStarRenderUiShadersApi>(
                NORTHSTARCPP_RENDER_UI_SHADERS_MODULE_ABI);
        }
        if(!ui_render_api)
            throw std::runtime_error(
                "Active engine.render provider has no GPU UI compositor");
        if(!ui_project_shaders_loaded && project_assets && ui_shaders_api) {
            // Renderer selection is independent of project styling. VFS can
            // read loose files and packed archives using the same asset IDs.
            ui_project_shaders_loaded=true;
            // Shader asset IDs are authored by the project UI document.
            auto shaderPath=[&](char const* key)->std::string {
                for(auto const& node:document->nodes)
                    for(auto const& [property,value]:node.properties)
                        if(property==key && value.kind==NORTHSTAR_UI_VALUE_STRING)
                            return value.text;
                return {};
            };
            auto loadShader=[&](char const* path)->std::vector<std::uint8_t> {
                try {
                    if(project_assets->size(path)==0)return {};
                    return project_assets->read(path);
                } catch(std::exception const& error) {
                    warn_asset("Project UI shader",path,error);
                    return {};
                }
            };
            if(ui_shaders_api->set_hlsl) {
                auto path=shaderPath("ui_shader_hlsl");
                auto source=path.empty()?std::vector<std::uint8_t>{}:loadShader(path.c_str());
                if(!source.empty()) {
                    if(source.size()>262144 ||
                       !ui_shaders_api->set_hlsl(renderer,
                         reinterpret_cast<char const*>(source.data()),
                         static_cast<std::uint32_t>(source.size()))) {
                        std::fprintf(stderr,
                            "[NorthStarCpp] Warning: invalid project UI HLSL %s: %s\n",
                            path.c_str(),module->error("Project shader error").what());
                    } else {
                        std::fprintf(stderr,
                            "[NorthStarCpp] Project UI shader loaded: %s\n",path.c_str());
                    }
                }
            }
            if(ui_shaders_api->set_spirv) {
                auto path=shaderPath("ui_shader_spirv");
                auto bytes=path.empty()?std::vector<std::uint8_t>{}:loadShader(path.c_str());
                if(!bytes.empty()) {
                    if(bytes.size()>1048576 || bytes.size()%4!=0) {
                        std::fprintf(stderr,
                            "[NorthStarCpp] Warning: invalid project UI SPIR-V %s\n",path.c_str());
                    } else {
                        std::vector<std::uint32_t> words(bytes.size()/4);
                        std::memcpy(words.data(),bytes.data(),bytes.size());
                        if(!ui_shaders_api->set_spirv(renderer,words.data(),
                                static_cast<std::uint32_t>(words.size()))) {
                            std::fprintf(stderr,
                                "[NorthStarCpp] Warning: project UI SPIR-V rejected: %s\n",
                                path.c_str());
                        } else {
                            std::fprintf(stderr,
                                "[NorthStarCpp] Project UI shader loaded: %s\n",path.c_str());
                        }
                    }
                }
            }
        }
        if(!ui_module) {
            ui_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_UI);
            ui_api=ui_module->query<NorthStarUIApi>(NORTHSTARCPP_UI_ABI);
            ui_raster=ui_module->query_extension<NorthStarUIRasterApi>(
                NORTHSTARCPP_UI_RASTER_ABI);
            ui_raster_layers=ui_module->query_extension<NorthStarUIRasterLayersApi>(
                NORTHSTARCPP_UI_RASTER_LAYERS_MODULE_ABI);
            ui_input_api=ui_module->query_extension<NorthStarUIInputApi>(
                NORTHSTARCPP_UI_INPUT_ABI);
            if(!ui_raster)
                throw std::runtime_error("Active engine.ui cannot rasterize");
        }
        auto set=[&](NorthStarUIHandle handle,
                     std::pair<std::string,UiPropertyValue> const& property) {
            auto const& input=property.second;
            NorthStarUIValue value{};
            switch(input.kind) {
            case NORTHSTAR_UI_VALUE_NUMBER: value=ui::number(input.number);break;
            case NORTHSTAR_UI_VALUE_BOOL: value=ui::boolean(input.boolean);break;
            case NORTHSTAR_UI_VALUE_STRING: value=ui::text(input.text);break;
            case NORTHSTAR_UI_VALUE_COLOR_RGBA8:
                value={sizeof(NorthStarUIValue),NORTHSTAR_UI_VALUE_COLOR_RGBA8,
                    0,input.rgba,0,{}};break;
            default:throw std::invalid_argument("Invalid UI property value type");
            }
            if(ui_api->set_property(ui_context,handle,ui::string(property.first),
                &value)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot apply UI property");
        };
        bool same_structure=ui_context &&
            document->nodes.size()==ui_applied_document.nodes.size();
        if(same_structure) {
            for(std::size_t i=0;i<document->nodes.size();++i) {
                const auto& current=document->nodes[i];
                const auto& previous=ui_applied_document.nodes[i];
                if(current.id!=previous.id || current.parent!=previous.parent ||
                   current.kind!=previous.kind ||
                   current.properties.size()!=previous.properties.size()) {
                    same_structure=false;break;
                }
                for(std::size_t j=0;j<current.properties.size();++j)
                    if(current.properties[j].first!=previous.properties[j].first ||
                       current.properties[j].second.kind!=
                       previous.properties[j].second.kind) {
                        same_structure=false;break;
                    }
                if(!same_structure)break;
            }
        }
        if(!same_structure) {
            if(ui_context) {
                ui_api->destroy_context(ui_context);
                ui_context=nullptr;
                ui_handles.clear();
                ui_names_by_handle.clear();
                ui_events.clear();
            }
            NorthStarUIConfig cfg{sizeof(NorthStarUIConfig),0,nullptr};
            if(ui_api->create_context(&cfg,&ui_context)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create UI context");
            // Provider callbacks are synchronous during pump_events().
            // Buffer the event; script callbacks run later, never reentrantly
            // inside the C ABI UI provider.
            if(ui_api->set_event_callback(ui_context,
                    [](void* owner,const NorthStarUIEvent* event) {
                        if(!owner||!event)return;
                        auto& state=*static_cast<State*>(owner);
                        try {
                            if(state.ui_events.size()>=1024)return;
                            auto it=state.ui_names_by_handle.find(event->target);
                            if(it==state.ui_names_by_handle.end())return;
                            UiInteractionEvent next;
                            next.id=it->second;
                            next.kind=event->kind;
                            next.value_kind=event->value.kind;
                            next.number=event->value.number;
                            next.boolean=event->value.boolean!=0;
                            if(event->name.data && event->name.length<=64)
                                next.name.assign(event->name.data,
                                                 static_cast<size_t>(event->name.length));
                            if(event->value.kind==NORTHSTAR_UI_VALUE_STRING &&
                               event->value.string.data &&
                               event->value.string.length<=4096)
                                next.text.assign(event->value.string.data,
                                                 static_cast<size_t>(event->value.string.length));
                            state.ui_events.push_back(std::move(next));
                        } catch(...) {
                            // Never throw through an external provider callback.
                        }
                    },this)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot register UI event sink");
            auto root=ui::element(NORTHSTAR_UI_ROOT,0,"screen");
            NorthStarUIHandle root_handle=0;
            if(ui_api->create_element(ui_context,&root,&root_handle)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot create UI root");
            ui_handles.emplace("",root_handle);
            for(auto const& node:document->nodes) {
                auto parent=ui_handles.find(node.parent);
                if(parent==ui_handles.end())
                    throw std::invalid_argument("UI node parent must precede child");
                auto descriptor=ui::element(node.kind,parent->second,node.id);
                NorthStarUIHandle handle=0;
                if(ui_api->create_element(ui_context,&descriptor,&handle)!=NORTHSTAR_UI_OK)
                    throw ui_module->error("Cannot create UI node");
                ui_handles.emplace(node.id,handle);
                ui_names_by_handle.emplace(handle,node.id);
            }
            ui_last_width=ui_last_height=0;
        }
        if(!same_structure || !(*document==ui_applied_document)) {
            for(std::size_t i=0;i<document->nodes.size();++i) {
                const auto& node=document->nodes[i];
                const auto& previous=same_structure?
                    ui_applied_document.nodes[i]:node;
                const auto handle=ui_handles.at(node.id);
                for(std::size_t j=0;j<node.properties.size();++j)
                    if(!same_structure ||
                       !(node.properties[j].second==previous.properties[j].second))
                        set(handle,node.properties[j]);
            }
            ui_applied_document=*document;
            ui_dirty=true;
        }
        RECT rect{};
        if(!GetClientRect(window_handle,&rect) ||
           rect.right<=rect.left||rect.bottom<=rect.top)return;
        const auto width=static_cast<std::uint32_t>(rect.right-rect.left);
        const auto height=static_cast<std::uint32_t>(rect.bottom-rect.top);
        if(width>4096||height>4096 ||
           static_cast<std::uint64_t>(width)*height*4>64ull*1024*1024)return;
        // Keep the last isolated layer during the short hover fade-out.
        const float fxElapsed=std::chrono::duration<float>(
            std::chrono::steady_clock::now()-ui_fx_changed).count();
        if(!ui_fx_hover && !ui_fx_target_id.empty() && fxElapsed>=0.22f) {
            ui_fx_target_id.clear();
            ui_dirty=true;
        }
        auto layerHandle=ui_handles.find(ui_fx_target_id);
        const bool layered=ui_raster_layers && ui_effects_api &&
            layerHandle!=ui_handles.end() &&
            static_cast<std::uint64_t>(width)*height*8<=64ull*1024*1024 &&
            height<=4096;
        // GPU hover animation advances without rerasterizing the UI texture.
        if(ui_effects_api && ui_effects_api->set_hover_fx) {
            const auto elapsed=std::chrono::duration<float>(
                std::chrono::steady_clock::now()-ui_fx_changed).count();
            const float fade=std::clamp(elapsed/0.22f,0.f,1.f);
            const float ease=fade*fade*(3.f-2.f*fade);
            const float strength=ui_fx_hover?ease:1.f-ease;
            NorthStarRenderUiHoverFx fx{};
            // 150-ms release pulse remains visible even if the click completes
            // between two rendered frames.
            const float pressAge=std::chrono::duration<float>(
                std::chrono::steady_clock::now()-ui_fx_press_changed).count();
            const float releaseFade=std::clamp(1.f-pressAge/0.150f,0.f,1.f);
            const float pressBurst=ui_fx_pressed?1.f:
                (ui_fx_press_burst?releaseFade*releaseFade:0.f);
            fx.layered=layered?1.f:0.f;
            // Time must advance even without hover so the project can animate
            // a very faint resting shimmer without CPU re-rasterization.
            fx.seconds=std::chrono::duration<float>(
                std::chrono::steady_clock::now()-ui_fx_clock_start).count();
            // Initialize position even on the first hover frame (fade=0).
            if(layered && ui_fx_rect.width>0 && ui_fx_rect.height>0) {
                fx.x=ui_fx_rect.x/static_cast<float>(width);
                fx.y=ui_fx_rect.y/static_cast<float>(height);
                fx.width=ui_fx_rect.width/static_cast<float>(width);
                fx.height=ui_fx_rect.height/static_cast<float>(height);
                fx.strength=strength;
                fx.pressed=pressBurst;
                fx.primary=ui_fx_primary?1.f:0.f;
                fx.origin_slot=ui_fx_origin_slot;
                fx.seed=ui_fx_seed;
                fx.layered=layered?1.f:0.f;
                // Hover age also synchronizes the short entry flicker
                // with the pointer-enter event that starts the NAC cue.
                fx.reserved=elapsed;
            }
            if(!layered)fx.strength=0.f;
            if(!ui_effects_api->set_hover_fx(renderer,&fx))
                throw module->error("Cannot update UI hover shader parameters");
        }
        if(ui_active && ui_last_width==width && ui_last_height==height &&
           !ui_dirty)return;
        if(ui_api->resize(ui_context,width,height,1.f)!=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot resize UI context");
        const auto screenBytes=static_cast<std::size_t>(width)*height*4;
        ui_pixels.resize(screenBytes*(layered?2u:1u));
        if(layered) {
            // Base layer omits ONLY the target; the second layer contains the
            // button surface, bevels, glyphs and alpha with transparent space.
            if(ui_raster_layers->draw_filtered_rgba(ui_context,
                ui_pixels.data(),width*4,width,height,layerHandle->second,1)
                !=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot rasterize UI base without button");
            if(ui_raster_layers->draw_filtered_rgba(ui_context,
                ui_pixels.data()+screenBytes,width*4,width,height,
                layerHandle->second,2)!=NORTHSTAR_UI_OK)
                throw ui_module->error("Cannot rasterize isolated UI button");
        } else if(ui_raster->draw_rgba(ui_context,ui_pixels.data(),
                     width*4,width,height)!=NORTHSTAR_UI_OK)
            throw ui_module->error("Cannot rasterize UI document");
        // Project-authored UI images are separate asset-backed layers.
        // Backgrounds composite UNDER the existing premultiplied UI;
        // logos/foreground images composite OVER it with their own alpha.
        // The second isolated-button atlas layer remains untouched.
        for(auto const& node:ui_applied_document.nodes) {
            if(node.kind!=NORTHSTAR_UI_IMAGE || !project_assets)continue;
            auto getString=[&](char const* key)->std::string {
                auto found=std::find_if(node.properties.begin(),node.properties.end(),
                    [&](auto const& item){return item.first==key &&
                        item.second.kind==NORTHSTAR_UI_VALUE_STRING;});
                return found!=node.properties.end()?found->second.text:std::string{};
            };
            auto const source=getString("source");
            if(source.empty())continue;
            auto found=ui_image_cache.find(source);
            if(found==ui_image_cache.end()) {
                UiCachedImage asset;
                try {
                    auto at=source.rfind('@');
                    if(at==std::string::npos || at<4 || at+1>=source.size() ||
                       source.find("..")!=std::string::npos || source[0]=='/' ||
                       source.find(':')!=std::string::npos ||
                       source.substr(at-4,4)!=".ytd")
                        throw std::invalid_argument(
                            "UI source must be project-relative .ytd@entry");
                    auto textures=shared_assets::load_textures(
                        *project_assets,source.substr(0,at));
                    auto chosen=std::find_if(textures.begin(),textures.end(),
                        [&](auto const& texture){
                            return texture.name==source.substr(at+1);
                        });
                    if(chosen==textures.end() || chosen->format_id!=1 ||
                       chosen->mips.empty())
                        throw std::runtime_error("YTD RGBA8 UI entry missing");
                    auto const& mip=chosen->mips.front();
                    if(!mip.width || !mip.height || mip.width>4096 ||
                       mip.height>4096 ||
                       mip.size!=static_cast<std::size_t>(mip.width)*mip.height*4 ||
                       mip.offset>chosen->data.size() ||
                       mip.size>chosen->data.size()-mip.offset)
                        throw std::runtime_error("Invalid YTD UI image mip");
                    asset.width=mip.width;
                    asset.height=mip.height;
                    asset.rgba.assign(
                        chosen->data.begin()+static_cast<std::ptrdiff_t>(mip.offset),
                        chosen->data.begin()+static_cast<std::ptrdiff_t>(mip.offset+mip.size));
                    std::fprintf(stderr,
                        "[NorthStarCpp] UI image loaded: %s (%ux%u)\n",
                        source.c_str(),mip.width,mip.height);
                } catch(std::exception const& error) {
                    warn_asset("UI image",source,error);
                }
                found=ui_image_cache.emplace(source,std::move(asset)).first;
            }
            auto const& asset=found->second;
            if(asset.rgba.empty() || !asset.width || !asset.height)continue;
            auto handle=ui_handles.find(node.id);
            if(handle==ui_handles.end())continue;
            NorthStarUIRect rect{};
            if(ui_raster->get_rect(ui_context,handle->second,&rect)!=NORTHSTAR_UI_OK)
                continue;
            const int srcW=static_cast<int>(asset.width);
            const int srcH=static_cast<int>(asset.height);
            int bx=static_cast<int>(rect.x),by=static_cast<int>(rect.y);
            int bw=std::max(1,static_cast<int>(rect.width));
            int bh=std::max(1,static_cast<int>(rect.height));
            int cropW=srcW,cropH=srcH,offX=0,offY=0;
            if(getString("fit")=="contain") {
                // Preserve aspect ratio and transparency, especially logos.
                if(static_cast<std::int64_t>(bw)*srcH <
                   static_cast<std::int64_t>(bh)*srcW)
                    bh=std::max(1,bw*srcH/srcW);
                else bw=std::max(1,bh*srcW/srcH);
                bx+= (static_cast<int>(rect.width)-bw)/2;
                by+= (static_cast<int>(rect.height)-bh)/2;
            } else {
                // CSS cover: crop from the centre, never resample on disk.
                if(static_cast<std::int64_t>(bw)*srcH >
                   static_cast<std::int64_t>(bh)*srcW) {
                    cropH=std::max(1,srcW*bh/bw);
                    offY=(srcH-cropH)/2;
                } else {
                    cropW=std::max(1,srcH*bw/bh);
                    offX=(srcW-cropW)/2;
                }
            }
            const bool underlay=getString("layer")!="foreground";
            for(int y=std::max(0,by);
                y<std::min(static_cast<int>(height),by+bh);++y) {
                auto sy=std::clamp(offY+(y-by)*cropH/bh,0,srcH-1);
                for(int x=std::max(0,bx);
                    x<std::min(static_cast<int>(width),bx+bw);++x) {
                    auto sx=std::clamp(offX+(x-bx)*cropW/bw,0,srcW-1);
                    auto src=(static_cast<std::size_t>(sy)*srcW+sx)*4;
                    auto dst=(static_cast<std::size_t>(y)*width+x)*4;
                    unsigned alpha=asset.rgba[src+3];
                    if(!alpha)continue;
                    unsigned dstAlpha=ui_pixels[dst+3];
                    unsigned factor=underlay?((255u-dstAlpha)*alpha+127)/255:
                                               alpha;
                    for(unsigned c=0;c<3;++c) {
                        unsigned contribution=(unsigned(asset.rgba[src+c])*factor+127)/255;
                        unsigned existing=underlay?ui_pixels[dst+c]:
                            (unsigned(ui_pixels[dst+c])*(255u-alpha)+127)/255;
                        ui_pixels[dst+c]=static_cast<std::uint8_t>(
                            std::min(255u,existing+contribution));
                    }
                    ui_pixels[dst+3]=underlay?
                        static_cast<std::uint8_t>(std::min(255u,dstAlpha+factor)):
                        static_cast<std::uint8_t>(std::min(255u,
                            alpha+(dstAlpha*(255u-alpha)+127)/255));
                }
            }
        }
        if(!ui_render_api->set_rgba(renderer,ui_pixels.data(),
                width,layered?height*2:height,width*4))
            throw module->error("Cannot upload GPU UI document");
        ui_last_width=width;
        ui_last_height=height;
        ui_active=true;
        ui_dirty=false;
    }
    #include "viewport_world_ui_raster.inc"
    void fallback_to_d3d11() {
        if (module && module->provider()!="d3d12") return;
        std::array<char,8> selected{};
        if(GetEnvironmentVariableA("NORTHSTARCPP_RENDER_PROVIDER",
             selected.data(),static_cast<DWORD>(selected.size()))>0)
            throw std::runtime_error(
                "Selected renderer lacks character support; explicit provider fallback disabled");
        auto next_module=std::make_unique<Module>(NORTHSTARCPP_CAPABILITY_RENDER,"d3d11");
        require_provider_render_features(*next_module,
            renderer_requirements_from_environment());
        auto next_api=next_module->query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
        // DXGI only permits one active flip-model swapchain per HWND.
        // Release the D3D12 swapchain before creating the replacement.
        if(renderer)api->destroy(renderer);
        renderer=nullptr;
        auto next_renderer=next_api->create(window_handle,software_renderer?1u:0u);
        if(!next_renderer)throw next_module->error("Cannot initialize D3D11 fallback");
        renderer=next_renderer;api=next_api;module=std::move(next_module);
        ui_render_api=nullptr;
        ui_effects_api=nullptr;
        ui_shaders_api=nullptr;
        ui_project_shaders_loaded=false;
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
        shader_reload_api=module->query_extension<NorthStarRenderShaderReloadApi>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI);
        shader_reload_v2=module->query_extension<NorthStarRenderShaderReloadApiV2>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI);
        postfx_api=module->query_extension<NorthStarRenderPostFxApi>(
            NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI);
        timing_api=module->query_extension<NorthStarRenderTimingApi>(
            NORTHSTARCPP_RENDER_TIMING_MODULE_ABI);
        skinned_material_api=module->query_extension<NorthStarRenderSkinnedMaterialsApi>(
            NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI);
        lights_api=module->query_extension<NorthStarRenderLightsApi>(
            NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI);
        editor_grid_api=module->query_extension<NorthStarRenderEditorGridApi>(
            NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI);
        pick_api=module->query_extension<NorthStarRenderPickApi>(
            NORTHSTARCPP_RENDER_PICK_MODULE_ABI);
        pick_v2_api=module->query_extension<NorthStarRenderPickApiV2>(
            NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI);
        pick_async_api=module->query_extension<NorthStarRenderPickAsyncApi>(
            NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI);
    }
    HWND window_handle=nullptr;
    bool software_renderer=false;
    std::unique_ptr<Module> module;
    NorthStarRenderApi const* api = nullptr;
    NorthStarRenderUiApi const* ui_render_api = nullptr;
    NorthStarRenderUiEffectsApi const* ui_effects_api = nullptr;
    NorthStarRenderUiShadersApi const* ui_shaders_api = nullptr;
    bool ui_project_shaders_loaded=false;
    std::unique_ptr<Module> ui_module;
    NorthStarUIApi const* ui_api = nullptr;
    NorthStarUIRasterApi const* ui_raster = nullptr;
    NorthStarUIRasterLayersApi const* ui_raster_layers = nullptr;
    NorthStarUIInputApi const* ui_input_api = nullptr;
    void* ui_context = nullptr;
    std::vector<std::uint8_t> ui_pixels;
    struct UiCachedImage {
        std::vector<std::uint8_t> rgba;
        std::uint32_t width=0,height=0;
    };
    std::unordered_map<std::string,UiCachedImage> ui_image_cache;
    UiDocument ui_document{};
    UiDocument ui_applied_document{};
    std::unordered_map<std::string,WorldUiLayer> world_ui;
    std::unordered_map<std::string,NorthStarUIHandle> ui_handles;
    std::unordered_map<NorthStarUIHandle,std::string> ui_names_by_handle;
    std::vector<UiInteractionEvent> ui_events;
    std::uint32_t ui_last_width=0,ui_last_height=0;
    bool ui_dirty=true,ui_active=false,ui_was_hovering=false;
    std::string ui_fx_id;
    std::string ui_fx_target_id;
    NorthStarUIRect ui_fx_rect{};
    bool ui_fx_hover=false,ui_fx_primary=false,ui_fx_pressed=false;
    bool ui_fx_press_burst=false;
    std::chrono::steady_clock::time_point ui_fx_press_changed=
        std::chrono::steady_clock::now();
    float ui_fx_origin_slot=0.f,ui_fx_seed=0.f;
    float ui_fx_pointer_x=0.5f,ui_fx_pointer_y=0.5f;
    std::string ui_raster_hover_id;
    std::chrono::steady_clock::time_point ui_fx_changed=
        std::chrono::steady_clock::now();
    // Universal GPU-UI timebase. Only the project shader decides its style;
    // the default engine compositor remains entirely neutral.
    std::chrono::steady_clock::time_point ui_fx_clock_start=
        std::chrono::steady_clock::now();

    std::vector<NorthStarRenderCube> cubes;
    ViewportAssetStats asset_stats;
    std::string detail_level="cinematic";
    std::vector<std::uint32_t> surface_ids;
    NorthStarRenderSurfacesApi const* surface_api = nullptr;
    NorthStarRenderShaderReloadApi const* shader_reload_api=nullptr;
    NorthStarRenderShaderReloadApiV2 const* shader_reload_v2=nullptr;
    NorthStarRenderPostFxApi const* postfx_api=nullptr;
    NorthStarRenderTimingApi const* timing_api=nullptr;
    std::unordered_map<std::string,std::uint64_t> reload_pending;
    NorthStarRenderMaterialsApi const* material_api = nullptr;
    NorthStarRenderMaterialsApiV2 const* material_api_v2=nullptr;
    NorthStarRenderSkinnedMaterialsApi const* skinned_material_api=nullptr;
    NorthStarRenderLightsApi const* lights_api=nullptr;
    NorthStarRenderEditorGridApi const* editor_grid_api=nullptr;
    NorthStarRenderPickApi const* pick_api=nullptr;
    NorthStarRenderPickApiV2 const* pick_v2_api=nullptr;
    NorthStarRenderPickAsyncApi const* pick_async_api=nullptr;
    std::uint64_t pick_generation=0, next_pick_request=1;
    std::unordered_map<std::uint64_t,int> pick_entity_actor;
    bool pick_scene_complete=false;
    std::vector<int> cube_actor_map;
    bool warned_missing_light_provider=false;
    std::unordered_map<std::string,MaterialDefinition> cached_materials;
    // Failed resources are quarantined for this VFS mount, not retried in
    // every frame. Changing mounted asset sources clears the quarantine.
    std::unordered_set<std::string> invalid_materials;
    std::unordered_set<std::string> invalid_meshes;
    std::unordered_set<std::uint64_t> warned_unbound_players;
    void warn_asset(char const* type,std::string const& path,
                    std::exception const& error) const {
        std::fprintf(stderr,"[WARN] [engine.assets] Skipping invalid %s '%s': %s\n",
            type,path.c_str(),error.what());
    }
    std::optional<shared_assets::DrawableResources> retained_sky;
    bool world_surfaces_ready = false;
    NorthStarRenderCharacterApi const* character_api = nullptr;
    std::unique_ptr<Character> character;
    bool character_arm_mode=false;
    std::unique_ptr<AssetStore> project_assets;
    std::filesystem::path source_project_root;
    std::unordered_map<std::string,MeshAsset> cached_meshes;
    // Reuse original YDD eight-weight influences, YMT bind skeleton and
    // decoded YCD clips. GPU providers receive a CPU-deformed PBR mesh.
    struct NativeRig {
        Skeleton skeleton;
        std::vector<SkinWeights> weights;
        math::float4x4 mesh_to_skeleton{};
        // Bounded one-time mask for optional arm-only native viewmodels.
        std::vector<std::uint8_t> first_person_arm_vertices;
        std::unordered_map<std::string,SkeletalClip> clips;
        std::unordered_set<std::string> invalid_clips;
        std::optional<viewport_detail::PreparedSkinSections> gpu_sections;
        std::optional<viewport_detail::PreparedSkinSections> gpu_arm_sections;
    };
    std::unordered_map<std::string,NativeRig> cached_native_skins;
    struct NativePlayback {
        std::string clip_reference;
        std::string mesh_source;
        float seconds=0.f;
        std::unique_ptr<SkeletonPose> pose;
        MeshAsset cpu_mesh; // Persistent, per-actor scratch with cached topology.
    };
    std::unordered_map<std::uint64_t,NativePlayback> native_playback;
    std::unordered_set<std::uint64_t> reported_socket_attachments;
    std::unordered_set<std::uint64_t> reported_grip_attachments;
    std::chrono::steady_clock::time_point last_native_frame{};
    std::unordered_map<std::string,GeometryData> cached_geometry;
    std::unique_ptr<GeometryService> geometry_service;
    std::vector<NorthStarRenderTriangleVertex> mesh_vertices;
    viewport_detail::SkinFrameBatches skin_batches;
    viewport_detail::SkeletalSceneFrame skeletal_frame;
    std::filesystem::path skin_cache_dir=viewport_detail::skin_cache_directory();
    NorthStarRenderTrianglesApi const* triangle_api = nullptr;
    void* renderer = nullptr;
};

Viewport::Viewport(HWND window, bool software) : state(std::make_unique<State>(window, software)) {}
Viewport::~Viewport() = default;

void Viewport::set_detail_level(std::string_view value) {
    if(value!="balanced" && value!="high" && value!="ultra" &&
       value!="cinematic")
        throw std::invalid_argument("Unknown rendering detail level");
    state->detail_level=std::string(value);
    std::fprintf(stderr,"[INFO] [engine.render] detail_level=%s full_mesh_resolution=true\n",
                 state->detail_level.c_str());
}

std::string Viewport::detail_level() const {
    return state->detail_level;
}

std::string Viewport::renderer_provider_name() const
{
    return state->module->provider();
}

std::string Viewport::renderer_provider_version() const
{
    return state->module->version();
}

bool Viewport::supports_async_gpu_pick() const noexcept {
    return state && state->renderer && state->pick_async_api &&
           state->pick_v2_api;
}
std::optional<std::uint64_t> Viewport::enqueue_actor_gpu_pick(
    std::uint32_t x,std::uint32_t y) const {
    if(!supports_async_gpu_pick() || !state->pick_scene_complete)
        return std::nullopt;
    auto const id=state->next_pick_request++;
    NorthStarRenderPickAsyncRequest request{};
    request.request_id=id;
    request.scene_generation=state->pick_generation;
    request.x=x;request.y=y;
    if(!state->pick_async_api->enqueue(state->renderer,&request))
        return std::nullopt; // bounded queue busy or GPU device lost
    return id;
}
std::optional<Viewport::PickResult> Viewport::poll_actor_gpu_pick(
    std::uint64_t requestId) const {
    if(!supports_async_gpu_pick())return std::nullopt;
    NorthStarRenderPickAsyncResult result{};
    if(!state->pick_async_api->poll(state->renderer,requestId,&result))
        return std::nullopt;
    if(result.status==NORTHSTARCPP_PICK_PENDING)
        return PickResult{PickStatus::Pending,-1};
    if(result.status==NORTHSTARCPP_PICK_STALE ||
       result.scene_generation!=state->pick_generation)
        return PickResult{PickStatus::Stale,-1};
    if(result.status!=NORTHSTARCPP_PICK_READY)
        return PickResult{PickStatus::Unknown,-1};
    if(!result.entity_id)return PickResult{PickStatus::Ready,-1};
    if(auto it=state->pick_entity_actor.find(result.entity_id);
       it!=state->pick_entity_actor.end())
        return PickResult{PickStatus::Ready,it->second};
    return PickResult{PickStatus::Stale,-1}; // entity removed during GPU work
}
std::optional<int> Viewport::pick_actor_gpu(
    std::uint32_t x,std::uint32_t y) const {
    if(!state || !state->renderer)return std::nullopt;
    if(state->pick_v2_api && state->pick_scene_complete) {
        std::uint64_t entity_id=0;
        if(!state->pick_v2_api->read_entity_id(state->renderer,x,y,&entity_id))
            return std::nullopt;
        if(!entity_id)return -1;
        if(auto found=state->pick_entity_actor.find(entity_id);
           found!=state->pick_entity_actor.end())
            return found->second;
        return std::nullopt; // ID stale after scene mutation, use raycast
    }
    if(!state->pick_api)return std::nullopt;
    // The first ID implementation covers only cube primitives. Material
    // meshes/characters must use the exact geometry ray cast until providers
    // implement per-mesh object IDs, otherwise hidden cubes could be selected.
    auto const& stats=state->asset_stats;
    if(stats.native_meshes || stats.material_batches ||
       stats.textured_material_batches || stats.invalid_meshes)
        return std::nullopt;
    std::int32_t draw_index=-1;
    if(!state->pick_api->read_cube_id(state->renderer,x,y,&draw_index))
        return std::nullopt;
    if(draw_index<0)return -1;
    if(static_cast<std::size_t>(draw_index)>=state->cube_actor_map.size())
        return std::nullopt;
    return state->cube_actor_map[static_cast<std::size_t>(draw_index)];
}
ViewportAssetStats Viewport::asset_stats() const noexcept
{
    return state->asset_stats;
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
    actor->appearance=load_character_appearance(assets,actor->model,
        spec.material,spec.textures);
    // Prepare triangle-only arm geometry once. The first-person camera must
    // not see the head/torso; preserve all original skinned vertex ordering
    // because the RenderCharacter ABI updates a contiguous pose array.
    actor->arm_indices.reserve(actor->model.parts.size());
    std::vector<bool> arm_joint(actor->skeleton.joints.size(),false);
    for(std::size_t i=0;i<arm_joint.size();++i) {
        int ancestor=static_cast<int>(i);
        for(std::size_t depth=0;ancestor>=0 &&
            depth<arm_joint.size();++depth) {
            auto const& joint=actor->skeleton.joints[ancestor];
            auto const& n=joint.name;
            if(n.rfind("l_clavicle",0)==0 ||
               n.rfind("r_clavicle",0)==0) {
                arm_joint[i]=true;break;
            }
            ancestor=joint.parent;
        }
    }
    std::size_t arm_triangles=0;
    for(auto const& part:actor->model.parts) {
        auto const& mesh=part.mesh;
        std::vector<bool> arm_vertex(mesh.vertices.size(),false);
        if(part.skin.size()!=mesh.vertices.size())
            throw std::runtime_error("Character skin weights mismatch");
        for(std::size_t index=0;index<part.skin.size();++index) {
            float total=0;
            auto const& influence=part.skin[index];
            for(unsigned j=0;j<8;++j)
                if(influence.joints[j]<arm_joint.size() &&
                   arm_joint[influence.joints[j]])
                    total+=influence.weights[j];
            arm_vertex[index]=total>=0.48f;
        }
        auto& subset=actor->arm_indices.emplace_back();
        for(std::size_t idx=0;idx+2<mesh.indices.size();idx+=3) {
            auto const a=mesh.indices[idx],b=mesh.indices[idx+1],
                       c=mesh.indices[idx+2];
            if(a<arm_vertex.size() && b<arm_vertex.size() &&
               c<arm_vertex.size() &&
               arm_vertex[a] && arm_vertex[b] && arm_vertex[c]) {
                subset.push_back(a);subset.push_back(b);subset.push_back(c);
                ++arm_triangles;
            }
        }
        if(subset.empty())
            subset={0,0,0}; // Required by RenderCharacter ABI; degenerate.
    }
    if(!arm_triangles)
        std::fprintf(stderr,
            "[WARN] [engine.character] No skinned arm triangles; viewmodel unavailable\n");
    else std::fprintf(stderr,
        "[INFO] [engine.character] Native first-person skinned arm triangles=%zu\n",
        arm_triangles);
    s.character=std::move(actor);
    s.character_arm_mode=true; // force initial upload of third-person indices
    set_character_viewmodel(false);
}

void Viewport::set_character_viewmodel(bool first_person_arms) {
    auto& s=*state;
    if(!s.character || !s.character_api ||
       s.character_arm_mode==first_person_arms)return;
    auto const& actor=*s.character;
    std::vector<std::vector<NorthStarSurfaceMip>> mips(actor.appearance.size());
    std::vector<std::vector<NorthStarCharacterVertex>> initial(actor.appearance.size());
    std::vector<NorthStarCharacterPart> parts;
    parts.reserve(actor.appearance.size());
    for(std::size_t i=0;i<actor.appearance.size();++i) {
        auto const& mesh=actor.model.parts[i].mesh;
        auto const& image=actor.appearance[i].texture;
        for(auto const& mip:image.mips)
            mips[i].push_back({mip.width,mip.height,
                static_cast<std::uint32_t>(mip.offset),
                static_cast<std::uint32_t>(mip.size)});
        initial[i].reserve(mesh.vertices.size());
        for(auto const& v:mesh.vertices)
            initial[i].push_back({{v.position[0],v.position[1],v.position[2]},
                {v.normal[0],v.normal[1],v.normal[2]},{v.uv[0],v.uv[1]}});
        auto const& indices=first_person_arms?actor.arm_indices[i]:mesh.indices;
        parts.push_back({initial[i].data(),
            static_cast<std::uint32_t>(mesh.vertices.size()),
            indices.data(),static_cast<std::uint32_t>(indices.size()),
            {0,image.format_id,image.srgb?1u:0u,
             static_cast<std::uint32_t>(mips[i].size()),
             mips[i].data(),image.data.data(),
             static_cast<std::uint32_t>(image.data.size())},
             actor.appearance[i].cutoff});
    }
    if(!s.character_api->set_model(s.renderer,parts.data(),
        static_cast<std::uint32_t>(parts.size())))
        throw s.module->error("Cannot upload skinned character/viewmodel geometry");
    s.character_arm_mode=first_person_arms;
}

void Viewport::pose_character(Vector3 foot, float heading, float dt,
                              std::string const& animation,
                              bool first_person_arms,Vector3 eye,float pitch)
{
    auto& s = *state;
    if (!s.character) return;
    set_character_viewmodel(first_person_arms);
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
    // FPS arms follow the active camera (including look pitch) rather than
    // the world Pawn body. This hides the head/torso without an extra render
    // pass and keeps the original animated hand/forearm vertex weights.
    auto const anchor=first_person_arms?
        Vector3{eye.x+std::sin(heading)*std::cos(pitch)*0.47f,
                eye.y-1.37f+std::sin(pitch)*0.47f,
                eye.z+std::cos(heading)*std::cos(pitch)*0.47f}:foot;
    auto const to_world = actor.skeleton_to_mesh * create_rotation_y(-heading) *
        (first_person_arms?create_rotation_x(-pitch):create_rotation_x(0.f)) *
        create_translation({anchor.x,anchor.y,anchor.z});
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

void Viewport::set_ui_document(UiDocument document) {
    if(document.target=="world") {
        if(document.nodes.empty()) {
            state->world_ui.erase(document.entity_id);
            return;
        }
        auto found=state->world_ui.find(document.entity_id);
        if(found==state->world_ui.end()) {
            if(state->world_ui.size()>=16)
                throw std::length_error("World UI limited to 16 entity surfaces");
            found=state->world_ui.try_emplace(document.entity_id).first;
        }
        if(!(found->second.document==document)) {
            found->second.document=std::move(document);
            found->second.dirty=true;
        }
        return;
    }
    if(!(state->ui_document==document)) {
        state->ui_document=std::move(document);
        state->ui_dirty=true;
        state->ui_fx_id.clear();
        state->ui_fx_target_id.clear();
        state->ui_fx_rect={};
        state->ui_fx_hover=false;
        state->ui_fx_pressed=false;
        state->ui_fx_press_burst=false;
        state->ui_raster_hover_id.clear();
    }
}

bool Viewport::ui_hit_test(float x,float y) const {
    const auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_raster||
       !std::isfinite(x)||!std::isfinite(y))return false;
    // Only interactive nodes intercept gameplay input. Panels/text do not
    // block camera movement, shooting or world-space picking.
    for(const auto& node:s.ui_applied_document.nodes) {
        if(node.kind!=NORTHSTAR_UI_BUTTON && node.kind!=NORTHSTAR_UI_INPUT)
            continue;
        auto handle=s.ui_handles.find(node.id);
        if(handle==s.ui_handles.end())continue;
        NorthStarUIRect rect{};
        if(s.ui_raster->get_rect(s.ui_context,handle->second,&rect)!=NORTHSTAR_UI_OK)
            continue;
        if(x>=rect.x && y>=rect.y && x<rect.x+rect.width &&
           y<rect.y+rect.height)return true;
    }
    return false;
}

bool Viewport::ui_pointer(float x,float y,std::uint32_t phase) {
    auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_raster)return false;
    const bool handled=ui_hit_test(x,y);
    if(s.ui_raster->pointer(s.ui_context,x,y,phase)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI pointer dispatch failed");
    // Pick the topmost interactive element. Only project-authored neon
    // buttons receive GPU shading; all other UI retains normal rasterization.
    std::string hoveredFx;
    std::string hoveredId;
    NorthStarUIRect hoveredRect{};
    bool hoveredPrimary=false;
    float hoveredOrigin=-1.f;
    for(auto it=s.ui_applied_document.nodes.rbegin();
        it!=s.ui_applied_document.nodes.rend();++it) {
        if(it->kind!=NORTHSTAR_UI_BUTTON &&
           it->kind!=NORTHSTAR_UI_INPUT)continue;
        auto handle=s.ui_handles.find(it->id);
        if(handle==s.ui_handles.end())continue;
        NorthStarUIRect rect{};
        if(s.ui_raster->get_rect(s.ui_context,handle->second,&rect)!=NORTHSTAR_UI_OK)
            continue;
        if(x<rect.x || y<rect.y || x>=rect.x+rect.width ||
           y>=rect.y+rect.height)continue;
        hoveredId=it->id;
        if(it->kind==NORTHSTAR_UI_BUTTON) {
            for(auto const& [key,value]:it->properties) {
                if(key=="style" && value.kind==NORTHSTAR_UI_VALUE_STRING &&
                   (value.text=="neon" || value.text=="cyberpunk")) hoveredFx=it->id;
                if(key=="primary" && value.kind==NORTHSTAR_UI_VALUE_BOOL)
                    hoveredPrimary=value.boolean;
                if(key=="fx_origin" && value.kind==NORTHSTAR_UI_VALUE_NUMBER &&
                   std::isfinite(value.number))
                    hoveredOrigin=std::clamp(static_cast<float>(value.number),
                                              0.f,11.f);
            }
            hoveredRect=rect;
        }
        break;
    }
    const bool visualHoverChanged=hoveredId!=s.ui_raster_hover_id;
    s.ui_raster_hover_id=hoveredId;
    if(hoveredFx!=s.ui_fx_id) {
        s.ui_fx_id=hoveredFx;
        s.ui_fx_hover=!hoveredFx.empty();
        s.ui_fx_changed=std::chrono::steady_clock::now();
        s.ui_fx_pressed=false;
        s.ui_fx_press_burst=false;
        if(s.ui_fx_hover) {
            s.ui_fx_target_id=hoveredFx;
            s.ui_fx_rect=hoveredRect;
            s.ui_fx_primary=hoveredPrimary;
            // FNV-1a is stable across platforms and runs. Variants are
            // assigned by the project; seed decorrelates timing per button.
            std::uint32_t hash=2166136261u;
            for(unsigned char c:hoveredFx)hash=(hash^c)*16777619u;
            s.ui_fx_seed=float(hash%100000u)/100000.f;
            s.ui_fx_origin_slot=hoveredOrigin>=0.f?
                hoveredOrigin:float(hash%12u);
        }
    }
    if(s.ui_fx_hover && hoveredRect.width>0 && hoveredRect.height>0) {
        s.ui_fx_pointer_x=std::clamp((x-hoveredRect.x)/hoveredRect.width,0.f,1.f);
        s.ui_fx_pointer_y=std::clamp((y-hoveredRect.y)/hoveredRect.height,0.f,1.f);
    }
    if(phase==1 && s.ui_fx_hover) {
        s.ui_fx_pressed=true;
        s.ui_fx_press_burst=true;
        s.ui_fx_press_changed=std::chrono::steady_clock::now();
    } else if(phase==0) {
        if(s.ui_fx_pressed && s.ui_fx_hover) {
            s.ui_fx_press_burst=true;
            s.ui_fx_press_changed=std::chrono::steady_clock::now();
        }
        s.ui_fx_pressed=false;
    }
    // Refresh hover/pressed pixels when the pointer touches an element,
    // leaves the previous one, or changes mouse-button phase. Moving in
    // empty space should not reupload the entire screen-sized UI texture.
    if(visualHoverChanged || phase!=2)
        s.ui_dirty=true;
    s.ui_was_hovering=handled;
    if(s.ui_api->pump_events(s.ui_context)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI event pump failed");
    return handled;
}

bool Viewport::ui_key(std::uint32_t key,bool down,std::string_view utf8) {
    auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_input_api)return false;
    NorthStarUIHandle focused=0;
    if(s.ui_input_api->focused(s.ui_context,&focused)!=NORTHSTAR_UI_OK ||
       !focused)return false;
    if(s.ui_input_api->keyboard(s.ui_context,key,down?1u:0u,ui::string(utf8))
       !=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI keyboard dispatch failed");
    if(s.ui_api->pump_events(s.ui_context)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI event pump failed");
    s.ui_dirty=true;
    return true;
}

bool Viewport::ui_wheel(float dx,float dy) {
    auto& s=*state;
    if(!s.ui_active||!s.ui_context||!s.ui_input_api)return false;
    if(s.ui_input_api->wheel(s.ui_context,dx,dy)!=NORTHSTAR_UI_OK)
        throw s.ui_module->error("UI wheel dispatch failed");
    s.ui_dirty=true;
    return true;
}

std::vector<UiInteractionEvent> Viewport::take_ui_events() {
    auto output=std::move(state->ui_events);
    state->ui_events.clear();
    return output;
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
    state->character_arm_mode=false;
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
                auto const candidate=dir/"engine/northstarcpp/resources/sharedAssets";
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
        assets->mount_runtime_archives();
        if (!common.empty()) assets->mount_shared_assets(common);
        assets->mount_archive("",archive,archive_data.empty()?archive:archive_data);
    } else if (!root.empty()) {
        assets->mount_project_assets(root,common);
    }
    state->project_assets=std::move(assets);
    // Never keep the previous project's shader when mounting a new project.
    if(state->renderer && state->ui_shaders_api) {
        if(state->ui_shaders_api->set_hlsl)
            state->ui_shaders_api->set_hlsl(state->renderer,nullptr,0);
        if(state->ui_shaders_api->set_spirv)
            state->ui_shaders_api->set_spirv(state->renderer,nullptr,0);
    }
    state->ui_project_shaders_loaded=false;
    state->ui_image_cache.clear();
    state->ui_dirty=true;
    state->source_project_root=archive.empty() ? std::filesystem::absolute(root) :
        std::filesystem::path{};
    state->cached_meshes.clear();
    state->warned_unbound_players.clear();
    state->cached_native_skins.clear();
    state->native_playback.clear();
    state->reported_socket_attachments.clear();
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
    auto& skeletal_frame=state->skeletal_frame;
    diagnostics::FrameProfileScope::measure_current(diagnostics::FrameStage::animation,
        [&] { skeletal_frame.prepare(scene); });
    if(skeletal_frame.has_cycles)
        std::fprintf(stderr,"[WARN] [engine.scene] Cyclic bone attachment ignored\n");
    auto const& animation_bindings=skeletal_frame.animations;
    struct FirstPersonView {
        float forward=0.55f, drop=0.98f, arm_pitch=55.f;
    };
    std::unordered_map<std::uint64_t,FirstPersonView> fps_visuals;

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
    // Presentation-only. The same persistent Entity/YDD/YMT/YCD is
    // rendered using its arm-weighted triangles when the camera enters FPS.
    // Never mutate the world scene, entity identity, or animation playback.
    if(camera_pose && !gizmo)
        for(auto const& component:scene.components) {
            if(component.type!="engine.model.visual_mode" ||
               component.version!=1 || !component.entity_id ||
               component.payload.size()>128)continue;
            std::istringstream input(component.payload);
            std::string mode,extra;
            FirstPersonView view;
            if(!(input>>mode>>view.forward>>view.drop>>view.arm_pitch) ||
               mode!="first_person_arms" ||
               (input>>extra) ||
               !std::isfinite(view.forward) || view.forward<0.f ||
               view.forward>3.f || !std::isfinite(view.drop) ||
               view.drop<0.f || view.drop>3.f ||
               !std::isfinite(view.arm_pitch) ||
               std::abs(view.arm_pitch)>180.f)continue;
            fps_visuals[component.entity_id]=view;
        }
    for(auto it=state->native_playback.begin();
        it!=state->native_playback.end();) {
        if(!animation_bindings.count(it->first))
            it=state->native_playback.erase(it);
        else ++it;
    }
    state->asset_stats={};
    // A camera/scene redraw makes old GPU picking snapshots obsolete.
    // Invalidate logically; provider keeps in-flight resources until its fence.
    ++state->pick_generation;
    if(state->pick_async_api &&
       !state->pick_async_api->invalidate_before(
           state->renderer,state->pick_generation))
        throw state->module->error("Cannot invalidate stale GPU pick requests");
    state->pick_entity_actor.clear();
    state->pick_scene_complete=false;
    auto& cubes = state->cubes;
    cubes.clear();
    state->cube_actor_map.clear();
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
    bool const collect_pick=state->pick_v2_api && gizmo && camera_pose && !fps;
    bool pick_complete=true;
    std::vector<NorthStarRenderPickVertex> pick_triangles;
    std::vector<std::uint64_t> pick_cube_entities;
    std::uint64_t current_pick_entity=0;
    auto add_pick=[&](Vector3 const& position) {
        if(!collect_pick || !current_pick_entity || !pick_complete)return;
        if(pick_triangles.size()>=180000u) {pick_complete=false;return;}
        pick_triangles.push_back({position.x,position.y,position.z,0,
                                  current_pick_entity});
    };
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
    auto& skin_batches=state->skin_batches;
    skin_batches.begin_frame();
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
        add_pick(position);
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
    std::unordered_set<std::uint64_t> character_entities;
    if(gizmo) for(auto const& c:scene.components)
        if(c.type==player_character::pawn_type && c.entity_id)
            character_entities.insert(c.entity_id);
    // A visualized Pawn must not render a solid placeholder capsule on top of
    // its own SkeletalMesh. Its collider is drawn separately by the collision
    // debug overlay, and its child visual owns the render/picking triangles.
    std::unordered_set<std::uint64_t> visualized_pawns;
    if(gizmo && !character_entities.empty()) {
        std::unordered_map<std::uint64_t,std::uint64_t> parents;
        parents.reserve(scene.objects.size());
        for(auto const& object:scene.objects)
            parents.emplace(object.node.id,object.node.parent);
        for(auto const& object:scene.objects) {
            if(!object.node.visible || object.node.asset.empty() ||
               state->invalid_meshes.count(object.node.asset))continue;
            auto parent=object.node.parent;
            for(std::size_t depth=0;parent && depth<scene.objects.size();++depth) {
                if(character_entities.count(parent)) {
                    visualized_pawns.insert(parent);
                    break;
                }
                auto it=parents.find(parent);
                if(it==parents.end() || it->second==parent)break;
                parent=it->second;
            }
        }
    }

    for (auto const i : skeletal_frame.order) {
        // Keep the selected editor entity editable even when HLOD substitutes
        // its authored draw; the proxy is still allowed behind selection.
        if (streaming && i<authored_count && !scene_world->object_visible(i) &&
            (selected<0 || i!=static_cast<std::size_t>(selected)))
            continue;
        auto const& object = scene.objects[i];
        current_pick_entity=object.node.id;
        if(collect_pick && current_pick_entity &&
           !state->pick_entity_actor.emplace(current_pick_entity,
               static_cast<int>(i)).second)
            pick_complete=false; // duplicated stable ECS identity
        // Visibility is an authored actor property, not only a runtime-spawn
        // flag. In particular, the Editor Outliner's eye must hide original
        // NSCN objects as well as newly spawned ones.
        if (!object.node.visible) continue;
        // Detail controls native asset draw distance, never mesh reduction.
        // Cinematic renders the original triangles at all authored distances.
        if(camera_pose && !gizmo && state->detail_level!="cinematic" &&
           !scene_pawn::is_local(scene,object)) {
            auto const& asset=object.node.asset;
            auto const path=asset.substr(0,asset.find('@'));
            bool const native=path.size()>=4 &&
                (path.compare(path.size()-4,4,".ydr")==0 ||
                 path.compare(path.size()-4,4,".ydd")==0);
            if(native) {
                float const max_distance=state->detail_level=="balanced"?180.f:
                    state->detail_level=="high"?480.f:1200.f;
                auto const dx=object.position.x-camera_pose->position.x;
                auto const dy=object.position.y-camera_pose->position.y;
                auto const dz=object.position.z-camera_pose->position.z;
                if(dx*dx+dy*dy+dz*dz>max_distance*max_distance)continue;
            }
        }
        if ((fps || (camera_pose && selected==-2)) && scene_pawn::is_local(scene,object)) continue;
        Vector3 position = object.position;
        auto const view=fps_visuals.find(object.node.id);
        bool const fps_arms=view!=fps_visuals.end() && camera_pose;
        if(fps_arms) {
            auto const yaw=camera_pose->yaw,pitch=camera_pose->pitch;
            auto const d=view->second.forward;
            // The camera is a separate scene/rig entity. Only the submitted
            // mesh transform follows its view; the actor remains at its Pawn.
            position={
                camera_pose->position.x+std::sin(yaw)*std::cos(pitch)*d,
                camera_pose->position.y-view->second.drop+std::sin(pitch)*d,
                camera_pose->position.z+std::cos(yaw)*std::cos(pitch)*d
            };
        }
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
            if(fps_arms) {
                // Same authored mesh and skeletal animation; only view-space
                // orientation changes. No second arms asset is loaded.
                auto const yaw=camera_pose->yaw;
                auto const pitch=view->second.arm_pitch*0.01745329251994329577f
                    -camera_pose->pitch;
                float const sx=std::sin(pitch),cx=std::cos(pitch);
                float const sy=std::sin(yaw),cy=std::cos(yaw);
                float const basis[9]={
                    cy, sy*sx, sy*cx,
                    0.f, cx, -sx,
                    -sy, cy*sx, cy*cx
                };
                std::copy(std::begin(basis),std::end(basis),
                          std::begin(cube.rotation));
            }
            float const scale[3] = {object.node.scale.x, object.node.scale.y,
                                    object.node.scale.z};
            for (unsigned row = 0; row != 3; ++row)
                for (unsigned column = 0; column != 3; ++column)
                    cube.rotation[row * 3 + column] *= scale[column];
        }
        // Child geometry remains local to its own YDD pivot; the parent hand
        // provides the complete world-space rotation and translation.
        if(auto it=skeletal_frame.attachments.find(object.node.id);
           it!=skeletal_frame.attachments.end()) {
            auto const& a=it->second;
            const auto resolved=skeletal_frame.attachment_transform(a,camera_pose);
            if(!resolved)continue;
            auto const& transform=*resolved;
            position={transform.elements[3][0],
                      transform.elements[3][1],
                      transform.elements[3][2]};
            cube.x=position.x;cube.y=position.y;cube.z=position.z;
            if(object.size<=0.f)continue;
            if(state->reported_socket_attachments.insert(object.node.id).second)
                std::fprintf(stderr,
                    "[INFO] [engine.scene] bone_attachment=PASS child=%llu parent=%llu joint=%s at=(%.3f,%.3f,%.3f) axis_z=(%.3f,%.3f,%.3f)\n",
                    static_cast<unsigned long long>(object.node.id),
                    static_cast<unsigned long long>(a.parent),
                    a.bone.c_str(),position.x,position.y,position.z,
                    transform.elements[2][0],
                    transform.elements[2][1],
                    transform.elements[2][2]);
            for(unsigned row=0;row<3;++row)
                for(unsigned col=0;col<3;++col)
                    cube.rotation[row*3+col]=
                        transform.elements[col][row]/object.size;
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
                if(static_cast<int>(i)==selected || (selected==-2 && scene_pawn::is_local(scene,object)))
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
                auto const& v=mesh_vertices.back();
                add_pick({v.x,v.y,v.z});
            }
            if(static_cast<int>(i)==selected || (selected==-2 && scene_pawn::is_local(scene,object)))
                focus=position;
            continue;
        }
        // Do not silently use an unrelated sample character for an actor
        // without an authored model. The Inspector is the source of truth
        // for player geometry, including prefab child nodes. In the editor,
        // an unbound player remains selectable in Outliner but is not a cube.
        std::string resource_path=object.node.asset;
        if(resource_path.empty() && gizmo &&
           visualized_pawns.count(object.node.id)) {
            if(static_cast<int>(i)==selected)focus=position;
            continue; // a genuine child model represents this logical Pawn
        }
        if(resource_path.empty() && gizmo &&
           character_entities.count(object.node.id) &&
           native_shape==procedural.end()) {
            auto const marker_start=mesh_vertices.size();
            if(state->triangle_api &&
               editor_character_marker::append(mesh_vertices,object,
                   static_cast<int>(i)==selected)) {
                for(auto k=marker_start;k<mesh_vertices.size();++k) {
                    auto const& v=mesh_vertices[k];
                    add_pick({v.x,v.y,v.z});
                }
                if(static_cast<int>(i)==selected)focus=position;
                continue;
            }
            // Renderer extension unavailable or triangle budget exhausted;
            // fall through to the old cube proxy rather than hiding the actor.
            if(state->warned_unbound_players.insert(object.node.id).second)
                std::fprintf(stderr,
                    "[WARN] [editor.viewport] Pawn ID %llu has no model asset; "
                    "using fallback proxy (assign a YDD/YDR/OBJ).\n",
                    static_cast<unsigned long long>(object.node.id));
            // Proceed to the fallback proxy.
        }
        auto const selector=resource_path.find('@');
        auto const extpath=resource_path.substr(0,selector);
        bool const is_scene_mesh=extpath.size()>4 &&
            (extpath.compare(extpath.size()-4,4,".obj")==0 ||
             extpath.compare(extpath.size()-4,4,".ydd")==0 ||
             extpath.compare(extpath.size()-4,4,".ydr")==0);
        // Never represent a referenced native mesh as a normal color cube:
        // a missing renderer extension is a capability error, not a model.
        if(is_scene_mesh && (!state->triangle_api || !state->project_assets))
            throw std::runtime_error(
                "Scene mesh requires engine.render triangles ABI and mounted "
                "engine.assets VFS. Check the selected provider.");
        if (state->triangle_api && state->project_assets && is_scene_mesh) {
            if(state->invalid_meshes.count(resource_path)) {
                ++state->asset_stats.invalid_meshes;
                continue;
            }
            auto found=state->cached_meshes.find(resource_path);
            if (found==state->cached_meshes.end()) {
                try {
                // Directory-backed projects must not follow symlink/junction
                // redirections when loading authored mesh assets.
                if (!state->source_project_root.empty()) {
                    auto relative=std::filesystem::u8path(extpath);
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
                    auto const t_native=std::chrono::steady_clock::now();
                    auto native=load_native_model_resource(
                        *state->project_assets,resource_path,true);
                    auto const native_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now()-t_native).count();
                    if(native_ms>100)std::fprintf(stderr,
                        "[PERF] [engine.assets] native_model %s %lld ms\n",
                        resource_path.c_str(),static_cast<long long>(native_ms));
                    if(!native.unresolved_materials.empty())
                        std::fprintf(stderr,
                            "[WARN] [engine.assets] Model '%s': %zu unresolved YMAT slots; "
                            "using neutral PBR fallbacks.\\n",
                            resource_path.c_str(),native.unresolved_materials.size());
                    for(auto& entry:native.materials)
                        state->cached_materials.try_emplace(
                            entry.first,std::move(entry.second));
                    if(native.has_skin()) {
                        State::NativeRig rig{};
                        rig.skeleton=std::move(*native.skeleton);
                        rig.weights=std::move(native.vertex_skin);
                        rig.mesh_to_skeleton=native.mesh_to_skeleton;
                        state->cached_native_skins.try_emplace(
                            resource_path,std::move(rig));
                    }
                    found=state->cached_meshes.emplace(
                        resource_path,std::move(native.mesh)).first;
                } else {
                    auto model=load_scene_mesh(
                        *state->project_assets,resource_path);
                    found=state->cached_meshes.emplace(
                        resource_path,std::move(model)).first;
                }
                } catch(std::exception const& error) {
                    ++state->asset_stats.invalid_meshes;
                    if(state->invalid_meshes.insert(resource_path).second)
                        state->warn_asset("mesh",resource_path,error);
                    continue; // Only this authored mesh is omitted.
                }
            }
            if(found->second.indices.empty()) {
                ++state->asset_stats.invalid_meshes;
                continue;
            }
            ++state->asset_stats.native_meshes;
            // First-person arms are a native scene mesh, not an unsupported
            // Character ABI or a billboard. Keep material UV and YCD skinning.
            const bool first_person_arm_mesh=fps_arms ||
                object.node.layer=="FirstPersonArms";
            auto arms_skin=state->cached_native_skins.find(resource_path);
            if(first_person_arm_mesh && arms_skin==state->cached_native_skins.end()) {
                if(state->invalid_meshes.insert(resource_path+"#arms").second)
                    std::fprintf(stderr,
                        "[WARN] [engine.assets] Arm viewmodel has no native YMT skin: %s\n",
                        resource_path.c_str());
                continue;
            }
            if(first_person_arm_mesh &&
               arms_skin->second.first_person_arm_vertices.empty()) {
                auto& mask=arms_skin->second.first_person_arm_vertices;
                auto const& rig=arms_skin->second;
                auto const& joints=rig.skeleton.joints;
                std::vector<bool> arm_joint(joints.size(),false);
                for(std::size_t joint_index=0;joint_index<joints.size();++joint_index) {
                    int ancestor=static_cast<int>(joint_index);
                    for(std::size_t depth=0;ancestor>=0 && depth<joints.size();++depth) {
                        auto const& j=joints[ancestor];
                        if(j.name=="l_elbow" || j.name=="r_elbow" ||
                           j.name=="l_elbow_helper" ||
                           j.name=="r_elbow_helper") {
                            arm_joint[joint_index]=true;break;
                        }
                        ancestor=j.parent;
                    }
                }
                // FPS rendering intentionally begins at the elbow, not at the
                // clavicle. Rendering the torso and shoulders under aggressive
                // two-hand IK created long, stretched strips across the
                // screen. Retain complete original forearm/hand triangles.
                // Only include helper-driven vertices when both driving
                // sources are distal arm bones; otherwise upper-arm/chest
                // twist weights leak back into the first-person view.
                for(std::size_t pass=0;pass<3;++pass) {
                    for(auto const& [helper,source]:rig.skeleton.mirrored_helpers)
                        if(helper<arm_joint.size() && source<arm_joint.size() &&
                           arm_joint[source])arm_joint[helper]=true;
                    for(auto const& rule:rig.skeleton.secondary_palette_constraints)
                        if(rule.bone<arm_joint.size() &&
                           rule.source_a<arm_joint.size() &&
                           rule.source_b<arm_joint.size() &&
                           (arm_joint[rule.source_a] && arm_joint[rule.source_b]))
                            arm_joint[rule.bone]=true;
                }
                mask.resize(rig.weights.size());
                std::size_t selected=0;
                for(std::size_t vertex=0;vertex<mask.size();++vertex) {
                    float w=0;
                    for(unsigned inf=0;inf<8;++inf) {
                        auto const joint=rig.weights[vertex].joints[inf];
                        if(joint<arm_joint.size() && arm_joint[joint])
                            w+=rig.weights[vertex].weights[inf];
                    }
                    // Imported Abby forearms include complex twist and finger helpers.
                    // Viewmodel masking excludes mixed shoulder/chest vertices;
                    // full model geometry and all eight weights remain intact.
                    // Simple humanoid rigs retain the ordinary soft mask.
                    const float min_arm_weight=
                        rig.skeleton.secondary_palette_constraints.size()>32?
                            .975f:.48f;
                    mask[vertex]=w>=min_arm_weight?1u:0u;
                    selected+=mask[vertex];
                }
                std::fprintf(stderr,
                    "[INFO] [engine.character] Native first-person viewmodel arm vertices=%zu/%zu\n",
                    selected,mask.size());
            }
            // Optional scene v1 component payload:
            // animations/weapon/pistol/military-pistol.ycd@p-fire
            // Only this entity animates; the shared native model is immutable.
            MeshAsset const* selected_mesh=&found->second;
            SkeletonPose const* gpu_skin_pose=nullptr;
            math::float4x4 mesh_world;
            std::optional<viewport_detail::SkinFrameBatches::Palette> gpu_skin_palette;
            auto const animation=animation_bindings.find(object.node.id);
            auto rig_it=state->cached_native_skins.find(resource_path);
            if(animation!=animation_bindings.end() &&
               rig_it!=state->cached_native_skins.end()) {
                auto& rig=rig_it->second;
                auto& play=state->native_playback[object.node.id];
                if(play.mesh_source!=resource_path) {
                    play.mesh_source=resource_path;
                    play.pose.reset();
                    play.cpu_mesh=MeshAsset{};
                }
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
                            auto const t_clip=std::chrono::steady_clock::now();
                            auto loaded=load_skeletal_clips(*state->project_assets,
                                ref.substr(0,at),{ref.substr(at+1)},rig.skeleton);
                            auto const clip_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now()-t_clip).count();
                            if(clip_ms>100)std::fprintf(stderr,
                                "[PERF] [engine.animation] ycd %s %lld ms\n",
                                ref.c_str(),static_cast<long long>(clip_ms));
                            clip=rig.clips.emplace(ref,std::move(loaded.front())).first;
                        }
                        if(!play.pose)
                            play.pose=std::make_unique<SkeletonPose>(rig.skeleton);
                        auto& pose=*play.pose;
                        mesh_world.identity();
                        for(unsigned row=0;row<3;++row)
                            for(unsigned col=0;col<3;++col)
                                mesh_world.elements[row][col]=
                                    cube.rotation[col*3+row]*object.size;
                        mesh_world.elements[3][0]=position.x;
                        mesh_world.elements[3][1]=position.y;
                        mesh_world.elements[3][2]=position.z;
                        std::optional<viewport_detail::GripView> grip;
                        if(camera_pose && skeletal_frame.wants_hand_grip(object.node.id))
                            grip=viewport_detail::GripView{camera_pose->yaw,camera_pose->pitch,fps_arms,camera_pose->position};
                        diagnostics::FrameProfileScope::measure_current(diagnostics::FrameStage::animation,[&] {
                            const viewport_detail::PoseInput input{rig.skeleton,clip->second,
                                play.seconds,mesh_world,rig.mesh_to_skeleton,grip};
                            const auto results=viewport_detail::evaluate_pose(input,pose);
                            skeletal_frame.publish_sockets(object.node.id,rig.skeleton,pose,
                                rig.mesh_to_skeleton,mesh_world);
                            for(std::size_t hand=0;hand<results.size();++hand) {
                                if(!results[hand] || !state->reported_grip_attachments.insert(
                                        object.node.id*2+hand).second)continue;
                                auto const& [target,actual,solved]=*results[hand];
                                std::fprintf(stderr,
                                    "[INFO] [engine.character] %s_grip=%s"
                                    " target=(%.3f,%.3f,%.3f) wrist=(%.3f,%.3f,%.3f)"
                                    " residual=%.3f\n",
                                    fps_arms?"fps":"tps",solved?"SOLVED":"UNSOLVED",
                                    target.x,target.y,target.z,actual.x,actual.y,actual.z,
                                    (target-actual).magnitude());
                            }
                        });

                        // GPU only for opaque materials supported by v1.
                        // The CPU path remains mandatory for custom graphs,
                        // transparent assets or external render providers.
                        bool compatible=state->skinned_material_api &&
                            rig.skeleton.joints.size()<=NORTHSTARCPP_SKIN_MAX_JOINTS &&
                            rig.weights.size()==found->second.vertices.size() &&
                            !found->second.sections.empty() &&
                            skin_batches.draw_count()+found->second.sections.size()<=
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
                            gpu_skin_pose=&pose;
                        } else {
                            skin_native_mesh_cpu_into(play.cpu_mesh,found->second,
                                rig.skeleton,rig.weights,pose.palette(),
                                first_person_arm_mesh?
                                    &rig.first_person_arm_vertices:nullptr);
                            selected_mesh=&play.cpu_mesh;
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
            auto arm_triangle=[&](std::size_t k) -> bool {
                if(!first_person_arm_mesh)return true;
                auto const& mask=arms_skin->second.first_person_arm_vertices;
                if(k+2>=mesh.indices.size())return false;
                for(unsigned v=0;v<3;++v) {
                    auto const index=mesh.indices[k+v];
                    if(index>=mask.size() || !mask[index])return false;
                }
                return true;
            };
            auto submit_range=[&](std::size_t begin,std::size_t count,
                                  std::string const& material,std::size_t section_index) {
                if(begin>mesh.indices.size() || count>mesh.indices.size()-begin ||
                   count%3!=0)
                    throw std::invalid_argument("Invalid scene mesh section range");
                if(gpu_skin_pose) {
                    auto& rig=rig_it->second;
                    auto& cached=first_person_arm_mesh?rig.gpu_arm_sections:rig.gpu_sections;
                    if(!cached) {
                        const auto provider=state->module->provider();
                        const auto version=state->module->version();
                        diagnostics::FrameProfileScope::measure_current(
                            diagnostics::FrameStage::asset_streaming,[&] {
                                auto prepared=viewport_detail::prepare_skin_sections(
                                    {mesh,rig.weights,first_person_arm_mesh?
                                         &rig.first_person_arm_vertices:nullptr,provider,version},
                                    state->skin_cache_dir);
                                if(!state->skin_cache_dir.empty())
                                    std::fprintf(stderr,"[INFO] [engine.assets] ydd_gpu_ddc=%s variant=%s %s\n",
                                        prepared.cache_hit?"hit":"miss",
                                        first_person_arm_mesh?"arms":"full",resource_path.c_str());
                                cached=std::move(prepared.sections);
                            });
                    }
                    auto const& vertices=cached->at(section_index);
                    if(vertices.empty())return; // no arm triangles in this section
                    if(!gpu_skin_palette)
                        gpu_skin_palette=skin_batches.append_palette(
                            gpu_skin_pose->palette(),mesh_world);
                    // GPU picking has a hard budget. Never do the expensive
                    // eight-weight CPU skin projection once that budget is
                    // exceeded; preserve visible GPU geometry in full.
                    if(collect_pick && pick_complete) {
                        auto const* palette=skin_batches.palette_data(*gpu_skin_palette);
                        for(auto const& point:vertices) {
                            if(pick_triangles.size()>=180000u) {
                                pick_complete=false;
                                break;
                            }
                            // The visible skin shader uses world-space palette
                            // matrices. Mirror only the position transform for
                            // depth-correct selection; materials still use GPU skin.
                            Vector3 weighted{};
                            float total=0.f;
                            for(unsigned joint=0;joint<8;++joint) {
                                float const w=point.weights[joint];
                                if(w<=0.f)continue;
                                auto const index=point.joints[joint];
                                if(index>=gpu_skin_palette->joint_count) {
                                    pick_complete=false;break;
                                }
                                auto const* m=palette+std::size_t(index)*16;
                                weighted.x+=w*(point.x*m[0]+point.y*m[4]+point.z*m[8]+m[12]);
                                weighted.y+=w*(point.x*m[1]+point.y*m[5]+point.z*m[9]+m[13]);
                                weighted.z+=w*(point.x*m[2]+point.y*m[6]+point.z*m[10]+m[14]);
                                total+=w;
                            }
                            if(total<=1.e-5f)pick_complete=false;
                            else add_pick(weighted);
                        }
                    }
                    skin_batches.add_draw(state->cached_materials.at(material),
                                          vertices,*gpu_skin_palette);
                    return;
                }
                if(!material.empty()) {
                    if(!state->material_api && !state->material_api_v2)
                        throw std::runtime_error(
                            "Native YDD/YDR requires render Materials ABI v1 or v2");
                    auto& batch=material_vertices(material);
                    for(std::size_t k=begin;k<begin+count;++k) {
                        if(!arm_triangle(begin+((k-begin)/3)*3))continue;
                        auto const& v=mesh.vertices.at(mesh.indices[k]);
                        auto const point=transformed({
                            v.position.x*object.size,v.position.y*object.size,
                            v.position.z*object.size},cube,position);
                        auto const normal=normal_direction(v.normal,cube);
                        push_material(batch,point,normal,v.u,v.v);
                    }
                } else {
                    if(mesh_vertices.size()+count>300000)
                        throw std::runtime_error("Scene triangle budget exceeded");
                    auto const color=static_cast<int>(i)==selected ?
                        0xffd474u : object.color;
                    for(std::size_t k=begin;k<begin+count;++k) {
                        if(!arm_triangle(begin+((k-begin)/3)*3))continue;
                        auto const& v=mesh.vertices.at(mesh.indices[k]);
                        auto const world=transformed({
                            v.position.x*object.size,v.position.y*object.size,
                            v.position.z*object.size},cube,position);
                        mesh_vertices.push_back({world.x,world.y,world.z,color});
                        add_pick(world);
                    }
                }
            };
            if(!mesh.sections.empty()) {
                for(std::size_t index=0;index<mesh.sections.size();++index) {
                    auto const& section=mesh.sections[index];
                    submit_range(section.first_index,section.index_count,
                        has_material ? material_it->second : section.material_reference,index);
                }
            } else {
                submit_range(0,mesh.indices.size(),
                    has_material ? material_it->second : std::string{},0);
            }
            if (static_cast<int>(i)==selected || (selected==-2 && scene_pawn::is_local(scene,object)))
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
               (selected==-2 && scene_pawn::is_local(scene,object)))focus=position;
            continue;
        }
        if (static_cast<int>(i) == selected) {
            selected_render_index = static_cast<int>(cubes.size());
            focus = position;
        } else if (selected == -2 && scene_pawn::is_local(scene,object)) {
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
        state->cube_actor_map.resize(cubes.size()+1,-1);
        state->cube_actor_map.back()=static_cast<int>(i);
        cubes.push_back(cube);
        if(collect_pick)pick_cube_entities.push_back(current_pick_entity);
        ++state->asset_stats.cube_proxies;
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

    // Authoring-only adaptive ground grid. The layout is computed by the
    // same camera-relative grid algorithm as Qt's software Viewport. It
    // follows the active camera indefinitely; no finite scene terrain
    // limits, no ECS/physics objects, and no renderer ABI changes.
    if(gizmo && gizmo->show_grid && camera_pose && !fps &&
       !state->editor_grid_api) {
        auto const layout=editor_grid::build(
            *camera_pose,gizmo->grid_width,gizmo->grid_height);
        auto const minor=std::clamp(layout.minor_step*.022f,.009f,20.f);
        for(auto const& line:layout.lines) {
            if(cubes.size()>=max_scene_objects)break;
            NorthStarRenderCube strip{};
            strip.size=1.f;
            strip.rgb=line.color;
            strip.x=(line.from.x+line.to.x)*.5f;
            // The native provider positions its 1-unit cube +size/2
            // on Y. A thin floating strip stays above the ground.
            strip.y=.012f-.5f;
            strip.z=(line.from.z+line.to.z)*.5f;
            strip.rotation[0]=std::max(minor*.75f,std::abs(line.to.x-line.from.x));
            strip.rotation[4]=std::max(.008f,minor*.25f);
            strip.rotation[8]=std::max(minor*.75f,std::abs(line.to.z-line.from.z));
            auto const thickness=line.origin?minor*2.f:
                line.major?minor*1.35f:minor;
            if(std::abs(line.to.x-line.from.x)>.01f)
                strip.rotation[8]=thickness;
            else strip.rotation[0]=thickness;
            cubes.push_back(strip);
        }
    }

    if(gizmo && gizmo->show_collisions && !fps)
        viewport_debug::append_colliders(cubes,scene,selected);

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
                constexpr int segments=56;
                for (int i=0;i<segments && cubes.size()<max_scene_objects;++i) {
                    float const t0=tau*float(i)/float(segments);
                    float const t1=tau*float(i+1)/float(segments);
                    auto point=[&](float angle) {
                        return add(center,add(mul(u,gizmo->length*.8333333f*std::cos(angle)),
                            mul(v,gizmo->length*.8333333f*std::sin(angle))));
                    };
                    segment(point(t0),point(t1),color,
                            std::max(.018f,gizmo->length*.028f));
                }
            } else {
                auto const tip=add(center,mul(direction,gizmo->length));
                segment(center,tip,color,
                    std::max(.018f,gizmo->length*.025f));
                if(gizmo->mode==0) {
                    // Four short rods form a true spatial arrowhead, rather
                    // than the old indistinguishable lines with no handles.
                    auto const seed=std::abs(direction.y)>.9f?
                        Vector3{0.f,0.f,1.f}:Vector3{0.f,1.f,0.f};
                    auto const u=normalize(cross(direction,seed));
                    auto const v=normalize(cross(direction,u));
                    auto const base=add(tip,mul(direction,-gizmo->length*.19f));
                    float const wing=gizmo->length*.095f;
                    float const thickness=std::max(.018f,gizmo->length*.025f);
                    for(auto const side:{u,mul(u,-1.f),v,mul(v,-1.f)})
                        segment(add(base,mul(side,wing)),tip,color,thickness);
                }else if (gizmo->mode==2 && cubes.size()<max_scene_objects) {
                    // Scale is a solid square cube at the end of each axis,
                    // consistently anchored along Y regardless of zoom.
                    NorthStarRenderCube handle{};
                    handle.size=std::max(.08f,gizmo->length*.19f);
                    handle.x=tip.x;
                    handle.y=tip.y-handle.size*.5f;
                    handle.z=tip.z;
                    handle.rgb=color;
                    handle.rotation[0]=handle.rotation[4]=handle.rotation[8]=1.f;
                    cubes.push_back(handle);
                }
            }
        }
    }

    // The host resolves material resources through VFS; GPU plugins only
    // see copied UV geometry, factors and bounded RGBA8 textures.
    std::vector<std::string> gpu_material_refs;
    std::vector<NorthStarMaterialBatch> gpu_materials;
    gpu_materials.reserve(material_draws.size());
    std::vector<NorthStarMaterialBatchV2> gpu_materials_v2;
    gpu_materials_v2.reserve(material_draws.size());
    bool needs_material_v2=false;
    std::size_t material_vertex_total=0;
    for(auto const& entry:material_draws) {
        auto const& reference=entry.first;
        auto const& geometry=entry.second;
        if(geometry.empty())continue;
        if(state->invalid_materials.count(reference)) {
            pick_complete=false; // rejected material must not leave pick-only geometry
            continue;
        }
        if(!state->project_assets) {
            pick_complete=false;
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
                pick_complete=false;
                if(state->invalid_materials.insert(reference).second)
                    state->warn_asset("material",reference,error);
                continue; // No placeholder material: skip only this batch.
            }
        }
        material_vertex_total+=geometry.size();
        if(material_vertex_total>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)
            throw std::runtime_error("Material geometry exceeds GPU batch budget");
        auto const& material=found->second;
        // A color-only ID pass cannot honor alpha-cutout textures or blended
        // fragments yet. Use exact geometry raycast for such scenes.
        if(material.blend_mode!=0 || material.opacity<0.999f)
            pick_complete=false;
        auto batch=viewport_detail::material_batch(material);
        batch.vertices=geometry.data();
        batch.vertex_count=static_cast<std::uint32_t>(geometry.size());
        gpu_materials.push_back(batch);
        ++state->asset_stats.material_batches;
        for(auto const& texture:material.textures)
            if(!texture.rgba8.empty()) {
                ++state->asset_stats.textured_material_batches;
                break;
            }
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
        gpu_material_refs.push_back(reference);
    }
    #include "viewport_world_ui_batches.inc"
    auto const& gpu_skinned_batches=skin_batches.build();
    state->cube_actor_map.resize(cubes.size(),-1);
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
    // Optional backend-native analytic overlay. External ABI 5 providers
    // without this capability keep the transient geometry fallback above.
    // Explicitly disable on every non-editor / hidden-grid submission so
    // provider state cannot leak between Editor and Player frames.
    if(state->editor_grid_api) {
        NorthStarRenderEditorGridSettings settings{};
        settings.abi_version=NORTHSTARCPP_RENDER_EDITOR_GRID_ABI;
        settings.enabled=(gizmo && gizmo->show_grid && camera_pose && !fps)?1u:0u;
        if(settings.enabled) {
            auto const layout=editor_grid::build(
                *camera_pose,gizmo->grid_width,gizmo->grid_height);
            settings.minor_step=layout.minor_step;
            settings.opacity=.92f;
            settings.fade_start=std::max(layout.radius*.5f,35.f);
            settings.fade_end=std::max(layout.radius*1.7f,
                                      settings.fade_start+10.f);
        } else {
            settings.minor_step=1.f;
            settings.opacity=0.f;
            settings.fade_start=1.f;
            settings.fade_end=2.f;
        }
        if(!state->editor_grid_api->set_grid(state->renderer,&settings))
            throw state->module->error("Cannot configure analytic editor grid");
    }
    auto const scene_lights=northstarcpp::render::scene_render_lights(scene);
    if(collect_pick) {
        pick_cube_entities.resize(cubes.size(),0); // debug/HLOD/gizmo are not actors
        if(pick_triangles.size()%3 ||
           pick_triangles.size()+cubes.size()*36u>200000u ||
           state->asset_stats.invalid_meshes)
            pick_complete=false;
    }
    auto submit=[&] {
        auto* counters=diagnostics::frame_memory::counters;
        const auto* memory_api=counters?state->module->query_extension<NorthStarRenderFrameMemoryApi>(
            NORTHSTARCPP_RENDER_FRAME_MEMORY_MODULE_ABI):nullptr;
        const diagnostics::frame_memory::Scope memory_scope(counters,memory_api);
        if(state->pick_v2_api) {
            // Always clear stale provider IDs, including after scene changes.
            auto const* v=collect_pick && pick_complete &&
                !pick_triangles.empty()?pick_triangles.data():nullptr;
            auto const vc=collect_pick && pick_complete?
                static_cast<std::uint32_t>(pick_triangles.size()):0u;
            auto const* ids=collect_pick && pick_complete &&
                !pick_cube_entities.empty()?pick_cube_entities.data():nullptr;
            auto const nc=collect_pick && pick_complete?
                static_cast<std::uint32_t>(pick_cube_entities.size()):0u;
            if(!state->pick_v2_api->set_world_triangles(state->renderer,v,vc) ||
               !state->pick_v2_api->set_cube_entities(state->renderer,ids,nc))
                throw state->module->error("Cannot submit entity ID picking streams");
            state->pick_scene_complete=collect_pick && pick_complete;
        }
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
        if(state->shader_reload_v2 || state->shader_reload_api) {
            std::vector<const char*> keys;
            keys.reserve(gpu_material_refs.size());
            for(auto const& key:gpu_material_refs)keys.push_back(key.c_str());
            if(!(state->shader_reload_v2?
                 state->shader_reload_v2->set_material_references(
                     state->renderer,keys.empty()?nullptr:keys.data(),
                     static_cast<std::uint32_t>(keys.size())):
                 state->shader_reload_api->set_material_references(
                state->renderer,keys.empty()?nullptr:keys.data(),
                static_cast<std::uint32_t>(keys.size()))))
                throw state->module->error("Cannot bind material keys for shader reload");
        }
        state->prepare_ui_overlay(fps!=nullptr || !state->ui_document.nodes.empty());
        return state->api->draw(state->renderer,&frame)!=0;
    };
    if(!submit()) {
        if(state->module->provider()!="d3d12")
            throw state->module->error("Cannot render frame");
        state->fallback_to_d3d11();
        if(!submit())throw state->module->error("Cannot render frame after D3D12 fallback");
    }
}

bool Viewport::supports_postfx() const noexcept {
    return state && state->postfx_api &&
           state->postfx_api->abi_version==NORTHSTARCPP_RENDER_POSTFX_ABI &&
           state->postfx_api->struct_size>=sizeof(NorthStarRenderPostFxApi) &&
           state->postfx_api->set_settings && state->postfx_api->get_settings;
}
void Viewport::set_postfx_settings(NorthStarRenderPostFxSettings const& settings) {
    if(!supports_postfx())
        throw std::runtime_error("Selected render provider lacks Filmic PostFX ABI v1");
    if(!state->postfx_api->set_settings(state->renderer,&settings))
        throw state->module->error("Render provider rejected Filmic PostFX parameters");
}
std::optional<NorthStarRenderPostFxSettings> Viewport::postfx_settings() const {
    if(!supports_postfx())return std::nullopt;
    NorthStarRenderPostFxSettings result{};
    result.struct_size=sizeof(result);
    if(!state->postfx_api->get_settings(state->renderer,&result))
        throw state->module->error("Unable to query Filmic PostFX parameters");
    return result;
}

std::optional<NorthStarRenderTimingStats> Viewport::gpu_frame_timing() const {
    if(!state || !state->timing_api ||
       state->timing_api->abi_version!=NORTHSTARCPP_RENDER_TIMING_ABI ||
       !state->timing_api->read)return std::nullopt;
    NorthStarRenderTimingStats result{};
    if(!state->timing_api->read(state->renderer,&result) || !result.valid)
        return std::nullopt;
    if(!std::isfinite(result.total_gpu_ms) || result.total_gpu_ms<0.f ||
       !std::isfinite(result.postfx_gpu_ms) || result.postfx_gpu_ms<0.f)
        return std::nullopt;
    return result;
}

std::size_t Viewport::queue_fragment_shader_reload(
    std::filesystem::path const& shader_file,
    std::vector<std::uint8_t> const& bytes)
{
    if(!state || (!state->shader_reload_v2 && !state->shader_reload_api))
        throw std::runtime_error("Selected render provider lacks Shader Reload ABI");
    if(state->source_project_root.empty())
        throw std::runtime_error("Shader hot reload requires a mounted project source root");
    if(bytes.size()<24 ||
       bytes.size()>NORTHSTARCPP_SHADER_RELOAD_MAX_BINARY_BYTES ||
       (!state->shader_reload_v2 && bytes.size()%4))
        throw std::invalid_argument("Invalid size of compiled GLSL SPIR-V");
    auto const root=std::filesystem::weakly_canonical(state->source_project_root);
    auto const file=std::filesystem::weakly_canonical(shader_file);
    auto const relative=file.lexically_relative(root);
    if(relative.empty() || relative.is_absolute())
        throw std::invalid_argument("Shader is outside the active project");
    for(auto const& part:relative)
        if(part=="..")
            throw std::invalid_argument("Shader is outside the active project");
    auto const virtual_path=relative.generic_string();
    bool const hlsl=relative.extension()==".hlsl";
    if(!hlsl && relative.extension()!=".frag" && relative.extension()!=".vert")
        throw std::invalid_argument("Shader hot reload requires .vert/.frag/.hlsl");
    std::vector<std::uint32_t> words((bytes.size()+3)/4);
    std::memcpy(words.data(),bytes.data(),bytes.size());
    std::size_t matched=0;
    for(auto const& [reference,material]:state->cached_materials) {
        const std::string* paths[3]{};
        if(hlsl) {
            paths[0]=&material.vertex_hlsl_asset;
            paths[1]=&material.fragment_hlsl_asset;
            paths[2]=&material.gbuffer_hlsl_asset;
        }else{
            paths[0]=&material.vertex_shader_asset;
            paths[1]=&material.fragment_shader_asset;
            paths[2]=&material.gbuffer_shader_asset;
        }
        for(std::uint32_t i=0;i<3;++i) {
            if(paths[i]->empty() || *paths[i]!=virtual_path)continue;
            const std::uint32_t stage=i+1;
            bool accepted=false;
            if(state->shader_reload_v2)
                accepted=state->shader_reload_v2->queue_shader_binary(
                    state->renderer,reference.c_str(),stage,
                    hlsl?NS_SHADER_BINARY_DXIL:NS_SHADER_BINARY_SPIRV,
                    bytes.data(),static_cast<std::uint32_t>(bytes.size()))!=0;
            else if(!hlsl && stage==NS_SHADER_PROGRAM_FRAGMENT_FORWARD)
                accepted=state->shader_reload_api->queue_fragment_spirv(
                    state->renderer,reference.c_str(),words.data(),
                    static_cast<std::uint32_t>(words.size()))!=0;
            if(!accepted)
                throw std::runtime_error("Render provider rejected shader stage for material: "+
                    reference+": "+state->module->error("Shader Reload").what());
            NorthStarShaderReloadStatus status{};
            status.struct_size=sizeof(status);
            bool const valid=state->shader_reload_v2?
                state->shader_reload_v2->poll(state->renderer,reference.c_str(),&status)!=0:
                state->shader_reload_api->poll(state->renderer,reference.c_str(),&status)!=0;
            if(!valid)
                throw state->module->error("Shader reload status read failed");
            state->reload_pending[reference]=status.revision;
            ++matched;
        }
    }
    return matched;
}
Viewport::ShaderReloadProgress Viewport::poll_shader_reload() {
    ShaderReloadProgress report;
    if(!state || (!state->shader_reload_v2 && !state->shader_reload_api))return report;
    for(auto it=state->reload_pending.begin();it!=state->reload_pending.end();) {
        NorthStarShaderReloadStatus status{};
        status.struct_size=sizeof(status);
        bool const valid=state->shader_reload_v2?
            state->shader_reload_v2->poll(state->renderer,it->first.c_str(),&status)!=0:
            state->shader_reload_api->poll(state->renderer,it->first.c_str(),&status)!=0;
        if(valid && (status.state==NS_SHADER_RELOAD_QUEUED ||
                     status.state==NS_SHADER_RELOAD_BUILDING)) {
            report.pending=true;++it;continue;
        }
        std::string message="Shader PSO ["+it->first+"]: ";
        if(!valid)message+="provider status unavailable";
        else if(status.state==NS_SHADER_RELOAD_READY &&
                status.installed_revision==status.revision)
            message+="GPU pipeline installed (revision "+
                     std::to_string(status.installed_revision)+")";
        else if(status.state==NS_SHADER_RELOAD_FAILED)
            message+="FAILED: "+std::string(status.diagnostic);
        else message+="shader request superseded or invalidated";
        report.completed.push_back(std::move(message));
        it=state->reload_pending.erase(it);
    }
    return report;
}

} // namespace northstarcpp
