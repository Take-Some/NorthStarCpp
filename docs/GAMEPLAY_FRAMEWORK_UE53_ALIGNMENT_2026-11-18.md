# NorthStarCpp Gameplay Framework — UE5.3 architectural alignment

> **Document status:** Earlier implementation snapshot; newer acceptance evidence and remaining production gates are maintained in [Gameplay Production Gates](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md).

Date: 2026-11-18. Reference: https://github.com/Mccrae123/Unreal-Engine-5.3.2 (release branch); architectural cross-check with Unreal's documented Controller, Pawn, Character and Camera Manager contracts.

## Principles

- Controller identity and lifetime are distinct from Pawn identity.
- A controller may be unpossessed. Invalid/removed Pawn IDs must not be retained.
- A Pawn is a stable authored Scene V3 Entity ID, never a transient Jolt index or Flecs handle.
- PlayerController and AIController share the same possession state and validation contract.
- Physics provider owns actual character movement/collision; gameplay controllers emit intent.
- Camera view targets can change without transferring Pawn possession.
- CharacterMotor, Camera and SkeletalMesh are independently-authored child Scene Entities in a Player Blueprint. The Pawn remains their logical/physical root.
- Animation follows actual physics movement; Root Motion enters collision-resolved motor, never an unchecked visual teleport.

## Changes

1. Added `gameplay_possession_state.hpp`: common provider-neutral PossessionState, stable Pawn ID, revision counter, validated and idempotent possession, cleanup on missing Pawn.
2. Refactored `player_controller.hpp` and `ai_controller.hpp` to delegate to the shared component rather than each storing their own Pawn ID.
3. Added `configure_view_target_camera` in `player_camera_runtime.hpp`; native CameraDirector can be configured for a Pawn or an authored child Camera Entity while leaving the possessed Pawn unchanged.
4. Updated `window_script_player_bindings.cpp` so `engine.player.setViewTarget()` / `resetViewTarget()` synchronize camera settings.
5. Extracted `physics_player_possession.inc`, `physics_player_root_motion.inc`, `physics_ai_motor_bridge.inc` from the monolithic physics.cpp. They remain included in a single translation unit until the private State/pimpl dependency is moved into a dedicated shared header.
6. Extracted `window_runtime_ai_controllers.inc` from the FPS camera bridge: AI navigation/motor/animation routines are no longer embedded in camera implementation.
7. Harmonized Input Actions XML writer across bundled and system Boost implementations by using the default PropertyTree XML writer.
8. Extended `gameplay_framework_smoke.cpp` with controller lifecycle, invalid Pawn rejection, idempotent Possess, revision and cinematic View Target tests.
9. Extended `player_blueprint_physics_smoke.cpp` with independent Camera Entity target and target-reset tests.

## Build and QA

- Removed stale build-registry identity for the previously deleted redundant prefab `prefabs/weapon/pistol_attached.nprefab` using `--prune` (no source asset recreated or deleted by the command).
- Rebuilt the shared NSP, `northstarcpp_player.exe`, `northstarcpp_editor_qt.exe`, gameplay and physics smoke executables and NavMesh smoke.
- CTest passed: navigation_navmesh, gameplay_framework, nsp_verify, asset_pipeline_identity, player_blueprint_physics (5/5).
- Shared Engine Asset Registry: 223 source assets, 21 direct textual edges. Archive contains 224 entries including generated metadata/resource.
- No production dist replacement, commit or push.

## Not yet complete

- Separate all PhysicsScene::State-dependent gameplay .inc routines into compiled translation units with an isolated provider bridge/pimpl ABI.
- Complete Jolt/Bullet provider parity and behavior with possession transfer, camera self-collision, moving platforms and network authority.
- Refactor runtime App window lifecycle into separate controller/AI/camera services and test all gameplay modes.
- Add explicit controller-to-pawn ownership arbitration for multiple players/AI, event notifications OnPossess/OnUnPossess, and entity-destruction subscriptions.
- Improve NavMesh multi-level pathfinding, dynamic replanning, locomotion-root-motion/IK continuity and GPU visual acceptance tests.
- Do not claim bit-for-bit equivalence to Unreal or full gameplay production parity: the work adopts UE-style responsibilities without copying Unreal source.
