#include <northstarcpp/scene_hlod_baker.hpp>
#include <cmath>
#include <stdexcept>
using namespace northstarcpp;
using namespace northstarcpp::scene_hlod;
void require(bool x,char const* m){if(!x)throw std::runtime_error(m);}
int main(){
    MeshAsset quad;
    quad.vertices={
        {{0,0,0},{0,1,0},0,0}, {{1,0,0},{0,1,0},1,0},
        {{0,0,1},{0,1,0},0,1}, {{1,0,1},{0,1,0},1,1}};
    quad.indices={0,1,2, 2,1,3};
    quad.sections={{"Ground","materials/pavement.ymat",0,6,true}};
    Source a; a.mesh=&quad;
    Source b=a; b.transform[3]=10.f; b.transform[0]=2.f;
    auto baked=bake({a,b});
    require(baked.sources==2 && baked.source_triangles==4 &&
            baked.mesh.indices.size()==12 && baked.mesh.vertices.size()==8,
            "HLOD geometry did not merge/compact index buffers");
    require(baked.mesh.sections.size()==1 &&
            baked.mesh.sections[0].material_reference=="materials/pavement.ymat" &&
            baked.mesh.sections[0].index_count==12,
            "HLOD lost material ranges");
    require(baked.mesh.minimum.x==0.f && baked.mesh.maximum.x==12.f &&
            baked.mesh.maximum.z==1.f,"HLOD bounds/transform wrong");
    bool uv_found=false;
    for(auto const& vertex:baked.mesh.vertices)
        if(vertex.position.x==12.f && vertex.u==1.f && vertex.v==1.f &&
           std::abs(vertex.normal.y-1.f)<1.e-5f) uv_found=true;
    require(uv_found,"HLOD UV or normal transform lost");

    Source c=a; c.material_override="materials/concrete.ymat";
    auto distinct=bake({a,c});
    require(distinct.mesh.sections.size()==2 &&
            distinct.mesh.indices.size()==12,"different materials were merged");

    Source flipped=a; flipped.transform[0]=-1.f;
    auto reversed=bake({flipped});
    require(reversed.mesh.indices.size()==6 &&
            reversed.mesh.minimum.x==-1.f,"mirrored world transform broken");

    bool threw=false;
    try{ Source invalid=a;invalid.is_static=false;(void)bake({invalid}); }
    catch(std::invalid_argument const&){threw=true;}
    require(threw,"dynamic actor baked into HLOD");
    threw=false;
    try{ (void)bake({a},3); }
    catch(std::length_error const&){threw=true;}
    require(threw,"HLOD triangle count budget was ignored");
    return 0;
}
