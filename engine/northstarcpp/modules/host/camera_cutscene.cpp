// NorthStar camera cutscene playback / shot authoring, original code.
#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/assets.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace northstarcpp {
namespace {
constexpr float max_duration=36000.f;
constexpr float pi=3.14159265358979323846f;
bool finite(float x) noexcept {return std::isfinite(x);}
float angle(float a,float b,float t) noexcept {
    return std::remainder(a+
        std::remainder(b-a,2.f*pi)*t,2.f*pi);
}
float lerp(float a,float b,float t) noexcept {return a+(b-a)*t;}
CameraPose blend(CameraPose const& a,CameraPose const& b,float t) noexcept {
    CameraPose result=b;
    result.position={
        lerp(a.position.x,b.position.x,t),
        lerp(a.position.y,b.position.y,t),
        lerp(a.position.z,b.position.z,t)};
    result.yaw=angle(a.yaw,b.yaw,t);
    result.pitch=lerp(a.pitch,b.pitch,t);
    result.roll=angle(a.roll,b.roll,t);
    result.fov_y_degrees=lerp(a.fov_y_degrees,b.fov_y_degrees,t);
    result.near_plane=lerp(a.near_plane,b.near_plane,t);
    result.far_plane=lerp(a.far_plane,b.far_plane,t);
    return result;
}
float ease(float x) noexcept {
    x=std::clamp(x,0.f,1.f);
    return x*x*x*(x*(x*6.f-15.f)+10.f);
}
bool valid_name(std::string const& s) noexcept {
    if(s.empty() || s.size()>80)return false;
    for(unsigned char c:s)
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
             (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.'))
            return false;
    return true;
}
}
void CameraCutscene::set_shots(std::vector<CameraCutsceneShot> shots) {
    if(shots.empty() || shots.size()>256)
        throw std::invalid_argument("Cutscene requires 1..256 shots");
    std::vector<float> starts;
    starts.reserve(shots.size());
    std::unordered_set<std::string> names;
    float total=0.f;
    for(std::size_t i=0;i<shots.size();++i) {
        auto const& s=shots[i];
        if(!valid_name(s.name) || !names.insert(s.name).second ||
           !finite(s.duration) || s.duration<=0.f ||
           s.duration>max_duration-total ||
           !finite(s.transition_seconds) || s.transition_seconds<0.f ||
           s.transition_seconds>(i==0?0.f:s.duration) ||
           s.track.empty() ||
           s.track.duration()>s.duration+0.0001f)
            throw std::invalid_argument("Invalid cutscene shot/transition/track");
        starts.push_back(total);
        total+=s.duration;
    }
    shots_=std::move(shots);
    starts_=std::move(starts);
    duration_=total;
}
CameraCutsceneFrame CameraCutscene::sample(float seconds) const {
    if(shots_.empty() || !finite(seconds))
        throw std::invalid_argument("Invalid cutscene sample time");
    auto const t=std::clamp(seconds,0.f,duration_);
    auto upper=std::upper_bound(starts_.begin(),starts_.end(),t);
    std::size_t index=upper==starts_.begin()?0:
        static_cast<std::size_t>(upper-starts_.begin()-1);
    index=std::min(index,shots_.size()-1);
    auto const& shot=shots_[index];
    float const local=std::min(t-starts_[index],shot.duration);
    auto pose=shot.track.sample(local);
    if(index>0 && shot.transition_seconds>0.f &&
       local<shot.transition_seconds) {
        auto const& previous=shots_[index-1];
        auto const source=previous.track.sample(previous.duration);
        pose=blend(source,pose,ease(local/shot.transition_seconds));
    }
    return {pose,shot.name,index,local};
}

namespace {
void validate_lens(CameraCinematicLens const& p) {
    if(!finite(p.focus_metres) || p.focus_metres<.05f || p.focus_metres>10000.f ||
       !finite(p.aperture_f) || p.aperture_f<.7f || p.aperture_f>32.f ||
       !finite(p.exposure_ev) || std::abs(p.exposure_ev)>5.f ||
       !finite(p.motion_blur) || p.motion_blur<0.f || p.motion_blur>1.f ||
       !finite(p.letterbox) || p.letterbox<0.f || p.letterbox>.4f ||
       !finite(p.dof_strength) || p.dof_strength<0.f || p.dof_strength>1.f)
        throw std::invalid_argument("Invalid cutscene camera lens/exposure");
}
bool valid_lane(std::string const& s) {
    if(s.empty() || s.size()>80)return false;
    for(unsigned char c:s)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))
            return false;
    return true;
}
bool valid_type(std::string const& type) {
    return type=="animation" || type=="audio" || type=="dialogue" ||
           type=="event" || type=="overlay";
}
std::string quote(std::string const& s) {
    std::ostringstream result;
    result << '"';
    constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:s) {
        if(c=='"' || c=='\\')result<<'\\'<<char(c);
        else if(c=='\n')result<<"\\n";
        else if(c=='\r')result<<"\\r";
        else if(c=='\t')result<<"\\t";
        else if(c<32)result<<"\\u00"<<hex[c>>4]<<hex[c&15];
        else result<<char(c);
    }
    result << '"';
    return result.str();
}
CameraCinematicLens lerp_lens(CameraCinematicLens const& a,
                              CameraCinematicLens const& b,float t) {
    return {lerp(a.focus_metres,b.focus_metres,t),
            lerp(a.aperture_f,b.aperture_f,t),
            lerp(a.exposure_ev,b.exposure_ev,t),
            lerp(a.motion_blur,b.motion_blur,t),
            lerp(a.letterbox,b.letterbox,t),
            lerp(a.dof_strength,b.dof_strength,t)};
}
}
void CameraCutscene::set_cues(std::vector<CameraCutsceneCue> cues) {
    if(cues.size()>8192)throw std::invalid_argument("Cutscene event limit exceeded");
    for(auto const& cue:cues) {
        if(!finite(cue.seconds) || cue.seconds<0.f || cue.seconds>duration_ ||
           !valid_type(cue.type) || !valid_lane(cue.lane) ||
           cue.target.size()>128 || cue.action.size()>80 ||
           cue.value.size()>2048)
            throw std::invalid_argument("Invalid cutscene track cue");
    }
    std::stable_sort(cues.begin(),cues.end(),
                     [](auto const& a,auto const& b){return a.seconds<b.seconds;});
    cues_=std::move(cues);
}
void CameraCutscene::set_lens_keys(std::vector<CameraLensKey> keys) {
    if(keys.size()>4096)throw std::invalid_argument("Cutscene lens key limit exceeded");
    float previous=-1.f;
    for(auto const& key:keys) {
        if(!finite(key.seconds) || key.seconds<=previous ||
           key.seconds>duration_)
            throw std::invalid_argument("Invalid cutscene lens key time");
        validate_lens(key.lens);
        previous=key.seconds;
    }
    lens_keys_=std::move(keys);
}
std::vector<CameraCutsceneCue> CameraCutscene::cues_between(
        float from,float to) const {
    if(!finite(from)||!finite(to)||to<from || to>duration_+.001f)
        throw std::invalid_argument("Invalid cutscene event interval");
    auto const start=std::upper_bound(cues_.begin(),cues_.end(),from,
        [](float t,CameraCutsceneCue const& c){return t<c.seconds;});
    auto const stop=std::upper_bound(start,cues_.end(),to,
        [](float t,CameraCutsceneCue const& c){return t<c.seconds;});
    return {start,stop};
}
CameraCinematicLens CameraCutscene::lens_at(float seconds) const {
    if(!finite(seconds))throw std::invalid_argument("Invalid lens sample time");
    if(lens_keys_.empty())return {};
    if(seconds<=lens_keys_.front().seconds)return lens_keys_.front().lens;
    if(seconds>=lens_keys_.back().seconds)return lens_keys_.back().lens;
    auto const upper=std::upper_bound(lens_keys_.begin(),lens_keys_.end(),seconds,
        [](float t,CameraLensKey const& k){return t<k.seconds;});
    auto const& b=*upper;
    auto const& a=*(upper-1);
    return lerp_lens(a.lens,b.lens,(seconds-a.seconds)/(b.seconds-a.seconds));
}
std::string serialize_camera_cues(std::vector<CameraCutsceneCue> const& cues) {
    std::ostringstream out;
    out<<std::setprecision(9)<<'[';
    for(std::size_t i=0;i<cues.size();++i) {
        auto const& c=cues[i];
        if(i)out<<',';
        out<<"{\"time\":"<<c.seconds
           <<",\"type\":"<<quote(c.type)
           <<",\"lane\":"<<quote(c.lane)
           <<",\"target\":"<<quote(c.target)
           <<",\"action\":"<<quote(c.action)
           <<",\"value\":"<<quote(c.value)<<"}";
    }
    out<<']';
    return out.str();
}
std::string serialize_camera_cutscene(CameraCutscene const& scene) {
    if(scene.empty())throw std::invalid_argument("Cannot serialize empty cutscene");
    std::ostringstream out;out<<std::setprecision(9);
    out<<"{\n  \"schema\":1,\n  \"space\":"
       <<quote(scene.space()==CameraCutsceneSpace::world?"world":"target")
       <<",\n  \"shots\":[\n";
    for(std::size_t i=0;i<scene.shots().size();++i) {
        auto const& shot=scene.shots()[i];
        out<<"    {\"name\":"<<quote(shot.name)
           <<",\"duration\":"<<shot.duration
           <<",\"transition_seconds\":"<<shot.transition_seconds
           <<",\"track\":"<<serialize_camera_track(shot.track)<<"}";
        if(i+1<scene.shots().size())out<<',';
        out<<'\n';
    }
    out<<"  ],\n  \"tracks\":[\n";
    // Keep a logical lane per (type,name), with individual keys ordered by
    // absolute cutscene time. This makes export fully reversible in the UI.
    std::vector<std::pair<std::string,std::string>> lanes;
    for(auto const& cue:scene.cues()) {
        auto key=std::make_pair(cue.type,cue.lane);
        if(std::find(lanes.begin(),lanes.end(),key)==lanes.end())
            lanes.push_back(std::move(key));
    }
    std::size_t written=0;
    for(auto const& lane:lanes) {
        if(written++)out<<",\n";
        out<<"    {\"type\":"<<quote(lane.first)
           <<",\"name\":"<<quote(lane.second)<<",\"keys\":[";
        bool first=true;
        for(auto const& cue:scene.cues()) {
            if(cue.type!=lane.first || cue.lane!=lane.second)continue;
            if(!first)out<<',';
            first=false;
            out<<"{\"time\":"<<cue.seconds
               <<",\"target\":"<<quote(cue.target)
               <<",\"action\":"<<quote(cue.action)
               <<",\"value\":"<<quote(cue.value)<<"}";
        }
        out<<"]}";
    }
    if(!scene.lens_keys().empty()) {
        if(written++)out<<",\n";
        out<<"    {\"type\":\"lens\",\"name\":\"lens\",\"keys\":[";
        bool first=true;
        for(auto const& k:scene.lens_keys()) {
            if(!first)out<<',';
            first=false;
            out<<"{\"time\":"<<k.seconds
               <<",\"focus_m\":"<<k.lens.focus_metres
               <<",\"aperture_f\":"<<k.lens.aperture_f
               <<",\"exposure_ev\":"<<k.lens.exposure_ev
               <<",\"motion_blur\":"<<k.lens.motion_blur
               <<",\"letterbox\":"<<k.lens.letterbox
               <<",\"dof_strength\":"<<k.lens.dof_strength<<"}";
        }
        out<<"]}";
    }
    out<<"\n  ]\n}\n";
    return out.str();
}

