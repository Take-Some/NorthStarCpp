#include "../engine/northstarcpp/modules/viewport/viewport_frame_submission.hpp"
#include <cstdint>
#include <stdexcept>
#include <vector>

#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)

namespace {
struct Probe {unsigned vertices=0,cubes=0,materials=0,keys=0; bool reject=false;};
int pick_vertices(void* user,NorthStarRenderPickVertex const* data,std::uint32_t n) {
    auto& p=*static_cast<Probe*>(user);p.vertices=n;
    return (n==0 || data) && !p.reject;
}
int pick_cubes(void* user,std::uint64_t const* data,std::uint32_t n) {
    auto& p=*static_cast<Probe*>(user);p.cubes=n;
    return (n==0 || data) && !p.reject;
}
int set_v1(void* user,NorthStarMaterialBatch const* data,std::uint32_t n) {
    auto& p=*static_cast<Probe*>(user);p.materials=n;
    return (n==0 || data) && !p.reject;
}
int set_refs(void* user,char const* const* data,std::uint32_t n) {
    auto& p=*static_cast<Probe*>(user);p.keys=n;
    return (n==0 || data) && !p.reject;
}
}
int main() {
    using namespace northstarcpp::viewport_detail;
    Probe probe{};
    NorthStarRenderPickApiV2 pick{};
    pick.set_world_triangles=&pick_vertices;
    pick.set_cube_entities=&pick_cubes;
    std::vector<NorthStarRenderPickVertex> triangles(3);
    std::vector<std::uint64_t> cubes{10,20};
    CHECK(submit_pick_streams(&probe,&pick,true,true,triangles,cubes));
    CHECK(probe.vertices==3 && probe.cubes==2);
    CHECK(submit_pick_streams(&probe,&pick,false,false,triangles,cubes));
    CHECK(probe.vertices==0 && probe.cubes==0);
    probe.reject=true;
    CHECK(!submit_pick_streams(&probe,&pick,true,true,triangles,cubes));
    probe.reject=false;

    NorthStarRenderMaterialsApi materials{};
    materials.set_batches=&set_v1;
    NorthStarRenderShaderReloadApi reload{};
    reload.set_material_references=&set_refs;
    std::vector<NorthStarMaterialBatch> v1(1);
    std::vector<NorthStarMaterialBatchV2> v2;
    std::vector<const char*> keys{"materials/example.ymat"};
    CHECK(submit_material_streams(&probe,&materials,nullptr,&reload,nullptr,
        false,v1,v2,keys)==MaterialSubmitStatus::ok);
    CHECK(probe.materials==1 && probe.keys==1);
    CHECK(submit_material_streams(&probe,&materials,nullptr,nullptr,nullptr,
        true,v1,v2,keys)==MaterialSubmitStatus::missing_graph_provider);
    probe.reject=true;
    CHECK(submit_material_streams(&probe,&materials,nullptr,nullptr,nullptr,
        false,v1,v2,keys)==MaterialSubmitStatus::material_upload_failed);
    probe.reject=false;
    CHECK(submit_material_streams(&probe,&materials,nullptr,&reload,nullptr,
        false,{},v2,{})==MaterialSubmitStatus::ok);
    CHECK(probe.materials==0 && probe.keys==0);
}
