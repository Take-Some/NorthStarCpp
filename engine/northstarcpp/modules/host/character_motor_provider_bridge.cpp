#include "character_motor_provider_bridge.hpp"
#include <climits>
namespace northstarcpp::gameplay::physics_bridge {
Result transfer(NorthStarPhysicsCharacterApi const* character,
    NorthStarPhysicsAiMotorApi const* ai,void* world,std::uint32_t index,
    NorthStarPhysicsCharacterMotor const& tuning,bool release_ai) noexcept {
    if(!character || character->abi_version!=NORTHSTARCPP_PHYSICS_CHARACTER_ABI ||
       !character->configure_motor || !character->possess_body)
        return Result::MissingProvider;
    if(!world || index==UINT32_MAX)return Result::InvalidTarget;
    if(!character->configure_motor(world,index,&tuning))
        return Result::ConfigurationRejected;
    // Ownership arbitration: the new player must not inherit a prior NPC's
    // planar motor command. This is an explicit provider operation.
    if(release_ai && ai && ai->abi_version==NORTHSTARCPP_PHYSICS_AI_MOTOR_ABI &&
       ai->clear_intent && !ai->clear_intent(world,index))
        return Result::AiReleaseRejected;
    if(!character->possess_body(world,index))
        return Result::PossessionRejected;
    return Result::Ok;
}
Result release(NorthStarPhysicsCharacterApi const* character,void* world) noexcept {
    if(!character || character->abi_version!=NORTHSTARCPP_PHYSICS_CHARACTER_ABI ||
       !character->possess_body)return Result::MissingProvider;
    if(!world)return Result::InvalidTarget;
    return character->possess_body(world,UINT32_MAX)?
        Result::Ok:Result::UnpossessionRejected;
}
char const* message(Result result) noexcept {
    switch(result) {
    case Result::Ok:return "CharacterMotor transfer complete";
    case Result::MissingProvider:return "Physics provider lacks CharacterMotor ABI";
    case Result::InvalidTarget:return "Invalid CharacterMotor world or Pawn index";
    case Result::ConfigurationRejected:return "CharacterMotor configuration rejected";
    case Result::AiReleaseRejected:return "NPC movement intent release rejected";
    case Result::PossessionRejected:return "Physics provider rejected Pawn possession";
    case Result::UnpossessionRejected:return "Physics provider rejected Pawn unpossession";
    }
    return "Unknown CharacterMotor bridge result";
}
} // namespace northstarcpp::gameplay::physics_bridge
