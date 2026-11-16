#pragma once
#include <northstarcpp/player_blueprint_core.hpp>

namespace northstarcpp::player_blueprint {
enum class ViewMode:std::uint32_t {FirstPerson=0,ThirdPerson=1};
struct Camera {
    ViewMode mode=ViewMode::FirstPerson;
    float spring_arm_length=0.f;
    float shoulder=0.f;
    float lag=0.f;
};
inline std::string serialize_camera(Camera const& c) {
    if(static_cast<std::uint32_t>(c.mode)>1||
       !std::isfinite(c.spring_arm_length)||c.spring_arm_length<0.f||
       c.spring_arm_length>20.f||
       !std::isfinite(c.shoulder)||std::abs(c.shoulder)>5.f||
       !std::isfinite(c.lag)||c.lag<0.f||c.lag>10.f)
        throw std::invalid_argument("Invalid Player camera setup");
    std::string bytes;
    player_character::detail::push32(bytes,static_cast<std::uint32_t>(c.mode));
    for(float v:{c.spring_arm_length,c.shoulder,c.lag})
        player_character::detail::pushfloat(bytes,v);
    return bytes;
}
inline std::optional<Camera> deserialize_camera(std::string const& bytes) {
    if(bytes.size()!=16)return std::nullopt;
    Camera c;
    c.mode=static_cast<ViewMode>(player_character::detail::get32(bytes.data(),0));
    c.spring_arm_length=player_character::detail::getfloat(bytes.data(),4);
    c.shoulder=player_character::detail::getfloat(bytes.data(),8);
    c.lag=player_character::detail::getfloat(bytes.data(),12);
    try {serialize_camera(c);}catch(...){return std::nullopt;}
    return c;
}
inline Camera camera(Scene const& scene,std::uint64_t entity) {
    if(auto* c=find(scene,entity,camera_attachment_type))
        if(auto decoded=deserialize_camera(c->payload))return *decoded;
    return {};
}
inline void set_camera(Scene& s,std::uint64_t id,Camera const& c) {
    assign(s,id,camera_attachment_type,serialize_camera(c));
}
}
