#!/usr/bin/env python3
"""Move the actual Viewport::draw method into a compiled .cpp.

Makes an internal header containing the shared pimpl State and its helper types.
No public Viewport API changes; source must match established extraction markers.
Does not modify the old legacy .inc until the new TU has built and passed.
"""
from pathlib import Path
import hashlib

repo=Path(__file__).resolve().parents[2]
dir=repo/"engine/northstarcpp/modules/viewport"
original=dir/"viewport.cpp"
legacy=dir/"viewport_draw.inc"
private=dir/"viewport_internal.hpp"
compiled=dir/"viewport_draw.cpp"
for name in (private,compiled):
    if name.exists():
        raise RuntimeError(f"Already split: {name}")
data=original.read_bytes()
old=legacy.read_bytes()
nl=b"\r\n" if b"\r\n" in data else b"\n"
canon=lambda x:x.replace(b"\r\n",b"\n")
s=canon(data)
draw=canon(old)
start=s.index(b"namespace northstarcpp {\n")
end=s.index(b'#include "viewport_state.inc"', start)
endline=s.index(b"\n",end)+1
definitions=s[start:end]
assert definitions.startswith(b"namespace northstarcpp {\n\nnamespace {\n")
assert definitions.endswith(b"}\n\n")
# Promote anonymous helper Character and provider guards to one internal
# named namespace. Definitions are inline to keep State ODR-identical.
definitions=definitions.replace(b"namespace {\n",b"namespace viewport_detail {\n",1)
definitions=definitions.replace(b"std::vector<std::string> renderer_requirements_from_environment()",b"inline std::vector<std::string> renderer_requirements_from_environment()",1)
definitions=definitions.replace(b"void require_provider_render_features(",b"inline void require_provider_render_features(",1)
definitions=definitions[:-3]+b"} // namespace viewport_detail\n"
defs_uses=(b"using viewport_detail::Character;\n"
           b"using viewport_detail::renderer_requirements_from_environment;\n"
           b"using viewport_detail::require_provider_render_features;\n\n")
# Includes are shared so the out-of-class draw definition and inline state
# constructor see identical full types, provider ABI and internal helpers.
internal=b"#pragma once\n"+s[:start]+definitions+b"\n"+defs_uses+b'#include "viewport_state.inc"\n} // namespace northstarcpp\n'
anchor=b"// Optional per-entity authoring component uses one of the native YTD\n"
begin_draw=s.index(anchor,endline)
end_draw=s.index(b'#include "viewport_draw.inc"',begin_draw)
end_drawline=s.index(b"\n",end_draw)+1
support=s[begin_draw:end_draw]
assert b"static std::uint32_t world_surface_category" in support
assert b"void Viewport::draw(" in draw
new_cpp=b'#include "viewport_internal.hpp"\n\nnamespace northstarcpp {\n'+support+draw+b"\n} // namespace northstarcpp\n"
# Remove the old Class State and draw implementation from viewport.cpp entirely.
new_main=b'#include "viewport_internal.hpp"\n\nnamespace northstarcpp {\n'+s[endline:begin_draw]+s[end_drawline:]
assert new_main.count(b"Viewport::Viewport(")==1
assert new_main.count(b"void Viewport::draw(")==0
assert new_cpp.count(b"void Viewport::draw(")==1
assert internal.count(b"struct Viewport::State")==0   # State is still included once
assert new_main.rstrip().endswith(b"} // namespace northstarcpp")
# Preserve original input for byte-exact rollback in the pre-existing QA backup area.
backup=repo/"out/refactor_backups"/("viewport_pre_compiled_draw_"+
        hashlib.sha256(data).hexdigest()[:12]+".original")
backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():backup.write_bytes(data)
encode=lambda x: x.replace(b"\n",nl) if nl==b"\r\n" else x
private.write_bytes(encode(internal))
compiled.write_bytes(encode(new_cpp))
if original.read_bytes()!=data:
    raise RuntimeError("Concurrent viewport.cpp change detected; source not overwritten")
original.write_bytes(encode(new_main))
print(f"Viewport::draw compiled source: {len(new_cpp)} bytes, internal state header: {len(internal)} bytes")
print(f"viewport.cpp: {len(data)} -> {len(encode(new_main))}; backup {backup}")
