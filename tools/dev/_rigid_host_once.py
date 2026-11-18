from pathlib import Path
p=Path("engine/northstarcpp/modules/viewport/viewport.cpp")
s=p.read_text("utf-8")
def edit(before,after,expected=1):
 global s
 assert s.count(before)==expected,(before[:110],s.count(before))
 s=s.replace(before,after,1)
 print("viewport patch:",before[:54])
edit('#include <northstarcpp/render_materials_api.h>',
     '#include <northstarcpp/render_materials_api.h>\n#include <northstarcpp/render_rigid_instances_api.h>')
edit('''        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);''',
'''        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        rigid_api=module->query_extension<NorthStarRenderRigidInstancesApi>(
            NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI);
        if(rigid_api && (rigid_api->abi_version!=
            NORTHSTARCPP_RENDER_RIGID_INSTANCES_ABI ||
            rigid_api->struct_size<sizeof(NorthStarRenderRigidInstancesApi) ||
            !rigid_api->upload_mesh || !rigid_api->clear_meshes ||
            !rigid_api->set_instances))rigid_api=nullptr;''',expected=2)
# Fall-back method repeats material_api_v2 query, but we require exact
# scope replacement to avoid touching the constructor again.
edit('''        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        shader_reload_api=module->query_extension<NorthStarRenderShaderReloadApi>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI);''',
'''        material_api_v2=module->query_extension<NorthStarRenderMaterialsApiV2>(
            NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI);
        rigid_api=nullptr;
        uploaded_rigid_meshes.clear();
        rejected_rigid_meshes.clear();
        shader_reload_api=module->query_extension<NorthStarRenderShaderReloadApi>(
            NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI);''')
edit('''    NorthStarRenderMaterialsApiV2 const* material_api_v2=nullptr;''',
'''    NorthStarRenderMaterialsApiV2 const* material_api_v2=nullptr;
    NorthStarRenderRigidInstancesApi const* rigid_api=nullptr;
    std::unordered_set<std::string> uploaded_rigid_meshes;
    std::unordered_set<std::string> rejected_rigid_meshes;
    std::vector<NorthStarRigidInstance> rigid_draw_scratch;
    std::vector<NorthStarMaterialVertex> rigid_upload_vertices;''')
edit('''    // Stop and join the old project's decode worker BEFORE destroying its VFS.
    // A new project never sees stale results from the previous asset mount.''',
'''    // Old project keys must not alias a new project's GPU geometry.
    if(state->rigid_api &&
       !state->rigid_api->clear_meshes(state->renderer))
        throw state->module->error("Cannot reset rigid GPU mesh residency");
    state->uploaded_rigid_meshes.clear();
    state->rejected_rigid_meshes.clear();
    // Stop and join the old project's decode worker BEFORE destroying its VFS.
    // A new project never sees stale results from the previous asset mount.''')
# Start of per-frame staging
edit('''    auto& skin_batches=state->skin_batches;
    skin_batches.begin_frame();''',
'''    auto& skin_batches=state->skin_batches;
    skin_batches.begin_frame();
    auto& rigid_draws=state->rigid_draw_scratch;
    rigid_draws.clear();''')
# Before submit_range for non-skinned whole mesh. Only opaque, no shader
# overrides or special alpha, graphics scene rig or editor GPU pick.
edit('''                if(!material.empty()) {
                    if(!state->material_api && !state->material_api_v2)''',
'''                // Only original opaque static mesh sections can use the
                // immutable indexed GPU path. Transparencies, custom graph,
                // skinned, editor-picking and external providers use the
                // original full-resolution triangle stream unchanged.
                if(state->rigid_api && !gpu_skin_pose && !fps_skin_mask &&
                   !collect_pick && !gizmo && !material.empty() &&
                   mesh.indices.size()<=NORTHSTARCPP_RIGID_MAX_MESH_INDICES &&
                   mesh.vertices.size()<=NORTHSTARCPP_RIGID_MAX_MESH_VERTICES &&
                   !state->rejected_rigid_meshes.count(resource_path)) {
                    auto mat=state->cached_materials.find(material);
                    if(mat!=state->cached_materials.end()) {
                        auto const& pbr=mat->second;
                        if(pbr.blend_mode==0 && pbr.opacity>=.999f &&
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
                                        " retaining full-detail CPU renderer\\n",
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
                    if(!state->material_api && !state->material_api_v2)''')
# Submit after ordinary material streams but before draw()
edit('''        state->prepare_ui_overlay(fps!=nullptr || !state->ui_document.nodes.empty());
        return state->api->draw(state->renderer,&frame)!=0;''',
'''        if(state->rigid_api &&
           !state->rigid_api->set_instances(state->renderer,
               rigid_draws.empty()?nullptr:rigid_draws.data(),
               static_cast<std::uint32_t>(rigid_draws.size())))
            throw state->module->error("Cannot submit GPU rigid instances");
        state->prepare_ui_overlay(fps!=nullptr || !state->ui_document.nodes.empty());
        return state->api->draw(state->renderer,&frame)!=0;''')
p.write_text(s,"utf-8")
print("Viewport integrated: provider query, full-detail fallback, project reset, GPU rigid instances")
