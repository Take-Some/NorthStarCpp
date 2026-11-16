# NorthStar Material Editor — unified visual workflow (2026-11-16)

## Intent
Reorganize the existing Qt Material Graph authoring experience into a single editing workspace resembling the supplied Unreal Engine material editor reference, without replacing material compiler, texture providers, or existing NEF8 authoring.

## Changes
- Native Qt Material Editor shows the active GPU preview, a scrollable Surface Inspector, and the Material Graph simultaneously. The layout uses adjustable splitters and defaults to a graph-dominant workspace.
- Graph Editor provides a searchable, categorized Material Expressions tree (Input / Constants, Mathematics, Conversions, Functions). Double-click adds a compatible existing expression node at the canvas center. Space focuses the library search.
- Existing expression Details Inspector remains visible below the tree. Color Constant nodes display a color swatch.
- Native YMAT library editor shows the existing saved-material sphere preview beside its graph, without linking standalone Content Browser against the full Qt Editor GPU preview implementation.
- Existing graph validation, node type rules, project material save, and YMAT entry selection/save paths are unchanged. Multi-entry YMAT libraries are still edited by explicit entry and preserved on save.

## Verification
- Fresh isolated CMake configure: PASS (MinGW GCC 16.2, Ninja, Release).
- Build northstarcpp_editor_qt: PASS.
- Build northstarcpp_qt_material_asset_dialog_smoke: PASS.
- CTest northstarcpp_qt_material_asset_dialog: 1/1 PASS with QT_QPA_PLATFORM=offscreen.
- git diff --check: PASS.
- The test checks the YMAT dialog structure and data path; manual GPU pixel/readback, DPI and interactive drag/pan QA have not been performed.
- Production dist was not replaced, and no Git commit or push was performed.

## Remaining improvements
1. Independent GPU preview service usable from standalone Asset Browser to make YMAT graph preview live without creating a binary dependency on the Editor executable.
2. Improve inline node value editing, asset drag-and-drop into texture nodes, and context search at cursor.
3. Save splitter sizes, run visual tests on different screen sizes/DPI, and validate Vulkan/D3D12 preview equivalence.
