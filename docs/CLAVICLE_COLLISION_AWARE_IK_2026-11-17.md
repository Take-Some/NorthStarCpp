# Abby / WeaponLab — collision-aware clavicle IK acceptance
Date: 2026-11-17

## Scope
Continue the existing NorthStarCpp canonical skeleton + two-handed pistol work without replacing the production editor or discarding unrelated working-tree changes.

## Implementation
- `engine/northstarcpp/modules/viewport/viewport_elbow_collision_ik.hpp`: bounded 32-sample elbow bend-direction search on the two-bone reachable circle, scoring torso/head capsule intrusion and anatomical bend. This file was introduced earlier in the work session.
- `engine/northstarcpp/modules/viewport/viewport_skeletal_animation.cpp`: if FPS elbow overlap after pole correction is greater than 30 mm, locate the actual ancestor clavicle by skeleton hierarchy, apply a single 21 mm outward + 12 mm body-forward scapular translation (24.19 mm vector) in clavicle-parent space, then re-solve the elbow against the **same wrist target**, without translating the firearm or the ADS sight line.
- One clavicle correction per hand per evaluated frame to prevent accumulation through repeated ADS IK calls.
- `engine/northstarcpp/modules/animation/skeleton.cpp`: synchronize importer-verified bind-identical mirrored helper nodes with the primary adjusted joint, avoiding divergence in the GPU/CPU skin palette.
- `tests/viewport_skeletal_pipeline_smoke.cpp`: enforce 35 mm maximum elbow torso/head capsule overlap and add yaw ±0.45 at pitch −1.10; verify clavicle excursion <=27 mm, real adjustment >10 mm, and preserved upper/lower arm segment lengths (<3 mm delta).

## Verified QA
Command root: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
- Build: `cmake --build out/build --target northstarcpp_viewport_skeletal_pipeline_smoke northstarcpp_viewport_pistol_ads_smoke northstarcpp_weaponlab_grip_retarget_smoke -j 4` — PASS.
- `northstarcpp_viewport_skeletal_pipeline_smoke.exe engine/northstarcpp/resources/sharedAssets` — PASS, including strict <35 mm gate.
- `northstarcpp_viewport_pistol_ads_smoke.exe engine/northstarcpp/resources/sharedAssets` — PASS.
- `northstarcpp_weaponlab_grip_retarget_smoke.exe engine/northstarcpp/resources/sharedAssets` — PASS.
- At pitch −1.10 and yaw 0: left elbow 0.03457 m overlap; right elbow 0; wrist intrusions 0.00472 / 0; gun proxy 0.
- At pitch −1.10 and yaw ±0.45: identical elbow overlap 0.03457 m; acceptance PASS.
- Clavicle displacement at pitch −1.10: 0.02419 m; both arm segment lengths preserved.
- `git diff --check` on touched source and pipeline test files: PASS.

## Editor visual QA / limitations
- Qt editor opened the real `Projects/AbbyPistolDemo/main.nscn` scene and produced `out/qa/abby_editor_clavicle_check.png` using isolated QA settings and `--qa-capture-editor` in offscreen mode.
- This screenshot shows **Proxy View**, with bounding cubes rather than native skinned assets. It is NOT evidence that the updated pistol grip or elbow silhouette is visually correct.
- A running Editor Engine GPU view (or Play-in-Editor) must show the updated right/left arms from front, side and aiming-down at pitch −1.10; check no elbow intersection, no twisting skinning artifacts, correct support-hand contact, visible front/rear sights, and no arm-crossing.
- The skeletal smoke logs have separate full-body CPU skin mesh stress metrics; a passing skeleton IK test is not equivalent to full visual acceptance.
- No source commit/push, no production `dist` replacement, and no final visual acceptance claimed.

## Artifacts
- `out/qa/clavicle_guard_build.log`
- `out/qa/clavicle_guard_pipeline.log`
- `out/qa/clavicle_guard_ads.log`
- `out/qa/clavicle_guard_retarget.log`
- `out/qa/clavicle_yaw_build.log`
- `out/qa/clavicle_yaw_pipeline.log`
- `out/qa/abby_editor_clavicle_check.png`
