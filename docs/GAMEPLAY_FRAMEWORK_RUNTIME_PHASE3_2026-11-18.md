# NorthStarCpp Gameplay Framework — Phase 3 implementation & QA

> **Document status:** Phase-3 historical snapshot; see [Gameplay Production Gates](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md) for later lifecycle and CharacterMotor work.
Date: 2026-11-18
Source: local working tree, NorthStar Suite V4. Target: NorthStarCpp 0.7.0.
Unreal Engine framework is a conceptual reference only; NO proprietary Unreal source code was copied.

## Implemented this phase

### Native multi-agent NPC motion
- New optional physics extension `NorthStarPhysicsAiMotorApi` (version 1) over existing Physics ABI; old plugins can omit extension.
- Jolt owns separate per-body movement velocities in its fixed physics update. Never convert NPC motion to per-frame entity teleportation or redirect the PlayerController. Gravity, dynamic body integration and collisions remain active.
- PhysicsScene exposes `supports_ai_motor`, `set_ai_move_intent`, `clear_ai_move_intent`.
- Player host connects `gameplay::AIController` to Jolt with an `engine.ai` domain supporting `moveTo`, `stop`, `isMoving`.
- Motion input direction is collision-checked with a short physics sphere sweep; simple local left/right steering is provided. On stop, pending AI motor commands are cleared.
- NPC animation controller samples actual post-physics velocity and ground state; sends the selected clip to the mesh-animation runtime, avoiding empty state clips.
- Scene changes and script shutdown clear NPC commands/animation selections.
- **Not a polygon NavMesh**: no global route planning, multi-level path navigation, crowd avoidance or moving-platform rider logic yet. Complex city NPC navigation requires a proper NavMesh provider; simple sweep steering is not a substitute.

### Input Actions
- `InputActionMap` is authoritative for native Player keyboard WASD, Jump, Interact, Pause while preserving gamepad routes and legacy raw input API.
- Versioned `assets/input/actions.xml` via `input_actions_xml.hpp` with finite bounded parsing and duplicate-key validation.
- `engine.input.rebindAction(context,action,keyCode)` writes project XML transactionally and rolls runtime mapping back if file persistence fails. Windows uses replace-existing atomic rename.
- Missing optional map silently chooses defaults. Packaged-game rebind writes are disallowed without writable project.
- Contexts: Gameplay, Vehicle and UI with priority and modal UI capture.
- Project scripts using legacy raw input are not automatically rewritten.

### Qt Editor
- Registered typed ECS Float32/UInt32 component property editing now supports multiple selected Entity IDs: only explicitly edited fields are patched per matching component, all unknown bytes and unrelated scalar values preserved. Entire batch validated and applied using one `editScene` undo transaction. Aborts when components are missing/duplicated/version-incompatible.
- A transient `viewportDropGhost` placement marker is displayed for actor/prefab/mesh drags, alongside existing coordinate/asset feedback; no Actor is created until Drop. The ghost disappears on invalid drags, leave or drop. Material/texture drags keep their existing specialized feedback.
- Existing Outliner multi-select, custom field search, Details favorites, component schema and asset-drop workflows remain.
- This is a 2D marker aligned to projected ground position, not an actual loaded 3D model ghost. Full asset residency/bounds preview remains outstanding.

## Verification performed
- `cmake --build out/build --target northstarcpp_physics_jolt -j 4`: PASS
- `cmake --build out/build --target northstarcpp_player_blueprint_physics_smoke -j 4`: PASS
- CTest `northstarcpp_player_blueprint_physics`: PASS, including AI motor two-Pawn dynamic collision/stop and existing Root Motion checks.
- `cmake --build out/build --target northstarcpp_gameplay_framework_smoke`: PASS
- CTest `northstarcpp_gameplay_framework`: PASS, including InputActions XML round-trip, context priority and rebinding.
- `cmake --build out/build --target northstarcpp_camera_cutscene_gpu_smoke`: PASS
- CTest `northstarcpp_camera_cutscene_gpu_d3d12`: PASS
- CTest `northstarcpp_camera_cutscene_gpu_vulkan`: PASS
- `cmake --build out/build --target northstarcpp_player -j 8`: PASS, after fixing vendored Boost `xml_writer_make_settings<char>` incompatibility.
- `cmake --build out/build --target northstarcpp_editor_qt -j 12`: PASS
- `out/build/northstarcpp_player.exe --runtime-init-smoke --scene Projects/NorthStarFirstPersonPlatform/main.nscn`: PASS, startup READY.
- `out/build/northstarcpp_editor_qt.exe --smoke`: PASS (full editor scene/asset placement/undo/redo, including drag overlay).
- Qt and Player source GCC 16.2 syntax checks PASS. Qt emitted preexisting SFINAE compiler warning, no syntax errors.
- No full interactive session on the *user's* city project, no side-by-side GPU recording of View Target or skinned NPC Root Motion, no end-user dist/deployment replacement.

## Open issues
1. Need proper polygon NavMesh generation, path queries, dynamic obstacle avoidance, vertical navigation, NPC crowd coordination and path repair; local sweep steering does NOT guarantee path traversal.
2. NPC animation graph/physical Root Motion is not visually accepted in KaylaNightCity. Real YCD skeletal draw and weapon attachment require frame captures.
3. Qt registered component reflection still supports the pre-existing scalar field metadata types only. Additional plugin-specific field types need their own editors.
4. 3D ghost mesh streaming in drag/drop is not implemented; only a non-mutating projected placement marker.
5. Runtime smoke logs a separate existing project PistolWeapon.start warning about dynamic bodies; outside this phase scope.
6. Changes are uncommitted in an already-dirty working tree. No dist replacement, commit or push performed.

## Binaries built
- `out/build/northstarcpp_player.exe`
- `out/build/northstarcpp_editor_qt.exe`
- `out/build/physics_jolt.dll`
- `out/build/render_d3d12.dll`
- `out/build/render_vulkan.dll`

All paths are on the linked Windows workspace, not on the ChatGPT sandbox.
