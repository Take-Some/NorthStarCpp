// Provider-independent locomotion state machine for Player Blueprint.
// Supplies stable clip bindings, timing and blend weights to animation providers.
#pragma once
#include <northstarcpp/player_blueprint.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace northstarcpp::player_animation {
struct Sample {
    player_blueprint::AnimationState state=player_blueprint::AnimationState::Idle;
    player_blueprint::AnimationState previous=player_blueprint::AnimationState::Idle;
    std::string clip;
    std::string previous_clip;
    float playback_seconds=0.f;
    float blend_alpha=1.f; // new-state contribution (0..1)
    float horizontal_speed=0.f;
    bool grounded=true;
};
inline char const* name(player_blueprint::AnimationState state) noexcept {
    using S=player_blueprint::AnimationState;
    switch(state) {
    case S::Idle:return "Idle";
    case S::Walk:return "Walk";
    case S::Run:return "Run";
    case S::Jump:return "Jump";
    case S::Fall:return "Fall";
    }
    return "Unknown";
}
class Controller {
public:
    explicit Controller(player_blueprint::Animation graph)
        :graph_(std::move(graph)) {
        if(!player_blueprint::good_animation(graph_))
            throw std::invalid_argument("Invalid animation graph");
        sample_.clip=player_blueprint::clip(graph_,sample_.state);
    }
    void advance(float seconds,float speed,bool grounded,bool rising) {
        if(!std::isfinite(seconds)||seconds<0.f||seconds>1.f||
           !std::isfinite(speed)||speed<0.f)
            throw std::invalid_argument("Invalid animation update");
        auto next=player_blueprint::pick_animation(graph_,speed,grounded,rising);
        if(next!=sample_.state) {
            sample_.previous=sample_.state;
            sample_.previous_clip=sample_.clip;
            sample_.state=next;
            sample_.clip=player_blueprint::clip(graph_,next);
            sample_.playback_seconds=0.f;
            blend_elapsed_=0.f;
        } else sample_.playback_seconds+=seconds;
        blend_elapsed_+=seconds;
        sample_.blend_alpha=graph_.blend_seconds<=0.f?1.f:
            std::clamp(blend_elapsed_/graph_.blend_seconds,0.f,1.f);
        sample_.horizontal_speed=speed;
        sample_.grounded=grounded;
    }
    Sample const& sample() const noexcept {return sample_;}
    player_blueprint::Animation const& graph() const noexcept {return graph_;}
private:
    player_blueprint::Animation graph_;
    Sample sample_{};
    float blend_elapsed_=100.f;
};
} // namespace northstarcpp::player_animation
