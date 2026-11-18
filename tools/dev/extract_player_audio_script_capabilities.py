#!/usr/bin/env python3
"""Extract the remaining Player/Audio App callbacks into scoped native services."""
from pathlib import Path
import hashlib,re
root=Path(__file__).resolve().parents[2];folder=root/"engine/northstarcpp/modules/editor"
paths=[folder/x for x in ("window_script_bindings_host.cpp",
"window_script_player_bindings.cpp","window_script_audio_bindings.cpp",
"window_script_domain_contracts.hpp")]
old={p:p.read_bytes() for p in paths}
norm=lambda data:data.decode("utf-8").replace("\r\n","\n")
host,player,audio,hdr=[norm(old[p]) for p in paths]
assert "PlayerDomainCapabilities" not in hdr
assert "AudioDomainCapabilities" not in hdr

pa=host.index('        domains.player.add("controlledEntity",')
pb=host.index('        runtime.bind("engine.physics","sweepSphere"',pa)
pbody=host[pa:pb]
assert pbody.count('domains.player.add(')==11
assert pbody.count('[this]')==11

aa=host.index('        domains.audio.add("playNac",')
ab=host.index('        // Optional voice controls;',aa)
ac=host.index('        domains.audio.add("stopVoice",',ab)
ad=host.index('        runtime.bind("engine.animation","play"',ac)
abody=host[aa:ab]+host[ac:ad]
assert abody.count('domains.audio.add(')==5
assert abody.count('[this]')==5

decl='''// Player-only live state, no App or Win32 implementation detail.
struct PlayerDomainCapabilities final {
    std::unique_ptr<PhysicsScene> const* physics_slot=nullptr;
    std::unique_ptr<CameraDirector> const* camera_slot=nullptr;
    bool const* scripted_game=nullptr;
    CameraSettings* camera_settings=nullptr;
    float* eye_height=nullptr;
    float* move_x=nullptr;
    float* move_z=nullptr;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale Player scene generation");
    }
    PhysicsScene* physics() const {require_scene();return physics_slot?physics_slot->get():nullptr;}
    CameraDirector* camera() const {require_scene();return camera_slot?camera_slot->get():nullptr;}
};
void collect_player_runtime_script_bindings(PlayerScriptBindings&,PlayerDomainCapabilities);

// Audio-only live resources; lazily create the same provider instances as
// before without capturing the complete host object.
struct AudioDomainCapabilities final {
    std::unique_ptr<AssetStore> const* assets_slot=nullptr;
    std::unique_ptr<AssetManager> const* fps_assets_slot=nullptr;
    std::unordered_map<std::string,std::vector<nac::Track>>* nac_cache_slot=nullptr;
    std::unordered_map<std::string,FpsAudioClip>* clip_cache_slot=nullptr;
    std::unique_ptr<AudioEngine>* voice_audio_slot=nullptr;
    std::unique_ptr<AudioOutput>* output_slot=nullptr;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale Audio scene generation");
    }
    AssetStore* assets() const {require_scene();return assets_slot?assets_slot->get():nullptr;}
    AssetManager* fps_assets() const {require_scene();return fps_assets_slot?fps_assets_slot->get():nullptr;}
    auto& nac_cache() const {require_scene();return *nac_cache_slot;}
    auto& clip_cache() const {require_scene();return *clip_cache_slot;}
    auto& voice_audio() const {require_scene();return *voice_audio_slot;}
    auto& output() const {require_scene();return *output_slot;}
};
void collect_audio_runtime_script_bindings(AudioScriptBindings&,AudioDomainCapabilities);

'''
hdr=hdr.replace('struct ScriptDomainBindings final {',decl+'struct ScriptDomainBindings final {',1)
hdr=hdr.replace('#include <northstarcpp/assets.hpp>',
    '#include <northstarcpp/assets.hpp>\n'
    '#include <northstarcpp/fps_assets.hpp>\n'
    '#include <northstarcpp/nac.hpp>\n'
    '#include <vector>',1)

pbody=pbody.replace('domains.player.add(','bindings.add(').replace('[this]','[service]')
for oldword,newword in {
    'preview':'service.physics()',
    'fps_camera':'service.camera()',
    'scripted_game':'*service.scripted_game',
    'fps_camera_settings':'*service.camera_settings',
    'script_eye_height':'*service.eye_height',
    'script_move_x':'*service.move_x',
    'script_move_z':'*service.move_z',
    'script_args':'runtime_script_contracts::script_args',
    'script_number':'runtime_script_contracts::script_number',
    'script_index':'runtime_script_contracts::script_index',
    'script_json_quote':'runtime_project_files::script_json_quote',
}.items():
    pbody=re.sub(r'\b'+oldword+r'\b',newword,pbody)
