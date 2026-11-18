#include <northstarcpp/procedural_ik.hpp>
#include <northstarcpp/animation_graph.hpp>
#include <northstarcpp/assets.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace northstarcpp;
namespace {
void expect(bool ok,char const* msg){if(!ok)throw std::runtime_error(msg);}
math::float3 translation(math::float4x4 const& m){
    return {m.elements[3][0],m.elements[3][1],m.elements[3][2]};
}
Skeleton synthetic() {
    Skeleton rig;
    auto add=[&](char const* name,int parent,math::float3 location){
        SkeletonJoint j;
        j.name=name;j.parent=parent;j.bind.position=location;
        j.bind.rotation.identity();rig.joints.push_back(j);
        math::float4x4 m;m.identity();rig.inverse_bind.push_back(m);
    };
    add("hip",-1,{0,1,0});
    add("knee",0,{.2f,-.5f,0});
    add("ankle",1,{.2f,-.5f,0});
    return rig;
}
}
int main(int argc,char** argv){
    try {
        auto rig=synthetic();
        SkeletonPose pose(rig);
        ProceduralIkController solver(rig);
        expect(solver.register_chain({"leg","hip","knee","ankle",1.5f,1.8f,.05f,.1f}),
            "Failed registering a valid authored chain");
        expect(!solver.register_chain({"leg","hip","knee","ankle"}),
            "Duplicate chain was accepted");
        expect(!solver.register_chain({"broken","ankle","hip","knee"}),
            "Invalid ancestry was accepted");
        IkTargetRequest request;
        request.position={.42f,.03f,.08f};
        request.pole={.3f,.65f,.18f};request.priority=5;
        request.weight=1;
        expect(solver.submit("leg",request),"Valid IK goal rejected");
        auto competing=request;
        competing.priority=1;
        competing.position={100,100,100};
        expect(solver.submit("leg",competing),"Lower-priority goal improperly rejected");
        solver.evaluate(pose,.1f,1);
        expect(solver.reports().size()==1 &&solver.reports()[0].attempted &&
               solver.reports()[0].effective_weight>.7f,
               "IK arbitration/smoothing did not activate chain");
        auto const ankle=translation(pose.joints_world()[2]);
        expect((ankle-request.position).magnitude()<.30f,
            "Lower-priority goal superseded the selected IK target");
        expect(std::isfinite(solver.reports()[0].residual_meters),
            "IK report contains invalid residual");
        auto invalid=request;
        invalid.position.x=std::numeric_limits<float>::quiet_NaN();
        expect(!solver.submit("leg",invalid),"Nonfinite IK target accepted");
        expect(!solver.submit("unknown",request),"Unknown IK chain accepted");
        auto rejected_ground=IkGroundContact{};
        rejected_ground.hit=true;rejected_ground.normal={0,.1f,1};
        expect(!solver.submit_ground("leg",rejected_ground,request,pose),
            "Steep invalid ground normal was accepted");
        IkGroundContact contact;
        contact.hit=true;contact.point={ankle.x,ankle.y-.05f,ankle.z};
        contact.normal={0,1,0};
        contact.max_drop=.1f;contact.max_raise=.1f;
        expect(solver.submit_ground("leg",contact,request,pose),
            "Valid provider ground contact was rejected");
        solver.evaluate(pose,.12f,1);
        expect(solver.reports()[0].attempted &&
               solver.reports()[0].residual_meters<.3f,
               "Foot-plant IK failed for valid ground hit");
        solver.evaluate(pose,.25f,0);
        expect(!solver.reports()[0].attempted,
            "IK LOD solver budget was ignored");
        solver.reset();
        solver.evaluate(pose,.05f);
        expect(!solver.reports()[0].attempted,
            "Reset did not release IK activation");
        if(argc==2) {
            AssetStore store;store.mount_shared_assets(argv[1]);
            auto abby=load_skeleton(store,"characters/abby/models/abby.ymt");
            SkeletonPose character(abby);
            auto clips=load_skeletal_clips(store,
                "characters/abby/animations/mm-explore.ycd",
                {"abby-mm-explore-idle"},abby);
            character.sample(abby,clips[0],.2f,false);
            ProceduralIkController real(abby);
            expect(real.register_chain({"right_hand","r_shoulder","r_elbow","r_wrist"}),
                "Imported Abby arm bones did not form a valid chain");
            auto const wrist=abby.joint_index("r_wrist");
            auto const elbow=abby.joint_index("r_elbow");
            auto const hand=translation(character.joints_world()[wrist]);
            auto const elbow_pos=translation(character.joints_world()[elbow]);
            IkTargetRequest grab;
            grab.position=hand+math::float3{.015f,.015f,.02f};
            grab.pole=elbow_pos;
            expect(real.submit("right_hand",grab),"Abby IK request rejected");
            real.evaluate(character,.12f,1);
            expect(real.reports()[0].attempted &&
                character.palette().size()==abby.joints.size() &&
                std::isfinite(character.palette()[wrist].elements[3][0]),
                "IK destroyed imported Abby bone palette");
        }
        std::cout<<"Procedural IK requests, priorities, LOD and ground contact PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"Procedural IK FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
