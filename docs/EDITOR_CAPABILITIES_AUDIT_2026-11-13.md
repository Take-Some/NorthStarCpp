# NorthStarCpp — editor capability audit and implementation status
Date: 2026-11-13
Workspace: C:\Users\AidenFox\Documents\xray2
Connector: NorthStar – Suite V4.2

## Integration principle

The editor must operate through stable NorthStarCpp C++/C APIs and capability contracts.
No direct dependency on `flecs::world`, Bullet internals or D3D11 implementation details.
An ECS world is the authoritative source for edit-time entity properties. `Scene` is the authoring document and the current serialization/render projection.

## Existing engine capabilities identified

| Area | Existing C++ implementation | GUI status |
| --- | --- | --- |
| Project Manager | Qt 6, project templates/catalog/search/favorites | Separate GUI, launchable |
| Entity management | ECS API v2 + Flecs, provider-neutral | Integrated with editor |
| Scene documents | v1/v2/v3, stable IDs, parent links, layers, cells, rooms, volumes, portals, LOD | v3 fields partially exposed |
| World streaming | SceneWorld, index, cells, layers, volumes, portals | Validation/diagnostics entry point; no visual cell editor |
| Camera | First/third/vehicle/orbit/free/cinematic, timeline track | Separate timeline tool; not all modes exposed in viewport |
| Assets | Directory/FAT/DB VFS, async streaming, drawable/material/texture resource APIs | Bound-asset picker and reference diagnostics; no thumbnail browser |
| Physics | Bullet/Jolt provider interface | Mass and collision flags exposed; no shape/constraint UI |
| Rendering | D3D11 provider/viewport, scene and sky support | Basic viewport; no full material graph |
| Input | Win32/XInput provider | Player input; no action-mapping editor |
| FPS gameplay | Player, moving targets, weapon/reload/health, camera | FPS launch entry point, gameplay lives in player |
| Audio | WinMM PCM16, FPS clips | Runtime integrated; no audio-mixer editor |
| Packaging | Engine/game packaging CLI | Not yet a native Build/Package GUI |

## This integration step

- Introduced `modules/editor/editor_tools.hpp/.cpp`: C++ scene-authoring operations with scene-level validation.
- Added v1/v2 → v3 migration on advanced editing, stable scene IDs, duplication, parenting, subtree deletion and simple layer/cell/volume/portal creation.
- Implemented bounded snapshot-based Undo/Redo in the editor (32 transaction limit).
- Added editor File/Edit/World/Assets/Play/Tools menus and keyboard accelerators.
- Added inspector tabs for basic entity data, rotation/scale, hierarchy/assets and streaming/LOD.
- Added C++ asset selection/asset-reference check using the active AssetManager, Project Manager launch, camera timeline entry and FPS player launch.
- Added an ECS/world summary in the status line and SceneWorld validation diagnostics.
- Added regression test `tests/editor_tools_smoke.cpp` and `northstarcpp_editor_authoring` CTest, using a CMake-resolved ECS provider.

## Verification

- Target `northstarcpp_editor` and `northstarcpp_editor_tools_smoke` compiled with GCC Release configuration.
- After CMake regeneration and rebuilding the authoring test, targeted CTest: **3/3 PASS**:
  - `northstarcpp_editor_authoring`
  - `northstarcpp_scene_world_v3`
  - `northstarcpp_editor_player`
- A wider clean build, full test matrix, GUI visual acceptance, and standalone game distribution checks are still necessary. A prior parallel Release recompilation encountered memory exhaustion; sequential compilation was successful for the affected test target.

## Remaining for production-level editor

1. Replace the flat Win32 list with a real entity tree, drag-and-drop parenting, layer/cell/room outlines, and a visual object selector/gizmo.
2. Build a reflected component registry and extensible inspector with versioned persistence for custom ECS component data.
3. Provide the Content Browser with thumbnails, import, asset references, dependency validation, materials/textures, shaders and caching.
4. Add viewport camera-mode controls, collision and streaming visualization, prefab authoring, and render/material tooling.
5. Add physics bodies/shapes/constraints and animation/NPC navigation editors as real provider-neutral C++ tools.
6. Provide a first-class Build/Package interface, project build configurations, actionable diagnostics and release acceptance tests.
7. Stress-test large v3 worlds and streaming, validate Play-in-Editor isolation, authoring undo/redo behavior across scene reopen, and complete full runtime profiling.

**Important:** Existence of historical code under `modules/foundation` does not imply those capabilities are compiled or usable. Each additional capability must be linked, integrated, tested and redistributable before being claimed as production-ready.
