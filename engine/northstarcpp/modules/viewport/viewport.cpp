#include "viewport_internal.hpp"
#include "viewport_sky_layers.hpp"
#include <northstarcpp/sky_project_config.hpp>

namespace northstarcpp {

Viewport::Viewport(HWND window, bool software) : state(std::make_unique<State>(window, software)) {}
Viewport::~Viewport() = default;

void Viewport::set_scene_animation_override(
    std::uint64_t entity_id, std::string const& clip_reference, float playback_rate) {
    if(!entity_id || !std::isfinite(playback_rate) ||
       playback_rate<0.1f || playback_rate>4.f)
        throw std::invalid_argument("Invalid skeletal animation playback rate/entity");
    if(clip_reference.empty()) {
        state->script_animation_overrides.erase(entity_id);
        return;
    }
    auto const at=clip_reference.rfind('@');
    if(clip_reference.size()>512 || at==std::string::npos || at<4 ||
       at+1>=clip_reference.size() ||
       clip_reference.substr(at-4,4)!=".ycd" ||
       clip_reference.find("..")!=std::string::npos ||
       clip_reference.find('\\')!=std::string::npos ||
       clip_reference.find(':')!=std::string::npos)
        throw std::invalid_argument("Invalid native YCD animation reference");
    auto& active=state->script_animation_overrides[entity_id];
    active.clip_reference=clip_reference;
    active.playback_rate=playback_rate;
}

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

bool Viewport::request_scene_frame_capture() {
    if(!state || !state->capture_api || !state->renderer)return false;
    return state->capture_api->request(state->renderer)!=0;
}
std::optional<Viewport::CapturedFrame> Viewport::read_scene_frame_capture() const {
    if(!state || !state->capture_api || !state->renderer)return std::nullopt;
    std::uint32_t w=0,h=0;
    auto const* api=state->capture_api;
    if(!api->read(state->renderer,nullptr,0,&w,&h)||
       w<64 || h<64 || w>3840 || h>2160)return std::nullopt;
    CapturedFrame image;
    image.width=w;image.height=h;
    image.rgba.resize(std::size_t(w)*h*4u);
    if(!api->read(state->renderer,image.rgba.data(),
                  static_cast<std::uint32_t>(image.rgba.size()),&w,&h))
        return std::nullopt;
    return image;
}

std::string Viewport::renderer_provider_name() const
{
    return state->module->provider();
}

bool Viewport::supports_shader_hot_reload() const noexcept {
    return state && state->renderer &&
        (state->shader_reload_v2 || state->shader_reload_api);
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
std::optional<Vector3> Viewport::attached_local_point(
    std::uint64_t child_entity,Vector3 weapon_local_point,
    CameraPose const* camera_pose) const
{
    if(!state || !child_entity || !weapon_local_point.valid())
        return std::nullopt;
    auto const found=state->skeletal_frame.attachments.find(child_entity);
    if(found==state->skeletal_frame.attachments.end())return std::nullopt;
    auto const transform=state->skeletal_frame.attachment_transform(
        found->second,camera_pose);
    if(!transform)return std::nullopt;
    auto const result=transform->transform_position(weapon_local_point);
    return result.valid()?std::optional<Vector3>{result}:std::nullopt;
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
void Viewport::set_preview_static_cache(bool enabled) {
    if(state->preview_static_cache_enabled==enabled)return;
    state->preview_static_cache_enabled=enabled;
    state->preview_material_cache.clear();
    state->preview_cache_vertices=0;
}
std::pair<std::size_t,std::size_t> Viewport::preview_cache_frame_stats() const noexcept {
    return {state->preview_frame_hits,state->preview_frame_vertices};
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
    auto const to_world = actor.skeleton_to_mesh *
        viewport_detail::skeletal_presentation_basis(to_skeleton) *
        create_rotation_y(-heading) *
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
    auto const common=shared_root.empty()?
        asset_paths::locate_shared_root(root,asset_paths::executable_dir()):
        shared_root;
    auto assets=std::make_unique<AssetStore>();
    if (!archive.empty()) {
        assets->mount_runtime_archives();
        if (!common.empty()) assets->mount_shared_assets(common);
        assets->mount_archive("",archive,archive_data.empty()?archive:archive_data);
    } else if (!root.empty()) {
        assets->mount_project_assets(root,common);
    }
    // Old project keys must not alias a new project's GPU geometry.
    if(state->rigid_api &&
       !state->rigid_api->clear_meshes(state->renderer))
        throw state->module->error("Cannot reset rigid GPU mesh residency");
    state->uploaded_rigid_meshes.clear();
    state->rigid_inactive_frames.clear();
    state->rejected_rigid_meshes.clear();
    state->rigid_opaque_materials.clear();
    state->preview_material_cache.clear();
    state->preview_cache_vertices=0;
    // Stop and join the old project's decode worker BEFORE destroying its VFS.
    // A new project never sees stale results from the previous asset mount.
    state->native_model_preloader.reset();
    state->project_assets=std::move(assets);
    // Project switches invalidate the previous world's sky and GPU atlas.
    state->retained_sky.reset();
    state->sky_gpu_uploaded=false;
    if(!state->api->set_sky(state->renderer,nullptr))
        throw state->module->error("Cannot invalidate previous project sky");
    // Sky source images are owned by the currently mounted VFS, never global.
    state->sky_source_layers.reset();
    state->sky_source_preset.clear();
    state->sky_source_checked=false;
    state->native_model_preloader=std::make_unique<
        viewport_detail::NativeModelPreloader>(
        [store=state->project_assets.get()](std::string const& name) {
            return load_native_model_resource(*store,name,true);
        });
    state->vehicle_models_prewarm_queued=false;
    // Must validate against the NEW mount, not the previous project root.
    state->source_project_root=archive.empty()?
        std::filesystem::absolute(root):std::filesystem::path{};
    // Generic project-owned prefetch hints are small XML data, not gameplay
    // policy. They allow MT28 and other large models to decode while a menu
    // is displayed, before the first visible frame of the destination scene.
    try {
        constexpr char manifest[]="preload/models.xml";
        std::uint64_t bytes=0;
        try {bytes=state->project_assets->size(manifest);}
        catch(std::runtime_error const&) {
            // Optional on all other projects; absence is not a warning.
        }
        if(bytes>0 && bytes<=65536) {
            auto raw=state->project_assets->read(manifest);
            std::string xml(raw.begin(),raw.end());
            if(xml.find("<!DOCTYPE")!=std::string::npos ||
               xml.find("<!ENTITY")!=std::string::npos)
                throw std::invalid_argument("Prefab preload DTD not allowed");
            boost::property_tree::ptree tree;
            std::istringstream input(xml);
            boost::property_tree::read_xml(input,tree,
                boost::property_tree::xml_parser::trim_whitespace);
            auto const& root=tree.get_child("NorthStarPreload");
            if(root.get<int>("<xmlattr>.version")!=1)
                throw std::invalid_argument("Unsupported model preload version");
            std::size_t total=0;
            for(auto const& element:root) {
                if(element.first!="Model")continue;
                auto const name=element.second.get<std::string>(
                    "<xmlattr>.asset","");
                if(!viewport_detail::safe_native_preload_reference(
                    name,state->source_project_root))
                    throw std::invalid_argument("Unsafe model preload path");
                state->native_model_preloader->enqueue(name);
                if(++total>=24)break;
            }
            if(total) {
                state->vehicle_models_prewarm_queued=true;
                std::fprintf(stderr,
                    "[INFO] [engine.assets] scheduled %zu full-res model preloads\n",
                    total);
            }
        }
    } catch(std::exception const& e) {
        std::fprintf(stderr,
            "[WARN] [engine.assets] Model prefetch manifest ignored: %s\n",
            e.what());
    }
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
    state->cached_hlod_meshes.clear();
    state->hlod_unbakeable_assets.clear();
    state->active_mesh_residency.clear();
    state->warned_unbound_players.clear();
    state->cached_native_skins.clear();
    state->native_playback.clear();
    state->script_animation_overrides.clear();
    state->reported_socket_attachments.clear();
    state->last_native_frame={};
    state->cached_materials.clear();
    // Project remount invalidates authored names and frees retained geometry
    // from the previous scene; normal frames keep their scratch capacity.
    state->frame_scratch.reset_project();
    state->frame_builder.reset_project();
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


void Viewport::set_sky_asset_reference(std::string const& drawable) {
    if(drawable.empty()) {
        state->retained_sky.reset();
        state->sky_gpu_uploaded=false;
        if(!state->api->set_sky(state->renderer,nullptr))
            throw state->module->error("Cannot clear authored project sky");
        return;
    }
    project_sky::validate_drawable_reference(drawable);
    if(!state->project_assets)
        throw std::runtime_error("Project sky needs a mounted VFS");
    // The asset system resolves the caller-supplied drawable ID, YMAT and YTD.
    // No file names or dictionary entries are selected by the renderer.
    auto decoded=state->project_assets->load_drawable_resources(drawable);
    set_sky(decoded);
}

void Viewport::configure_project_sky(std::string_view scene) {
    project_sky::Configuration profile;
    if(state->project_assets && !scene.empty()) {
        try {
            profile=project_sky::load(*state->project_assets,scene);
        } catch(std::exception const& error) {
            state->warn_asset("sky configuration",std::string(scene),error);
        }
    }
    // Clear the previous profile before uploading drawable-only textures;
    // the renderer selects a different color space for atmospheric data.
    set_sky_asset_reference({});
    set_sky_environment(SkyEnvironment{});
    if(!profile.drawable.empty()) {
        try {
            set_sky_asset_reference(profile.drawable);
        } catch(std::exception const& error) {
            state->warn_asset("sky drawable",profile.drawable,error);
            set_sky_asset_reference({});
        }
    }
    if(profile.settings.enabled)
        set_sky_environment(SkyEnvironment(profile.settings));
}

bool Viewport::supports_gpu_sky() const noexcept {
    return state && state->sky_gpu_api &&
        state->sky_gpu_api->abi_version==NORTHSTARCPP_RENDER_SKY_ABI &&
        state->sky_gpu_api->struct_size>=sizeof(NorthStarRenderSkyApi) &&
        state->sky_gpu_api->set_uniforms;
}
void Viewport::set_sky_environment(SkyEnvironment const& environment) {
    state->sky_environment=environment;
    const bool gpu=supports_gpu_sky();
    if(gpu) {
        auto const snap=environment.snapshot();
        NorthStarGpuSkyUniforms uniforms{};
        uniforms.sun_direction[0]=snap.sun_direction.x;
        uniforms.sun_direction[1]=snap.sun_direction.y;
        uniforms.sun_direction[2]=snap.sun_direction.z;
        uniforms.sun_direction[3]=float(snap.settings.fog_density);
        uniforms.moon_direction[0]=snap.moon_direction.x;
        uniforms.moon_direction[1]=snap.moon_direction.y;
        uniforms.moon_direction[2]=snap.moon_direction.z;
        uniforms.atmosphere[0]=float(snap.daylight);
        uniforms.atmosphere[1]=float(snap.settings.haze);
        uniforms.atmosphere[2]=float(snap.settings.sun_intensity);
        uniforms.atmosphere[3]=float(snap.settings.moon_intensity);
        uniforms.cloud[0]=float(snap.settings.cloud_coverage);
        uniforms.cloud[1]=float(snap.settings.cloud_speed);
        uniforms.cloud[2]=float(snap.flow_phase);
        uniforms.cloud[3]=float(snap.moon_illumination);
        uniforms.flags[0]=snap.settings.enabled?1.f:0.f;
        uniforms.flags[1]=snap.settings.sun?1.f:0.f;
        uniforms.flags[2]=snap.settings.moon?1.f:0.f;
        uniforms.flags[3]=snap.settings.clouds?1.f:0.f;
        // Renderer supplies the live camera each frame; the snapshot is
        // world weather state and deliberately contains no camera position.
        if(!state->sky_gpu_api->set_uniforms(state->renderer,&uniforms))
            throw state->module->error("GPU Sky constant update failed");
        // One small UBO update per frame; no shader recompilation or texture
        // re-upload when only sun/cloud motion changes.
        if(snap.settings.enabled && state->sky_gpu_uploaded &&
           state->sky_source_preset==snap.settings.preset)return;
    }
    if(!environment.settings().enabled) {
        state->sky_gpu_uploaded=false;
        if(state->retained_sky)set_sky(*state->retained_sky);
        else if(!state->api->set_sky(state->renderer,nullptr))
            throw state->module->error("Cannot clear engine.sky");
        return;
    }
    // Decode authored sky-layered.fx texture slots just once per VFS mount
    // and source preset. The exact same CPU-composited RGBA8 is sent to
    // Vulkan and D3D12; neither provider reads a separate sky atlas.
    auto const preset=environment.settings().preset;
    if(!state->sky_source_checked || state->sky_source_preset!=preset) {
        state->sky_source_checked=true;
        state->sky_source_preset=preset;
        state->sky_source_layers.reset();
        if(state->project_assets) {
            try {
                state->sky_source_layers=viewport_detail::load_sky_source_layers(
                    *state->project_assets,preset);
                std::fprintf(stderr,
                    "[INFO] [engine.sky] loaded sky.ytd/clouds.ytd preset=%s "
                    "source_layers=4 provider=%s\\n",
                    preset.c_str(),state->module->provider().c_str());
            } catch(std::exception const& error) {
                state->warn_asset("sky preset",preset,error);
            }
        }
    }
    if(state->retained_sky) {
        set_sky(*state->retained_sky);
        state->sky_gpu_uploaded=gpu;
        return;
    }
    // A project may enable engine.sky before providing a sky model. Use a
    // regular UV sphere with the same provider-neutral NorthStarSky contract,
    // not a second D3D12/Vulkan-specific rendering path.
    if(state->procedural_sky_vertices.empty()) {
        constexpr int columns=64,rows=32;
        constexpr float radius=180.f;
        constexpr float pi=3.14159265358979323846f;
        auto& vertices=state->procedural_sky_vertices;
        auto& indices=state->procedural_sky_indices;
        vertices.reserve((columns+1)*(rows+1));
        indices.reserve(columns*rows*6);
        for(int y=0;y<=rows;++y) {
            const auto v=float(y)/rows;
            const auto altitude=(v-.5f)*pi;
            for(int x=0;x<=columns;++x) {
                const auto u=float(x)/columns;
                const auto azimuth=u*2.f*pi;
                const auto cos_alt=std::cos(altitude);
                vertices.push_back({radius*cos_alt*std::sin(azimuth),
                    radius*std::sin(altitude),
                    radius*cos_alt*std::cos(azimuth),u,v});
            }
        }
        for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
            const auto a=std::uint32_t(y*(columns+1)+x);
            const auto b=a+1u;
            const auto c=a+columns+1u;
            indices.insert(indices.end(),{a,c,b,b,c,c+1u});
        }
    }
    constexpr std::uint32_t width=512u,height=256u;
    auto pixels=gpu?
        SkyEnvironment::render_gpu_cloud_rgba(width,height,
            state->sky_source_layers?&*state->sky_source_layers:nullptr):
        environment.render_rgba(width,height,nullptr,
            state->sky_source_layers?&*state->sky_source_layers:nullptr);
    auto const& vertices=state->procedural_sky_vertices;
    auto const& indices=state->procedural_sky_indices;
    NorthStarSky const sky{vertices.data(),
        static_cast<std::uint32_t>(vertices.size()),
        indices.data(),static_cast<std::uint32_t>(indices.size()),
        pixels.data(),width,height,1.f,1.f,1.f};
    if(!state->api->set_sky(state->renderer,&sky))
        throw state->module->error("Cannot submit procedural engine.sky dome");
    state->sky_gpu_uploaded=gpu;
}

void Viewport::set_sky(shared_assets::DrawableResources const& resources)
{
    // Preserve authored sky resources across an optional render-provider
    // fallback when material-capable geometry first enters the scene.
    // Subsequent atmosphere refreshes reuse the retained asset rather than
    // cloning the full YDD/YTD sky package on every time-of-day update.
    if(!state->retained_sky || &resources!=&*state->retained_sky)
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
    std::vector<std::uint8_t> atmosphere_pixels;
    if(state->sky_environment.settings().enabled) {
        if(supports_gpu_sky()) {
            // GPU shaders consume an immutable cloud field map, not the
            // already-coloured CPU atmosphere as a false cloud mask.
            width=1024u;height=512u;
            atmosphere_pixels=SkyEnvironment::render_gpu_cloud_rgba(
                width,height,
                state->sky_source_layers?&*state->sky_source_layers:nullptr);
        }else {
            atmosphere_pixels=state->sky_environment.render_rgba(
                width,height,pixels,
                state->sky_source_layers?&*state->sky_source_layers:nullptr);
        }
        pixels=atmosphere_pixels.data();
        tint[0]=tint[1]=tint[2]=1.f;
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

std::optional<NorthStarRigidInstanceStats>
Viewport::rigid_gpu_stats() const {
    if(!state || !state->rigid_api || !state->rigid_api->read_stats)
        return std::nullopt;
    NorthStarRigidInstanceStats stats{};
    stats.struct_size=sizeof(stats);
    if(!state->rigid_api->read_stats(state->renderer,&stats))
        return std::nullopt;
    return stats;
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
