#include "../engine/northstarcpp/modules/render/shared/gbuffer_semantics.hpp"
#include "../engine/northstarcpp/modules/render/shared/height_fog.hpp"
#include "../engine/northstarcpp/modules/render/d3d12/renderer_pass_graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <cstdio>
#define REQUIRE(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
  using namespace northstarcpp::render;
  REQUIRE(gbuffer_color_count(GbufferVersion::Legacy)==3);
  REQUIRE(gbuffer_color_count(GbufferVersion::Extended)==4);
  REQUIRE(gbuffer_bytes_per_pixel(GbufferVersion::Extended)==24);
  REQUIRE(parse_gbuffer_debug("material_ao")==GbufferDebug::MaterialAo);
  REQUIRE(parse_gbuffer_debug("clearcoat")==GbufferDebug::Clearcoat);
  REQUIRE(parse_gbuffer_debug("sheen")==GbufferDebug::Sheen);
  REQUIRE(parse_gbuffer_debug("bad_name")==GbufferDebug::Off);
  namespace D=northstarcpp::render::d3d12::detail;
  auto graph=D::build_frame_graph(true,true,true,true,true);
  auto const& passes=graph.passes();
  auto const& resources=graph.resources();
  auto const& geometry=passes.at(1);
  REQUIRE(resources.at(static_cast<unsigned>(D::FrameResource::GBufferAuxiliary)).name==
      "G-buffer v2 auxiliary");
  auto aux=static_cast<ResourceId>(D::FrameResource::GBufferAuxiliary);
  auto depth=static_cast<ResourceId>(D::FrameResource::Depth);
  REQUIRE(std::find(geometry.writes.begin(),geometry.writes.end(),aux)!=geometry.writes.end());
  REQUIRE(std::find(passes.at(2).reads.begin(),passes.at(2).reads.end(),aux)!=
      passes.at(2).reads.end());
  auto const& alpha=passes.at(3);
  REQUIRE(alpha.name=="forward translucency");
  REQUIRE(std::find(alpha.reads.begin(),alpha.reads.end(),depth)!=alpha.reads.end());
  REQUIRE(std::find(alpha.writes.begin(),alpha.writes.end(),depth)==alpha.writes.end());
  REQUIRE(std::any_of(alpha.usages.begin(),alpha.usages.end(),
      [=](auto const& usage){return usage.resource==depth &&
          usage.access==ResourceAccess::DepthRead;}));
  REQUIRE(passes.size()==10); // shadow/gbuffer/lighting/alpha/motion/fx4/present
  auto compiled=graph.compile();
  REQUIRE(compiled.passes.size()==passes.size());
  REQUIRE(compiled.lifetimes[aux].second>=2);
  // Inactive effects create no uninitialized reads or hidden material draws.
  auto minimal=D::build_frame_graph(false,false,false,false,false);
  REQUIRE(minimal.compile().passes.size()==9);
  REQUIRE(height_fog_extinction(0,0,0,.05f)==0);
  REQUIRE(height_fog_extinction(20,0,0,0)==0);
  auto f1=height_fog_extinction(10,0,0,.025f);
  auto f2=height_fog_extinction(80,0,0,.025f);
  auto upper=height_fog_extinction(80,25,25,.025f);
  REQUIRE(f1>0 && f2>f1 && f2<=.96f && upper<f2);
  REQUIRE(std::isfinite(height_fog_extinction(100000,0,0,.25f)));
#ifdef _WIN32
  _putenv_s("NORTHSTARCPP_HEIGHT_FOG_DENSITY","0.017");
  REQUIRE(std::abs(height_fog_density_from_environment()-.017f)<1.e-5f);
  _putenv_s("NORTHSTARCPP_HEIGHT_FOG_DENSITY","nan");
  REQUIRE(height_fog_density_from_environment()==0.f);
  _putenv_s("NORTHSTARCPP_HEIGHT_FOG_DENSITY","4.5");
  REQUIRE(height_fog_density_from_environment()==0.f);
  _putenv_s("NORTHSTARCPP_HEIGHT_FOG_DENSITY","");
#endif
  std::puts("NorthStar GBuffer v2, RenderGraph/alpha-depth and fog: PASS");
  return 0;
}
