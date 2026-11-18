#!/usr/bin/env python3
"""Extract all App-dependent Scene handlers into typed Scene capabilities."""
from pathlib import Path
import hashlib,re
root=Path(__file__).resolve().parents[2];folder=root/"engine/northstarcpp/modules/editor"
paths=[folder/"window_script_bindings_host.cpp",folder/"window_script_scene_bindings.cpp",
       folder/"window_script_domain_contracts.hpp"]
old={p:p.read_bytes() for p in paths}
norm=lambda b:b.decode().replace("\r\n","\n")
host,mod,hdr=[norm(old[p]) for p in paths]
first_a=host.index('        domains.scene.add("spawn",')
first_b=host.index('        runtime.bind("engine.prefab","read"',first_a)
first=host[first_a:first_b]
assert first.count('domains.scene.add(')==4
second_a=host.index('        domains.scene.add("setLayerVisible",')
second_b=host.index('        // Runtime VFX is independent',second_a)
second=host[second_a:second_b]
assert second.count('domains.scene.add(')==1
third_a=host.index('        domains.scene.add("objects",')
third_b=host.index('        runtime.bind("engine.ui","setTitle"',third_a)
third=host[third_a:third_b]
assert third.count('domains.scene.add(')==9
body=first+second+third
assert body.count('domains.scene.add(')==14
assert 'SceneDomainCapabilities' not in hdr

decl='''// Live, scene-owned capabilities, independent of App and Win32.
class Viewport;
struct SceneDomainCapabilities final {
    std::unique_ptr<PhysicsScene> const* physics_slot=nullptr;
    std::unique_ptr<EcsWorld> const* ecs_slot=nullptr;
    std::unique_ptr<AssetStore> const* assets_slot=nullptr;
    std::unique_ptr<Viewport> const* graphics_slot=nullptr;
    std::unique_ptr<CameraDirector> const* camera_slot=nullptr;
    CameraPose const* camera_pose=nullptr;
    std::unordered_map<std::uint64_t,std::size_t>* indices=nullptr;
    std::unordered_map<NsEntity,std::uint64_t>* handles=nullptr;
    std::unordered_map<std::uint64_t,NsEntity>* spawn_handles_slot=nullptr;
    std::unordered_map<std::uint64_t,std::uint64_t>* owners=nullptr;
    std::uint64_t* next_id=nullptr;
    std::unordered_map<std::size_t,std::uint32_t>* colors=nullptr;
    std::unordered_map<std::uint64_t,std::string>* animations=nullptr;
    std::unordered_map<std::uint64_t,std::string>* visual_modes=nullptr;
    std::unordered_map<std::uint64_t,std::string>* attachments=nullptr;
    std::uint64_t owner=0;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    std::function<void(std::uint64_t,std::uint64_t)> destroy_entity;
    std::function<bool()> layer_available;
    std::function<void(std::string const&,bool)> set_layer_visible;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale script scene generation");
    }
    PhysicsScene* physics() const {require_scene();return physics_slot?physics_slot->get():nullptr;}
    EcsWorld* ecs() const {require_scene();return ecs_slot?ecs_slot->get():nullptr;}
    AssetStore* assets() const {require_scene();return assets_slot?assets_slot->get():nullptr;}
    Viewport* graphics() const {require_scene();return graphics_slot?graphics_slot->get():nullptr;}
    CameraDirector* camera() const {require_scene();return camera_slot?camera_slot->get():nullptr;}
    auto& entity_indices() const {require_scene();return *indices;}
    auto& handle_ids() const {require_scene();return *handles;}
    auto& spawn_handles() const {require_scene();return *spawn_handles_slot;}
    auto& spawn_owners() const {require_scene();return *owners;}
    auto& object_colors() const {require_scene();return *colors;}
    auto& model_animations() const {require_scene();return *animations;}
    auto& model_visual_modes() const {require_scene();return *visual_modes;}
    auto& model_bone_attachments() const {require_scene();return *attachments;}
    std::size_t live_index(std::uint64_t id) const {
        auto const& map=entity_indices();
        auto it=map.find(id);
        if(it==map.end() || !physics())
            throw std::out_of_range("Stale script scene entity ID");
        auto index=it->second;
        auto* world=physics();
        if(index>=world->scene().objects.size() ||
           world->scene().objects[index].node.id!=id ||
           (index>=world->authored_object_count() && !world->runtime_alive(index)))
            throw std::out_of_range("Stale script scene entity ID");
        return index;
    }
};
void collect_scene_runtime_script_bindings(SceneScriptBindings&,SceneDomainCapabilities);

'''
hdr=hdr.replace('struct ScriptDomainBindings final {',decl+'struct ScriptDomainBindings final {',1)
body=body.replace('domains.scene.add(','bindings.add(').replace('[this,script_owner]','[service]').replace('[this]','[service]')
# Narrow-scoped streaming capability instead of the Win32/editor world type.
body=body.replace('scene_streaming.world()->set_layer_visible(','service.set_layer_visible(')
body=body.replace('scene_streaming.world()','service.layer_available()')
for x,y in {
    'destroy_script_spawn':'service.destroy_entity',
    'script_owner':'service.owner',
    'script_ecs':'service.ecs()','preview':'service.physics()',
    'script_assets':'service.assets()',
    'script_entity_indices':'service.entity_indices()',
    'script_handle_ids':'service.handle_ids()',
    'script_spawn_handles':'service.spawn_handles()',
    'script_spawn_owners':'service.spawn_owners()',
    'next_script_spawn_id':'*service.next_id',
    'script_object_colors':'service.object_colors()',
    'script_model_animations':'service.model_animations()',
    'script_model_visual_modes':'service.model_visual_modes()',
    'script_model_bone_attachments':'service.model_bone_attachments()',
    'graphics':'service.graphics()',
    'fps_camera':'service.camera()',
    'fps_pose':'*service.camera_pose',
    'script_args':'runtime_script_contracts::script_args',
    'script_index':'runtime_script_contracts::script_index',
    'script_number':'runtime_script_contracts::script_number',
    'script_json_quote':'runtime_project_files::script_json_quote'
}.items():
    body=re.sub(r'\b'+x+r'\b',y,body)
