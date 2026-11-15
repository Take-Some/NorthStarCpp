// NorthStarCpp: provider-independent, camera-adaptive editor ground grid.
// Builds a bounded set of world-space lines that tracks the visible XY
// projection onto the XZ ground plane. No scene objects are created.
#pragma once
#include <northstarcpp/camera.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace northstarcpp::editor_grid {
struct Line {
    Vector3 from{},to{};
    std::uint32_t color=0;
    bool major=false;
    bool origin=false;
};
struct Layout {
    std::vector<Line> lines;
    float minor_step=1.f;
    float major_step=10.f;
    float radius=100.f;
    Vector3 center{};
};
inline float dot(Vector3 a,Vector3 b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
inline Vector3 direction(CameraPose const& p) {
    return {std::sin(p.yaw)*std::cos(p.pitch),std::sin(p.pitch),
            std::cos(p.yaw)*std::cos(p.pitch)};
}
inline float preferredStep(float requested) {
    if(!std::isfinite(requested)||requested<=0.f)return 1.f;
    float const exponent=std::floor(std::log10(requested));
    float const order=std::pow(10.f,exponent);
    float const normalized=requested/order;
    float const factor=normalized<=1.f?1.f:normalized<=2.f?2.f:
                       normalized<=5.f?5.f:10.f;
    return factor*order;
}
inline Layout build(CameraPose const& pose,int pixel_width,int pixel_height) {
    Layout result;
    if(pixel_width<32 || pixel_height<32 ||
       !std::isfinite(pose.position.x)||
       !std::isfinite(pose.position.y)||
       !std::isfinite(pose.position.z)||
       !std::isfinite(pose.yaw)||!std::isfinite(pose.pitch))return result;
    float const fov=std::clamp(pose.fov_y_degrees,10.f,135.f)*.01745329252f;
    float const aspect=float(pixel_width)/float(pixel_height);
    float const slope=std::tan(fov*.5f);
    auto const forward=direction(pose);
    Vector3 const up{-std::sin(pose.yaw)*std::sin(pose.pitch),
        std::cos(pose.pitch),-std::cos(pose.yaw)*std::sin(pose.pitch)};
    // Follow a visible ground intersection, not the world origin. This
    // prevents the grid ending after flying a few hundred meters away.
    float range=std::clamp(std::abs(pose.position.y)*4.f+70.f,100.f,12000.f);
    for(float v:{0.f,-.5f,-.85f}) {
        auto const ray=Vector3{forward.x+up.x*slope*v,
            forward.y+up.y*slope*v,forward.z+up.z*slope*v};
        if(std::abs(ray.y)<.0005f)continue;
        auto const t=-pose.position.y/ray.y;
        if(t>1.f && t<20000.f) {
            range=std::clamp(t,30.f,12000.f);
            break;
        }
    }
    result.center={
        pose.position.x+forward.x*range,0.f,
        pose.position.z+forward.z*range};
    // Cover both the visible ground footprint and a generous distance
    // beyond the edge of the screen. Line count stays bounded when zooming.
    auto const camera_distance=std::max(10.f,range);
    auto const width_at_depth=2.f*slope*camera_distance*aspect;
    auto const height_at_depth=2.f*slope*camera_distance;
    result.radius=std::clamp(
        std::max({width_at_depth,height_at_depth,
                  std::abs(pose.position.y)*3.f,100.f})*1.65f,
        100.f,30000.f);
    float const metres_per_pixel=
        2.f*slope*camera_distance/float(pixel_height);
    result.minor_step=preferredStep(metres_per_pixel*28.f);
    // Keep major divisions 5x apart; use highlighted origin axes.
    result.major_step=result.minor_step*5.f;
    float const step=result.minor_step;
    float const span=result.radius;
    // A bounded 181x2 line budget, regardless of scene size or zoom.
    auto append=[&](int axis) {
        float const center=axis==0?result.center.x:result.center.z;
        long long const base=static_cast<long long>(
            std::floor(double(center)/double(step)));
        int const half=std::clamp(
            int(std::ceil(span/step)),12,88);
        for(int k=-half;k<=half;++k) {
            long long const index=base+k;
            double const world=double(index)*double(step);
            if(!std::isfinite(world)||std::abs(world)>1.e7)continue;
            bool const origin=index==0;
            bool const major=(index%5)==0;
            std::uint32_t const color=origin?
                (axis==0?0x558bb7u:0xb76369u):
                (major?0x51647au:0x324358u);
            Line line{};
            line.major=major;line.origin=origin;line.color=color;
            if(axis==0) {
                line.from={float(world),0.f,result.center.z-span};
                line.to={float(world),0.f,result.center.z+span};
            }else {
                line.from={result.center.x-span,0.f,float(world)};
                line.to={result.center.x+span,0.f,float(world)};
            }
            result.lines.push_back(line);
        }
    };
    append(0);
    append(1);
    return result;
}
} // namespace northstarcpp::editor_grid
