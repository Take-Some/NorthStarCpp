#!/usr/bin/env python3
"""Move safe, read-mostly callbacks into independent scoped domain modules.

Each callback receives a typed state view / capability; no full App access.
Keeps the original argument validation and JSON serialization.
"""
from pathlib import Path
import hashlib

root=Path(__file__).resolve().parents[2]
folder=root/"engine/northstarcpp/modules/editor"
host=folder/"window_script_bindings_host.cpp"
header=folder/"window_script_domain_contracts.hpp"
module_names={d:folder/f"window_script_{d}_bindings.cpp"
    for d in ("scene","camera","ecs","player","audio")}
before=host.read_bytes()
h_before=header.read_bytes()
m_before={d:p.read_bytes() for d,p in module_names.items()}
s=before.decode("utf-8").replace("\r\n","\n")
h=h_before.decode("utf-8").replace("\r\n","\n")
mods={d:v.decode("utf-8").replace("\r\n","\n") for d,v in m_before.items()}
if "struct SceneCountStateView" in h:raise RuntimeError("Already migrated")
# Extract one complete method entry with the next distinct domain method
bounds={
  "scene":('domains.scene.add("objectCount",','domains.scene.add("setLayerVisible",'),
  "camera":('domains.camera.add("available",','domains.camera.add("setMode",'),
  "ecs":('domains.ecs.add("entities",','domains.ecs.add("queryMany",'),
  "player":('domains.player.add("motion",','domains.player.add("ground",'),
  "audio":('domains.audio.add("supportsVoices",','domains.audio.add("stopVoice",')
}
views={
"scene":'''        collect_scene_count_script_binding(domains.scene, SceneCountStateView{
            [this] { return preview ? preview->scene().objects.size() : std::size_t{0}; }
        });
''',
"camera":'''        collect_camera_available_script_binding(domains.camera, CameraAvailableStateView{
            [this] { return fps_camera!=nullptr; }
        });
''',
"ecs":'''        collect_ecs_entities_script_binding(domains.ecs, EcsEntitiesStateView{
            [this]() -> Scene const* { return preview ? &preview->scene() : nullptr; }
        });
''',
"player":'''        collect_player_motion_script_binding(domains.player, PlayerMotionStateView{
            &script_move_x, &script_move_z
        });
''',
"audio":'''        collect_audio_voices_script_binding(domains.audio, AudioVoicesStateView{
            &script_voice_audio
        });
'''
}
body={}
for d,(a,b) in bounds.items():
    start=s.index("        "+a)
    end=s.index("        "+b,start)
    chunk=s[start:end]
    assert chunk.count(f'domains.{d}.add(')==1,(d,chunk.count('.add('))
    assert "});" in chunk
    body[d]=chunk.rstrip()
    s=s[:start]+views[d]+s[end:]
assert all(call in s for call in views.values())
# Typed state views: immutable snapshots for lightweight reads; references to
# owned live resources remain valid only during the App ScriptRuntime session.
declarations='''
// Domain-specific views expose only what a native callback needs. These
// App-free adapters hold no Win32/App field, and are not part of engine ABI.
struct SceneCountStateView final {
    std::function<std::size_t()> object_count;
};
struct CameraAvailableStateView final {
    std::function<bool()> available;
};
struct EcsEntitiesStateView final {
    std::function<Scene const*()> current_scene;
};
struct PlayerMotionStateView final {
    float const* move_x=nullptr;
    float const* move_z=nullptr;
};
struct AudioVoicesStateView final {
    std::unique_ptr<AudioEngine>* voice_audio=nullptr;
};
void collect_scene_count_script_binding(SceneScriptBindings&,SceneCountStateView);
void collect_camera_available_script_binding(CameraScriptBindings&,CameraAvailableStateView);
void collect_ecs_entities_script_binding(EcsScriptBindings&,EcsEntitiesStateView);
void collect_player_motion_script_binding(PlayerScriptBindings&,PlayerMotionStateView);
void collect_audio_voices_script_binding(AudioScriptBindings&,AudioVoicesStateView);

'''
needle='struct ScriptDomainBindings final {'
assert needle in h
h=h.replace(needle,declarations+needle,1)
h=h.replace('#include <northstarcpp/input.hpp>',
 '#include <northstarcpp/input.hpp>\n#include <northstarcpp/audio.hpp>\n'
 '#include <northstarcpp/scene.hpp>\n#include <functional>\n#include <memory>',1)
