// Gameplay access to the independent motor child entity (legacy Pawn fallback).
#pragma once
#include <northstarcpp/player_blueprint_motor.hpp>
namespace northstarcpp::gameplay {
using CharacterMotorSettings=player_blueprint::Motor;
inline CharacterMotorSettings motor_settings(Scene const& scene,
    std::uint64_t pawn) { return player_blueprint::motor(scene,pawn); }
} // namespace northstarcpp::gameplay
