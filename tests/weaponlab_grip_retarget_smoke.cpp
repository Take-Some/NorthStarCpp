#include "../engine/northstarcpp/modules/viewport/viewport_weapon_grip_profiles.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/skeleton.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

#define REQUIRE(test) do {if(!(test))throw std::runtime_error(#test);}while(false)
using namespace northstarcpp;
using namespace northstarcpp::viewport_detail;

static math::float3 relative_tip(SkeletonPose const& pose,
                                 Skeleton const& rig,char const* wrist,
                                 char const* tip) {
    auto const w=rig.joint_index(wrist),t=rig.joint_index(tip);
    REQUIRE(w<rig.joints.size() && t<rig.joints.size());
    math::float4x4 inverse;
    REQUIRE(inverse.try_invert(pose.joints_world()[w]));
    return (pose.joints_world()[t]*inverse).transform_position({0,0,0});
}

int main(int argc,char** argv) {
    REQUIRE(argc>1);
    AssetStore store;
    store.mount_shared_assets(std::filesystem::path(argv[1]));
    auto const source=store.read(
        "characters/abby/animations/weapon_grips.xml");
    auto const profiles=parse_weapon_grips_xml({
        reinterpret_cast<char const*>(source.data()),source.size()});
    auto const* pistol=profiles.find("pistol");
    auto const* rifle=profiles.find("rifle");
    REQUIRE(pistol && rifle);
    REQUIRE(pistol->bends.size()==30 && rifle->bends.size()==30);
    REQUIRE(pistol->has_support_grip && rifle->has_support_grip);
    REQUIRE(pistol->support_grip.x<pistol->primary_grip.x-.04f);
    REQUIRE((pistol->support_grip-pistol->primary_grip).magnitude()<.11f);
    REQUIRE((pistol->front_sight-pistol->rear_sight).z>.10f);
    REQUIRE(std::abs(pistol->front_sight.y-pistol->rear_sight.y)<.005f);
    REQUIRE(pistol->support_offset.x>.02f);
    REQUIRE(rifle->support_offset.z>.29f);
    REQUIRE(rifle->support_offset.z>pistol->support_offset.z+.20f);
    auto native=load_native_model_resource(store,
        "characters/abby/models/abby.ydd");
    REQUIRE(native.skeleton.has_value());
    auto const& rig=*native.skeleton;
    REQUIRE(rig.humanoid.valid);
    REQUIRE(rig.humanoid.hand_basis_valid[0] &&
            rig.humanoid.hand_basis_valid[1]);
    REQUIRE(rig.humanoid.r_hand!=rig.humanoid.l_hand);
    // Corrupted chain cannot silently acquire a valid humanoid mapping.
    Skeleton broken=rig;
    auto const elbow=broken.joint_index("l_elbow");
    REQUIRE(elbow<broken.joints.size());
    broken.joints[elbow].parent=-1;
    REQUIRE(!detect_canonical_humanoid_skeleton(broken).valid);
    // Simulate a source rig whose arm names are mirrored while the bind
    // geometry and parent-child chains remain intact. A confident detector
    // must repair exactly the right/left reference pairs.
    Skeleton mirrored=rig;
    for(auto const& suffix:{"shoulder","elbow","wrist",
                             "hand_prop_attachment"}) {
        auto const right=mirrored.joint_index(std::string("r_")+suffix);
        auto const left=mirrored.joint_index(std::string("l_")+suffix);
        if(right<mirrored.joints.size() && left<mirrored.joints.size())
            std::swap(mirrored.joints[right].name,mirrored.joints[left].name);
    }
    mirrored.joint_name_order.clear();
    auto const repaired=detect_canonical_humanoid_skeleton(mirrored);
    REQUIRE(repaired.valid);
    if(rig.humanoid.side_confirmed) {
        REQUIRE(repaired.side_confirmed);
        REQUIRE(repaired.side_repaired!=rig.humanoid.side_repaired);
    }
    auto clips=load_skeletal_clips(store,
        "characters/abby/animations/mm-explore.ycd",
        {"abby-mm-explore-idle"},rig);
    REQUIRE(!clips.empty());
    SkeletonPose baseline(rig);
    baseline.sample(rig,clips.front(),0.f,false);

    for(auto const* profile:{pistol,rifle}) {
        SkeletonPose gripped(rig);
        gripped.sample(rig,clips.front(),0.f,false);
        REQUIRE(gripped.apply_finger_bends(rig,profile->bends));
        std::size_t visibly_bent=0;
        for(auto const& [wrist,tip]:
            std::array<std::pair<char const*,char const*>,8>{{
                {"r_wrist","r_indexc"},{"l_wrist","l_indexc"},
                {"r_wrist","r_middlec"},{"l_wrist","l_middlec"},
                {"r_wrist","r_ringc"},{"l_wrist","l_ringc"},
                {"r_wrist","r_pinkyc"},{"l_wrist","l_pinkyc"}}}) {
            auto delta=(relative_tip(gripped,rig,wrist,tip)-
                        relative_tip(baseline,rig,wrist,tip)).magnitude();
            if(delta>.008f)++visibly_bent;
            std::printf("%s %s bend_tip_delta=%.4f\n",
                        profile==pistol?"pistol":"rifle",tip,delta);
        }
        REQUIRE(visibly_bent>=6);
        std::size_t aligned=0;
        for(auto const& [helper,main]:rig.mirrored_helpers) {
            if(helper>=rig.joints.size() || main>=rig.joints.size())continue;
            auto const name=rig.joints[main].name;
            if(name.find("index")==std::string::npos &&
               name.find("middle")==std::string::npos &&
               name.find("thumb")==std::string::npos)continue;
            float error=0.f;
            for(unsigned row=0;row<4;++row)
                for(unsigned col=0;col<4;++col)
                    error=std::max(error,std::abs(
                        gripped.palette()[main].elements[row][col]-
                        gripped.palette()[helper].elements[row][col]));
            REQUIRE(error<.0001f);
            ++aligned;
        }
        REQUIRE(aligned>=10);
        auto before=skin_native_model_cpu(native,baseline.palette());
        auto after=skin_native_model_cpu(native,gripped.palette());
        REQUIRE(before.vertices.size()==after.vertices.size());
        REQUIRE(!before.vertices.empty());
        float max_shift=0.f;
        for(std::size_t i=0;i<before.vertices.size();++i) {
            auto const d=(after.vertices[i].position-before.vertices[i].position);
            REQUIRE(d.valid());
            max_shift=std::max(max_shift,d.magnitude());
        }
        // A finger pose cannot move a single skin vertex across a torso.
        REQUIRE(max_shift>.005f && max_shift<.40f);
        std::size_t new_tears=0;
        for(std::size_t k=0;k+2<native.mesh.indices.size();k+=3) {
            for(unsigned j=0;j<3;++j) {
                auto a=native.mesh.indices[k+j];
                auto c=native.mesh.indices[k+(j+1)%3];
                REQUIRE(a<after.vertices.size() && c<after.vertices.size());
                auto const rest=(native.mesh.vertices[a].position-
                                 native.mesh.vertices[c].position).magnitude();
                auto const old=(before.vertices[a].position-
                                before.vertices[c].position).magnitude();
                auto const now=(after.vertices[a].position-
                                after.vertices[c].position).magnitude();
                const auto limit=std::max(.025f,3.f*rest);
                if(old<=limit && now>limit)++new_tears;
            }
        }
        std::printf("%s max_skin_vertex_shift=%.4f new_torn_edges=%zu\n",
                    profile==pistol?"pistol":"rifle",max_shift,new_tears);
        REQUIRE(new_tears==0);
    }
}
