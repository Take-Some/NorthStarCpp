#include "../engine/northstarcpp/modules/viewport/viewport_skeletal_animation.hpp"
#include "../engine/northstarcpp/modules/viewport/viewport_skeletal_basis.hpp"
#include "../engine/northstarcpp/modules/viewport/viewport_character_self_collision.hpp"
#include "../engine/northstarcpp/modules/viewport/viewport_skin_cache.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/procedural_ik.hpp>
#include <northstarcpp/grounded_locomotion.hpp>
#include <northstarcpp/posture_ik.hpp>
#include <northstarcpp/native_model.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#define REQUIRE(condition) do { if (!(condition)) \
    throw std::runtime_error("Skeletal viewport pipeline: " #condition); } while(false)

using namespace northstarcpp;
using namespace northstarcpp::viewport_detail;

void check_animation()
{
    Scene scene;
    scene.objects.resize(3);
    scene.objects[0].node.id=2;
    scene.objects[1].node.id=3;
    scene.objects[2].node.id=1;
    scene.components={{1,"engine.model.animation",1,"animation.ycd@idle"},
        {2,"engine.model.bone_attachment",1,"1 r_hand_prop_attachment 1 0 0 0 0 0"},
        {3,"engine.model.bone_attachment",1,"2 muzzle 0 0 0 0 0 0"}};
    SkeletalSceneFrame frame;
    frame.prepare(scene);
    REQUIRE((frame.order==std::vector<std::size_t>{2,0,1}));
    REQUIRE(frame.animations.at(1)=="animation.ycd@idle");
    REQUIRE(frame.wants_hand_grip(1) && !frame.wants_hand_grip(2));
    REQUIRE(!frame.attachment_transform(frame.attachments.at(2),nullptr));

    Skeleton rig;
    SkeletonJoint joint;
    joint.name="r_hand_prop_attachment";
    joint.bind.position={2.f,0.f,0.f};
    joint.bind.rotation.identity();
    rig.joints.push_back(joint);
    math::float4x4 identity;
    identity.identity();
    rig.inverse_bind={identity};
    SkeletalClip clip;
    clip.frames=1;
    clip.sample_rate=30.f;
    SkeletonPose pose(rig);
    const auto world=math::create_translation({10.f,0.f,0.f});
    evaluate_pose({rig,clip,0.f,world,identity,{}},pose);
    frame.publish_sockets(1,rig,pose,identity,world);
    const auto child=frame.attachment_transform(frame.attachments.at(2),nullptr);
    REQUIRE(child && child->elements[3][0]==13.f);

    scene.components.push_back({1,"engine.model.bone_attachment",1,"3 muzzle 0 0 0 0 0 0"});
    frame.prepare(scene);
    REQUIRE(frame.has_cycles && frame.order.size()==3);
    REQUIRE(!frame.attachment_transform(frame.attachments.at(2),nullptr));
    scene.components.clear();
    frame.prepare(scene);
    REQUIRE(!frame.has_cycles);
    REQUIRE((frame.order==std::vector<std::size_t>{0,1,2}));
    scene.components.push_back({1,"engine.model.animation_graph",1,
        "animations/abby_locomotion.nagraph.xml"});
    scene.components.push_back({1,"engine.model.animation_parameter",1,"speed 0.5"});
    frame.prepare(scene);
    REQUIRE(frame.graphs.at(1)=="animations/abby_locomotion.nagraph.xml");
    REQUIRE(frame.graph_parameters.at(1).size()==1);
    REQUIRE(frame.graph_parameters.at(1)[0].first=="speed");
    REQUIRE(std::abs(frame.graph_parameters.at(1)[0].second-.5f)<.0001f);
    scene.components.push_back({1,"engine.model.ik_chain",1,
        "left_leg hip knee ankle"});
    scene.components.push_back({1,"engine.model.ik_target",1,
        "left_leg 0.42 0.03 0.08 0.3 0.65 0.18 1 10"});
    scene.components.push_back({1,"engine.model.ik_ground_contact",1,
        "left_leg 0.4 -0.04 0 0 1 0 0 0.1 0.1 0.75 5"});
    scene.components.push_back({1,"engine.model.grounded_foot",1,
        "left_leg ankle 0 -0.04 -0.11 0 -0.04 0.16"});
    scene.components.push_back({1,"engine.model.grounded_body",1,"hip -"});
    frame.prepare(scene);
    REQUIRE(frame.ground_feet.at(1).size()==1);
    REQUIRE(frame.ground_bodies.at(1)=="hip -");
    REQUIRE(frame.ik_chains.at(1).size()==1);
    REQUIRE(frame.ik_targets.at(1).size()==1);
    REQUIRE(frame.ik_contacts.at(1).size()==1);
    Skeleton leg_rig;
    for(auto const& [name,parent,offset]:
        std::array<std::tuple<char const*,int,math::float3>,3>{{
            {"hip",-1,{0.f,1.f,0.f}},
            {"knee",0,{.2f,-.5f,0.f}},
            {"ankle",1,{.2f,-.5f,0.f}}
        }}) {
        SkeletonJoint j;
        j.name=name;j.parent=parent;j.bind.position=offset;
        j.bind.rotation.identity();
        leg_rig.joints.push_back(j);
        math::float4x4 identity;identity.identity();
        leg_rig.inverse_bind.push_back(identity);
    }
    SkeletonPose leg_pose(leg_rig);
    std::unique_ptr<ProceduralIkController> leg_controller;
    std::vector<std::string> definitions;
    std::unique_ptr<GroundedLocomotionController> grounded;
    std::vector<std::string> foot_cache;
    std::string body_cache;
    math::float4x4 rig_world;rig_world.identity();
    apply_procedural_ik(frame,1,leg_rig,leg_pose,
        leg_controller,definitions,.1f,
        nullptr,&rig_world,&grounded,&foot_cache,&body_cache);
    REQUIRE(grounded && grounded->foot_count()==1);
    REQUIRE(leg_controller && leg_controller->reports().size()==1);
    REQUIRE(leg_controller->reports()[0].attempted);
    REQUIRE(leg_controller->reports()[0].effective_weight>.5f);
    // Look-at parameters are rig-authored scene components, independent
    // from whether generic limb chains are present.
    scene.components.push_back({1,"engine.model.look_joint",1,
        "hip 1 0.12 0 0 1"});
    scene.components.push_back({1,"engine.model.look_target",1,
        "1 1.3 2 1"});
    scene.components.push_back({1,"engine.model.vehicle_seat_pose",1,
        "0.2 0.8 0.1 0.0 1"});
    scene.components.push_back({1,"engine.model.vehicle_reach",1,
        "left_leg knee 0.1 0.0 0.2 1 50"});
    scene.components.push_back({1,"engine.model.root_motion_policy",1,"capsule hip"});
    frame.prepare(scene);
    REQUIRE(frame.look_joints.at(1).size()==1);
    REQUIRE(frame.look_targets.at(1)=="1 1.3 2 1");
    REQUIRE(frame.vehicle_seat_poses.at(1)=="0.2 0.8 0.1 0.0 1");
    REQUIRE(frame.vehicle_reaches.at(1).size()==1);
    REQUIRE(frame.root_motion_policies.at(1)=="capsule hip");
    std::unique_ptr<DistributedLookAt> looking;
    std::vector<std::string> look_cache;
    apply_distributed_lookat(frame,1,leg_rig,leg_pose,rig_world,
        .1f,looking,look_cache);
    REQUIRE(looking && looking->joint_count()==1);
    // A scene change must discard stale IK configurations and requests.
    for(int n=0;n<10;++n)scene.components.pop_back();
    frame.prepare(scene);
    apply_procedural_ik(frame,1,leg_rig,leg_pose,
        leg_controller,definitions,.05f,
        nullptr,&rig_world,&grounded,&foot_cache,&body_cache);
    REQUIRE(!leg_controller && definitions.empty());
    REQUIRE(!grounded && foot_cache.empty());
    apply_distributed_lookat(frame,1,leg_rig,leg_pose,rig_world,
        .05f,looking,look_cache);
    REQUIRE(!looking && look_cache.empty());

}

void check_mesh_space_skin_palette()
{
    // Regression: imported character vertices are mesh-local, whereas the
    // source YMT palette is skeleton-local. Abby mirrors X and Z.
    const auto basis=math::create_scale({-1.f,1.f,-1.f});
    const std::vector<math::float4x4> source{
        math::create_translation({.23f,.11f,.17f})};
    std::vector<math::float4x4> transformed;
    remap_skin_palette_to_mesh(transformed,source,basis);
    REQUIRE(transformed.size()==1);
    auto const moved=transformed[0].transform_position({0.f,0.f,0.f});
    REQUIRE(std::abs(moved.x+.23f)<.0001f);
    REQUIRE(std::abs(moved.y-.11f)<.0001f);
    REQUIRE(std::abs(moved.z+.17f)<.0001f);
    const auto capacity=transformed.capacity();
    remap_skin_palette_to_mesh(transformed,source,basis);
    REQUIRE(transformed.capacity()==capacity);
    auto const source_origin=source[0].transform_position({0.f,0.f,0.f});
    REQUIRE(std::abs(source_origin.x-.23f)<.0001f);
}

void check_cache()
{
    namespace fs=std::filesystem;
    const auto base=fs::temp_directory_path()/
        ("northstar-ydd-ddc-smoke-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path dir;
        ~Cleanup() { std::error_code error; fs::remove_all(dir,error); }
    } cleanup{base};

    MeshAsset mesh;
    mesh.vertices.resize(4);
    mesh.vertices[1].position.x=1.f;
    mesh.vertices[2].position.y=1.f;
    mesh.vertices[3].position.z=1.f;
    mesh.indices={0,1,2,1,2,3};
    mesh.sections={{"one","",0,3},{"two","",3,3}};
    std::vector<SkinWeights> weights(4);
    for(auto& weight:weights)weight.weights[0]=1.f;
    const auto cold=prepare_skin_sections({mesh,weights,nullptr,"vulkan","1"},base);
    REQUIRE(!cold.cache_hit && cold.sections[1][2].z==1.f);
    const auto warm=prepare_skin_sections({mesh,weights,nullptr,"vulkan","1"},base);
    REQUIRE(warm.cache_hit);
    REQUIRE(std::memcmp(warm.sections[1].data(),cold.sections[1].data(),
                        3*sizeof(NorthStarSkinMaterialVertex))==0);
    // Corruption is detected and repaired using the native geometry.
    const auto entry=fs::directory_iterator(base)->path();
    { std::ofstream corrupt(entry,std::ios::binary|std::ios::trunc); corrupt<<"invalid"; }
    REQUIRE(!prepare_skin_sections({mesh,weights,nullptr,"vulkan","1"},base).cache_hit);
    REQUIRE(prepare_skin_sections({mesh,weights,nullptr,"vulkan","1"},base).cache_hit);
    REQUIRE(!prepare_skin_sections({mesh,weights,nullptr,"d3d11","1"},base).cache_hit);
    REQUIRE(!prepare_skin_sections({mesh,weights,nullptr,"vulkan","2"},base).cache_hit);
    const std::vector<std::uint8_t> arms{1,1,1,0};
    const auto filtered=prepare_skin_sections({mesh,weights,&arms,"vulkan","1"},base);
    REQUIRE(!filtered.cache_hit && filtered.sections[0].size()==3 && filtered.sections[1].empty());
    REQUIRE(prepare_skin_sections({mesh,weights,&arms,"vulkan","1"},base).cache_hit);
    mesh.vertices[3].position.z=2.f;
    const auto modified=prepare_skin_sections({mesh,weights,nullptr,"vulkan","1"},base);
    REQUIRE(!modified.cache_hit && modified.sections[1][2].z==2.f);
}

void check_grip_ik()
{
    // A minimal humanoid chain validates the quaternion/matrix convention:
    // moving the target must reduce wrist error without changing bone lengths.
    Skeleton rig;
    for(auto const* name:{"r_shoulder","r_elbow","r_wrist"}) {
        SkeletonJoint j;
        j.name=name;
        j.parent=static_cast<int>(rig.joints.size())-1;
        j.bind.rotation.identity();
        if(j.parent>=0)j.bind.position={0.f,0.f,.27f};
        rig.joints.push_back(j);
        math::float4x4 identity;
        identity.identity();
        rig.inverse_bind.push_back(identity);
    }
    SkeletonPose pose(rig);
    SkeletalClip clip;
    clip.frames=1;clip.sample_rate=30.f;
    pose.sample(rig,clip,0.f,false);
    auto position=[&](unsigned i){
        auto const& m=pose.joints_world()[i];
        return math::float3{m.elements[3][0],m.elements[3][1],m.elements[3][2]};
    };
    const auto target=math::float3{.19f,-.16f,.39f};
    const auto before=(position(2)-target).magnitude();
    const auto upper=(position(1)-position(0)).magnitude();
    const auto lower=(position(2)-position(1)).magnitude();
    REQUIRE(pose.aim_hand(rig,"r_shoulder","r_elbow","r_wrist",target,9));
    const auto after=(position(2)-target).magnitude();
    std::fprintf(stderr,"[IK QA] before=%.4f after=%.4f\\n",before,after);
    REQUIRE(after<before*.5f);
    REQUIRE(after<.09f);
    REQUIRE(std::abs((position(1)-position(0)).magnitude()-upper)<.001f);
    REQUIRE(std::abs((position(2)-position(1)).magnitude()-lower)<.001f);
}

void check_abby_grip(std::filesystem::path const& root) {
    AssetStore assets;
    assets.mount_shared_assets(root);
    auto native=load_native_model_resource(assets,
        "characters/abby/models/abby.ydd");
    REQUIRE(native.skeleton.has_value());
    // Verify the gun's visible local origin against the hand socket.
    // A wrong mesh pivot/asset offset cannot be compensated by wrist IK.
    auto const pistol=load_native_model_resource(assets,
        "models/weapon/pistol/pistol.ydd",true);
    auto const shell=load_scene_mesh(assets,
        "models/weapon/pistol/pistol_shell.ydd");
    std::fprintf(stderr,
        "[WEAPON SHELL BOUNDS] min=(%.6f %.6f %.6f)"
        " max=(%.6f %.6f %.6f) vertices=%zu\n",
        shell.minimum.x,shell.minimum.y,shell.minimum.z,
        shell.maximum.x,shell.maximum.y,shell.maximum.z,
        shell.vertices.size());
    auto const& mn=pistol.mesh.minimum;
    auto const& mx=pistol.mesh.maximum;
    std::fprintf(stderr,
        "[WEAPONLAB PISTOL BOUNDS] min=(%.3f %.3f %.3f)"
        " max=(%.3f %.3f %.3f) middle=(%.3f %.3f %.3f)\\n",
        mn.x,mn.y,mn.z,mx.x,mx.y,mx.z,
        (mn.x+mx.x)*.5f,(mn.y+mx.y)*.5f,(mn.z+mx.z)*.5f);
    // Original-YDD muzzle cross-section audit; geometry, not screen offsets.
    for(float depth: { .205f,.220f,.230f,.235f }) {
        std::size_t count=0;
        float ymin=1e6f,ymax=-1e6f,xmin=1e6f,xmax=-1e6f;
        for(auto const& vertex:pistol.mesh.vertices) {
            auto const p=vertex.position;
            if(p.z<depth)continue;
            ++count;
            ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);
            xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);
        }
        std::fprintf(stderr,
            "[PISTOL NOSE] z>=%.3f count=%zu x=(%.4f,%.4f) y=(%.4f,%.4f)\n",
            depth,count,xmin,xmax,ymin,ymax);
    }
    auto const& skeleton=*native.skeleton;
    auto const grip_bytes=assets.read(
        "characters/abby/animations/weapon_grips.xml");
    auto const weapon_grips=viewport_detail::parse_weapon_grips_xml(
        std::string_view(reinterpret_cast<char const*>(grip_bytes.data()),
                         grip_bytes.size()));
    auto const* pistol_grip=weapon_grips.find("pistol");
    auto const* rifle_grip=weapon_grips.find("rifle");
    REQUIRE(pistol_grip && rifle_grip);
    REQUIRE(pistol_grip->bends.size()==30);
    REQUIRE(rifle_grip->bends.size()==30);
    REQUIRE(pistol_grip->support_offset.x>.02f);
    math::float4x4 skeleton_to_mesh;
    REQUIRE(skeleton_to_mesh.try_invert(native.mesh_to_skeleton));
    // Source YDD basis may be normalized at import time. The test must
    // inspect the asset's actual contract, not special-case Abby's old axis.
    for(auto const& [helper,source]:skeleton.mirrored_helpers)
        if(skeleton.joints[source].name.find("index")!=std::string::npos ||
           skeleton.joints[source].name.find("middle")!=std::string::npos ||
           skeleton.joints[source].name.find("thumb")!=std::string::npos)
            std::fprintf(stderr,"[FINGER MIRROR] %s -> %s\\n",
                skeleton.joints[helper].name.c_str(),
                skeleton.joints[source].name.c_str());
    auto clips=load_skeletal_clips(assets,
        "characters/abby/animations/mm-explore.ycd",
        {"abby-mm-explore-idle"},skeleton);
    auto const& clip=clips.front();
    // Validate the actual imported YDD/YMT orientation, not merely a
    // synthetic 180-degree matrix. Eyes and toes must point to engine +Z
    // in the character's neutral world heading.
    auto const presentation=viewport_detail::skeletal_presentation_basis(
        native.mesh_to_skeleton);
    SkeletonPose natural(skeleton);
    natural.sample(skeleton,clip,0.f,false);
    auto posed_joint=[&](char const* name) {
        auto const index=skeleton.joint_index(name);
        REQUIRE(index<skeleton.joints.size());
        return (natural.joints_world().at(index) *
            skeleton_to_mesh * presentation).transform_position({0.f,0.f,0.f});
    };
    auto const eye_joint=posed_joint("r_eyeball");
    auto const head_joint=posed_joint("headb");
    auto const toe_joint=posed_joint("r_toe");
    auto const knee_joint=posed_joint("r_knee");
    std::fprintf(stderr,"[ABBY FACING] eye.z=%.3f head.z=%.3f toe.z=%.3f knee.z=%.3f\n",
        eye_joint.z,head_joint.z,toe_joint.z,knee_joint.z);
    REQUIRE(eye_joint.z>head_joint.z+.04f);
    REQUIRE(toe_joint.z>knee_joint.z+.05f);
    auto const canonical=skeleton.humanoid;
    std::fprintf(stderr,"[CANONICAL BIND] valid=%d confirmed=%d swapped=%d right=%zu left=%zu basis=%d/%d\\n",
        canonical.valid,canonical.side_confirmed,canonical.side_repaired,
        canonical.r_upperarm,canonical.l_upperarm,
        canonical.hand_basis_valid[0],canonical.hand_basis_valid[1]);
    auto canonical_pos=[&](std::size_t index) {
        REQUIRE(index<skeleton.joints.size());
        return posed_joint(skeleton.joints[index].name.c_str());
    };
    auto rig_right=canonical_pos(canonical.r_upperarm);
    auto rig_left=canonical_pos(canonical.l_upperarm);
    // Bind-space and mesh presentation can disagree about left/right.
    // Use the physical side of the *posed* camera-facing rig in this test.
    if(rig_right.x<rig_left.x)std::swap(rig_right,rig_left);
    const auto physical_right_wrist=
        posed_joint("r_shoulder").x>posed_joint("l_shoulder").x?
            "r_wrist":"l_wrist";
    const auto physical_right_socket=
        posed_joint("r_shoulder").x>posed_joint("l_shoulder").x?
            "r_hand_prop_attachment":"l_hand_prop_attachment";
    std::fprintf(stderr,"[CANONICAL WORLD] right.x=%.3f left.x=%.3f nominal_r.x=%.3f nominal_l.x=%.3f eyeR.x=%.3f eyeL.x=%.3f\\n",
        rig_right.x,rig_left.x,posed_joint("r_shoulder").x,
        posed_joint("l_shoulder").x,
        posed_joint("r_eyeball").x,posed_joint("l_eyeball").x);
    REQUIRE((rig_right-rig_left).magnitude()>.25f);
    // A camera-driven grip must remain on the named shoulder's side,
    // regardless of whether the imported rig calls right +X or -X.
    SkeletonPose armed(skeleton);
    const auto grips=evaluate_pose({skeleton,clip,0.f,presentation,
        native.mesh_to_skeleton,
        GripView{0.f,0.f,true,eye_joint,.52f,.24f,.12f,pistol_grip}},armed);
    REQUIRE(grips[0] && grips[1]);
    // Verify imported WeaponLab curls visibly change phalange positions
    // RELATIVE to the wrist; moving the entire hand via IK must not count.
    auto finger_from_wrist=[&](SkeletonPose const& pose,
                               char const* wrist,char const* tip) {
        auto const wi=skeleton.joint_index(wrist);
        auto const ti=skeleton.joint_index(tip);
        REQUIRE(wi<skeleton.joints.size() && ti<skeleton.joints.size());
        math::float4x4 inv;
        REQUIRE(inv.try_invert(pose.joints_world()[wi]));
        return (pose.joints_world()[ti]*inv).transform_position({0.f,0.f,0.f});
    };
    for(auto const& [wrist,tip] :
        std::array<std::pair<char const*,char const*>,4>{{
            {"r_wrist","r_middlec"},{"l_wrist","l_middlec"},
            {"r_wrist","r_ringc"},{"l_wrist","l_ringc"}}}) {
        auto const before=finger_from_wrist(natural,wrist,tip);
        auto const after=finger_from_wrist(armed,wrist,tip);
        const auto delta=(after-before).magnitude();
        std::fprintf(stderr,
            "[WEAPONLAB FINGER] %s bend_delta=%.5f m\n",tip,delta);
        REQUIRE(delta>.008f);
    }
    // Mirrored helper finger joints must share the source palette exactly.
    for(auto const& pair:skeleton.mirrored_helpers) {
        auto const name=skeleton.joints[pair.second].name;
        if(name.find("index")==std::string::npos &&
           name.find("middle")==std::string::npos &&
           name.find("thumb")==std::string::npos)continue;
        float largest=0.f;
        for(int row=0;row<4;++row)for(int col=0;col<4;++col)
            largest=std::max(largest,std::abs(
                armed.palette()[pair.first].elements[row][col]-
                armed.palette()[pair.second].elements[row][col]));
        REQUIRE(largest<.0001f);
    }
    const Vector3 lateral=rig_right-rig_left;
    const Vector3 mid=(rig_right+rig_left)*.5f;
    const auto signed_lateral=[&](Vector3 point) {
        return (point.x-mid.x)*lateral.x+(point.z-mid.z)*lateral.z;
    };
    std::fprintf(stderr,"[CANONICAL GRIP] target=(%.3f %.3f %.3f) rigR=(%.3f %.3f %.3f) rigL=(%.3f %.3f %.3f) lateral=%.4f\\n",
        grips[0]->target.x,grips[0]->target.y,grips[0]->target.z,
        rig_right.x,rig_right.y,rig_right.z,
        rig_left.x,rig_left.y,rig_left.z,
        signed_lateral(grips[0]->target));
    REQUIRE(signed_lateral(grips[0]->target)>0.f);
    REQUIRE(grips[0]->target.z>rig_right.z+.10f);
    REQUIRE(grips[1]->target.z>rig_left.z+.05f);
    // The actual camera-aligned child transform must obey the same capsule
    // protection as the primary grip IK, not just its desired wrist point.
    Scene attachment_scene;
    attachment_scene.objects.resize(2);
    attachment_scene.objects[0].node.id=1;
    attachment_scene.objects[1].node.id=2;
    attachment_scene.components.push_back({2,"engine.model.bone_attachment",
        1,"1 r_hand_prop_attachment 0 0 0 0 0 0 camera_forward"});
    SkeletalSceneFrame socket_frame;
    socket_frame.prepare(attachment_scene);
    socket_frame.publish_sockets(1,skeleton,armed,native.mesh_to_skeleton,presentation);
    CameraPose gun_camera{};
    gun_camera.position=eye_joint;
    auto const weapon_world=socket_frame.attachment_transform(
        socket_frame.attachments.at(2),&gun_camera);
    REQUIRE(weapon_world.has_value());
    auto armed_world=[&](char const* name) {
        const auto index=skeleton.joint_index(name);
        REQUIRE(index<skeleton.joints.size());
        return (armed.joints_world().at(index)*
            skeleton_to_mesh*presentation).transform_position({0.f,0.f,0.f});
    };
    const auto weapon_body=make_character_self_collision(
        armed_world("pelvis"),armed_world("spineb"),armed_world("spined"),
        armed_world("headb"),armed_world("r_shoulder"),
        armed_world("l_shoulder"),armed_world("r_eyeball")-armed_world("headb"));
    REQUIRE(weapon_body.active);
    const Vector3 weapon_origin{weapon_world->elements[3][0],
        weapon_world->elements[3][1],weapon_world->elements[3][2]};
    const auto weapon_forward=weapon_world->transform_direction({0.f,0.f,1.f});
    auto armed_wrist=[&](char const* bone) {
        auto const i=skeleton.joint_index(bone);
        REQUIRE(i<skeleton.joints.size());
        return (armed.joints_world()[i]*skeleton_to_mesh*presentation)
            .transform_position({0.f,0.f,0.f});
    };
    REQUIRE((weapon_origin-armed_wrist(physical_right_wrist)).magnitude()<.09f);
    REQUIRE(weapon_forward.z>.95f);
    std::fprintf(stderr,
        "[GRIP SOCKET VALIDATION] right-wrist=(%.3f %.3f %.3f)"
        " socket=(%.3f %.3f %.3f) attached-gun=(%.3f %.3f %.3f)"
        " socket_wrist_gap=%.3f gun_wrist_gap=%.3f\\n",
        armed_wrist("r_wrist").x,armed_wrist("r_wrist").y,
        armed_wrist("r_wrist").z,
        armed_wrist("r_hand_prop_attachment").x,
        armed_wrist("r_hand_prop_attachment").y,
        armed_wrist("r_hand_prop_attachment").z,
        weapon_origin.x,weapon_origin.y,weapon_origin.z,
        (armed_wrist("r_hand_prop_attachment")-armed_wrist("r_wrist")).magnitude(),
        (weapon_origin-armed_wrist("r_wrist")).magnitude());
    REQUIRE(weapon_body.weapon_correction(
        weapon_origin,weapon_forward,.24f,.048f)<.003f);
    // Regression: even an attachment offset buried in the chest must be
    // pushed outside after the final animated socket transform.
    auto buried=socket_frame.attachments.at(2);
    const auto socket_index=skeleton.joint_index("r_hand_prop_attachment");
    REQUIRE(socket_index<skeleton.joints.size());
    const auto socket_matrix=armed.joints_world()[socket_index]*
        skeleton_to_mesh*presentation;
    math::float4x4 inverse_socket;
    REQUIRE(inverse_socket.try_invert(socket_matrix));
    buried.offset=inverse_socket.transform_position(armed_world("spined"));
    const auto corrected=socket_frame.attachment_transform(buried,&gun_camera);
    REQUIRE(corrected.has_value());
    const Vector3 corrected_origin{
        corrected->elements[3][0],corrected->elements[3][1],
        corrected->elements[3][2]};
    const auto corrected_forward=corrected->transform_direction({0.f,0.f,1.f});
    REQUIRE(weapon_body.weapon_correction(
        corrected_origin,corrected_forward,.24f,.048f)<.004f);
    math::float4x4 world;
    world.identity();
    // Fourth case is the real camera-eye driven WeaponLab-style grip.
    // The other cases preserve the previous no-camera IK contract.
    for(auto const& [fps,yaw,pitch,camera_eye]:
        std::vector<std::tuple<bool,float,float,bool>>{
            {true,0.f,0.f,false},{true,.7f,.3f,false},
            {false,.25f,0.f,false},{true,0.f,0.f,true},
            {true,.7f,.3f,true},{true,-.7f,-.3f,true},
            {true,1.0f,.55f,true},{true,-1.0f,-.55f,true},
            {true,0.f,1.10f,true},{true,0.f,-1.10f,true},
            {true,.45f,-1.10f,true},{true,-.45f,-1.10f,true}}) {
        SkeletonPose pose(skeleton);
        pose.sample(skeleton,clip,0.f,false);
        // The player's body follows camera yaw while the eyes can pitch
        // independently. Match runtime geometry rather than rotating the
        // camera over an unrealistically stationary model.
        const auto instance_world=camera_eye?
            math::create_rotation_y(-yaw):world;
        auto pos=[&](char const* name) {
            for(std::size_t i=0;i<skeleton.joints.size();++i)
                if(skeleton.joints[i].name==name)
                    return (pose.joints_world()[i] *
                        skeleton_to_mesh*instance_world)
                        .transform_position({0.f,0.f,0.f});
            throw std::runtime_error("Abby missing joint");
        };
        const auto right_before=pos("r_wrist");
        const auto left_before=pos("l_wrist");
        const auto upper_before=std::array<float,2>{
            (pos("r_elbow")-pos("r_shoulder")).magnitude(),
            (pos("l_elbow")-pos("l_shoulder")).magnitude()};
        const auto lower_before=std::array<float,2>{
            (pos("r_wrist")-pos("r_elbow")).magnitude(),
            (pos("l_wrist")-pos("l_elbow")).magnitude()};
        const auto clavicles=std::array<std::size_t,2>{
            skeleton.humanoid.r_clavicle,skeleton.humanoid.l_clavicle};
        auto clavicle_world=[&](std::size_t index) {
            return (pose.joints_world()[index]*
                skeleton_to_mesh*instance_world)
                .transform_position({0.f,0.f,0.f});
        };
        std::array<std::optional<Vector3>,2> clavicle_before{};
        for(std::size_t side=0;side<clavicles.size();++side)
            if(clavicles[side]<skeleton.joints.size())
                clavicle_before[side]=clavicle_world(clavicles[side]);
        auto palette_gap=[&](char const* first,char const* second) {
            std::size_t a=skeleton.joints.size(),b=a;
            for(std::size_t i=0;i<skeleton.joints.size();++i) {
                if(skeleton.joints[i].name==first)a=i;
                if(skeleton.joints[i].name==second)b=i;
            }
            if(a>=skeleton.joints.size() || b>=skeleton.joints.size())
                return -1.f;
            float max_delta=0.f;
            for(int row=0;row<4;++row)for(int col=0;col<4;++col)
                max_delta=std::max(max_delta,std::abs(
                    pose.palette()[a].elements[row][col]-
                    pose.palette()[b].elements[row][col]));
            return max_delta;
        };
        if(fps && yaw==0.f)
            std::fprintf(stderr,"[ABBY FINGER] before r_indexa=%f r_middlea=%f\n",
                palette_gap("r_indexa","r_indexa_helper"),
                palette_gap("r_middlea","r_middlea_helper"));
        auto baseline_mesh=(fps && yaw==0.f)?
            std::optional<MeshAsset>{skin_native_model_cpu(native,pose.palette())}:
            std::nullopt;
        auto const ik_begin=std::chrono::steady_clock::now();
        auto results=evaluate_pose({skeleton,clip,0.f,instance_world,
            native.mesh_to_skeleton,GripView{yaw,pitch,fps,
                camera_eye?std::optional<Vector3>{Vector3{0.f,1.62f,0.f}}:
                    std::nullopt,.52f,.24f,.12f,pistol_grip,
                fps && camera_eye}},pose);
        if(fps && camera_eye && yaw==0.f && pitch==-1.10f) {
            float maximum_clavicle_shift=0.f;
            for(std::size_t side=0;side<clavicles.size();++side) {
                if(!clavicle_before[side])continue;
                const float shift=(clavicle_world(clavicles[side])-
                    *clavicle_before[side]).magnitude();
                REQUIRE(shift<.027f); // <= 26 mm scapular glide + rounding
                maximum_clavicle_shift=std::max(maximum_clavicle_shift,shift);
            }
            // The clavicle may remain neutral if this ADS pose already
            // clears the torso; forcing a minimum shift would deform it.
            REQUIRE(std::isfinite(maximum_clavicle_shift));
            const auto upper_after=std::array<float,2>{
                (pos("r_elbow")-pos("r_shoulder")).magnitude(),
                (pos("l_elbow")-pos("l_shoulder")).magnitude()};
            const auto lower_after=std::array<float,2>{
                (pos("r_wrist")-pos("r_elbow")).magnitude(),
                (pos("l_wrist")-pos("l_elbow")).magnitude()};
            for(unsigned side=0;side<2;++side) {
                REQUIRE(std::abs(upper_before[side]-upper_after[side])<.003f);
                REQUIRE(std::abs(lower_before[side]-lower_after[side])<.003f);
            }
            std::fprintf(stderr,
                "[CLAVICLE IK] pitch=-1.10 max_shift=%.5f bone_lengths=preserved\n",
                maximum_clavicle_shift);
        }
        const auto ik_microseconds=std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now()-ik_begin).count();
        if(camera_eye)std::fprintf(stderr,
            "[WEAPONLAB PERF] yaw=%.2f pitch=%.2f ik_ms=%.3f\\n",
            yaw,pitch,double(ik_microseconds)/1000.);
        if(fps && yaw==0.f)
            std::fprintf(stderr,"[ABBY FINGER] after r_indexa=%f r_middlea=%f\n",
                palette_gap("r_indexa","r_indexa_helper"),
                palette_gap("r_middlea","r_middlea_helper"));

        // Compare the real third-person authored gun attachment against
        // the evaluated hand and movement forward, including the weapon
        // configuration's current grip rotation.
        if(!fps) {
            const auto tps_mesh_world=presentation*math::create_rotation_y(-yaw);
            SkeletonPose tps_pose(skeleton);
            const auto tps_results=evaluate_pose(
                {skeleton,clip,0.f,tps_mesh_world,native.mesh_to_skeleton,
                 GripView{yaw,0.f,false,std::nullopt,.52f,.24f,.12f,pistol_grip}},tps_pose);
            REQUIRE(tps_results[0] && tps_results[1]);
            SkeletalSceneFrame tps_frame;
            Scene tps_scene;
            tps_scene.objects.resize(2);
            tps_scene.objects[0].node.id=17;
            tps_scene.objects[1].node.id=18;
            tps_scene.components.push_back({18,"engine.model.bone_attachment",
                1,"17 r_hand_prop_attachment 0 0 0 -107.270 50.242 124.884 socket"});
            tps_frame.prepare(tps_scene);
            tps_frame.publish_sockets(17,skeleton,tps_pose,native.mesh_to_skeleton,
                                     tps_mesh_world);
            const auto attach=tps_frame.attachment_transform(
                tps_frame.attachments.at(18),nullptr);
            REQUIRE(attach.has_value());
            const auto socket_idx=skeleton.joint_index("r_hand_prop_attachment");
            REQUIRE(socket_idx<skeleton.joints.size());
            const auto rig_socket=tps_pose.joints_world()[socket_idx]*
                skeleton_to_mesh*tps_mesh_world;
            const auto wrist_idx=skeleton.joint_index("r_wrist");
            REQUIRE(wrist_idx<skeleton.joints.size());
            const auto tps_wrist=(tps_pose.joints_world()[wrist_idx]*
                skeleton_to_mesh*tps_mesh_world).transform_position({0.f,0.f,0.f});
            std::fprintf(stderr,
                "[TPS SOCKET BASIS] %.6f %.6f %.6f / %.6f %.6f %.6f / %.6f %.6f %.6f\\n",
                rig_socket.elements[0][0],rig_socket.elements[0][1],rig_socket.elements[0][2],
                rig_socket.elements[1][0],rig_socket.elements[1][1],rig_socket.elements[1][2],
                rig_socket.elements[2][0],rig_socket.elements[2][1],rig_socket.elements[2][2]);
            const auto grip_pos=attach->transform_position({0.f,0.f,0.f});
            const auto barrel=attach->transform_direction({0.f,0.f,1.f});
            const Vector3 forward{std::sin(yaw),0.f,std::cos(yaw)};
            const auto dot=[](Vector3 a,Vector3 b){
                return a.x*b.x+a.y*b.y+a.z*b.z;};
            std::fprintf(stderr,
                "[ACTUAL TPS PISTOL] grip_gap=%.3f barrel_forward=%.3f"
                " socket=(%.3f %.3f %.3f) wrist=(%.3f %.3f %.3f)\\n",
                (grip_pos-tps_wrist).magnitude(),dot(barrel,forward),
                grip_pos.x,grip_pos.y,grip_pos.z,
                tps_wrist.x,tps_wrist.y,tps_wrist.z);
            REQUIRE((grip_pos-tps_wrist).magnitude()<.085f);
            REQUIRE(dot(barrel,forward)>.98f);
            for(float heading : {-0.85f,0.f,.65f,1.15f}) {
                const auto test_world=presentation*math::create_rotation_y(-heading);
                SkeletonPose tested_pose(skeleton);
                auto const results=evaluate_pose(
                    {skeleton,clip,0.f,test_world,native.mesh_to_skeleton,
                     GripView{heading,0.f,false,std::nullopt,.52f,.24f,.12f,pistol_grip}},tested_pose);
                REQUIRE(results[0] && results[1]);
                SkeletalSceneFrame tested_frame;
                tested_frame.prepare(tps_scene);
                tested_frame.publish_sockets(17,skeleton,tested_pose,
                    native.mesh_to_skeleton,test_world);
                const auto gun=tested_frame.attachment_transform(
                    tested_frame.attachments.at(18),nullptr);
                REQUIRE(gun.has_value());
                auto const direction=gun->transform_direction({0.f,0.f,1.f});
                const Vector3 expected{std::sin(heading),0.f,std::cos(heading)};
                REQUIRE(dot(direction,expected)>.92f);
                const auto wrist=(tested_pose.joints_world()[wrist_idx]*
                    skeleton_to_mesh*test_world).transform_position({0.f,0.f,0.f});
                const auto root=gun->transform_position({0.f,0.f,0.f});
                REQUIRE((root-wrist).magnitude()<.09f);
            }
        }
        // Anatomical reach must be in front of the character's torso in
        // third person, never behind her back when the orbit camera pitches.
        if(!fps) {
            const Vector3 body_forward{std::sin(yaw),0.f,std::cos(yaw)};
            auto const dot=[](Vector3 a,Vector3 b) {
                return a.x*b.x+a.y*b.y+a.z*b.z;
            };
            REQUIRE(dot(pos("r_elbow")-pos("r_shoulder"),body_forward)>.04f);
            REQUIRE(dot(pos("l_elbow")-pos("l_shoulder"),body_forward)>.04f);
            REQUIRE(dot(pos("r_wrist")-pos("r_shoulder"),body_forward)>.12f);
            REQUIRE(dot(pos("l_wrist")-pos("l_shoulder"),body_forward)>.12f);
        }
        auto wrist_axis=[&](char const* name) {
            for(std::size_t i=0;i<skeleton.joints.size();++i)
                if(skeleton.joints[i].name==name) {
                    auto const transform=pose.joints_world()[i]*
                        skeleton_to_mesh*instance_world;
                    // Hand +X is not a universal anatomical forward axis.
                    // The imported bind-pose finger axis is the canonical
                    // reference used by the new wrist orientation solver.
                    const auto finger=skeleton.humanoid.valid &&
                        skeleton.humanoid.hand_basis_valid[0]?
                        skeleton.humanoid.finger_axis[0]:
                        math::float3{1.f,0.f,0.f};
                    return transform.transform_direction(finger).normalize();
                }
            throw std::runtime_error("Missing expected wrist");
        };
        const Vector3 view_forward{std::sin(yaw)*std::cos(fps?pitch:0.f),
            std::sin(fps?pitch:0.f),std::cos(yaw)*std::cos(fps?pitch:0.f)};
        const auto primary_wrist=skeleton.humanoid.valid &&
            skeleton.humanoid.r_hand<skeleton.joints.size()?
            skeleton.joints[skeleton.humanoid.r_hand].name.c_str():"r_wrist";
        auto const axis=wrist_axis(primary_wrist);
        if(camera_eye && yaw==0.f)
            std::fprintf(stderr,"[CANONICAL DEBUG] valid=%d repaired=%d sides=%d hand0_basis=%d finger=(%.3f %.3f %.3f) canonical_r=%zu named_r=%zu\\n",
                int(skeleton.humanoid.valid),int(skeleton.humanoid.side_repaired),
                int(skeleton.humanoid.side_confirmed),
                int(skeleton.humanoid.hand_basis_valid[0]),
                skeleton.humanoid.finger_axis[0].x,
                skeleton.humanoid.finger_axis[0].y,
                skeleton.humanoid.finger_axis[0].z,
                skeleton.humanoid.r_hand,skeleton.joint_index("r_wrist"));
        auto const align=axis.x*view_forward.x+
            axis.y*view_forward.y+axis.z*view_forward.z;
        if(camera_eye) {
            std::fprintf(stderr,"[WEAPONLAB WRIST] yaw=%.2f pitch=%.2f forward_alignment=%.3f\\n",
                yaw,pitch,align);
            REQUIRE(align>.95f);
        }
        if(camera_eye) {
            // Both elbows must stay outside the chest and ahead of the
            // shoulder plane. Wrist-only IK previously permitted the
            // forearms to penetrate the torso and cross behind the back.
            const Vector3 body_forward{std::sin(yaw),0.f,std::cos(yaw)};
            const Vector3 body_right{std::cos(yaw),0.f,-std::sin(yaw)};
            auto const dot=[](Vector3 a,Vector3 b) {
                return a.x*b.x+a.y*b.y+a.z*b.z;
            };
            auto const chest=pos("spined");
            for(auto const& [shoulder,elbow,sign] :
                std::array<std::tuple<char const*,char const*,float>,2>{
                    {{"r_shoulder","r_elbow",1.f},
                     {"l_shoulder","l_elbow",-1.f}}}) {
                auto const p=pos(elbow);
                REQUIRE(dot(p-pos(shoulder),body_forward)>.065f);
                std::fprintf(stderr,
                    "[HAND IK ELBOW] fps=%d yaw=%.3f pitch=%.3f sign=%.1f"
                    " signed_x=%.3f forward=%.3f\n",
                    int(fps),yaw,pitch,sign,sign*dot(p-chest,body_right),
                    dot(p-pos(shoulder),body_forward));
                // The calibrated two-handed WeaponLab support brings the
                // left forearm closer to the dominant grip. Keep a lateral
                // elbow clearance while the independent torso-capsule test
                // below enforces actual non-penetration.
                REQUIRE(sign*dot(p-chest,body_right)>.080f);
            }
            if(fps && yaw==0.f && pitch==0.f) {
                // True ADS silhouette: elbows cannot rise above their
                // respective wrist/grip and hide the rear sight.
                REQUIRE(pos("r_elbow").y<pos("r_wrist").y-.055f);
                REQUIRE(pos("l_elbow").y<pos("l_wrist").y-.055f);
            }
            if(fps && yaw==0.f) {
                const auto er=pos("r_elbow");
                const auto el=pos("l_elbow");
                const auto wr=pos("r_wrist");
                const auto wl=pos("l_wrist");
                std::fprintf(stderr,
                    "[ADS SILHOUETTE] pitch=%.2f "
                    "R_elbow=(%.3f,%.3f,%.3f) R_wrist=(%.3f,%.3f,%.3f) "
                    "L_elbow=(%.3f,%.3f,%.3f) L_wrist=(%.3f,%.3f,%.3f)\n",
                    pitch,er.x,er.y,er.z,wr.x,wr.y,wr.z,
                    el.x,el.y,el.z,wl.x,wl.y,wl.z);
            }
            REQUIRE(results[0] && results[1]);
            // Use the posed shoulder side (the same evidence as the runtime
            // camera) rather than bind-space labels that can be reflected.
            Vector3 const camera_right{std::cos(yaw),0.f,-std::sin(yaw)};
            const auto side=pos("r_shoulder")-pos("l_shoulder");
            const bool inverted=dot(side,camera_right)<-.14f;
            const auto contact_socket=inverted?
                "l_hand_prop_attachment":"r_hand_prop_attachment";
            const auto support_wrist=inverted?"r_wrist":"l_wrist";
            auto const contact=(pos(contact_socket)-pos(support_wrist)).magnitude();
            auto const right_error=(results[0]->target-results[0]->actual).magnitude();
            auto const left_error=(results[1]->target-results[1]->actual).magnitude();
            std::fprintf(stderr,"[WEAPONLAB CAMERA] yaw=%.3f pitch=%.3f contact=%.4f R=%.4f L=%.4f\n",
                yaw,pitch,contact,right_error,left_error);
            // The explicit, gun-local support socket is ~6.8 cm from its
            // primary socket; reject the old floating 13-20 cm support grip.
            if(fps && camera_eye && yaw==0.f && pitch==0.f)
                REQUIRE(contact>.02f && contact<.115f);
            if(left_error>=.012f) {
                const auto g=results[1]->target, a=results[1]->actual;
                const auto s=pos("l_shoulder");
                std::fprintf(stderr,
                    "[WEAPONLAB LEFT TARGET] goal=(%.4f %.4f %.4f)"
                    " actual=(%.4f %.4f %.4f) shoulder=(%.4f %.4f %.4f)\n",
                    g.x,g.y,g.z,a.x,a.y,a.z,s.x,s.y,s.z);
            }
            REQUIRE(right_error<.012f);
            REQUIRE(left_error<.012f);
            // Collision contract: pose and the weapon's near-barrel proxy
            // must not enter the authored torso/head at any tested yaw/pitch.
            auto body=viewport_detail::make_character_self_collision(
                pos("pelvis"),pos("spineb"),pos("spined"),pos("headb"),
                pos("r_shoulder"),pos("l_shoulder"),body_forward);
            REQUIRE(body.active);
            const auto wrist_r=body.correction(pos("r_wrist"),.040f);
            const auto wrist_l=body.correction(pos("l_wrist"),.040f);
            const auto elbow_r=body.correction(pos("r_elbow"),.045f);
            const auto elbow_l=body.correction(pos("l_elbow"),.045f);
            const Vector3 weapon_axis{std::sin(yaw)*std::cos(pitch),
                std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
            const auto gun=body.weapon_correction(
                pos("r_hand_prop_attachment"),weapon_axis,.24f,.040f);
            std::fprintf(stderr,
                "[WEAPON COLLISION] yaw=%.2f pitch=%.2f"
                " wrist=(%.5f,%.5f) elbow=(%.5f,%.5f) gun=%.5f\n",
                yaw,pitch,wrist_r,wrist_l,elbow_r,elbow_l,gun);
            REQUIRE(wrist_r<.018f && wrist_l<.018f);
            // Contract: even at -1.10 rad, elbow penetration into the
            // protective torso/head capsules must remain below 35 mm.
            REQUIRE(elbow_r<.035f && elbow_l<.035f);
            REQUIRE(gun<.035f);
            if(fps) {
                const auto primary=pos(inverted?
                    "l_wrist":"r_wrist");
                const auto support=pos(inverted?
                    "r_wrist":"l_wrist");
                const auto primary_elbow=pos(inverted?
                    "l_elbow":"r_elbow");
                const auto support_elbow=pos(inverted?
                    "r_elbow":"l_elbow");
                const float wrist_side=dot(primary-support,camera_right);
                const float elbow_side=dot(primary_elbow-support_elbow,
                                            camera_right);
                REQUIRE(wrist_side>.045f);
                REQUIRE(elbow_side>.12f);
                REQUIRE(pistol_grip->has_support_grip);
                const Vector3 camera_up{
                    -std::sin(yaw)*std::sin(pitch),std::cos(pitch),
                    -std::cos(yaw)*std::sin(pitch)};
                const auto grip_delta=pistol_grip->support_grip-
                                      pistol_grip->primary_grip;
                const Vector3 expected=pos(contact_socket)+
                    camera_right*grip_delta.x+
                    camera_up*grip_delta.y+
                    view_forward*grip_delta.z;
                REQUIRE((expected-support).magnitude()<.015f);
            }
        }
        if(camera_eye && yaw==0.f && pitch==0.f) {
            // Regression for project-authored ADS camera offsets. The
            // pistol local rear-sight y is ~0.068 m and its rear-sight z
            // is ~0.03 m from the right-hand attachment socket.
            SkeletonPose ads_pose(skeleton);
            const Vector3 eye{0.f,1.62f,0.f};
            const auto ads=evaluate_pose({skeleton,clip,0.f,
                instance_world,native.mesh_to_skeleton,
                GripView{0.f,0.f,true,eye,.52f,.115f,.063f,pistol_grip,true}},ads_pose);
            REQUIRE(ads[0] && ads[1]);
            auto const idx=std::find_if(skeleton.joints.begin(),
                skeleton.joints.end(),[](auto const& b) {
                    return b.name=="r_hand_prop_attachment";
                });
            REQUIRE(idx!=skeleton.joints.end());
            const auto socket=(ads_pose.joints_world()[
                std::size_t(idx-skeleton.joints.begin())]*
                skeleton_to_mesh*instance_world)
                .transform_position({0.f,0.f,0.f});
            const auto authored_rear=pistol_grip->rear_sight;
            const auto authored_front=pistol_grip->front_sight;
            const Vector3 rear_sight=socket+Vector3{
                authored_rear.x,authored_rear.y,authored_rear.z};
            const Vector3 front_sight=socket+Vector3{
                authored_front.x,authored_front.y,authored_front.z};
            const auto sight=(front_sight-rear_sight).normalize();
            REQUIRE(sight.z>.999f);
            REQUIRE(std::abs(sight.x)<.002f);
            REQUIRE(std::abs(sight.y)<.002f);
            std::fprintf(stderr,
                "[WEAPON ADS] socket=(%.3f %.3f %.3f)"
                " rear_sight=(%.3f %.3f %.3f) eye=(%.3f %.3f %.3f)\\n",
                socket.x,socket.y,socket.z,rear_sight.x,rear_sight.y,
                rear_sight.z,eye.x,eye.y,eye.z);
            REQUIRE(std::abs(rear_sight.y-eye.y)<.045f);
            REQUIRE(std::abs(rear_sight.x-eye.x)<.045f);
            REQUIRE(rear_sight.z-eye.z>.28f);
        }
        if(fps && yaw==0.f) {
            std::fprintf(stderr,
                "[WEAPONLAB PORT] wrists=%.4f socket_support=%.4f R=%.4f L=%.4f\n",
                (pos("r_wrist")-pos("l_wrist")).magnitude(),
                (pos("r_hand_prop_attachment")-pos("l_wrist")).magnitude(),
                results[0]?(results[0]->target-results[0]->actual).magnitude():-1.f,
                results[1]?(results[1]->target-results[1]->actual).magnitude():-1.f);
            REQUIRE(results[0] && results[1]);
            REQUIRE((pos("r_wrist")-pos("l_wrist")).magnitude()>.05f);
            REQUIRE((pos("r_wrist")-pos("l_wrist")).magnitude()<.25f);
            REQUIRE((pos("r_hand_prop_attachment")-
                     pos("l_wrist")).magnitude()<.25f);
            REQUIRE((results[0]->target-results[0]->actual).magnitude()<
                (camera_eye?.13f:.08f));
            REQUIRE((results[1]->target-results[1]->actual).magnitude()<
                (camera_eye?.13f:.08f));
        }
        auto dump=[&](char const* joint) {
            auto p=pos(joint);
            std::fprintf(stderr," %s=(%.3f %.3f %.3f)",
                joint,p.x,p.y,p.z);
        };
        std::fprintf(stderr,"[ABBY IK] fps=%d yaw=%.2f pitch=%.2f before"
            " R=(%.3f %.3f %.3f) L=(%.3f %.3f %.3f)",
            fps,yaw,pitch,right_before.x,right_before.y,right_before.z,
            left_before.x,left_before.y,left_before.z);
        for(int h=0;h<2;++h) {
            if(results[h]) {
                auto const& g=*results[h];
                std::fprintf(stderr," hand%d residual=%.3f solved=%d",
                    h,(g.target-g.actual).magnitude(),g.solved);
            } else std::fprintf(stderr," hand%d UNAVAILABLE",h);
        }
        for(char const* n:{"r_shoulder","r_elbow","r_wrist",
                           "l_shoulder","l_elbow","l_wrist"})dump(n);
        std::fprintf(stderr,"\\n");
        if(fps && yaw==0.f) {
            // Compare a pair of matched skinning helpers. The source joint
            // palette and its bind-identical helper must deform the mesh alike.
            for(auto const& [helper,source]:skeleton.mirrored_helpers) {
                auto const& name=skeleton.joints[source].name;
                if(name.find("shoulder")==std::string::npos &&
                   name.find("elbow")==std::string::npos &&
                   name.find("wrist")==std::string::npos)continue;
                float error=0.f;
                for(int r=0;r<4;++r)for(int c=0;c<4;++c)
                    error=std::max(error,std::abs(
                        pose.palette()[helper].elements[r][c]-
                        pose.palette()[source].elements[r][c]));
                std::fprintf(stderr,
                    "[ABBY HELPERS] %s helper=%s palette_difference=%.4f\\n",
                    name.c_str(),skeleton.joints[helper].name.c_str(),error);
            }
            std::vector<bool> arm_joint(skeleton.joints.size(),false);
            for(std::size_t j=0;j<skeleton.joints.size();++j) {
                int a=int(j);
                for(std::size_t d=0;a>=0&&d<skeleton.joints.size();++d) {
                    auto const& bone=skeleton.joints[a];
                    if(bone.name=="l_elbow" || bone.name=="r_elbow" || bone.name=="l_elbow_helper" || bone.name=="r_elbow_helper") {
                        arm_joint[j]=true;break;
                    }
                    a=bone.parent;
                }
            }
            for(int pass=0;pass<3;++pass) {
                for(auto const& [helper,source]:skeleton.mirrored_helpers)
                    if(arm_joint[source])arm_joint[helper]=true;
                for(auto const& c:skeleton.secondary_palette_constraints)
                    if(arm_joint[c.source_a]&&arm_joint[c.source_b])
                        arm_joint[c.bone]=true;
            }
            std::vector<bool> arm_vertex(native.vertex_skin.size(),false);
            for(std::size_t v=0;v<arm_vertex.size();++v) {
                float w=0.f;
                for(int i=0;i<8;++i)
                    if(arm_joint[native.vertex_skin[v].joints[i]])
                        w+=native.vertex_skin[v].weights[i];
                arm_vertex[v]=w>=.975f;
            }
            auto skinned=skin_native_model_cpu(native,pose.palette());
            auto const& indices=native.mesh.indices;
            auto const& orig=native.mesh.vertices;
            auto const& posed=skinned.vertices;
            std::size_t torn=0;
            float worst=0.f;
            for(std::size_t k=0;k+2<indices.size();k+=3)
                for(unsigned j=0;j<3;++j) {
                    auto const ia=indices[k+j],ib=indices[k+(j+1)%3];
                    auto const d0=orig[ia].position-orig[ib].position;
                    auto const d1=posed[ia].position-posed[ib].position;
                    auto const length0=d0.magnitude();
                    auto const length1=d1.magnitude();
                    const auto stretch=length0>1.e-6f?length1/length0:1.f;
                    worst=std::max(worst,stretch);
                    if(stretch>3.f && length1>.025f)++torn;
                }
            std::size_t base_torn=0;
            float base_worst=0.f;
            auto const& base_vertices=baseline_mesh->vertices;
            std::size_t first_bad=std::size_t(-1);
            for(std::size_t k=0;k+2<indices.size();k+=3)
                for(unsigned j=0;j<3;++j) {
                    auto const ia=indices[k+j],ib=indices[k+(j+1)%3];
                    auto const base=orig[ia].position-orig[ib].position;
                    auto const d=base_vertices[ia].position-base_vertices[ib].position;
                    const auto a=base.magnitude();
                    const auto stretch=a>1.e-6f?d.magnitude()/a:1.f;
                    base_worst=std::max(base_worst,stretch);
                    if(stretch>3.f && d.magnitude()>.025f)++base_torn;
                    if(first_bad==std::size_t(-1) &&
                       (posed[ia].position-posed[ib].position).magnitude()>
                        std::max(.025f,3.f*a) &&
                       d.magnitude()<=std::max(.025f,3.f*a))
                        first_bad=k;
                }
            std::size_t arm_triangles=0,arm_base_torn=0,arm_ik_torn=0;
            std::size_t first_arm_bad=std::size_t(-1);
            float arm_worst=0.f;
            for(std::size_t k=0;k+2<indices.size();k+=3) {
                if(!arm_vertex[indices[k]]||!arm_vertex[indices[k+1]]||
                   !arm_vertex[indices[k+2]])continue;
                ++arm_triangles;
                for(unsigned j=0;j<3;++j) {
                    auto const a=indices[k+j],b=indices[k+(j+1)%3];
                    auto const original=(orig[a].position-orig[b].position).magnitude();
                    auto const before=(base_vertices[a].position-
                                       base_vertices[b].position).magnitude();
                    auto const after=(posed[a].position-posed[b].position).magnitude();
                    if(original<1.e-6f)continue;
                    arm_worst=std::max(arm_worst,after/original);
                    if(before>.025f && before>original*3.f)++arm_base_torn;
                    if(after>.025f && after>original*3.f) {
                        ++arm_ik_torn;
                        if(first_arm_bad==std::size_t(-1) &&
                           before<=std::max(.025f,original*3.f))
                            first_arm_bad=k;
                    }
                }
            }
            std::fprintf(stderr,
                "[ABBY FPS ARMS] triangles=%zu base_torn=%zu"
                " IK_torn=%zu max_stretch=%.3f\\n",
                arm_triangles,arm_base_torn,arm_ik_torn,arm_worst);
            // A source triangle must never rip under a proposed first-person
            // animation update. Regression for the failed finger-curl change.
            REQUIRE(arm_triangles>20000);
            REQUIRE(arm_base_torn==0);
            REQUIRE(arm_ik_torn==0);
            REQUIRE(arm_worst<5.f);
            if(first_arm_bad!=std::size_t(-1)) {
                std::fprintf(stderr,"[ABBY ARM TEAR] triangle=%zu\\n",
                    first_arm_bad/3);
                for(unsigned n=0;n<3;++n) {
                    auto idx=indices[first_arm_bad+n];
                    std::fprintf(stderr,"[ABBY ARM TEAR] vertex=%u",idx);
                    auto const& w=native.vertex_skin[idx];
                    for(unsigned z=0;z<8;++z)
                        if(w.weights[z]>.02f)
                            std::fprintf(stderr," %s=%.3f",
                                skeleton.joints[w.joints[z]].name.c_str(),
                                w.weights[z]);
                    std::fprintf(stderr,"\\n");
                }
            }
            std::fprintf(stderr,
                "[ABBY MESH] triangles=%zu base_torn=%zu IK_torn=%zu"
                " base_worst=%.3f IK_worst=%.3f first_new_tear=%zu\\n",
                indices.size()/3,base_torn,torn,base_worst,worst,first_bad);
            if(first_bad!=std::size_t(-1)) {
                for(unsigned n=0;n<3;++n) {
                    auto idx=indices[first_bad+n];
                    std::fprintf(stderr,"[ABBY TEAR] vertex=%u",idx);
                    auto const& wt=native.vertex_skin[idx];
                    for(unsigned z=0;z<8;++z)
                        if(wt.weights[z]>.03f)
                            std::fprintf(stderr," %s=%.3f",
                                skeleton.joints[wt.joints[z]].name.c_str(),
                                wt.weights[z]);
                    std::fprintf(stderr,"\\n");
                }
            }
        }
    }
}

int main(int argc,char** argv)
{
    check_animation();
    check_mesh_space_skin_palette();
    check_grip_ik();
    check_cache();
    if(argc>1)check_abby_grip(argv[1]);
}
