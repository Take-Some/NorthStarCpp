#include <northstarcpp/grounded_locomotion.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/posture_ik.hpp>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <memory>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace northstarcpp;
namespace {
void ensure(bool ok,char const* message) {
    if(!ok)throw std::runtime_error(message);
}
Skeleton make_rig() {
    Skeleton rig;
    auto add=[&](char const* name,int parent,math::float3 position) {
        SkeletonJoint joint;
        joint.name=name;joint.parent=parent;
        joint.bind.position=position;joint.bind.rotation.identity();
        rig.joints.push_back(std::move(joint));
        math::float4x4 identity;identity.identity();
        rig.inverse_bind.push_back(identity);
    };
    add("pelvis",-1,{0,1,0});
    add("torso",0,{0,.3f,0});
    add("l_thigh",0,{-.17f,-.18f,0});
    add("l_knee",2,{0,-.41f,.03f});
    add("l_ankle",3,{0,-.40f,.02f});
    add("r_thigh",0,{.17f,-.18f,0});
    add("r_knee",5,{0,-.41f,.03f});
    add("r_ankle",6,{0,-.40f,.02f});
    add("l_toe",4,{0,-.04f,.14f});
    add("r_toe",7,{0,-.04f,.14f});
    return rig;
}
PhysicsGroundProbe hit(float y,math::float3 normal={0,1,0}) {
    PhysicsGroundProbe result;result.hit=true;
    result.point={0,y,0};result.normal=normal;
    result.fraction=.5f;return result;
}
}
int main() {
    try {
        auto rig=make_rig();
        SkeletonPose pose(rig);
        // Root displacement is presentation-only when the physical Pawn
        // already owns character translation.
        SkeletonPose root_pose(rig);
        std::vector<JointTransform> root_locals;
        for(auto const& j:rig.joints)root_locals.push_back(j.bind);
        root_locals[0].position.x+=.4f;
        root_pose.set_local_transforms(rig,root_locals,
            std::vector<std::uint8_t>(rig.joints.size(),0));
        auto const removed=root_pose.consume_visual_root_motion(rig,"pelvis");
        ensure(removed &&std::abs(removed->x-.4f)<.0001f &&
               std::abs(root_pose.joints_world()[0].elements[3][0])<.0001f,
               "Physics-authoritative root motion failed to keep rig in place");
        ensure(!root_pose.consume_visual_root_motion(rig,"l_ankle"),
               "Non-root bone was accepted for capsule motion policy");
        ProceduralIkController limbs(rig);
        ensure(limbs.register_chain({"left","l_thigh","l_knee","l_ankle"}),
            "Left leg definition invalid");
        ensure(limbs.register_chain({"right","r_thigh","r_knee","r_ankle"}),
            "Right leg definition invalid");
        GroundedFootDefinition left;
        left.chain="left";left.ankle="l_ankle";left.toe_bone="l_toe";
        GroundedFootDefinition right;
        right.chain="right";right.ankle="r_ankle";right.toe_bone="r_toe";
        GroundedLocomotionController ground(rig,{left,right},
            {"pelvis","torso",.12f,.10f});
        math::float4x4 world;world.identity();
        ensure(ground.foot_count()==2,"Foot registration is incorrect");
        auto contact=GroundedFootContact{};
        contact.heel=hit(-.15f,{.2f,.98f,0});
        contact.toe=hit(-.14f,{.2f,.98f,0});
        ensure(ground.provide_contact("left",contact),
            "Ground contact input was rejected");
        ensure(ground.provide_contact("right",contact),
            "Ground contact input was rejected");
        ground.update(nullptr,std::size_t(-1),pose,limbs,world,.1f);
        ensure(ground.reports().size()==2,"Ground report count incorrect");
        ensure(ground.pelvis_offset()<0.f &&
               ground.pelvis_offset()>=-.121f,"Pelvis offset was not bounded");
        ensure(ground.reports()[0].heel_hit&&
               ground.reports()[0].toe_hit,
               "Independent heel/toe probes were not combined");
        limbs.evaluate(pose,.1f);
        // Frame 2: zero animated foot speed should plant both contacts.
        SkeletonPose standing(rig);
        ground.provide_contact("left",contact);
        ground.provide_contact("right",contact);
        ground.update(nullptr,std::size_t(-1),standing,limbs,world,.1f);
        ensure(ground.reports()[0].planted&&ground.reports()[1].planted,
               "Stance contact failed to lock");
        ensure(ground.reports()[0].target_adjustment<=.20f &&
               ground.reports()[0].target_adjustment>=-.25f,
               "Ground vertical adjustment escaped authored range");
        limbs.evaluate(standing,.1f);
        auto const toe_before=standing.joints_world()[8];
        ground.finish_toes(standing);
        auto const toe_after=standing.joints_world()[8];
        ensure(std::abs(toe_before.elements[2][1]-
                        toe_after.elements[2][1])>.0001f,
               "Ground contact failed to articulate the authored toe joint");
        // A small world transform movement must not unlock the foot.
        math::float4x4 moved; moved.identity();
        moved.elements[3][0]=.01f;
        SkeletonPose translated(rig);
        ground.provide_contact("left",contact);
        ground.provide_contact("right",contact);
        ground.update(nullptr,std::size_t(-1),translated,limbs,moved,.1f);
        ensure(ground.reports()[0].planted,
               "Foot lock did not persist in world coordinates");
        ensure(ground.set_foot_phase("left",false),
               "Graph foot-lift marker could not select the chain");
        SkeletonPose swinging(rig);
        ground.provide_contact("left",contact);
        ground.update(nullptr,std::size_t(-1),swinging,limbs,moved,.08f);
        ensure(!ground.reports()[0].planted,
               "Foot lift marker failed to release planted contact");
        ensure(ground.set_foot_phase("left",true),
               "Graph foot-plant marker could not select the chain");
        ground.provide_contact("left",contact);
        ground.update(nullptr,std::size_t(-1),swinging,limbs,moved,.08f);
        ensure(ground.reports()[0].planted,
               "Animation Graph plant marker failed to set stance");
        // Explicit missed probes must release the contact, not preserve it.
        GroundedFootContact misses;
        misses.heel=PhysicsGroundProbe{};
        misses.toe=PhysicsGroundProbe{};
        ground.provide_contact("left",misses);
        ground.provide_contact("right",misses);
        ground.update(nullptr,std::size_t(-1),translated,limbs,moved,.2f);
        ensure(!ground.reports()[0].planted &&!ground.reports()[1].planted,
               "Missing contact retained stale world-space anchors");
        GroundedLocomotionController stale(rig,{left},
            {"pelvis","torso",.12f,.10f});
        SkeletonPose stale_pose(rig);
        stale.provide_contact("left",contact);
        stale.update(nullptr,std::size_t(-1),stale_pose,limbs,world,.1f);
        stale.update(nullptr,std::size_t(-1),stale_pose,limbs,world,.25f);
        stale.update(nullptr,std::size_t(-1),stale_pose,limbs,world,.2f);
        ensure(!stale.reports()[0].heel_hit &&
               !stale.reports()[0].planted,
               "Unrefreshed ground hits persisted beyond expiry");
        ground.reset();
        ensure(ground.pelvis_offset()==0.f,"Ground IK reset did not clear pelvis");
        bool rejected=false;
        try {
            GroundedLocomotionController invalid(rig,{{"left","missing"}},{});
        } catch(std::invalid_argument const&) { rejected=true; }
        ensure(rejected,"Unknown foot bone was accepted");
        // Actual provider async handoff: the ticket is NOT ready before
        // stepping and becomes consumable only after the physics tick.
        Scene scene;scene.version=3;scene.name="Ground IK Deferred Probe QA";
        PhysicsScene physics(scene);
        auto bad=physics.enqueue_ground_probe({0,0,0},{0,1,0});
        ensure(bad==0,"Upward ray accepted as a ground probe");
        auto const ticket=physics.enqueue_ground_probe(
            {0,.5f,0},{0,-.5f,0});
        ensure(ticket!=0,"Ground probe queue rejected valid ray");
        ensure(!physics.consume_ground_probe(ticket),
               "Deferred probe executed prematurely on render thread");
        physics.step(1.f/60.f);
        auto response=physics.consume_ground_probe(ticket);
        ensure(response.has_value(),"Physics tick did not service queued probe");
        ensure(response->hit &&std::abs(response->point.y)<.08f &&
               response->normal.y>.8f,
               "Ground probe failed to hit the provider ground plane");
        ensure(!physics.consume_ground_probe(ticket),
               "Ground query result was consumed more than once");
        Scene platform_scene;
        platform_scene.version=3;
        platform_scene.name="Ground IK Moving Platform";
        SceneObject support;
        support.node.id=900;
        support.position={0.f,0.f,0.f};
        support.size=1.f;
        support.mass=0.f;
        platform_scene.objects.push_back(support);
        PhysicsScene moving(platform_scene);
        ensure(moving.body_transform(0).has_value(),
               "Live physics body transform was not exposed");
        GroundedLocomotionController platform_ik(rig,{left},
            {"pelvis","torso",.12f,.10f});
        auto platform_contact=contact;
        platform_contact.heel->object_index=0;
        platform_contact.toe->object_index=0;
        SkeletonPose first_step(rig);
        platform_ik.provide_contact("left",platform_contact);
        platform_ik.update(&moving,std::size_t(-1),first_step,
            limbs,world,.1f);
        SkeletonPose second_step(rig);
        platform_ik.provide_contact("left",platform_contact);
        platform_ik.update(&moving,std::size_t(-1),second_step,
            limbs,world,.1f);
        ensure(platform_ik.reports()[0].planted &&
               platform_ik.reports()[0].support_body==0,
               "Foot did not attach planted contact to its support body");
        auto const anchored=platform_ik.reports()[0].anchor_world;
        moving.move_static(0,{.08f,0.f,0.f});
        SkeletonPose moved_step(rig);
        platform_ik.provide_contact("left",platform_contact);
        platform_ik.update(&moving,std::size_t(-1),moved_step,
            limbs,world,.1f);
        ensure(platform_ik.reports()[0].planted &&
               std::abs(platform_ik.reports()[0].anchor_world.x-
                        anchored.x-.08f)<.001f,
               "Planted anchor did not follow the moving collision body");
        moving.move_static(0,{5.f,0.f,0.f});
        SkeletonPose jumped_step(rig);
        platform_ik.provide_contact("left",platform_contact);
        platform_ik.update(&moving,std::size_t(-1),jumped_step,
            limbs,world,.1f);
        ensure(!platform_ik.reports()[0].planted,
               "Support body teleport retained stale foot lock");
        // A real sloped Jolt triangle mesh, not an injected normal:
        // ground probing must observe inclination and actual contact height.
        Scene incline;incline.version=3;
        incline.name="Grounded IK Ramp QA";
        PhysicsScene slope(incline);
        std::vector<Vector3> vertices{
            {-1.f,.10f,2.f}, {-1.f,.50f,4.f},
            { 1.f,.10f,2.f}, { 1.f,.50f,4.f}
        };
        slope.add_static_mesh(vertices,{0,1,2,2,1,3});
        auto ramp_hit=slope.raycast_channel(
            {0.f,1.10f,3.f},{0.f,-.40f,3.f},2u);
        ensure(ramp_hit.has_value() &&
               ramp_hit->normal.y>.80f &&ramp_hit->normal.y<.999f,
               "Jolt ramp raycast did not expose the sloped surface normal");
        auto const ramp_y=1.10f+( -.40f-1.10f )*ramp_hit->fraction;
        ensure(std::abs(ramp_y-.30f)<.06f,
               "Jolt ramp hit height differs from authored slope geometry");

        Scene staircase;staircase.version=3;
        staircase.name="Grounded IK Stairs QA";
        for(int i=0;i<3;++i) {
            SceneObject step;
            step.node.id=std::uint64_t(400+i);
            step.position={0.f,0.f,.7f+float(i)*.75f};
            step.size=.30f+float(i)*.20f;
            step.mass=0.f;
            staircase.objects.push_back(step);
        }
        PhysicsScene stair_world(staircase);
        for(int i=0;i<3;++i) {
            auto const z=.7f+float(i)*.75f;
            auto const height=.3f+float(i)*.2f;
            auto hit=stair_world.raycast_channel(
                {0.f,1.2f,z},{0.f,-.25f,z},2u);
            ensure(hit &&hit->object_index==static_cast<std::uint32_t>(i),
                   "Stair probe selected the wrong physical step");
            auto const foot_y=1.2f+( -.25f-1.2f )*hit->fraction;
            ensure(std::abs(foot_y-height)<.07f,
                   "Grounded IK stair probe returned an invalid step height");
        }
        // Distributed Look-at is opt-in and uses per-bone authored weights.
        SkeletonPose look_pose(rig);
        DistributedLookAt look(rig,{{"torso",1.f,.14f,{0,0,1}}});
        auto const target=math::float3{1.f,1.3f,2.f};
        auto const score=[&]() {
            auto const& m=look_pose.joints_world()[rig.joint_index("torso")];
            auto direction=m.transform_direction({0,0,1}).normalize();
            auto delta=target-m.transform_position({0,0,0});
            delta.normalize();
            return math::dot_product(direction,delta);
        };
        float const before=score();
        ensure(look.set_target(target,1.f),"Look-at target rejected");
        for(int i=0;i<10;++i)look.apply(look_pose,world,.1f);
        ensure(score()>before+.01f,"Look-at did not improve gaze alignment");
        // The seat pose arrives from engine.vehicle; empty seats cannot issue
        // hand/limb targets even if the caller presents authored offsets.
        NorthStarVehicleSeatState seat{};
        seat.size=sizeof(seat);seat.occupant=1;
        seat.world_yaw=.2f;seat.world_position[1]=.5f;
        auto const targets=VehicleSeatIk::submit(rig,look_pose,limbs,world,seat,
            {{"left","l_knee",{.1f,.2f,.3f},1.f,40}});
        ensure(targets==1,"Vehicle seat-relative reach not submitted");
        seat.occupant=0;
        ensure(VehicleSeatIk::submit(rig,look_pose,limbs,world,seat,
            {{"left","l_knee",{.1f,.2f,.3f},1.f,40}})==0,
            "Empty vehicle seat issued a reach target");
        // 128 actor batch, observational CPU p50/p95/p99 only.
        struct Actor {
            SkeletonPose pose;
            ProceduralIkController ik;
            GroundedLocomotionController grounded;
            Actor(Skeleton const& r,GroundedFootDefinition a,GroundedFootDefinition b)
              :pose(r),ik(r),grounded(r,{std::move(a),std::move(b)},
                 {"pelvis","torso",.12f,.10f}) {
                ensure(ik.register_chain({"left","l_thigh","l_knee","l_ankle"}),
                    "Benchmark left chain");
                ensure(ik.register_chain({"right","r_thigh","r_knee","r_ankle"}),
                    "Benchmark right chain");
            }
        };
        std::vector<std::unique_ptr<Actor>> actors;
        for(int i=0;i<128;++i)
            actors.push_back(std::make_unique<Actor>(rig,left,right));
        std::vector<JointTransform> bind;
        for(auto const& j:rig.joints)bind.push_back(j.bind);
        std::vector<std::uint8_t> keyed(rig.joints.size(),0);
        std::vector<double> frame_ms;
        for(int f=0;f<90;++f) {
            auto start=std::chrono::steady_clock::now();
            for(auto& actor:actors) {
                actor->pose.set_local_transforms(rig,bind,keyed);
                actor->grounded.provide_contact("left",contact);
                actor->grounded.provide_contact("right",contact);
                actor->grounded.update(nullptr,std::size_t(-1),
                    actor->pose,actor->ik,world,1.f/60.f);
                actor->ik.evaluate(actor->pose,1.f/60.f,2);
                actor->grounded.finish_toes(actor->pose);
            }
            frame_ms.push_back(std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-start).count());
        }
        std::sort(frame_ms.begin(),frame_ms.end());
        auto pct=[&](float q){return frame_ms[
            std::size_t(q*float(frame_ms.size()-1))];};
        ensure(std::isfinite(pct(.99f))&&pct(.99f)>=pct(.95f),
               "Invalid IK 128 actor p99");
        std::cout<<"Grounded IK 128 actors p50="<<pct(.50f)
                 <<"ms p95="<<pct(.95f)<<"ms p99="<<pct(.99f)<<"ms\\n";
        std::cout<<"Grounded IK: heel+toe, foot plant, pelvis, slope, deferred physics PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"Grounded IK FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
