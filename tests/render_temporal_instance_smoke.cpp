#include "../engine/northstarcpp/modules/render/shared/temporal_instance_motion.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
using namespace northstarcpp::render;
static std::array<float,16> matrix(float x=0.f) {
    std::array<float,16> result{};
    result[0]=result[5]=result[10]=result[15]=1.f;
    result[12]=x;
    return result;
}
static NorthStarRenderTemporalInstance object(
    std::uint64_t id,const std::array<float,16>& pose,
    const std::vector<float>* palette=nullptr,std::uint64_t revision=7) {
    NorthStarRenderTemporalInstance result{};
    result.instance_id=id;
    for(std::size_t i=0;i<16;++i)result.world_transform[i]=pose[i];
    if(palette) {
        result.bone_palette=palette->data();
        result.joint_count=static_cast<std::uint32_t>(palette->size()/16u);
    }
    result.topology_revision=revision;
    return result;
}
int main() {
    TemporalInstanceHistory ids;
    auto m0=matrix(0.f),m1=matrix(1.f),m2=matrix(2.f);
    auto a=object(11,m0),b=object(22,m1);
    std::array<NorthStarRenderTemporalInstance,2> first{a,b};
    ids.stage(first.data(),2);
    assert(!ids.lookup(11,true).valid);
    ids.commit();
    assert(ids.tracked_count()==2);

    // Reordering the submission MUST NOT change previous pose association.
    std::array<NorthStarRenderTemporalInstance,2> reorder{
        object(22,m2),object(11,m1)};
    ids.stage(reorder.data(),2);
    auto stable=ids.lookup(11,true);
    assert(stable.valid && stable.previous && stable.current);
    assert(stable.previous->transform[12]==0.f);
    assert(stable.current->transform[12]==1.f);
    TemporalCamera c{};c.width=1280;c.height=720;
    TemporalHistoryTracker cameras;
    cameras.begin(c);
    auto frame=cameras.begin(c);
    std::array<float,2> pixels{};
    assert(temporal_instance_pixel_velocity(
        stable,frame,{0.f,0.f,10.f},TemporalJitter{},pixels));
    assert(pixels[0]<-40.f);

    // A failed setter must not disturb already validated pending state.
    auto duplicate=reorder;duplicate[0].instance_id=11;
    bool rejected=false;
    try{ids.stage(duplicate.data(),2);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    assert(ids.lookup(11,true).valid);
    ids.commit();
    assert(ids.lookup(11,true).current==nullptr);

    // Retain a previous pose only if rig revision matches.
    auto changed=object(11,m2,nullptr,8);
    ids.stage(&changed,1);
    assert(!ids.lookup(11,true).valid);
    ids.commit();

    // Camera cuts and teleports explicitly reject reprojection.
    auto after=object(11,m1,nullptr,8);
    ids.stage(&after,1);
    assert(!ids.lookup(11,false).valid);
    ids.commit();
    after.flags=NORTHSTARCPP_TEMPORAL_INSTANCE_TELEPORT;
    ids.stage(&after,1);
    assert(!ids.lookup(11,true).valid);
    ids.commit();

    // Animated instance: previous palette must follow the stable entity ID.
    std::vector<float> joints0(m0.begin(),m0.end());
    std::vector<float> joints1(m1.begin(),m1.end());
    auto skinned0=object(333,m0,&joints0);
    ids.stage(&skinned0,1);
    ids.commit();
    auto skinned1=object(333,m0,&joints1);
    ids.stage(&skinned1,1);
    auto pair=ids.lookup(333,true);
    assert(pair.valid && pair.previous->joints==1u);
    std::array<std::uint16_t,8> indexes{};
    std::array<float,8> weights{};weights[0]=1.f;
    assert(temporal_instance_skin_pixel_velocity(
        pair,frame,{0.f,0.f,10.f},indexes,weights,TemporalJitter{},pixels));
    assert(pixels[0]<-40.f);
    ids.commit();

    auto bad=skinned1;
    bad.joint_count=257;
    rejected=false;
    try{ids.stage(&bad,1);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    bad=skinned1;
    bad.instance_id=0;
    rejected=false;
    try{ids.stage(&bad,1);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    bad=skinned1;bad.bone_palette=nullptr;
    rejected=false;
    try{ids.stage(&bad,1);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);

    // No implicit persistence when the application does not resubmit an ID.
    ids.commit();assert(ids.tracked_count()==0);
    return 0;
}
