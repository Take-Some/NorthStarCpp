#!/usr/bin/env python3
"""Extract 7 ECS native callbacks and reject stale stable IDs/handles."""
from pathlib import Path
import hashlib,re

root=Path(__file__).resolve().parents[2];folder=root/"engine/northstarcpp/modules/editor"
paths=[folder/"window_script_bindings_host.cpp",folder/"window_script_ecs_bindings.cpp",
       folder/"window_script_domain_contracts.hpp"]
old={p:p.read_bytes() for p in paths}
norm=lambda x:x.decode().replace("\r\n","\n")
host,mod,hdr=[norm(old[p]) for p in paths]
a=host.index('        domains.ecs.add("queryMany"')
b=host.index('        // Bind scoped capabilities',a)
body=host[a:b]
assert body.count('domains.ecs.add(')==7
assert body.count('[this]')==7
assert 'EcsDomainCapabilities' not in hdr

hdr=hdr.replace('#include <northstarcpp/scene.hpp>',
    '#include <northstarcpp/scene.hpp>\n#include <northstarcpp/ecs.hpp>\n'
    '#include <northstarcpp/physics.hpp>\n#include <unordered_map>\n#include <optional>',1)
declaration='''// Narrow live ECS capability. No App fields or App pointer cross this ABI.
struct EcsDomainCapabilities final {
    std::unique_ptr<PhysicsScene> const* physics_slot=nullptr;
    std::unique_ptr<EcsWorld> const* ecs_slot=nullptr;
    std::unordered_map<std::uint64_t,std::size_t> const* indices=nullptr;
    std::unordered_map<NsEntity,std::uint64_t> const* handles=nullptr;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale scene generation");
    }
    PhysicsScene* physics() const {
        require_scene();
        return physics_slot ? physics_slot->get() : nullptr;
    }
    EcsWorld* ecs() const {
        require_scene();
        return ecs_slot ? ecs_slot->get() : nullptr;
    }
    auto const& entity_indices() const {return *indices;}
    auto const& handle_ids() const {return *handles;}
    std::optional<std::size_t> find_live_index(std::uint64_t id) const {
        auto* world=ecs();
        auto* scene=physics();
        if(!id || !indices || !world || !scene)return std::nullopt;
        auto it=indices->find(id);
        if(it==indices->end())return std::nullopt;
        auto idx=it->second;
        auto const& objects=scene->scene().objects;
        if(idx>=objects.size() || idx>=world->size() ||
           objects[idx].node.id!=id ||
           (idx>=scene->authored_object_count() && !scene->runtime_alive(idx)))
            return std::nullopt;
        return idx;
    }
    std::size_t require_live_index(std::uint64_t id) const {
        auto index=find_live_index(id);
        if(!index)throw std::out_of_range("Stale or unknown scene entity ID");
        return *index;
    }
    bool live_handle(NsEntity handle,std::uint64_t id) const {
        auto idx=find_live_index(id);
        return idx && ecs()->at(*idx)==handle;
    }
};
void collect_ecs_runtime_script_bindings(EcsScriptBindings&,EcsDomainCapabilities);

'''
hdr=hdr.replace('struct ScriptDomainBindings final {',declaration+'struct ScriptDomainBindings final {',1)
body=body.replace('domains.ecs.add(','bindings.add(').replace('[this]','[service]')
for x,y in {
    'script_ecs':'service.ecs()','preview':'service.physics()',
    'script_entity_indices':'service.entity_indices()',
    'script_handle_ids':'service.handle_ids()',
    'script_args':'runtime_script_contracts::script_args',
    'script_index':'runtime_script_contracts::script_index',
    'script_number':'runtime_script_contracts::script_number',
    'script_json_quote':'runtime_project_files::script_json_quote'
}.items():
    body=re.sub(r'\b'+x+r'\b',y,body)
# Index-and-handle validation: IDs must match current scene objects, and
# runtime indices must remain live before any ECS world access.
q_orig='''                if(it==service.handle_ids().end() || !it->second) continue;'''
q_new='''                if(it==service.handle_ids().end() || !it->second ||
                   !service.live_handle(handle,it->second)) continue;'''