# The actual lambda code is moved, retaining validation and return types.
replace_by_domain={
"scene":[('[this]','[view]'),
   ('preview ? preview->scene().objects.size() : 0u','view.object_count()')],
"camera":[('[this]','[view]'),
    ('fps_camera ? std::string("true") : std::string("false")','view.available() ? std::string("true") : std::string("false")')],
"ecs":[('[this]','[view]'),
    ('if (preview) for (auto const& object:preview->scene().objects)', 'if (auto const* scene=view.current_scene()) for (auto const& object:scene->objects)'),
    ('script_json_quote(', 'runtime_project_files::script_json_quote(')],
"player":[('[this]','[view]'),
    ('script_move_x','*view.move_x'),('script_move_z','*view.move_z')],
"audio":[('[this]','[view]'),
    ('if(!script_voice_audio)', 'if(!*view.voice_audio)'),
    ('script_voice_audio=std::make_unique<AudioEngine>()', '*view.voice_audio=std::make_unique<AudioEngine>()'),
    ('script_voice_audio->', '(*view.voice_audio)->')]
}
for d in body:
    chunk=body[d]
    for a,b in replace_by_domain[d]:
        assert a in chunk,(d,a)
        chunk=chunk.replace(a,b)
    # Using explicit public script-contract helpers rather than private App alias.
    chunk=chunk.replace('script_args(', 'runtime_script_contracts::script_args(')
    chunk=chunk.replace(f'domains.{d}.add(', 'bindings.add(')
    assert 'domains.' not in chunk and '[this]' not in chunk
    fn={
     "scene": "collect_scene_count_script_binding",
     "camera":"collect_camera_available_script_binding",
     "ecs":"collect_ecs_entities_script_binding",
     "player":"collect_player_motion_script_binding",
     "audio":"collect_audio_voices_script_binding"}[d]
    typ={"scene":"SceneCountStateView","camera":"CameraAvailableStateView",
         "ecs":"EcsEntitiesStateView","player":"PlayerMotionStateView",
         "audio":"AudioVoicesStateView"}[d]
    bname=d.capitalize()+"ScriptBindings"
    if d in ("scene","camera","ecs"):
        validity=f"if(!view.{ {'scene':'object_count','camera':'available','ecs':'current_scene'}[d]})"
    elif d=="player":validity="if(!view.move_x || !view.move_z)"
    else:validity="if(!view.voice_audio)"
    func=(f"void {fn}({bname}& bindings, {typ} view) {{\n"
          f"    {validity} throw std::invalid_argument(\"Invalid {d} ScriptRuntime view\");\n"
          +chunk+"\n}\n\n")
    x=mods[d]
    assert "void register_"+d+"_script_bindings" in x
    x=x.replace("void register_"+d+"_script_bindings",func+"void register_"+d+"_script_bindings",1)
    if d in ("scene","camera","ecs","player","audio"):
        x=x.replace('#include "window_script_domain_contracts.hpp"',
        '#include "window_script_domain_contracts.hpp"\n'
        '#include "window_runtime_script_contracts.hpp"\n'
        '#include "window_runtime_project_files.hpp"\n'
        '#include <stdexcept>\n#include <string>',1)
    if d=="audio":x=x.replace('#include <string>','#include <string>\n#include <memory>',1)
    mods[d]=x
# Retain the script-domain forward declarations for callback lifetime safety:
# the App-owned closure remains attached to ScriptRuntime, as before.
backup=root/"out/refactor_backups"/("window_bindings_before_scoped_views_"+hashlib.sha256(before).hexdigest()[:12]+".original")
backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():backup.write_bytes(before)
assert host.read_bytes()==before and header.read_bytes()==h_before
assert all(module_names[d].read_bytes()==m_before[d] for d in mods)
host.write_text(s,encoding="utf-8")
header.write_text(h,encoding="utf-8")
for d,v in mods.items():module_names[d].write_text(v,encoding="utf-8")
print("App-free native state-backed callbacks extracted:",list(body))
print("host bytes",len(before),"->",host.stat().st_size)
