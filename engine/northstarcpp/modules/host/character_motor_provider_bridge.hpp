// CharacterMotor provider-neutral handoff. The physics ABI remains unchanged.
#pragma once
#include <northstarcpp/physics_plugin.h>
#include <cstdint>
namespace northstarcpp::gameplay::physics_bridge {
enum class Result : std::uint8_t {
    Ok, MissingProvider, InvalidTarget, ConfigurationRejected,
    AiReleaseRejected, PossessionRejected, UnpossessionRejected
};
// No mutable engine/scene state, allocation, callbacks, or implicit fallback.
// The caller owns the transaction and commits Controller state on success.
Result transfer(NorthStarPhysicsCharacterApi const* character,
    NorthStarPhysicsAiMotorApi const* ai,void* world,std::uint32_t index,
    NorthStarPhysicsCharacterMotor const& tuning,bool release_ai) noexcept;
Result release(NorthStarPhysicsCharacterApi const* character,void* world) noexcept;
char const* message(Result result) noexcept;
} // namespace northstarcpp::gameplay::physics_bridge
