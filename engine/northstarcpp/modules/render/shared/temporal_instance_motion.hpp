#pragma once
// Reference motion interpretation for stable-ID render instances.
#include "temporal_instance_history.hpp"
#include "temporal_motion_vectors.hpp"
#include <array>
#include <cstddef>
#include <cmath>

namespace northstarcpp::render {
inline bool temporal_transform_point(
    const std::array<float,16>& m,
    const std::array<float,3>& local,
    std::array<float,3>& result) noexcept {
    for(std::size_t row=0;row<3;++row) {
        const float value=local[0]*m[row]+local[1]*m[4+row]+
                          local[2]*m[8+row]+m[12+row];
        if(!std::isfinite(value))return false;
        result[row]=value;
    }
    return true;
}
inline bool temporal_instance_pixel_velocity(
    const TemporalInstanceHistory::Pair& pair,
    const TemporalFrameState& frame,
    const std::array<float,3>& local,
    const TemporalJitter& jitter,
    std::array<float,2>& velocity) noexcept {
    if(!pair.valid || !pair.current || !pair.previous)return false;
    std::array<float,3> now{},before{};
    return temporal_transform_point(pair.current->transform,local,now) &&
           temporal_transform_point(pair.previous->transform,local,before) &&
           temporal_vertex_pixel_velocity(frame,now,before,jitter,velocity);
}
// Skin input palette is world-space (native GPU skinning ABI).
inline bool temporal_instance_skin_pixel_velocity(
    const TemporalInstanceHistory::Pair& pair,
    const TemporalFrameState& frame,
    const std::array<float,3>& local,
    const std::array<std::uint16_t,8>& joints,
    const std::array<float,8>& weights,
    const TemporalJitter& jitter,
    std::array<float,2>& velocity) noexcept {
    if(!pair.valid || !pair.current || !pair.previous ||
       pair.current->joints==0u)return false;
    return temporal_skinned_pixel_velocity(frame,local,joints,weights,
        pair.current->palette.data(),pair.previous->palette.data(),
        pair.current->joints,jitter,velocity);
}
} // namespace northstarcpp::render
