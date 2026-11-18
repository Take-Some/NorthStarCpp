# NorthStarCpp Qt Editor — UX Phase 2 (UE-style workflows)

Date: 2026-11-18
Scope: Native NorthStarCpp Qt editor. No Epic Games source code imported.

## Goal

Improve five existing workflows without changing the project/asset formats,
renderer-provider contracts, or game-play runtime:

1. Searchable and grouped **Details**
2. Contextual **World Outliner**
3. **Batch editing** and atomic Undo
4. Customizable **keyboard shortcuts**
5. Clear **viewport drag/drop feedback**

Reference architectural patterns are informed by the provided Unreal Engine
5.3.2 Level Editor, Content Browser and Details organization. Implementation
is new, independent Qt/C++ code.

## Details

- Reuses the **existing** `detailsPropertySearch` field in the dock header
  (the search bar stays visible while the property pane scrolls).
- Existing generic `QFormLayout` search handler is disconnected locally
  when category-aware filtering is installed. No duplicate search control.
- Added collapsible property categories:
  Actor / Identity, Appearance / Material, Transform, Visibility,
  Lighting, Physics / Collision, Components / ECS.
- Categories persist expansion state under
  `KaylasSystems/NorthStarQtEditor/Details/Expanded/*`.
- Searching includes form-row labels, widget/object IDs and nested labels,
  overriding collapsed-state presentation without modifying values.
- The lighting category respects actual selected-actor ECS component
  ownership instead of exposing light widgets on unrelated entities.
- The existing mixed-values and explicit-dirty-only batch inspector
  contract is preserved.

## Outliner and batch operations

- Right-click on an already selected actor no longer resets the rest of
  the multi-selection.
- Duplicate N actors and Delete N actors both use one `editScene()`
  transaction / one Undo step.
- Group visibility Show/Hide, Hide Unselected, Show All.
- Move selected actors to existing scene layers.
- Select All and Include Descendants, Copy Selected Actor Names.
- Rename and Attach To remain single-actor actions by design;
  Attach To is disabled for multi-selection to avoid hierarchy cycles.
- Batch operation targets stable Entity IDs, not positional indices.
- `editor_tools::duplicate()` retains source ECS and opaque plugin
  component records, with updated owner IDs.
- `editor_tools::delete_subtree()` removes ECS and opaque records
  for deleted entities (scene-global and surviving records preserved)
  to prevent `validate_scene: ECS component refers to missing entity`.

## Keyboard shortcuts

- Tools -> Keyboard Shortcuts...
- Searchable action catalog, based on existing named `QAction` objects.
- Edit using Qt `QKeySequenceEdit`, with a conflict check before
  assigning and before persisting.
- Reset selected / Reset all to built-in defaults.
- Persist user overrides under
  `KaylasSystems/NorthStarQtEditor/Shortcuts/<actionId>`.
- No duplicated Save/Undo/Play handlers; action state and existing
  internal validation remain authoritative.

## Drag and drop

- Dragging an engine asset or a Place Actors entry into the viewport
  displays a temporary overlay: source name and an approximate ground
  position in scene coordinates.
- Native GPU mode also displays an OS tooltip over the swapchain child,
  which may otherwise obscure ordinary Qt overlay widgets.
- No Scene V3 entity is created until **drop**. Cancellation/leave and
  completed drop clear the feedback.
- Actual placement continues through the original
  `assetDropped` / `placeableDropped` signals and the authoring
  ray-hit logic; the preview position is approximate and non-committal.

## Files modified

- `engine/northstarcpp/modules/editor/qt_editor_window.hpp`
- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_setup.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_inspector_workspace.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_workspace_signals.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_smoke.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_viewport.hpp`
- `engine/northstarcpp/modules/editor/qt_editor_viewport.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_keyboard_shortcuts.hpp` (new)
- `engine/northstarcpp/modules/editor/editor_tools.cpp`

## Verification

Native MinGW Release objects and Qt editor were built for Windows.
`northstarcpp_editor_qt_ux2.exe` was linked as a staging executable
to avoid replacing the installed application before QA.

- **PASS**: Qt editor `--smoke` with Qt offscreen (no native GPU),
  including Details filtering, shortcut conflict detection, canceling
  a viewport drag, atomic batch duplicate/delete and Undo.
- **PASS**: `--place-actors-smoke` with Qt offscreen.
- **PASS**: `--gpu-smoke` on Windows with normal Qt platform backend.
- All three tests exited code 0.
- Full Ninja rebuild initially hit a Windows file lock while replacing
  `out/build/archive/northstar_shared.nsp`; the specific modified
  sources were compiled, their libraries rebuilt and the executable
  linked directly with the same generated CMake/Ninja commands.
- Linker still prints existing Boost `any` duplicate-section warnings;
  linking itself succeeded.
- Final manual visual acceptance of hover overlay on Vulkan/D3D12 and
  interactive dialog/layout testing is **not yet complete**.

## Non-goals / remaining UX work

- This is not Unreal Engine functional or licensing parity.
- No editable shortcuts for undocumented low-level viewport input
  (e.g., physical keyboard movement mapping) in this pass.
- Multi-selection copies actor entity/components independently. It does
  not yet clone an entire prefab hierarchy and remap arbitrary internal
  entity-ID references in third-party component payloads.
- Asset drag preview uses ground projection for feedback; final placement
  may snap to the nearest existing scene mesh on release.
- Follow-up: UI automation for nested properties and interactive GPU
  hover across DPI scaling and multiple monitors.

No commit or push was requested or performed. No proprietary game
assets were copied or published.


## Deployment

- Verified \`northstarcpp_editor_qt_ux2.exe\` was atomically promoted to
  \`out/build/northstarcpp_editor_qt.exe\` after all three QA suites passed.
- Installed executable size: **14,702,987 bytes**.
- Installed executable SHA-256:
  \`f38e5092f0fa8a96410e1e3272e9d1a08db4d351240715f4316fdfd210802c15\`.
- Original executable retained at
  \`out/qa/editor_ux2/backups/northstarcpp_editor_qt_before_ux2_398c97bd2879.exe\`.
- Only the developer build is replaced; an external production
  installation or exported \`dist\` directory was not modified.
