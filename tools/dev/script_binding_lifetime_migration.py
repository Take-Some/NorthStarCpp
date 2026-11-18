#!/usr/bin/env python3
"""Atomic/backup guarded migration of ScriptRuntime context leases.

Each registered domain callback owns only weak session validity. Successful
hot reload revokes old leases; failed hot reload revokes only candidate.
"""
from pathlib import Path
import hashlib

root=Path(__file__).resolve().parents[2]
e=root/"engine/northstarcpp/modules/editor"
names=[
"window_script_domain_contracts.hpp",
"window_app_internal.hpp",
"window_app_state.hpp",
"window_script_bindings_host.cpp",
"window_runtime_script_lifecycle.inc",
]
before={n:(e/n).read_bytes() for n in names}
text={n:b.decode("utf-8").replace("\r\n","\n") for n,b in before.items()}
def change(n,a,b):
    assert text[n].count(a)==1,(n,a[:110],text[n].count(a))
    text[n]=text[n].replace(a,b,1)

domain="window_script_domain_contracts.hpp"
app="window_app_internal.hpp"
state="window_app_state.hpp"
host="window_script_bindings_host.cpp"
life="window_runtime_script_lifecycle.inc"

change(domain,
    '            runtime.bind(std::string(Tag::capability),std::string(Tag::names[i]),\n'
    '                         std::move(callbacks_[i]));',
    '''            auto callback=std::move(callbacks_[i]);
            if(guarded_) {
                // Weak token prevents callback storage keeping a dead host alive.
                runtime.bind(std::string(Tag::capability),std::string(Tag::names[i]),
                    [lease=lifetime_,callback=std::move(callback)](std::string_view json) {
                        auto token=lease.lock();
                        if(!token || !token->valid())
                            throw std::out_of_range("Stale ScriptRuntime binding context");
                        return callback(json);
                    });
            } else {
                runtime.bind(std::string(Tag::capability),std::string(Tag::names[i]),
                             std::move(callback));
            }''')
change(domain,'    std::array<ScriptRuntime::ScriptHandler,count> callbacks_{};',
    '    std::array<ScriptRuntime::ScriptHandler,count> callbacks_{};\n'
    '    std::weak_ptr<ScriptBindingLease> lifetime_;\n'
    '    bool guarded_=false;')
change(domain,'struct ScriptDomainBindings final {\n',
    '''struct ScriptDomainBindings final {
    void set_lifetime(std::shared_ptr<ScriptBindingLease> const& lease) noexcept {
        scene.set_lifetime(lease);
        camera.set_lifetime(lease);
        ecs.set_lifetime(lease);
        input.set_lifetime(lease);
        player.set_lifetime(lease);
        audio.set_lifetime(lease);
    }
''')
change(app,'#include "window_runtime_script_contracts.hpp"',
          '#include "window_runtime_script_contracts.hpp"\n#include "window_script_lifetime.hpp"')
change(state,
    '    std::uint64_t active_script_context_epoch=0;',
    '''    std::uint64_t active_script_context_epoch=0;
    // Each ScriptRuntime receives an independent lease. During failed hot
    // reload the old context remains valid while the candidate is revoked.
    std::unordered_map<std::uint64_t,std::shared_ptr<ScriptBindingLease>>
        script_binding_leases;
    // Incremented only when a whole scene is replaced or torn down.
    std::uint64_t script_scene_generation=1;''')
change(host,
    '''    ScriptDomainBindings domains;
        auto const script_owner=++script_context_epoch;''',
    '''    ScriptDomainBindings domains;
        auto const script_owner=++script_context_epoch;
        auto lease=std::make_shared<ScriptBindingLease>();
        script_binding_leases.emplace(script_owner,lease);
        domains.set_lifetime(lease);''')
# Candidate and old context may be live simultaneously. Old stop callbacks
# must run before old epoch is revoked; failed candidate only is revoked.
change(life,
    '''            cleanup_script_spawns(active_script_context_epoch);
            active_script_context_epoch=script_context_epoch;''',
    '''            cleanup_script_spawns(active_script_context_epoch);
            if(auto it=script_binding_leases.find(active_script_context_epoch);
               it!=script_binding_leases.end()) {
                it->second->revoke();
                script_binding_leases.erase(it);
            }
            active_script_context_epoch=script_context_epoch;''')
change(life,
    '''            cleanup_script_spawns(script_context_epoch);
            if (old_animations && script_animations)''',
    '''            cleanup_script_spawns(script_context_epoch);
            if(auto it=script_binding_leases.find(script_context_epoch);
               it!=script_binding_leases.end()) {
                it->second->revoke();
                script_binding_leases.erase(it);
            }
            if (old_animations && script_animations)''')
change(life,
    '''        cleanup_script_spawns(0);
        active_script_context_epoch=0;''',
    '''        cleanup_script_spawns(0);
        // Stop event has already completed; all callbacks now become stale.
        for(auto& [epoch,lease]:script_binding_leases) {
            (void)epoch;
            lease->revoke();
        }
        script_binding_leases.clear();
        ++script_scene_generation;
        active_script_context_epoch=0;''')
change(life,
    '''        script_ecs=std::make_unique<EcsWorld>();
        script_ecs->load(scene);''',
    '''        // Even if initialize is called without an explicit stop, a new
        // scene cannot inherit stale ECS IDs from the previous scene.
        ++script_scene_generation;
        script_entity_indices.clear();
        script_handle_ids.clear();
        script_ecs=std::make_unique<EcsWorld>();
        script_ecs->load(scene);''')
# Save only if all sources remain unmodified; keep original-byte backups.
backup=root/"out/refactor_backups"
backup.mkdir(parents=True,exist_ok=True)
for n,b in before.items():
    if (e/n).read_bytes()!=b:raise RuntimeError("Concurrent modification: "+n)
for n,b in before.items():
    f=backup/("before_script_binding_lease_"+n+"_"+hashlib.sha256(b).hexdigest()[:10]+".original")
    if not f.exists():f.write_bytes(b)
for n,b in before.items():
    s=text[n]
    data=s.replace("\n","\r\n").encode("utf-8") if b"\r\n" in b else s.encode("utf-8")
    (e/n).write_bytes(data)
print("Script context leases integrated: 6 domains; hot reload success/failure; stop; scene generation")
