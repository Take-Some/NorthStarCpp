#include <northstarcpp/animation_graph.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace northstarcpp {
namespace {
void check(bool ok,char const* reason) {
    if(!ok)throw std::invalid_argument(reason);
}
JointTransform blend(JointTransform const& a,JointTransform const& b,float weight) {
    auto const w=std::clamp(weight,0.f,1.f);
    return {a.position+(b.position-a.position)*w,
        math::slerp(a.rotation,b.rotation,w),
        a.scale+(b.scale-a.scale)*w};
}
float clip_length(SkeletalClip const& clip) {
    return clip.frames>1 ? (clip.frames-1)/clip.sample_rate:
        1.f/clip.sample_rate;
}
}
AnimationGraphInstance::AnimationGraphInstance(Skeleton const& rig,AnimationGraph graph)
    : rig_(&rig),graph_(std::move(graph)),
      required_refs_(graph_.references()),values_(graph_.defaults) {
    check(!rig.joints.empty() && rig.joints.size()<=4096,
          "Animation Graph requires 1..4096 joints");
    auto const n=rig.joints.size();
    pose_.resize(n);scratch_.resize(n);scratch_b_.resize(n);previous_.resize(n);
    last_evaluated_pose_.resize(n);
    keyed_.resize(n);keyed_a_.resize(n);keyed_b_.resize(n);keyed_old_.resize(n);
    events_.reserve(32);
    for(std::size_t i=0;i<graph_.states.size();++i)
        if(graph_.states[i].name==graph_.initial) {active_=i;break;}
    for(auto const& layer:graph_.layers) {
        std::vector<std::uint8_t> mask(n,layer.bone_roots.empty());
        for(auto const& name:layer.bone_roots)
            check(rig.joint_index(name)<n,"Unknown Animation Graph bone mask root");
        if(!layer.bone_roots.empty())
            for(std::size_t i=0;i<n;++i) {
                auto parent=static_cast<int>(i);
                for(std::size_t guard=0;parent>=0 && guard<n;++guard) {
                    auto const& joint=rig.joints[static_cast<std::size_t>(parent)];
                    if(std::find(layer.bone_roots.begin(),layer.bone_roots.end(),
                        joint.name)!=layer.bone_roots.end()) {
                        mask[i]=1;break;
                    }
                    parent=joint.parent;
                }
            }
        masks_.push_back(std::move(mask));
    }
}
void AnimationGraphInstance::bind_clip(std::string const& ref,
    std::shared_ptr<SkeletalClip const> clip) {
    auto const& refs=required_refs_;
    check(std::find(refs.begin(),refs.end(),ref)!=refs.end(),
        "Animation Graph does not reference this clip");
    check(bool(clip)&&clip->frames>0&&clip->frames<=12000 &&
        std::isfinite(clip->sample_rate)&&clip->sample_rate>=1.f &&
        clip->joints.size()<=rig_->joints.size() &&
        clip->samples.size()==std::size_t(clip->frames)*clip->joints.size(),
        "Invalid Animation Graph clip");
    std::unordered_set<std::uint32_t> indices;
    for(auto index:clip->joints)
        check(index<rig_->joints.size()&&indices.insert(index).second,
            "Graph clip contains invalid/duplicate joint");
    clips_[ref]=std::move(clip);
}
bool AnimationGraphInstance::ready() const noexcept {
    for(auto const& ref:required_refs_)if(!clips_.count(ref))return false;
    return true;
}
void AnimationGraphInstance::set_parameter(std::string const& name,float value) {
    check(std::isfinite(value),"Graph parameter must be finite");
    auto it=values_.find(name);
    check(it!=values_.end(),"Unknown graph parameter");
    it->second=value;
}
float AnimationGraphInstance::get(std::string const& name) const noexcept {
    auto it=values_.find(name);
    return it==values_.end()?0.f:it->second;
}
float AnimationGraphInstance::parameter(std::string const& name) const {
    auto it=values_.find(name);
    if(it==values_.end())throw std::out_of_range("Unknown graph parameter");
    return it->second;
}
std::string const& AnimationGraphInstance::state() const noexcept {
    return graph_.states[active_].name;
}
float AnimationGraphInstance::duration(std::string const& ref) const {
    auto it=clips_.find(ref);
    if(it==clips_.end())return 1.f;
    return clip_length(*it->second);
}
void AnimationGraphInstance::evaluate_clip(std::string const& ref,float seconds,
    std::vector<JointTransform>& out,std::vector<std::uint8_t>& keyed) {
    for(std::size_t i=0;i<out.size();++i)out[i]=rig_->joints[i].bind;
    std::fill(keyed.begin(),keyed.end(),std::uint8_t{0});
    auto it=clips_.find(ref);
    if(it==clips_.end())return; // asynchronous preload: retain bind pose
    auto const& clip=*it->second;
    double const last=static_cast<double>(clip.frames-1);
    double const ticks=static_cast<double>(seconds)*clip.sample_rate;
    auto frame=last<=0?0.:clip.loop?
        std::fmod(ticks,last):std::min(ticks,last);
    auto const first=static_cast<std::size_t>(frame);
    auto const next=std::min(first+1,std::size_t(clip.frames-1));
    auto const fraction=static_cast<float>(frame-first);
    auto const tracks=clip.joints.size();
    for(std::size_t i=0;i<tracks;++i) {
        auto const index=clip.joints[i];
        auto const& a=clip.samples[first*tracks+i];
        auto const& b=clip.samples[next*tracks+i];
        out[index]=blend(a,b,fraction);
        keyed[index]=1;
    }
}
void AnimationGraphInstance::evaluate_state(std::size_t index,float time,
    std::vector<JointTransform>& out,std::vector<std::uint8_t>& keyed) {
    auto const& state=graph_.states[index];
    if(state.points.empty()) {
        evaluate_clip(state.clip,time,out,keyed);
        return;
    }
    auto const& pts=state.points;
    auto const speed=get(state.parameter);
    auto upper=std::upper_bound(pts.begin(),pts.end(),speed,
        [](float v,AnimationBlendPoint const& p){return v<p.at;});
    auto const right=upper==pts.begin()?std::size_t(0):
        upper==pts.end()?pts.size()-1:
            static_cast<std::size_t>(upper-pts.begin());
    auto const left=right==0?0:right-1;
    auto const weight=left==right?0.f:
        std::clamp((speed-pts[left].at)/(pts[right].at-pts[left].at),0.f,1.f);
    // Every source in a synchronized blend uses a shared normalized phase.
    // The dominant source supplies the cycle duration; footsteps and weapon
    // transitions stay aligned even when source clips have unequal frame rates.
    float a_seconds=time,b_seconds=time;
    if(!state.sync_group.empty()) {
        auto const& master=pts[weight<.5f?left:right].clip;
        auto const period=duration(master);
        auto const phase=std::fmod(time,period)/period;
        a_seconds=phase*duration(pts[left].clip);
        b_seconds=phase*duration(pts[right].clip);
    }
    if(weight>=1.f) {
        evaluate_clip(pts[right].clip,b_seconds,out,keyed);
        return;
    }
    evaluate_clip(pts[left].clip,a_seconds,out,keyed);
    if(left!=right && weight>0.f) {
        evaluate_clip(pts[right].clip,b_seconds,scratch_,keyed_a_);
        for(std::size_t i=0;i<out.size();++i) {
            out[i]=blend(out[i],scratch_[i],weight);
            keyed[i]=keyed[i]||keyed_a_[i];
        }
    }
}
void AnimationGraphInstance::emit_notifies(std::size_t index,float from,float to) {
    if(to<=from)return;
    auto const& state=graph_.states[index];
    std::array<std::string_view,48> emitted{};
    std::size_t emitted_count=0;
    auto emit=[&](std::string const& ref,float sync_period) {
        auto it=clips_.find(ref);
        if(it==clips_.end())return;
        for(std::size_t i=0;i<emitted_count;++i)
            if(emitted[i]==ref)return;
        emitted[emitted_count++]=ref;
        auto const period=sync_period>0.f?sync_period:duration(ref);
        if(!std::isfinite(period)||period<=0.f)return;
        for(auto const& marker:graph_.markers) {
            if(marker.clip!=ref)continue;
            auto const point=marker.normalized_time*period;
            auto count=std::uint64_t{0};
            if(!it->second->loop) {
                count=from<point && to>=point?1u:0u;
            } else {
                auto const first=std::floor(
                    (static_cast<double>(from)-point)/period)+1.;
                auto const last=std::floor(
                    (static_cast<double>(to)-point)/period);
                if(last>=first)
                    count=static_cast<std::uint64_t>(std::min(1024.,last-first+1.));
            }
            while(count--) {
                if(events_.size()<32)
                    events_.push_back({marker.name,ref,++sequence_});
                else ++dropped_events_;
            }
        }
    };
    if(state.points.empty()) {
        emit(state.clip,0.f);
    } else {
        auto const& pts=state.points;
        auto const speed=get(state.parameter);
        auto upper=std::upper_bound(pts.begin(),pts.end(),speed,
            [](float value,AnimationBlendPoint const& p){return value<p.at;});
        auto const right=upper==pts.begin()?std::size_t(0):
            upper==pts.end()?pts.size()-1:
                static_cast<std::size_t>(upper-pts.begin());
        auto const left=right==0?0:right-1;
        auto const weight=left==right?0.f:
            std::clamp((speed-pts[left].at)/
                (pts[right].at-pts[left].at),0.f,1.f);
        auto const period=state.sync_group.empty()?0.f:
            duration(pts[weight<.5f?left:right].clip);
        if(weight<1.f)emit(pts[left].clip,period);
        if(weight>0.f)emit(pts[right].clip,period);
    }
    for(auto const& layer:graph_.layers) {
        auto const weight=layer.weight*(layer.parameter.empty()?1.f:
            std::clamp(get(layer.parameter),0.f,1.f));
        if(weight>0.f)emit(layer.clip,0.f);
    }
}

