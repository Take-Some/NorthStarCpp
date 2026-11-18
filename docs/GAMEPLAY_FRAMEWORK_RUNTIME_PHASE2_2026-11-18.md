# NorthStarCpp: Gameplay Framework Phase 2 — 2026-11-18

> **Document status:** Phase-2 historical snapshot, not the current gameplay feature matrix. Later events, physics and AI integration are documented in [Gameplay Production Gates](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md).

## Architecture
Architecture is informed by UE 5.3.2 concepts; no Epic code/assets are copied.
Retains Scene V3 IDs, Jolt motor, V8, Qt and existing camera/animation providers.

## Implemented
1. Camera Runtime: scripted Player methods `viewTarget`, `setViewTarget`, `resetViewTarget` now connect PhysicsScene view target to per-frame CameraDirector; supports selected Entity pose, optional scene camera FOV/near/far, and 0.35s camera transitions with `camera.viewTargetStarted`/`camera.viewTargetFinished` events. Controller Pawn remains unchanged.
2. Player + AI: shared validated Pawn target; independent AIController with possess/unpossess/reconcile/move_to/stop/next_intent, normalized horizontal vector and arrival detection.
3. Animation: retains last valid clip on missing action clip (Jump/Fall etc), attempts alternative startup clip if Idle unset; avoids sending empty clips during transitions.
4. Input: provider-independent `InputActionMap`: Gameplay, Vehicle and UI contexts, priority/consumption, bounds and rebind; V8 `engine.input.actionDown`, `rebindAction`, `setContextEnabled` exposed. Legacy `isDown` preserved.
5. Qt Details: right-click Pin Group to Favorites (persistent QSettings), search `@favorites`, Ctrl+Alt+F to focus existing Details search.

## QA
- `cmake --preset release`: PASS.
- `cmake --build out/build --target northstarcpp_gameplay_framework_smoke -j 4`: PASS.
- `cmake --build out/build --target northstarcpp_player_blueprint_smoke -j 4`: PASS.
- `ctest --test-dir out/build -R '^(northstarcpp_gameplay_framework|northstarcpp_player_blueprint)$' --output-on-failure`: 2/2 PASS.
- GCC 16.2 syntax-only: `window.cpp` (Unicode), `window_script_bindings_host.cpp` (runtime-version macro), player/input script bindings, Qt Details editor: PASS.
- `git diff --check`: PASS.

## Deliberate limitations
- AIController computes motion intent but is not yet hooked to NavMesh, collision avoidance or Jolt-controlled NPC movement; does NOT make an NPC walk by itself.
- Input action remapping is runtime only, not saved to project; existing native movement still needs to be routed to the new action system.
- No frame-by-frame GPU/Editor visual QA, no complete production Player/Editor build or distribution replacement.
- No commit or push. Unrelated preexisting working-tree edits untouched.

## Next
NavMesh/Jolt NPC locomotion + animation pose continuity; fully authoritative input contexts and project saved bindings; rendered camera transition integration smoke; Qt multi-edit/reflection and player/editor production build QA.
