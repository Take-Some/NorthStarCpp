# NorthStarCpp: Root Motion / Skeletal Viewport / Prefab / Script Assets QA

Date: 2026-11-18
Scope: working-tree changes only. No production `dist` replacement, commit, push,
forced process termination, or deletion of original project source backups.

## Rebuild and root-cause findings

The original `out/build` test binaries were stale relative to current source.
A targeted rebuild initially removed the old error
`Valid Root Motion rejected tick=1` but revealed the actual current
substep problem: one animation delta was consumed once by Jolt while its
generated linear velocity carried forward into extra fixed steps.

`engine/northstarcpp/modules/physics/jolt/jolt.cpp` now explicitly
tracks a root-motion-driven motor velocity. While applying root-motion
displacement, the motor applies exact commanded velocity rather than
rate-limiting it using gameplay acceleration; on the first subsequent
grounded no-input substep it brakes the residual. Airborne impulse handling
and physically solved Jolt collisions remain enabled. Motor tracking
resets upon possession changes. The existing strict `80` tick collision
and multi-substep conservation tests were **not weakened**.

## Skeletal viewport / ADS

`engine/northstarcpp/resources/sharedAssets/characters/abby/animations/weapon_grips.xml`

- Align pistol front/rear iron-sight vertical positions to the same
  optical axis (rear/front y = `0.09189` m).
- Calibrate the authored rear sight to eye distance to `0.295` m:
  the former `0.39` m reach was mechanically unreachable by Abby's
  secondary arm on large pitch/yaw, causing the IK solver to miss
  its grip socket by up to `0.0481` m.
- Maintain the test's full precision `<0.012` m on both wrist IK targets,
  original torso collision limits, lateral separation, and sight alignment.
- Add a diagnostic left target/actual/shoulder print only when IK fails
  (in `tests/viewport_skeletal_pipeline_smoke.cpp`).

The fresh compiled skeletal viewport pipeline smoke passes. Live visual
capture and comparative Vulkan/D3D12 screenshots remain necessary.

## Prefabs

Canonical project-wide weapon prefab contract was restored:

- `prefabs/weapon/pistol.nprefab`: mass `0.65` kg for physical pistol.
- `prefabs/weapon/pistol_shell.nprefab`: preserve `size 0.05`,
  `mass 0.006` kg, use unit scale.
- `prefabs/weapon/rifle_shell.nprefab`: preserve `size 0.062`,
  `mass 0.011` kg, use unit scale.

Attached pistol prefab remains massless/non-colliding as before.
No assets were renamed and no scene entity IDs were remapped.

## Game project script assets

`tools/build_game.py` excludes authoring backup files whose file name
contains `.bak` when copying arbitrary `assets` into the release,
preserving them in the source workspace but not exposing them through
the shipped VFS. Symlink checks remain performed before this filter.

`tests/project_script_assets_layout_smoke.py` now verifies the
package excludes a deliberately created `main.js.bak-20261118`
fixture while keeping the canonical script and the legacy export
compatibility test. Existing source backups are not deleted or moved.

## Fresh Release build / verification

The original `out/build` disappeared during work. CMake was re-run with
`cmake --preset release`, rebuilding the targeted tests, Jolt and Flecs
providers. A first Flecs link was transiently refused by Windows
(`ecs_flecs.dll: Permission denied`); the subsequent targeted retry
succeeded.

Final targeted CTest command:

```powershell
ctest --test-dir out/build --output-on-failure --timeout 20 -R "^northstarcpp_(player_blueprint_physics|viewport_skeletal_pipeline|editor_prefab|project_script_assets)$"
```

Results on fresh targeted Release build:

| CTest | Result |
| --- | --- |
| `northstarcpp_player_blueprint_physics` | PASS |
| `northstarcpp_viewport_skeletal_pipeline` | PASS |
| `northstarcpp_editor_prefab` | PASS |
| `northstarcpp_project_script_assets` | PASS |

**4/4 selected CTest PASS**, total test execution ~2.67 seconds.
This is **not a full clean-build or Release 1.0 certification**.

## Separate outstanding MT28 material regression

`northstarcpp_prefab_composition` still FAILS:
`MT28 prefab preview did not load all models and materials`.
Model geometry and mesh paths load, but `load_composition` emits missing
named-material warnings, notably for
`materials/mahir_mt28/v_mahir_mt28_coach__ext01_body_01.ymat` entries
`@vehicle_branding`, `@light_covers_glass`, `@stickers`,
`@stickers_barcode`, `@decals_secondary` and others.

Existing MT28 YMAT asset files in both KaylaNightCity and
NorthStarFirstPersonPlatform contain NEF8 binary payloads; the missing
named entries have not been reconstructed. No fake materials or
nontransparent fallback were substituted to silence the regression.
This remains an explicit separate material/asset conversion task.

## Remaining acceptance

- Live FPS gun pose capture at extreme yaw/pitch and animation transition
- Visual no-bind-pose validation on D3D12 and Vulkan
- Full game Player startup and Root Motion interaction smoke
- MT28 material dictionary reconciliation and prefab composition smoke
- Broader CTest suite, packaged game and clean-machine acceptance

Work remains local only, with pre-existing uncommitted files preserved.