void AnimationGraphInstance::advance(float dt) {
    check(std::isfinite(dt)&&dt>=0.f&&dt<=.25f,
        "Animation Graph frame delta outside [0,.25]");
    events_.clear();
    if(!transitioning_) {
        for(auto const& t:graph_.transitions) {
            if(t.from!=state()||t.to==state())continue;
            auto const value=get(t.parameter);
            if(t.operation==AnimationTransitionOp::greater_equal?
                value<t.threshold:value>t.threshold)continue;
            old_=active_;old_clock_=clock_;
            for(std::size_t i=0;i<graph_.states.size();++i)
                if(graph_.states[i].name==t.to) {active_=i;break;}
            clock_=0.f;
            fade_duration_=t.fade_seconds;fade_elapsed_=0.f;
            transitioning_=fade_duration_>0.f;
            break;
        }
    }
    auto const old_time=clock_;
    clock_+=dt;
    emit_notifies(active_,old_time,clock_);
    if(transitioning_) {
        old_clock_+=dt;
        fade_elapsed_+=dt;
        if(fade_elapsed_>=fade_duration_)transitioning_=false;
    }
}
void AnimationGraphInstance::evaluate(SkeletonPose& output) {
    evaluate_state(active_,clock_,pose_,keyed_);
    if(transitioning_) {
        evaluate_state(old_,old_clock_,previous_,keyed_old_);
        auto const t=std::clamp(fade_elapsed_/fade_duration_,0.f,1.f);
        for(std::size_t i=0;i<pose_.size();++i) {
            bool const from=keyed_old_[i]!=0,to=keyed_[i]!=0;
            // A missing track is NOT a request to return that joint to
            // YMT bind pose. Retain the outgoing animated transform.
            if(from&&!to)pose_[i]=previous_[i];
            else if(!from&&to)
                previous_[i]=has_last_evaluated_pose_?
                    last_evaluated_pose_[i]:pose_[i];
            else if(!from&&!to&&has_last_evaluated_pose_)
                previous_[i]=pose_[i]=last_evaluated_pose_[i];
            pose_[i]=blend(previous_[i],pose_[i],t);
            keyed_[i]=from||to;
        }
    }
    for(std::size_t layer_id=0;layer_id<graph_.layers.size();++layer_id) {
        auto const& layer=graph_.layers[layer_id];
        auto weight=layer.weight*(layer.parameter.empty()?1.f:
            std::clamp(get(layer.parameter),0.f,1.f));
        if(weight<=0.f||!clips_.count(layer.clip))continue;
        evaluate_clip(layer.clip,clock_,scratch_b_,keyed_b_);
        auto const& mask=masks_[layer_id];
        for(std::size_t i=0;i<pose_.size();++i) {
            if(!mask[i]||!keyed_b_[i])continue;
            auto const& target=scratch_b_[i];
            auto& base=pose_[i];
            if(layer.mode==AnimationLayerMode::override_pose) {
                base=blend(base,target,weight);
            } else {
                auto const& bind=rig_->joints[i].bind;
                base.position+=(target.position-bind.position)*weight;
                base.scale=base.scale*(math::float3{1.f,1.f,1.f}+
                    (target.scale-bind.scale)*weight);
                math::quaternion identity;identity.identity();
                auto inverse_bind_rotation=bind.rotation;
                inverse_bind_rotation.conjugate();
                auto const delta=(inverse_bind_rotation*target.rotation).normalize();
                base.rotation=(base.rotation*
                    math::slerp(identity,delta,weight)).normalize();
            }
            keyed_[i]=1;
        }
    }
    // Normal streaming and partial layers must also keep all unkeyed
    // joints at the last playable pose. Copy only pre-IK transforms.
    if(has_last_evaluated_pose_)
        for(std::size_t i=0;i<pose_.size();++i)
            if(!keyed_[i])pose_[i]=last_evaluated_pose_[i];
    std::copy(pose_.begin(),pose_.end(),last_evaluated_pose_.begin());
    has_last_evaluated_pose_=true;
    output.set_local_transforms(*rig_,pose_,keyed_);
}
std::vector<AnimationGraphInstance::Event> AnimationGraphInstance::drain_events() {
    auto out=std::move(events_);
    events_.clear();
    events_.reserve(32);
    return out;
}
}
