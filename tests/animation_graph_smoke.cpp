#include <northstarcpp/animation_graph.hpp>
#include <northstarcpp/assets.hpp>
#include <cmath>
#include <chrono>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace northstarcpp;
namespace {
void need(bool valid,char const* message) {
    if(!valid)throw std::runtime_error(message);
}
JointTransform joint(float y) {
    JointTransform t;
    t.rotation.identity();
    t.position={0.f,y,0.f};
    return t;
}
std::shared_ptr<SkeletalClip const> clip(std::string name,float spine,float hand) {
    SkeletalClip c;c.name=std::move(name);
    c.frames=3;c.sample_rate=10.f;c.loop=true;
    c.joints={1,2};
    for(int frame=0;frame<3;++frame) {
        c.samples.push_back(joint(spine));
        c.samples.push_back(joint(hand));
    }
    return std::make_shared<SkeletalClip const>(std::move(c));
}
Skeleton fake_rig() {
    Skeleton rig;
    for(auto const& name:{"root","spine","hand"}) {
        SkeletonJoint joint;
        joint.name=name;joint.parent=static_cast<int>(rig.joints.size())-1;
        joint.bind.rotation.identity();
        rig.joints.push_back(joint);
        math::float4x4 identity;identity.identity();
        rig.inverse_bind.push_back(identity);
    }
    return rig;
}
float translation(SkeletonPose const& pose,int index) {
    return pose.joints_world()[index].elements[3][1];
}
}
int main(int argc,char** argv) {
    try {
        constexpr char xml[]=R"XML(
<AnimationGraph schema="1" initial="locomotion">
 <Parameters>
  <Float name="speed" default="0"/>
  <Float name="attack" default="0"/>
  <Float name="aim" default="0"/>
 </Parameters>
 <States>
  <State name="locomotion" sync="gait">
   <Blend1D parameter="speed">
    <Point at="0" ref="animations/idle.ycd@idle"/>
    <Point at="1" ref="animations/run.ycd@run"/>
   </Blend1D>
  </State>
  <State name="attack">
   <Clip ref="animations/attack.ycd@attack"/>
  </State>
 </States>
 <Transitions>
  <Transition from="locomotion" to="attack" parameter="attack" op="ge"
              value="0.5" fade="0.1"/>
  <Transition from="attack" to="locomotion" parameter="attack" op="le"
              value="0.1" fade="0.1"/>
 </Transitions>
 <Layers>
  <Layer name="hand_aim" ref="animations/aim.ycd@aim" roots="hand"
         weight="1" weight_parameter="aim" mode="additive"/>
 </Layers>
 <Events>
  <Notify name="right_footstep" ref="animations/run.ycd@run" at="0.5"/>
  <Notify name="ik.foot.plant:left" ref="animations/run.ycd@run" at="0.5"/>
 </Events>
</AnimationGraph>)XML";
        auto graph=parse_animation_graph_xml(xml);
        need(graph.references().size()==4,"Clip refs were not deduplicated");
        auto rig=fake_rig();
        auto pose=SkeletonPose(rig);
        AnimationGraphInstance instance(rig,graph);
        instance.bind_clip("animations/idle.ycd@idle",clip("idle",0.f,0.f));
        instance.bind_clip("animations/run.ycd@run",clip("run",2.f,0.f));
        instance.bind_clip("animations/attack.ycd@attack",clip("attack",3.f,0.f));
        instance.bind_clip("animations/aim.ycd@aim",clip("aim",0.f,1.f));
        need(instance.ready(),"Graph clips are not resident");
        AnimationGraphInstance stopped(rig,graph);
        stopped.bind_clip("animations/idle.ycd@idle",clip("idle",0.f,0.f));
        stopped.bind_clip("animations/run.ycd@run",clip("run",2.f,0.f));
        stopped.bind_clip("animations/attack.ycd@attack",clip("attack",3.f,0.f));
        stopped.bind_clip("animations/aim.ycd@aim",clip("aim",0.f,1.f));
        stopped.advance(.1f);
        need(stopped.events().empty(),
             "Inactive Blend1D point must not publish notifies");
        stopped.set_parameter("speed",1.f);
        stopped.advance(.2f);
        need(!stopped.events().empty(),
             "Active Blend1D point must publish loop notifies");
        instance.set_parameter("speed",.5f);
        instance.advance(.1f);
        instance.evaluate(pose);
        need(std::abs(translation(pose,1)-1.f)<.001f,"Blend1D pose is wrong");
        need(instance.events().size()==2 &&
             instance.events()[0].name=="right_footstep" &&
             instance.events()[1].name=="ik.foot.plant:left",
             "Synchronized event did not fire on crossing");
        instance.advance(0.f);
        need(instance.events().empty(),"Duplicate zero-delta notify");
        instance.set_parameter("aim",1.f);
        instance.evaluate(pose);
        need(std::abs(translation(pose,1)-1.f)<.001f &&
             std::abs(translation(pose,2)-2.f)<.001f,
             "Masked additive hand layer changed unmasked spine");
        instance.set_parameter("attack",1.f);
        instance.advance(.05f);
        instance.evaluate(pose);
        need(instance.state()=="attack" &&
             translation(pose,1)>1.f && translation(pose,1)<3.f,
             "State transition crossfade failed");
        instance.advance(.1f);
        instance.evaluate(pose);
        need(std::abs(translation(pose,1)-3.f)<.001f,
             "Attack state did not complete transition");
        instance.set_parameter("attack",0.f);
        instance.advance(.1f);
        instance.advance(.1f);
        need(instance.state()=="locomotion","Reverse transition failed");
        // Graph transitions must preserve the torso when entering a sparse
        // animation (common in landing/crouch and additive actions).
        {
            constexpr char sparse_xml[]=R"XML(
<AnimationGraph schema="1" initial="whole">
 <Parameters><Float name="switch" default="0"/></Parameters>
 <States>
  <State name="whole"><Clip ref="animations/full.ycd@full"/></State>
  <State name="partial"><Clip ref="animations/partial.ycd@partial"/></State>
 </States>
 <Transitions>
  <Transition from="whole" to="partial" parameter="switch"
              op="ge" value="0.5" fade="0.1"/>
 </Transitions>
</AnimationGraph>)XML";
            auto sparse=AnimationGraphInstance(
                rig,parse_animation_graph_xml(sparse_xml));
            sparse.bind_clip("animations/full.ycd@full",clip("full",2.f,3.f));
            SkeletalClip partial;partial.name="partial";
            partial.frames=3;partial.sample_rate=10.f;partial.loop=true;
            partial.joints={2};
            for(int k=0;k<3;++k)partial.samples.push_back(joint(8.f));
            sparse.bind_clip("animations/partial.ycd@partial",
                std::make_shared<SkeletalClip const>(std::move(partial)));
            SkeletonPose body(rig);
            sparse.advance(.01f);sparse.evaluate(body);
            need(std::abs(body.local_transforms()[1].position.y-2.f)<.001f,
                 "Initial graph body pose invalid");
            sparse.set_parameter("switch",1.f);
            sparse.advance(.05f);sparse.evaluate(body);
            need(std::abs(body.local_transforms()[1].position.y-2.f)<.001f,
                 "Animation Graph partial transition flashed bind spine");
            need(body.local_transforms()[2].position.y>3.f &&
                 body.local_transforms()[2].position.y<8.f,
                 "Animation Graph partial transition lost hand crossfade");
            sparse.advance(.1f);sparse.evaluate(body);
            sparse.advance(.1f);sparse.evaluate(body);
            need(std::abs(body.local_transforms()[1].position.y-2.f)<.001f &&
                 std::abs(body.local_transforms()[2].position.y-8.f)<.001f,
                 "Sparse graph state reset an unkeyed joint after transition");
        }
        bool rejected=false;
        try {instance.set_parameter("nonexistent",1.f);}
        catch(std::exception const&) {rejected=true;}
        need(rejected,"Unknown parameter accepted");
        rejected=false;
        try {parse_animation_graph_xml("<AnimationGraph schema='2' initial='bad'/>");}
        catch(std::exception const&) {rejected=true;}
        need(rejected,"Incorrect XML schema was accepted");
        // Sparse YCD clips (landing, turn, fall) often leave bones unkeyed.
        // Crossfade must preserve their previous animation instead of YMT
        // bind pose, including at the first frame after a clip switch.
        {
            auto skeleton=fake_rig();
            SkeletonPose switching(skeleton);
            SkeletalClip idle{};
            idle.name="running"; idle.frames=2;idle.sample_rate=30.f;
            idle.joints={0,1,2};
            for(unsigned frame=0;frame<2;++frame) {
                idle.samples.push_back(joint(1.f));
                idle.samples.push_back(joint(2.f));
                idle.samples.push_back(joint(3.f));
            }
            SkeletalClip turn{};
            turn.name="turn";turn.frames=2;turn.sample_rate=30.f;
            turn.joints={2}; // only hand is keyed
            turn.samples={joint(7.f),joint(7.f)};
            switching.sample(skeleton,idle,0.f,false);
            auto const outgoing=switching.local_transforms();
            switching.sample(skeleton,turn,0.f,false);
            switching.preserve_unkeyed_and_blend(skeleton,outgoing,outgoing,0.f);
            need(std::abs(switching.local_transforms()[0].position.y-1.f)<.001f &&
                 std::abs(switching.local_transforms()[1].position.y-2.f)<.001f &&
                 std::abs(switching.local_transforms()[2].position.y-3.f)<.001f,
                 "Animation switch flashed bind pose at first frame");
            switching.sample(skeleton,turn,0.f,false);
            switching.preserve_unkeyed_and_blend(skeleton,outgoing,outgoing,.5f);
            need(std::abs(switching.local_transforms()[0].position.y-1.f)<.001f &&
                 std::abs(switching.local_transforms()[1].position.y-2.f)<.001f &&
                 std::abs(switching.local_transforms()[2].position.y-5.f)<.001f,
                 "Sparse YCD blend fell through bind pose");
            switching.sample(skeleton,turn,0.f,false);
            switching.preserve_unkeyed_and_blend(skeleton,outgoing,outgoing,1.f);
            need(std::abs(switching.local_transforms()[1].position.y-2.f)<.001f &&
                 std::abs(switching.local_transforms()[2].position.y-7.f)<.001f,
                 "Completed sparse YCD transition reset an unkeyed bone");
            bool invalid=false;
            try {
                switching.preserve_unkeyed_and_blend(
                    skeleton,outgoing,outgoing,-.1f);
            }catch(std::invalid_argument const&) {invalid=true;}
            need(invalid,"Invalid crossfade alpha was accepted");
        }
        if(argc>1) {
            AssetStore store;
            store.mount_shared_assets(argv[1]);
            auto xml_asset=load_animation_graph(
                store,"animations/abby_locomotion.nagraph.xml");
            need(xml_asset.states.size()==1 &&
                 xml_asset.references().size()==2,
                 "NSP/shared XML Animation Graph failed to load");
            auto skeleton=std::make_shared<Skeleton const>(load_skeleton(
                store,"characters/abby/models/abby.ymt"));
            AsyncYcdClipLoader loader(store,skeleton,1);
            constexpr auto ref="characters/abby/animations/mm-explore.ycd@abby-mm-explore-idle";
            loader.request(ref);
            for(unsigned attempt=0;attempt<600 && loader.pending();++attempt) {
                loader.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            need(loader.pending()==0,"Asynchronous YCD loader did not complete");
            need(loader.error(ref).empty(),"Async YCD decoder returned an error");
            need(bool(loader.find(ref)),"Real YCD clip not resident after async load");
            need(loader.find(ref)->frames>1,"Decoded YCD clip invalid");
            loader.request(ref); // duplicate request is idempotent
            // Real shipped Abby YCD transitions: loading a clip name is
            // different from merely spotting an ASCII name inside a pack.
            constexpr std::array transitions{
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-walk-loop-fw",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-run-loop-fw",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-sprint-loop-fw",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-walk-loop-bkwd-180",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-run-loop-bkwd-180",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-walk-loop-fw-90l",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-walk-loop-fw-90r",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-run-loop-fw-90l",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-run-loop-fw-90r",
                "characters/abby/animations/mm-explore.ycd@abby-mm-soft-landing^explore-idle",
                "characters/abby/animations/mm-explore.ycd@abby-mm-med-landing^explore-idle",
                "characters/abby/animations/mm-explore.ycd@abby-mm-hard-landing^explore-idle",
                "characters/abby/animations/fall-medium.ycd@abby-fall-medium",
                "characters/abby/animations/mm-explore.ycd@abby-mm-explore-crouch-idle^explore-idle"
            };
            for(auto const* entry:transitions)loader.request(entry);
            for(unsigned attempt=0;attempt<1600 && loader.pending();++attempt) {
                loader.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            need(loader.pending()==0,"Abby YCD transition assets timed out");
            for(auto const* entry:transitions) {
                auto const found=loader.find(entry);
                if(!found ||found->joints.empty()) {
                    std::cerr<<"Abby YCD unavailable "<<entry<<": "
                             <<loader.error(entry)<<"\n";
                    throw std::runtime_error("Animation transition references a missing/empty clip");
                }
                std::cout<<"Abby YCD "<<entry<<" tracks="<<found->joints.size()
                         <<" frames="<<found->frames<<"\n";
            }
        }
        std::cout<<"Animation Graph XML/Blend1D/layers/sync/events/async YCD PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"Animation Graph FAILED: "<<e.what()<<"\n";return 1;
    }
}
