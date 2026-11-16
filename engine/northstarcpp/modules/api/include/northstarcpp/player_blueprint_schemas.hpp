#pragma once
#include <northstarcpp/player_blueprint_factory.hpp>

namespace northstarcpp::player_blueprint {
inline bool valid_component(void*,std::uint32_t v,
                            void const* data,std::size_t length,
                            char const* type) {
    if(v!=version||!data)return false;
    auto payload=std::string(static_cast<char const*>(data),length);
    if(std::string(type)==controller_type)
        return deserialize_controller(payload).has_value();
    if(std::string(type)==camera_attachment_type)
        return deserialize_camera(payload).has_value();
    if(std::string(type)==mesh_type)
        return deserialize_mesh(payload).has_value();
    if(std::string(type)==animation_type)
        return deserialize_animation(payload).has_value();
    return false;
}
inline bool controller_valid(void* u,std::uint32_t v,void const* p,std::size_t n) {
    return valid_component(u,v,p,n,controller_type);
}
inline bool camera_valid(void* u,std::uint32_t v,void const* p,std::size_t n) {
    return valid_component(u,v,p,n,camera_attachment_type);
}
inline bool mesh_valid(void* u,std::uint32_t v,void const* p,std::size_t n) {
    return valid_component(u,v,p,n,mesh_type);
}
inline bool animation_valid(void* u,std::uint32_t v,void const* p,std::size_t n) {
    return valid_component(u,v,p,n,animation_type);
}
inline bool register_schemas() {
    auto ensure=[](SceneComponentType entry) {
        if(scene_component_schema(entry.type))return true;
        return register_scene_component_type(std::move(entry));
    };
    return ensure({controller_type,version,version,controller_valid,nullptr})&&
           ensure({camera_attachment_type,version,version,camera_valid,nullptr})&&
           ensure({mesh_type,version,version,mesh_valid,nullptr})&&
           ensure({animation_type,version,version,animation_valid,nullptr});
}
}
