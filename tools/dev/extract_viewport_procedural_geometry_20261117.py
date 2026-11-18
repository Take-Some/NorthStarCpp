#!/usr/bin/env python3
"""Extract pure CPU procedural mesh generation; preserve provider contracts."""
from pathlib import Path
v=Path('engine/northstarcpp/modules/viewport/viewport.cpp')
cm=Path('engine/northstarcpp/CMakeLists.txt')
s=v.read_text(encoding='utf-8'); c=cm.read_text(encoding='utf-8')
anchor='    auto emit_procedural=[&](std::vector<NorthStarMaterialVertex>& dst,'
end='    auto focus = Vector3(0.f, 0.f, 0.f);'
assert s.count(anchor)==1
a=s.index(anchor)
z=s.index(end,a)
section=s[a:z]
assert 'for(std::size_t k=0;k<data.indices.size();k+=3)' in section
assert 'push_material(dst,world[idx],n,tu,tv);' in section
assert s.count('                emit_procedural(batch,geometry,object,cube);')==1
assert s.count('#include "viewport_frame_submission.hpp"')==1
assert c.count('\n    modules/viewport/viewport_frame_submission.cpp\n')==1
s=s[:a]+s[z:]
s=s.replace('                emit_procedural(batch,geometry,object,cube);',
'''                viewport_detail::append_procedural_geometry(
                    batch,geometry,object,cube,pick_triangles,
                    collect_pick,current_pick_entity,pick_complete);''',1)
s=s.replace('#include "viewport_frame_submission.hpp"',
            '#include "viewport_frame_submission.hpp"\n#include "viewport_procedural_geometry.hpp"',1)
c=c.replace('\n    modules/viewport/viewport_frame_submission.cpp\n',
            '\n    modules/viewport/viewport_frame_submission.cpp\n    modules/viewport/viewport_procedural_geometry.cpp\n',1)
v.write_text(s,encoding='utf-8')
cm.write_text(c,encoding='utf-8')
print('Extracted',len(section.splitlines()),'procedural mesh staging lines')
