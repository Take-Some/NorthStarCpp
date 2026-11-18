// CameraPose -> conservative world-space six-plane SceneViewFrustum.
// yaw=0 faces +Z, pitch positive looks up; roll is visual-only in CameraPose.
#pragma once
#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene_world.hpp>
#include <array>
#include <cmath>
#include <stdexcept>

namespace northstarcpp {
inline SceneViewFrustum make_scene_view_frustum(CameraPose const& pose,float aspect) {
    if(!std::isfinite(aspect)||aspect<=0.f||aspect>100.f ||
       !std::isfinite(pose.yaw)||!std::isfinite(pose.pitch)||
       !std::isfinite(pose.fov_y_degrees)||
       pose.fov_y_degrees<1.f||pose.fov_y_degrees>=179.f||
       !std::isfinite(pose.near_plane)||!std::isfinite(pose.far_plane)||
       !(pose.near_plane>0.f)||!(pose.far_plane>pose.near_plane))
        throw std::invalid_argument("invalid camera projection for portal clipping");
    auto const cp=std::cos(pose.pitch),sp=std::sin(pose.pitch);
    auto const cy=std::cos(pose.yaw),sy=std::sin(pose.yaw);
    Vector3 const forward{sy*cp,sp,cy*cp};
    Vector3 const right{cy,0.f,-sy};
    Vector3 const up{-sy*sp,cp,-cy*sp};
    auto const add_scaled=[](Vector3 a,Vector3 b,float scale) {
        return Vector3{a.x+b.x*scale,a.y+b.y*scale,a.z+b.z*scale};
    };
    auto const cross=[](Vector3 a,Vector3 b) {
        return Vector3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    };
    auto const dot=[](Vector3 a,Vector3 b) {
        return a.x*b.x+a.y*b.y+a.z*b.z;
    };
    auto const h=std::tan(pose.fov_y_degrees*0.008726646259971648f);
    auto const v=h*aspect;
    auto const norm=[](Vector3 vec) {
        auto const len=std::sqrt(vec.x*vec.x+vec.y*vec.y+vec.z*vec.z);
        if(!std::isfinite(len)||!(len>1.e-7f))
            throw std::invalid_argument("invalid camera frustum plane");
        return Vector3{vec.x/len,vec.y/len,vec.z/len};
    };
    auto const left_dir=add_scaled(forward,right,-v);
    auto const right_dir=add_scaled(forward,right,v);
    auto const top_dir=add_scaled(forward,up,h);
    auto const bottom_dir=add_scaled(forward,up,-h);
    std::array<Vector3,6> normals{
        norm(cross(up,left_dir)),norm(cross(right_dir,up)),
        norm(cross(right,top_dir)),norm(cross(bottom_dir,right)),
        forward,Vector3{-forward.x,-forward.y,-forward.z}
    };
    SceneViewFrustum result;
    for(std::size_t i=0;i<6;++i) {
        result.planes[i].normal=normals[i];
        result.planes[i].offset=-dot(normals[i],pose.position);
    }
    result.planes[4].offset-=pose.near_plane;
    result.planes[5].offset+=pose.far_plane;
    return result;
}
} // namespace northstarcpp
