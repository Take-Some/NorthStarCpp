#pragma once
// Provider-neutral per-rendered-frame temporal instance and skeleton history.
// Previous pose never depends on material order or host pointer ownership.
#include <northstarcpp/render_temporal_instances_api.h>
#include "temporal_history.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace northstarcpp::render {
struct TemporalInstancePose {
    std::array<float,16> transform{};
    std::vector<float> palette;
    std::uint64_t topology_revision=0;
    bool cut=false;
    std::uint32_t joints=0;
};
class TemporalInstanceHistory {
public:
    // Atomic batch validation/copy, including duplicate IDs and finite data.
    void stage(const NorthStarRenderTemporalInstance* instances,
               std::uint32_t count) {
        if(count>250000u || (count&&!instances))
            throw std::invalid_argument("Invalid temporal instance count/pointer");
        std::unordered_map<std::uint64_t,TemporalInstancePose> next;
        next.reserve(count);
        std::uint64_t palette_floats=0;
        for(std::uint32_t index=0;index<count;++index) {
            const auto& item=instances[index];
            if(item.instance_id==0 || item.flags&~NORTHSTARCPP_TEMPORAL_INSTANCE_TELEPORT ||
               item.joint_count>NORTHSTARCPP_TEMPORAL_INSTANCE_MAX_JOINTS ||
               (item.joint_count!=0 && !item.bone_palette))
                throw std::invalid_argument("Invalid temporal instance metadata");
            TemporalInstancePose pose;
            pose.joints=item.joint_count;
            pose.cut=(item.flags&NORTHSTARCPP_TEMPORAL_INSTANCE_TELEPORT)!=0;
            pose.topology_revision=item.topology_revision;
            for(std::uint32_t j=0;j<16;++j) {
                const float value=item.world_transform[j];
                if(!std::isfinite(value)||std::abs(value)>1.e8f)
                    throw std::invalid_argument("Invalid temporal world transform");
                pose.transform[j]=value;
            }
            const std::uint64_t floats=std::uint64_t(item.joint_count)*16u;
            if(palette_floats+floats>16u*1024u*1024u)
                throw std::invalid_argument("Temporal palette memory limit exceeded");
            palette_floats+=floats;
            if(floats) {
                for(std::uint64_t j=0;j<floats;++j) {
                    const float value=item.bone_palette[j];
                    if(!std::isfinite(value)||std::abs(value)>1.e8f)
                        throw std::invalid_argument("Invalid temporal bone palette");
                }
                pose.palette.assign(item.bone_palette,item.bone_palette+floats);
            }
            if(!next.emplace(item.instance_id,std::move(pose)).second)
                throw std::invalid_argument("Duplicate temporal instance ID");
        }
        staged_=std::move(next);
        has_staged_=true;
    }

    struct Pair {
        const TemporalInstancePose* current=nullptr;
        const TemporalInstancePose* previous=nullptr;
        bool valid=false;
    };
    Pair lookup(std::uint64_t id,bool camera_history_valid) const noexcept {
        if(!has_staged_ || id==0)return {};
        auto now=staged_.find(id);
        if(now==staged_.end())return {};
        Pair result{&now->second,nullptr,false};
        if(!camera_history_valid||now->second.cut)return result;
        auto before=committed_.find(id);
        if(before==committed_.end())return result;
        const auto& past=before->second;
        if(past.topology_revision!=now->second.topology_revision ||
           past.joints!=now->second.joints || past.cut)
            return result;
        result.previous=&past;
        result.valid=true;
        return result;
    }
    // Advance history after GPU submission was successful. If the caller
    // skips stage(), no stale implicit resubmission may survive.
    void commit() noexcept {
        if(has_staged_)committed_.swap(staged_);
        else committed_.clear();
        staged_.clear();
        has_staged_=false;
    }
    void invalidate() noexcept {committed_.clear();}
    std::size_t tracked_count() const noexcept{return committed_.size();}
private:
    std::unordered_map<std::uint64_t,TemporalInstancePose> staged_,committed_;
    bool has_staged_=false;
};
} // namespace northstarcpp::render
