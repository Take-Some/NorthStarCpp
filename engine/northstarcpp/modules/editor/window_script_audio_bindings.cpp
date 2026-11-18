#include "window_script_domain_contracts.hpp"
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

#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <stdexcept>
#include <string>
#include <memory>
namespace northstarcpp::window_app_internal {
void collect_audio_voices_script_binding(AudioScriptBindings& bindings, AudioVoicesStateView view) {
    if(!view.voice_audio) throw std::invalid_argument("Invalid audio ScriptRuntime view");
        bindings.add("supportsVoices",[view](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty())
                throw std::invalid_argument("audio.supportsVoices()");
            if(!*view.voice_audio)
                *view.voice_audio=std::make_unique<AudioEngine>();
            return (*view.voice_audio)->supports_voices() ?
                std::string("true") : std::string("false");
        });
}

void collect_audio_runtime_script_bindings(
    AudioScriptBindings& bindings, AudioDomainCapabilities service) {
    if(!service.assets_slot || !service.fps_assets_slot ||
       !service.nac_cache_slot || !service.clip_cache_slot ||
       !service.voice_audio_slot || !service.output_slot ||
       !service.scene_generation)
        throw std::invalid_argument("Invalid Audio domain capabilities");
        bindings.add("playNac",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if((args.size()!=4 && args.size()!=7) || !service.assets())
                throw std::invalid_argument(
                    "audio.playNac(virtualNacPath,clipPrefix,variation,gain)");
            auto const& asset_path=args[0];
            auto const& prefix=args[1];
            if(asset_path.size()>256 || asset_path.rfind("audio/",0)!=0 ||
               fs::u8path(asset_path).extension()!=".nac" ||
               asset_path.find('\\')!=std::string::npos ||
               asset_path.find(':')!=std::string::npos ||
               prefix.size()>48 || prefix.empty() ||
               !std::all_of(prefix.begin(),prefix.end(),[](unsigned char c){
                   return std::isalnum(c) || c=='_';
               }))
                throw std::invalid_argument("Invalid NAC asset or clip prefix");
            for(auto const& segment:fs::u8path(asset_path))
                if(segment==".." || segment=="." || segment.empty())
                    throw std::invalid_argument("NAC path traversal is forbidden");
            auto const variation=runtime_script_contracts::script_index(args[2]);
            auto const gain=runtime_script_contracts::script_number(args[3]);
            if(variation>1000000 || gain<0 || gain>2)
                throw std::invalid_argument("Invalid NAC variation or gain");
            auto& cache=service.nac_cache();
            auto it=cache.find(asset_path);
            std::vector<nac::Track> transient;
            if(it==cache.end()) {
                constexpr std::size_t cache_clip_bytes=16u*1024*1024;
                if(service.assets()->size(asset_path)>nac::max_container_bytes)
                    throw std::length_error("NAC exceeds the encoded size supported by NEF8");
                auto data=service.assets()->read(asset_path);
                std::string error;
                if(!nac::decode(data,transient,error))
                    throw std::runtime_error("Cannot decode NEF8 NAC: "+error);
                std::size_t pcm_bytes=0;
                for(auto const& track:transient)
                    pcm_bytes+=track.samples.size()*sizeof(std::int16_t);
                if(cache.size()<32 && pcm_bytes<=cache_clip_bytes)
                    it=cache.emplace(asset_path,std::move(transient)).first;
            }
            auto const& tracks=it!=cache.end()?it->second:transient;
            // Exact name wins for one-shot cues (fire_body/reload/equip).
            // Prefix + underscore selects variations in any NAC audio
            // collection without substring ambiguity.
            auto const exact=std::find_if(tracks.begin(),tracks.end(),
                [&](auto const& track){return track.name==prefix;});
            bool const exact_match=exact!=tracks.end();
            std::size_t matches=exact_match?1:0;
            if(!exact_match)
                for(auto const& track:tracks)
                    if(track.name.rfind(prefix+"_",0)==0) ++matches;
            if(!matches)
                throw std::runtime_error("No matching NAC audio clips: "+prefix);
            auto const wanted=variation%matches;
            std::size_t count=0;
            for(auto const& track:tracks) {
                if(exact_match ? track.name!=prefix :
                   track.name.rfind(prefix+"_",0)!=0) continue;
                if(count++ != wanted) continue;
                if(!service.voice_audio())
                    service.voice_audio()=std::make_unique<AudioEngine>();
                NorthStarAudioVoiceOptions options{
                    sizeof(NorthStarAudioVoiceOptions),
                    static_cast<float>(gain),0,0,{0,0,0}};
                if(args.size()==7) {
                    const Vector3 pos{
                        static_cast<float>(runtime_script_contracts::script_number(args[4])),
                        static_cast<float>(runtime_script_contracts::script_number(args[5])),
                        static_cast<float>(runtime_script_contracts::script_number(args[6]))};
                    if(!pos.valid() ||
                       std::max({std::abs(pos.x),std::abs(pos.y),
                                 std::abs(pos.z)})>1000000.f)
                        throw std::invalid_argument("Invalid spatial NAC position");
                    options.spatial=1;
                    options.position={pos.x,pos.y,pos.z};
                }
                NorthStarPcm16 pcm{track.samples.data(),
                    static_cast<std::uint32_t>(track.samples.size()/track.channels),
                    track.sample_rate,track.channels};
                // ABI-1 providers support single-stream playback only.
                // Generic single-voice fallback for ABI-1 providers.
                // Per-voice gain/overlap require optional voices ABI-2.
                if(!service.voice_audio()->supports_voices()) {
                    service.voice_audio()->play(pcm);
                    return std::string("0");
                }
                return std::to_string(service.voice_audio()->play_voice(pcm,options));
            }
            throw std::runtime_error("NAC clip selection failed");
        });
        bindings.add("stopVoice",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.voice_audio() ||
               !service.voice_audio()->supports_voices())
                throw std::invalid_argument("audio.stopVoice(voiceId) requires voices ABI-2");
            auto const id=runtime_script_contracts::script_index(args[0]);
            if(!id)throw std::invalid_argument("Voice ID must be nonzero");
            service.voice_audio()->stop_voice(
                static_cast<NorthStarAudioVoiceId>(id));
            return std::string("true");
        });
        bindings.add("setVoiceGain",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=2 || !service.voice_audio() ||
               !service.voice_audio()->supports_voices())
                throw std::invalid_argument("audio.setVoiceGain(id,gain) requires voices ABI-2");
            auto const id=runtime_script_contracts::script_index(args[0]);
            auto const gain=runtime_script_contracts::script_number(args[1]);
            if(!id || gain<0 || gain>2)
                throw std::invalid_argument("Invalid voice ID or gain");
            service.voice_audio()->set_voice_gain(
                static_cast<NorthStarAudioVoiceId>(id),
                static_cast<float>(gain));
            return std::string("true");
        });
        bindings.add("setVoicePosition",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=4 || !service.voice_audio() ||
               !service.voice_audio()->supports_voices())
                throw std::invalid_argument("audio.setVoicePosition(id,x,y,z) requires voices ABI-2");
            auto const id=runtime_script_contracts::script_index(args[0]);
            const Vector3 pos{static_cast<float>(runtime_script_contracts::script_number(args[1])),
                              static_cast<float>(runtime_script_contracts::script_number(args[2])),
                              static_cast<float>(runtime_script_contracts::script_number(args[3]))};
            if(!id || !pos.valid() ||
               std::max({std::abs(pos.x),std::abs(pos.y),std::abs(pos.z)})>1000000.f)
                throw std::invalid_argument("Invalid voice ID or spatial position");
            service.voice_audio()->set_voice_position(
                static_cast<NorthStarAudioVoiceId>(id),
                {pos.x,pos.y,pos.z});
            return std::string("true");
        });
        bindings.add("play",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.fps_assets())
                throw std::invalid_argument("audio.play(virtualWavAsset)");
            auto path=fs::u8path(args[0]);
            if(args[0].rfind("audio/",0)!=0 || path.extension()!=".wav" ||
                args[0].size()>256 || args[0].find("..")!=std::string::npos ||
                args[0].find('\\')!=std::string::npos)
                throw std::invalid_argument("Audio asset path must be audio/*.wav");
            auto it=service.clip_cache().find(args[0]);
            if(it==service.clip_cache().end()) {
                auto request=service.fps_assets()->request_audio_clip(args[0]);
                it=service.clip_cache().emplace(args[0],*request.get()).first;
            }
            auto const& sound=it->second;
            if(!service.output()) service.output()=std::make_unique<AudioOutput>();
            NorthStarPcm16 pcm{sound.samples.data(),sound.frames(),
                              sound.sample_rate,sound.channels};
            service.output()->play(pcm);
            return std::string("null");
        });
}

void register_audio_script_bindings(ScriptRuntime& runtime,
                                   AudioScriptBindings&& handlers) {
    std::move(handlers).bind_all(runtime);
}
} // namespace northstarcpp::window_app_internal
