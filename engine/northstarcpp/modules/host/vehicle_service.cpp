// NorthStarCpp engine.vehicle facade: stable ABI v1 and optional dynamics ABI v2.
#include <northstarcpp/vehicle_service.hpp>
#include "module_loader.hpp"
#include <cstdint>
#include <stdexcept>
namespace northstarcpp {
struct VehicleService::State {
    Module module{NORTHSTARCPP_CAPABILITY_VEHICLE};
    NorthStarVehicleApi const& api=
        *module.query<NorthStarVehicleApi>(NORTHSTARCPP_VEHICLE_ABI);
    NorthStarVehicleDynamicsApi const* extended=
        module.query_extension<NorthStarVehicleDynamicsApi>(
            NORTHSTARCPP_VEHICLE_DYNAMICS_ABI);
    NorthStarVehicleCabinApi const* cabin=
        module.query_extension<NorthStarVehicleCabinApi>(NORTHSTARCPP_VEHICLE_CABIN_ABI);
    NorthStarVehiclePowertrainApi const* powertrain=
        module.query_extension<NorthStarVehiclePowertrainApi>(
            NORTHSTARCPP_VEHICLE_POWERTRAIN_ABI);
    void* world{};
    State() {
        world=api.create_world();
        if(!world)throw module.error("engine.vehicle create world failed");
    }
    ~State() {if(world)api.destroy_world(world);}
    NorthStarVehiclePowertrainApi const& require_powertrain() const {
        if(!powertrain)throw std::runtime_error(
            "Selected engine.vehicle provider lacks Powertrain ABI v4");
        return *powertrain;
    }
    NorthStarVehicleCabinApi const& require_cabin() const {
        if(!cabin)throw std::runtime_error(
            "Selected engine.vehicle provider lacks Cabin ABI v3");
        return *cabin;
    }
    NorthStarVehicleDynamicsApi const& require_dynamics() const {
        if(!extended)throw std::runtime_error(
            "The selected engine.vehicle provider lacks Dynamics ABI v2");
        return *extended;
    }
};
bool VehicleService::supports_cabin() const noexcept {return state_->cabin!=nullptr;}
bool VehicleService::supports_powertrain() const noexcept {
    return state_->powertrain!=nullptr;
}
void VehicleService::configure_powertrain(std::uint32_t id,
    NorthStarVehiclePowertrainConfig const& config) {
    auto const& api=state_->require_powertrain();
    if(!api.configure(state_->world,id,&config))
        throw state_->module.error("engine.vehicle configure powertrain failed");
}
NorthStarVehiclePowertrainState VehicleService::powertrain(std::uint32_t id) const {
    auto const& api=state_->require_powertrain();
    NorthStarVehiclePowertrainState out{};
    out.size=sizeof(out);
    if(!api.state(state_->world,id,&out) || out.size<sizeof(out))
        throw state_->module.error("engine.vehicle powertrain telemetry unavailable");
    return out;
}
void VehicleService::select_gear(std::uint32_t id,int gear) {
    auto const& api=state_->require_powertrain();
    if(!api.select_gear(state_->world,id,gear))
        throw state_->module.error("engine.vehicle gear selection failed");
}
void VehicleService::configure_doors(std::uint32_t id,
    std::vector<NorthStarVehicleDoorConfig> const& doors) {
    auto const& api=state_->require_cabin();
    if(!api.configure_doors(state_->world,id,
            doors.empty()?nullptr:doors.data(),static_cast<std::uint32_t>(doors.size())))
        throw state_->module.error("engine.vehicle configure doors failed");
}
void VehicleService::set_door(std::uint32_t id,std::uint32_t n,bool open) {
    auto const& api=state_->require_cabin();
    if(!api.set_door(state_->world,id,n,open?1u:0u))
        throw state_->module.error("engine.vehicle door command failed");
}
NorthStarVehicleDoorState VehicleService::door(std::uint32_t id,std::uint32_t n) const {
    auto const& api=state_->require_cabin();
    NorthStarVehicleDoorState out{};out.size=sizeof(out);
    if(!api.door_state(state_->world,id,n,&out) ||
       out.size<sizeof(out)||out.index!=n)
        throw state_->module.error("engine.vehicle door state unavailable");
    return out;
}
std::uint32_t VehicleService::door_count(std::uint32_t id) const {
    return state_->require_cabin().door_count(state_->world,id);
}
VehicleService::VehicleService():state_(std::make_unique<State>()){}
VehicleService::~VehicleService()=default;
std::string VehicleService::provider_name() const {return state_->module.provider();}
std::string VehicleService::provider_source() const {return state_->module.source();}
void VehicleService::attach(NorthStarVehicleDefinition const& def) {
    if(!state_->api.attach(state_->world,&def))
        throw state_->module.error("engine.vehicle attach failed");
}
void VehicleService::detach(std::uint32_t id) {
    if(!state_->api.detach(state_->world,id))
        throw state_->module.error("engine.vehicle detach failed");
}
void VehicleService::control(std::uint32_t id,float throttle,float steering,float brake) {
    if(!state_->api.controls(state_->world,id,throttle,steering,brake))
        throw state_->module.error("engine.vehicle controls failed");
}
void VehicleService::waypoint(std::uint32_t id,float x,float z,float speed,float radius) {
    if(!state_->api.waypoint(state_->world,id,x,z,speed,radius))
        throw state_->module.error("engine.vehicle waypoint failed");
}
void VehicleService::step(float dt) {
    if(!state_->api.step(state_->world,dt))
        throw state_->module.error("engine.vehicle step failed");
}
NorthStarVehicleState VehicleService::state(std::uint32_t id) const {
    NorthStarVehicleState out{};out.size=sizeof(out);
    if(!state_->api.read(state_->world,id,&out) ||
       out.size<sizeof(out) || out.entity_index!=id)
        throw state_->module.error("engine.vehicle state failed");
    return out;
}
std::vector<NorthStarVehicleState> VehicleService::states() const {
    std::vector<NorthStarVehicleState> result;
    states_into(result);
    return result;
}
void VehicleService::states_into(std::vector<NorthStarVehicleState>& result) const {
    auto const n=count();
    result.resize(n);
    for(std::uint32_t ordinal=0;ordinal<n;++ordinal) {
        NorthStarVehicleState out{};out.size=sizeof(out);
        if(!state_->api.at(state_->world,ordinal,&out) ||
           out.size<sizeof(out) || out.entity_index>=16384)
            throw state_->module.error("engine.vehicle enumeration failed");
        result[ordinal]=out;
    }
}
std::uint32_t VehicleService::count() const{return state_->api.count(state_->world);}
bool VehicleService::supports_dynamics() const noexcept {return state_->extended!=nullptr;}
void VehicleService::configure(std::uint32_t id,NorthStarVehicleRig const& rig) {
    auto const& api=state_->require_dynamics();
    if(!api.configure(state_->world,id,&rig))
        throw state_->module.error("engine.vehicle configure rig failed");
}
std::uint32_t VehicleService::wheel_count(std::uint32_t id) const {
    return state_->require_dynamics().wheel_count(state_->world,id);
}
NorthStarVehicleWheelState VehicleService::wheel(std::uint32_t id,std::uint32_t n) const {
    auto const& api=state_->require_dynamics();
    NorthStarVehicleWheelState out{};out.size=sizeof(out);
    if(!api.wheel_state(state_->world,id,n,&out) ||
       out.size<sizeof(out))
        throw state_->module.error("engine.vehicle wheel state failed");
    return out;
}
std::uint32_t VehicleService::seat_count(std::uint32_t id) const {
    return state_->require_dynamics().seat_count(state_->world,id);
}
NorthStarVehicleSeatState VehicleService::seat(std::uint32_t id,std::uint32_t n) const {
    auto const& api=state_->require_dynamics();
    NorthStarVehicleSeatState out{};out.size=sizeof(out);
    if(!api.seat_state(state_->world,id,n,&out) ||
       out.size<sizeof(out) || out.index!=n)
        throw state_->module.error("engine.vehicle seat state failed");
    return out;
}
void VehicleService::occupy(std::uint32_t id,std::uint32_t n,std::uint64_t occupant) {
    auto const& api=state_->require_dynamics();
    if(!api.occupy_seat(state_->world,id,n,occupant))
        throw state_->module.error("engine.vehicle occupy seat failed");
}
void VehicleService::release(std::uint32_t id,std::uint32_t n,std::uint64_t occupant) {
    auto const& api=state_->require_dynamics();
    if(!api.release_seat(state_->world,id,n,occupant))
        throw state_->module.error("engine.vehicle release seat failed");
}
void VehicleService::set_contact(std::uint32_t id,
    NorthStarVehicleGroundContact const& contact) {
    auto const& api=state_->require_dynamics();
    if(!api.ground_contact(state_->world,id,&contact))
        throw state_->module.error("engine.vehicle contact update failed");
}
NorthStarVehicleMotionState VehicleService::motion(std::uint32_t id) const {
    auto const& api=state_->require_dynamics();
    NorthStarVehicleMotionState out{};out.size=sizeof(out);
    if(!api.motion_state(state_->world,id,&out)||out.size<sizeof(out))
        throw state_->module.error("engine.vehicle motion state failed");
    return out;
}
void VehicleService::resolve_collision(std::uint32_t id,
    NorthStarVehicleCollision const& event) {
    auto const& api=state_->require_dynamics();
    if(!api.resolve_collision(state_->world,id,&event))
        throw state_->module.error("engine.vehicle collision resolution failed");
}
}
