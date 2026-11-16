#include <northstarcpp/skeleton.hpp>
#include <algorithm>
#include <cmath>
namespace northstarcpp {
bool SkeletonPose::pose_two_bone(Skeleton const& rig,
    char const* shoulder_name,char const* elbow_name,char const* wrist_name,
    math::float3 target,math::float3 pole,float shoulder_limit,float elbow_limit)
{
    using namespace math;
    if(!target.valid()||!pole.valid()||shoulder_limit<=0.f||elbow_limit<=0.f||
       !std::isfinite(shoulder_limit)||!std::isfinite(elbow_limit))return false;
    auto index=[&](char const* name)->std::size_t {
        auto it=std::find_if(rig.joints.begin(),rig.joints.end(),
            [&](auto const& joint){return joint.name==name;});
        return std::size_t(it-rig.joints.begin());
    };
    auto const shoulder=index(shoulder_name),elbow=index(elbow_name),
               wrist=index(wrist_name);
    if(shoulder>=local_.size()||elbow>=local_.size()||wrist>=local_.size())
        return false;
    auto is_descendant=[&](std::size_t child,std::size_t parent) {
        for(std::size_t n=0;n<local_.size();++n) {
            if(child==parent)return true;
            int p=rig.joints[child].parent;
            if(p<0)return false;
            child=std::size_t(p);
        }
        return false;
    };
    if(!is_descendant(elbow,shoulder)||!is_descendant(wrist,elbow))
        return false;
    auto pos=[&](std::size_t j)->float3 {
        auto const& m=global_[j];
        return {m.elements[3][0],m.elements[3][1],m.elements[3][2]};
    };
    auto const start=pos(shoulder);
    float const upper=(pos(elbow)-start).magnitude();
    float const lower=(pos(wrist)-pos(elbow)).magnitude();
    if(upper<.01f||lower<.01f)return false;
    auto dir=target-start;
    float distance=dir.magnitude();
    if(distance<.005f)return false;
    dir=dir/distance;
    float const min_reach=std::abs(upper-lower)+.008f;
    float const max_reach=upper+lower-.008f;
    if(min_reach>=max_reach)return false;
    distance=std::clamp(distance,min_reach,max_reach);
    target=start+dir*distance;
    auto bend=pole-start-dir*dot_product(pole-start,dir);
    if(bend.magnitude()<.0001f) {
        auto const idle=pos(elbow)-start;
        bend=idle-dir*dot_product(idle,dir);
    }
    if(bend.magnitude()<.0001f)return false;
    bend=bend.normalize();
    float const along=(upper*upper-lower*lower+distance*distance)/(2.f*distance);
    float const side=std::sqrt(std::max(0.f,upper*upper-along*along));
    auto const elbow_target=start+dir*along+bend*side;
    auto orient=[&](std::size_t joint,std::size_t child,float3 goal,
                    float max_angle) {
        auto const pivot=pos(joint);
        auto const current=pos(child)-pivot,desired=goal-pivot;
        if(current.magnitude()<.0001f||desired.magnitude()<.0001f)return false;
        auto axis=cross_product(desired,current);
        auto const magnitude=axis.magnitude();
        if(magnitude<.00001f)return false;
        float const angle=std::min(std::atan2(magnitude,
                                        dot_product(current,desired)),max_angle);
        if(angle<.00005f)return false;
        axis=axis/magnitude;
        int parent=rig.joints[joint].parent;
        if(parent>=0) {
            float4x4 inverse;
            if(!inverse.try_invert(global_[std::size_t(parent)]))return false;
            axis=inverse.transform_direction(axis);
            if(axis.magnitude()<.0001f)return false;
            axis=axis.normalize();
        }
        quaternion delta(axis,angle),original=local_[joint].rotation;
        float best=(pos(child)-goal).magnitude(),initial=best;
        quaternion chosen=original;
        // Respect the actual row-vector FK convention for imported rigs.
        for(unsigned order=0;order<2;++order) {
            local_[joint].rotation=(order==0?
                original*delta:delta*original).normalize();
            evaluate(rig);
            auto const error=(pos(child)-goal).magnitude();
            if(error+.00001f<best){best=error;chosen=local_[joint].rotation;}
        }
        local_[joint].rotation=chosen;
        for(auto const& [helper,source]:rig.mirrored_helpers)
            if(source==joint)local_[helper].rotation=chosen;
        evaluate(rig);
        return best+.00001f<initial;
    };
    bool const raised=orient(shoulder,elbow,elbow_target,
                            std::min(1.9f,shoulder_limit));
    bool const bent=orient(elbow,wrist,target,std::min(2.f,elbow_limit));
    for(auto const& rule:rig.secondary_palette_constraints) {
        auto const& a=palette_[rule.source_a],&b=palette_[rule.source_b];
        auto& out=palette_[rule.bone];
        for(int row=0;row<4;++row)for(int col=0;col<4;++col)
            out.elements[row][col]=
                a.elements[row][col]*(1.f-rule.source_b_weight)+
                b.elements[row][col]*rule.source_b_weight;
    }
    return raised||bent;
}

}
