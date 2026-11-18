#include "viewport_skeletal_animation.hpp"
#include <northstarcpp/procedural_ik.hpp>
#include <northstarcpp/grounded_locomotion.hpp>
#include <northstarcpp/posture_ik.hpp>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

namespace northstarcpp::viewport_detail {
namespace {
template<class T> std::vector<std::string_view> const* values_for(
    std::unordered_map<std::uint64_t,std::vector<T>> const& data,
    std::uint64_t id) {
    auto it=data.find(id);
    return it==data.end()?nullptr:&it->second;
}
bool same_definitions(std::vector<std::string> const& cached,
    std::vector<std::string_view> const* input) {
    if(!input)return cached.empty();
    if(input->size()!=cached.size())return false;
    for(std::size_t i=0;i<cached.size();++i)
        if(cached[i]!=(*input)[i])return false;
    return true;
}
bool parse_chain(std::string_view source,IkChainDefinition& result) {
    std::istringstream stream{std::string(source)};
    std::string rest;
    if(!(stream>>result.name>>result.root>>result.hinge>>
         result.effector) || stream>>rest)return false;
    return true;
}
bool parse_goal(std::string_view source,std::string& chain,
    IkTargetRequest& goal) {
    std::istringstream stream{std::string(source)};
    std::string extra;
    if(!(stream>>chain>>
         goal.position.x>>goal.position.y>>goal.position.z>>
         goal.pole.x>>goal.pole.y>>goal.pole.z>>
         goal.weight>>goal.priority) || stream>>extra)return false;
    return true;
}
bool parse_ground(std::string_view source,std::string& chain,
    IkGroundContact& contact,IkTargetRequest& goal) {
    std::istringstream stream{std::string(source)};
    std::string extra;
    contact.hit=true;
    if(!(stream>>chain>>
         contact.point.x>>contact.point.y>>contact.point.z>>
         contact.normal.x>>contact.normal.y>>contact.normal.z>>
         contact.foot_clearance>>contact.max_raise>>contact.max_drop>>
         goal.weight>>goal.priority) || stream>>extra)return false;
    return true;
}
}
void apply_procedural_ik(SkeletalSceneFrame const& frame,std::uint64_t entity,
    Skeleton const& rig,SkeletonPose& pose,
    std::unique_ptr<ProceduralIkController>& controller,
    std::vector<std::string>& cache,float delta_seconds,
    PhysicsScene const* physics,math::float4x4 const* rig_to_world,
    std::unique_ptr<GroundedLocomotionController>* grounded,
    std::vector<std::string>* foot_cache,std::string* body_cache,
    std::size_t ignored_body) {
    auto const definitions=values_for(frame.ik_chains,entity);
    if(!same_definitions(cache,definitions)) {
        cache.clear();
        controller.reset();
        if(definitions && !definitions->empty()) {
            auto candidate=std::make_unique<ProceduralIkController>(rig);
            for(auto const& source:*definitions) {
                cache.emplace_back(source);
                IkChainDefinition definition;
                if(!parse_chain(source,definition) ||
                   !candidate->register_chain(std::move(definition))) {
                    std::fprintf(stderr,
                        "[WARN] [engine.ik] entity=%llu invalid IK chain definition\n",
                        static_cast<unsigned long long>(entity));
                }
            }
            if(candidate->chain_count())controller=std::move(candidate);
        }
    }
    if(!controller) {
        if(grounded)grounded->reset();
        if(foot_cache)foot_cache->clear();
        if(body_cache)body_cache->clear();
        return;
    }
    if(auto goals=values_for(frame.ik_targets,entity))
        for(auto const& payload:*goals) {
            std::string name;
            IkTargetRequest request;
            if(parse_goal(payload,name,request))
                controller->submit(name,request);
        }
    if(auto contacts=values_for(frame.ik_contacts,entity))
        for(auto const& payload:*contacts) {
            std::string name;
            IkTargetRequest request;
            IkGroundContact contact;
            if(parse_ground(payload,name,contact,request))
                controller->submit_ground(name,contact,request,pose);
        }
    // Recreate only when the rig-authored foot or body configuration changes.
    // Repeated frames retain foot anchors and outstanding probe tickets.
    if(grounded && foot_cache && body_cache && rig_to_world) {
        auto defs=values_for(frame.ground_feet,entity);
        auto body=frame.ground_bodies.find(entity);
        auto const body_text=body==frame.ground_bodies.end()?
            std::string_view{}:body->second;
        if(!same_definitions(*foot_cache,defs) ||*body_cache!=body_text ||
           (!*grounded &&defs &&!defs->empty())) {
            grounded->reset();
            foot_cache->clear();
            *body_cache=std::string(body_text);
            if(defs && !defs->empty()) {
                std::vector<GroundedFootDefinition> parsed;
                parsed.reserve(defs->size());
                bool all_valid=true;
                for(auto const text:*defs) {
                    foot_cache->emplace_back(text);
                    GroundedFootDefinition f;
                    std::istringstream in{std::string(text)};
                    std::string remainder;
                    if(!(in>>f.chain>>f.ankle>>
                        f.heel_offset.x>>f.heel_offset.y>>f.heel_offset.z>>
                        f.toe_offset.x>>f.toe_offset.y>>f.toe_offset.z)) {
                        all_valid=false;
                        break;
                    }
                    if(in>>f.toe_bone) {
                        if(in>>remainder) {all_valid=false;break;}
                    }
                    parsed.push_back(std::move(f));
                }
                GroundedBodyDefinition rig_body;
                if(!body_text.empty()) {
                    std::istringstream in{std::string(body_text)};
                    std::string remainder;
                    if(!(in>>rig_body.pelvis>>rig_body.torso) ||
                       (in>>remainder))all_valid=false;
                    if(rig_body.pelvis=="-")rig_body.pelvis.clear();
                    if(rig_body.torso=="-")rig_body.torso.clear();
                }
                if(all_valid &&controller) {
                    try {
                        *grounded=std::make_unique<GroundedLocomotionController>(
                            rig,std::move(parsed),std::move(rig_body));
                    } catch(std::exception const&) {
                        all_valid=false;
                    }
                }
                if(!all_valid)
                    std::fprintf(stderr,
                        "[WARN] [engine.ik] invalid grounded foot/body for entity %llu\n",
                        static_cast<unsigned long long>(entity));
            }
        }
        if(*grounded &&controller)
            (*grounded)->update(physics,ignored_body,pose,*controller,
                *rig_to_world,delta_seconds);
    }
    // The live VehicleService::seat() snapshot is supplied by gameplay
    // as an entity-owned component. Reject mismatched occupants so a nearby
    // vehicle cannot pull hands away from an unrelated character.
    if(rig_to_world) {
        auto seat_it=frame.vehicle_seat_poses.find(entity);
        auto reaches=values_for(frame.vehicle_reaches,entity);
        if(seat_it!=frame.vehicle_seat_poses.end() &&reaches) {
            NorthStarVehicleSeatState seat{};
            seat.size=sizeof(seat);
            std::string extra;
            std::istringstream in{std::string(seat_it->second)};
            if((in>>seat.world_position[0]>>seat.world_position[1]>>
                seat.world_position[2]>>seat.world_yaw>>seat.occupant) &&
               !(in>>extra) &&seat.occupant==entity) {
                math::float4x4 world_to_rig;
                if(world_to_rig.try_invert(*rig_to_world)) {
                    std::vector<VehicleSeatReach> goals;
                    goals.reserve(reaches->size());
                    for(auto const& text:*reaches) {
                        VehicleSeatReach request;
                        std::istringstream source{std::string(text)};
                        if((source>>request.chain>>request.knee_or_elbow>>
                            request.seat_local_target.x>>
                            request.seat_local_target.y>>
                            request.seat_local_target.z>>
                            request.weight>>request.priority) &&
                           !(source>>extra))
                            goals.push_back(std::move(request));
                    }
                    VehicleSeatIk::submit(rig,pose,*controller,
                        world_to_rig,seat,goals);
                }
            }
        }
    }
    // Total active constraints are bounded by 16 chains and 8 solves/frame;
    // optional requests fade out when the caller stops submitting them.
    controller->evaluate(pose,delta_seconds,8);
    if(grounded && *grounded)
        (*grounded)->finish_toes(pose);
}
} // namespace northstarcpp::viewport_detail
