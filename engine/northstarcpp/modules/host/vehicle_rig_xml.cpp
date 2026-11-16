// NorthStar C++ declarative vehicle rig loader.
// Strict project-authored XML. Plugin receives POD copies via Dynamics ABI v2.
#include "vehicle_rig_xml.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>
namespace northstarcpp {
namespace {
using boost::property_tree::ptree;
template<class T> T attr(ptree const& node,char const* key) {
    T value=node.get<T>(std::string("<xmlattr>.")+key);
    if constexpr(std::is_floating_point_v<T>) {
        if(!std::isfinite(value))throw std::invalid_argument(
            std::string("Non-finite vehicle XML parameter: ")+key);
    }
    return value;
}
}
VehicleRigAsset parse_vehicle_rig_xml(std::string_view bytes) {
    if(bytes.empty()||bytes.size()>131072)
        throw std::invalid_argument("Vehicle XML exceeds 128 KiB");
    std::istringstream stream{std::string(bytes)};
    stream.imbue(std::locale::classic());
    ptree tree;
    boost::property_tree::read_xml(stream,tree,
        boost::property_tree::xml_parser::no_comments);
    auto const& parent=tree.get_child("NorthStarVehicleRig");
    if(attr<unsigned>(parent,"version")!=2)
        throw std::invalid_argument("Unsupported vehicle rig XML schema version");
    VehicleRigAsset asset;
    auto& r=asset.setup;
    r.size=sizeof(r);
    r.mass=attr<float>(parent,"mass");
    r.yaw_inertia=attr<float>(parent,"yaw_inertia");
    r.center_of_mass_y=attr<float>(parent,"center_of_mass_y");
    r.engine_force=attr<float>(parent,"engine_force");
    r.brake_force=attr<float>(parent,"brake_force");
    r.aerodynamic_drag=attr<float>(parent,"aerodynamic_drag");
    r.rolling_resistance=attr<float>(parent,"rolling_resistance");
    r.abs_enabled=attr<unsigned>(parent,"abs");
    r.traction_control_enabled=attr<unsigned>(parent,"tcs");
    asset.contact_mode=parent.get<std::string>("<xmlattr>.contact_mode","flat");
    if(asset.contact_mode!="flat"&&asset.contact_mode!="physics")
        throw std::invalid_argument("Vehicle contact_mode must be flat or physics");
    asset.surface_friction=attr<float>(parent,"surface_friction");
    if(asset.surface_friction<.05f||asset.surface_friction>3.f)
        throw std::invalid_argument("Vehicle surface friction outside 0.05..3");
    for(auto const& node:parent) {
        if(node.first=="<xmlattr>")continue;
        if(node.first=="Wheel") {
            if(asset.wheels.size()>=NORTHSTARCPP_VEHICLE_MAX_WHEELS)
                throw std::invalid_argument("Vehicle rig wheel count exceeds 16");
            auto const& a=node.second;
            NorthStarVehicleWheelConfig w{};
            w.size=sizeof(w);
            w.mount[0]=attr<float>(a,"x");
            w.mount[1]=attr<float>(a,"y");
            w.mount[2]=attr<float>(a,"z");
            w.radius=attr<float>(a,"radius");
            w.rest_length=attr<float>(a,"rest_length");
            w.travel=attr<float>(a,"travel");
            w.spring_rate=attr<float>(a,"spring_rate");
            w.damper_rate=attr<float>(a,"damper_rate");
            w.grip_long=attr<float>(a,"grip_long");
            w.grip_lat=attr<float>(a,"grip_lat");
            w.steer_factor=attr<float>(a,"steer");
            w.drive_factor=attr<float>(a,"drive");
            w.brake_factor=attr<float>(a,"brake");
            w.rotational_inertia=attr<float>(a,"inertia");
            auto visual=a.get<std::string>("<xmlattr>.visual","");
            if(visual.size()>120 || visual.find('\0')!=std::string::npos)
                throw std::invalid_argument("Invalid wheel visual scene binding");
            if(!visual.empty()) {
                for(auto const& previous:asset.wheel_visual_nodes)
                    if(previous==visual)
                        throw std::invalid_argument("Duplicate vehicle wheel visual binding");
            }
            asset.wheel_visual_nodes.push_back(std::move(visual));
            asset.wheels.push_back(w);
        } else if(node.first=="Door") {
            if(asset.doors.size()>=NORTHSTARCPP_VEHICLE_MAX_DOORS)
                throw std::invalid_argument("Too many doors in vehicle rig");
            auto const& a=node.second;
            NorthStarVehicleDoorConfig door{};
            door.size=sizeof(door);
            door.hinge[0]=attr<float>(a,"x");
            door.hinge[1]=attr<float>(a,"y");
            door.hinge[2]=attr<float>(a,"z");
            door.open_angle=attr<float>(a,"open_angle");
            door.angular_speed=attr<float>(a,"speed");
            if(!std::isfinite(door.open_angle) ||
               std::abs(door.open_angle)<.1f ||std::abs(door.open_angle)>2.7f||
               door.angular_speed<.05f||door.angular_speed>5.f)
                throw std::invalid_argument("Invalid vehicle door swing");
            auto const visual=a.get<std::string>("<xmlattr>.visual","");
            if(visual.size()>120)
                throw std::invalid_argument("Door visual reference too long");
            if(!visual.empty())
                for(auto const& name:asset.door_visual_nodes)
                    if(visual==name)
                        throw std::invalid_argument("Duplicate door visual binding");
            asset.door_visual_nodes.push_back(visual);
            asset.doors.push_back(door);
        } else if(node.first=="Seat") {
            if(asset.seats.size()>=NORTHSTARCPP_VEHICLE_MAX_SEATS)
                throw std::invalid_argument("Vehicle rig seats exceed 64");
            auto const& a=node.second;
            NorthStarVehicleSeatConfig seat{};
            seat.size=sizeof(seat);
            seat.position[0]=attr<float>(a,"x");
            seat.position[1]=attr<float>(a,"y");
            seat.position[2]=attr<float>(a,"z");
            seat.yaw=attr<float>(a,"yaw");
            seat.role=attr<unsigned>(a,"role");
            asset.seats.push_back(seat);
        } else {
            throw std::invalid_argument("Unknown node in vehicle rig XML: "+node.first);
        }
    }
    if(asset.wheels.size()<2)
        throw std::invalid_argument("Vehicle rig requires at least two wheels");
    return asset;
}
}
