#include "native_mixer.hpp"
#include <northstarcpp/nac.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace northstarcpp::native_audio {
namespace {
bool finite(NorthStarAudioVector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float dot(NorthStarAudioVector3 a, NorthStarAudioVector3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
NorthStarAudioVector3 sub(NorthStarAudioVector3 a, NorthStarAudioVector3 b) {
    return {a.x-b.x, a.y-b.y, a.z-b.z};
}
NorthStarAudioVector3 cross(NorthStarAudioVector3 a, NorthStarAudioVector3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
NorthStarAudioVector3 normalized(NorthStarAudioVector3 v) {
    auto const length = std::sqrt(dot(v,v));
    return length > 0.00001f ? NorthStarAudioVector3{v.x/length,v.y/length,v.z/length}
                             : NorthStarAudioVector3{1,0,0};
}
std::int16_t saturate(float sample) {
    const auto clamped = std::max(-1.f,std::min(1.f,sample));
    return static_cast<std::int16_t>(std::lrint(clamped * 32767.f));
}
} // namespace

Mixer::Voice* Mixer::find(std::uint64_t id) {
    for (auto& v : voices_) if (v.id == id) return &v;
    return nullptr;
}

Mixer::PreparedPcm Mixer::prepare(NorthStarPcm16 const& pcm) {
    if(!pcm.samples || !pcm.frames ||
       pcm.sample_rate<8000u || pcm.sample_rate>192000u ||
       (pcm.channels!=1u&&pcm.channels!=2u))
        throw std::invalid_argument("NorthStar Audio: invalid PCM format");
    const auto bytes=std::uint64_t(pcm.frames)*pcm.channels*sizeof(std::int16_t);
    if(bytes>nac::max_pcm_bytes)
        throw std::invalid_argument("NorthStar Audio: PCM exceeds mixer capacity");
    PreparedPcm prepared{};
    prepared.frames=pcm.frames;prepared.rate=pcm.sample_rate;
    prepared.channels=pcm.channels;
    prepared.samples.assign(pcm.samples,
        pcm.samples+std::size_t(pcm.frames)*pcm.channels);
    return prepared;
}
std::uint64_t Mixer::play(NorthStarPcm16 const& pcm,
                          NorthStarAudioVoiceOptions const& options) {
    return play(prepare(pcm),options);
}
std::uint64_t Mixer::play(PreparedPcm&& pcm,
                          NorthStarAudioVoiceOptions const& opt) {
    if(!pcm.frames || !pcm.rate || (pcm.channels!=1u&&pcm.channels!=2u) ||
       pcm.samples.size()!=std::size_t(pcm.frames)*pcm.channels ||
       !std::isfinite(opt.gain) || opt.gain<0.f || opt.gain>8.f ||
       !finite(opt.position) || opt.struct_size<sizeof(opt))
        throw std::invalid_argument("NorthStar Audio: invalid prepared voice");
    const auto required=std::uint64_t(pcm.samples.size())*sizeof(std::int16_t);
    std::uint64_t used=0;
    for(auto const& v:voices_)used+=v.pcm.size()*sizeof(std::int16_t);
    // Never interrupt existing one-shot audio to admit a newer sound.
    if(used+required>nac::max_pcm_bytes || voices_.size()>=max_voices)
        return 0;
    Voice v{};
    v.id=next_id_++;
    if(!next_id_)next_id_=1;
    v.rate=pcm.rate;v.channels=pcm.channels;v.frames=pcm.frames;
    v.gain=opt.gain;v.looping=opt.looping!=0;
    v.spatial=opt.spatial!=0;v.pos=opt.position;
    v.pcm=std::move(pcm.samples); // O(1) under the real-time mixer lock.
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

bool Mixer::stop(std::uint64_t id) {
    auto const it = std::find_if(voices_.begin(),voices_.end(),
        [id](Voice const& v){return v.id==id;});
    if (it == voices_.end()) return false;
    voices_.erase(it);
    return true;
}
void Mixer::stop_all() noexcept { voices_.clear(); }
bool Mixer::gain(std::uint64_t id, float amount) {
    if (!std::isfinite(amount) || amount < 0 || amount > 8) return false;
    if (auto* v = find(id)) { v->gain = amount; return true; }
    return false;
}
bool Mixer::position(std::uint64_t id, NorthStarAudioVector3 p) {
    if (!finite(p)) return false;
    if (auto* v = find(id)) { v->pos = p; return true; }
    return false;
}
bool Mixer::listener(NorthStarAudioListener const& l) {
    if (l.struct_size < sizeof(l) || !finite(l.position) ||
        !finite(l.forward) || !finite(l.up) ||
        dot(l.forward,l.forward) < 0.0001f || dot(l.up,l.up) < 0.0001f ||
        dot(cross(l.forward,l.up), cross(l.forward,l.up)) < 0.0001f)
        return false;
    listener_ = l;
    return true;
}

void Mixer::mix(std::int16_t* output, std::uint32_t frames) {
    if (!output || frames > block_frames)
        throw std::invalid_argument("NorthStar Audio: block larger than 256 frames");
    std::array<float, block_frames * 2> accum{};
    auto const right = normalized(cross(listener_.forward,listener_.up));
    for (auto& voice : voices_) {
        float gl = voice.gain, gr = voice.gain;
        if (voice.spatial) {
            auto const relative = sub(voice.pos, listener_.position);
            float const dist = std::sqrt(dot(relative,relative));
            // V1 stereo spatialization: distance attenuation and equal-power panning.
            float const atten = 1.f / (1.f + 0.08f * dist * dist);
            float const pan = dist > 0.0001f ?
                std::clamp(dot(relative,right) / dist,-1.f,1.f) : 0.f;
            gl = voice.gain * atten * std::sqrt(1.f-pan);
            gr = voice.gain * atten * std::sqrt(1.f+pan);
        }
        auto const step = double(voice.rate) / double(sample_rate);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            if (voice.cursor >= voice.frames) {
                if (!voice.looping) break;
                voice.cursor = std::fmod(voice.cursor,double(voice.frames));
            }
            auto const i0 = std::uint32_t(voice.cursor);
            auto const i1 = i0 + 1 < voice.frames ? i0 + 1 :
                (voice.looping ? std::uint32_t(voice.loop_start) : i0);
            auto const frac = float(voice.cursor - i0);
            auto sample = [&](std::uint32_t f, std::uint32_t ch) {
                return float(voice.pcm[std::size_t(f)*voice.channels + ch]) / 32768.f;
            };
            auto const a = sample(i0,0)*(1.f-frac) + sample(i1,0)*frac;
            float l=a, r=a;
            if (voice.channels == 2) {
                auto const second=sample(i0,1)*(1.f-frac)+sample(i1,1)*frac;
                if (voice.spatial) l=r=(a+second)*0.5f;
                else r=second;
            }
            accum[std::size_t(frame)*2] += l*gl;
            accum[std::size_t(frame)*2+1] += r*gr;
            voice.cursor += step;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(),voices_.end(),
        [](Voice const& v){return !v.looping && v.cursor >= v.frames;}),voices_.end());
    // Mixing many independent voices can exceed 0 dBFS even when every
    // individual PCM stream is valid. Hard clamping produced harsh audible
    // crackle on gunshots/metal impacts. Apply one stereo-linked peak limiter
    // before PCM16 quantization to preserve spatial balance and transients.
    float peak=0.f;
    for(std::size_t i=0;i<std::size_t(frames)*2;++i)
        peak=std::max(peak,std::abs(accum[i]));
    const auto target=peak>.94f ? .94f/peak : 1.f;
    if(target<output_gain_)
        output_gain_=target; // sample block lookahead: no clipping on attack
    else {
        // 140ms release, so closely spaced gunshots don't make the gain pump.
        const float decay=std::exp(-float(frames)/
            (float(sample_rate)*.14f));
        output_gain_=target+(output_gain_-target)*decay;
    }
    for(std::size_t i=0;i<std::size_t(frames)*2;++i)
        output[i]=saturate(accum[i]*output_gain_);
}
} // namespace northstarcpp::native_audio
