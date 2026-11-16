#pragma once
#include <northstarcpp/vehicle_cabin_plugin.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace northstarcpp {
struct VehicleRigAsset {
    NorthStarVehicleRig setup{};
    std::vector<NorthStarVehicleWheelConfig> wheels;
    std::vector<std::string> wheel_visual_nodes; // optional authored scene names
    std::vector<NorthStarVehicleSeatConfig> seats;
    std::vector<NorthStarVehicleDoorConfig> doors;
    std::vector<std::string> door_visual_nodes;
    std::string contact_mode="flat";
    float surface_friction=1.f;
    NorthStarVehicleRig view() const {
        auto result=setup;
        result.wheel_count=static_cast<std::uint32_t>(wheels.size());
        result.wheels=wheels.data();
        result.seat_count=static_cast<std::uint32_t>(seats.size());
        result.seats=seats.data();
        return result;
    }
};
VehicleRigAsset parse_vehicle_rig_xml(std::string_view text);
}
