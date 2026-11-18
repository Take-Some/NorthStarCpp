#!/usr/bin/env python3
"""Extract remaining camera script callbacks into an App-independent unit."""
from pathlib import Path
import hashlib,re
root=Path(__file__).resolve().parents[2]
base=root/"engine/northstarcpp/modules/editor"
paths=[base/x for x in ("window_script_bindings_host.cpp","window_script_camera_bindings.cpp","window_script_domain_contracts.hpp")]
old={p:p.read_bytes() for p in paths}
norm=lambda b:b.decode("utf-8").replace("\r\n","\n")
host,mod,hdr=map(lambda p:norm(old[p]),paths)
a=host.index('        domains.camera.add("setMode",')
b=host.index('        runtime.bind("engine.character","load"',a)
body=host[a:b]
assert body.count('domains.camera.add(')==22
assert body.count('[this]')==22
decl='''// App-free live camera capability.
struct CameraDomainCapabilities final {
    std::unique_ptr<CameraDirector> const* camera_slot=nullptr;
    std::unique_ptr<AssetStore> const* assets_slot=nullptr;
    bool const* first_person=nullptr;
    float* yaw=nullptr;
    float* pitch=nullptr;
    CameraDirector* camera() const noexcept {
        return camera_slot ? camera_slot->get() : nullptr;
    }
    AssetStore* assets() const noexcept {
        return assets_slot ? assets_slot->get() : nullptr;
    }
};
void collect_camera_runtime_script_bindings(CameraScriptBindings&,CameraDomainCapabilities);

'''
assert 'CameraDomainCapabilities' not in hdr
hdr=hdr.replace('struct ScriptDomainBindings final {',decl+'struct ScriptDomainBindings final {',1)
hdr=hdr.replace('#include <northstarcpp/scene.hpp>','#include <northstarcpp/scene.hpp>\n#include <northstarcpp/camera.hpp>\n#include <northstarcpp/assets.hpp>',1)
body=body.replace('domains.camera.add(','bindings.add(').replace('[this]','[service]')
for x,y in {
 'fps_camera':'service.camera()', 'script_assets':'service.assets()',
 'scripted_first_person':'*service.first_person','script_yaw':'*service.yaw',
 'script_pitch':'*service.pitch','script_args':'runtime_script_contracts::script_args',
 'script_number':'runtime_script_contracts::script_number',
 'script_json_quote':'runtime_project_files::script_json_quote'}.items():
    body=re.sub(r'\b'+x+r'\b',y,body)
fn='''void collect_camera_runtime_script_bindings(
    CameraScriptBindings& bindings, CameraDomainCapabilities service) {
    if(!service.camera_slot || !service.assets_slot || !service.first_person ||
       !service.yaw || !service.pitch)
        throw std::invalid_argument("Invalid camera capabilities");
'''+body+'\n}\n\n'
mod=mod.replace('void register_camera_script_bindings(',fn+'void register_camera_script_bindings(',1)
mod=mod.replace('#include "window_script_domain_contracts.hpp"',
'''#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
''',1)
host=host[:a]+'''        collect_camera_runtime_script_bindings(domains.camera,
            CameraDomainCapabilities{&fps_camera,&script_assets,
                &scripted_first_person,&script_yaw,&script_pitch});
'''+host[b:]
assert 'domains.camera.add(' not in host
assert mod.count('bindings.add(')==23
for p in paths:
    if p.read_bytes()!=old[p]:raise RuntimeError("Concurrent edit: "+str(p))
backup=root/"out/refactor_backups";backup.mkdir(parents=True,exist_ok=True)
for p,data in zip(paths,(host,mod,hdr)):
    b=old[p]
    target=backup/("before_camera_capabilities_"+p.name+"_"+hashlib.sha256(b).hexdigest()[:10]+".original")
    if not target.exists():target.write_bytes(b)
    p.write_bytes(data.replace("\n","\r\n").encode() if b"\r\n" in b else data.encode())
print("22 camera callbacks migrated; 23/23 now App independent")
