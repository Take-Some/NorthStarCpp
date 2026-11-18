# AbbyPistolDemo — ADS hand/grip sightline visibility

Date: 2026-11-17. Scope: NorthStarCpp first-person Abby skeleton and authored pistol.

## User-observed defect
Targeting and muzzle-origin hits are correct, but in first-person ADS the right sleeve/forearm and hand cross the view, blocking the iron sights. The previous smoke tests only checked hand/socket position and did not check arm silhouette.

## Changes
1. **Skeletal IK** — `viewport_skeletal_animation.cpp`
   - Match first-person lateral elbow/grip direction to the physical shoulder handedness of mirrored YMT rigs.
   - During ADS, place elbow-pole preferences below and outside the firearm; keep both physical wrist targets, hand anatomy and `r_hand_prop_attachment` binding.
   - Trigger a secondary bounded two-bone correction when the forearm approaches the actual camera sight ray, not only when its forward depth is insufficient.
   - Accept corrective poses only if sightline clearance or forearm depth improves, torso intrusion remains bounded, and wrist IK residual stays below 12 mm.
2. **Camera ray clearance** — `viewport_elbow_collision_ik.hpp`
   - New `ads_forearm_sight_clearance()` samples the sleeve-bearing 80% of each forearm and computes minimal distance to the camera line.
   - The final ADS elbow-pole candidate search penalizes forearm intrusions into the optical corridor without moving the wrist or gun.
3. **Weapon fitting** — `characters/abby/animations/weapon_grips.xml`
   - Increase the rear-notch-to-eye distance from 0.34 m to **0.39 m**. This improves first-person perspective and arm extension while remaining reachable in extreme camera pitch tests. The attempted 0.43 m failed steep-angle IK and was **not retained**.
   - Keep corrected rear/front sight coordinates from actual `pistol.ydd` geometry. Update the WeaponLab import script to persist the 0.39 m value.
4. **No separate arms-only duplicate** — the temporary ADS `first_person_arms` experiment caused a genuine ~0.79 m sightline detachment and was **reverted**. Both hip and ADS retain a single `first_person_body` skinned pawn.

## Verification
- CMake `northstarcpp_viewport_pistol_ads_smoke`: built and **PASS** for seven yaw/pitch cases.
- C++ smoke now asserts optical ray clearance for **both forearms** above **0.14 m** (bone-line screening) and validates iron sight, wrist IK and independent hand anatomy. Sample near-centred right/left forearm clearances are ~0.165 m/~0.170 m at yaw/pitch 0/0. Note that mesh sleeves are wider than bones; this is not a pixel-by-pixel occlusion test.
- `tests/abby_pistol_demo_smoke.cjs`: **PASS**.
- `tests/weapon_ballistics_shared_smoke.cjs`: **PASS**.
- `tests/weapon_controller_aim_smoke.cjs`: **PASS**.
- `tests/player_look_ads_smoke.cjs`: **PASS**.
- CMake `northstarcpp_player`: completed successfully, exit code 0; it includes the updated viewport native code.
- A subsequent D3D12 WARP Player launch was blocked at unrelated `NorthStar D3D12 sky` shader compilation (`X3014`, line 73, numeric-type constructor arguments). Vulkan WARP was also unavailable (`Vulkan 1.3 graphics+present device with dynamic rendering/sync2 unavailable`). Therefore no honest claim can be made that the **new** arm silhouette has been visually accepted in a complete gameplay frame.
- The earlier first-person gameplay capture showed a large sleeve occlusion. An attempt to fix it by switching to arms-only mode failed and was reverted; no residual sight detachment is intentionally shipped.

## Next acceptance
Run a current real GPU Player frame when the independent D3D12 sky shader error is resolved; capture ADS from camera with and without recoil, verify both hands stay low/outside sight, no sleeves in crosshair or rail, and that actual camera ray and muzzle hit agree over distances 2/5/20/50 m. Compare D3D12/Vulkan. Do not move the muzzle, fake a tracer, or apply camera-only model offsets.

No commit, push or production `dist` replacement performed.
