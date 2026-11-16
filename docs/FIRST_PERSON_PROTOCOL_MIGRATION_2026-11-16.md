# NorthStar: First Person Protocol — Player Framework game migration
Date: 2026-11-16. Scope: current `NorthStarCpp` worktree.

## Scope

Upgrade `Projects/NorthStarFirstPersonPlatform` from the legacy first-person
physics sandbox to a playable training-course vertical slice using the
component-based Pawn, stable-ID PlayerController, Character Motor, Camera
Rig and Jolt. No legacy `SceneObject::player` ownership is reintroduced.

## Updated runtime contracts

- `engine.player.controlledEntity()` -> controlled stable Entity ID or null.
- `engine.player.controlledIndex()` -> current scene index or -1.
- `engine.player.possess(entityId)` -> switch to a valid authored Pawn.
- `engine.player.unpossess()` -> clear ownership and stop stale movement.
- `engine.player.sprint(active)` -> request Jolt Motor speed change.
- `@northstar/engine` exports corresponding `Player` getters/methods.
- `Scene.findPawn()` resolves controller ownership, not serialized flags.
- Initial camera eye height and default FOV resolve from authored Camera Rig;
  possession refreshes both and resets camera interpolation.

## Project

- `northstarcpp.project.json`: Jolt physics preference and updated game
  metadata.
- `main.nscn`: current NEF8 format, 41 authored entities, 8 components,
  Auto Possess Local Pawn ID 1; start position (4, 1.2, -12).
- `scripts/playerConfig.js`: first-person initial camera, Shift sprint,
  optional Abby character resources retained.
- `scripts/player/playerMovement.js`: project-owned input mapping and
  Jolt-bound movement/jump/sprint; independent of character visuals.
- `scripts/main.js`: four-stage training course:
  navigate cyan beacon, displace physics crate, navigate orange beacon,
  reach extraction. Entity spawning and objective logic use script SDK.
- `run.cmd` and `verify.cmd`: explicitly select Jolt and D3D11, point to
  updated development Player, with `NORTHSTARCPP_PLAYER_EXE` override.
- `README.md`: controls, architecture, gameplay and test instructions.
- `tools/migrate_fps_scene.cpp`: one-time importer/converter test utility
  with optional spawn Y argument, preserves original scene.

Scene backups:
`Projects/NorthStarFirstPersonPlatform/backups/main-pre-pawn-migration-2026-11-16.nscn`
and
`Projects/NorthStarFirstPersonPlatform/backups/main-before-ground-spawn-2026-11-16.nscn`.

## Verification evidence

| Check | Result |
| --- | --- |
| Build C++ Player and Qt Editor (Debug) | PASS |
| Project `verify.cmd`: WARP scene check + runtime-init | PASS |
| Qt Editor `--smoke` | PASS |
| `git diff --check` scoped to changed files | PASS |
| `node tests/first_person_protocol_logic_smoke.cjs` | PASS |
| `ctest -R northstarcpp_first_person_protocol_logic` | PASS (1/1) |
| Real Player/Jolt/V8/Flecs `first_person_protocol_runtime_smoke.py` | PASS (48 entities, 4 update frames, Pawn ID 1, native Unpossess/Repossess and Sprint) |
| Native Jolt `player_framework_runtime_smoke.cpp` | PASS (120 physics steps, 15.302 m X movement, ownership restored) |
| NEF8 scene migration / reload | PASS (41 entities, Pawn ID 1) |
| Manual full mission traversal and visual parity | NOT YET VERIFIED |

During verification, the project initially failed because direct `--scene`
does not apply the project manager's preferred physics provider. Jolt is
selected explicitly in the launchers and QA environment. The first temporary
V8 test also lacked `--shared-assets`; QA now passes the required mount.
A spawn height of Y=11.515491 was corrected to Y=1.2 to put the Pawn near
the platform surface; the mission crate was placed at Y=1.25.

An optional asset warning remains for
`prefabs/tree_animate/textures/tree_animate.ytd` and may affect that
material's appearance, not the runtime startup.

## Outstanding product work

- Manual end-to-end level traversal, verify jump / collision feel and
  mouse capture, verify third-person Abby animation and model occlusion.
- Actual in-world mission HUD (now objectives appear in the window title
  and logs), sound design, stage dressing, checkpoints/save progression.
- Longer CPU/GPU frame-time and memory regression, release configuration,
  staged installation and final distribution acceptance.
- Script and editor documentation should describe only component-based
  ownership; older tests that expect `object.player` need separate
  migration as part of the wider workspace cleanup.

All changes stay local; production binaries are unchanged, no commit/push.
