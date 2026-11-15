# NorthStarCpp — Material Graph 2.0 and native GPU Material Preview

Date: 2026-11-15
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
Editor target: `northstarcpp_editor_qt` (Qt6/C++17)
QA build: `out/native-model-pipeline-qa-20261114`

## Outcome

Material Graph authoring now has a live, GPU-backed material preview panel
embedded into the existing Material Editor dialog. It uses the current
`northstarcpp::Viewport` and negotiates the existing `engine.render`
material provider API; no parallel material renderer or legacy editor was
added. Both the Qt authoring and runtime graph compiler share the same
`*.material.json` schema 1.

New source files:

- `modules/editor/qt_material_gpu_preview.hpp`
- `modules/editor/qt_material_gpu_preview.cpp`
- `modules/editor/qt_material_graph_clipboard.cpp`

Modified:

- `modules/editor/qt_editor_window.cpp`
- `modules/editor/qt_material_graph_editor.hpp`
- `modules/editor/qt_material_graph_editor.cpp`
- `modules/editor/qt_editor_main.cpp`
- `modules/editor/qt_material_authoring.cpp`
- `engine/northstarcpp/CMakeLists.txt`

## GPU Preview workflow

1. Material Editor remains within the current Qt Editor.
2. The Material Graph tab shares material data with the Surface tab.
3. A side-by-side preview panel stages the working material in an isolated
   `QTemporaryDir`, under `assets/Materials/Preview.material.json`.
4. Texture references are copied from validated project-local sources into
   the isolated assets directory; no project material is overwritten.
   Preview copies are limited to 64 MiB per texture, and absent/oversize
   resources are explicitly reported.
5. The preview scene has a stable actor and registered
   `engine.geometry.primitive` component. Shape selection supports
   ellipsoid/sphere, cube, and torus. SOLID geometry uses mandated zero
   `u/v`; torus uses segment counts.
6. The scene actor references the staged material via the same stable
   `northstarcpp.editor.material` scene component as ordinary scenes.
7. The Qt GPU surface wraps the current `northstarcpp::Viewport` with an
   engine.render swapchain. The material v2 shader path and PBR renderer
   operate on these assets, not a separate QPainter sphere simulation.
8. Graph, factor, color and texture edits schedule a throttled (250 ms)
   preview reload. Graph evaluation is validated before staging. An invalid
   graph is rejected with a diagnostic; project file is unchanged.
9. Failed GPU initialization returns to the editor's safe Qt proxy and
   reports the provider error.

The initial GPU surface is created only AFTER a valid temporary material
has been staged, preventing a missing preview asset on the first render.

## Material Graph usability

- Search existing nodes by ID, operation, slot or parameter, with visual
  highlighting and center-on-match.
- Ctrl+F search, Ctrl+C/Ctrl+V node clipboard and Delete.
- Pasted nodes receive unique IDs, internal edges are remapped and dangling
  edges are removed; clipboard payload and node count are bounded.
- Existing graph drag/reconnect, selection, pan/zoom, editing, reset,
  Undo/Redo and Save remain available.
- `MaterialGraphEditor::graphChanged` refreshes the preview without modifying
  on-disk source materials until the user explicitly saves.
- The graph compiler independently rejects cycles and malformed output.

## Verification

### Clean/incremental QA build
- `northstarcpp_editor_qt.exe`: linked successfully in the isolated
  `out/native-model-pipeline-qa-20261114` directory.
- C++17 syntax checks: Graph Editor, Clipboard, GPU Preview, Qt Editor
  window, Material Authoring and entry point compiled.
- A missing `QGraphicsItem` include in the new CLI test was corrected.
- Graphics primitive settings were aligned to the actual geometry API.

### CTest

```
ctest --test-dir out/native-model-pipeline-qa-20261114 -R "^northstarcpp_qt_(material_graph_authoring|editor_authoring|editor_gpu_surface)$" --output-on-failure
```

Results: **3/3 PASS**, total real time 4.02 s.

### Specific acceptance

The rebuilt editor executable was also run directly with:

- `--material-preview-gpu-smoke`: **PASS**, exit 0; staged a material
  with graph and successfully rendered with the real native GPU surface.
- `--material-graph-smoke`: **PASS**, exit 0; graph node creation, pin
  binding, clipboard, runtime compiler, temporary asset validation and
  rejection of a directed cycle.

Note: the preview GPU smoke verifies GPU surface and successful draw return,
not full pixel readback, physically calibrated lighting, or visual equivalence
to Unreal Engine. These require dedicated visual reference captures and
shader/pixel acceptance tests.

## Outstanding milestones

- HDRI/environment and user-adjustable light direction/intensity/exposure in
  the preview (current shader lighting uses its default illumination).
- Material Function/Subgraph nodes, group/comment frames, material layers,
  material instances and shader parameter collections.
- Node port types and conversion rules beyond the current bounded float4 DAG.
- Asynchronous incremental shader compilation, error locations per graph
  node, PSO/variant cache measurements and GPU timestamp profiling.
- GPU screenshots/readback validation for every graph node and scene mesh;
  high volume and long-run memory/stability testing.
- Broader Unreal-style editor conveniences, inspector workflows and world
  composition tools.

Production `dist` was not replaced. No Git commit or push was performed.
Existing unrelated working-tree modifications were left intact.
