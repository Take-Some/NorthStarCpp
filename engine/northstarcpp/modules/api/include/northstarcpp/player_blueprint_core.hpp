#pragma once
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp::player_blueprint {
inline constexpr std::uint32_t version=1;
inline constexpr char controller_type[]="northstar.gameplay.player_controller";
inline constexpr char camera_attachment_type[]="northstar.gameplay.camera_attachment";
inline constexpr char mesh_type[]="northstar.gameplay.skeletal_mesh";
inline constexpr char animation_type[]="northstar.gameplay.animation_controller";
inline bool safe_asset(std::string const& s) {
    if(s.empty())return true;
    if(s.size()>512||s.front()=='/'||s.find('\\')!=std::string::npos||
       s.find(':')!=std::string::npos)return false;
    std::string segment;
    for(char ch:s) {
        if(static_cast<unsigned char>(ch)<32||ch==127)return false;
        if(ch=='/') {
            if(segment.empty()||segment=="."||segment=="..")return false;
            segment.clear();
        } else segment.push_back(ch);
    }
    return !segment.empty()&&segment!="."&&segment!="..";
}
inline void assign(Scene& scene,std::uint64_t id,char const* type,
                   std::string payload) {
    if(scene.version!=3||!id||payload.size()>4096)
        throw std::invalid_argument("Player component requires a V3 entity");
    bool found=false;
    for(auto const& object:scene.objects)
        found|=object.node.id==id;
    if(!found)throw std::invalid_argument("Player component owner missing");
    SceneComponentBlob next{id,type,version,std::move(payload)};
    for(auto& component:scene.components)
        if(component.entity_id==id&&component.type==type) {
            component=std::move(next);
            return;
        }
    scene.components.push_back(std::move(next));
}
inline SceneComponentBlob const* find(Scene const& scene,std::uint64_t id,
                                      char const* type) {
    for(auto const& c:scene.components)
        if(c.entity_id==id&&c.type==type&&c.version==version)return &c;
    return nullptr;
}
}
