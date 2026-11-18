#include <northstarcpp/world_foundation.hpp>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
using namespace northstarcpp::world;
static bool near(double a,double b,double e=1e-4){return std::abs(a-b)<e;}
static void check(bool v){if(!v)throw std::runtime_error("World foundation smoke assertion");}
int main(){
  TerrainConfig cfg;
  TerrainProvider p(cfg);
  auto a=p.generate({0,0},0),b=p.generate({1,0},2);
  check(a.render.vertices.size()==33*33+4*33);
  check(a.render.indices.size()==6*32*32+24*32);
  check(a.collision.vertices.size()==17*17);
  check(b.collision.vertices.size()==a.collision.vertices.size());
  for(unsigned z=0;z<=8;z++){
    check(near(a.render.vertices[(z*4)*33+32].position.y,
               b.render.vertices[z*9].position.y,1e-5));
    check(near(b.render.vertices[z*9].position.y,p.height(64.,z*8.),1e-5));
  }
  auto norm=p.normal(1.23,5.67);
  check(near(std::sqrt(norm.x*norm.x+norm.y*norm.y+norm.z*norm.z),1.,1e-5));
  TerrainStreamer s(cfg);
  auto initial=s.update({8,0,8});
  check(initial.resident_count==49&&initial.uploaded.size()==49);
  check(s.update({8,0,8}).uploaded.empty());
  auto moved=s.update({70,0,8});
  check(moved.resident_count==49&&!moved.evicted.empty()&&!moved.uploaded.empty());
  bool fails=false;
  try{TerrainConfig bad;bad.quads_per_axis=31;TerrainProvider nope(bad);}
  catch(std::invalid_argument const&){fails=true;}
  check(fails);
  CityRoot home(1),target(2);
  home.set_motion({100,0,30},0.,{12,0,0},0.);
  target.set_motion({130,0,30},0.,{-3,0,0},0.);
  auto actor=home.transfer_to(target,{{5,2,3},{1,0,0}});
  check(near(actor.position.x,-25.)&&near(actor.velocity.x,16.));
  home.advance(1./120.);
  check(near(home.position().x,100.1));
  home.set_motion({0,0,0},0.,{0,0,0},2.);
  auto support=home.support_velocity({4,0,8});
  check(near(support.x,16.)&&near(support.z,-8.));
  home.advance(1./120.);
  auto recovered=home.to_local(home.to_world({1,0,0}));
  check(near(recovered.x,1.)&&near(recovered.z,0.));
  CityGpuTransformBatch gpu(1);
  gpu.set_instances({{100,{1,0,2},0.},{101,{0,2,3},0.}});
  check(gpu.collect_dirty(home).size()==2);
  check(gpu.collect_dirty(home).empty());
  home.advance(1./120.);
  check(gpu.collect_dirty(home).size()==2);
  fails=false;
  try{gpu.set_instances({{100,{1,0,1},0.},{100,{1,0,1},0.}});}
  catch(std::invalid_argument const&){fails=true;}
  check(fails&&gpu.instance_count()==2);
  // Sea of Rust: world-anchored terrain over tens of kilometres, with
  // deterministic backtracking, non-flat relief and bounded residency.
  TerrainConfig rustsea;
  rustsea.seed=20261117u;
  rustsea.width_m=64.0;
  rustsea.radius=4;
  rustsea.quads_per_axis=32;
  rustsea.collision_quads=16;
  rustsea.max_lod=3;
  rustsea.vertical_scale_m=5.;
  rustsea.base_height_m=-1.3;
  TerrainProvider heightfield(rustsea);
  double lower=1e6,upper=-1e6;
  for(int ix=-40;ix<=40;ix++)
  for(int iz=-40;iz<=40;iz++){
    auto h=heightfield.height(ix*17.75,iz*16.75);
    lower=std::min(lower,h);upper=std::max(upper,h);
    check(std::isfinite(h)&&h>=-3.81&&h<=1.21);
  }
  check(upper-lower>.5 && upper-lower<5.01);
  for(int x:{-1536,-512,0,512,1536}) {
    for(int z:{-1536,-512,0,512,1536}) {
      ChunkKey akey{int(x/64),int(z/64)};
      auto first=heightfield.generate(akey,0);
      auto again=heightfield.generate(akey,0);
      check(first.render.vertices.size()==again.render.vertices.size());
      for(std::size_t i=0;i<first.render.vertices.size();++i)
        check(first.render.vertices[i].position.y==again.render.vertices[i].position.y);
      auto adjacent=heightfield.generate({akey.x+1,akey.z},0);
      for(int k=0;k<=32;k++)
        check(near(first.render.vertices[k*33+32].position.y,
                   adjacent.render.vertices[k*33].position.y,1e-6));
    }
  }
  TerrainStreamer infinite_stream(rustsea);
  std::size_t loaded=0,evicted=0;
  std::uint64_t generation=0;
  for(int tile=-156;tile<=156;tile++) {
    auto delta=infinite_stream.update({0,0,tile*64.},{0,0,26});
    loaded+=delta.uploaded.size();
    evicted+=delta.evicted.size();
    generation=delta.generation;
    check(delta.resident_count==81);
    check(infinite_stream.resident_count()==81);
    check(delta.uploaded.size()<=81 && delta.evicted.size()<=81);
  }
  auto returned=infinite_stream.update({0,0,0},{0,0,0});
  check(returned.resident_count==81 && !returned.uploaded.empty());
  check(generation>200 && loaded>2000 && evicted>1000);
  std::cout<<"PASS Rust Sea procedural heightfield 20km travel, "
           <<"relief="<<(upper-lower)<<" m, loaded="<<loaded
           <<" evicted="<<evicted<<" bounded=81 chunks"<<'\n';

  FloatingOrigin origin(512.,64.);
  check(!origin.proposal({100.,0.,100.}));
  Vec3d const true_position{1234567.25,5.,-765432.75};
  auto const rebase=origin.proposal(true_position);
  check(rebase.has_value());
  origin.commit(*rebase);
  auto const local=origin.to_local(true_position);
  check(std::abs(local.x)<64. && std::abs(local.z)<64.);
  auto const recovered_position=origin.to_absolute(local);
  check(near(true_position.x,recovered_position.x,1.e-7) &&
        near(true_position.z,recovered_position.z,1.e-7) &&
        near(true_position.y,recovered_position.y,1.e-7));
  check(!origin.proposal(true_position));
  TerrainAsyncStreamer background(rustsea);
  auto bootstrap=background.update({0,0,0});
  check(bootstrap && bootstrap->uploaded.size()==81 &&
        bootstrap->resident_count==81);
  auto scheduled=background.update({0,0,512},{0,0,25});
  check(!scheduled && background.pending());
  std::optional<TerrainDelta> async_result;
  for(int spin=0;spin<5000 && !async_result;spin++){
    async_result=background.update({0,0,512},{0,0,25});
    if(!async_result)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(async_result && async_result->resident_count==81);
  check(!async_result->uploaded.empty() && !async_result->evicted.empty());
  // A fast new location is coalesced while a prior worker is running.
  auto later=background.update({0,0,4096},{0,0,50});
  check(!later && background.pending());
  std::optional<TerrainDelta> reached;
  for(int spin=0;spin<5000 && !reached;spin++){
    reached=background.update({0,0,4096},{0,0,50});
    if(!reached)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(reached && reached->resident_count==81);
  std::cout<<"PASS async chunk generation: first ring ready, "
           <<"long-distance requests moved to worker, nonblocking polls"<<'\n';

  std::cout<<"PASS terrain=49 chunks, edge continuity, collision mesh, CityRoot transfer, GPU transforms\n";
}
