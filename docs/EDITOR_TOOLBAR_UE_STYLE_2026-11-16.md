# NorthStarCpp Editor: UE-inspired authoring toolbar
Date: 2026-11-16

## Scope
- Independently implemented Qt 6 interaction patterns inspired by Unreal Engine 5.3.2's Level Editor Toolbar and Place Actors UI. No Epic source code copied.
- `engine/northstarcpp/modules/editor/qt_editor_window_setup.cpp` now exposes a compact Scene / Run toolbar: Save, Undo, Redo, Add Actor, Content, the six existing PIE transport buttons, and Play Options.
- Actor placement menus in the main toolbar and World Outliner share the same factory. Each has case-insensitive search, categories for Shapes / Geometry and Characters / Gameplay, and a Content Browser entry point.
- Search results are menu-owned proxy actions that trigger existing editor QAction handlers. This avoids hiding shared Edit menu actions when filtering. Creation uses existing scene transactions (undo/redo, selection, NSCN serialization).
- Play Options reuses the existing Play In Editor, standalone player, First Person Exploration and FPS Arena QAction objects; transport shortcuts and the player process/IPC lifecycle remain unchanged.
- QMenu popup hover/selection, toolbar grouping and placement search field use the native NorthStar dark theme.

## Supported placements
- Procedural Cube, Cylinder, Ellipsoid, Ring and Torus.
- Character Pawn and Player Blueprint (Controller + Pawn + Camera + SkeletalMesh + Animation).
- Native model/prefab placement is available through the docked Content Browser. The placement menu opens that browser; it does not invent unsupported actor types.

## Validation (Windows / Qt 6 / MinGW)
- CMake configuration: PASS with `NORTHSTARCPP_V8_SIDECAR=OFF` and `BUILD_TESTING=OFF`.
- `cmake --build ... --target northstarcpp_editor_qt --parallel 4`: PASS, linked editor executable.
- Built binary: `C:\Users\Kayla\Documents\NorthStar\cache\northstar_toolbar_qa\northstarcpp_editor_qt.exe`.
- `--content-dock-smoke` (`QT_QPA_PLATFORM=offscreen`): PASS.
- `--inspector-layout-smoke` (`QT_QPA_PLATFORM=offscreen`): PASS.
- `--mouse-look-smoke` (`QT_QPA_PLATFORM=offscreen`): PASS.
- `--smoke`: FAIL later at `hierarchy roundtrip saveScene() failed`. The earlier checks for search, both actor placement menus, and Play Options did not fail. Root cause of the later scene save failure was not investigated in this task.
- `git diff --check` of the two edited source paths: PASS (note: these two paths currently appear as untracked files in this repository).
- No production editor binary or distribution was replaced. No Git commit/push was made.

## Remaining scope for deeper UE parity
- True cross-type Place Actors panel with drag-to-viewport, component/provider-driven entries, actor thumbnails, a project-local Recent list and favorites.
- Native point/spot/directional lights and standalone editor cameras depend on a defined, validated scene-component/renderer contract; no fake placeholders were added.
- DPI, narrow-width layout and mouse-focus QA on the real Windows desktop remain to be verified.
