#pragma once
// Backend-neutral camera history for temporal AA, reprojection and velocity.
// No GPU objects, no ownership crossing the engine.render plugin ABI.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace northstarcpp::render {
struct TemporalCamera {
    std::array<float,3> position{};
    float yaw=0.f,pitch=0.f,roll=0.f;
    float fov_y_degrees=70.f,near_clip=.1f,far_clip=250.f;
    std::uint32_t width=0,height=0;
    bool first_person=true;
};
enum class TemporalReset : std::uint32_t {
    None=0,FirstFrame=1,Resize=2,Teleport=4,Rotation=8,
    Projection=16,CameraMode=32,External=64
};
inline constexpr TemporalReset operator|(TemporalReset a,TemporalReset b) {
    return static_cast<TemporalReset>(
        static_cast<std::uint32_t>(a)|static_cast<std::uint32_t>(b));
}
inline bool has_reset(TemporalReset state,TemporalReset bit) noexcept {
    return (static_cast<std::uint32_t>(state)&
            static_cast<std::uint32_t>(bit))!=0;
}
struct TemporalFrameState {
    TemporalCamera current{},previous{};
    TemporalReset reset=TemporalReset::FirstFrame;
    bool history_valid=false;
    std::uint64_t generation=0;
};
class TemporalHistoryTracker {
public:
    TemporalFrameState begin(TemporalCamera const& next) {
        if(next.width==0 || next.height==0 ||
           !std::isfinite(next.fov_y_degrees) ||
           next.fov_y_degrees<=1.f || next.fov_y_degrees>=179.f ||
           !std::isfinite(next.near_clip)||!std::isfinite(next.far_clip) ||
           next.near_clip<=0 || next.far_clip<=next.near_clip)
            throw std::invalid_argument("Invalid temporal camera projection");
        for(float v:next.position)
            if(!std::isfinite(v))
                throw std::invalid_argument("Invalid temporal camera position");
        if(!std::isfinite(next.yaw)||!std::isfinite(next.pitch)||
           !std::isfinite(next.roll))
            throw std::invalid_argument("Invalid temporal camera rotation");
        TemporalReset reason=TemporalReset::None;
        if(!initialized_)reason=reason|TemporalReset::FirstFrame;
        if(explicit_reset_)reason=reason|TemporalReset::External;
        if(initialized_) {
            if(current_.width!=next.width||current_.height!=next.height)
                reason=reason|TemporalReset::Resize;
            float distance_squared=0.f;
            for(int i=0;i<3;++i) {
                float d=current_.position[i]-next.position[i];
                distance_squared+=d*d;
            }
            if(distance_squared>400.f)reason=reason|TemporalReset::Teleport;
            constexpr float pi=3.14159265358979323846f;
            auto delta=[](float a,float b) {
                return std::atan2(std::sin(a-b),std::cos(a-b));
            };
            if(std::abs(delta(current_.yaw,next.yaw))>.60f||
               std::abs(delta(current_.pitch,next.pitch))>.60f||
               std::abs(delta(current_.roll,next.roll))>.60f)
                reason=reason|TemporalReset::Rotation;
            if(std::abs(current_.fov_y_degrees-next.fov_y_degrees)>3.f ||
               std::abs(current_.near_clip-next.near_clip)>1.e-4f ||
               std::abs(current_.far_clip-next.far_clip)>
                   std::max(.05f,.01f*current_.far_clip))
                reason=reason|TemporalReset::Projection;
            if(current_.first_person!=next.first_person)
                reason=reason|TemporalReset::CameraMode;
        }
        if(reason!=TemporalReset::None)++generation_;
        TemporalFrameState result{
            next,initialized_?current_:next,reason,
            initialized_ && reason==TemporalReset::None,generation_};
        current_=next;
        initialized_=true;
        explicit_reset_=false;
        return result;
    }
    void invalidate() noexcept {explicit_reset_=true;}
    std::uint64_t generation() const noexcept{return generation_;}
private:
    TemporalCamera current_{};
    bool initialized_=false,explicit_reset_=false;
    std::uint64_t generation_=0;
};

// Pixel velocity (previous minus current), useful for TAA reprojection and
// per-object motion blur. Returns false for clipped/nonfinite projections;
// a GPU velocity attachment must compute this for deformed geometry too.
inline bool temporal_project(TemporalCamera const& camera,
                             std::array<float,3> const& world,
                             std::array<float,2>& pixel) noexcept {
    float const dx=world[0]-camera.position[0];
    float const dy=world[1]-camera.position[1];
    float const dz=world[2]-camera.position[2];
    float const cy=std::cos(camera.yaw),sy=std::sin(camera.yaw);
    float const cp=std::cos(camera.pitch),sp=std::sin(camera.pitch);
    float const horizontal=dx*cy-dz*sy;
    float const forward=dx*sy+dz*cy;
    float const vertical=dy*cp-forward*sp;
    float const distance=dy*sp+forward*cp;
    if(!std::isfinite(distance)||distance<=camera.near_clip||
       distance>=camera.far_clip)return false;
    float const scale=1.f/std::tan(camera.fov_y_degrees*
                                   3.14159265358979323846f/360.f);
    float const aspect=float(camera.width)/camera.height;
    float const nx=horizontal*scale/(aspect*distance);
    float const ny=vertical*scale/distance;
    pixel={.5f*(nx+1.f)*camera.width,
           .5f*(1.f-ny)*camera.height};
    return std::isfinite(pixel[0])&&std::isfinite(pixel[1]);
}
inline bool temporal_pixel_velocity(TemporalFrameState const& history,
                                    std::array<float,3> const& world,
                                    std::array<float,2>& velocity) noexcept {
    if(!history.history_valid)return false;
    std::array<float,2> now{},previous{};
    if(!temporal_project(history.current,world,now) ||
       !temporal_project(history.previous,world,previous))return false;
    velocity={previous[0]-now[0],previous[1]-now[1]};
    return true;
}
} // namespace northstarcpp::render
