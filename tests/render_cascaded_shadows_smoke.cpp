#include "../engine/northstarcpp/modules/render/shared/cascaded_shadows.hpp"
#include "../engine/northstarcpp/modules/render/shared/deferred_math.hpp"
#include <cassert>
#include <initializer_list>
#include <cmath>
#include <stdexcept>
int main(){
  using namespace northstarcpp::render;
  // D3D perspective, row-vector input, camera looking down +Z.
  constexpr float near=1.f,far=120.f;
  float a=far/(far-near),b=-far*near/(far-near);
  std::array<float,16> vp={1,0,0,0, 0,1,0,0, 0,0,a,1, 0,0,b,0};
  auto inv=inverse_view_projection(vp.data());
  auto plan=build_shadow_cascades(vp,inv,{0,0,0},{0,0,1},near,far,512);
  float previous=near;
  for(auto const& c:plan.cascades){
    assert(c.near_distance==previous);
    assert(c.far_distance>c.near_distance);
    assert(c.world_texel>0.f);
    for(float k:c.matrix)assert(std::isfinite(k));
    // Every near/far corner of this slice must fit the corresponding
    // stabilized shadow projection, including valid D3D [0,1] depth.
    for(float d:{c.near_distance,c.far_distance}) {
        auto projected=project_shadow(vp,{0,0,d,1});
        double z_ndc=projected[2]/projected[3];
        for(double y:{-1.,1.})for(double x:{-1.,1.}) {
            auto h=project_shadow(inv,{x,y,z_ndc,1});
            auto clip=project_shadow(c.matrix,{
                h[0]/h[3],h[1]/h[3],h[2]/h[3],1});
            assert(std::abs(clip[0])<=1.0001);
            assert(std::abs(clip[1])<=1.0001);
            assert(clip[2]>=-0.0001 && clip[2]<=1.0001);
        }
    }
    previous=c.far_distance;
  }
  assert(std::abs(previous-far)<0.00001f);
  bool rejected=false;
  try{(void)build_shadow_cascades(vp,inv,{0,0,0},{0,0,1},0,far,512);}
  catch(std::invalid_argument const&){rejected=true;}
  assert(rejected);
  // Moving camera much less than a texel with fixed VP rotation should
  // preserve the projected center after light-space snapping.
  auto shifted=vp;
  shifted[12]-=0.00001f;
  auto inv_shifted=inverse_view_projection(shifted.data());
  auto p2=build_shadow_cascades(shifted,inv_shifted,{.00001f,0,0},{0,0,1},near,far,512);
  for(unsigned i=0;i<shadow_cascade_count;++i)
    assert(std::abs(plan.cascades[i].matrix[12]-p2.cascades[i].matrix[12])<0.01f);
}
