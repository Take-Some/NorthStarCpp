// Optional engine.vehicle Powertrain ABI v4, implemented in the native
// provider using the same validated world and entity lifetime as v1/v2/v3.
int configure_powertrain(void* world,std::uint32_t id,
    NorthStarVehiclePowertrainConfig const* config) noexcept {
    return guarded([&] {
        require(config && northstarcpp::vehicle_schema::valid_powertrain(*config),
                "Invalid powertrain specification");
        auto& vehicle=get(checked(world),id);
        require(std::hypot(vehicle.forward_speed,vehicle.lateral_speed)<.15f,
                "Powertrain cannot be replaced while moving");
        sim::Powertrain updated;
        sim::configure_powertrain(updated,*config);
        vehicle.powertrain=updated; // commit only after all checks
    });
}
int powertrain_state(void* world,std::uint32_t id,
    NorthStarVehiclePowertrainState* result) noexcept {
    return guarded([&] {
        require(result && result->size>=sizeof(*result),
                "Missing powertrain state buffer");
        auto const& vehicle=get(checked(world),id);
        require(vehicle.powertrain.enabled,"Powertrain has not been configured");
        *result=vehicle.powertrain.state;
    });
}
int powertrain_select_gear(void* world,std::uint32_t id,
    std::int32_t gear) noexcept {
    return guarded([&] {
        auto& vehicle=get(checked(world),id);
        sim::select_gear(vehicle.powertrain,gear,vehicle.forward_speed);
    });
}
NorthStarVehiclePowertrainApi const powertrain_api{
    NORTHSTARCPP_VEHICLE_POWERTRAIN_ABI,
    sizeof(NorthStarVehiclePowertrainApi),
    configure_powertrain,powertrain_state,powertrain_select_gear
};
NorthStarModule const powertrain_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_VEHICLE,"native","4.0.0",
    NORTHSTARCPP_VEHICLE_POWERTRAIN_ABI,sizeof(powertrain_api),
    &powertrain_api,northstarcpp::module_provider::last_error
};