assert pbody.count('bindings.add(')==11
assert '[this]' not in pbody and 'domains.player.add(' not in pbody
player_fn='''void collect_player_runtime_script_bindings(
    PlayerScriptBindings& bindings, PlayerDomainCapabilities service) {
    if(!service.physics_slot || !service.camera_slot || !service.scripted_game ||
       !service.camera_settings || !service.eye_height || !service.move_x ||
       !service.move_z || !service.scene_generation)
        throw std::invalid_argument("Invalid Player domain capabilities");
'''+pbody.rstrip()+'\n}\n\n'
player=player.replace('void register_player_script_bindings(',
    player_fn+'void register_player_script_bindings(',1)
player=player.replace('#include "window_script_domain_contracts.hpp"',
'''#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <northstarcpp/player_camera_runtime.hpp>
#include <northstarcpp/physics_surface.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
''',1)

abody=abody.replace('domains.audio.add(','bindings.add(').replace('[this]','[service]')
for oldword,newword in {
    'script_assets':'service.assets()',
    'script_nac_cache':'service.nac_cache()',
    'script_voice_audio':'service.voice_audio()',
    'fps_assets':'service.fps_assets()',
    'script_audio_cache':'service.clip_cache()',
    'script_args':'runtime_script_contracts::script_args',
    'script_number':'runtime_script_contracts::script_number',
    'script_index':'runtime_script_contracts::script_index',
}.items():
    abody=re.sub(r'\b'+oldword+r'\b',newword,abody)
# This exact mutable playback output was called "audio" in App; do not
# replace literal audio/ VFS paths or audio method names.
assert abody.count('if(!audio) audio=std::make_unique<AudioOutput>();')==1
abody=abody.replace('if(!audio) audio=std::make_unique<AudioOutput>();',
                    'if(!service.output()) service.output()=std::make_unique<AudioOutput>();')
assert abody.count('audio->play(pcm)')==1
abody=abody.replace('audio->play(pcm)','service.output()->play(pcm)')
assert abody.count('bindings.add(')==5
assert '[this]' not in abody and 'domains.audio.add(' not in abody
audio_fn='''void collect_audio_runtime_script_bindings(
    AudioScriptBindings& bindings, AudioDomainCapabilities service) {
    if(!service.assets_slot || !service.fps_assets_slot ||
       !service.nac_cache_slot || !service.clip_cache_slot ||
       !service.voice_audio_slot || !service.output_slot ||
       !service.scene_generation)
        throw std::invalid_argument("Invalid Audio domain capabilities");
'''+abody.rstrip()+'\n}\n\n'
audio=audio.replace('void register_audio_script_bindings(',
    audio_fn+'void register_audio_script_bindings(',1)
audio=audio.replace('#include "window_script_domain_contracts.hpp"',
'''#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include <northstarcpp/nac.hpp>
#include <northstarcpp/audio.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
namespace fs=std::filesystem;
''',1)
host=host[:pa]+'''        collect_player_runtime_script_bindings(domains.player,
            PlayerDomainCapabilities{&preview,&fps_camera,&scripted_game,
                &fps_camera_settings,&script_eye_height,&script_move_x,
                &script_move_z,&script_scene_generation,script_scene_generation});
'''+host[pb:]
# Re-locate audio regions after Player replacement.
aa=host.index('        domains.audio.add("playNac",')
ab=host.index('        // Optional voice controls;',aa)
ac=host.index('        domains.audio.add("stopVoice",',ab)
ad=host.index('        runtime.bind("engine.animation","play"',ac)
audio_call='''        collect_audio_runtime_script_bindings(domains.audio,
            AudioDomainCapabilities{&script_assets,&fps_assets,
                &script_nac_cache,&script_audio_cache,&script_voice_audio,
                &audio,&script_scene_generation,script_scene_generation});
'''
host=host[:ac]+host[ad:]
host=host[:aa]+audio_call+host[ab:]
assert 'domains.player.add(' not in host and 'domains.audio.add(' not in host
assert player.count('bindings.add(')==12 and audio.count('bindings.add(')==6
for p in paths:
    if p.read_bytes()!=old[p]:raise RuntimeError("Concurrent edit "+str(p))
backup=root/"out/refactor_backups";backup.mkdir(parents=True,exist_ok=True)
for p,data in zip(paths,(host,player,audio,hdr)):
    prev=old[p]
    bak=backup/("before_player_audio_capabilities_"+p.name+"_"+hashlib.sha256(prev).hexdigest()[:10]+".original")
    if not bak.exists():bak.write_bytes(prev)
    p.write_bytes(data.replace("\n","\r\n").encode() if b"\r\n" in prev else data.encode())
print("Player 11 and Audio 5 callbacks moved: all 69 domain callbacks are App independent")
