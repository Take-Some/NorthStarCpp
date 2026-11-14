// NorthStarCpp — cinematic camera splines (original implementation).
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/assets.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace northstarcpp {
namespace {
constexpr float pi=3.14159265358979323846f;
bool finite(float x){return std::isfinite(x);}
bool finite(Vector3 const& v){return finite(v.x)&&finite(v.y)&&finite(v.z);}
float lerp(float a,float b,float t){return a+(b-a)*t;}
float angle_lerp(float a,float b,float t){
    return std::remainder(a + std::remainder(b-a,2.f*pi)*t,2.f*pi);
}
void validate(CameraKeyframe const& k) {
    auto const& p=k.pose;
    if (!finite(k.seconds)||k.seconds<0.f||k.seconds>36000.f||
        !finite(p.position)||!finite(p.yaw)||!finite(p.pitch)||!finite(p.roll)||
        !finite(p.fov_y_degrees)||!finite(p.near_plane)||!finite(p.far_plane)||
        std::abs(p.pitch)>=pi*.5f||p.fov_y_degrees<25.f||
        p.fov_y_degrees>120.f||p.near_plane<0.01f||
        p.near_plane>2.f||p.far_plane<=p.near_plane+1.f||p.far_plane>10000.f)
        throw std::invalid_argument("Invalid camera cinematic keyframe");
}
Vector3 slope(CameraKeyframe const& a,CameraKeyframe const& b) {
    float const dt=b.seconds-a.seconds;
    return {(b.pose.position.x-a.pose.position.x)/dt,
            (b.pose.position.y-a.pose.position.y)/dt,
            (b.pose.position.z-a.pose.position.z)/dt};
}
float hermite(float x,float y,float dx,float dy,float t,float duration) {
    float const t2=t*t,t3=t2*t;
    return (2*t3-3*t2+1)*x+(t3-2*t2+t)*duration*dx+
           (-2*t3+3*t2)*y+(t3-t2)*duration*dy;
}
Vector3 parse_vector(boost::property_tree::ptree const& tree) {
    Vector3 out{};
    float values[3]{};
    unsigned index=0;
    for (auto const& item:tree) {
        if (!item.first.empty()||index>=3)
            throw std::invalid_argument("Camera track position needs three numeric coordinates");
        values[index++]=item.second.get_value<float>();
    }
    if (index!=3) throw std::invalid_argument("Camera track position needs three coordinates");
    out={values[0],values[1],values[2]};
    return out;
}
} // namespace

void CameraTrack::set_keyframes(std::vector<CameraKeyframe> frames) {
    if (frames.empty()||frames.size()>65536)
        throw std::invalid_argument("Camera track needs 1..65536 keyframes");
    float previous=-1.f;
    for (auto const& item:frames) {
        validate(item);
        if (item.seconds<=previous)
            throw std::invalid_argument("Camera track keyframe times must increase");
        previous=item.seconds;
    }
    frames_=std::move(frames);
}
float CameraTrack::duration() const noexcept {
    return frames_.empty()?0.f:frames_.back().seconds;
}
CameraPose CameraTrack::sample(float seconds) const {
    if (frames_.empty()) throw std::runtime_error("Camera track is empty");
    if (!finite(seconds)) throw std::invalid_argument("Invalid camera track playback time");
    if (seconds<=frames_.front().seconds) return frames_.front().pose;
    if (seconds>=frames_.back().seconds) return frames_.back().pose;
    auto const upper=std::upper_bound(frames_.begin(),frames_.end(),seconds,
        [](float t,CameraKeyframe const& k){return t<k.seconds;});
    std::size_t const i=static_cast<std::size_t>(upper-frames_.begin())-1u;
    auto const& a=frames_[i];
    auto const& b=frames_[i+1];
    float const duration=b.seconds-a.seconds;
    float const t=(seconds-a.seconds)/duration;
    auto const tangent_a=slope(frames_[i==0?0:i-1],b);
    auto const tangent_b=slope(a,frames_[std::min(i+2,frames_.size()-1)]);
    CameraPose p=a.pose;
    p.position={
        hermite(a.pose.position.x,b.pose.position.x,tangent_a.x,tangent_b.x,t,duration),
        hermite(a.pose.position.y,b.pose.position.y,tangent_a.y,tangent_b.y,t,duration),
        hermite(a.pose.position.z,b.pose.position.z,tangent_a.z,tangent_b.z,t,duration)};
    p.yaw=angle_lerp(a.pose.yaw,b.pose.yaw,t);
    p.pitch=lerp(a.pose.pitch,b.pose.pitch,t);
    p.roll=angle_lerp(a.pose.roll,b.pose.roll,t);
    p.fov_y_degrees=lerp(a.pose.fov_y_degrees,b.pose.fov_y_degrees,t);
    p.near_plane=lerp(a.pose.near_plane,b.pose.near_plane,t);
    p.far_plane=lerp(a.pose.far_plane,b.pose.far_plane,t);
    return p;
}
CameraTrack parse_camera_track(std::string const& json) {
    if (json.size()>24*1024*1024)
        throw std::invalid_argument("Camera track exceeds 24 MiB");
    boost::property_tree::ptree root;
    std::istringstream stream(json);
    boost::property_tree::read_json(stream,root);
    if (root.get<int>("schema")!=1)
        throw std::invalid_argument("Unsupported camera track schema");
    std::vector<CameraKeyframe> frames;
    for (auto const& item:root.get_child("keyframes")) {
        CameraKeyframe frame;
        auto const& node=item.second;
        frame.seconds=node.get<float>("time");
        frame.pose.position=parse_vector(node.get_child("position"));
        frame.pose.yaw=node.get<float>("yaw");
        frame.pose.pitch=node.get<float>("pitch");
        frame.pose.roll=node.get<float>("roll",0.f);
        frame.pose.fov_y_degrees=node.get<float>("fov_y_degrees",75.f);
        frame.pose.near_plane=node.get<float>("near",0.08f);
        frame.pose.far_plane=node.get<float>("far",500.f);
        frames.push_back(frame);
        if (frames.size()>65536)
            throw std::invalid_argument("Camera track keyframe limit exceeded");
    }
    CameraTrack result;
    result.set_keyframes(std::move(frames));
    return result;
}
std::string serialize_camera_track(CameraTrack const& track) {
    if (track.empty()) throw std::invalid_argument("Cannot serialize empty camera track");
    std::ostringstream out;
    out << std::setprecision(9);
    out << "{\n  \"schema\": 1,\n  \"keyframes\": [\n";
    auto const& frames=track.keyframes();
    for (std::size_t i=0;i<frames.size();++i) {
        auto const& f=frames[i];
        auto const& p=f.pose;
        out << "    {\"time\":" << f.seconds
            << ",\"position\":[" << p.position.x << "," << p.position.y
            << "," << p.position.z << "],\"yaw\":" << p.yaw
            << ",\"pitch\":" << p.pitch
            << ",\"roll\":" << p.roll
            << ",\"fov_y_degrees\":" << p.fov_y_degrees
            << ",\"near\":" << p.near_plane
            << ",\"far\":" << p.far_plane << "}";
        if (i+1<frames.size())out<<",";
        out<<"\n";
    }
    out<<"  ]\n}\n";
    return out.str();
}

void CameraReplayRecorder::start(float interval) {
    if(!finite(interval)||interval<1.f/240.f||interval>2.f)
        throw std::invalid_argument("Invalid camera recording sample interval");
    frames_.clear();
    interval_=interval;
    elapsed_=last_sample_=0.f;
    active_=true;
    paused_=false;
}
void CameraReplayRecorder::record(float dt,CameraPose const& pose) {
    if(!active_) throw std::logic_error("Camera recorder has not started");
    if(!finite(dt)||dt<0.f||dt>0.25f)
        throw std::invalid_argument("Invalid camera recording delta");
    if(paused_) return;
    if(frames_.empty()) {
        frames_.push_back({0.f,pose});
        CameraTrack validation;
        validation.set_keyframes(frames_);
    }
    elapsed_+=dt;
    if(elapsed_>36000.f || frames_.size()>=65536)
        throw std::overflow_error("Camera recording limit exceeded");
    if(elapsed_-last_sample_+0.000001f>=interval_) {
        if(elapsed_>last_sample_)frames_.push_back({elapsed_,pose});
        last_sample_=elapsed_;
    }
}
CameraTrack CameraReplayRecorder::finish() {
    if(!active_||frames_.empty())
        throw std::logic_error("No camera recording to finish");
    active_=false;
    CameraTrack track;
    track.set_keyframes(std::move(frames_));
    frames_.clear();
    return track;
}

CameraTrack load_camera_track(AssetStore const& assets,std::string const& virtual_path) {
    auto const bytes=assets.read(virtual_path);
    return parse_camera_track(std::string(bytes.begin(),bytes.end()));
}
} // namespace northstarcpp
