# NorthStarCpp Material Editor 3.0 — implementation and QA

Date: 2026-11-15
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
Target: northstarcpp_editor_qt (Qt 6, C++17)
Runtime contract: existing engine.render ABI, no ABI bump
Deployment: source-only; production dist not replaced; no git commit/push

## Implemented

### Preview Studio
- Added opt-in orbit camera to QtEditorViewport: position from orbit target, yaw,
  pitch, and bounded distance, with mouse drag and wheel zoom. Scene fly mode
  remains the default outside the preview.
- Preview uses native engine.render GPU surface, staged project-local material,
  selectable sphere/cube/torus, and a toggleable GPU grid made from
  non-colliding scene line proxies (not an editor-only QPainter overlay).
- Existing preview material isolation and debounced reload remain active.
- HDRI, editable studio light rigs and exposure are NOT implemented.

### Material Graph 3.0
- Added comment/group frames in QGraphicsScene, with selection-based grouping,
  Ctrl+G, name editing and ungrouping. Child nodes are parented to frames, and
  their absolute positions are persisted in graph JSON.
- Group metadata is editor-only: the existing bounded graph compiler ignores
  `groups` and accepts the same typed execution DAG.
- Existing search, pin connection, copy/paste, undo/redo and compile-to-HLSL
  remain in use.
- Material Functions, richer typed pins and automatic type conversions are
  NOT implemented.

### Material Instances
- Project-local .material-instance.json source files can be created with a
  parent material/instance and named overrides, matching existing runtime
  schema 1.
- Added instance inspector for standard_lit/unlit, Base Color, metallic,
  roughness, opacity and five texture bindings. Each override may be toggled
  independently to inherit parent values.
- Asset Catalog recognizes instances as material assets; material choice and
  sphere preview read the resolved parent settings.
- Runtime contract remains AssetStore -> load_material_definition, using
  the same VFS safety boundaries. Recursive parent-chain cycle and depth
  validation, bounded descriptor parsing and QSaveFile atomic writes added.
- Advanced graph override UI is not yet implemented; any existing instance
  graph override is preserved unchanged.

### Shader preflight / diagnostics
- Added QtConcurrent worker for bounded Material Graph compile/preflight,
  with 250 ms debounce, 64-entry result cache, generation IDs to discard stale
  results, and inline node-ID diagnostics in the graph editor.
- Corrected compile_material_graph to annotate per-node failures.
- Invalid graph is not handed to GPU preview; last valid material is preserved.
- D3D11 and D3D12 runtime shader/PSO variant compilation is still synchronous
  inside providers. This milestone does not add asynchronous PSO creation,
  persistent disk PSO cache, GPU timestamp traces, or pixel-level validation.

### Professional UX
- Saves Material Editor geometry, active Surface/Graph tab, and splitter
  proportions through existing QSettings.
- Existing scene editor dock layout is unchanged.

## Source touchpoints

New:
- engine/northstarcpp/modules/editor/qt_material_instances.hpp
- engine/northstarcpp/modules/editor/qt_material_instances.cpp
- engine/northstarcpp/modules/editor/qt_material_instance_dialog.cpp
- engine/northstarcpp/modules/editor/qt_material_graph_groups.cpp
- tests/material_graph_v3_smoke.cpp

Updated:
- engine/northstarcpp/modules/editor/qt_material_gpu_preview.{hpp,cpp}
- engine/northstarcpp/modules/editor/qt_editor_viewport.{hpp,cpp}
- engine/northstarcpp/modules/editor/qt_material_graph_editor.{hpp,cpp}
- engine/northstarcpp/modules/editor/qt_material_authoring.cpp
- engine/northstarcpp/modules/editor/qt_editor_window.cpp
- engine/northstarcpp/modules/editor/qt_editor_main.cpp
- engine/northstarcpp/modules/editor/asset_catalog.cpp
- engine/northstarcpp/modules/material/material_graph.cpp
- engine/northstarcpp/CMakeLists.txt

## Tests and evidence

1. Eleven changed C++ translation units passed C++17 syntax-only compilation
   using actual flags from the active Ninja configuration
   out/native-skin-final-qa-20261115.
2. Standalone Material Graph compiler regression:
   tests/material_graph_v3_smoke.cpp + material_graph.cpp, g++17 with bundled Boost.
   Compilation succeeded, execution returned 0:
   `Material Graph V3 standalone compiler regression: PASS`.
   Covers valid graph with comment-frame metadata, compilation to generated HLSL
   defines, cycle rejection with offending node ID.
3. Git diff --check returned 0 for tracked touched sources. Git produced only
   LF/CRLF working-copy warnings.
4. After CMake reconfiguration of out/native-model-pipeline-qa-20261114, the
   D3D11/D3D12 provider sources included their separate shader_compiler.cpp
   objects and both renderer DLLs linked successfully in that QA build.
5. The editor-linked smoke paths were added:
   `--material-instance-smoke` tests inherited overrides and actual VFS runtime
   material resolution, plus cycle rejection and atomic save.
   `--material-graph-smoke` includes comment groups with node displacement,
   and asynchronous worker rejection of a cyclic graph.
   `--material-preview-gpu-smoke` exercises orbit camera and scene GPU grid.
   A CTest entry for `northstarcpp_qt_material_instance_authoring` was added.
   **These latest editor CLI/CTest cases have not yet executed successfully.**

### Build conflict / blocker
During QA, previous out/native-model-pipeline-qa-20261114 and other QA
directories disappeared while separate build processes were active. The
replacement active build out/native-skin-final-qa-20261115 did not yet
contain northstarcpp_editor_qt.exe and reported more than 550 pending Ninja
steps with other Ninja processes running. No competing full build was launched
against it. The final full Qt editor binary link and new end-to-end GUI/GPU
regressions are unverified for this milestone.

## Next acceptance gates

- Obtain isolated stable QA build folder; run full editor link and the three
  new/extended CLI smoke tests. Re-run core editor authoring and GPU surface
  CTests, validate real GPU pixels and long-run stability.
- Add editable HDRI environment, light direction/intensity and exposure in
  provider-backed preview (requires actual GPU shader/scene changes).
- Add typed pins, Material Functions/subgraphs, instance parameters and shader
  parameter collections; validate file and graph import/export.
- Implement asynchronous D3D shader/PSO compilation, variant error reporting
  and deterministic cache/invalidation; add profiling and p50/p95/p99 timings.

Existing unrelated uncommitted work and production dist were left intact.
