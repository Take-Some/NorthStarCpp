// NorthStarCpp — scene transform animation service
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_animation.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include "transition_curve.h"
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace northstarcpp {
namespace {
bool finite(Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool bounds(Vector3 v, float limit) {
    return finite(v) && std::abs(v.x) <= limit &&
           std::abs(v.y) <= limit && std::abs(v.z) <= limit;
}
Vector3 multiply(Vector3 a, Vector3 b) {
    return {a.x*b.x, a.y*b.y, a.z*b.z};
}
Vector3 add(Vector3 a, Vector3 b) {
    return {a.x+b.x, a.y+b.y, a.z+b.z};
}
Vector3 interpolate(Vector3 a, Vector3 b, float t) {
    return {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t};
}
Vector3 read_vector(boost::property_tree::ptree const& node, char const* label) {
    float values[3]{};
    std::size_t count=0;
    for (auto const& item:node) {
        if (!item.first.empty() || count==3)
            throw std::invalid_argument(std::string("Invalid animation vector: ")+label);
        values[count++]=item.second.get_value<float>();
    }
    if (count!=3) throw std::invalid_argument(std::string("Invalid vector length: ")+label);
    return {values[0],values[1],values[2]};
}
void validate(SceneAnimationKeyframe const& key) {
    if (!std::isfinite(key.time) || key.time < 0.f || key.time > 3600.f ||
        !bounds(key.pose.translation,100000.f) ||
        !bounds(key.pose.rotation,36000.f) ||
        !bounds(key.pose.scale,1000.f) ||
        key.pose.scale.x<=0.f || key.pose.scale.y<=0.f || key.pose.scale.z<=0.f)
        throw std::invalid_argument("Invalid scene animation keyframe");
}
} // namespace

void SceneAnimationClip::set_keyframes(
    std::vector<SceneAnimationKeyframe> frames, SceneAnimationInterpolation interpolation) {
    if (frames.size()<2 || frames.size()>8192)
        throw std::invalid_argument("Scene animation requires 2..8192 keyframes");
    if (frames.front().time!=0.f)
        throw std::invalid_argument("Scene animation must start at time zero");
    float prev=-1.f;
    for (auto const& key:frames) {
        validate(key);
        if (key.time<=prev)
            throw std::invalid_argument("Scene animation times must strictly increase");
        prev=key.time;
    }
    frames_=std::move(frames);
    interpolation_=interpolation;
}

float SceneAnimationClip::duration() const noexcept {
    return frames_.empty()?0.f:frames_.back().time;
}

SceneAnimationPose SceneAnimationClip::sample(float seconds) const {
    if (frames_.empty()) throw std::logic_error("Scene animation has no keys");
    if (!std::isfinite(seconds)) throw std::invalid_argument("Invalid animation sample time");
    if (seconds<=0.f) return frames_.front().pose;
    if (seconds>=duration()) return frames_.back().pose;
    auto upper=std::upper_bound(frames_.begin(),frames_.end(),seconds,
        [](float t,SceneAnimationKeyframe const& k){return t<k.time;});
    auto const& b=*upper;
    auto const& a=*(upper-1);
    if (interpolation_==SceneAnimationInterpolation::step) return a.pose;
    auto t=(seconds-a.time)/(b.time-a.time);
    if (interpolation_==SceneAnimationInterpolation::fermi_dirac)
        t=animation::fermi_dirac_weight(t,1.f,0.01f);
    return {interpolate(a.pose.translation,b.pose.translation,t),
            interpolate(a.pose.rotation,b.pose.rotation,t),
            interpolate(a.pose.scale,b.pose.scale,t)};
}

SceneAnimationClip parse_scene_animation_clip(std::string const& source) {
    if (source.empty() || source.size()>1024*1024)
        throw std::invalid_argument("Scene animation JSON must fit in 1 MiB");
    std::istringstream input(source);
    boost::property_tree::ptree data;
    boost::property_tree::read_json(input,data);
    if (data.get<int>("schema")!=1)
        throw std::invalid_argument("Unsupported scene animation schema");
    if (data.get<std::string>("type")!="scene-transform")
        throw std::invalid_argument("Expected scene-transform animation clip");
    auto mode=data.get<std::string>("interpolation","linear");
    auto interpolation=SceneAnimationInterpolation::linear;
    if (mode=="step")
        interpolation=SceneAnimationInterpolation::step;
    else if (mode=="fermi-dirac")
        interpolation=SceneAnimationInterpolation::fermi_dirac;
    else if (mode!="linear")
        throw std::invalid_argument("Unsupported scene animation interpolation");
    std::vector<SceneAnimationKeyframe> keys;
    auto const& array=data.get_child("keyframes");
    for (auto const& entry:array) {
        if (!entry.first.empty()) throw std::invalid_argument("Keyframes must be an array");
        SceneAnimationKeyframe key;
        auto const& frame=entry.second;
        key.time=frame.get<float>("time");
        if (auto v=frame.get_child_optional("translation"))
            key.pose.translation=read_vector(*v,"translation");
        if (auto v=frame.get_child_optional("rotation"))
            key.pose.rotation=read_vector(*v,"rotation");
        if (auto v=frame.get_child_optional("scale"))
            key.pose.scale=read_vector(*v,"scale");
        keys.push_back(key);
        if (keys.size()>8192)
            throw std::invalid_argument("Scene animation keyframe limit exceeded");
    }
    SceneAnimationClip clip;
    clip.set_keyframes(std::move(keys),interpolation);
    return clip;
}


void SceneAnimationPlayer::play(Scene const& scene, std::uint64_t id,
                                SceneAnimationClip clip, bool loop, float speed) {
    if (!id || !std::isfinite(speed) || speed<=0.f || speed>16.f)
        throw std::invalid_argument("Invalid entity ID or animation speed");
    if (!clip.keyframe_count())
        throw std::invalid_argument("Cannot play an empty animation clip");
    if (playing_.size()>=4096 && !playing_.count(id))
        throw std::out_of_range("Scene has exceeded 4096 active animations");
    auto found=std::find_if(scene.objects.begin(),scene.objects.end(),
        [id](SceneObject const& obj){return obj.node.id==id;});
    if (found==scene.objects.end())
        throw std::out_of_range("Animation entity ID is not present in the scene");
    if (scene_pawn::is_pawn(scene,*found) || found->mass!=0.f)
        throw std::invalid_argument("Transform clips currently require static non-player entities");
    auto const i=static_cast<std::size_t>(found-scene.objects.begin());
    playing_.insert_or_assign(id,Instance{std::move(clip),i,id,
        found->position,found->node.rotation,found->node.scale,
        0.f,speed,loop,false,false});
    if(std::find(scene_order_.begin(),scene_order_.end(),id)==scene_order_.end())
        scene_order_.push_back(id);
    std::sort(scene_order_.begin(),scene_order_.end(),
        [this](std::uint64_t a,std::uint64_t b) {
            auto const x=playing_.at(a).scene_index;
            auto const y=playing_.at(b).scene_index;
            return x==y?a<b:x<y;
        });
}
void SceneAnimationPlayer::stop(std::uint64_t id) noexcept {
    playing_.erase(id);
    scene_order_.erase(std::remove(scene_order_.begin(),scene_order_.end(),id),
                       scene_order_.end());
}
void SceneAnimationPlayer::clear() noexcept {
    playing_.clear();
    scene_order_.clear();
}
bool SceneAnimationPlayer::active(std::uint64_t id) const noexcept {
    return playing_.count(id)!=0;
}
void SceneAnimationPlayer::pause(std::uint64_t id, bool paused) {
    playing_.at(id).paused=paused;
}
void SceneAnimationPlayer::seek(std::uint64_t id, float seconds) {
    auto& anim=playing_.at(id);
    if (!std::isfinite(seconds) || seconds<0.f || seconds>anim.clip.duration())
        throw std::invalid_argument("Invalid scene animation seek time");
    anim.time=seconds;
    anim.finished=seconds==anim.clip.duration() && !anim.loop;
}
SceneAnimationStatus SceneAnimationPlayer::status(std::uint64_t id) const {
    auto const& item=playing_.at(id);
    return {item.time,item.clip.duration(),item.speed,item.loop,item.paused,item.finished};
}
std::vector<SceneAnimationSample> SceneAnimationPlayer::advance(float seconds) {
    std::vector<SceneAnimationSample> result;
    advance_into(seconds,result);
    return result;
}
void SceneAnimationPlayer::advance_into(float seconds,
    std::vector<SceneAnimationSample>& result) {
    if (!std::isfinite(seconds) || seconds<0.f || seconds>0.25f)
        throw std::invalid_argument("Invalid animation frame delta");
    result.clear();
    result.reserve(scene_order_.size());
    for (auto const entity_id:scene_order_) {
        auto& anim=playing_.at(entity_id);
        float const duration=anim.clip.duration();
        if (!anim.paused && !anim.finished && seconds>0.f) {
            auto const time=static_cast<double>(anim.time)+
                            static_cast<double>(seconds)*anim.speed;
            if (anim.loop) {
                // Keep exact final sample for non-looping animations; looping
                // playback wraps while preserving fractional excess time.
                anim.time=static_cast<float>(std::fmod(time,duration));
            } else if (time>=duration) {
                anim.time=duration;
                anim.finished=true;
            } else {
                anim.time=static_cast<float>(time);
            }
        }
        auto const pose=anim.clip.sample(anim.time);
        result.push_back({anim.entity_id,anim.scene_index,
            add(anim.base_position,pose.translation),
            add(anim.base_rotation,pose.rotation),
            multiply(anim.base_scale,pose.scale)});
    }

}
} // namespace northstarcpp
