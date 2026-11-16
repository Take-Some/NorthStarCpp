#pragma once
#include <northstarcpp/player_blueprint_core.hpp>

namespace northstarcpp::player_blueprint {
enum class AnimationState:std::uint32_t {Idle,Walk,Run,Jump,Fall};
struct Animation {
    std::string idle,walk,run,jump,fall;
    float walk_threshold=.15f;
    float run_threshold=5.6f;
    float blend_seconds=.18f;
    bool root_motion=false;
};
inline bool good_animation(Animation const& a) {
    if(!std::isfinite(a.walk_threshold)||a.walk_threshold<0.f||
       a.walk_threshold>30.f||!std::isfinite(a.run_threshold)||
       a.run_threshold<a.walk_threshold||a.run_threshold>100.f||
       !std::isfinite(a.blend_seconds)||a.blend_seconds<0.f||
       a.blend_seconds>5.f)return false;
    for(auto const* v:{&a.idle,&a.walk,&a.run,&a.jump,&a.fall})
        if(!safe_asset(*v))return false;
    return true;
}
inline std::string serialize_animation(Animation const& a) {
    if(!good_animation(a))throw std::invalid_argument("Invalid Animation Graph");
    std::ostringstream s;
    s<<std::setprecision(9)<<std::quoted(a.idle)<<' '
     <<std::quoted(a.walk)<<' '<<std::quoted(a.run)<<' '
     <<std::quoted(a.jump)<<' '<<std::quoted(a.fall)<<' '
     <<a.walk_threshold<<' '<<a.run_threshold<<' '
     <<a.blend_seconds<<' '<<(a.root_motion?1:0);
    return s.str();
}
inline std::optional<Animation> deserialize_animation(std::string const& data) {
    if(data.size()>3000)return std::nullopt;
    Animation a;int root=0;std::istringstream s(data);
    if(!(s>>std::quoted(a.idle)>>std::quoted(a.walk)>>std::quoted(a.run)
          >>std::quoted(a.jump)>>std::quoted(a.fall)
          >>a.walk_threshold>>a.run_threshold>>a.blend_seconds>>root)||
       (root!=0&&root!=1))return std::nullopt;
    s>>std::ws;
    if(!s.eof())return std::nullopt;
    a.root_motion=root!=0;
    return good_animation(a)?std::optional<Animation>(a):std::nullopt;
}
inline AnimationState pick_animation(Animation const& graph,
    float horizontal_speed,bool grounded,bool rising) {
    if(!grounded)return rising?AnimationState::Jump:AnimationState::Fall;
    if(!std::isfinite(horizontal_speed)||horizontal_speed<graph.walk_threshold)
        return AnimationState::Idle;
    return horizontal_speed>=graph.run_threshold?
        AnimationState::Run:AnimationState::Walk;
}
inline std::string const& clip(Animation const& graph,AnimationState state) {
    switch(state) {
    case AnimationState::Idle:return graph.idle;
    case AnimationState::Walk:return graph.walk;
    case AnimationState::Run:return graph.run;
    case AnimationState::Jump:return graph.jump;
    default:return graph.fall;
    }
}
inline Animation animation(Scene const& scene,std::uint64_t entity) {
    if(auto* c=find(scene,entity,animation_type))
        if(auto decoded=deserialize_animation(c->payload))return *decoded;
    return {};
}
inline void set_animation(Scene& s,std::uint64_t id,Animation const& a) {
    assign(s,id,animation_type,serialize_animation(a));
}
}
