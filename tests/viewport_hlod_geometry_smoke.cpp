#include "viewport_hlod_geometry.hpp"
#include <stdexcept>
#include <unordered_map>
using namespace northstarcpp;
using namespace northstarcpp::viewport_detail;
void check(bool v,char const* what){if(!v)throw std::runtime_error(what);}
int main() {
    MeshAsset quad;
    quad.vertices={
        {{0,0,0},{0,1,0},0,0},{{1,0,0},{0,1,0},1,0},
        {{0,0,1},{0,1,0},0,1},{{1,0,1},{0,1,0},1,1}};
    quad.indices={0,1,2,2,1,3};
    quad.sections={{"wall","wall.ymat",0,6,true}};
    std::unordered_map<std::string,MeshAsset> decoded{{"wall.obj",quad}};
    std::unordered_map<std::string,MeshAsset> cache;
    Scene s;s.version=3;
    for(int i=0;i<3;++i) {
        SceneObject obj;obj.node.id=i+1;
        obj.node.asset="wall.obj";obj.position.x=float(i*5);
        s.objects.push_back(obj);
    }
    SceneHlodProxy proxy;proxy.members={0,1,2};proxy.member_count=3;
    auto material=[](std::uint64_t id)->std::string {
        return id==2?"override.ymat":"";
    };
    auto dynamic=[](std::string const&,std::uint64_t){return false;};
    auto const* first=resolve_hlod_mesh(s,proxy,decoded,cache,material,dynamic,42);
    check(first && first->indices.size()==18 && first->sections.size()==2,
          "HLOD viewport failed to produce real material-aware geometry");
    check(cache.size()==1,"HLOD viewport did not cache result");
    auto const* repeated=resolve_hlod_mesh(s,proxy,decoded,cache,material,dynamic,42);
    check(first==repeated,"camera-only frames rebuilt cached HLOD mesh");
    check(first->maximum.x==11.f && first->minimum.x==0.f,
          "incorrect world-space baked source transforms");
    s.objects[2].position.x=30.f;
    auto const* moved=resolve_hlod_mesh(s,proxy,decoded,cache,material,dynamic,42);
    check(moved && moved!=first && cache.size()==2 &&
          moved->maximum.x==31.f,
          "HLOD cache failed to invalidate changed transforms");
    std::unordered_map<std::string,MeshAsset> missing;
    check(resolve_hlod_mesh(s,proxy,missing,cache,material,dynamic,42)==nullptr,
          "missing mesh must retain original AABB fallback");
    auto skip=[](std::string const&,std::uint64_t id){return id==2;};
    check(resolve_hlod_mesh(s,proxy,decoded,cache,material,skip,42)==nullptr,
          "animated mesh was baked as static geometry");
    return 0;
}