assert body.count(q_orig)==1
body=body.replace(q_orig,q_new,1)
q_orig='''                if (it==service.handle_ids().end() || !it->second) continue;'''
q_new='''                if (it==service.handle_ids().end() || !it->second ||
                    !service.live_handle(handle,it->second)) continue;'''
assert body.count(q_orig)==1
body=body.replace(q_orig,q_new,1)
# Split per callback for narrow validation rewrites.
names=("queryMany","query","has","get","getScript","getPosition","setPosition")
starts=[body.index(f'bindings.add("{x}"') for x in names]
chunks=[]
for i,x in enumerate(names):
    chunk=body[starts[i]:starts[i+1] if i+1<len(names) else len(body)]
    chunks.append((x,chunk))
new=[]
for x,chunk in chunks:
    if x=="has":
        find='''auto it=service.entity_indices().find(runtime_script_contracts::script_index(args[0]));
            if (it==service.entity_indices().end()) return std::string("false");'''
        repl='''auto const index=service.find_live_index(runtime_script_contracts::script_index(args[0]));
            if (!index) return std::string("false");'''
        assert find in chunk,x
        chunk=chunk.replace(find,repl).replace('service.ecs()->at(it->second)','service.ecs()->at(*index)')
    elif x in ("get","getScript","getPosition","setPosition"):
        var="found" if x=="get" else "it"
        find=f'''auto {var}=service.entity_indices().find(runtime_script_contracts::script_index(args[0]));
            if ({var}==service.entity_indices().end())
                throw std::out_of_range("Unknown stable scene entity ID");'''
        repl='''auto const index=service.require_live_index(runtime_script_contracts::script_index(args[0]));'''
        assert find in chunk,(x,chunk[:180])
        chunk=chunk.replace(find,repl)
        if x=="setPosition":
            chunk=chunk.replace('auto const idx=it->second;','auto const idx=index;')
        else:
            chunk=chunk.replace(f'{var}->second','index')
    new.append(chunk)
body=''.join(new)
assert body.count('bindings.add(')==7
assert all(x not in body for x in ('script_ecs','script_entity_indices','script_handle_ids'))
assert '[this]' not in body
fn='''void collect_ecs_runtime_script_bindings(
    EcsScriptBindings& bindings, EcsDomainCapabilities service) {
    if(!service.physics_slot || !service.ecs_slot || !service.indices ||
       !service.handles || !service.scene_generation)
        throw std::invalid_argument("Invalid ECS capabilities");
'''+body.rstrip()+'\n}\n\n'
mod=mod.replace('void register_ecs_script_bindings(',fn+'void register_ecs_script_bindings(',1)
mod=mod.replace('#include "window_script_domain_contracts.hpp"',
'''#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <northstarcpp/scene_pawn.hpp>
#include <cmath>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
''',1)
host=host[:a]+'''        collect_ecs_runtime_script_bindings(domains.ecs,
            EcsDomainCapabilities{&preview,&script_ecs,
                &script_entity_indices,&script_handle_ids,
                &script_scene_generation,script_scene_generation});
'''+host[b:]
assert "domains.ecs.add(" not in host
assert mod.count('bindings.add(')==8
for p in paths:
    if p.read_bytes()!=old[p]:raise RuntimeError("Concurrent modification: "+str(p))
backup=root/"out/refactor_backups";backup.mkdir(parents=True,exist_ok=True)
for p,data in zip(paths,(host,mod,hdr)):
    prev=old[p]
    dest=backup/("before_ecs_capabilities_"+p.name+"_"+hashlib.sha256(prev).hexdigest()[:10]+".original")
    if not dest.exists():dest.write_bytes(prev)
    p.write_bytes(data.replace("\n","\r\n").encode() if b"\r\n" in prev else data.encode())
print("7 ECS callbacks migrated, now 8/8 App-free; stable-ID, live-index and handle validity checked")
