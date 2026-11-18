// Read-only character acceptance: actual native Ellie/Isaac rigs, YDD skins
// and project-owned YCD frames. Rejects bind pose gliding and bad deformations.
#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/character.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/assets.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include <string>
using namespace northstarcpp;

struct SkinStats{
    double rms=0,peak=0,move_percent=0,max_horizontal=0;
    std::size_t vertices=0,over_1m=0;
};
SkinStats measure(CharacterModel const& model,Skeleton const& skeleton,
                  std::vector<math::float4x4> const& basis,
                  std::vector<math::float4x4> const& animated,
                  bool diagnostics=false) {
    if(basis.size()!=animated.size())
        throw std::runtime_error("Skin palette length differs");
    std::vector<math::float4x4> a,b;
    remap_skin_palette_to_mesh(a,basis,model.mesh_to_skeleton);
    remap_skin_palette_to_mesh(b,animated,model.mesh_to_skeleton);
    SkinStats result{};
    double moved=0,sq=0;
    std::size_t printed=0;
    for(auto const& part:model.parts) {
        if(part.mesh.vertices.size()!=part.skin.size())
            throw std::runtime_error("Vertex count mismatch");
        for(std::size_t i=0;i<part.skin.size();++i) {
            auto const& v=part.mesh.vertices[i];
            auto const& w=part.skin[i];
            auto x=math::float3{0,0,0},y=math::float3{0,0,0};
            for(std::size_t j=0;j<8;++j)if(w.weights[j]>0.f) {
                if(w.joints[j]>=a.size())
                    throw std::runtime_error("Invalid animated skin joint");
                auto const p=math::float3{v.position[0],v.position[1],v.position[2]};
                x+=a[w.joints[j]].transform_position(p)*w.weights[j];
                y+=b[w.joints[j]].transform_position(p)*w.weights[j];
            }
            auto const dist=(y-x).magnitude();
            if(!std::isfinite(dist))
                throw std::runtime_error("Nonfinite animated vertex");
            if(diagnostics && dist>.9 && printed<12) {
                std::cerr<<"OUTLIER dist="<<dist<<" part="
                    <<part.mesh.material<<" original=("<<v.position[0]
                    <<","<<v.position[1]<<","<<v.position[2]
                    <<") rest=("<<x.x<<","<<x.y<<","<<x.z
                    <<") animated=("<<y.x<<","<<y.y<<","<<y.z<<") bones=";
                for(unsigned b=0;b<8;++b)if(w.weights[b]>.03f)
                    std::cerr<<skeleton.joints[w.joints[b]].name
                             <<"="<<w.weights[b]<<" ";
                std::cerr<<"\n";
                ++printed;
            }
            result.peak=std::max(result.peak,static_cast<double>(dist));
            result.max_horizontal=std::max(result.max_horizontal,
                static_cast<double>(std::hypot(y.x-x.x,y.z-x.z)));
            result.over_1m+=dist>1.f;
            moved+=dist>.015f;
            sq+=double(dist)*double(dist);
            result.vertices++;
        }
    }
    result.rms=std::sqrt(sq/result.vertices);
    result.move_percent=100.*moved/result.vertices;
    return result;
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        AssetManager store;
        store.mount_directory("",argv[1]);
        store.mount_directory("",argv[2]);
        for(std::string const slug:{"ellie","isaac"}) {
            auto const mesh_path="characters/"+slug+"/models/"+slug+".ydd";
            auto rig=load_skeleton(store,"characters/"+slug+"/models/"+slug+".ymt");
            auto model=load_character_model(store,mesh_path,rig);
            auto clips=load_skeletal_clips(store,
                "animations/npcs/"+slug+"_locomotion.ycd",
                {"npc-idle","npc-walk"},rig);
            SkeletonPose bind(rig);
            if(model.parts.empty() || clips.size()!=2)
                throw std::runtime_error("Missing native skinned body");
            for(std::size_t k=0;k<clips.size();++k) {
                auto const& clip=clips[k];
                for(float t:{.1f,.45f,.9f,1.4f,2.4f,3.0f}) {
                    SkeletonPose pose(rig);
                    pose.sample(rig,clip,t,false);
                    // Exactly the viewport transition pipeline.
                    auto stats=measure(model,rig,bind.palette(),pose.palette(),
                        slug=="ellie" && k==1 && t==.45f);
                    std::cout<<slug<<" "<<clip.name<<" t="<<t
                        <<" rms="<<std::setprecision(4)<<stats.rms
                        <<" max="<<stats.peak
                        <<" moved_pct="<<stats.move_percent
                        <<" over_1m="<<stats.over_1m
                        <<" vertices="<<stats.vertices<<"\n";
                    // An unverified raw NVRM palette dragged Ellie hair
                    // toward the knee by 1.52 m. Fail that exact regression.
                    if(stats.peak>.90 || stats.over_1m!=0)
                        throw std::runtime_error(
                            "Native NPC skin exceeds anatomical motion envelope");
                }
            }
        }
        std::cout<<"Real NPC native YMT/YDD/YCD skin QA: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"NPC native skin QA FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
