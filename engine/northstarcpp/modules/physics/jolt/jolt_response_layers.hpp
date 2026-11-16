// Thread-safe Jolt collision-profile registry supporting 16 channels.
// ObjectLayer is a compact profile ID, not a packed 16x2-bit response mask.
#pragma once
#include <northstarcpp/physics_plugin.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>
#include <Jolt/Physics/Body/Body.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace northstarcpp::jolt_collision {
namespace j=JPH;
inline constexpr std::uint16_t dynamic_bit=0x8000u;
inline constexpr std::uint16_t index_mask=0x7fffu;
inline constexpr j::ObjectLayer static_default=0;
inline constexpr j::ObjectLayer dynamic_default=dynamic_bit|1u;
inline bool dynamic(j::ObjectLayer layer) {
    return (layer&dynamic_bit)!=0;
}
struct Profile {
    std::uint8_t type=0;
    std::array<std::uint8_t,16> responses{};
    Profile(){responses.fill(2);}
    bool operator==(Profile const& b) const {
        return type==b.type&&responses==b.responses;
    }
};
class Registry {
    mutable std::shared_mutex lock_;
    std::vector<Profile> profiles_;
public:
    Registry() {
        Profile world,physics;
        physics.type=1;
        profiles_.push_back(world);
        profiles_.push_back(physics);
    }
    Profile get(j::ObjectLayer layer) const {
        std::shared_lock lock(lock_);
        auto idx=std::uint32_t(layer)&index_mask;
        return idx<profiles_.size()?profiles_[idx]:profiles_[0];
    }
    j::ObjectLayer assign(bool is_dynamic,std::uint32_t channel,
                          const std::uint8_t* responses,std::uint32_t count) {
        if(channel>=16||count!=16||!responses)
            throw std::invalid_argument("Invalid 16-channel collision profile");
        Profile next;
        next.type=static_cast<std::uint8_t>(channel);
        for(int i=0;i<16;++i) {
            if(responses[i]>2)
                throw std::invalid_argument("Invalid channel response");
            next.responses[i]=responses[i];
        }
        std::unique_lock lock(lock_);
        for(std::uint32_t i=0;i<profiles_.size();++i)
            if(profiles_[i]==next)
                return static_cast<j::ObjectLayer>(
                    i|(is_dynamic?dynamic_bit:0u));
        if(profiles_.size()>=index_mask)
            throw std::runtime_error("Too many unique physics collision profiles");
        auto idx=static_cast<std::uint32_t>(profiles_.size());
        profiles_.push_back(next);
        return static_cast<j::ObjectLayer>(
            idx|(is_dynamic?dynamic_bit:0u));
    }
    j::ObjectLayer with_type(j::ObjectLayer layer,std::uint32_t type) {
        auto profile=get(layer);
        return assign(dynamic(layer),type,profile.responses.data(),16);
    }
    bool pair_allowed(j::ObjectLayer a,j::ObjectLayer b) const {
        if(!dynamic(a)&&!dynamic(b))return false;
        auto pa=get(a),pb=get(b);
        return pa.responses[pb.type]!=0&&pb.responses[pa.type]!=0;
    }
    bool pair_overlap(j::ObjectLayer a,j::ObjectLayer b) const {
        auto pa=get(a),pb=get(b);
        return pa.responses[pb.type]==1||pb.responses[pa.type]==1;
    }
    bool query_allowed(j::ObjectLayer target,std::uint32_t channel,
                       std::uint32_t mask) const {
        if(channel>=16)return false;
        auto profile=get(target);
        return (mask & (1u<<profile.responses[channel]))!=0;
    }
};
class QueryLayerFilter final:public j::ObjectLayerFilter {
    Registry const& registry_;
    std::uint32_t channel_,mask_;
public:
    QueryLayerFilter(Registry const& r,std::uint32_t channel,std::uint32_t mask)
        :registry_(r),channel_(channel),mask_(mask){}
    bool ShouldCollide(j::ObjectLayer l) const override {
        return registry_.query_allowed(l,channel_,mask_);
    }
};
// Thread-safe Begin/End event collection grouped by body pair.
class Events final:public j::ContactListener {
    struct Active {
        NorthStarPhysicsContactEvent event{};
        std::uint32_t contacts=0;
    };
    static std::uint64_t key(j::BodyID a,j::BodyID b) {
        auto x=a.GetIndexAndSequenceNumber(),y=b.GetIndexAndSequenceNumber();
        if(x>y)std::swap(x,y);
        return (std::uint64_t(x)<<32)|y;
    }
    static std::uint32_t scene_index(std::uint64_t user) {
        return user?static_cast<std::uint32_t>(user-1u):UINT32_MAX;
    }
    Registry const& registry_;
    std::mutex mutex_;
    std::unordered_map<std::uint64_t,Active> active_;
    std::deque<NorthStarPhysicsContactEvent> queue_;
    void emit(NorthStarPhysicsContactEvent event) {
        if(queue_.size()>=4096)queue_.pop_front();
        queue_.push_back(event);
    }
    void sensor(j::Body const& a,j::Body const& b,j::ContactSettings& settings) {
        settings.mIsSensor=registry_.pair_overlap(a.GetObjectLayer(),b.GetObjectLayer());
    }
public:
    explicit Events(Registry const& registry):registry_(registry){}
    void OnContactAdded(j::Body const& a,j::Body const& b,
                        j::ContactManifold const&,j::ContactSettings& settings) override {
        sensor(a,b,settings);
        auto const k=key(a.GetID(),b.GetID());
        std::lock_guard lock(mutex_);
        auto& item=active_[k];
        if(item.contacts++==0) {
            item.event={scene_index(a.GetUserData()),scene_index(b.GetUserData()),
                        0u,settings.mIsSensor?1u:2u};
            emit(item.event);
        }
    }
    void OnContactPersisted(j::Body const& a,j::Body const& b,
                            j::ContactManifold const&,j::ContactSettings& settings) override {
        sensor(a,b,settings);
    }
    void OnContactRemoved(j::SubShapeIDPair const& pair) override {
        std::lock_guard lock(mutex_);
        auto const k=key(pair.GetBody1ID(),pair.GetBody2ID());
        auto it=active_.find(k);
        if(it==active_.end())return;
        if(it->second.contacts>1){--it->second.contacts;return;}
        auto event=it->second.event;
        event.phase=1;
        emit(event);
        active_.erase(it);
    }
    std::uint32_t drain(NorthStarPhysicsContactEvent* out,std::uint32_t capacity) {
        if(!out&&capacity)throw std::invalid_argument("Null event destination");
        std::lock_guard lock(mutex_);
        auto const n=std::min<std::size_t>(capacity,queue_.size());
        for(std::size_t i=0;i<n;++i) {
            out[i]=queue_.front();
            queue_.pop_front();
        }
        return static_cast<std::uint32_t>(n);
    }
};
} // namespace northstarcpp::jolt_collision
