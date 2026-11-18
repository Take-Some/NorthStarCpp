# NorthStarCpp — Unreal Engine 5.3.2 architecture comparison (2026-11-18)

## Reference and legal boundary

Reference: https://github.com/Mccrae123/Unreal-Engine-5.3.2 (public, unofficial mirror, branch `release`).
Reference documentation: Epic Unreal Gameplay Framework and UE 5.3 Release Notes.

The reference repository's `LICENSE.md` explicitly points to the Unreal Engine EULA.
NorthStarCpp must **not** vendor, copy, translate, or relicense Unreal Engine code.
All changes in this work item are independent NorthStarCpp implementations of documented architectural concepts.

## Current foundation (verified in the NorthStarCpp working tree)

| UE architectural concept | NorthStarCpp counterpart | State |
| --- | --- | --- |
| Actor / SceneComponent | Scene V3 Entity + versioned component records / FLECS | Existing |
| Pawn / Character | `northstar.gameplay.pawn` plus independent capsule | Existing |
| PlayerController / Possess | `gameplay::PlayerController`, `PhysicsScene::possess` and Jolt provider | Existing |
| CharacterMovement | independent CharacterMotor entity, settings and Jolt motor | Existing |
| CameraComponent / SpringArm | independent Camera entity and `player_blueprint::Camera` | Existing |
| PlayerCameraManager | `CameraDirector` with smoothing, collision and cinematic shots | Existing |
| SkeletalMesh / AnimInstance | independent visual entity and animation graph | Existing, runtime visual QA incomplete |
| Components tree / Details | `qt_player_blueprint_editor.hpp`; Qt inspector and Outliner | Existing |
| ViewTarget (independent of Pawn) | explicit stable-ID target in `PlayerController` and `PhysicsScene` | Added API in this change |

## Changes in this iteration

1. `PlayerController` now offers `set_view_target`, `reset_view_target`,
   `view_target`, `view_target_index`, and a clear distinction between camera
   override and possessed Pawn. View target may persist while unpossessed.
   Invalid targets are rejected without modifying the existing override.
2. `reconcile` revokes possession if the stable-ID Entity exists but its
   validated Pawn component has disappeared; deleted camera override IDs
   are cleared separately.
3. `PhysicsScene` exposes the same View Target operations without changing
   Jolt's physical possession or its provider ABI.
4. Qt Player Blueprint editor disables Third Person-only spring-arm fields
   in First Person view, preserves their authored values, and explains the
   distinction. Existing component tabs, undo/redo, serialization, and
   asset types remain unchanged.
5. Added checks to the Gameplay, Physics and Qt Player Blueprint smoke sources.

## Engineering acceptance / build

- GCC 16.2.0 `-fsyntax-only` on `tests/player_blueprint_smoke.cpp`: PASS
- GCC 16.2.0 `-fsyntax-only` on `engine/northstarcpp/modules/host/physics.cpp`: PASS
- GCC 16.2.0 with Qt6Widgets headers `-fsyntax-only` on
  `tests/player_blueprint_qt_smoke.cpp`: PASS
- `git diff --check` on changed gameplay and QA sources: PASS
- Full CMake preset `release`: configure succeeds, but Ninja regeneration
  fails: `failed recompaction: Permission denied`. No full link or on-screen
  integration QA should be asserted until the build-directory lock is resolved.
- Do not replace the installed Editor/Player or publish a release from this change.
- No Git commit or push was requested or performed.

## Remaining parity work, in priority order

1. **P0 — Camera runtime integration.** Route explicit
   `PhysicsScene::view_target()` into the live CameraDirector selection and
   into the scripting layer, with clean camera-cut semantics. Current change
   exposes the API but does *not* automatically switch the rendered camera.
2. **P0 — Pawns and AI Controllers.** Single `Controller` interface for human
   and AI ownership; transactional possess/unpossess with notifications and
   ability to swap control without duplicating physical or visual entities.
3. **P0 — Animation continuity.** Anim graph state, skeleton/pose validation,
   root motion collision and blend preservation. No bind-pose fallback on
   ordinary state transitions, especially for NPCs.
4. **P1 — Input actions.** Project-authored action maps and contexts with
   input focus, modal UI capture, rebinding and per-Controller routing.
5. **P1 — Editor workflow.** Unified actor creation, per-component property
   customizations, filtering, multi-edit, Outliner context actions,
   drag/drop previews, Play-in-Editor state and keyboard remapping.
6. **P1 — World gameplay.** GameMode-style startup rules, PlayerState/
   checkpoint save data, camera ownership and spawn-point selection.
7. **P2 — Visual QA / parity.** Side-by-side recordings of FPS/TPS transitions,
   spectator view, vehicle possession, NPC locomotion, undo/redo and scene reload.

Do not duplicate the existing scene, animation, camera or physics providers.
Make each gameplay feature available through provider-neutral `engine.*` contracts
and preserve stable Scene V3 Entity IDs.
