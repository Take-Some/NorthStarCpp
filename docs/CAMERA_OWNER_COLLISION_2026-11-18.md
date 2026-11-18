# Camera-versus-player collision regression — 2026-11-18

## User-observed defect
The first camera fix removed head geometry from a full-body actor. In the
Abby pistol scene, that left a headless visible character rather than keeping
the camera from intersecting the actual skinned head.

## Corrected contract
- Preserve every original YDD mesh triangle and YMT joint. Never delete or
  cull the player's face, eyes, hair, teeth, or head to resolve camera overlap.
- Keep Jolt CharacterMotor, its gameplay capsule, collision channels, and
  rendered skeleton independent.
- Use the local possessed Pawn as camera-owner, not a specific file or actor
  name. The owner may be authored as YDD/YDR or an entity with
  northstar.gameplay.skeletal_mesh referencing an NPREFAB.
- Camera rendering applies an owner capsule clearance before GPU draw.
  CameraDirector rechecks after blends, camera shake and owner/world collision.
- When an obstacle compresses camera distance, prefer another physically
  unobstructed angle around the player rather than moving through the head.
  The camera leaves another NPC's geometry and editor free camera untouched.
- Owner child visuals must not count as world occluders for the owner's camera.

## Changed code
- modules/viewport/viewport_draw.cpp: removed all prior owner-head masks,
  actual local Pawn camera clearance and full-body presentation.
- modules/host/camera.cpp: owns camera collision policy during mode transitions,
  checks skinned/prefab visual parentage, and ignores owner visual for fallback
  static world line-of-sight checks.
- modules/api/include/northstarcpp/camera_owner_clearance.hpp:
  provider-agnostic capsule projection with candidate alternatives.
- tests/camera_owner_clearance_smoke.cpp: pure geometry, no engine asset.
- tests/camera_smoke.cpp: possession with original child YDD as well as
  northstar.gameplay.skeletal_mesh prefab, transitions first/third/first.

## Verified
- CMake build --target northstarcpp_player: PASS.
- CMake build --target northstarcpp_editor_qt: PASS.
- CMake build --target libviewport.a: PASS.
- Camera Smoke (rebuilt, owned YDD and prefab): EXIT 0.
- Jolt Camera Physics Smoke (rebuilt): EXIT 0, Jolt 5.6.0.
- Viewport Skeletal Pipeline, Skeletal Animation: EXIT 0.
- Camera Owner Capsule Clearance Smoke: EXIT 0.
- AbbyPistolDemo player --warp --smoke: process EXIT 0; engine.startup
  READY, 20 startup checks passed, zero startup warnings/errors.

## Build-system repair
A pre-existing deleted duplicate prefab, pistol_attached.nprefab, blocked
shared asset NSP generation. The local out/q7 asset identity registry was
reconciled by the supported --prune --apply path (223 assets, 21 edges); the
duplicate prefab was NOT reintroduced. Only the local build cache/registry
was updated.

## Outstanding visual acceptance
The automated WARP player smoke completed successfully, but it did not
produce a screenshot at the requested output path. Therefore, the exact
close-up captured in the user's screenshot has not been visually re-shot.
Inspect actual output during standing, walking, aiming, crouch, third-person
obstacle compression, and transitions; assess any camera/gun parallax.

## Release handling
Built binaries are in out/q7. Production package/dist not overwritten.
No commit or push, and no redistribution of game-derived assets.

## ADS visibility regression and fix (follow-up, 2026-11-18)

The previous local-player collision change used a first-person head exclusion
radius of 0.54 m. Including the 0.02 m safety distance, this displaced the
viewer approximately 0.56 m in front of Abby's original eye and beyond the
reachable two-hand pistol ADS pose. The actual imported model/skeleton smoke
confirmed that the pistol rear sight then appeared **0.186 m behind** the
camera. This explains the gun disappearing during aim-down-sights.

Fixed by a shared `camera_collision::first_person_head_radius = 0.20f`
(0.22 m effective eye offset) in BOTH `CameraDirector` and `Viewport`.
Third-person collision radii and unchanged full-head mesh rendering remain.
The original single YDD pistol, original YMT wrists, real bone attachment,
and projectile/physical muzzle semantics are preserved.

Real Abby model and pistol YDD/YMT smoke checks:
- Normal eye: rear sight depth +0.295 m.
- Corrected FPS eye +0.22 m: rear sight depth +0.295 m.
- Former oversized FPS eye +0.55 m: rear sight depth -0.186 m (invalid).
- Corrected eye with nonzero yaw/pitch: rear-sight depths +0.257..+0.295 m,
  lateral sight errors 0..0.012 m, four rotated samples pass bounded QA.
- FPS owner collision, third-person transition, third-party Jolt physics,
  and the actual pistol ADS source-asset smoke all PASS.
- Updated Player and Qt Editor rebuilt successfully under `out/q7`.
- WARP smoke Player startup READY (20 checks, zero warnings/errors).

The WARP startup smoke does not replace a captured on-screen ADS acceptance
frame. Validate actual weapon visibility and near-plane behavior visually
before shipping the production dist. No commit or push was performed.
