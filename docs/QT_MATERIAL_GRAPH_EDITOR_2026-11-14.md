# NorthStarCpp Qt Material Graph Editor — first authoring milestone

Date: 2026-11-14
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
Active editor: northstarcpp_editor_qt.exe

## Scope

The existing Qt Material Editor has a new **Material Graph** tab, backed by the
same `compile_material_graph` function used by the first-class material
runtime; there is no separate authoring shader language and no legacy editor.

Files:

- `engine/northstarcpp/modules/editor/qt_material_graph_editor.hpp`
- `engine/northstarcpp/modules/editor/qt_material_graph_editor.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`
- `engine/northstarcpp/modules/editor/qt_material_authoring.cpp`
- `engine/northstarcpp/CMakeLists.txt`

## Editor capabilities

- Native QGraphicsScene canvas; mousewheel zoom, middle-button pan.
- Nodes can be placed, selected, moved, deleted, and edited.
- Node creation via palette and right-click menu.
- Typed directionality (one output into an input), multiple consumers of one
  output, and explicit disconnected input handling.
- Drag output-to-input connections; right-click input to disconnect.
- Reject graph cycles while wiring. Compiler also validates malformed,
  missing or unsupported nodes and graph complexity before Save.
- Undo/redo for graph structure and node movement (up to 80 snapshots).
- Persist node positions in the existing JSON `graph.nodes[].x/y` metadata.
- Graph opt-in: materials without graphs retain their original PBR setup.
- Existing Surface tab retains colors, factors, texture assets and previews.

Supported runtime node vocabulary: `parameter`, `texture`, `scalar`,
`color`, `add`, `multiply`, `lerp`, `one_minus`, `saturate`.
Outputs: `base_color`, `roughness`, `metallic`, `emissive`, `opacity`.

## Integration contract

A project material remains `*.material.json` schema 1. The graph JSON is
embedded in that descriptor and is validated with the runtime graph compiler
before the atomic `QSaveFile` commit. On a successful save, the editor
refreshes Content Browser, material caches, asset sources and GPU viewport.

Native YMAT remains read-only in this editor; create project-local material
sources to author graph overrides. Material assignment remains actor ->
material resource, never actor -> texture resource.

## Quality and remaining work

This is a **first functional graph UI**, not full Unreal Engine Material
Editor feature parity. Remaining: typed vectors and conversion nodes, GPU
shader diagnostics with source locations, asynchronous incremental shader
compilation, live sphere preview using the actual GPU backend, material
instances/layers, drag from Content Browser into texture sample nodes, graph
subfunctions and comment boxes, copy/paste, multi-selection, a minimap, and
GPU performance analysis.

Validation of a new build must include:
1. Build `northstarcpp_editor_qt`.
2. Open a scene and project material; enable graph, add nodes, connect pins.
3. Save and reopen the material, confirm positions and edges.
4. Verify cycle rejection and compiler rejection for incomplete output graphs.
5. Render the graph material on the D3D11/D3D12 GPU surface and inspect its
   result against the authoring preview.

Production dist promotion, full QA and feature parity are not implied merely
by the source changes.
