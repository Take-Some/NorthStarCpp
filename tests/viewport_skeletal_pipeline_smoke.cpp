#include "../engine/northstarcpp/modules/viewport/viewport_skeletal_animation.hpp"
#include "../engine/northstarcpp/modules/viewport/viewport_skin_cache.hpp"
#include <chrono>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/native_model.hpp>
#include <cstdio>
#include <chrono>
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
    auto const& skeleton=*native.skeleton;
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
    math::float4x4 world;
    world.identity();
    // Fourth case is the real camera-eye driven WeaponLab-style grip.
    // The other cases preserve the previous no-camera IK contract.
    for(auto const& [fps,yaw,pitch,camera_eye]:
        std::vector<std::tuple<bool,float,float,bool>>{
            {true,0.f,0.f,false},{true,.7f,.3f,false},
            {false,.25f,0.f,false},{true,0.f,0.f,true},
            {true,.7f,.3f,true},{true,-.7f,-.3f,true},
            {true,1.0f,.55f,true},{true,-1.0f,-.55f,true}}) {
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
                        math::create_scale({-1.f,1.f,-1.f})*instance_world)
                        .transform_position({0.f,0.f,0.f});
            throw std::runtime_error("Abby missing joint");
        };
        const auto right_before=pos("r_wrist");
        const auto left_before=pos("l_wrist");
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
                    std::nullopt}},pose);
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

        if(camera_eye) {
            REQUIRE(results[0] && results[1]);
            auto const contact=(pos("r_hand_prop_attachment")-pos("l_wrist")).magnitude();
            auto const right_error=(results[0]->target-results[0]->actual).magnitude();
            auto const left_error=(results[1]->target-results[1]->actual).magnitude();
            std::fprintf(stderr,"[WEAPONLAB CAMERA] yaw=%.3f pitch=%.3f contact=%.4f R=%.4f L=%.4f\n",
                yaw,pitch,contact,right_error,left_error);
            REQUIRE(contact<.16f);
            REQUIRE(right_error<.012f);
            REQUIRE(left_error<.012f);
        }
        if(fps && yaw==0.f) {
            if(camera_eye)std::fprintf(stderr,
                "[WEAPONLAB PORT] wrists=%.4f socket_support=%.4f R=%.4f L=%.4f\n",
                (pos("r_wrist")-pos("l_wrist")).magnitude(),
                (pos("r_hand_prop_attachment")-pos("l_wrist")).magnitude(),
                results[0]?(results[0]->target-results[0]->actual).magnitude():-1.f,
                results[1]?(results[1]->target-results[1]->actual).magnitude():-1.f);
            REQUIRE(results[0] && results[1]);
            REQUIRE((pos("r_wrist")-pos("l_wrist")).magnitude()<
                (camera_eye?.16f:.085f));
            REQUIRE((pos("r_hand_prop_attachment")-
                     pos("l_wrist")).magnitude()<.10f);
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
    check_grip_ik();
    check_cache();
    if(argc>1)check_abby_grip(argv[1]);
}
