#pragma once
// Backend-neutral reference for camera/object/skinned pixel motion and
// subpixel jitter. The GPU velocity buffer in each provider must preserve
// these definitions; this header intentionally owns no GPU resources.
#include "temporal_history.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace northstarcpp::render {

// Units: pixel centers, (x right, y down). Reprojection is
// previous_pixel - current_pixel. This sign is shared by Vulkan and D3D12.
struct TemporalJitter {
    std::array<float,2> current{};
    std::array<float,2> previous{};
};
inline float temporal_halton(std::uint32_t index,
                             std::uint32_t radix) noexcept {
    float result=0.f,inv=1.f/float(radix);
    while(index!=0u) {
        result+=float(index%radix)*inv;
        index/=radix;
        inv/=float(radix);
    }
    return result;
}
class TemporalJitterSequence {
public:
    TemporalJitter next(TemporalFrameState const& camera) noexcept {
        if(!camera.history_valid || camera.generation!=generation_) {
            generation_=camera.generation;
            index_=0u;
            current_=sample(index_);
            // On frame 0, no fake previous-frame motion is allowed.
            return {current_,current_};
        }
        auto previous=current_;
        index_=(index_+1u)%8u;
        current_=sample(index_);
        return {current_,previous};
    }
    void invalidate() noexcept {
        generation_=std::numeric_limits<std::uint64_t>::max();
    }
    static std::array<float,2> sample(std::uint32_t phase) noexcept {
        phase=phase%8u+1u;
        return {temporal_halton(phase,2u)-.5f,
                temporal_halton(phase,3u)-.5f};
    }
private:
    std::uint64_t generation_=std::numeric_limits<std::uint64_t>::max();
    std::uint32_t index_=0;
    std::array<float,2> current_{};
};

// Supports camera AND moving object motion, unlike the static-world helper
// temporal_pixel_velocity(). Call with previous/current skinned positions
// to capture joint and morph deformation in addition to camera movement.
inline bool temporal_vertex_pixel_velocity(
    TemporalFrameState const& history,
    std::array<float,3> const& current_world,
    std::array<float,3> const& previous_world,
    TemporalJitter const& jitter,
    std::array<float,2>& out_pixels) noexcept {
    if(!history.history_valid)return false;
    std::array<float,2> current{},previous{};
    if(!temporal_project(history.current,current_world,current) ||
       !temporal_project(history.previous,previous_world,previous))
        return false;
    for(unsigned i=0;i<2u;++i) {
        out_pixels[i]=(previous[i]+jitter.previous[i])-
                      (current[i]+jitter.current[i]);
        if(!std::isfinite(out_pixels[i]))return false;
    }
    return true;
}

// Exact 8-influence native GPU palette ABI: x'=x*m0+y*m4+z*m8+m12.
// The caller supplies stable identity and both palettes. Missing/invalid
// previous palettes MUST invalidate history, not fabricate static motion.
inline bool temporal_skin_world_position(
    std::array<float,3> const& local_position,
    std::array<std::uint16_t,8> const& joint_ids,
    std::array<float,8> const& joint_weights,
    const float* palette,std::uint32_t joints,
    std::array<float,3>& out_world) noexcept {
    if(!palette||joints==0u||joints>256u)return false;
    out_world={0.f,0.f,0.f};
    float total_weight=0.f;
    for(unsigned influence=0;influence<8u;++influence) {
        const float weight=joint_weights[influence];
        if(!std::isfinite(weight)||weight<0.f)return false;
        if(weight==0.f)continue;
        const auto joint=joint_ids[influence];
        if(joint>=joints)return false;
        auto const* m=palette+std::size_t(joint)*16u;
        for(unsigned row=0;row<3u;++row) {
            const float position=local_position[0]*m[row]+
                local_position[1]*m[4u+row]+
                local_position[2]*m[8u+row]+m[12u+row];
            if(!std::isfinite(position))return false;
            out_world[row]+=position*weight;
        }
        total_weight+=weight;
    }
    if(!(total_weight>0.f)||!std::isfinite(total_weight))return false;
    for(float value:out_world)
        if(!std::isfinite(value))return false;
    return true;
}
inline bool temporal_skinned_pixel_velocity(
    TemporalFrameState const& history,
    std::array<float,3> const& local_position,
    std::array<std::uint16_t,8> const& joint_ids,
    std::array<float,8> const& joint_weights,
    const float* current_palette,const float* previous_palette,
    std::uint32_t joints,
    TemporalJitter const& jitter,
    std::array<float,2>& velocity) noexcept {
    if(!history.history_valid)return false;
    std::array<float,3> current{},previous{};
    return temporal_skin_world_position(local_position,joint_ids,joint_weights,
               current_palette,joints,current)&&
           temporal_skin_world_position(local_position,joint_ids,joint_weights,
               previous_palette,joints,previous)&&
           temporal_vertex_pixel_velocity(history,current,previous,
                                          jitter,velocity);
}
} // namespace northstarcpp::render
