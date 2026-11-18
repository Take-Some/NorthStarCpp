// A real legacy YMT skeleton must interoperate with the 2026 palette utility.
// This verifies native source integration without loading GPU/character materials.
#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/character.hpp>
#include <northstarcpp/math.hpp>
#include <algorithm>
#include <cmath>
#include <northstarcpp/skeletal_animation.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/material.hpp>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        northstarcpp::AssetManager store;
        store.mount_directory("",argv[1]);
        auto rig=northstarcpp::load_skeleton(store,"characters/abby/models/abby.ymt");
        if(rig.mirrored_helpers.size()<40)
            throw std::runtime_error("Auxiliary skeleton mirror mapping missing");
        if(rig.joint_name_order.size()!=rig.joints.size())
            throw std::runtime_error("Imported skeleton joint lookup missing");
        for(std::size_t i=0;i<rig.joints.size();++i)
            if(rig.joint_index(rig.joints[i].name)!=i)
                throw std::runtime_error("Imported joint lookup mismatch");
        if(rig.joint_index("__not_a_joint__")!=rig.joints.size())
            throw std::runtime_error("Missing joint lookup returned a bone");
        northstarcpp::SkeletonPose pose(rig);
        if(rig.joints.empty() || rig.inverse_bind.size()!=rig.joints.size() ||
           pose.palette().size()!=rig.joints.size())
            throw std::runtime_error("Native YMT bind pose or joint table invalid");
        // The data format shared with the GPU upload contract is row-major
        // native NorthStar float4x4. Unsupported larger rigs are explicit.
        if(pose.palette().size()<=256) {
            const auto bytes=northstarcpp::pack_skinning_palette(pose.palette());
            if(bytes.size()!=rig.joints.size()*16)
                throw std::runtime_error("Native YMT palette GPU packing mismatch");
        }
        auto model=northstarcpp::load_character_model(store,"characters/abby/models/abby.ydd",rig);
        // Source YDD keeps its imported materials/character_abby reference.
        // Resolve that metadata to the namespaced material without changing
        // the original binary file and verify the GPU PBR loader can bind it.
        for(auto const& part:model.parts) {
            if(part.mesh.material.rfind("characters/abby/materials/abby.ymat@",0)!=0)
                throw std::runtime_error("Character material namespace was not rebased");
        }
        auto gpu_material=northstarcpp::load_material_definition(
            store,model.parts.front().mesh.material);
        if(gpu_material.textures[0].rgba8.empty())
            throw std::runtime_error("Character namespaced YMAT/YTD did not resolve");
        // Ensure imported YMT bind pivots align with real YDD skin clusters.
        // This catches quaternion-handedness regressions that otherwise
        // produce bloated forearms, wide limbs and disconnected attachments.
        auto const conversion=northstarcpp::math::invert4x3(model.mesh_to_skeleton);
        for(unsigned bone:{24u,25u,38u,39u,40u,41u}) {
            northstarcpp::math::float4x4 bind;
            if(!bind.try_invert(rig.inverse_bind[bone]))
                throw std::runtime_error("Invalid bind matrix");
            auto pivot=conversion.transform_position(bind.transform_position({0,0,0}));
            double x=0,y=0,z=0,total=0;
            for(auto const& part:model.parts)
                for(std::size_t v=0;v<part.mesh.vertices.size();++v) {
                    float weight=0;
                    for(unsigned j=0;j<8;++j)
                        if(part.skin[v].joints[j]==bone)
                            weight+=part.skin[v].weights[j];
                    if(weight<0.25f)continue;
                    auto const& position=part.mesh.vertices[v].position;
                    x+=weight*position[0];
                    y+=weight*position[1];
                    z+=weight*position[2];
                    total+=weight;
                }
            if(total<10.0)throw std::runtime_error("Missing weighted joint geometry");
            auto const dx=pivot.x-x/total;
            auto const dy=pivot.y-y/total;
            auto const dz=pivot.z-z/total;
            auto const distance=std::sqrt(dx*dx+dy*dy+dz*dz);
            if(distance>0.13)
                throw std::runtime_error(
                    "YMT/YDD joint pivots do not coincide; quaternion convention invalid");
            std::cerr<<"BIND_ALIGNMENT "<<bone<<" "<<rig.joints[bone].name
                <<" distance "<<distance<<" m\\n";
        }
        auto appearance=northstarcpp::load_character_appearance(
            store,model,"characters/abby/materials/abby.ymat","characters/abby/textures/abby.ytd");
        if(appearance.size()!=model.parts.size())
            throw std::runtime_error("Abby material/mesh part mismatch");
        unsigned bc3=0;
        for(auto const& surface:appearance) {
            if(surface.texture.format_id==103)++bc3;
        }
        if(bc3==0)throw std::runtime_error("Abby BC3 albedo misidentified as BC7");
        std::cerr<<"ABBY_ALBEDO corrected_BC3 "<<bc3<<" parts "<<appearance.size()<<"\\n";
        auto clips=northstarcpp::load_skeletal_clips(store,
            "characters/abby/animations/mm-explore.ycd",
            {"abby-mm-explore-idle","abby-mm-explore-walk-loop-fw"},rig);
        std::cerr<<"ABBY CHARACTER model parts "<<model.parts.size()
                 <<" clips "<<clips.size()<<" joints "<<rig.joints.size()<<"\\n";
        auto const basis=model.mesh_to_skeleton;
        std::cerr<<"MODEL_BASIS";
        for(int row=0;row<4;++row)for(int col=0;col<4;++col)
            std::cerr<<" "<<basis.elements[row][col];
        std::cerr<<"\\n";
        for(auto const& clip:clips) {
            northstarcpp::SkeletonPose sampled(rig);
            sampled.sample(rig,clip,0.2f,true);
            if(clip.name.find("idle")!=std::string::npos) {
                auto joint_position=[&](std::size_t index) {
                    northstarcpp::math::float4x4 bind;
                    if(!bind.try_invert(rig.inverse_bind[index]))
                        throw std::runtime_error("Invalid animated joint bind");
                    auto const animated=bind*sampled.palette()[index];
                    return conversion.transform_position(
                        animated.transform_position({0,0,0}));
                };
                auto const left_wrist=joint_position(24);
                auto const right_wrist=joint_position(25);
                auto const left_elbow=joint_position(22);
                auto const right_elbow=joint_position(23);
                // Natural symmetric rest: hands by the hips, elbows below
                // the shoulders. The old implementation failed these bounds.
                if(left_wrist.x>-.1f || right_wrist.x<.1f ||
                   std::abs(left_wrist.x)>0.5f || std::abs(right_wrist.x)>0.5f ||
                   std::abs(left_wrist.x+right_wrist.x)>0.2f ||
                   left_elbow.y>1.28f || right_elbow.y>1.28f)
                    throw std::runtime_error("Abby idle posture diverged from natural proportions");
                std::cerr<<"IDLE_POSTURE wrists "<<left_wrist.x<<","<<right_wrist.x
                         <<" elbows_height "<<left_elbow.y<<","<<right_elbow.y<<"\\n";
            }
            float original_max=0,skinned_max=0,rest_max=0,max_matrix=0;
            unsigned bad=0;
            for(auto const& matrix:sampled.palette())
              for(int row=0;row<4;++row)for(int col=0;col<4;++col)
                max_matrix=std::max(max_matrix,std::abs(matrix.elements[row][col]));
            for(auto const& part:model.parts) {
                for(std::size_t i=0;i<part.mesh.vertices.size();++i) {
                    auto const& v=part.mesh.vertices[i];
                    auto const& w=part.skin[i];
                    northstarcpp::math::float3 pt{v.position[0],v.position[1],v.position[2]};
                    original_max=std::max(original_max,std::max({std::abs(pt.x),std::abs(pt.y),std::abs(pt.z)}));
                    northstarcpp::math::float3 dest{0,0,0},rest{0,0,0};
                    for(int k=0;k<8;++k)if(w.weights[k]>0.f){
                        dest+=sampled.palette()[w.joints[k]].transform_position(pt)*w.weights[k];
                        rest+=pose.palette()[w.joints[k]].transform_position(pt)*w.weights[k];
                    }
                    skinned_max=std::max(skinned_max,std::max({std::abs(dest.x),std::abs(dest.y),std::abs(dest.z)}));
                    rest_max=std::max(rest_max,std::max({std::abs(rest.x),std::abs(rest.y),std::abs(rest.z)}));
                    if(!std::isfinite(dest.x)||!std::isfinite(dest.y)||!std::isfinite(dest.z))++bad;
                }
            }
            float max_edge_rest=0,max_edge_animated=0;unsigned long long torn=0,edges=0;
            for(auto const& part:model.parts) {
                unsigned long long part_torn=0;
                std::vector<northstarcpp::math::float3> deformed;
                deformed.reserve(part.mesh.vertices.size());
                for(std::size_t i=0;i<part.mesh.vertices.size();++i) {
                    auto const& v=part.mesh.vertices[i]; auto const& w=part.skin[i];
                    northstarcpp::math::float3 pt{v.position[0],v.position[1],v.position[2]};
                    northstarcpp::math::float3 position{0,0,0};
                    for(int j=0;j<8;++j)if(w.weights[j]>0.f)
                      position+=sampled.palette()[w.joints[j]].transform_position(pt)*w.weights[j];
                    deformed.push_back(position);
                }
                auto distance=[](northstarcpp::math::float3 a,northstarcpp::math::float3 b){
                    auto d=a-b;return std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
                };
                for(std::size_t k=0;k<part.mesh.indices.size();k+=3) {
                    for(int e=0;e<3;++e) {
                        auto a=part.mesh.indices[k+e],b=part.mesh.indices[k+(e+1)%3];
                        auto const& pa=part.mesh.vertices[a].position;
                        auto const& pb=part.mesh.vertices[b].position;
                        float rest=distance({pa[0],pa[1],pa[2]},{pb[0],pb[1],pb[2]});
                        float animated=distance(deformed[a],deformed[b]);
                        max_edge_rest=std::max(max_edge_rest,rest);
                        max_edge_animated=std::max(max_edge_animated,animated);
                        if(animated>rest*4.f+0.05f) {
                            ++torn;
                            ++part_torn;
                            if(part_torn<=3) {
                                std::cerr<<"TEAR "<<clip.name<<" part "<<part.mesh.name
                                     <<" vertices "<<a<<" "<<b
                                     <<" rest "<<rest<<" animated "<<animated
                                     <<" weights ";
                                for(auto index:{a,b}) {
                                    auto const& w=part.skin[index];
                                    std::cerr<<" [";
                                    for(int j=0;j<8;++j)if(w.weights[j]>0.001f)
                                        std::cerr<<w.joints[j]<<":"<<w.weights[j]<<",";
                                    std::cerr<<"]";
                                }
                                std::cerr<<"\\n";
                            }
                        }
                        ++edges;
                    }
                }
                if(part_torn)std::cerr<<"PART_TORN "<<clip.name<<" "<<part.mesh.name<<" "<<part_torn<<"\\n";
            }
            // Scan the ENTIRE animation. A single 0.2-second snapshot can
            // miss a discontinuity near a wrap or a late-frame elbow motion.
            std::uint64_t worst_time_tears=0;
            for(float time:{0.f,0.2f,0.5f,1.f,2.f,3.f,5.f,8.f}) {
                northstarcpp::SkeletonPose sampled_at(rig);
                sampled_at.sample(rig,clip,time,true);
                std::uint64_t bad_edges=0;
                for(auto const& part:model.parts) {
                    std::vector<northstarcpp::math::float3> coordinates;
                    coordinates.reserve(part.mesh.vertices.size());
                    for(std::size_t vi=0;vi<part.mesh.vertices.size();++vi) {
                        auto const& v=part.mesh.vertices[vi];
                        auto const& w=part.skin[vi];
                        northstarcpp::math::float3 pos{v.position[0],v.position[1],v.position[2]};
                        northstarcpp::math::float3 output{0,0,0};
                        for(int k=0;k<8;++k)if(w.weights[k]>0.f)
                            output+=sampled_at.palette()[w.joints[k]].transform_position(pos)*w.weights[k];
                        coordinates.push_back(output);
                    }
                    auto length=[](northstarcpp::math::float3 x) {
                        return std::sqrt(x.x*x.x+x.y*x.y+x.z*x.z);
                    };
                    for(std::size_t edge=0;edge<part.mesh.indices.size();edge+=3)
                        for(int e=0;e<3;++e) {
                            auto a=part.mesh.indices[edge+e],b=part.mesh.indices[edge+(e+1)%3];
                            auto const& va=part.mesh.vertices[a].position;
                            auto const& vb=part.mesh.vertices[b].position;
                            auto rest=northstarcpp::math::float3{
                                va[0]-vb[0],va[1]-vb[1],va[2]-vb[2]};
                            if(length(coordinates[a]-coordinates[b])>length(rest)*4.f+0.05f)
                                ++bad_edges;
                        }
                }
                worst_time_tears=std::max(worst_time_tears,bad_edges);
                std::cerr<<"FRAME_TEAR_SCAN "<<clip.name<<" t "<<time
                         <<" torn "<<bad_edges<<"\\n";
            }
            // Compare the exact Player space-conversion matrices to the
            // direct CPU reference. The prior acceptance tests omitted the
            // YDD mesh_to_skeleton conjugation!
            {
                std::vector<northstarcpp::math::float4x4> rendered;
                auto const back=northstarcpp::math::invert4x3(basis);
                rendered.reserve(sampled.palette().size());
                for(auto const& bone:sampled.palette())
                    rendered.push_back(basis * bone * back);
                std::uint64_t rendered_torn=0;
                for(auto const& part:model.parts) {
                    std::vector<northstarcpp::math::float3> vertices;
                    vertices.reserve(part.mesh.vertices.size());
                    for(std::size_t vi=0;vi<part.mesh.vertices.size();++vi) {
                        auto const& v=part.mesh.vertices[vi];
                        auto const& w=part.skin[vi];
                        northstarcpp::math::float3 point{v.position[0],v.position[1],v.position[2]};
                        northstarcpp::math::float3 dst{0,0,0};
                        for(unsigned k=0;k<8;++k)if(w.weights[k]>0.f)
                            dst+=rendered[w.joints[k]].transform_position(point)*w.weights[k];
                        vertices.push_back(dst);
                    }
                    auto length=[](northstarcpp::math::float3 p) {
                        return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
                    };
                    for(std::size_t i=0;i<part.mesh.indices.size();i+=3)
                        for(unsigned k=0;k<3;++k) {
                            auto a=part.mesh.indices[i+k],b=part.mesh.indices[i+(k+1)%3];
                            auto const& va=part.mesh.vertices[a].position;
                            auto const& vb=part.mesh.vertices[b].position;
                            auto rest=northstarcpp::math::float3{va[0]-vb[0],va[1]-vb[1],va[2]-vb[2]};
                            if(length(vertices[a]-vertices[b])>length(rest)*4.f+0.05f)
                                ++rendered_torn;
                        }
                }
                if(rendered_torn)
                    throw std::runtime_error("Viewport YDD basis produced torn character mesh");
                std::cerr<<"ACTUAL_VIEWPORT_SKINNING "<<clip.name
                    <<" direct "<<torn<<" rendered "<<rendered_torn<<"\\n";
            }
            // Handedness, joint binding, helper constraints and native
            // weighted skinning must not produce any severe edge stretch at
            // the selected idle/walk timestamps.
            if(worst_time_tears!=0 || torn!=0)
                throw std::runtime_error("Multi-frame skeletal tearing regression");
            std::cerr<<"TRIANGLE_EDGES "<<clip.name<<" original_max "<<max_edge_rest
                     <<" anim_max "<<max_edge_animated<<" torn "<<torn<<"/"<<edges<<"\\n";
            std::cerr<<"SKIN_BOUNDS "<<clip.name<<" bind "<<rest_max<<" raw "<<original_max
                     <<" animated "<<skinned_max<<" matrix "<<max_matrix<<" bad "<<bad<<"\\n";
        }
        std::cout<<"YMT skeleton -> shared animation palette: "
                 <<rig.joints.size()<<" joints PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"YMT skeletal interoperability FAILED: "<<e.what()<<'\n';
        return 1;
    }
}
