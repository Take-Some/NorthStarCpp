# NorthStarCpp — Place Actors Panel / Scene Camera & Light Entities
Date: 2026-11-16
Workspace: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`

## Delivered
- Native Qt dockable **Place Actors** window; one panel shared by the editor session, accessible from the top toolbar and View menu, tabbed with World Outliner. No separate process.
- Typed, searchable catalog with Geometry / Gameplay / Cameras / Lights / Components, plus Favorites and Recent filters.
- Drag payload `application/x-northstar-placeable-id` to the actual editor Viewport. Placement uses the existing world-space ray / nearest surface and is applied through the scene authoring transaction with Undo/Redo, stable scene IDs and selection.
- Favorites and last 16 placements persisted with `QSettings("KaylasSystems","NorthStarQtEditor")` under `PlaceActorsV1/favorites` and `PlaceActorsV1/recent`.
- One placement dispatcher in `QtEditorWindow::placeActorById`, used by the top menu, Outliner menu and panel; no second ECS or gameplay API.
- Existing Cube, Cylinder, Ellipsoid, Ring, Torus, Player Blueprint, Character Pawn, project/engine Asset Browser workflows preserved.
- Independent Camera Actor with `northstar.scene.camera` ECS component. Validated v1 binary payload contains FOV, near, far, active, priority; registered reflected fields appear in the editor component inspector. The camera Director selects active scene camera requests by priority, while preserving gameplay-shot precedence on ties; disabling/removing an Actor does not leave stale director requests.
- Native Point Light and Spot Light actors use the **existing** `engine.render.light` Scene V3 bridge, with entity-relative position and actual renderer light records. A strict component validator is registered.
- Plugin/component actors are automatically discoverable from `list_scene_component_schemas()` only when their default (zero-initialized, size-bounded) value passes `validate_scene_component_blob`. Unregistered providers disappear from the refreshed palette. Unknown component records remain losslessly serializable.
- World Outliner differentiates Camera, Point Light and Spot Light by ECS component (not actor name); dedicated vector-painted icons and `type:camera` / `type:light` queries.

## Technical integration
- `engine/northstarcpp/modules/editor/qt_place_actors_panel.{hpp,cpp}`: searchable, persistent catalog and drag initiation
- `engine/northstarcpp/modules/editor/qt_editor_window.{hpp,cpp}`: scene-authoring factories, Outliner display
- `engine/northstarcpp/modules/editor/qt_editor_window_setup.cpp`: dock, toolbar, actions and ECS validators
- `engine/northstarcpp/modules/editor/qt_editor_viewport.{hpp,cpp}`: viewport placement drops and nearest-surface location
- `engine/northstarcpp/modules/api/include/northstarcpp/scene_camera_actor.hpp`: camera component ABI, validation and scene shot translation
- `engine/northstarcpp/modules/host/camera.cpp`: CameraDirector arbitration
- `engine/northstarcpp/modules/editor/qt_editor_main.cpp` and `qt_editor_window_smoke.cpp`: focused `--place-actors-smoke` QA
- `engine/northstarcpp/CMakeLists.txt`: new Qt editor sources added

## QA
- CMake configure with `NORTHSTARCPP_V8_SIDECAR=OFF`, `BUILD_TESTING=OFF`: PASS.
- All 15 C++ source objects of the editor target recompiled successfully; Qt automoc regenerated; the editor QA EXE linked successfully against the available prebuilt renderer/runtime libraries.
- `northstarcpp_editor_qt.exe --place-actors-smoke` with `QT_QPA_PLATFORM=offscreen`: **PASS**.
  Validates catalog/search, Camera Actor registration + runtime director selection, Point and Spot GPU light bridge, Outliner typed filtering, favorites/recent persistence, dynamic plugin schema discovery/placement/unregistration, Qt drag/drop creation and Undo.
- `--content-dock-smoke`: PASS.
- `--inspector-layout-smoke`: PASS.
- `--mouse-look-smoke`: PASS.
- `--smoke`: FAIL later at `hierarchy roundtrip saveScene() failed`. This appears to be an independent regression outside Place Actors.
- `git diff --check` for tracked affected paths: PASS (untracked files are not included by Git in this check).

## Build and deployment limitations
- Full CMake dependency-graph build was blocked in an unrelated, concurrently changing D3D12 source: `RendererState::ensure_material_buffers(std::size_t)` definition in `renderer_resources.cpp` conflicts with the header's two-argument declaration. This task did **not** modify those renderer sources.
- Standalone QA executable is located at `C:\Users\Kayla\Documents\NorthStar\cache\northstar_toolbar_qa\northstarcpp_editor_qt.exe` (QA cache, not a production distribution).
- Native GPU/DPI/real-mouse visual acceptance is pending. Headless Qt DnD was verified.
- Light contract presently supports Point/Spot; true Directional Light is **not** implemented by this dynamic-light ABI. It must use a supported sun/directional contract, not be emulated by a giant spotlight.
- Real in-viewport camera and light billboard/gizmo drawing has not yet been completed in GPU mode. World Outliner uses dedicated icons.
- No production binary replacement, Git commit or push was performed.
