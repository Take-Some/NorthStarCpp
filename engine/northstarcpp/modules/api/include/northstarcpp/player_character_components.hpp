// NorthStar Gameplay: versioned authoring archetype, not a monolithic Player class.
// Existing legacy SceneObject::player remains a compatibility bridge until the
// runtime Controller -> Pawn possession interface is implemented.
#pragma once
#include <northstarcpp/scene.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::player_character {
inline constexpr char pawn_type[]="northstar.gameplay.pawn";
inline constexpr char motor_type[]="northstar.gameplay.character_motor";
inline constexpr char camera_type[]="northstar.gameplay.camera_rig";
inline constexpr std::uint32_t version=1;

// POD-free, little-endian stable scalar layouts: never serialize native C++
// object memory or pointers. Inspector uses the existing reflected fields.
namespace detail {
inline void push32(std::string& out,std::uint32_t v) {
    for(unsigned i=0;i<4;++i)out.push_back(char((v>>(8*i))&255));
}
inline void pushfloat(std::string& out,float v) {
    std::uint32_t u=0;
    std::memcpy(&u,&v,4);
    push32(out,u);
}
inline std::uint32_t get32(void const* p,std::size_t offset) {
    auto const* v=static_cast<unsigned char const*>(p)+offset;
    return std::uint32_t(v[0])|(std::uint32_t(v[1])<<8)|
           (std::uint32_t(v[2])<<16)|(std::uint32_t(v[3])<<24);
}
inline float getfloat(void const* p,std::size_t offset) {
    auto const u=get32(p,offset);
    float f=0.f;
    std::memcpy(&f,&u,4);
    return f;
}
inline bool finite_range(void const* p,std::size_t offset,float lo,float hi) {
    float const value=getfloat(p,offset);
    return std::isfinite(value)&&value>=lo&&value<=hi;
}
inline bool pawn_valid(void*,std::uint32_t v,void const* p,std::size_t n) {
    return v==version && n==12 && p &&
        get32(p,0)==1 && get32(p,4)<=1 && get32(p,8)<=1;
}
inline bool motor_valid(void*,std::uint32_t v,void const* p,std::size_t n) {
    if(v!=version||n!=24||!p)return false;
    if(!finite_range(p,0,.01f,200.f) ||
       !finite_range(p,4,.01f,200.f) ||
       !finite_range(p,8,0.f,100.f) ||
       !finite_range(p,12,0.f,10000.f) ||
       !finite_range(p,16,0.f,1.f) ||
       !finite_range(p,20,0.f,20.f))return false;
    return getfloat(p,4)>=getfloat(p,0);
}
inline bool camera_valid(void*,std::uint32_t v,void const* p,std::size_t n) {
    return v==version && n==16 && p &&
        finite_range(p,0,0.f,5.f) &&
        finite_range(p,4,10.f,170.f) &&
        finite_range(p,8,-89.f,0.f) &&
        finite_range(p,12,0.f,89.f);
}
inline void add_if_absent(Scene& scene,std::uint64_t id,
                          char const* type,std::string payload) {
    for(auto const& entry:scene.components)
        if(entry.entity_id==id && entry.type==type)return;
    scene.components.push_back({id,type,version,std::move(payload)});
}
} // namespace detail

inline bool has_pawn(Scene const& scene,std::uint64_t id) {
    for(auto const& entry:scene.components)
        if(entry.entity_id==id && entry.type==pawn_type &&
           entry.version==version &&
           detail::pawn_valid(nullptr,entry.version,
                              entry.payload.data(),entry.payload.size()))
            return true;
    return false;
}

inline void add_character_components(Scene& scene,std::uint64_t id,
                                     bool local_player) {
    if(scene.version!=3 || id==0)
        throw std::invalid_argument("Character requires a stable V3 entity ID");
    bool found=false;
    for(auto const& object:scene.objects)
        if(object.node.id==id) {found=true;break;}
    if(!found)throw std::invalid_argument("Character entity is missing");

    std::string pawn;
    detail::push32(pawn,1u); // Pawn kind: humanoid character
    detail::push32(pawn,local_player?1u:0u); // auto-possess local player 0
    detail::push32(pawn,1u); // enable input when possessed
    std::string motor;
    for(float v:{5.f,8.f,5.f,18.f,.35f,1.f})
        detail::pushfloat(motor,v); // walk/run/jump/accel/air/gravity
    std::string camera;
    for(float v:{1.65f,75.f,-85.f,85.f})detail::pushfloat(camera,v);
    detail::add_if_absent(scene,id,pawn_type,std::move(pawn));
    detail::add_if_absent(scene,id,motor_type,std::move(motor));
    if(local_player)
        detail::add_if_absent(scene,id,camera_type,std::move(camera));

    if(!collision_authoring::lookup(scene,id)) {
        auto collider=collision_authoring::defaults(1.8f);
        collider.shape=collision_authoring::Shape::Capsule;
        collider.dimensions={.65f,1.8f,.65f};
        collider.object_type=collision_authoring::ObjectType::Pawn;
        collision_authoring::assign(scene,id,collider);
    }
}

inline bool register_schemas() {
    auto ensure=[](SceneComponentType type) {
        if(scene_component_schema(type.type))return true;
        return register_scene_component_type(std::move(type));
    };
    return ensure({pawn_type,version,version,detail::pawn_valid,nullptr,{
        {"Character Kind",SceneComponentFieldKind::UInt32,0},
        {"Auto Possess Local",SceneComponentFieldKind::UInt32,4},
        {"Input Enabled",SceneComponentFieldKind::UInt32,8}}}) &&
        ensure({motor_type,version,version,detail::motor_valid,nullptr,{
        {"Walk Speed",SceneComponentFieldKind::Float32,0},
        {"Run Speed",SceneComponentFieldKind::Float32,4},
        {"Jump Speed",SceneComponentFieldKind::Float32,8},
        {"Acceleration",SceneComponentFieldKind::Float32,12},
        {"Air Control",SceneComponentFieldKind::Float32,16},
        {"Gravity Scale",SceneComponentFieldKind::Float32,20}}}) &&
        ensure({camera_type,version,version,detail::camera_valid,nullptr,{
        {"Eye Height",SceneComponentFieldKind::Float32,0},
        {"Field of View",SceneComponentFieldKind::Float32,4},
        {"Min Pitch",SceneComponentFieldKind::Float32,8},
        {"Max Pitch",SceneComponentFieldKind::Float32,12}}});
}
} // namespace northstarcpp::player_character