CameraCutscene parse_camera_cutscene(std::string const& json) {
    if(json.size()>24u*1024u*1024u)
        throw std::invalid_argument("Cutscene file exceeds 24 MiB");
    boost::property_tree::ptree root;
    std::istringstream stream(json);
    boost::property_tree::read_json(stream,root);
    if(root.get<int>("schema")!=1)
        throw std::invalid_argument("Unsupported camera cutscene schema");
    auto const& nodes=root.get_child("shots");
    std::vector<CameraCutsceneShot> shots;
    for(auto const& node:nodes) {
        if(!node.first.empty() || shots.size()>=256)
            throw std::invalid_argument("Cutscene shots must be a bounded array");
        auto const& value=node.second;
        CameraCutsceneShot shot;
        shot.name=value.get<std::string>("name");
        shot.duration=value.get<float>("duration");
        shot.transition_seconds=value.get<float>("transition_seconds",0.f);
        boost::property_tree::ptree track;
        track.put("schema",1);
        track.add_child("keyframes",value.get_child("track.keyframes"));
        std::ostringstream serialized;
        boost::property_tree::write_json(serialized,track,false);
        shot.track=parse_camera_track(serialized.str());
        shots.push_back(std::move(shot));
    }
    CameraCutscene sequence;
    auto const space=root.get<std::string>("space","world");
    if(space=="target")
        sequence.set_space(CameraCutsceneSpace::target);
    else if(space!="world")
        throw std::invalid_argument("Unknown cutscene coordinate space");
    sequence.set_shots(std::move(shots));
    std::vector<CameraCutsceneCue> cues;
    std::vector<CameraLensKey> lens_keys;
    if(auto tracks=root.get_child_optional("tracks")) {
        if(tracks->size()>64)
            throw std::invalid_argument("Cutscene track count exceeds 64");
        for(auto const& track:*tracks) {
            if(!track.first.empty())
                throw std::invalid_argument("Cutscene tracks must be an array");
            auto const type=track.second.get<std::string>("type");
            auto const lane=track.second.get<std::string>("name",type);
            if(!valid_lane(lane))
                throw std::invalid_argument("Invalid cutscene track name");
            auto const& keys=track.second.get_child("keys");
            if(type=="lens") {
                if(!lens_keys.empty())
                    throw std::invalid_argument("Multiple lens lanes not supported");
                for(auto const& node:keys) {
                    if(!node.first.empty())
                        throw std::invalid_argument("Lens keys must be an array");
                    CameraLensKey key;
                    key.seconds=node.second.get<float>("time");
                    key.lens.focus_metres=node.second.get<float>("focus_m",5.f);
                    key.lens.aperture_f=node.second.get<float>("aperture_f",4.f);
                    key.lens.exposure_ev=node.second.get<float>("exposure_ev",0.f);
                    key.lens.motion_blur=node.second.get<float>("motion_blur",0.f);
                    key.lens.letterbox=node.second.get<float>("letterbox",0.f);
                    key.lens.dof_strength=node.second.get<float>("dof_strength",0.f);
                    lens_keys.push_back(key);
                    if(lens_keys.size()>4096)
                        throw std::invalid_argument("Too many cutscene lens keys");
                }
            } else {
                if(!valid_type(type))
                    throw std::invalid_argument("Unsupported cutscene track type");
                for(auto const& node:keys) {
                    if(!node.first.empty())
                        throw std::invalid_argument("Cue keys must be an array");
                    CameraCutsceneCue cue;
                    cue.seconds=node.second.get<float>("time");
                    cue.type=type;cue.lane=lane;
                    cue.target=node.second.get<std::string>("target","");
                    cue.action=node.second.get<std::string>("action","");
                    cue.value=node.second.get<std::string>("value","");
                    cues.push_back(std::move(cue));
                    if(cues.size()>8192)
                        throw std::invalid_argument("Too many cutscene cues");
                }
            }
        }
    }
    sequence.set_cues(std::move(cues));
    sequence.set_lens_keys(std::move(lens_keys));
    return sequence;
}
CameraCutscene load_camera_cutscene(AssetStore const& assets,
                                   std::string const& path) {
    auto const bytes=assets.read(path);
    return parse_camera_cutscene(std::string(bytes.begin(),bytes.end()));
}
} // namespace northstarcpp
