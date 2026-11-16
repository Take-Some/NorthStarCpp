// NorthStarCpp provider-neutral native vehicle service.
// The stable v1 API is required; the more detailed v2 dynamics extension is optional.
#pragma once
#include <northstarcpp/vehicle_dynamics_plugin.h>
#include <northstarcpp/vehicle_cabin_plugin.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace northstarcpp {
class VehicleService final {
public:
    VehicleService();
    ~VehicleService();
    VehicleService(VehicleService const&)=delete;
    VehicleService& operator=(VehicleService const&)=delete;
    std::string provider_name() const;
    std::string provider_source() const;
    void attach(NorthStarVehicleDefinition const& definition);
    void detach(std::uint32_t id);
    void control(std::uint32_t id,float throttle,float steering,float brake);
    void waypoint(std::uint32_t id,float x,float z,float speed,float radius=2.f);
    void step(float dt);
    NorthStarVehicleState state(std::uint32_t id) const;
    std::vector<NorthStarVehicleState> states() const;
    std::uint32_t count() const;

    bool supports_dynamics() const noexcept;
    bool supports_cabin() const noexcept;
    void configure_doors(std::uint32_t id,std::vector<NorthStarVehicleDoorConfig> const& doors);
    void set_door(std::uint32_t id,std::uint32_t index,bool open);
    NorthStarVehicleDoorState door(std::uint32_t id,std::uint32_t index) const;
    std::uint32_t door_count(std::uint32_t id) const;
    void configure(std::uint32_t id,NorthStarVehicleRig const& rig);
    std::uint32_t wheel_count(std::uint32_t id) const;
    NorthStarVehicleWheelState wheel(std::uint32_t id,std::uint32_t ordinal) const;
    std::uint32_t seat_count(std::uint32_t id) const;
    NorthStarVehicleSeatState seat(std::uint32_t id,std::uint32_t ordinal) const;
    void occupy(std::uint32_t id,std::uint32_t seat,std::uint64_t entity);
    void release(std::uint32_t id,std::uint32_t seat,std::uint64_t entity);
    void set_contact(std::uint32_t id,NorthStarVehicleGroundContact const& contact);
    NorthStarVehicleMotionState motion(std::uint32_t id) const;
    void resolve_collision(std::uint32_t id,NorthStarVehicleCollision const& event);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
