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
        return rig.joint_index(name);
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

bool SkeletonPose::orient_wrist(Skeleton const& rig,char const* wrist_name,
    math::float4x4 const& desired_global,float max_angle_radians)
{
    using namespace math;
    if(!std::isfinite(max_angle_radians) || max_angle_radians<=0.f)
        return false;
    auto const joint=rig.joint_index(wrist_name);
    if(joint>=rig.joints.size())return false;
    if(joint>=local_.size())return false;
    float4x4 target=desired_global;
    auto const parent=rig.joints[joint].parent;
    if(parent>=0) {
        float4x4 inverse;
        if(!inverse.try_invert(global_[std::size_t(parent)]))
            return false;
        target=desired_global*inverse;
    }
    // Strip translation and normalize parent scale before quaternion
    // extraction; imported YDD bind transforms may carry non-unit scale.
    for(unsigned row=0;row<3;++row) {
        float length=0.f;
        for(unsigned col=0;col<3;++col)
            length+=target.elements[row][col]*target.elements[row][col];
        if(length<1e-8f || !std::isfinite(length))return false;
        length=std::sqrt(length);
        for(unsigned col=0;col<3;++col)
            target.elements[row][col]/=length;
    }
    for(unsigned col=0;col<3;++col) {
        target.elements[3][col]=0.f;
        target.elements[col][3]=0.f;
    }
    target.elements[3][3]=1.f;
    auto desired=quaternion(target).normalize();
    auto const current=local_[joint].rotation;
    if(!desired.valid() || !current.valid())return false;
    float dot=current.x*desired.x+current.y*desired.y+
              current.z*desired.z+current.w*desired.w;
    dot=std::clamp(std::abs(dot),0.f,1.f);
    const float angle=2.f*std::acos(dot);
    if(angle<1e-4f)return true;
    const float amount=std::min(1.f,std::min(max_angle_radians,2.2f)/angle);
    auto const selected=slerp(current,desired,amount).normalize();
    local_[joint].rotation=selected;
    // These helpers are importer-verified bind-identical copies. Only
    // matched wrist helpers can follow the wrist rotation, not arbitrary
    // finger-roll or twist branches (which previously tore the skin).
    for(auto const& [helper,source]:rig.mirrored_helpers)
        if(source==joint && helper<local_.size())
            local_[helper].rotation=selected;
    evaluate(rig);
    for(auto const& rule:rig.secondary_palette_constraints) {
        if(rule.bone>=palette_.size() || rule.source_a>=palette_.size() ||
           rule.source_b>=palette_.size())continue;
        auto const& a=palette_[rule.source_a];
        auto const& b=palette_[rule.source_b];
        auto& out=palette_[rule.bone];
        for(int row=0;row<4;++row)
            for(int col=0;col<4;++col)
                out.elements[row][col]=a.elements[row][col]*
                    (1.f-rule.source_b_weight)+
                    b.elements[row][col]*rule.source_b_weight;
    }
    return true;
}

}
