from pathlib import Path
src=Path('engine/northstarcpp/modules/viewport/viewport.cpp')
cm=Path('engine/northstarcpp/CMakeLists.txt')
s=src.read_text(encoding='utf-8')
c=cm.read_text(encoding='utf-8')
def rep(old,new):
 global s
 assert s.count(old)==1, (old[:95],s.count(old))
 s=s.replace(old,new,1)
def cut(start,end,replacement=''):
 global s
 assert s.count(start)==1,(start,s.count(start))
 a=s.index(start)
 z=s.index(end,a)
 assert z>a,(start,end)
 s=s[:a]+replacement+s[z:]
rep('#include "viewport_frame_scratch.hpp"',
    '#include "viewport_frame_scratch.hpp"\n#include "viewport_frame_builder.hpp"')
rep('    viewport_detail::ViewportFrameScratch frame_scratch;',
    '    viewport_detail::ViewportFrameScratch frame_scratch;\n    viewport_detail::ViewportFrameBuilder frame_builder;')
rep('    state->frame_scratch.reset_project();',
    '    state->frame_scratch.reset_project();\n    state->frame_builder.reset_project();')
cut('    struct FirstPersonView {','    for(auto it=state->native_playback.begin();',
    '    auto& frame_index=state->frame_builder;\n'
    '    frame_index.prepare(scene,state->world_surfaces_ready,camera_pose && !gizmo);\n\n')
rep('        if(!active_entities.count(it->first))',
    '        if(!frame_index.is_active(it->first))')
cut('    // Presentation-only. The same persistent Entity/YDD/YMT/YCD is',
    '    for(auto it=state->native_playback.begin();')
cut('    std::unordered_map<std::uint64_t,std::uint32_t> surface_overrides;',
    '    auto& mesh_vertices=state->mesh_vertices;')
cut('    // Authored object materials remain a separate triangle stream with',
    '    // Native drawable sections carry their own YMAT references; they must')
rep('    if((!assigned_materials.empty() || has_native_materials) &&',
    '    if((frame_index.has_materials() || has_native_materials) &&')
cut('    // Components are serialized by stable authored ID. This lookup is',
    '    auto focus = Vector3(0.f, 0.f, 0.f);')
rep('        auto const view=fps_visuals.find(object.node.id);\n'
    '        bool const fps_arms=view!=fps_visuals.end() && camera_pose;\n'
    '        bool const fps_body=fps_arms && view->second.full_body;',
    '        auto const* view=frame_index.first_person(object.node.id);\n'
    '        bool const fps_arms=view && camera_pose;\n'
    '        bool const fps_body=fps_arms && view->full_body;')
rep('view->second.forward','view->forward')
rep('view->second.drop','view->drop')
rep('        auto const material_it=assigned_materials.find(object.node.id);\n'
    '        bool const has_material=material_it!=assigned_materials.end();',
    '        auto const* material_ref=frame_index.material(object.node.id);\n'
    '        bool const has_material=material_ref!=nullptr;')
rep('        auto const native_shape=procedural.find(object.node.id);\n'
    '        if(state->triangle_api && native_shape!=procedural.end() &&',
    '        auto const* native_shape=frame_index.procedural(object.node.id);\n'
    '        if(state->triangle_api && native_shape &&')
rep('        if(resource_path.empty() && gizmo &&\n'
    '           character_entities.count(object.node.id) &&\n'
    '           native_shape==procedural.end()) {',
    '        if(resource_path.empty() && gizmo &&\n'
    '           character_entities.count(object.node.id) &&\n'
    '           !native_shape) {')
assert s.count('material_vertices(material_it->second)')==2
s=s.replace('material_vertices(material_it->second)','material_vertices(*material_ref)')
rep('            auto const over=surface_overrides.find(object.node.id);\n'
    '            auto const id=over!=surface_overrides.end()?over->second:',
    '            auto const* over=frame_index.surface(object.node.id);\n'
    '            auto const id=over?*over:')
assert c.count('    modules/viewport/viewport.cpp')==1
c=c.replace('    modules/viewport/viewport.cpp',
            '    modules/viewport/viewport.cpp\n    modules/viewport/viewport_frame_builder.cpp',1)
src.write_text(s,encoding='utf-8')
cm.write_text(c,encoding='utf-8')
print('Viewport updated, remaining draw begins at line',s[:s.index('void Viewport::draw(')].count('\n')+1)
