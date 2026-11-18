#pragma once
// Project-authored weapon grip profiles, imported from WeaponLab anatomy.
// Read once per native rig from shared/project VFS, never in a frame loop.
#include <northstarcpp/skeleton.hpp>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace northstarcpp::viewport_detail {
struct WeaponFingerProfile {
    std::vector<FingerJointBend> bends;
    // Authoring space: local gun +X lateral, +Y up, +Z barrel forward.
    std::array<math::float3,2> wrist_offset{};
    std::array<math::float3,2> socket_offset{};
    math::float3 support_offset{0.f,0.f,0.f};
    // Gun-local +Z is its sight/barrel axis. A point on the rear-sight
    // notch aligns to the camera ray during ADS; no gun-specific C++ offsets.
    math::float3 rear_sight{0.f,0.f,0.f};
    math::float3 front_sight{0.f,0.f,.18f};
    // Gun-local primary/support hand socket targets, in metres.
    math::float3 primary_grip{0.f,0.f,0.f};
    math::float3 support_grip{0.f,0.f,0.f};
    bool has_support_grip=false;
    float sight_eye_distance=.34f;
    // Project-authored ADS two-hand silhouette (metres, unitless stiffness).
    float ads_elbow_outward_bias=.39f,ads_elbow_drop_bias=.40f;
    float ads_support_hand_stiffness=1.f;
    float ads_forearm_visibility_guard=.235f;
};
struct WeaponGripLibrary {
    std::unordered_map<std::string,WeaponFingerProfile> profiles;
    WeaponFingerProfile const* find(std::string_view key) const noexcept {
        auto it=profiles.find(std::string(key));
        return it==profiles.end()?nullptr:&it->second;
    }
};
WeaponGripLibrary parse_weapon_grips_xml(std::string_view text);
} // namespace northstarcpp::viewport_detail
