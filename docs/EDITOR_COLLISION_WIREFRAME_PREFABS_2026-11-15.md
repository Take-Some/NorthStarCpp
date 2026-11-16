# NorthStarCpp — Collider Authoring and Wireframe (2026-11-15)

## Scope
- Qt Editor > Details | Properties > Physics | Collision Shape: enable, Box/Sphere/Capsule, dimensions, offset.
- Toolbar and View menu: Collision Wireframe toggle; show selected/other/disabled colliders.
- Native GPU Viewport and Qt proxy use common collision wireframe generator; authoring overlay is not scene geometry.
- Asset Browser > Prefab Preview > Edit Collisions... opens isolated prefab viewport. Select child actor, set enabled/shape/dimensions/offset, Apply Collision and Save Prefab.
- Scene ECS record `northstar.physics.collider` v1 persists metadata in NEF8 scene component records.
- Text V3 .nprefab codec now supports generic versioned ECS component records with hex-encoded payload; previous prefab scenes remain readable.
- Capturing and instantiating prefab remaps component owner entity IDs and retains enabled state, shapes and offsets.
- Optional provider ABI NORTHSTARCPP_PHYSICS_COLLIDER_MODULE_ABI: collision shape applied to Jolt and Bullet. Primary Physics ABI 4 remains unchanged.

## Test execution
- Direct compilation of edited runtime, Qt, viewport, Jolt and Bullet C++ objects: PASS.
- `northstarcpp_editor_prefab_smoke.exe`: PASS (capsule persistence, text V3 round-trip, two instances with remapped IDs).
- `northstarcpp_physics_smoke.exe`: PASS for both Bullet and Jolt (disabled collision and authored sphere sweep).
- `northstarcpp_editor_collisions_qa.exe --smoke` using `QT_QPA_PLATFORM=offscreen`: PASS, including collision wireframe control checks.
- QA output: `out/editor-ue-parity-qa-20261115/`.
- The original `northstarcpp_editor_qt.exe` was in use (PID 10352), so final relink output used separate `northstarcpp_editor_collisions_qa.exe`; running editor was not replaced or terminated.

## Known limitations / follow-up
- Box/Sphere/Capsule colliders are currently axis-aligned in physics and debug view; actor Euler rotation and non-uniform visual scaling are not propagated to the collider provider. Wireframe matches current physics semantics rather than rotated render meshes.
- Overlay currently caps drawing to the first 80 actors (selected actor prioritized), and uses thin cube segments in native GPU mode. This prevents runaway editor draw calls but is not a scalable large-world debug-renderer replacement.
- GPU pixel-level visual acceptance, GUI interaction with a real prefab, deployed distribution and full workspace regression are not yet verified; offscreen Qt smoke is not a screenshot review.
- Older physics providers without the new optional collider API reject scenes that request authored collider shapes rather than silently ignoring the configuration.
- No commit/push was performed. Many unrelated changes exist in the working tree.
