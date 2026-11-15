#pragma once
// Native CSM planning: practical split distances, frustum-sphere fitting and
// light-space texel-grid stabilization. Matrices use row-vector multiplication
// and D3D zero-to-one depth, matching NorthStar's deferred shader convention.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace northstarcpp::render {
inline constexpr std::uint32_t shadow_cascade_count=3;
struct ShadowVec3 { double x,y,z; };
inline ShadowVec3 operator+(ShadowVec3 a,ShadowVec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline ShadowVec3 operator-(ShadowVec3 a,ShadowVec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline ShadowVec3 operator*(ShadowVec3 a,double k){return {a.x*k,a.y*k,a.z*k};}
inline double dot_shadow(ShadowVec3 a,ShadowVec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline ShadowVec3 cross_shadow(ShadowVec3 a,ShadowVec3 b) {
 return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline ShadowVec3 unit_shadow(ShadowVec3 a) {
 const double n=std::sqrt(dot_shadow(a,a));
 if(!std::isfinite(n)||n<1.e-10)throw std::invalid_argument("Degenerate CSM direction");
 return a*(1.0/n);
}
inline std::array<double,4> project_shadow(const std::array<float,16>& m,
                                           std::array<double,4> v) {
 std::array<double,4> out{};
 for(int c=0;c<4;++c)for(int k=0;k<4;++k)
     out[c]+=v[k]*static_cast<double>(m[k*4+c]);
 return out;
}
struct ShadowCascade {
 std::array<float,16> matrix{};
 float near_distance=0,far_distance=0,world_texel=0;
};
struct ShadowCascadePlan {
 std::array<ShadowCascade,shadow_cascade_count> cascades{};
};
inline ShadowCascadePlan build_shadow_cascades(
   const std::array<float,16>& view_projection,
   const std::array<float,16>& inverse_view_projection,
   std::array<float,3> camera_position,std::array<float,3> camera_forward,
   float near_plane,float far_plane,std::uint32_t shadow_resolution,
   float lambda=0.70f) {
 if(!(near_plane>0)||!(far_plane>near_plane)||!std::isfinite(far_plane)||
    !std::isfinite(near_plane)||!(lambda>=0&&lambda<=1)||
    shadow_resolution<32||shadow_resolution>8192)
    throw std::invalid_argument("Invalid CSM camera parameters");
 const ShadowVec3 eye{camera_position[0],camera_position[1],camera_position[2]};
 const auto forward=unit_shadow({camera_forward[0],camera_forward[1],camera_forward[2]});
 const ShadowVec3 to_sun=unit_shadow({.453,.824,.302});
 const auto right=unit_shadow(cross_shadow({0,1,0},to_sun));
 const auto up=unit_shadow(cross_shadow(to_sun,right));
 ShadowCascadePlan plan;
 float last=near_plane;
 for(std::uint32_t i=0;i<shadow_cascade_count;++i) {
   const float t=float(i+1)/shadow_cascade_count;
   const double logarithmic=near_plane*std::pow(far_plane/near_plane,t);
   const double uniform=near_plane+(far_plane-near_plane)*t;
   const float distance=static_cast<float>(lambda*logarithmic+(1-lambda)*uniform);
   auto& cascade=plan.cascades[i];
   cascade.near_distance=last;cascade.far_distance=distance;
   auto ndc_depth=[&](float d){
     const auto projected=project_shadow(view_projection,{
       eye.x+forward.x*d,eye.y+forward.y*d,eye.z+forward.z*d,1.});
     if(std::abs(projected[3])<1.e-10)
       throw std::invalid_argument("Invalid CSM projection W");
     return projected[2]/projected[3];
   };
   const auto z0=ndc_depth(last),z1=ndc_depth(distance);
   std::array<ShadowVec3,8> corners{};
   ShadowVec3 center{0,0,0};
   for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
      const auto p=project_shadow(inverse_view_projection,{
          x?1.:-1.,y?1.:-1.,z?z1:z0,1.});
      if(std::abs(p[3])<1.e-10||!std::isfinite(p[3]))
        throw std::invalid_argument("Invalid unprojected CSM corner");
      const auto world=ShadowVec3{p[0]/p[3],p[1]/p[3],p[2]/p[3]};
      corners[(z*4)+(y*2)+x]=world;center=center+world;
   }
   center=center*(1./8.);
   double radius=0;
   for(auto corner:corners){
     const auto v=corner-center;
     radius=std::max(radius,std::sqrt(dot_shadow(v,v)));
   }
   radius=std::ceil((radius*1.02+0.25)*16.)/16.;
   if(!std::isfinite(radius)||radius<=0)
     throw std::invalid_argument("Invalid CSM frustum radius");
   const double texel=(radius*2.)/shadow_resolution;
   const double center_x=std::round(dot_shadow(center,right)/texel)*texel;
   const double center_y=std::round(dot_shadow(center,up)/texel)*texel;
   // A fixed margin gives shadow casters room outside the camera slice.
   const double margin=std::max(20.,radius*.25);
   const double depth_start=dot_shadow(center,to_sun)+radius+margin;
   const double depth_range=2.*(radius+margin);
   const auto row=[&](ShadowVec3 axis,int which) {
      cascade.matrix[which]=static_cast<float>(axis.x);
      cascade.matrix[4+which]=static_cast<float>(axis.y);
      cascade.matrix[8+which]=static_cast<float>(axis.z);
   };
   row(right*(1./radius),0);
   row(up*(1./radius),1);
   row(to_sun*(-1./depth_range),2);
   cascade.matrix[12]=static_cast<float>(-center_x/radius);
   cascade.matrix[13]=static_cast<float>(-center_y/radius);
   cascade.matrix[14]=static_cast<float>(depth_start/depth_range);
   cascade.matrix[15]=1.f;
   cascade.world_texel=static_cast<float>(texel);
   last=distance;
 }
 return plan;
}
} // namespace northstarcpp::render
