#include "viewport_skeletal_animation.hpp"
#include <northstarcpp/posture_ik.hpp>
#include <cstdio>
#include <sstream>
#include <utility>

namespace northstarcpp::viewport_detail {
void apply_distributed_lookat(SkeletalSceneFrame const& frame,
    std::uint64_t entity,Skeleton const& rig,SkeletonPose& pose,
    math::float4x4 const& rig_to_world,float dt,
    std::unique_ptr<DistributedLookAt>& controller,
    std::vector<std::string>& cache) {
    auto it=frame.look_joints.find(entity);
    auto const& definitions=it==frame.look_joints.end()?
        std::vector<std::string_view>{}:it->second;
    bool changed=definitions.size()!=cache.size();
    if(!changed)
        for(std::size_t i=0;i<cache.size();++i)
            if(cache[i]!=definitions[i]){changed=true;break;}
    if(changed) {
        controller.reset();
        cache.clear();
        std::vector<LookAtJoint> nodes;
        nodes.reserve(definitions.size());
        bool valid=true;
        for(auto const input:definitions) {
            cache.emplace_back(input);
            std::istringstream parser{std::string(input)};
            std::string extra;
            LookAtJoint node;
            if(!(parser>>node.bone>>node.influence>>node.max_radians>>
                 node.forward.x>>node.forward.y>>node.forward.z) ||
               (parser>>extra)) {
                valid=false;break;
            }
            nodes.push_back(std::move(node));
        }
        if(valid&&!nodes.empty()) {
            try {
                controller=std::make_unique<DistributedLookAt>(
                    rig,std::move(nodes));
            } catch(std::exception const&) {
                valid=false;
            }
        }
        if(!valid)
            std::fprintf(stderr,
                "[WARN] [engine.ik] invalid look-at rig entity %llu\n",
                static_cast<unsigned long long>(entity));
    }
    if(!controller)return;
    auto target=frame.look_targets.find(entity);
    if(target!=frame.look_targets.end()) {
        std::istringstream parser{std::string(target->second)};
        std::string extra;
        math::float3 world;
        float weight;
        if((parser>>world.x>>world.y>>world.z>>weight) &&
            !(parser>>extra) &&controller->set_target(world,weight)) {
            // Evaluate exactly once below, including fade-in.
        }
        else controller->disable();
    } else controller->disable();
    // Keep the controller running while its influence fades back to the
    // underlying clip; never retain a stale joint rotation after target loss.
    controller->apply(pose,rig_to_world,dt);
}
}
