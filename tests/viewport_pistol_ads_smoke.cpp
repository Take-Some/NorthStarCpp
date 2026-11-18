#include "../engine/northstarcpp/modules/viewport/viewport_skeletal_animation.hpp"
#include "../engine/northstarcpp/modules/viewport/viewport_frame_builder.hpp"
#include "../engine/northstarcpp/modules/viewport/viewport_elbow_collision_ik.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/native_model.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#define CHECK(c) do { if(!(c)) throw std::runtime_error("pistol ADS: " #c); } while(false)
using namespace northstarcpp;
using namespace northstarcpp::viewport_detail;
int main(int argc,char** argv) {
    CHECK(argc==2);
    // A valid hierarchy can have names opposite to physical anatomy.
    // Repair canonical references only; never reorder or mutate skin joints.
    auto make_arm_rig=[](bool swapped) {
        Skeleton rig;
        auto add=[&](char const* name,int parent,math::float3 local) {
            JointTransform bind{};
            bind.position=local;
            bind.rotation.identity();
            rig.joints.push_back({name,parent,bind});
            return static_cast<int>(rig.joints.size()-1);
        };
        const int pelvis=add("pelvis",-1,{0.f,0.f,0.f});
        const int head=add("headb",pelvis,{0.f,1.6f,0.f});
        add("r_eyeball",head,{.035f,.015f,.10f});
        add("l_eyeball",head,{-.035f,.015f,.10f});
        const float r=swapped?-1.f:1.f;
        const int rs=add("r_shoulder",pelvis,{r*.23f,1.35f,0.f});
        const int re=add("r_elbow",rs,{r*.14f,-.24f,.09f});
        const int rw=add("r_wrist",re,{r*.03f,-.22f,.12f});
        add("r_hand_prop_attachment",rw,{0.f,0.f,.04f});
        const int ls=add("l_shoulder",pelvis,{-r*.23f,1.35f,0.f});
        const int le=add("l_elbow",ls,{-r*.14f,-.24f,.09f});
        const int lw=add("l_wrist",le,{-r*.03f,-.22f,.12f});
        add("l_hand_prop_attachment",lw,{0.f,0.f,.04f});
        return rig;
    };
    for(bool swapped:{false,true}) {
        const auto rig=make_arm_rig(swapped);
        const auto map=detect_canonical_humanoid_skeleton(rig);
        CHECK(map.valid);
        CHECK(map.side_confirmed);
        CHECK(map.side_repaired==swapped);
        CHECK(rig.joints[map.r_hand].name==(swapped?"l_wrist":"r_wrist"));
        CHECK(rig.joints[map.r_hand_prop].name==
            (swapped?"l_hand_prop_attachment":"r_hand_prop_attachment"));
    }
    AssetStore assets;assets.mount_shared_assets(std::filesystem::path(argv[1]));
    const auto xml=assets.read("characters/abby/animations/weapon_grips.xml");
    const auto grips=parse_weapon_grips_xml(std::string_view(
        reinterpret_cast<char const*>(xml.data()),xml.size()));
    auto const* pistol=grips.find("pistol");
    CHECK(pistol!=nullptr);
    // ADS must align with physical pistol.ydd iron-sight vertices, not
    // the old inside-slide (y=.068,z=.028) artificial reference points.
    CHECK(std::abs(pistol->rear_sight.y-.09189f)<.002f);
    CHECK(std::abs(pistol->rear_sight.z+.020f)<.003f);
    CHECK(std::abs(pistol->front_sight.y-.09238f)<.002f);
    CHECK(std::abs(pistol->front_sight.z-.178f)<.003f);
    auto native=load_native_model_resource(assets,
        "characters/abby/models/abby.ydd");
    CHECK(native.skeleton.has_value());
    auto const& skeleton=*native.skeleton;
    auto clips=load_skeletal_clips(assets,
        "characters/abby/animations/mm-explore.ycd",
        {"abby-mm-explore-idle"},skeleton);
    CHECK(!clips.empty());
    Scene scene;scene.objects.resize(2);
    scene.objects[0].node.id=1;scene.objects[1].node.id=2;
    scene.components.push_back({2,"engine.model.bone_attachment",1,
        "1 r_hand_prop_attachment 0 0 0 0 0 0 camera_forward"});
    auto const eye=Vector3{0.f,1.62f,0.f};
    for(auto const [yaw,pitch]:
        {std::pair{0.f,0.f},std::pair{.35f,.15f},std::pair{-.35f,-.15f},
         std::pair{.70f,.30f},std::pair{-.70f,-.30f},
         std::pair{.90f,.50f},std::pair{-.90f,-.50f}}) {
        auto const world=math::create_rotation_y(-yaw);
        SkeletonPose pose(skeleton);
        auto results=evaluate_pose({skeleton,clips.front(),0.f,
            world,native.mesh_to_skeleton,
            GripView{yaw,pitch,true,eye,.52f,.115f,.063f,pistol,true}},pose);
        CHECK(results[0] && results[1]);
        SkeletalSceneFrame frame;
        frame.prepare(scene);
        frame.publish_sockets(1,skeleton,pose,native.mesh_to_skeleton,world);
        CameraPose camera{};
        camera.position=eye;camera.yaw=yaw;camera.pitch=pitch;
        auto gun=frame.attachment_transform(frame.attachments.at(2),&camera);
        CHECK(gun.has_value());
        const Vector3 forward{std::sin(yaw)*std::cos(pitch),
            std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        const Vector3 rear=gun->transform_position(pistol->rear_sight);
        const Vector3 front=gun->transform_position(pistol->front_sight);
        const Vector3 delta=rear-eye;
        const float depth=CharacterSelfCollision::dot(delta,forward);
        const float offset=(delta-forward*depth).magnitude();
        const Vector3 front_delta=front-eye;
        const auto front_depth=CharacterSelfCollision::dot(front_delta,forward);
        const float front_offset=(front_delta-forward*front_depth).magnitude();
        const auto direction=gun->transform_direction({0.f,0.f,1.f});
        const float direction_dot=CharacterSelfCollision::dot(
            direction/direction.magnitude(),forward);
        const auto right=skeleton.joint_index("r_wrist");
        const auto left=skeleton.joint_index("l_wrist");
        CHECK(right<skeleton.joints.size() && left<skeleton.joints.size());
        math::float4x4 skeleton_to_mesh;
        CHECK(skeleton_to_mesh.try_invert(native.mesh_to_skeleton));
        const Vector3 rw=(pose.joints_world()[right]*skeleton_to_mesh*world)
            .transform_position({0,0,0});
        const Vector3 lw=(pose.joints_world()[left]*skeleton_to_mesh*world)
            .transform_position({0,0,0});
        std::fprintf(stderr,
            "[PISTOL ADS] yaw=%.2f pitch=%.2f sight_error=%.4f "
            "rear_depth=%.3f gun_axis=%.4f wrist_gap=%.3f\n",
            yaw,pitch,offset,depth,direction_dot,(rw-lw).magnitude());
        CHECK(offset<.005f);
        CHECK(front_offset<.005f);
        CHECK(std::abs(depth-pistol->sight_eye_distance)<.09f);
        CHECK(direction_dot>.98f);
        CHECK((rw-lw).magnitude()>.04f);
        CHECK((rw-lw).magnitude()<.22f);
        // Use repaired physical chains, never their possibly mirrored names.
        const auto& map=skeleton.humanoid;
        CHECK(map.valid && map.r_hand<skeleton.joints.size() &&
              map.l_hand<skeleton.joints.size() &&
              map.r_forearm<skeleton.joints.size() &&
              map.l_forearm<skeleton.joints.size());
        auto const at=[&](std::size_t joint) {
            return (pose.joints_world()[joint]*skeleton_to_mesh*world)
                .transform_position({0,0,0});
        };
        const Vector3 view_right{std::cos(yaw),0.f,-std::sin(yaw)};
        const auto signed_gap=[&](Vector3 a,Vector3 b) {
            const auto v=a-b;
            return v.x*view_right.x+v.y*view_right.y+v.z*view_right.z;
        };
        std::fprintf(stderr,"[ADS ANATOMY] wrist_gap_signed=%.3f elbow_gap_signed=%.3f repaired=%d\\n",
            signed_gap(at(map.r_hand),at(map.l_hand)),
            signed_gap(at(map.r_forearm),at(map.l_forearm)),
            int(map.side_repaired));
        const auto diagnostic_joint=[&](std::size_t index) {
            const auto p=at(index);
            return p;
        };
        for(auto const [name,index]:std::array<std::pair<char const*,std::size_t>,6>{
            {{"right_shoulder",map.r_upperarm},{"right_elbow",map.r_forearm},
             {"right_wrist",map.r_hand},{"left_shoulder",map.l_upperarm},
             {"left_elbow",map.l_forearm},{"left_wrist",map.l_hand}}}) {
            const auto pt=diagnostic_joint(index);
            const auto relative=pt-eye;
            const float x=CharacterSelfCollision::dot(relative,view_right);
            const float y=CharacterSelfCollision::dot(relative,
                Vector3{-std::sin(yaw)*std::sin(pitch),std::cos(pitch),
                        -std::cos(yaw)*std::sin(pitch)});
            const float z=CharacterSelfCollision::dot(relative,forward);
            std::fprintf(stderr,"[ADS JOINT] yaw=%.2f pitch=%.2f %s"
                " right=%.3f up=%.3f depth=%.3f\\n",
                yaw,pitch,name,x,y,z);
        }
        const float sight_right=ads_forearm_sight_clearance(
            eye,forward,at(map.r_forearm),at(map.r_hand));
        const float sight_left=ads_forearm_sight_clearance(
            eye,forward,at(map.l_forearm),at(map.l_hand));
        std::fprintf(stderr,
            "[ADS CLEARANCE] yaw=%.2f pitch=%.2f forearm_r=%.4f forearm_l=%.4f\\n",
            yaw,pitch,sight_right,sight_left);
        // The sleeve-bearing forearm must stay outside the eye's optical
        // corridor. Unlike a bone-length-only test, this also detects the
        // old crossed/raised-elbow silhouette that covered iron sights.
        CHECK(std::isfinite(sight_right) && std::isfinite(sight_left));
        CHECK(sight_right>.14f && sight_left>.14f);
        // Render-only recoil is physically solved on the same arm rig.
        // Its backstroke must not rotate the sights away from camera centre.
        SkeletonPose recoil_pose(skeleton);
        auto recoil_grip=GripView{yaw,pitch,true,eye,.52f,.115f,.063f,
            pistol,true};
        recoil_grip.recoil_back=.025f;
        recoil_grip.recoil_arm=.56f;
        auto recoil_result=evaluate_pose({skeleton,clips.front(),0.f,
            world,native.mesh_to_skeleton,recoil_grip},recoil_pose);
        CHECK(recoil_result[0] && recoil_result[1]);
        SkeletalSceneFrame recoil_frame;
        recoil_frame.prepare(scene);
        recoil_frame.publish_sockets(1,skeleton,recoil_pose,
            native.mesh_to_skeleton,world);
        auto recoil_gun=recoil_frame.attachment_transform(
            recoil_frame.attachments.at(2),&camera);
        CHECK(recoil_gun.has_value());
        const auto moved_rear=recoil_gun->transform_position(pistol->rear_sight);
        const auto moved_delta=moved_rear-eye;
        const float moved_depth=CharacterSelfCollision::dot(moved_delta,forward);
        const float moved_offset=(moved_delta-forward*moved_depth).magnitude();
        CHECK(moved_offset<.006f);
        CHECK(std::abs(moved_depth-(pistol->sight_eye_distance-.025f))<.08f);
        const auto shoulder_axis=at(map.r_upperarm)-at(map.l_upperarm);
        const float axis_sign=CharacterSelfCollision::dot(
            shoulder_axis,view_right)<0.f?-1.f:1.f;
        CHECK(axis_sign*signed_gap(at(map.r_hand),at(map.l_hand))>.03f);
        CHECK(axis_sign*signed_gap(at(map.r_forearm),at(map.l_forearm))>.09f);
    }
    // Test the actual imported wrist reach after an owner-camera correction.
    for(float camera_forward:{0.f,.22f,.55f}) {
        const Vector3 shifted_eye{0.f,1.62f,camera_forward};
        SkeletonPose shifted_pose(skeleton);
        math::float4x4 world;world.identity();
        const auto grips=evaluate_pose({skeleton,clips.front(),0.f,
            world,native.mesh_to_skeleton,
            GripView{0.f,0.f,true,shifted_eye,.52f,.115f,.063f,
                pistol,true}},shifted_pose);
        SkeletalSceneFrame frame;frame.prepare(scene);
        frame.publish_sockets(1,skeleton,shifted_pose,
            native.mesh_to_skeleton,world);
        CameraPose camera{};camera.position=shifted_eye;
        const auto gun=frame.attachment_transform(frame.attachments.at(2),&camera);
        CHECK(gun.has_value());
        const auto rear=gun->transform_position(pistol->rear_sight);
        if(camera_forward<=.22f) {
            // The muzzle, rear and front sights must remain ahead of the
            // camera after applying full-body FPS self-collision clearance.
            CHECK(grips[0] && grips[1]);
            CHECK(rear.z-shifted_eye.z>.15f);
            CHECK(std::abs((rear.z-shifted_eye.z)-
                pistol->sight_eye_distance)<.085f);
            CHECK(std::hypot(rear.x-shifted_eye.x,
                rear.y-shifted_eye.y)<.02f);
        }
        std::fprintf(stderr,"[ADS CAMERA CLEARANCE] camera_z=%.3f rear_z=%.3f depth=%.3f solved=%d\n",
            camera_forward,rear.z,rear.z-shifted_eye.z,
            int(grips[0].has_value()&&grips[0]->solved));
    }
    // The physically corrected first-person eye rotates with the actor.
    // Verify real iron sights remain ahead of that eye at nonzero yaw/pitch.
    for(auto const [yaw,pitch]:{
        std::pair{.35f,.15f},std::pair{-.35f,-.15f},
        std::pair{.70f,.30f},std::pair{-.70f,-.30f}}) {
        const Vector3 eye{std::sin(yaw)*.22f,1.62f,
                          std::cos(yaw)*.22f};
        const auto world=math::create_rotation_y(-yaw);
        SkeletonPose pose(skeleton);
        auto result=evaluate_pose({skeleton,clips.front(),0.f,
            world,native.mesh_to_skeleton,
            GripView{yaw,pitch,true,eye,.52f,.115f,.063f,pistol,true}},pose);
        CHECK(result[0] && result[1]);
        SkeletalSceneFrame frame;frame.prepare(scene);
        frame.publish_sockets(1,skeleton,pose,native.mesh_to_skeleton,world);
        CameraPose camera{};camera.position=eye;
        camera.yaw=yaw;camera.pitch=pitch;
        const auto gun=frame.attachment_transform(frame.attachments.at(2),&camera);
        CHECK(gun.has_value());
        const Vector3 view_forward{std::sin(yaw)*std::cos(pitch),
            std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        const Vector3 rear=gun->transform_position(pistol->rear_sight);
        const Vector3 delta=rear-eye;
        const float depth=CharacterSelfCollision::dot(delta,view_forward);
        const float lateral=(delta-view_forward*depth).magnitude();
        CHECK(depth>.15f);
        CHECK(std::abs(depth-pistol->sight_eye_distance)<.085f);
        std::fprintf(stderr,"[ADS ROTATED CLEARANCE] yaw=%.2f pitch=%.2f depth=%.3f lateral=%.3f\n",yaw,pitch,depth,lateral);
        CHECK(lateral<.015f);
    }
    Scene scene_modes;scene_modes.objects.resize(1);
    scene_modes.objects[0].node.id=3;
    scene_modes.components={{3,"engine.model.visual_mode",1,
        "first_person_body 0.52 0.115 0 0.063 ads 0.025 0.56"}};
    ViewportFrameBuilder visual;
    visual.prepare(scene_modes,true,true);
    CHECK(visual.first_person(3) && visual.first_person(3)->aim_down_sights);
    CHECK(std::abs(visual.first_person(3)->recoil_back-.025f)<1.e-5f);
    CHECK(std::abs(visual.first_person(3)->recoil_arm-.56f)<1.e-5f);
    scene_modes.components[0].payload=
        "first_person_body 0.52 0.24 0 0.12 ready";
    visual.prepare(scene_modes,true,true);
    CHECK(visual.first_person(3) && !visual.first_person(3)->aim_down_sights);
    std::puts("PASS pistol iron-sight camera ray and two-hand grip");
}
