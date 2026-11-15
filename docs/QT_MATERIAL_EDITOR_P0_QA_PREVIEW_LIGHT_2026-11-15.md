# NorthStarCpp Material Editor 3.0 — P0 Acceptance and Preview Studio Key Light

Date: 2026-11-15
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
Target: northstarcpp_editor_qt, Qt6/C++17
ABI: unchanged
Production dist: unchanged
Git commit/push: not performed

## P0 — isolated full QA build passed

A dedicated build folder was configured from the current workspace:
`out/material-editor-v3-p0-20261115`

CMake configuration succeeded. Initial full compilation reached link but the
generated D3D11 link command omitted independently compiled RendererState
modules (renderer_device, renderer_materials, renderer_frame, etc). Regenerating
the CMake build graph incorporated those existing modules, and D3D11 and D3D12
render provider DLLs both linked. The Qt editor executable linked successfully:

`out/material-editor-v3-p0-20261115/northstarcpp_editor_qt.exe`

CTest acceptance (run against the initially linked editor):
- northstarcpp_qt_material_instance_authoring: PASS (0.33 sec)
- northstarcpp_qt_material_graph_authoring: PASS (0.57 sec)
- northstarcpp_qt_editor_authoring: PASS (1.64 sec)
- northstarcpp_qt_editor_gpu_surface: PASS (2.33 sec)

Direct smoke tests:
- --material-preview-gpu-smoke: PASS (exit 0)
- --material-graph-smoke: PASS (exit 0)
- --material-instance-smoke: PASS (exit 0)

These smoke tests establish working preview draw and editor authoring contracts,
but not pixel-accurate PBR equivalence to Unreal Engine.

## Preview Studio — first provider-backed lighting control

The Qt Preview Studio now has explicit controls for the scene's key-light
intensity and azimuth:
- `materialPreviewLightIntensity`: 0–20 intensity, initial 3
- `materialPreviewLightAzimuth`: -180..180 degrees
- Uses existing Scene V3 `engine.render.light` component with stable
  version 1 payload: x y z range r g b intensity dx dy dz coneCos
- Updates the scene and redraws through the native GPU viewport, without
  changing the render provider ABI
- Uses non-colliding studio fixture, independent of the preview material
- Existing sphere/cube/torus, orbit camera and GPU grid remain supported.

The lighting extension is interpreted by providers that implement deferred
dynamic lighting; the D3D11 authored PBR forward path is not guaranteed to
respond equivalently. HDRI environment sampling, exposure slider, pixel readback
and visual reference acceptance are NOT completed.

## Latest QA variant: running executable lock

On relinking after the preview key-light modification, Windows reported
`Permission denied` when trying to overwrite `northstarcpp_editor_qt.exe`,
because another editor process was holding that path. The process was not
terminated. Instead, the exact current object files and existing link flags
were used to produce a second QA executable without replacing the locked file:

`out/material-editor-v3-p0-20261115/northstarcpp_editor_qt_material_p1_qa.exe`

This variant linked successfully (exit 0). Three direct tests using the
variant returned exit 0 and PASS:
- --material-preview-gpu-smoke (now changes key light intensity to 6)
- --material-instance-smoke
- --material-graph-smoke

The four earlier CTest results were recorded against the initial QA executable.
Do not misattribute those exact CTest runs to the light-control variant.

## Remaining required gates

P1 Preview Studio:
- GPU environment cubemap/HDRI decode and convolution/IBL.
- Provider-backed light rig controls, including intensity, direction,
  temperature/color and physically meaningful exposure.
- Screenshot/pixel readback tests verifying illumination, PBR response and
  comparable D3D11/D3D12 results.

P1 Graph:
- Fully typed scalar/vector/sampler pins, safe implicit conversions,
  Material Function/subgraph resources, function imports and cycle rejection.

P1 Shader Pipeline:
- True nonblocking GPU shader compilation and PSO creation in both provider
  implementations; persistent cache, invalidation and node-level HLSL
  diagnostics, timeline profiling.

P2 UX:
- Expanded parameter Inspector, asset drag-and-drop from Content Browser,
  robust graph grouping workflows, workspace presets, visual regression suite.

No production files were promoted. Existing unrelated working tree edits were
not rolled back.
