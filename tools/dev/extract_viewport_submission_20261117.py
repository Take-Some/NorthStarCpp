from pathlib import Path
p=Path('engine/northstarcpp/modules/viewport/viewport.cpp')
cm=Path('engine/northstarcpp/CMakeLists.txt')
s=p.read_text(encoding='utf8');c=cm.read_text(encoding='utf8')
assert s.count('#include "viewport_frame_builder.hpp"')==1
assert 'viewport_frame_submission.hpp' not in s
s=s.replace('#include "viewport_frame_builder.hpp"',
            '#include "viewport_frame_builder.hpp"\n#include "viewport_frame_submission.hpp"',1)
a=s.index('        if(state->pick_v2_api) {',s.index('    auto submit=[&]'))
z=s.index('        if(state->lights_api)',a)
s=s[:a]+'''        if(state->pick_v2_api) {
            if(!viewport_detail::submit_pick_streams(state->renderer,
                state->pick_v2_api,collect_pick,pick_complete,
                pick_triangles,pick_cube_entities))
                throw state->module->error("Cannot submit entity ID picking streams");
            state->pick_scene_complete=collect_pick && pick_complete;
        }
'''+s[z:]
a=s.index('        if(needs_material_v2 && !state->material_api_v2)')
z=s.index('        state->prepare_ui_overlay(',a)
s=s[:a]+'''        using MaterialStatus=viewport_detail::MaterialSubmitStatus;
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
'''+s[z:]
assert c.count('\n    modules/viewport/viewport_frame_builder.cpp\n')==1
c=c.replace('\n    modules/viewport/viewport_frame_builder.cpp\n',
            '\n    modules/viewport/viewport_frame_builder.cpp\n    modules/viewport/viewport_frame_submission.cpp\n',1)
p.write_text(s,encoding='utf8')
cm.write_text(c,encoding='utf8')
print("Moved GPU picking and material provider submissions into separate .cpp")
