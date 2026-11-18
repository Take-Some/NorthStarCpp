// Stable, provider-independent Controller -> Pawn ownership and transitions.
// Gameplay policy uses Scene V3 Entity IDs, not Jolt indices or Flecs handles.
#pragma once
#include <northstarcpp/controller_pawn.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace northstarcpp::gameplay {

enum class PossessionEventKind : std::uint8_t {
    OnPossess, OnUnPossess
};
struct PossessionEvent {
    PossessionEventKind kind=PossessionEventKind::OnPossess;
    std::uint64_t pawn=0;
    std::uint64_t other_pawn=0; // old Pawn on possess; next Pawn on unpossess
    std::uint64_t revision=0;
};

// Does not own a Scene, Actor, physics body or script callback. Events are
// polled by the game thread. The fixed queue avoids allocations on hot paths.
// If a client never polls, the oldest events are overwritten and the explicit
// overrun counter signals that the stream is no longer complete.
class PossessionState final {
public:
    static constexpr std::size_t event_capacity=64;
    bool possessed() const noexcept {return pawn_id_!=0;}
    std::uint64_t controlled_entity() const noexcept {return pawn_id_;}
    std::uint64_t revision() const noexcept {return revision_;}
    std::uint64_t overwritten_events() const noexcept {return overwritten_events_;}

    std::optional<std::size_t> controlled_index(Scene const& scene) const noexcept {
        if(!pawn_id_)return std::nullopt;
        for(std::size_t i=0;i<scene.objects.size();++i)
            if(scene.objects[i].node.id==pawn_id_)return i;
        return std::nullopt;
    }

    // May be called on an unpossessed controller. Repeated possession of the
    // same Pawn is idempotent; invalid targets never mutate the controller.
    bool possess(Scene const& scene,std::uint64_t target) {
        if(!valid_controller_pawn(scene,target))
            throw std::invalid_argument("Controller requires a valid Pawn Entity");
        if(pawn_id_==target)return false;
        auto const previous=pawn_id_;
        ++revision_;
        if(previous)push({PossessionEventKind::OnUnPossess,previous,target,revision_});
        pawn_id_=target;
        push({PossessionEventKind::OnPossess,target,previous,revision_});
        return true;
    }
    bool unpossess() noexcept {
        if(!pawn_id_)return false;
        auto const previous=pawn_id_;
        pawn_id_=0;
        ++revision_;
        push({PossessionEventKind::OnUnPossess,previous,0,revision_});
        return true;
    }
    bool reconcile(Scene const& scene) noexcept {
        return pawn_id_ && !valid_controller_pawn(scene,pawn_id_) && unpossess();
    }
    std::optional<PossessionEvent> take_event() noexcept {
        if(!count_)return std::nullopt;
        auto const result=events_[head_];
        head_=(head_+1)%event_capacity;
        --count_;
        return result;
    }
    std::size_t pending_events() const noexcept {return count_;}
private:
    void push(PossessionEvent event) noexcept {
        if(count_==event_capacity) {
            head_=(head_+1)%event_capacity;
            --count_;
            ++overwritten_events_;
        }
        events_[(head_+count_)%event_capacity]=event;
        ++count_;
    }
    std::uint64_t pawn_id_=0;
    std::uint64_t revision_=0;
    std::array<PossessionEvent,event_capacity> events_{};
    std::size_t head_=0,count_=0;
    std::uint64_t overwritten_events_=0;
};
} // namespace northstarcpp::gameplay
