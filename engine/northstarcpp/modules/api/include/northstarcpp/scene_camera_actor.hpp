// Scene camera entities: provider-validated, renderer-independent camera shots.
#pragma once
#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::scene_camera {
inline constexpr char type[]="northstar.scene.camera";
inline constexpr std::uint32_t version=1;
struct Settings {
    float fov=75.f,near_plane=.08f,far_plane=500.f;
    std::uint32_t active=0,priority=0;
};
inline std::string encode(Settings const& s) {
    if(!std::isfinite(s.fov)||s.fov<25.f||s.fov>120.f||
       !std::isfinite(s.near_plane)||s.near_plane<.01f||s.near_plane>2.f||
       !std::isfinite(s.far_plane)||s.far_plane<=s.near_plane+1.f||
       s.far_plane>10000.f||s.active>1||s.priority>100)
        throw std::invalid_argument("Invalid scene camera settings");
    std::string result(20,'\0');
    const float values[]{s.fov,s.near_plane,s.far_plane};
    for(int i=0;i<3;++i) {
        std::uint32_t bits=0;
        static_assert(sizeof(bits)==sizeof(float));
        std::memcpy(&bits,&values[i],sizeof(bits));
        for(int byte=0;byte<4;++byte)
            result[std::size_t(i*4+byte)]=char((bits>>(byte*8))&255u);
    }
    for(auto const& field:{std::pair<int,std::uint32_t>{12,s.active},
                            {16,s.priority}})
        for(int byte=0;byte<4;++byte)
            result[std::size_t(field.first+byte)]=
                char((field.second>>(byte*8))&255u);
    return result;
}
inline std::optional<Settings> decode(std::string const& bytes) {
    if(bytes.size()!=20)return std::nullopt;
    auto read32=[&](int offset) {
        std::uint32_t bits=0;
        for(int byte=0;byte<4;++byte)
            bits|=std::uint32_t(
                static_cast<unsigned char>(bytes[std::size_t(offset+byte)]))
                <<(byte*8);
        return bits;
    };
    Settings value;
    std::uint32_t bits=read32(0);
    std::memcpy(&value.fov,&bits,4);
    bits=read32(4);std::memcpy(&value.near_plane,&bits,4);
    bits=read32(8);std::memcpy(&value.far_plane,&bits,4);
    value.active=read32(12);value.priority=read32(16);
    try {encode(value);return value;} catch(...) {return std::nullopt;}
}
inline bool validator(void*,std::uint32_t v,void const* data,std::size_t size) {
    return v==version && data && size==20 &&
        decode(std::string(static_cast<char const*>(data),size)).has_value();
}
inline bool register_schema() {
    if(scene_component_schema(type))return true;
    return register_scene_component_type({type,version,version,validator,nullptr,
        {{"FOV",SceneComponentFieldKind::Float32,0},
         {"Near",SceneComponentFieldKind::Float32,4},
         {"Far",SceneComponentFieldKind::Float32,8},
         {"Active",SceneComponentFieldKind::UInt32,12},
         {"Priority",SceneComponentFieldKind::UInt32,16}}});
}
inline std::optional<CameraShot> active_shot(Scene const& scene) {
    std::optional<CameraShot> best;
    constexpr float toRad=.017453292519943295f;
    for(auto const& c:scene.components) {
        if(c.type!=type||c.version!=version||!c.entity_id)continue;
        const auto config=decode(c.payload);
        if(!config || !config->active)continue;
        auto it=std::find_if(scene.objects.begin(),scene.objects.end(),
            [&](SceneObject const& o) {return o.node.id==c.entity_id;});
        if(it==scene.objects.end()||!it->node.visible)continue;
        CameraShot shot;
        shot.source="scene.camera."+std::to_string(it->node.id);
        shot.priority=static_cast<int>(config->priority);
        shot.pose.position=it->position;
        shot.pose.yaw=it->node.rotation.y*toRad;
        shot.pose.pitch=it->node.rotation.x*toRad;
        shot.pose.roll=it->node.rotation.z*toRad;
        shot.pose.fov_y_degrees=config->fov;
        shot.pose.near_plane=config->near_plane;
        shot.pose.far_plane=config->far_plane;
        shot.blend_seconds=.35f;
        if(!best || shot.priority>best->priority)best=shot;
    }
    return best;
}
} // namespace northstarcpp::scene_camera
