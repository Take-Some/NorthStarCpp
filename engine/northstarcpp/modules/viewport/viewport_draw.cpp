#include "viewport_internal.hpp"
#include <cstring>
#include <cstdlib>
#include <northstarcpp/camera_owner_clearance.hpp>
#include <northstarcpp/player_camera_rig.hpp>

namespace northstarcpp {
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
                    bool show_streaming_debug, ViewportGizmo const* gizmo,
                    FpsWeaponEffects const* weapon_fx,
                    std::map<world::ChunkKey,world::TerrainChunk> const* terrain_chunks,
                    std::string const& terrain_material)
{
    // Scratch is reset before the host issues any new provider upload.
    // Its allocations are retained for the next frame, never shared across Viewports.
    state->frame_scratch.begin_frame();
    // Preload MT28 original meshes and their YMAT/YTD dependencies on the
    // single background decoder, before submitting visible objects.
    if(state->native_model_preloader && !state->vehicle_models_prewarm_queued) {
        std::size_t scheduled=0;
        for(auto const& obj:scene.objects) {
            auto const& ref=obj.node.asset;
            if(ref.find("mahir_mt28")==std::string::npos ||
               !viewport_detail::safe_native_preload_reference(
                   ref,state->source_project_root))continue;
            if(state->native_model_preloader->enqueue(ref,true))++scheduled;
            if(scheduled>=32u)break;
        }
        if(scheduled)state->vehicle_models_prewarm_queued=true;
    }
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
    // Script-authored locomotion overrides the saved scene component without
    // mutating project assets. Preserve the source graph when the override ends.
    for(auto const& [entity_id,override]:state->script_animation_overrides) {
        if(override.clip_reference.empty())continue;
        skeletal_frame.animations[entity_id]=override.clip_reference;
        skeletal_frame.graphs.erase(entity_id);
    }
    auto const& animation_bindings=skeletal_frame.animations;
    auto const& graph_bindings=skeletal_frame.graphs;
    state->grip_presets.clear();
    for(auto const& component:scene.components) {
        if(component.version==1 && component.entity_id &&
           component.type=="engine.model.weapon_grip" &&
           (component.payload=="pistol" || component.payload=="rifle"))
            state->grip_presets[component.entity_id]=component.payload;
    }
    auto& frame_index=state->frame_builder;
    frame_index.prepare(scene,state->world_surfaces_ready,camera_pose && !gizmo);
    // Runtime camera/owner collision is independent of Jolt body movement.
    // Do not hide head vertices: move the VIEW outside its own capsule.
    std::optional<CameraPose> owner_safe_camera;
    if(camera_pose && !gizmo && (selected==-2 || fps)) {
        if(auto const local=scene_pawn::local_index(scene)) {
            auto const& owner=scene.objects[*local];
            bool full_body_view=false;
            for(auto const& visual:scene.objects) {
                if(visual.node.id!=owner.node.id &&
                   visual.node.parent!=owner.node.id)continue;
                if(auto const* mode=frame_index.first_person(visual.node.id))
                    full_body_view|=mode->full_body;
            }
            const auto eye_height=gameplay::camera_rig(
                scene,owner.node.id).eye_height;
            const auto offset_x=camera_pose->position.x-owner.position.x;
            const auto offset_z=camera_pose->position.z-owner.position.z;
            const auto facing_depth=offset_x*std::sin(camera_pose->yaw)+
                offset_z*std::cos(camera_pose->yaw);
            // A saved first-person visual mode must not force an orbiting
            // third-person camera through the face when the boom contracts.
            const bool camera_at_authored_eye=
                std::abs(camera_pose->position.y-
                    (owner.position.y+eye_height))<.14f &&
                offset_x*offset_x+offset_z*offset_z<.025f;
            const bool first_person_eye=(full_body_view || fps ||
                camera_at_authored_eye) &&
                facing_depth>=-.08f &&
                offset_x*offset_x+offset_z*offset_z<.12f;
            const auto radius=first_person_eye?
                camera_collision::first_person_head_radius:
                (camera_pose->position.y>
                    owner.position.y+eye_height-.27f?.54f:.77f);
            const auto corrected=camera_collision::keep_camera_outside_player(
                camera_pose->position,owner.position,eye_height,
                camera_pose->yaw,radius,first_person_eye,
                [&](Vector3 const& candidate) {
                    if(!physics)return true;
                    const Vector3 pivot{owner.position.x,
                        owner.position.y+eye_height,owner.position.z};
                    const auto hit=physics->sweep_sphere(
                        pivot,candidate,.12f,*local);
                    return !hit || hit->fraction>=.999f;
                });
            if(camera_collision::player_camera_distance_squared(
                corrected,camera_pose->position)>1.e-8f) {
                owner_safe_camera=*camera_pose;
                owner_safe_camera->position=corrected;
                camera_pose=&*owner_safe_camera;
            }
        }
    }

    if(std::getenv("NORTHSTARCPP_TRACE_ARMAMENT") && camera_pose) {
        static unsigned camera_frames=0;
        if(camera_frames++<18u || camera_frames%40u==0u)
            std::fprintf(stderr,
                "[INFO] [engine.viewport] final_render_eye"
                " pos=(%.3f,%.3f,%.3f) adjusted=%u"
                " fov=%.1f near=%.3f\n",
                camera_pose->position.x,camera_pose->position.y,
                camera_pose->position.z,unsigned(bool(owner_safe_camera)),
                camera_pose->fov_y_degrees,camera_pose->near_plane);
    }
    for(auto it=state->native_playback.begin();
        it!=state->native_playback.end();) {
        if(!frame_index.is_active(it->first))
            it=state->native_playback.erase(it);
        else ++it;
    }
    for(auto it=state->native_playback.begin();
        it!=state->native_playback.end();) {
        if(!animation_bindings.count(it->first) &&
           !graph_bindings.count(it->first) &&
           !skeletal_frame.procedural_humanoids.count(it->first))
            it=state->native_playback.erase(it);
        else ++it;
    }
    state->asset_stats={};
    state->preview_frame_hits=0;
    state->preview_frame_vertices=0;
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
    auto& mesh_vertices=state->mesh_vertices;
    mesh_vertices.clear();
    bool const collect_pick=state->pick_v2_api && gizmo && camera_pose && !fps;
    bool pick_complete=true;
    auto& pick_triangles=state->frame_scratch.pick_triangles;
    auto& pick_cube_entities=state->frame_scratch.pick_cube_entities;
    std::uint64_t current_pick_entity=0;
    auto add_pick=[&](Vector3 const& position) {
        if(!collect_pick || !current_pick_entity || !pick_complete)return;
        if(pick_triangles.size()>=180000u) {pick_complete=false;return;}
        pick_triangles.push_back({position.x,position.y,position.z,0,
                                  current_pick_entity});
    };
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
    if((frame_index.has_materials() || has_native_materials) &&
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
    auto& material_draws=state->frame_scratch.material_draws;
    auto& skin_batches=state->skin_batches;
    skin_batches.begin_frame();
    auto& rigid_draws=state->rigid_draw_scratch;
    rigid_draws.clear();
    auto material_vertices=[&](std::string const& ref)
        ->std::vector<NorthStarMaterialVertex>& {
        return state->frame_scratch.material_vertices(ref);
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
        // Reuse CPU scratch and borrow asset names during this draw.
        // Avoid per-visible-mesh string copies and hash node allocations.
        auto& active_meshes=state->active_mesh_residency;
        auto const spawned_count=scene.objects.size()>authored_count
            ? scene.objects.size()-authored_count : 0u;
        active_meshes.begin(scene_world->visible_indices().size() +
            spawned_count+1);
        auto include_mesh=[&](std::size_t i) {
            active_meshes.include(scene.objects[i].node.asset);
        };
        for(auto const i:scene_world->visible_indices())include_mesh(i);
        // Spawned objects are persistent runtime entities, not authored cells.
        for(std::size_t i=authored_count;i<scene.objects.size();++i)
            if(scene.objects[i].node.visible) include_mesh(i);
        if(selected>=0 && static_cast<std::size_t>(selected)<scene.objects.size())
            include_mesh(static_cast<std::size_t>(selected));
        // Selected HLOD clusters require original source meshes for baking.
        // Other unloaded sectors still release their CPU mesh data.
        for(auto const& proxy:scene_world->hlod_proxies())
            for(auto const i:proxy.members)
                if(i<scene.objects.size())include_mesh(i);
        active_meshes.finish();
        for(auto it=state->cached_meshes.begin();it!=state->cached_meshes.end();) {
            if(!active_meshes.contains(it->first)) {
                state->cached_native_skins.erase(it->first);
                it=state->cached_meshes.erase(it);
            } else ++it;
        }
        // Release borrowed string_views before the caller can change Scene.
        // Retain asset key views until GPU submission finishes.
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
        if(std::getenv("NORTHSTARCPP_TRACE_ARMAMENT") &&
           skeletal_frame.attachments.count(scene.objects[i].node.id)) {
            static unsigned trace_count=0;
            if(trace_count++<14u || trace_count%40u==0u) {
                auto const& item=scene.objects[i];
                std::fprintf(stderr,"[INFO] [engine.viewport]"
                    " attached_input id=%llu asset='%s' visible=%u size=%.3f"
                    " index=%zu authored=%zu streamed=%u\\n",
                    static_cast<unsigned long long>(item.node.id),
                    item.node.asset.c_str(),unsigned(item.node.visible),
                    item.size,i,authored_count,unsigned(streaming));
            }
        }
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
        auto const* view=frame_index.first_person(object.node.id);
        bool const fps_arms=view && camera_pose;
        bool const fps_body=fps_arms && view->full_body;
        if(fps_arms && !fps_body) {
            auto const yaw=camera_pose->yaw,pitch=camera_pose->pitch;
            auto const d=view->forward;
            // The camera is a separate scene/rig entity. Only the submitted
            // mesh transform follows its view; the actor remains at its Pawn.
            position={
                camera_pose->position.x+std::sin(yaw)*std::cos(pitch)*d,
                camera_pose->position.y-view->drop+std::sin(pitch)*d,
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
            // Dynamic bodies may carry a temporary mesh-only facing yaw.
            // Physics retains the Jolt capsule transform (no teleport/rotate).
            if(physics && object.mass>0.f) {
                auto override=std::find_if(scene.components.begin(),
                    scene.components.end(),[&](SceneComponentBlob const& item) {
                        return item.entity_id==object.node.id &&
                            item.type=="engine.model.visual_yaw" &&
                            item.version==1 &&
                            item.payload.size()==sizeof(float);
                    });
                if(override!=scene.components.end()) {
                    float yaw=0.f;
                    std::memcpy(&yaw,override->payload.data(),sizeof(yaw));
                    if(std::isfinite(yaw) && std::abs(yaw)<=6.284f) {
                        auto const sn=std::sin(yaw),cs=std::cos(yaw);
                        const float basis[9]{
                            cs,0.f,sn, 0.f,1.f,0.f, -sn,0.f,cs};
                        std::copy(std::begin(basis),std::end(basis),
                                  std::begin(cube.rotation));
                    }
                }
            }
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
            if(fps_arms && !fps_body) {
                // Same authored mesh and skeletal animation; only view-space
                // orientation changes. No second arms asset is loaded.
                auto const yaw=camera_pose->yaw;
                auto const pitch=view->arm_pitch*0.01745329251994329577f
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
        auto const* material_ref=frame_index.material(object.node.id);
        bool const has_material=material_ref!=nullptr;
        // First-party foundation geometry, authored from the active Qt
        // Editor palette. The GPU triangle stream is shared with game runtime
        // and remains behind WorldPartition's verified visibility check.
        auto const* native_shape=frame_index.procedural(object.node.id);
        if(state->triangle_api && native_shape &&
           object.node.asset.empty()) {
            auto const& component=*native_shape;
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
                auto& batch=material_vertices(*material_ref);
                viewport_detail::append_procedural_geometry(
                    batch,geometry,object,cube,pick_triangles,
                    collect_pick,current_pick_entity,pick_complete);
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
           !native_shape) {
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
                    // Never decode YDR/YDD or their high-res YTD dependencies
                    // on the renderer/GUI thread. Missing assets are queued
                    // and rendered as soon as the full bundle is ready.
                    if(!state->native_model_preloader)
                        throw std::runtime_error("Native model preloader unavailable");
                    std::optional<NativeModelResource> decoded;
                    std::exception_ptr failure;
                    auto const status=state->native_model_preloader->take(
                        resource_path,decoded,failure);
                    if(status==viewport_detail::NativeModelPreloader::Status::missing) {
                        state->native_model_preloader->enqueue(resource_path,true);
                        continue;
                    }
                    if(status==viewport_detail::NativeModelPreloader::Status::pending)
                        continue;
                    if(status==viewport_detail::NativeModelPreloader::Status::failed)
                        std::rethrow_exception(failure);
                    if(!decoded)
                        throw std::runtime_error("Empty decoded native model");
                    auto native=std::move(*decoded);
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
                        rig.presentation_basis=viewport_detail::skeletal_presentation_basis(
                            native.mesh_to_skeleton);
                        // Native rig owns optional authored grip profile data,
                        // parsed only when loading the YDD/YMT.
                        auto const mesh_asset=std::filesystem::path(resource_path);
                        if(mesh_asset.parent_path().filename()=="models") {
                            auto const profile_path=
                                (mesh_asset.parent_path().parent_path()/
                                "animations"/"weapon_grips.xml").generic_string();
                            try {
                                if(state->project_assets->size(profile_path)>0) {
                                    auto const bytes=state->project_assets->read(profile_path);
                                    rig.weapon_grips=viewport_detail::parse_weapon_grips_xml(
                                        std::string_view(
                                            reinterpret_cast<char const*>(bytes.data()),
                                            bytes.size()));
                                }
                            }catch(std::exception const& e) {
                                std::fprintf(stderr,
                                    "[WARN] [engine.animation] Invalid grip XML %s: %s\n",
                                    profile_path.c_str(),e.what());
                            }
                        }
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
            if(std::getenv("NORTHSTARCPP_TRACE_ARMAMENT") && camera_pose &&
               skeletal_frame.attachments.count(object.node.id)) {
                static bool reported_frustum=false;
                if(!reported_frustum) {
                    reported_frustum=true;
                    const auto sy=std::sin(camera_pose->yaw);
                    const auto cy=std::cos(camera_pose->yaw);
                    const auto sp=std::sin(camera_pose->pitch);
                    const auto cp=std::cos(camera_pose->pitch);
                    const Vector3 f{sy*cp,sp,cy*cp},r{cy,0.f,-sy};
                    const Vector3 u{-sy*sp,cp,-cy*sp};
                    auto dot=[](Vector3 a,Vector3 b) {
                        return a.x*b.x+a.y*b.y+a.z*b.z;
                    };
                    const float scale=std::tan(camera_pose->fov_y_degrees*
                        0.008726646259971648f);
                    std::size_t ahead=0,visible=0;
                    float nearest=1.e7f,furthest=-1.e7f;
                    for(auto const& v:found->second.vertices) {
                        const auto pt=transformed({v.position.x*object.size,
                            v.position.y*object.size,v.position.z*object.size},
                            cube,position);
                        const auto d=pt-camera_pose->position;
                        const float depth=dot(d,f);
                        nearest=std::min(nearest,depth);
                        furthest=std::max(furthest,depth);
                        if(depth<=camera_pose->near_plane)continue;
                        ++ahead;
                        if(std::abs(dot(d,r))<depth*scale*1.5f &&
                           std::abs(dot(d,u))<depth*scale)++visible;
                    }
                    std::fprintf(stderr,
                        "[INFO] [engine.viewport] attached_frustum"
                        " total=%zu ahead=%zu in_view=%zu"
                        " min=%.3f max=%.3f pos=(%.3f,%.3f,%.3f)"
                        " eye=(%.3f,%.3f,%.3f)\\n",
                        found->second.vertices.size(),ahead,visible,
                        nearest,furthest,position.x,position.y,position.z,
                        camera_pose->position.x,camera_pose->position.y,
                        camera_pose->position.z);
                }
            }
            if(std::getenv("NORTHSTARCPP_TRACE_ARMAMENT") &&
               skeletal_frame.attachments.count(object.node.id)) {
                static unsigned trace_loaded=0;
                if(trace_loaded++<12u || trace_loaded%45u==0u)
                    std::fprintf(stderr,
                        "[INFO] [engine.viewport] attached_mesh_ready"
                        " path='%s' vertices=%zu triangles=%zu\\n",
                        resource_path.c_str(),found->second.vertices.size(),
                        found->second.indices.size()/3u);
            }
            // First-person arms are a native scene mesh, not an unsupported
            // Character ABI or a billboard. Keep material UV and YCD skinning.
            const bool first_person_arm_mesh=(fps_arms && !fps_body) ||
                object.node.layer=="FirstPersonArms";
            // ADS keeps the same skinned body/weapon world transform but
            // renders only distal forearms and hands to clear the sight picture.
            // FPS draws only forearms and hands, but retains the world-space
            // body pose and socket (no camera-relative transform).
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
            auto const* fps_skin_mask=first_person_arm_mesh?
                &arms_skin->second.first_person_arm_vertices:nullptr;
            // Optional scene v1 component payload:
            // animations/weapon/pistol/military-pistol.ycd@p-fire
            // Only this entity animates; the shared native model is immutable.
            MeshAsset const* selected_mesh=&found->second;
            std::vector<math::float4x4> const* gpu_skin_pose=nullptr;
            math::float4x4 mesh_world;
            std::optional<viewport_detail::SkinFrameBatches::Palette> gpu_skin_palette;
            auto const animation=animation_bindings.find(object.node.id);
            auto rig_it=state->cached_native_skins.find(resource_path);
            auto graph_binding=graph_bindings.find(object.node.id);
            bool const graph_mode=graph_binding!=graph_bindings.end();
            // Only explicitly opted-in NPC actors use source-independent
            // locomotion until their own YCD/Animation Graph is available.
            bool const procedural_npc=!graph_mode &&
                animation==animation_bindings.end() &&
                skeletal_frame.procedural_humanoids.count(object.node.id)>0;
            auto const prior_pose=state->native_playback.find(object.node.id);
            bool const has_retained_pose=prior_pose!=state->native_playback.end() &&
                                         prior_pose->second.pose_ready;
            if((animation!=animation_bindings.end() || graph_mode ||
                procedural_npc || has_retained_pose) &&
               rig_it!=state->cached_native_skins.end()) {
                auto& rig=rig_it->second;
                // Shared eligibility predicate for the ordinary and retained
                // skin paths. Never fall back to unskinned YDD triangles.
                auto gpu_skin_compatible=[&]() {
                    if(std::getenv("NORTHSTARCPP_FORCE_CPU_SKIN"))return false;
                    if(!state->skinned_material_api ||
                       rig.skeleton.joints.size()>NORTHSTARCPP_SKIN_MAX_JOINTS ||
                       rig.weights.size()!=found->second.vertices.size() ||
                       found->second.sections.empty() ||
                       skin_batches.draw_count()+found->second.sections.size()>
                           NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES)
                        return false;
                    for(auto const& section:found->second.sections) {
                        auto const& ref=has_material ?
                            *material_ref : section.material_reference;
                        auto material=state->cached_materials.find(ref);
                        if(ref.empty() ||material==state->cached_materials.end() ||
                           !material->second.graph_shader_defines.empty() ||
                           material->second.blend_mode!=0 ||
                           material->second.opacity!=1.f)
                            return false;
                    }
                    return true;
                };
                auto& play=state->native_playback[object.node.id];
                if(play.mesh_source!=resource_path) {
                    play.mesh_source=resource_path;
                    play.npc_position_valid=false;
                    play.npc_pose_reported=false;
                    play.npc_last_native_clip.clear();
                    play.npc_smoothed_speed=0.f;
                    play.npc_gait_phase=0.f;
                    play.pose.reset();
                    play.pose_ready=false;
                    play.base_pose.clear();
                    play.transition_pose.clear();
                    play.transition_seconds=0.f;
                    play.mesh_space_palette.clear();
                    play.graph.reset();
                    play.procedural_ik.reset();
                    play.grounded.reset();
                    play.lookat.reset();
                    play.lookat_cache.clear();
                    play.grounded_foot_cache.clear();
                    play.grounded_body_cache.clear();
                    play.grounded_physics_source=nullptr;
                    play.grounded_ignore_index=static_cast<std::size_t>(-1);
                    play.visual_root_valid=false;
                    play.procedural_ik_definitions.clear();
                    play.graph_path.clear();
                    play.graph_references.clear();
                    play.graph_bound_refs.clear();
                    play.cpu_mesh=MeshAsset{};
                }
                if(!graph_mode && animation!=animation_bindings.end() &&
                   play.clip_reference!=animation->second) {
                    // The outgoing pre-IK pose is the source of a transition;
                    // never interpolate through the bind/T-pose.
                    if(play.pose_ready && !play.base_pose.empty())
                        play.transition_pose=play.base_pose;
                    else play.transition_pose.clear();
                    play.transition_seconds=0.f;
                    play.clip_reference=std::string(animation->second);
                    play.visual_root_valid=false;
                    play.seconds=0.f;
                } else if(!graph_mode) {
                    auto const override=state->script_animation_overrides.find(object.node.id);
                    auto const rate=override==state->script_animation_overrides.end() ?
                        1.f : override->second.playback_rate;
                    play.seconds+=delta*rate;
                }
                try {
                    auto const& ref=play.clip_reference;
                    SkeletalClip const* selected_clip=nullptr;
                    if(graph_mode) {
                        if(!play.graph ||
                           std::string_view(play.graph_path)!=graph_binding->second) {
                            auto const asset_path=std::string(graph_binding->second);
                            auto definition=load_animation_graph(
                                *state->project_assets,asset_path);
                            auto references=definition.references();
                            auto graph=std::make_unique<AnimationGraphInstance>(
                                rig.skeleton,std::move(definition));
                            play.graph=std::move(graph);
                            play.visual_root_valid=false;
                            play.graph_path=asset_path;
                            play.graph_references=std::move(references);
                            play.graph_bound_refs.clear();
                        }
                        if(!rig.graph_loader)
                            rig.graph_loader=std::make_unique<AsyncYcdClipLoader>(
                                *state->project_assets,
                                std::make_shared<Skeleton const>(rig.skeleton),2);
                        for(auto const& reference:play.graph_references)
                            rig.graph_loader->request(reference);
                        rig.graph_loader->poll();
                        for(auto const& reference:play.graph_references) {
                            auto resident=rig.graph_loader->find(reference);
                            if(resident &&
                               play.graph_bound_refs.insert(reference).second)
                                play.graph->bind_clip(reference,std::move(resident));
                            auto const error=rig.graph_loader->error(reference);
                            if(!error.empty() &&
                               rig.invalid_clips.insert(reference).second)
                                std::fprintf(stderr,
                                    "[WARN] [engine.animation] async YCD %s: %s\n",
                                    reference.c_str(),error.c_str());
                        }
                        if(auto params=skeletal_frame.graph_parameters.find(
                            object.node.id);params!=skeletal_frame.graph_parameters.end())
                            for(auto const& [name,value]:params->second)
                                play.graph->set_parameter(name,value);
                    } else if(!procedural_npc) {
                        auto const at=ref.rfind('@');
                        if(at==std::string::npos || at<4 || at+1>=ref.size() ||
                           ref.find("..")!=std::string::npos ||
                           ref.find('\\')!=std::string::npos ||
                           ref.find(':')!=std::string::npos ||
                           ref.find('\0')!=std::string::npos ||
                           ref.substr(at-4,4)!=".ycd")
                            throw std::invalid_argument(
                                "Invalid native YCD clip reference");
                        if(!rig.invalid_clips.count(ref)) {
                            auto clip=rig.clips.find(ref);
                            if(clip==rig.clips.end()) {
                                auto const t_clip=std::chrono::steady_clock::now();
                                auto loaded=load_skeletal_clips(
                                    *state->project_assets,ref.substr(0,at),
                                    {ref.substr(at+1)},rig.skeleton);
                                auto const elapsed=std::chrono::duration_cast<
                                    std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now()-t_clip).count();
                                if(elapsed>100)std::fprintf(stderr,
                                    "[PERF] [engine.animation] ycd %s %lld ms\n",
                                    ref.c_str(),static_cast<long long>(elapsed));
                                clip=rig.clips.emplace(
                                    ref,std::move(loaded.front())).first;
                            }
                            selected_clip=&clip->second;
                        }
                    }
                    if(graph_mode || selected_clip || procedural_npc) {
                        if(selected_clip && selected_clip->joints.empty())
                            throw std::runtime_error(
                                "YCD clip has no animated skeletal joints");
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
                        // One authored forward conversion for body, skin and sockets.
                        mesh_world=rig.presentation_basis*mesh_world;
                         // Keep CPU-staged triangles in exactly the same world
                         // basis as GPU skin and published hand sockets.
                         viewport_detail::synchronize_skeletal_render_cube(
                             cube,mesh_world,object.size);
                        std::optional<viewport_detail::GripView> grip;
                        if(camera_pose && skeletal_frame.wants_hand_grip(object.node.id))
                            grip=viewport_detail::GripView{camera_pose->yaw,camera_pose->pitch,
                                fps_arms,camera_pose->position,
                                fps_body?view->forward:.52f,
                                fps_body?view->drop:.24f,
                                fps_body?view->right:.12f};
                        if(grip && rig.weapon_grips) {
                            grip->aim_down_sights=view && view->aim_down_sights;
                            grip->recoil_back=view?view->recoil_back:0.f;
                            grip->recoil_arm=view?view->recoil_arm:0.f;
                            auto const selected=state->grip_presets.find(object.node.id);
                            auto const weapon_class=selected==state->grip_presets.end()?
                                std::string_view("pistol"):selected->second;
                            grip->finger_profile=rig.weapon_grips->find(weapon_class);
                        }
                        diagnostics::FrameProfileScope::measure_current(diagnostics::FrameStage::animation,[&] {
                            static SkeletalClip const graph_placeholder{};
                            auto const& source_clip=selected_clip?
                                *selected_clip:graph_placeholder;
                            const viewport_detail::PoseInput input{rig.skeleton,source_clip,
                                play.seconds,mesh_world,rig.mesh_to_skeleton,grip};
                            viewport_detail::GripResults results{};
                            if(graph_mode) {
                                if(!play.graph->ready())
                                    throw std::runtime_error(
                                        "Animation Graph awaiting required YCD clips");
                                play.graph->advance(delta);
                                play.graph->evaluate(pose);
                                play.base_pose=pose.local_transforms();
                                // Procedural IK runs between graph evaluation and grip IK.
                                for(auto const& event:play.graph->events())
                                    std::fprintf(stderr,
                                        "[INFO] [engine.animation] entity=%llu event=%s clip=%s\n",
                                        static_cast<unsigned long long>(object.node.id),
                                        event.name.c_str(),event.clip.c_str());
                            } else if(procedural_npc) {
                                float displacement=0.f;
                                if(play.npc_position_valid) {
                                    auto const previous=play.npc_previous_position;
                                    displacement=std::hypot(position.x-previous.x,
                                        position.z-previous.z);
                                }
                                play.npc_previous_position={position.x,position.y,position.z};
                                play.npc_position_valid=true;
                                // Teleports/stream-in do not advance gait or explode speed.
                                auto const speed=delta>1.e-5f && displacement<1.5f ?
                                    std::clamp(displacement/delta,0.f,4.f):0.f;
                                auto const blend=std::clamp(delta*9.f,0.f,1.f);
                                play.npc_smoothed_speed+=
                                    (speed-play.npc_smoothed_speed)*blend;
                                play.npc_gait_phase+=displacement<1.5f ?
                                    displacement*8.f:0.f;
                                viewport_detail::sample_npc_procedural_pose(
                                    rig.skeleton,pose,play.seconds,
                                    play.npc_gait_phase,play.npc_smoothed_speed);
                                play.base_pose=pose.local_transforms();
                            } else {
                                pose.sample(rig.skeleton,*selected_clip,play.seconds,false);
                                if(!play.base_pose.empty()) {
                                    // Retain all unkeyed joints from prior authored animation.
                                    // Blend keyed joints directly between poses, never via bind.
                                    float alpha=1.f;
                                    if(!play.transition_pose.empty()) {
                                        play.transition_seconds+=std::max(0.f,delta);
                                        alpha=std::clamp(
                                            play.transition_seconds/.18f,0.f,1.f);
                                    }
                                    pose.preserve_unkeyed_and_blend(rig.skeleton,
                                        play.base_pose,play.transition_pose,alpha);
                                    if(alpha>=1.f)play.transition_pose.clear();
                                }
                                // Capture pre-IK locals to prevent accumulated grip drift.
                                play.base_pose=pose.local_transforms();
                            }
                            if(play.grounded_physics_source!=physics) {
                                play.grounded_physics_source=physics;
                                play.grounded_ignore_index=static_cast<std::size_t>(-1);
                                play.grounded.reset();
                                play.visual_root_valid=false;
                                if(physics) {
                                    auto const& bodies=physics->scene().objects;
                                    for(std::size_t i=0;i<bodies.size();++i)
                                        if(bodies[i].node.id==object.node.id) {
                                            play.grounded_ignore_index=i;
                                            break;
                                        }
                                }
                            }
                            math::float4x4 skeleton_to_mesh,rig_to_world;
                            auto const valid_basis=
                                skeleton_to_mesh.try_invert(rig.mesh_to_skeleton);
                            if(valid_basis)rig_to_world=skeleton_to_mesh*mesh_world;
                            // Clip-authored stance phase markers; no
                            // fixed clip names or hardcoded foot naming.
                            // Newly created IK controllers become eligible
                            // for phase updates on the following frame.
                            if(graph_mode &&play.grounded)
                                for(auto const& event:play.graph->events()) {
                                    std::string_view const name=event.name;
                                    if(name.rfind("ik.foot.plant:",0)==0)
                                        play.grounded->set_foot_phase(
                                            name.substr(14),true);
                                    else if(name.rfind("ik.foot.lift:",0)==0)
                                        play.grounded->set_foot_phase(
                                            name.substr(13),false);
                                }
                            // The physics Pawn is authoritative for world
                            // movement. Opt-in in-place sampling prevents
                            // clip root translation from double-moving the
                            // visible skeleton relative to its collision body.
                            if(physics &&play.grounded_ignore_index!=
                                static_cast<std::size_t>(-1)) {
                                auto const root=skeletal_frame.root_motion_policies.find(
                                    object.node.id);
                                if(root!=skeletal_frame.root_motion_policies.end() &&
                                   root->second.rfind("capsule ",0)==0) {
                                    auto const sampled=pose.consume_visual_root_motion(
                                        rig.skeleton,root->second.substr(8));
                                    if(sampled) {
                                        if(play.visual_root_valid&&delta>0.f&&
                                           valid_basis&&physics->supports_root_motion()) {
                                            auto const local=*sampled-play.previous_visual_root;
                                            auto const world=rig_to_world.transform_direction(local);
                                            // A loop discontinuity or teleport must not
                                            // generate an enormous motor impulse.
                                            if(world.valid() &&
                                               std::hypot(world.x,world.z)<=
                                                   std::min(.20f,delta*12.f))
                                                (void)physics->enqueue_root_motion(
                                                    object.node.id,
                                                    {world.x,0.f,world.z},delta);
                                        }
                                        play.previous_visual_root=*sampled;
                                        play.visual_root_valid=true;
                                    } else play.visual_root_valid=false;
                                } else play.visual_root_valid=false;
                            } else play.visual_root_valid=false;
                            viewport_detail::apply_procedural_ik(skeletal_frame,
                                object.node.id,rig.skeleton,pose,
                                play.procedural_ik,play.procedural_ik_definitions,delta,
                                physics,valid_basis?&rig_to_world:nullptr,
                                &play.grounded,&play.grounded_foot_cache,
                                &play.grounded_body_cache,play.grounded_ignore_index);
                            if(valid_basis)
                                viewport_detail::apply_distributed_lookat(
                                    skeletal_frame,object.node.id,rig.skeleton,
                                    pose,rig_to_world,delta,play.lookat,
                                    play.lookat_cache);
                            results=viewport_detail::evaluate_prepared_pose(input,pose);
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

                        // Native YDD mesh vertices are NOT in YMT joint space.
                        // The old Character renderer applied this basis change,
                        // but the Scene V3 CPU path skipped it. Consequently
                        // the vertices of hands rendered far from wrist sockets
                        // even when IK targets were correct.
                        remap_skin_palette_to_mesh(play.mesh_space_palette,
                            pose.palette(),rig.mesh_to_skeleton);
                        // GPU only for opaque materials supported by v1.
                        // The CPU path remains mandatory for custom graphs,
                        // transparent assets or external render providers.
                        bool compatible=gpu_skin_compatible();
                        if(compatible) {
                            gpu_skin_pose=&play.mesh_space_palette;
                        } else {
                            skin_native_mesh_cpu_into(play.cpu_mesh,found->second,
                                rig.skeleton,rig.weights,play.mesh_space_palette,
                                fps_skin_mask);
                            selected_mesh=&play.cpu_mesh;
                        }
                        if(selected_clip && std::getenv("NORTHSTARCPP_TRACE_NPC_POSE")) {
                            static std::size_t inspected=0;
                            if((++inspected%8)==0 && inspected<240) {
                                auto ankle=rig.skeleton.joint_index("r_ankle");
                                auto const& locals=pose.local_transforms();
                                std::fprintf(stderr,
                                    "[QA.NPC] entity=%llu clip=%s seconds=%.4f delta=%.4f "
                                    "ankle=(%.3f,%.3f,%.3f,%.3f) cpu=%d\n",
                                    static_cast<unsigned long long>(object.node.id),
                                    play.clip_reference.c_str(),play.seconds,delta,
                                    ankle<locals.size()?locals[ankle].rotation.x:0.f,
                                    ankle<locals.size()?locals[ankle].rotation.y:0.f,
                                    ankle<locals.size()?locals[ankle].rotation.z:0.f,
                                    ankle<locals.size()?locals[ankle].rotation.w:0.f,
                                    compatible?0:1);
                            }
                        }
                        play.pose_ready=true;
                        if(selected_clip &&
                           skeletal_frame.procedural_humanoids.count(object.node.id) &&
                           play.npc_last_native_clip!=play.clip_reference) {
                            std::fprintf(stderr,
                                "[INFO] [engine.npc.animation] entity=%llu YCD=%s tracks=%zu frames=%u skin=%s\\n",
                                static_cast<unsigned long long>(object.node.id),
                                play.clip_reference.c_str(),selected_clip->joints.size(),
                                selected_clip->frames,compatible?"GPU":"CPU");
                            play.npc_last_native_clip=play.clip_reference;
                        }
                        if(procedural_npc && !play.npc_pose_reported) {
                            std::fprintf(stderr,
                                "[INFO] [engine.npc.animation] entity=%llu rig_joints=%zu pose=PROCEDURAL_SKINNED\n",
                                static_cast<unsigned long long>(object.node.id),
                                rig.skeleton.joints.size());
                            play.npc_pose_reported=true;
                        }
                    }
                } catch(std::exception const& error) {
                    if(rig.invalid_clips.insert(graph_mode?std::string(graph_binding->second):play.clip_reference).second)
                        state->warn_asset(graph_mode?"Animation Graph":"YCD animation",graph_mode?std::string(graph_binding->second):play.clip_reference,error);
                }
                if(!gpu_skin_pose && selected_mesh==&found->second) {
                    // A missing/broken incoming clip must NOT expose the raw
                    // bind-pose model. Hold the last known good *skin* while
                    // retaining the current entity transform and camera yaw.
                    if(!play.pose_ready ||
                       play.mesh_space_palette.size()!=rig.skeleton.joints.size())
                        continue; // Initial asset streaming: invisible, no T-pose.
                    mesh_world.identity();
                    for(unsigned row=0;row<3;++row)
                        for(unsigned col=0;col<3;++col)
                            mesh_world.elements[row][col]=
                                cube.rotation[col*3+row]*object.size;
                    mesh_world.elements[3][0]=position.x;
                    mesh_world.elements[3][1]=position.y;
                    mesh_world.elements[3][2]=position.z;
                    mesh_world=rig.presentation_basis*mesh_world;
                    viewport_detail::synchronize_skeletal_render_cube(
                        cube,mesh_world,object.size);
                    if(gpu_skin_compatible())
                        gpu_skin_pose=&play.mesh_space_palette;
                    else {
                        try {
                            skin_native_mesh_cpu_into(play.cpu_mesh,found->second,
                                rig.skeleton,rig.weights,play.mesh_space_palette,
                                fps_skin_mask);
                            selected_mesh=&play.cpu_mesh;
                        } catch(std::exception const& error) {
                            if(rig.invalid_clips.insert(resource_path+"#skin-fallback").second)
                                state->warn_asset("retained skin",resource_path,error);
                            continue; // Never draw original, unskinned YDD.
                        }
                    }
                }
            }
            // An animated native model must NEVER be drawn in its raw
            // YMT bind pose, even when the animation component temporarily
            // disappears while switching clips or camera modes.
            if((animation!=animation_bindings.end() ||graph_mode ||
                has_retained_pose) &&
               rig_it==state->cached_native_skins.end())
                continue;
            auto const& mesh=*selected_mesh;
            // Preserve each native MeshPart's YMAT slot and UV topology.
            // The explicit scene material (if any) overrides every part.
            auto arm_triangle=[&](std::size_t k) -> bool {
                if(!fps_skin_mask)return true;
                auto const& mask=*fps_skin_mask;
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
                    auto& cached=first_person_arm_mesh?
                        rig.gpu_arm_sections:
                        rig.gpu_sections;
                    if(!cached) {
                        const auto provider=state->module->provider();
                        const auto version=state->module->version();
                        diagnostics::FrameProfileScope::measure_current(
                            diagnostics::FrameStage::asset_streaming,[&] {
                                auto prepared=viewport_detail::prepare_skin_sections(
                                    {mesh,rig.weights,fps_skin_mask,provider,version},
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
                            *gpu_skin_pose,mesh_world);
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
                // Only original opaque static mesh sections can use the
                // immutable indexed GPU path. Transparencies, custom graph,
                // skinned, editor-picking and external providers use the
                // original full-resolution triangle stream unchanged.
                // CPU skinning updates the transient vertex positions on EVERY
                // frame. Never register that scratch mesh as immutable GPU
                // geometry: doing so freezes the first sampled pose, making
                // locomoting NPCs slide even though YCD advances correctly.
                if(state->rigid_api && selected_mesh==&found->second &&
                   !gpu_skin_pose && !fps_skin_mask &&
                   !collect_pick && !gizmo && !material.empty() &&
                   mesh.indices.size()<=NORTHSTARCPP_RIGID_MAX_MESH_INDICES &&
                   mesh.vertices.size()<=NORTHSTARCPP_RIGID_MAX_MESH_VERTICES &&
                   !state->rejected_rigid_meshes.count(resource_path)) {
                    auto mat=state->cached_materials.find(material);
                    if(mat!=state->cached_materials.end()) {
                        auto const& pbr=mat->second;
                        // Imported alpha materials with fully opaque
                        // texels use immutable indexed GPU meshes safely.
                        auto [eligibility,firstCheck]=
                            state->rigid_opaque_materials.try_emplace(material,false);
                        if(firstCheck) {
                            bool opaque=pbr.base_color[3]>=.999f &&
                                pbr.opacity>=.999f;
                            auto const& albedo=pbr.textures[0];
                            if(opaque && !albedo.rgba8.empty()) {
                                opaque=albedo.rgba8.size()%4==0;
                                for(std::size_t pixel=3;opaque &&
                                    pixel<albedo.rgba8.size();pixel+=4)
                                    opaque=albedo.rgba8[pixel]==255;
                            } else if(opaque && (albedo.width || albedo.height))
                                opaque=false;
                            eligibility->second=opaque;
                        }
                        if(eligibility->second &&
                           pbr.graph_shader_defines.empty() &&
                           pbr.vertex_shader_asset.empty() &&
                           pbr.fragment_shader_asset.empty() &&
                           pbr.gbuffer_shader_asset.empty() &&
                           pbr.vertex_hlsl_asset.empty() &&
                           pbr.fragment_hlsl_asset.empty() &&
                           pbr.gbuffer_hlsl_asset.empty()) {
                            auto const mesh_key=std::uint64_t(
                                std::hash<std::string>{}(resource_path))|1ull;
                            bool uploaded=state->uploaded_rigid_meshes.count(resource_path)>0;
                            if(!uploaded) {
                                try {
                                    auto& src=state->rigid_upload_vertices;
                                    src.clear();
                                    src.reserve(mesh.vertices.size());
                                    for(auto const& vertex:mesh.vertices)
                                        src.push_back({vertex.position.x,
                                            vertex.position.y,vertex.position.z,
                                            vertex.normal.x,vertex.normal.y,
                                            vertex.normal.z,vertex.u,vertex.v});
                                    NorthStarRigidMesh gpu{
                                        mesh_key,src.data(),
                                        static_cast<std::uint32_t>(src.size()),
                                        mesh.indices.data(),
                                        static_cast<std::uint32_t>(mesh.indices.size())};
                                    uploaded=state->rigid_api->upload_mesh(
                                        state->renderer,&gpu)!=0;
                                    if(uploaded)state->uploaded_rigid_meshes.insert(resource_path);
                                }catch(std::exception const& e) {
                                    state->warn_asset("GPU rigid mesh",resource_path,e);
                                }
                                if(!uploaded) {
                                    state->rejected_rigid_meshes.insert(resource_path);
                                    std::fprintf(stderr,
                                        "[WARN] [engine.render] Rigid GPU upload rejected %s;"
                                        " retaining full-detail CPU renderer\n",
                                        resource_path.c_str());
                                }
                            }
                            if(uploaded) {
                                NorthStarRigidInstance instance{};
                                instance.mesh_key=mesh_key;
                                instance.entity_id=object.node.id;
                                instance.first_index=static_cast<std::uint32_t>(begin);
                                instance.index_count=static_cast<std::uint32_t>(count);
                                instance.world[15]=1.f;
                                for(unsigned row=0;row<3;++row)
                                    for(unsigned col=0;col<3;++col)
                                        instance.world[col*4+row]=
                                            cube.rotation[row*3+col]*object.size;
                                instance.world[12]=position.x;
                                instance.world[13]=position.y;
                                instance.world[14]=position.z;
                                instance.material=viewport_detail::material_batch(pbr);
                                rigid_draws.push_back(instance);
                                // Source UVs/normals and indices are read by
                                // shader directly from the resident mesh.
                                return;
                            }
                        }
                    }
                }
                if(!material.empty()) {
                    if(!state->material_api && !state->material_api_v2)
                        throw std::runtime_error(
                            "Native YDD/YDR requires render Materials ABI v1 or v2");
                    auto& batch=material_vertices(material);
                    // Only an immutable, non-skinned static preview object
                    // may reuse transformed CPU stream vertices between
                    // camera-only frames. The original indexed geometry is
                    // never decimated or altered.
                    bool const can_cache=state->preview_static_cache_enabled &&
                        !gpu_skin_pose && !fps_skin_mask && !collect_pick &&
                        !state->cached_native_skins.count(resource_path) &&
                        !skeletal_frame.attachments.count(object.node.id);
                    if(can_cache && count &&
                       count<=NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES) {
                        std::array<float,13> world{};
                        for(unsigned axis=0;axis<9;++axis)
                            world[axis]=cube.rotation[axis];
                        world[9]=position.x;world[10]=position.y;
                        world[11]=position.z;world[12]=object.size;
                        auto cache_key=resource_path+"|"+
                            std::to_string(object.node.id)+"|"+
                            std::to_string(begin)+"|"+material;
                        auto& cache=state->preview_material_cache;
                        auto found=cache.find(cache_key);
                        if(found!=cache.end() &&
                           found->second.transform==world &&
                           found->second.source==&mesh &&
                           found->second.index_count==count) {
                            auto const& vertices=found->second.vertices;
                            if(batch.size()>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES-
                                vertices.size())
                                throw std::runtime_error("Material mesh vertex limit exceeded");
                            batch.insert(batch.end(),vertices.begin(),vertices.end());
                            ++state->preview_frame_hits;
                            state->preview_frame_vertices+=vertices.size();
                            return;
                        }
                        if(found!=cache.end()) {
                            state->preview_cache_vertices-=found->second.vertices.size();
                            cache.erase(found);
                        }
                        constexpr std::size_t limit=2500000u; // <=80 MiB
                        if(count<=limit-state->preview_cache_vertices) {
                            State::PreviewMaterialCacheEntry entry;
                            entry.transform=world;
                            entry.source=&mesh;
                            entry.first_index=begin;
                            entry.index_count=count;
                            entry.vertices.reserve(count);
                            for(std::size_t k=begin;k<begin+count;++k) {
                                auto const& v=mesh.vertices.at(mesh.indices[k]);
                                auto const point=transformed({
                                    v.position.x*object.size,v.position.y*object.size,
                                    v.position.z*object.size},cube,position);
                                auto const normal=normal_direction(v.normal,cube);
                                entry.vertices.push_back({point.x,point.y,point.z,
                                    normal.x,normal.y,normal.z,v.u,v.v});
                            }
                            if(batch.size()>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES-
                                entry.vertices.size())
                                throw std::runtime_error("Material mesh vertex limit exceeded");
                            batch.insert(batch.end(),
                                entry.vertices.begin(),entry.vertices.end());
                            state->preview_cache_vertices+=entry.vertices.size();
                            cache.emplace(std::move(cache_key),std::move(entry));
                            return;
                        }
                    }
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
                        has_material ? *material_ref : section.material_reference,index);
                }
            } else {
                submit_range(0,mesh.indices.size(),
                    has_material ? *material_ref : std::string{},0);
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
            auto& batch=material_vertices(*material_ref);
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
            auto const* over=frame_index.surface(object.node.id);
            auto const id=over?*over:
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

    // Native HLOD source meshes are decoded on the existing worker and
    // materialized here only after decoding completes. Never synchronously
    // decode a cold YDD/YDR while rendering a distant sector.
    std::size_t hlod_preload_budget=8;
    if(streaming && state->native_model_preloader) {
        for(auto const& proxy:scene_world->hlod_proxies()) {
            for(auto const i:proxy.members) {
                if(i>=scene.objects.size())continue;
                auto const& ref=scene.objects[i].node.asset;
                if(state->cached_meshes.count(ref) ||
                   state->hlod_unbakeable_assets.count(ref))continue;
                auto const path=ref.substr(0,ref.find('@'));
                bool const native=path.size()>4 &&
                    (path.compare(path.size()-4,4,".ydr")==0 ||
                     path.compare(path.size()-4,4,".ydd")==0);
                if(!native)continue;
                try {
                    std::optional<NativeModelResource> decoded;
                    std::exception_ptr failure;
                    auto const result=state->native_model_preloader->take(
                        ref,decoded,failure);
                    if(result==viewport_detail::NativeModelPreloader::Status::missing) {
                        if(hlod_preload_budget &&
                           state->native_model_preloader->enqueue(ref,true))
                            --hlod_preload_budget;
                    } else if(result==viewport_detail::NativeModelPreloader::Status::ready &&
                              decoded) {
                        if(decoded->has_skin()) {
                            state->hlod_unbakeable_assets.insert(ref);
                            continue; // preserve original skinned semantics
                        }
                        for(auto& entry:decoded->materials)
                            state->cached_materials.try_emplace(
                                entry.first,std::move(entry.second));
                        state->cached_meshes.try_emplace(
                            ref,std::move(decoded->mesh));
                    } else if(result==viewport_detail::NativeModelPreloader::Status::failed) {
                        if(failure)std::rethrow_exception(failure);
                        throw std::runtime_error("Native HLOD decode failed");
                    }
                }catch(std::exception const& e) {
                    if(state->hlod_unbakeable_assets.insert(ref).second)
                        state->warn_asset("HLOD source",ref,e);
                }
            }
        }
    }
    // Two-level runtime HLOD prefers cached merged static geometry; unresolved
    // source assets retain the old conservative AABB. ECS/physics stay intact.
    if (streaming) {
        for (auto const& proxy:scene_world->hlod_proxies()) {
            // Build one true material-aware mesh per static cluster, cached
            // across camera-only frames. Absent source/skinning = old AABB.
            auto const* baked=viewport_detail::resolve_hlod_mesh(
                scene,proxy,state->cached_meshes,state->cached_hlod_meshes,
                [&](std::uint64_t id) -> std::string {
                    auto const* value=frame_index.material(id);
                    return value?*value:std::string{};
                },
                [&](std::string const& ref,std::uint64_t id) {
                    return state->cached_native_skins.count(ref)>0 ||
                           state->hlod_unbakeable_assets.count(ref)>0 ||
                           skeletal_frame.attachments.count(id)>0;
                },reinterpret_cast<std::uintptr_t>(scene_world));
            if(baked && (state->triangle_api || state->material_api ||
                         state->material_api_v2)) {
                std::unordered_map<std::string,std::size_t> needed;
                std::size_t untextured=0;
                for(auto const& section:baked->sections) {
                    if(section.material_reference.empty())
                        untextured+=section.index_count;
                    else needed[section.material_reference]+=section.index_count;
                }
                bool enough=mesh_vertices.size()<=300000u &&
                    untextured<=300000u-mesh_vertices.size() &&
                    (untextured==0 || state->triangle_api);
                if(!needed.empty() && !state->material_api &&
                   !state->material_api_v2)enough=false;
                if(enough)for(auto const& [material,count]:needed) {
                    auto& batch=material_vertices(material);
                    if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES-
                             batch.size()) {enough=false;break;}
                }
                if(enough) {
                    for(auto const& section:baked->sections) {
                        bool const textured=!section.material_reference.empty();
                        auto* batch=textured?&material_vertices(
                            section.material_reference):nullptr;
                        auto const end=section.first_index+section.index_count;
                        for(auto k=section.first_index;k<end;++k) {
                            auto const& v=baked->vertices[baked->indices[k]];
                            if(batch)batch->push_back({v.position.x,v.position.y,
                                v.position.z,v.normal.x,v.normal.y,v.normal.z,
                                v.u,v.v});
                            else mesh_vertices.push_back({v.position.x,v.position.y,
                                v.position.z,proxy.color});
                        }
                    }
                    ++state->asset_stats.baked_hlod_clusters;
                    if(collect_pick)pick_complete=false; // not actor geometry
                    continue;
                }
            }
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

    // D3D11/D3D12 retain the analytic GPU grid; Vulkan's temporary
    // geometric fallback must never extend its strips to the horizon.
    if(gizmo && gizmo->show_grid && camera_pose && !fps &&
       !state->editor_grid_api) {
        auto const layout=editor_grid::build(
            *camera_pose,gizmo->grid_width,gizmo->grid_height);
        auto const minor=std::clamp(layout.minor_step*.022f,.009f,20.f);
        const bool vk=state->module->provider()=="vulkan";
        const float radius=std::clamp(camera_pose->far_plane*.24f,30.f,190.f);
        for(auto const& line:layout.lines) {
            if(cubes.size()>=max_scene_objects)break;
            if(vk) {
                const bool x_axis=std::abs(line.to.x-line.from.x)>.01f;
                const float fixed=x_axis?line.from.z:line.from.x;
                const float camera_fixed=x_axis?
                    camera_pose->position.z:camera_pose->position.x;
                const float perp=fixed-camera_fixed;
                if(std::abs(perp)>=radius)continue;
                const float half=std::sqrt(radius*radius-perp*perp);
                const float center=x_axis?
                    camera_pose->position.x:camera_pose->position.z;
                const float a=x_axis?line.from.x:line.from.z;
                const float b=x_axis?line.to.x:line.to.z;
                const float lo=std::max(std::min(a,b),center-half);
                const float hi=std::min(std::max(a,b),center+half);
                if(hi-lo<.01f)continue;
                const int segments=std::clamp(
                    int(std::ceil((hi-lo)/24.f)),1,16);
                const float thickness=line.origin?minor*2.f:
                    line.major?minor*1.35f:minor;
                for(int i=0;i<segments &&
                    cubes.size()<max_scene_objects;++i) {
                    const float a0=lo+(hi-lo)*float(i)/segments;
                    const float a1=lo+(hi-lo)*float(i+1)/segments;
                    const float mid=(a0+a1)*.5f;
                    const float dist=std::hypot(perp,mid-center);
                    const float fade=std::clamp(
                        (radius-dist)/(radius*.35f),0.f,1.f);
                    if(fade<.07f)continue;
                    auto color=[&](int shift) {
                        const float value=float((line.color>>shift)&255u);
                        const float bg=float((0x18283bu>>shift)&255u);
                        return std::uint32_t(std::clamp(
                            bg+(value-bg)*fade,0.f,255.f));
                    };
                    NorthStarRenderCube strip{};
                    strip.size=1.f;
                    strip.rgb=(color(16)<<16)|(color(8)<<8)|color(0);
                    strip.x=x_axis?mid:fixed;
                    strip.z=x_axis?fixed:mid;
                    strip.y=.018f-.5f;
                    strip.rotation[0]=x_axis?a1-a0:thickness;
                    strip.rotation[4]=std::max(.008f,minor*.25f);
                    strip.rotation[8]=x_axis?thickness:a1-a0;
                    cubes.push_back(strip);
                }
                continue;
            }
            // Legacy external providers without analytic grid ABI.
            NorthStarRenderCube strip{};
            strip.size=1.f;
            strip.rgb=line.color;
            strip.x=(line.from.x+line.to.x)*.5f;
            strip.y=.012f-.5f;
            strip.z=(line.from.z+line.to.z)*.5f;
            strip.rotation[0]=std::max(minor*.75f,
                std::abs(line.to.x-line.from.x));
            strip.rotation[4]=std::max(.008f,minor*.25f);
            strip.rotation[8]=std::max(minor*.75f,
                std::abs(line.to.z-line.from.z));
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

    #include "viewport_procedural_terrain_batches.inc"
    // The host resolves material resources through VFS; GPU plugins only
    // see copied UV geometry, factors and bounded RGBA8 textures.
    auto& gpu_materials=state->frame_scratch.gpu_materials;
    auto& gpu_materials_v2=state->frame_scratch.gpu_materials_v2;
    auto& material_keys=state->frame_scratch.material_keys;
    auto const active_materials=state->frame_scratch.active_material_draws;
    gpu_materials.reserve(active_materials);
    gpu_materials_v2.reserve(active_materials);
    material_keys.reserve(active_materials);
    bool needs_material_v2=false;
    // Share one YTD decode cache across all newly loaded character materials.
    // Private streaming dictionaries contain hundreds of source mip chains.
    std::unique_ptr<MaterialLoadSession> material_loading_session;
    std::size_t material_vertex_total=0;
    for(std::size_t material_index=0;material_index<active_materials;++material_index) {
        auto const& entry=material_draws[material_index];
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
                if(!material_loading_session)
                    material_loading_session=std::make_unique<MaterialLoadSession>(
                        *state->project_assets);
                auto value=material_loading_session->load(reference);
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
        // The material name belongs to the host's current draw scratch and
        // is stable until submit returns. Avoid duplicating it per frame.
        material_keys.push_back(reference.c_str());
    }
    #include "viewport_world_ui_batches.inc"
    auto const& gpu_skinned_batches=skin_batches.build();
    // Short-lived shot visuals never enter the physics scene or allocate a
    // rigid body. All GPU providers receive the same bounded proxy geometry.
    if(weapon_fx && camera_pose && state->material_api_v2) {
        // Smooth, alpha-textured VFX using the same D3D12/D3D11 Materials V2
        // path as world materials. No per-effect opaque debug cubes.
        if(viewport_detail::append_weapon_sprites(
            *weapon_fx,*camera_pose,state->frame_scratch,
            gpu_materials,gpu_materials_v2,material_keys)>0)
            needs_material_v2=true;
    } else if(weapon_fx) {
        // Legacy external providers lacking blended material ABI only.
        for(auto const& effect:weapon_fx->visuals()) {
            if(cubes.size()>=max_scene_objects)break;
            NorthStarRenderCube cube{};
            cube.x=effect.position.x;
            cube.y=effect.position.y-.5f*effect.scale.y; // center correctly at muzzle
            cube.z=effect.position.z;
            cube.size=1.f;
            cube.rgb=effect.rgb;
            cube.rotation[0]=effect.scale.x;
            cube.rotation[4]=effect.scale.y;
            cube.rotation[8]=effect.scale.z;
            cubes.push_back(cube);
        }
    }
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
    auto scene_lights=northstarcpp::render::scene_render_lights(scene);
    // The common engine.sky clock controls world illumination as well as
    // celestial texture placement. Shader providers see the same directional
    // sun and ambient sky tags; individual games may still author local lights.
    if(state->sky_environment.settings().enabled &&
       scene_lights.size()+3u<=NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS) {
        auto const sky=state->sky_environment.snapshot();
        if(sky.settings.sun && sky.sun_direction.y>-.06f) {
            NorthStarRenderLight sunlight{};
            sunlight.position_range[3]=1.f;
            sunlight.color_intensity[0]=1.f;
            sunlight.color_intensity[1]=.92f;
            sunlight.color_intensity[2]=.80f;
            sunlight.color_intensity[3]=float(
                (0.08+.92*sky.daylight)*sky.settings.sun_intensity*2.);
            sunlight.direction_cone[0]=-sky.sun_direction.x;
            sunlight.direction_cone[1]=-sky.sun_direction.y;
            sunlight.direction_cone[2]=-sky.sun_direction.z;
            sunlight.direction_cone[3]=NORTHSTARCPP_LIGHT_DIRECTIONAL_TAG;
            scene_lights.push_back(sunlight);
        }
        if(sky.settings.moon && sky.moon_direction.y>0.f &&
           sky.daylight<.25) {
            NorthStarRenderLight moonlight{};
            moonlight.position_range[3]=1.f;
            moonlight.color_intensity[0]=.38f;
            moonlight.color_intensity[1]=.50f;
            moonlight.color_intensity[2]=.75f;
            moonlight.color_intensity[3]=float(
                sky.settings.moon_intensity*sky.moon_illumination*
                (1.-sky.daylight)*.45);
            moonlight.direction_cone[0]=-sky.moon_direction.x;
            moonlight.direction_cone[1]=-sky.moon_direction.y;
            moonlight.direction_cone[2]=-sky.moon_direction.z;
            moonlight.direction_cone[3]=NORTHSTARCPP_LIGHT_DIRECTIONAL_TAG;
            scene_lights.push_back(moonlight);
        }
        NorthStarRenderLight ambient{};
        ambient.position_range[3]=1.f;
        ambient.color_intensity[0]=float(.05+.27*sky.daylight);
        ambient.color_intensity[1]=float(.07+.35*sky.daylight);
        ambient.color_intensity[2]=float(.13+.46*sky.daylight);
        ambient.color_intensity[3]=1.f;
        ambient.direction_cone[1]=1.f;
        ambient.direction_cone[3]=NORTHSTARCPP_LIGHT_SKY_TAG;
        scene_lights.push_back(ambient);
    }
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
            if(!viewport_detail::submit_pick_streams(state->renderer,
                state->pick_v2_api,collect_pick,pick_complete,
                pick_triangles,pick_cube_entities))
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
        using MaterialStatus=viewport_detail::MaterialSubmitStatus;
        switch(viewport_detail::submit_material_streams(state->renderer,
            state->material_api,state->material_api_v2,
            state->shader_reload_api,state->shader_reload_v2,
            needs_material_v2,gpu_materials,gpu_materials_v2,material_keys)) {
        case MaterialStatus::ok:
            break;
        case MaterialStatus::missing_graph_provider:
            throw std::runtime_error("Render provider has no Material Graph/Instance ABI v2");
        case MaterialStatus::graph_upload_failed:
            throw state->module->error("Cannot upload authored Material Graph batches");
        case MaterialStatus::material_upload_failed:
            throw state->module->error("Cannot upload authored GPU materials");
        case MaterialStatus::shader_references_failed:
            throw state->module->error("Cannot bind material keys for shader reload");
        }
        if(state->rigid_api &&
           !state->rigid_api->set_instances(state->renderer,
               rigid_draws.empty()?nullptr:rigid_draws.data(),
               static_cast<std::uint32_t>(rigid_draws.size())))
            throw state->module->error("Cannot submit GPU rigid instances");
        state->prepare_ui_overlay(fps!=nullptr || !state->ui_document.nodes.empty());
        return state->api->draw(state->renderer,&frame)!=0;
    };
    if(!submit()) {
        if(state->module->provider()!="d3d12")
            throw state->module->error("Cannot render frame");
        state->fallback_to_d3d11();
        if(!submit())throw state->module->error("Cannot render frame after D3D12 fallback");
    }
    // Evict obsolete immutable D3D12 VBs/IBs after the current GPU frame
    // was submitted. The provider checks current draws and waits for its
    // queue fence once per batch before releasing GPU allocations.
    if(streaming && state->rigid_api && state->rigid_residency_api) {
        std::vector<std::uint64_t> stale_keys;
        std::vector<std::string> stale_paths;
        std::unordered_set<std::uint64_t> submitted_meshes;
        submitted_meshes.reserve(rigid_draws.size());
        for(auto const& instance:rigid_draws)
            submitted_meshes.insert(instance.mesh_key);
        bool const sector_unloaded=std::any_of(
            scene_world->cell_transitions().begin(),
            scene_world->cell_transitions().end(),
            [](SceneCellTransition const& t){return !t.loaded;});
        for(auto const& path:state->uploaded_rigid_meshes) {
            auto const key=std::uint64_t(std::hash<std::string>{}(path))|1ull;
            if(submitted_meshes.count(key)) {
                state->rigid_inactive_frames.erase(path);
                continue;
            }
            auto& idle=state->rigid_inactive_frames[path];
            if(idle<60)++idle;
            // Avoid mesh upload/eviction thrashing near view boundaries.
            if(!sector_unloaded && idle<60)continue;
            stale_keys.push_back(key);
            stale_paths.push_back(path);
            if(stale_keys.size()>=32)break; // bounded retirement per frame
        }
        if(!stale_keys.empty()) {
            if(state->rigid_residency_api->release_meshes(state->renderer,
                stale_keys.data(),std::uint32_t(stale_keys.size()))) {
                for(auto const& path:stale_paths) {
                    state->uploaded_rigid_meshes.erase(path);
                    state->rigid_inactive_frames.erase(path);
                }
            } else {
                std::fprintf(stderr,"[WARN] [scene.gpu] D3D12 mesh retirement rejected: %s\n",
                             state->module->error("GPU residency").what());
            }
        }
    }
    state->active_mesh_residency.clear();
    // Profiling is explicitly opt-in: the stable timing ABI waits for GPU
    // completion and would otherwise stall every production frame.
    static bool const gpu_profile_enabled=[] {
        auto const* flag=std::getenv("NORTHSTARCPP_SCENE_GPU_PROFILE");
        return flag && flag[0]=='1';
    }();
    if(gpu_profile_enabled && scene_world) {
        try {
            auto const gpu=gpu_frame_timing();
            if(gpu && gpu->total_gpu_ms<10000.0) {
                std::optional<std::uint64_t> rigid_bytes;
                if(auto const rigid=rigid_gpu_stats())
                    rigid_bytes=rigid->resident_geometry_bytes;
                auto& profiler=scene_world->cost_profiler();
                (void)profiler.record_gpu(profiler.newest_sequence(),
                    std::uint64_t(gpu->total_gpu_ms*1000.0),rigid_bytes);
            }
        }catch(std::exception const& e) {
            static bool warned=false;
            if(!warned) {
                std::fprintf(stderr,"[WARN] [scene.profiler] GPU sample: %s\n",e.what());
                warned=true;
            }
        }
    }
}


} // namespace northstarcpp
