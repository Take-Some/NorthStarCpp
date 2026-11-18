# NorthStarCpp — Editable Native YMAT Libraries QA

> **Document status:** Historical material-editor report (2026-11-15). Any kind-30 compatibility described below is superseded by the [XML-only YMAT format](YMAT_XML_ONLY_MIGRATION_2026-11-18.md); do not restore the legacy reader.

**Date:** 2026-11-15
**Workspace:** C:/Users/AidenFox/Documents/NorthStarCpp
**QA build:** out/ymat-entry-editor-qa-20261115
**Status:** 15/15 targeted CTest PASS; Qt Editor linked; production dist unchanged
**Plugin ABI:** unchanged

## User-facing workflow

- Double-click a native `.ymat` container in Content Browser to list **all** its material entries; no implicit first-material selection.
- Double-click `materials/library.ymat@MaterialName` to edit precisely the named entry.
- Material Editor tabs: Preview, Surface, Material Graph.
- Surface edits shader name, blend mode, Base Color, Metallic, Roughness, Alpha Cutoff, Two-sided, all typed arbitrary parameters, and all YTD@entry texture bindings. Add/remove custom parameters and texture slots.
- Material Graph supports existing typed nodes/functions and save into native YMAT.
- Scene Inspector Material Picker lists named entries for every multi-entry YMAT and supports selecting, applying, saving, undo/redo with explicit `@MaterialName` references. Single-entry YMAT references remain backwards compatible.
- Even if Graph HLSL is currently invalid or a referenced function is missing, a material may be opened to correct it. Compiling a repaired graph is required before save.

## Native YMAT codec and safety

**Kind 4:** XML material library (world materials), preserving NEF8 header, XML metadata, custom shaders, all sibling records, custom parameter names/types, texture links, original record order. Optional `<Graph>` fields can be compiled by the runtime.

**Kind 30:** JSON-bearing character/weapon material library under native compressed NEF8; per-entry edits retain unmodified material entries and unknown JSON fields.

- Read + decode into typed Qt editor document without discarding arbitrary parameter slots or custom shaders.
- Only selected named material is patched, never silently replace the multi-entry library with a one-entry file.
- Re-encode compressed body keeping the NEF8 kind, version, metadata and entry count intact. Recalculate BLAKE3 digest and compressed/uncompressed sizes. Validate all resulting materials through the runtime codec before committing.
- Atomic `QSaveFile` commit. `QLockFile` serializes concurrent editor saves of the same YMAT.
- Reject unsafe paths/ambiguous material selectors/duplicate fields and identity changes. Never use the first entry as an implicit fallback for multi-entry YMAT.
- Base Color editing preserves native alpha when RGB changes and preserves unmodified linear RGB precision. The advanced parameter editor and normal Surface panel stay synchronized.

**Non-YTD image inputs:** Dragging PNG/JPEG/TGA-like image assets onto an actor imports an RGBA8 native NEF8/YTD texture with a named entry and creates native `.ymat` binding that YTD; no ad hoc JSON material or unsupported PNG texture link remains in the runtime material. YTD import roundtrip is validated against the native texture decoder.

## QA validation

Final QA: **15/15 PASS** (4.26 seconds) in `out/ymat-entry-editor-qa-20261115`.

Tests:
1. `northstarcpp_qt_material_library_edit`: kind30 fixture with Blue Ceramic and Copper Panel; adding/editing/removing custom typed parameters and texture links, maintaining nonopaque color alpha; kind4 3-record `skydome.ymat`, preserving other world materials and NEF8 header/metadata; saving XML Graph and round-trip compiling HLSL; invalid save rejected without changing bytes.
2. `northstarcpp_qt_material_asset_dialog`: Content Browser two-entry browsing + named selection; Preview/Surface/Graph tabs and editable controls.
3. `northstarcpp_qt_editor_authoring`: end-to-end texture-drop → native YTD → native YMAT → scene Inspector → undo/redo and scene serialization; selection of named shared YMAT entry.
4. `northstarcpp_qt_editor_gpu_surface`: native engine.render viewport test.
5. `northstarcpp_material_precache`
6. `northstarcpp_material_functions`
7. `northstarcpp_material_graph_hlsl`
8. `northstarcpp_material_resources`
9. `northstarcpp_qt_asset_preview_warnings`
10. `northstarcpp_asset_catalog`
11. `northstarcpp_editor_asset_thumbnails`
12. `northstarcpp_editor_asset_pipeline`
13. `northstarcpp_shared_assets`
14. `northstarcpp_qt_material_graph_authoring`
15. `northstarcpp_qt_material_function_gpu`

The native Qt Editor and render provider binaries exist under QA folder. Scoped `git diff --check` passed; unrelated pre-existing workspace changes were not overwritten. No commit or push.

## Primary files

- `engine/northstarcpp/modules/editor/qt_material_library_edit.cpp`: named entry kind4/30 load/save
- `engine/northstarcpp/modules/host/material_ymat_authoring.cpp`: NEF8 re-encode and YTD importer
- `engine/northstarcpp/modules/editor/qt_material_asset_dialog.cpp`: PBR inspector and typed parameters/texture slots
- `engine/northstarcpp/modules/editor/qt_material_authoring.cpp`: asset resolver, YMAT material enumeration, image import
- `engine/northstarcpp/modules/editor/editor_tools.cpp`: scene actor material reference validation for `@name`
- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`: named entry selection and Qt scene workflow
- `engine/northstarcpp/modules/host/shared_asset_models.cpp`: XML YMAT graph decoding
- `tests/qt_material_library_edit_smoke.cpp`: reversible per-entry library QA

## Boundary

- Tests cover supported native NEF8/YMAT kind 4 and 30. Arbitrary unrecognized foreign binary formats are outside the verified codec coverage.
- Custom shader identifiers remain editable and preserved, but correct visual execution of every third-party shader requires appropriate renderer implementations.
- The Qt suite includes WARP GPU viewport/material-function tests. Independent full-scene visual pixel comparison for every imported material on physical hardware has not been completed.
- This work did not replace files in production `dist`, did not change C plugin ABI, and did not perform a Git commit or push.
