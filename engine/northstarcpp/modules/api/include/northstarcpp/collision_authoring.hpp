// NorthStarCpp - provider-neutral scene-authored collision metadata.
// SceneComponentBlob version 4 adds 16 configurable channels and preset labels.
#pragma once
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
namespace northstarcpp::collision_authoring {
inline constexpr char component_type[]="northstar.physics.collider";
inline constexpr std::uint32_t component_version=4;
inline constexpr std::size_t max_channels=16;
enum class Shape:std::uint32_t {
    Box=0,Sphere=1,Capsule=2,ComplexMesh=3,AutoConvex=4
};
enum class ObjectType:std::uint32_t {
    WorldStatic=0,WorldDynamic=1,Pawn=2,PhysicsBody=3,
    Vehicle=4,Destructible=5
};
enum class Response:std::uint8_t {Ignore=0,Overlap=1,Block=2};
inline constexpr std::array<char const*,6> channel_names{
    "WorldStatic","WorldDynamic","Pawn","PhysicsBody","Vehicle","Destructible"};
inline std::array<Response,max_channels> block_all() {
    std::array<Response,max_channels> v;
    v.fill(Response::Block);
    return v;
}
struct Collider {
    Shape shape=Shape::Box;
    Vector3 dimensions{1.f,1.f,1.f};
    Vector3 offset{0.f,0.f,0.f};
    std::uint32_t splits_num=4;
    ObjectType object_type=ObjectType::WorldStatic;
    std::array<Response,max_channels> responses=block_all();
    std::string preset="Custom";
};
inline Collider defaults(float size) {
    Collider c;
    c.dimensions={size,size,size};
    return c;
}
inline bool valid(Collider const& c) {
    if(static_cast<std::uint32_t>(c.shape)>4||c.splits_num<1||
       c.splits_num>16||static_cast<std::uint32_t>(c.object_type)>=max_channels||
       c.preset.empty()||c.preset.size()>40)
        return false;
    for(char ch:c.preset)
        if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
             (ch>='0'&&ch<='9')||ch=='_'||ch=='-'))
            return false;
    for(auto r:c.responses)
        if(static_cast<std::uint32_t>(r)>2)return false;
    for(float f:{c.dimensions.x,c.dimensions.y,c.dimensions.z})
        if(!std::isfinite(f)||f<.01f||f>10000.f)return false;
    for(float f:{c.offset.x,c.offset.y,c.offset.z})
        if(!std::isfinite(f)||std::abs(f)>10000.f)return false;
    return c.shape!=Shape::Capsule||c.dimensions.y>=c.dimensions.x;
}
inline std::string serialize(Collider const& c) {
    if(!valid(c))throw std::invalid_argument("Invalid collider profile");
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream<<std::setprecision(9)<<static_cast<unsigned>(c.shape)<<' '
          <<c.dimensions.x<<' '<<c.dimensions.y<<' '<<c.dimensions.z<<' '
          <<c.offset.x<<' '<<c.offset.y<<' '<<c.offset.z<<' '
          <<c.splits_num<<' '<<static_cast<std::uint32_t>(c.object_type);
    for(auto r:c.responses)stream<<' '<<static_cast<unsigned>(r);
    stream<<' '<<std::quoted(c.preset);
    return stream.str();
}
inline std::optional<Collider> deserialize(
    std::string const& bytes,std::uint32_t version=component_version) {
    if(bytes.size()>512||version<1||version>component_version)
        return std::nullopt;
    std::istringstream stream(bytes);
    stream.imbue(std::locale::classic());
    unsigned shape=0,type=0;
    Collider c;
    if(!(stream>>shape>>c.dimensions.x>>c.dimensions.y>>c.dimensions.z
         >>c.offset.x>>c.offset.y>>c.offset.z))
        return std::nullopt;
    if(version>=2&&!(stream>>c.splits_num>>type))return std::nullopt;
    if(version>=3) {
        for(std::size_t i=0;i<(version==3?6:max_channels);++i) {
            unsigned value=0;
            if(!(stream>>value)||value>2)return std::nullopt;
            c.responses[i]=static_cast<Response>(value);
        }
    }
    if(version>=4&&!(stream>>std::quoted(c.preset)))return std::nullopt;
    stream>>std::ws;
    if(!stream.eof()||shape>4||
       type>=(version<4?6u:static_cast<unsigned>(max_channels)))
        return std::nullopt;
    c.shape=static_cast<Shape>(shape);
    c.object_type=static_cast<ObjectType>(type);
    return valid(c)?std::optional<Collider>(std::move(c)):std::nullopt;
}
inline std::optional<Collider> lookup(Scene const& scene,std::uint64_t entity) {
    for(auto const& c:scene.components)
        if(c.entity_id==entity&&c.type==component_type &&
           c.version>=1&&c.version<=component_version)
            return deserialize(c.payload,c.version);
    return std::nullopt;
}
inline Collider for_object(Scene const& scene,SceneObject const& object) {
    auto saved=lookup(scene,object.node.id);
    return saved.value_or(defaults(object.size));
}
inline void assign(Scene& scene,std::uint64_t entity,Collider const& c) {
    if(!entity||!valid(c))throw std::invalid_argument("Invalid authored collider");
    SceneComponentBlob data{entity,component_type,component_version,serialize(c)};
    for(auto& current:scene.components)
        if(current.entity_id==entity&&current.type==component_type) {
            current=std::move(data);
            return;
        }
    scene.components.push_back(std::move(data));
}
} // namespace northstarcpp::collision_authoring
