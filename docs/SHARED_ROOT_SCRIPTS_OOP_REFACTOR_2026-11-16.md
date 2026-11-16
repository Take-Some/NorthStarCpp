# NorthStarCpp — shared script root OOP refactor

Date: 2026-11-16
Scope: `sharedAssets/scripts`, matching SDK declarations,
native static-import whitelist and targeted regression tests.

## Intent

Make engine-owned roots consistent, maintainable and project-independent
without moving gameplay (missions, HUD semantics, health, inventory,
weapons, NPC decisions) into native engine code.

## Architecture after refactor

- `NorthStarSDK`: one bootstrap registry for provider-neutral facades.
- `Script`: one start/update/stop lifecycle and owned event scope;
  reentrant installation is forbidden, `uninstall()` is safe and idempotent.
- `PlayerScript`: reusable Pawn-active/paused/config getters.
- `EventScope`: class instead of per-call object closures, with disposal.
- `InputAction`: reusable keyboard/gamepad edge detection;
  `Input.axis` combines movement controls without repeated code.
- `CharacterController`: cached animation-name lookup.
- `Prefab`: validate + dependency-order all nodes before spawning;
  cyclic/missing parents rejected; partial provider failures rolled back.
- `UIRoot`: independent, project-overridable script-side builder over the
  universal `engine.ui.apply()` contract.

Shared roots remain independent:
`playerActor`, `playerSession`, `playerMovement`,
`playerFootsteps`, `playerInteraction`, `playerCharacter`,
`playerCamera`, `playerLook`, `audio/footsteps`,
`character/Character`, input enums, Scene handles and UI.

The default player configuration is a class that computes its
`hasPawn` state once and does not inspect `SceneObject::player`.
`Scene.findPlayer()` is a deprecated alias for `Scene.findPawn()`.
Physics and ownership continue to use stable Entity IDs through the
native PlayerController.

## Compatibility

- Named `@northstar/engine` imports keep their existing names.
- Added `PlayerScript` and `InputAction` to both SDK module exports and
  the C++ import allowlist in `modules/host/scripting.cpp`.
- Default roots still load in the established dependency order.
- Grouped project overrides and existing flat aliases remain supported.
- Camera now uses the same `Script.install()` lifecycle as other roots;
  the existing `NorthStar.camera.installDefaultScript` entrypoint remains.
- ScriptRoot and EventScope do not create an alternative native event bus.
- No production executable was replaced, no commit/push was performed.

## Targeted checks

| Check | Result |
| --- | --- |
| JS syntax, shared root files | PASS |
| Generated keyboard/gamepad TypeScript declarations | PASS |
| C++ Player debug build after allowlist change | PASS |
| Shared root OOP Node test | PASS |
| Prefab dependency/cycle/rollback Node test | PASS |
| Native Pawn footstep event and NAC playback Node test | PASS |
| Scene SDK Node test (controlled Pawn, not legacy flag) | PASS |
| UI root and document Node tests | PASS |
| First Person Protocol deterministic mission script | PASS |
| CTest group | 7 / 7 PASS |
| Native V8/Jolt/D3D12 UI root and project override | PASS |
| Manual end-to-end game walkthrough | NOT VERIFIED |
| Full workspace/release regression | NOT VERIFIED |

Root reference and usage examples are maintained in
`sharedAssets/scripts/README.md`.

## Remaining work

1. Some older game-specific tests still model `player=true` and the
   previous demo scene. They need separate fixture migration to the
   component-authored Pawn before a complete workspace-wide regression
   may be declared green.
2. Unrelated current First Person Protocol game assets have previously
   produced `Dynamic bodies must use physics` during a direct game run;
   isolated shared-root runtime testing does not reproduce that failure.
3. A real performance comparison requires profiled native CPU frame times
   before/after, with input device and full level loaded. No FPS or p95/p99
   improvements are claimed from source-level cleanup alone.
