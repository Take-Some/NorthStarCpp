#pragma once
#include <northstarcpp/player_blueprint_core.hpp>

namespace northstarcpp::player_blueprint {
struct Mesh {
    Vector3 offset{0.f,-.9f,0.f};
    bool hide_in_first_person=true;
    std::string skeleton;
    std::string material;
    // VFS-relative .nprefab source of the visual hierarchy, not a second Pawn.
    // Optional for backward-compatible standalone YDD/YDR/OBJ meshes.
    std::string prefab;
};
inline bool good_mesh(Mesh const& m) {
    for(float f:{m.offset.x,m.offset.y,m.offset.z})
        if(!std::isfinite(f)||std::abs(f)>100.f)return false;
    return safe_asset(m.skeleton)&&safe_asset(m.material)&&
        safe_asset(m.prefab)&&
        (m.prefab.empty() ||
         (m.prefab.size()>8 && m.prefab.substr(m.prefab.size()-8)==".nprefab"));
}
inline std::string serialize_mesh(Mesh const& m) {
    if(!good_mesh(m))throw std::invalid_argument("Invalid SkeletalMesh references");
    std::ostringstream s;
    s<<std::setprecision(9)<<m.offset.x<<' '<<m.offset.y<<' '<<m.offset.z
     <<' '<<(m.hide_in_first_person?1:0)<<' '
     <<std::quoted(m.skeleton)<<' '<<std::quoted(m.material);
    if(!m.prefab.empty())s<<' '<<std::quoted(m.prefab);
    return s.str();
}
inline std::optional<Mesh> deserialize_mesh(std::string const& source) {
    if(source.size()>1200)return std::nullopt;
    Mesh m;int hide=0;
    std::istringstream s(source);
    if(!(s>>m.offset.x>>m.offset.y>>m.offset.z>>hide
          >>std::quoted(m.skeleton)>>std::quoted(m.material))||
       (hide!=0&&hide!=1))return std::nullopt;
    s>>std::ws;
    // Legacy v1 payloads end after material; newer ones append the prefab
    // without changing the component schema or breaking old scenes.
    if(!s.eof()) {
        if(!(s>>std::quoted(m.prefab)))return std::nullopt;
        s>>std::ws;
        if(!s.eof())return std::nullopt;
    }
    m.hide_in_first_person=hide!=0;
    return good_mesh(m)?std::optional<Mesh>(m):std::nullopt;
}
inline Mesh mesh(Scene const& scene,std::uint64_t entity) {
    if(auto* c=find(scene,entity,mesh_type))
        if(auto decoded=deserialize_mesh(c->payload))return *decoded;
    return {};
}
inline void set_mesh(Scene& s,std::uint64_t id,Mesh const& m) {
    assign(s,id,mesh_type,serialize_mesh(m));
}
}