# Correct ownership was checked before index dereference; now check whether
# the mapped slot is still the same live entity.
assert body.count('service.entity_indices().at(parent)')==1
body=body.replace('service.entity_indices().at(parent)','service.live_index(parent)')
assert body.count('service.entity_indices().at(id)')==2
body=body.replace('service.entity_indices().at(id)','service.live_index(id)')
# Shared scene access must fail rather than dereference a dead host context.
assert body.count('bindings.add(')==14
assert '[this]' not in body
fn='''void collect_scene_runtime_script_bindings(
    SceneScriptBindings& bindings, SceneDomainCapabilities service) {
    if(!service.physics_slot || !service.ecs_slot || !service.assets_slot ||
       !service.graphics_slot || !service.camera_slot || !service.camera_pose ||
       !service.indices || !service.handles || !service.spawn_handles_slot ||
       !service.owners || !service.next_id || !service.colors ||
       !service.animations || !service.visual_modes || !service.attachments ||
       !service.scene_generation || !service.destroy_entity ||
       !service.layer_available || !service.set_layer_visible)
        throw std::invalid_argument("Invalid Scene domain capabilities");
'''+body.rstrip()+'\n}\n\n'
mod=mod.replace('void register_scene_script_bindings(',fn+'void register_scene_script_bindings(',1)
mod=mod.replace('#include "window_script_domain_contracts.hpp"',
'''#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include "viewport.hpp"
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/scene_animation.hpp>
#include <northstarcpp/math.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
''',1)
# Replace blocks backwards so indexes remain original for each subsequent.
new_call='''        collect_scene_runtime_script_bindings(domains.scene,
            SceneDomainCapabilities{
                &preview,&script_ecs,&script_assets,&graphics,&fps_camera,
                &fps_pose,&script_entity_indices,&script_handle_ids,
                &script_spawn_handles,&script_spawn_owners,&next_script_spawn_id,
                &script_object_colors,&script_model_animations,
                &script_model_visual_modes,&script_model_bone_attachments,
                script_owner,&script_scene_generation,script_scene_generation,
                [this](std::uint64_t id,std::uint64_t owner) {
                    destroy_script_spawn(id,owner);
                },
                [this] {return scene_streaming.world()!=nullptr;},
                [this](std::string const& name,bool visible) {
                    if(auto* world=scene_streaming.world())
                        world->set_layer_visible(name,visible);
                    else
                        throw std::out_of_range("Script world is unavailable");
                }
            });
'''
# Combine all callbacks at original spawn slot (registration order is set by tag).
host=host[:third_a]+host[third_b:]
host=host[:second_a]+host[second_b:]
host=host[:first_a]+new_call+host[first_b:]
assert 'domains.scene.add(' not in host
assert mod.count('bindings.add(')==15
for p in paths:
    if p.read_bytes()!=old[p]:raise RuntimeError("Concurrent edit: "+str(p))
backup=root/"out/refactor_backups";backup.mkdir(parents=True,exist_ok=True)
for p,data in zip(paths,(host,mod,hdr)):
    prev=old[p]
    dest=backup/("before_scene_capabilities_"+p.name+"_"+hashlib.sha256(prev).hexdigest()[:10]+".original")
    if not dest.exists():dest.write_bytes(prev)
    p.write_bytes(data.replace("\n","\r\n").encode() if b"\r\n" in prev else data.encode())
print("14 Scene callbacks moved; 15/15 now App-independent module with scoped mutation capabilities")
