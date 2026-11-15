# World Outliner — UE-inspired Qt Editor (2026-11-15)

## Goal

Simplify the World Outliner to resemble the compact scene hierarchy of
Unreal Editor, preserving NorthStar's existing NSCN scene model and plugin ABI.

## Implemented

- Flat, dark compact styling; no intrusive bright alternating rows, no "Type"
  table column or horizontal scrollbar.
- Editor-rendered glyphs for folders, primitives, geometry, player and mesh
  actors instead of inconsistent Windows stock file icons.
- A fixed 32 px visibility-eye column: click toggles the underlying actor's
  `node.visible` without selecting it. Changes are validated and undoable.
- F2 edits the selected actor's name *in-place*; changes are deferred until
  Qt completes its editing signal and recorded in Undo history.
- Double-click focuses the selected actor in the scene viewport.
- Search supports multiple terms and the qualifiers `type:mesh`,
  `type:player`, `type:geometry`, `is:hidden`, `is:visible`;
  searches also cover actor name, layer, asset reference, and entity ID.
- Geometry class lookups are indexed once per rebuild, rather than scanning
  all components for every visible actor.
- A-Z toggle, compact Expand/Collapse controls, Add Cube menu and counts.
- Right-click on empty space offers Add Cube, Expand All and Collapse All;
  actor context menu has Focus, Rename, Duplicate, Delete, visibility and
  Attach/Detach.
- Preserves collapse state across filtered searches. Selection changes no
  longer clear and reallocate QTreeWidget rows. Drag/drop still uses the
  stable NSCN IDs, validation and undo.
- Existing core render and asset/provider contracts are untouched.

## Files

- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`
- Supporting build correction in
  `engine/northstarcpp/modules/render/d3d11/renderer_editor_grid.cpp`:
  explicit `ID3D11Buffer* [2]` instead of invalid `auto* [2]`.

## Verification

- Native MinGW/Qt compilation and CMake/Ninja link: **PASS**
- `build/provider-parity-qa-20261115/northstarcpp_editor_qt.exe --smoke`: **PASS**
- `ctest --test-dir build/provider-parity-qa-20261115
  -R ^northstarcpp_qt_editor_authoring$ --output-on-failure`: **PASS**
- Smoke exercises hierarchy filtering, search qualifiers, stable selection,
  in-place renaming plus Undo, real mouse event on visibility icon
  (without selecting actor) and visibility Undo.
- Git whitespace check for modified implementation: **PASS**

## Output and limitations

The tested editor executable is at
`build/provider-parity-qa-20261115/northstarcpp_editor_qt.exe`.

The standard `out/project-manager-qt` build was configured successfully, but
a complete clean rebuild of the Project Manager and its heavy dependencies was
not finished in this session. It should not be represented as deployed.
Existing release packages were not modified. GUI pixel-by-pixel visual parity
with Unreal Editor is not claimed. Git commit/push was not performed.
