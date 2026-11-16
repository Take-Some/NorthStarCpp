# Player SkeletalMesh visual prefab composition — 2026-11-16

## Defect and architecture
The Editor displayed the editor_character_marker capsule for an unbound logical Pawn, even when the game referenced a real Abby visual prefab. The old Abby game script created its visual actor only after the Player started, so the saved Scene V3 had no native skeletal visual to render before gameplay scripting.

The canonical ownership is:
- PlayerController (independent root) -> possesses Pawn stable Entity ID.
- Pawn -> CharacterMotor (movement), Camera (rig), SkeletalMesh (render-only).
- SkeletalMesh owns model and optional version-1 `northstar.gameplay.skeletal_mesh` component with a VFS-relative `.nprefab` reference; its component is independent of possession/physics and may be bound to compatible visual assets.
- Capsule collision remains on Pawn, separate from the visual prefab.

## Implementation
1. `player_blueprint_visual.hpp`: optional quoted prefab path in SkeletalMesh payload, with legacy-format deserialization, strict safe VFS path validation and `.nprefab` extension check.
2. `qt_player_blueprint_editor.hpp`: prefab path field and browser, validated against project and engine shared assets. Saving the player resolves the prefab's native root drawable; standalone YDD/YDR/OBJ links remain supported.
3. `editor_visual_prefab.hpp`: non-mutating visual-only Scene V3 composition. Reuses the existing SkeletalMesh Entity ID for the prefab root, remaps only additional visual children, strips collision and scripts, and does not import Pawn, motor or controller components. Canonical-path containment and 4 MiB limit protect local preview file reads.
4. `qt_editor_viewport.{hpp,cpp}`: transient composed render scene cached on Scene/asset-source changes, not rebuilt per GPU frame. Native Engine GPU draws real YDD/YDR geometry; missing/malformed prefabs emit nonfatal warnings. Authored Scene remains unchanged and is not baked by the preview.
5. `window.cpp`: Player resolves linked visual prefabs through `engine.assets` VFS prior to gameplay physics/script setup; it reuses the saved SkeletalMesh stable ID. Added `engine.scene.rotateStatic` binding for safe manipulation of authored static visuals.
6. `sharedAssets/api/northstar/bootstrap.js`, types: `Scene.handleByName` returns a non-owning authored handle whose move/rotate delegate to `moveStatic`/`rotateStatic`, never to spawn-only `moveRuntime`. Scripts only own and destroy their fallback instantiations.
7. `Projects/AbbyPistolDemo/main.nscn`: native-authored SkeletalMesh stable ID 44, parent Pawn ID 1, Abby model, prefab reference `prefabs/abby.nprefab`, collision disabled. Source backup: `Projects/AbbyPistolDemo/docs/main_before_visual_prefab_2026-11-16.nscn`. The project script now reuses the authored visual before attempting legacy `Prefab.instantiate`.

## Tooling repair
The Python NEF8 v2 writer now accepts explicit typed record versions to avoid rewriting Entity record v2 as v1. The demo generator uses Entity record v2 and corrects its historical entity field packing (`<fIfQQ>` without the stray byte). Python scene reference inspection uses the corrected field offset. New generated NEF8 scene was independently accepted by native Player.

## Verified
- QA build `northstarcpp_player.exe`: compiled and linked.
- QA build `northstarcpp_editor_qt.exe`: compiled and linked.
- C++ `northstarcpp_player_blueprint_smoke.exe`: PASS (serialization, legacy, prefab, immutable visual composition).
- C++ `--check-demo-visual Projects/AbbyPistolDemo/main.nscn`: PASS, real Abby prefab resolves to existing visual ID 44.
- Qt Player Blueprint smoke in offscreen mode: PASS.
- `node tests/abby_pistol_demo_smoke.cjs`: PASS, one authored Abby used across FPS/TPS and only pistol instantiated, authored visual survives stop.
- `python tools/create_abby_pistol_demo.py --verify`: PASS.
- Native Player `--check-scene --scene Projects/AbbyPistolDemo/main.nscn`: exit 0, Abby native mesh loaded (33 model sections).
- `git diff --check`: PASS before final report edits.

## Unverified / deployment
- Full interactive screen-space, GPU pixel, mouse selection and multi-resolution/DPI acceptance remains pending.
- The older broad Qt Editor `--smoke` failed with an opaque ECS component round-trip error; this was not attributed to the prefab feature.
- QA executables were produced in `C:\Users\Kayla\Documents\NorthStar\cache\northstarcpp-release-ui-20261116`; installed production Editor/Player were not replaced. No commit or push.
- Editor's CPU Proxy modes remain diagnostic silhouettes and cannot render arbitrary native models. Use Viewport -> Engine GPU to see the authored character.
