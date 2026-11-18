# NorthStarCpp — Scene / ECS / Camera capability refactor

Date: 2026-11-17.
Workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp.
Status: Player + typed-contract CMake targets built; 20/20 focused CTest passed; Vulkan native gameplay lifecycle QA passed; D3D12 currently blocked by an unrelated sky shader.

## Architecture

All **46 callbacks** across engine.scene (15), engine.camera (23), and engine.ecs (8) are implemented and registered in **independently compiled C++ translation units**, with no inclusion of window_app_internal.hpp or use of [this] captures:

- engine/northstarcpp/modules/editor/window_script_scene_bindings.cpp — 15 Scene methods, including mutations, spawn/destroy, skinned attachments and collision-aware transform operations.
- engine/northstarcpp/modules/editor/window_script_camera_bindings.cpp — 23 Camera methods, including modes, follow, angles, shots, cutscenes, tracks, lens and recoil.
- engine/northstarcpp/modules/editor/window_script_ecs_bindings.cpp — 8 ECS methods, including query, queryMany, get/has, getPosition, getScript, setPosition and entity enumeration.
- engine/northstarcpp/modules/editor/window_script_domain_contracts.hpp — three corresponding private capability views and typed method-name tables with strict completeness/duplicate/unknown-method validation.

The App host supplies only typed, narrow capabilities. Scene receives live slots for the physics world, FLECS ECS, project VFS, viewport, camera, animation/attachment and ownership maps, and two privileged host operations (owned-entity destruction and streaming-layer visibility). Camera receives live camera/VFS slots, first-person mode and yaw/pitch. ECS receives live physics/ECS slots, stable-ID and native-handle maps plus the captured scene generation. Slots resolve current resources rather than capturing raw resource pointers when the scene is replaced.

The central window_script_bindings.cpp remains App-free. App::configure_script_bindings() stays in window_script_bindings_host.cpp to construct the capability views and register unrelated engine APIs. Host size decreased to 58,558 bytes; independently compiled files: Scene 30,074, Camera 16,809, ECS 10,178 bytes. The host contains **zero** direct domain .add registrations for Scene, ECS and Camera, versus 16 still present for Player and Audio.

All **108 engine.* method registrations are preserved** across App host, Scene/Camera/ECS/Input/Player/Audio modules and the existing save module. The public ScriptRuntime API and JSON representation have not been expanded or changed.

## Context lease / lifetime safety

Added window_script_lifetime.hpp defining ScriptBindingLease with atomic active state and explicit revoke().

A fresh lease is created for each configure_script_bindings() call and stored by script_context_epoch in AppRuntimeState. Domain registrars capture weak leases and validate them before invoking callbacks. A stale/revoked context throws out_of_range instead of attempting to access a disposed host state.

Lifecycle behavior:

- Hot-reload success: old context receives entity.stop and stop events, its runtime spawns are cleaned, then its lease is revoked; newly accepted candidate remains active.
- Hot-reload failure: only the candidate context is cleaned and revoked. If the scripting provider failed to construct before allocating a new epoch, no existing active lease is revoked (explicit previous_context_epoch guard).
- stop_project_scripts(): after stop events, revoke every lease; clear the map and increment script_scene_generation.
- initialize_project_scripts(): revoke pre-existing leases, initialize a fresh scene generation and clear old stable-ID / native-handle maps before ECS world loading.
- loadScene/restart: use stop + reinitialization, preventing stale handles from previous sessions from resolving to objects in a new scene.

The weak lease is a lifetime guard, not a general multi-threaded scheduling API. The ScriptRuntime callbacks remain bound to the host session on the game's runtime thread.

## Stable-ID, native handle and index validation

EcsDomainCapabilities::find_live_index() checks:
1. Matching current scene generation;
2. Stable-ID present in the active scene's index map;
3. Index within the current PhysicsScene object vector and EcsWorld size;
4. SceneObject.node.id equals the requested ID;
5. Runtime slot is alive if it is outside authored object range.

require_live_index() raises out_of_range when validation fails. query/queryMany additionally confirm that the returned FLECS handle maps to the same live object, preventing reused/invalid handles being reported. has() returns false for nonexistent or destroyed IDs; get/getScript/getPosition/setPosition reject stale IDs.

