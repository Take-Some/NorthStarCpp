# NorthStarCpp Qt Editor — Unreal-style authoring workflow upgrade

Date: 2026-11-14
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
Active editor: northstarcpp_editor_qt.exe (Qt 6)
Implementation language: English for project UI and source; C++17.

## Architecture retained

NorthStarCpp uses a single Qt QMainWindow editor with docked
Content Browser, World Outliner, Details, Asset References and Output Log.
This update expands that existing editor; it does not resurrect the legacy
Win32 editor or the separate Python browser.
NSCN remains a NEF8 Scene ListFile with stable entity IDs and parent IDs.
All mutations go through the existing scene validation and History contracts.

## Working changes implemented

### World Outliner

- Parent-child scene hierarchy is now represented by nested QTreeWidgetItems.
- Roots are grouped by SceneNode.layer, instead of showing a flat list.
- Search by name, layer or stable ID preserves and expands ancestor chains.
- Actor rows display visibility state; collapse state is preserved during
  in-place updates and search expands matching hierarchy.
- RMB context menu: focus, rename, duplicate, delete, attach/detach.
- Drag actor onto another actor to set parent; drag onto a layer to move
  its root and layer. Cycle checks use an ID->parent hash map.
- Actor mutations use validated NSCN transactions and Undo/Redo.
- The limited "Attach To" context menu uses an O(N) indexed ancestor map
  rather than repeated linear searches; drag-and-drop is available for
  any listed actor.

### Viewport and camera

- RMB+WASD camera fly, Q/E elevation, Shift acceleration, Ctrl precision
  remain functional. MMB pan, mouse wheel speed/dolly and grid/draw
  modes remain usable.
- W/E/R when RMB is not held selects translate/rotate/scale gizmos.
- F frames selected object by its proxy bounds and FOV; Home frames
  the visible scene bounds.
- Three camera bookmarks per scene, via Ctrl+Alt+1/2/3 to store and
  Alt+1/2/3 to recall. Paths are hashed before entering QSettings keys.
- Camera pose is restored on workspace load and per-scene reopen.
  Camera state is also persisted when switching to another scene.
- Gizmo drag now performs in-place bounded transform updates with
  a single before-image and one Undo record for the entire drag,
  instead of cloning a potentially 16,384-actor NSCN every mouse move.
  Editor tools still validate the authored scene transforms.

### Details, game and assets

- Details fields auto-commit on editingFinished or checkbox click;
  manual Apply remains supported. Unchanged property values are left
  intact, avoiding incidental precision loss on unrelated transforms.
- Game menu has explicit FPS mode. Scene projects declaring
  "game_mode": "fps" select it automatically. The mode is mutually
  exclusive with First Person Exploration.
- F6 launches the configured or adjacent Player with the selected
  native game mode; Shift+F6 terminates the owned Player subprocess.
- Existing Content Browser asset drag/drop into the authoring viewport,
  material descriptor creation, dependency status and dock layout
  persistence remain integrated.

## Verification

Qt editor compiled successfully in an incremental Release MinGW64
configuration. The expanded authoring CTest covers actor placement,
Undo/Redo, NEF8 roundtrip, nested hierarchy, ancestor-aware search,
one-action gizmo Undo, layer reparenting and Undo/Redo, camera
bookmark recall, and automatic FPS manifest mode detection.
Existing geometric camera picking, Content Browser and Asset Catalog
CTests also passed.

The editor was additionally tested against an actual Player binary
via northstarcpp_editor_qt.exe --player-check with exit code 0.

Verification also uses an isolated clean Release directory:

    out/editor-ue-workflow-qa-20261114

DO NOT conflate the Qt QPainter proxy viewport with actual Vulkan
runtime rendering: no UE5-quality PBR/shadows/volumetrics/asset rendering
is claimed.

## Remaining gaps versus Unreal Engine

P0 GPU viewport integration:
- Expose the current plugin-driven render graph into a Qt render surface
  (QRhi/QWindow-compatible swapchain or native render-target bridge).
- Match GPU material/shader previews, lighting, shadows, overlays,
  selection outlines and visual reference frames.
- True GPU picking or stable ID-buffer actor selection.

P0 Editor usability:
- Rebuild Details using registered arbitrary ECS component schemas, including
  plugin-owned unknown component preservation, property metadata, edit
  transactions, validation, and component add/remove workflows.
- Full move/rotate/scale gizmo with accurate screen-space hit tests,
  numeric and local/world transforms, snapping and constraints.
- Non-blocking async asset thumbnail/registry queries on huge projects,
  virtualized Outliner and streamed-cell visibility.

P1 World Partition / level authoring:
- Streaming cell grid and residency overlays, HLOD and NavMesh,
  Data Layers, collision inspector, world origin/selection hierarchy,
  prefab inheritance and multi-level save transactions.
- Spawn/instantiate materials and actors from native asset references.

P1 Play-In-Editor:
- Proper embedded simulation/Play-In-Editor state separation, ejection,
  hot-reload, debug controls and error isolation; current F6 launches
  a separate native Player process.

P1 Editor QA/performance:
- Quantified p50/p95/p99 viewport frame times, Qt event-loop input latency
  and hardware Vulkan GPU timestamps for scenes with >10k actors.
- Regression snapshots with UE reference and large-world memory tests.

A complete Unreal Engine feature-for-feature editor is NOT finished.
The current milestone is a working native Qt authoring workflow upgrade.
Production dist was not overwritten.


## Final clean-build verification

An isolated Ninja/MinGW64 Release configuration was generated under:

    out/editor-ue-final-qa-20261114

The configuration has CMAKE_SUPPRESS_REGENERATION=ON to avoid contention
with other active build processes in the shared checkout.

- Build target northstarcpp_editor_qt: **85/85 build steps, exit 0**.
- New executable: out/editor-ue-final-qa-20261114/northstarcpp_editor_qt.exe
- Extended northstarcpp_qt_editor_authoring CTest: **PASS (1/1)**.
- Editor --player-check with a separately built native Player:
  **PASS, process exit 0**.
- Five related editor and Content Browser tests in the earlier
  out/nef8-ue-preview-qa-20261114 build: **PASS (5/5)**.
- git diff --check for the four updated Qt editor source/header files: PASS.
- No production dist was replaced, and no Git commit was created.

The expanded authoring smoke explicitly creates a real layer before
testing drag-drop into that layer, exercising actual NSCN validation
without disabling it. Native QA marks are not a substitute for full
visual/gpu usability or functional parity with UE5.

To reproduce the final editor check:

    cmake --build out/editor-ue-final-qa-20261114 --target northstarcpp_editor_qt
    ctest --test-dir out/editor-ue-final-qa-20261114 -R ^northstarcpp_qt_editor_authoring$ --output-on-failure

For an existing player binary:

    set NORTHSTARCPP_PLAYER_EXECUTABLE=C:\Path\To\northstarcpp_player.exe
    out\editor-ue-final-qa-20261114\northstarcpp_editor_qt.exe --player-check
