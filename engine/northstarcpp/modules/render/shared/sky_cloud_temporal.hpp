#pragma once
// Sky-only temporal resolve policy. No scene-color history is accepted:
// each GPU provider must own a dedicated cloud RGB/transmittance/depth
// history attachment before enabling this policy in a render graph.
#include "temporal_history.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace northstarcpp::render {
struct SkyCloudFrame {
    TemporalCamera camera{};
    bool clouds_enabled=true;
    float cloud_coverage=0.f; // current weather coverage [0,1]
    float cloud_phase=0.f; // continuous wind phase [0,1)
    float sun_elevation=0.f;
};
struct SkyCloudHistoryState {
    TemporalFrameState camera{};
    bool history_valid=false;
    bool weather_cut=false;
    bool lighting_cut=false;
};
class SkyCloudHistoryTracker {
public:
    SkyCloudHistoryState begin(SkyCloudFrame next) {
        if(!std::isfinite(next.cloud_coverage)||
           next.cloud_coverage<0.f||next.cloud_coverage>1.f||
           !std::isfinite(next.cloud_phase)||
           next.cloud_phase<0.f||next.cloud_phase>=1.f||
           !std::isfinite(next.sun_elevation))
            throw std::invalid_argument("Cloud history received invalid atmosphere");
        const bool weather=initialized_ &&
            (std::abs(next.cloud_coverage-previous_.cloud_coverage)>.12f ||
             next.clouds_enabled!=previous_.clouds_enabled);
        const bool lighting=initialized_ &&
            std::abs(next.sun_elevation-previous_.sun_elevation)>.14f;
        // Wrapped wind phase does not itself trigger a cut; world-anchored
        // velocity is handled by each backend's reprojection.
        if(weather||lighting)camera_.invalidate();
        auto frame=camera_.begin(next.camera);
        SkyCloudHistoryState result{frame,
            frame.history_valid&&next.clouds_enabled,
            weather,lighting};
        previous_=next;
        initialized_=true;
        return result;
    }
    void invalidate() noexcept {camera_.invalidate();}
private:
    TemporalHistoryTracker camera_{};
    SkyCloudFrame previous_{};
    bool initialized_=false;
};
struct CloudTemporalSample {
    std::array<float,3> radiance{}; // linear scene RGB only
    float opacity=0.f; // 0: clear sky, 1: opaque cloud
    float first_hit_km=0.f; // cloud depth, NOT scene depth
};
struct CloudTemporalResult {
    std::array<float,3> radiance{};
    float weight=0.f;
    bool disoccluded=true;
};
struct CloudTemporalSettings {
    float max_history_weight=.88f;
    float depth_relative_limit=.12f;
    float opacity_reject_limit=.20f;
    float velocity_scale=.055f;
    float variance_gamma=1.5f;
};
// CPU reference for one sky-cloud pixel. Correct GPU implementation must use
// a sky-only history texture, previous camera matrix, cloud opacity/depth,
// neighborhood clamp and per-pixel disocclusion checks. No scene accumulation.
inline CloudTemporalResult resolve_cloud_temporal(
    std::array<CloudTemporalSample,9> const& neighbors,
    CloudTemporalSample const& previous,
    std::array<float,2> const& history_motion_pixels,
    bool history_valid,
    CloudTemporalSettings settings={}) noexcept {
    CloudTemporalResult result{neighbors[4].radiance,0.f,true};
    auto valid=[](float x){return std::isfinite(x) && x>=0.f && x<=65504.f;};
    const auto& center=neighbors[4];
    if(!history_valid ||
       !valid(center.opacity)||center.opacity>1.f ||
       !valid(previous.opacity)||previous.opacity>1.f ||
       !valid(center.first_hit_km)||!valid(previous.first_hit_km) ||
       !std::isfinite(history_motion_pixels[0])||
       !std::isfinite(history_motion_pixels[1]) ||
       std::abs(center.opacity-previous.opacity)>
             std::clamp(settings.opacity_reject_limit,0.f,1.f))
        return result;
    // Two clear-sky pixels have no cloud geometry to disocclude.
    if(std::max(center.opacity,previous.opacity)>.03f) {
        if(center.first_hit_km<=0.f || previous.first_hit_km<=0.f)
            return result;
        const float depth_error=std::abs(
            center.first_hit_km-previous.first_hit_km)/
            std::max(center.first_hit_km,.001f);
        if(depth_error>std::clamp(settings.depth_relative_limit,0.f,1.f))
            return result;
    }
    const float speed=std::hypot(
        history_motion_pixels[0],history_motion_pixels[1]);
    const float weight=std::clamp(settings.max_history_weight,0.f,.98f)/
        (1.f+speed*std::max(0.f,settings.velocity_scale));
    for(unsigned c=0;c<3;++c) {
        float min_value=std::numeric_limits<float>::max();
        float max_value=0.f,mean=0.f,second=0.f;
        for(auto const& sample:neighbors) {
            const float value=sample.radiance[c];
            if(!valid(value))return result;
            min_value=std::min(min_value,value);
            max_value=std::max(max_value,value);
            mean+=value/9.f;
            second+=value*value/9.f;
        }
        if(!valid(previous.radiance[c]))return result;
        const float stddev=std::sqrt(std::max(0.f,second-mean*mean));
        const float lo=std::max(min_value,mean-settings.variance_gamma*stddev);
        const float hi=std::min(max_value,mean+settings.variance_gamma*stddev);
        const float clipped=std::clamp(previous.radiance[c],
            std::min(lo,hi),std::max(lo,hi));
        result.radiance[c]=center.radiance[c]*(1.f-weight)+clipped*weight;
    }
    result.weight=weight;
    result.disoccluded=false;
    return result;
}
// Cloud parallax sample: compute a view ray through the current pixel,
// intersect the 3 km spherical cloud shell and project that same world point
// into the previous camera. Output is previous-frame top-left pixel XY.
// The sky is geocentric (R=6360 km); it is not limited by scene far clip.
inline bool reproject_cloud_shell(
    TemporalCamera const& current,TemporalCamera const& previous,
    std::array<float,2> current_pixel,std::array<float,2>& previous_pixel,
    float cloud_altitude_km=3.f) noexcept {
    if(current.width==0||current.height==0||
       previous.width==0||previous.height==0 ||
       cloud_altitude_km<=0.f || cloud_altitude_km>=100.f)
        return false;
    constexpr float pi=3.14159265358979323846f;
    auto ray=[&](TemporalCamera const& c,std::array<float,2> pixel) {
        const float yaw=c.yaw,pitch=c.pitch;
        const float sn=std::sin(yaw),cs=std::cos(yaw);
        const float sp=std::sin(pitch),cp=std::cos(pitch);
        const float scale=std::tan(c.fov_y_degrees*pi/360.f);
        float nx=(2.f*pixel[0]/c.width-1.f)*
                 (float(c.width)/c.height)*scale;
        float ny=(1.f-2.f*pixel[1]/c.height)*scale;
        std::array<float,3> d{
            sn*cp+cs*nx-sn*sp*ny,
            sp+cp*ny,
            cs*cp-sn*nx-cs*sp*ny};
        float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        for(float& v:d)v/=std::max(len,1e-6f);
        return d;
    };
    auto d=ray(current,current_pixel);
    constexpr float radius=6360.f,observer_height=.15f;
    std::array<float,3> origin{current.position[0]*.001f,
        radius+observer_height+current.position[1]*.001f,
        current.position[2]*.001f};
    float const shell=radius+cloud_altitude_km;
    float const b=origin[0]*d[0]+origin[1]*d[1]+origin[2]*d[2];
    float const c=origin[0]*origin[0]+origin[1]*origin[1]+
                  origin[2]*origin[2]-shell*shell;
    float const discriminant=b*b-c;
    if(discriminant<=0.f)return false;
    float const t=-b+std::sqrt(discriminant);
    if(!(t>0.f) || t>220.f)return false;
    std::array<float,3> world{};
    for(unsigned i=0;i<3;++i)world[i]=origin[i]+d[i]*t;
    std::array<float,3> delta{
        world[0]-previous.position[0]*.001f,
        world[1]-radius-observer_height-previous.position[1]*.001f,
        world[2]-previous.position[2]*.001f};
    float cy=std::cos(previous.yaw),sy=std::sin(previous.yaw);
    float cp=std::cos(previous.pitch),sp=std::sin(previous.pitch);
    float horizontal=delta[0]*cy-delta[2]*sy;
    float forward=delta[0]*sy+delta[2]*cy;
    float vertical=delta[1]*cp-forward*sp;
    float depth=delta[1]*sp+forward*cp;
    if(depth<=0.f)return false;
    float scale=1.f/std::tan(previous.fov_y_degrees*pi/360.f);
    float aspect=float(previous.width)/previous.height;
    previous_pixel={
        (.5f+.5f*horizontal*scale/(aspect*depth))*previous.width,
        (.5f-.5f*vertical*scale/depth)*previous.height};
    return std::isfinite(previous_pixel[0])&&
           std::isfinite(previous_pixel[1])&&
           previous_pixel[0]>=0.f&&previous_pixel[0]<previous.width&&
           previous_pixel[1]>=0.f&&previous_pixel[1]<previous.height;
}
} // namespace northstarcpp::render