SceneDomainCapabilities also checks scene generation and verifies stable ID and live physics slot for runtime-parent references and runtime transforms. Destruction continues to use the existing owner-aware App implementation to maintain cleanup of attachments, FLECS entities and caches.

**Observed and fixed real defect:** engine.ecs.entities() originally iterated all PhysicsScene object slots and enumerated destroyed runtime entities. EcsEntitiesStateView now carries a live-index predicate; the native enumeration omits inactive runtime slots. The Scene.objects(start,count) API intentionally preserves slot positions and returns null for dead slots, matching its existing index-based semantics.

## Validation

- CMake build (Player): PASS, with independently compiled Scene/ECS/Camera modules and successful Player link, after fixing a C++ forward declaration mistake in a private header.
- CMake build (window_script_domain_contracts_smoke): PASS.
- Selected CTest: **20/20 PASS**. Includes native script contracts, modularity, SDK/project scripts, ScriptRuntime ABI, player camera scripts, FPS ADS and weapon effects.
- tests/window_script_bindings_modularity_smoke.py: PASS. Counts methods and ensures all Scene/ECS/Camera callbacks are native App-independent.
- tests/window_script_domain_contracts_smoke.cpp: PASS. Validates six typed sets, unknown/empty/duplicate handlers, and independent candidate-vs-active revocable leases.
- tests/window_script_entity_lifetime_e2e.py: **PASS on Vulkan** using real V8 and Jolt. Its scenario tests spawn -> ECS getPosition -> destroy -> stale ID rejection -> restart -> stale ID rejection -> loadScene -> stale ID rejection. Runs only in an isolated temporary project and LOCALAPPDATA. Session commands run on update rather than start because the HWND is not created until after the start event.
- tests/window_script_save_bindings_e2e.py: **PASS on Vulkan**, including typed domain availability and engine.save write/read/exists/remove.

CMake configuration or Ninja may emit transient GLOB and incremental build warnings because other processes are editing sharedAssets/render providers. These are external and must not be interpreted as FPS improvements or stable full-repo QA.

## Pending / blockers

- D3D12 gameplay E2E cannot currently create viewport: NorthStar D3D12 sky (line 73), HLSL X3014 incorrect number of arguments to numeric-type constructor. This appeared while D3D12/render resources were changing concurrently. The script capability code itself compiles; do not call D3D12 runtime QA passed on the latest renderer.
- Previous software WARP startup failure 0xC0000005 is not resolved.
- Full repository CTest was not rerun and is not certified green.
- Player/Audio still have **16 App-dependent callbacks** in window_script_bindings_host.cpp. The other engine APIs still contain App-dependent bindings. They should be migrated through separate scoped providers, preserving active-context leases.
- A raw view slot lives only inside an App session; do not re-use ScriptRuntime outside that lifetime. The weak-token guards are designed for controlled stop/reload/scene transitions.
- No valid matched FPS before/after benchmark. No FPS improvement claimed.

## Changed and new support code

- engine/northstarcpp/modules/editor/window_script_lifetime.hpp
- engine/northstarcpp/modules/editor/window_script_domain_contracts.hpp
- engine/northstarcpp/modules/editor/window_app_internal.hpp
- engine/northstarcpp/modules/editor/window_app_state.hpp
- engine/northstarcpp/modules/editor/window_script_bindings_host.cpp
- engine/northstarcpp/modules/editor/window_runtime_script_lifecycle.inc
- engine/northstarcpp/modules/editor/window_script_scene_bindings.cpp
- engine/northstarcpp/modules/editor/window_script_ecs_bindings.cpp
- engine/northstarcpp/modules/editor/window_script_camera_bindings.cpp
- tests/window_script_bindings_modularity_smoke.py
- tests/window_script_domain_contracts_smoke.cpp
- tests/window_script_entity_lifetime_e2e.py
- tests/window_script_save_bindings_e2e.py
- tools/dev/script_binding_lifetime_migration.py
- tools/dev/extract_camera_script_capabilities.py
- tools/dev/extract_ecs_script_capabilities.py
- tools/dev/extract_scene_script_capabilities.py

Original-byte backups for migrations are in out/refactor_backups. The migration scripts are guarded one-time operations, not to be rerun after successful extraction.

No git commit, push or production dist update was requested or performed.
