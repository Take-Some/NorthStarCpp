// Independent, versioned CharacterMotor entity contract.
#pragma once
#include <northstarcpp/player_blueprint_core.hpp>

namespace northstarcpp::player_blueprint {
struct Motor {
    // Humanoid baseline measured from Abby's stance-foot YCD trajectories.
    // Unmodified running is 2.95 m/s, Shift sprint is 4.44 m/s;
    // scripted Ctrl precision movement scales running to 1.48 m/s.
    // Project-specific CharacterMotor components always override defaults.
    float walk_speed=2.95f,run_speed=4.44f,jump_speed=5.35f;
    float acceleration=18.f,air_control=.35f,gravity_scale=1.f;
};
inline std::string serialize_motor(Motor const& motor) {
    std::string bytes;
    for(float x:{motor.walk_speed,motor.run_speed,motor.jump_speed,
                 motor.acceleration,motor.air_control,motor.gravity_scale})
        player_character::detail::pushfloat(bytes,x);
    if(!player_character::detail::motor_valid(
        nullptr,player_character::version,bytes.data(),bytes.size()))
        throw std::invalid_argument("Invalid character motor tuning");
    return bytes;
}
inline std::uint64_t motor_entity(Scene const& scene,std::uint64_t pawn) {
    std::uint64_t id=0;
    for(auto const& o:scene.objects)
        if(o.node.parent==pawn)
            for(auto const& c:scene.components)
                if(c.entity_id==o.node.id &&
                   c.type==player_character::motor_type &&
                   c.version==player_character::version &&
                   player_character::detail::motor_valid(
                       nullptr,c.version,c.payload.data(),c.payload.size())) {
                    if(id && id!=o.node.id)
                        throw std::invalid_argument(
                            "Pawn has multiple CharacterMotor entities");
                    id=o.node.id;
                }
    return id;
}
inline Motor motor(Scene const& scene,std::uint64_t pawn) {
    Motor value;
    auto const entity=motor_entity(scene,pawn);
    // Legacy Pawn-owned motor data remains readable during migration.
    auto const owner=entity?entity:pawn;
    for(auto const& c:scene.components)
        if(c.entity_id==owner &&
           c.type==player_character::motor_type &&
           c.version==player_character::version) {
            if(!player_character::detail::motor_valid(
                nullptr,c.version,c.payload.data(),c.payload.size()))
                throw std::invalid_argument("Invalid CharacterMotor data");
            auto const* b=c.payload.data();
            value.walk_speed=player_character::detail::getfloat(b,0);
            value.run_speed=player_character::detail::getfloat(b,4);
            value.jump_speed=player_character::detail::getfloat(b,8);
            value.acceleration=player_character::detail::getfloat(b,12);
            value.air_control=player_character::detail::getfloat(b,16);
            value.gravity_scale=player_character::detail::getfloat(b,20);
            return value;
        }
    return value;
}
inline void set_motor(Scene& scene,std::uint64_t motor_id,Motor const& value) {
    assign(scene,motor_id,player_character::motor_type,serialize_motor(value));
}
} // namespace northstarcpp::player_blueprint
