#include "viewport_weapon_grip_profiles.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace northstarcpp::viewport_detail {
namespace {
using boost::property_tree::ptree;
float attr(ptree const& item,char const* key) {
    auto const value=item.get<float>(std::string("<xmlattr>.")+key);
    if(!std::isfinite(value)||std::abs(value)>90.f)
        throw std::invalid_argument("Invalid authored finger rotation");
    return value;
}
math::float3 vector3(std::string const& value) {
    std::istringstream s(value);
    s.imbue(std::locale::classic());
    math::float3 out;
    std::string remaining;
    if(!(s>>out.x>>out.y>>out.z) || (s>>remaining) ||
       !out.valid() || out.magnitude()>1.f)
        throw std::invalid_argument("Invalid authored grip offset");
    return out;
}
}
WeaponGripLibrary parse_weapon_grips_xml(std::string_view bytes) {
    if(bytes.empty() || bytes.size()>16384)
        throw std::invalid_argument("Weapon grips XML exceeds 16 KiB");
    std::istringstream s{std::string(bytes)};
    s.imbue(std::locale::classic());
    ptree tree;
    boost::property_tree::read_xml(s,tree,
        boost::property_tree::xml_parser::no_comments);
    auto const& root=tree.get_child("NorthStarWeaponGrips");
    if(root.get<int>("<xmlattr>.version")!=1)
        throw std::invalid_argument("Unsupported WeaponGrips XML version");
    WeaponGripLibrary library;
    for(auto const& [tag,node]:root) {
        if(tag=="<xmlattr>")continue;
        if(tag!="Profile" || library.profiles.size()>=8)
            throw std::invalid_argument("Unknown weapon grip XML element");
        auto name=node.get<std::string>("<xmlattr>.class");
        if(name.size()>32 || name.empty())
            throw std::invalid_argument("Invalid weapon grip class");
        WeaponFingerProfile profile;
        bool seen[2]={false,false};
        for(auto const& [kind,hand]:node) {
            if(kind=="<xmlattr>")continue;
            if(kind!="Hand")throw std::invalid_argument("Unknown grip hand element");
            auto const side=hand.get<std::string>("<xmlattr>.side");
            unsigned id=side=="right"?0:side=="left"?1:2;
            if(id>=2 || seen[id])throw std::invalid_argument("Duplicate grip hand");
            seen[id]=true;
            profile.wrist_offset[id]=vector3(
                hand.get<std::string>("<xmlattr>.wrist_offset"));
            profile.socket_offset[id]=vector3(
                hand.get<std::string>("<xmlattr>.socket_offset"));
            for(auto const& [part,joint]:hand) {
                if(part=="<xmlattr>")continue;
                if(part!="Joint" || profile.bends.size()>=30)
                    throw std::invalid_argument("Invalid grip joint count");
                auto name=joint.get<std::string>("<xmlattr>.name");
                if(name.empty() || name.size()>96)
                    throw std::invalid_argument("Invalid finger joint name");
                profile.bends.push_back({std::move(name),
                    {attr(joint,"x"),attr(joint,"y"),attr(joint,"z")}});
            }
        }
        if(!seen[0] || !seen[1] || profile.bends.size()!=30)
            throw std::invalid_argument("Weapon grip requires both 15-finger hands");
        profile.support_offset=vector3(
            node.get<std::string>("<xmlattr>.support_offset","0 0 0"));
        profile.rear_sight=vector3(
            node.get<std::string>("<xmlattr>.rear_sight","0 0 0"));
        profile.front_sight=vector3(
            node.get<std::string>("<xmlattr>.front_sight","0 0 0.18"));
        profile.primary_grip=vector3(
            node.get<std::string>("<xmlattr>.primary_grip","0 0 0"));
        if(auto support=node.get_optional<std::string>("<xmlattr>.support_grip")) {
            profile.support_grip=vector3(*support);
            profile.has_support_grip=true;
        }
        const auto sight_axis=profile.front_sight-profile.rear_sight;
        if(sight_axis.z<.06f || std::abs(sight_axis.x)>.02f ||
           std::abs(sight_axis.y)>.02f)
            throw std::invalid_argument("Invalid firearm sight alignment");
        profile.sight_eye_distance=node.get<float>(
            "<xmlattr>.sight_eye_distance",.34f);
        if(!std::isfinite(profile.sight_eye_distance) ||
           profile.sight_eye_distance<.15f ||
           profile.sight_eye_distance>.8f)
            throw std::invalid_argument("Invalid ADS rear sight distance");
        profile.ads_elbow_outward_bias=node.get<float>(
            "<xmlattr>.ads_elbow_outward_bias",.39f);
        profile.ads_elbow_drop_bias=node.get<float>(
            "<xmlattr>.ads_elbow_drop_bias",.40f);
        profile.ads_support_hand_stiffness=node.get<float>(
            "<xmlattr>.ads_support_hand_stiffness",1.f);
        profile.ads_forearm_visibility_guard=node.get<float>(
            "<xmlattr>.ads_forearm_visibility_guard",.235f);
        if(!std::isfinite(profile.ads_elbow_outward_bias) ||
           profile.ads_elbow_outward_bias<.12f || profile.ads_elbow_outward_bias>.70f ||
           !std::isfinite(profile.ads_elbow_drop_bias) ||
           profile.ads_elbow_drop_bias<.16f || profile.ads_elbow_drop_bias>.70f ||
           !std::isfinite(profile.ads_support_hand_stiffness) ||
           profile.ads_support_hand_stiffness<.5f ||
           profile.ads_support_hand_stiffness>1.6f ||
           !std::isfinite(profile.ads_forearm_visibility_guard) ||
           profile.ads_forearm_visibility_guard<.10f ||
           profile.ads_forearm_visibility_guard>.40f)
            throw std::invalid_argument("Invalid authored ADS arm profile");
        if(!library.profiles.emplace(std::move(name),std::move(profile)).second)
            throw std::invalid_argument("Duplicate weapon grip class");
    }
    if(library.profiles.empty())
        throw std::invalid_argument("Weapon grip XML contains no profiles");
    return library;
}
} // namespace northstarcpp::viewport_detail
