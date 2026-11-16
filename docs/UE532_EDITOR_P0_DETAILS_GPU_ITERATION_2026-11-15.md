# NorthStarCpp Qt Editor — P0 Details / GPU hardware continuation

Date: 2026-11-15
Workspace: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
QA build tree: `out/editor-ue-parity-qa-20261115`

## Implemented in this iteration

### P0 Details — reflected metadata via the existing C host ABI

- Appended `register_type_reflected` to `NorthStarSceneComponentHostApi` without renumbering or replacing existing `register_type` / `unregister_type` offsets. Legacy plugins use the unchanged binary table prefix. New plugins must check `api->size >= offsetof(...,register_type_reflected)+sizeof(...)`.
- Added the POD `NorthStarSceneComponentFieldC` descriptor: UTF-8 label, 4-byte aligned offset and scalar kind U32/F32.
- Host copies all metadata immediately, bounds the number of fields at 64, validates the scalar kinds and labels, rejects duplicate offsets through the existing registry validator, and retains the provider's existing callback-based payload validation.
- The registered provider and its copied metadata are removed when `unregister_type` runs. Serialized components remain opaque and preserved after unload.
- Updated `northstarcpp_scene_chunked` test to cover C metadata registration, typed read/write, schema listing, overlap rejection, unregister and opaque preservation. The test passed.
- **Not yet completed:** automatic injection of this API table during the general external DLL plugin lifecycle, V2 reflection for complex types, version-dependent layouts, and a live third-party DLL integration smoke.

### P0 Details — multi-selection and batch property edits

- World Outliner now uses Qt `ExtendedSelection` (Ctrl/Shift).
- Selected actors are tracked by stable Scene V3 entity IDs and survive Outliner rebuilds and scene edit transactions.
- Explicit programmatic viewport selection resets the batch to one actor; user Outliner selection preserves the group.
- Shared numeric actor properties (position, rotation, scale, size, mass), visibility/collision toggles, and explicitly modified collider metadata are processed as a **single validated scene edit and one Undo record**.
- Batch changes compare against the primary selection; unchanged properties on other selected actors are never overwritten. Per-actor name/asset/material editing and collision mesh generation are disabled while multiple actors are selected.
- The main `--smoke` test now checks two-actor selection, batch transformation, preservation of unrelated Y values and atomic Undo, and passed on the alternate QA executable.
- **Not yet completed:** mixed-value controls, per-field explicit dirtiness independent of primary values, batch ECS component editing, multi-object gizmo and high-volume 10k actor GUI perf.

### P0 GPU / P2 hardware timing

- Both providers passed their out-of-process hardware `--full` probe on NVIDIA Quadro RTX 4000. Probes verified actual device identity and hardware adapter, GPU geometry readback, textured material and Material Graph readback, and GPU timing. Vulkan additionally verified GPU lights, skinning and compressed BC world surfaces. D3D12 reports missing/unsupported GPU skin and surface paths in this probe.
- Extended `tests/render_provider_device_probe.cpp` with `--stress=1000` (100–10,000 requested frames), periodic real pixel readback, failure-on-frame-error, per-frame CPU submission time and provider-reported GPU timestamp samples, plus p50/p95/p99 percentiles.
- Added permanent CTest cases `northstarcpp_device_probe_d3d12_stress` and `northstarcpp_device_probe_vulkan_stress`, each with a 90-second timeout. Alongside existing full hardware tests, **4/4 passed** after CMake reconfiguration.
- Each stress produced 1000 valid positive GPU timing samples over 1000 draw calls, without frame submission errors.
- **Not yet completed:** provider-backed ID-buffer picking, longer multi-hour crash/resize/device-lost matrix, Vulkan/D3D12 postprocess parity, and GPU profile of 10,000+ real Night City objects.

## Hardware results — 1000 frame probe

Device: NVIDIA Quadro RTX 4000 (driver-reported), non-software adapter.
Provider versions from tool: D3D12 1.6.0; Vulkan 0.1.0.
Measurements are milliseconds and **specific to the lightweight GPU probe**, not a Night City gameplay benchmark.

| Metric | Vulkan | D3D12 |
|---|---:|---:|
| GPU p50 | 0.042272 | 0.5088 |
| GPU p95 | 0.044352 | 3.70274 |
| GPU p99 | 0.0491453 | 4.7868 |
| CPU draw p50 | 16.6497 | 0.6107 |
| CPU draw p95 | 17.8135 | 2.39382 |
| CPU draw p99 | 34.1492 | 3.00603 |
| Valid GPU timing samples | 1000 | 1000 |
| Draw failures | 0 | 0 |

Vulkan's ~16.65 ms CPU draw median with ~0.04 ms GPU duration requires investigation into presentation / CPU blocking / frame pacing. These clocks measure different work and cannot be used alone as a backend ranking. Re-run on representative scenes and with explicit VSync controls before performance claims.

## QA and build safety

- `northstarcpp_scene_chunked`: PASS after ABI extension.
- CTest hardware full + 1000-frame stress cases (D3D12 and Vulkan): 4/4 PASS.
- Alternative linked QA Editor `northstarcpp_editor_qt_p0_batch_qa.exe --smoke`: PASS, covering multi-actor batch edit and Undo.
- Alternative linked QA Editor `northstarcpp_editor_qt_p0_batch_qa.exe --gpu-smoke`: PASS.
- Alternative linked QA Editor `northstarcpp_editor_qt_p0_batch_qa.exe --embedded-game-smoke`: PASS, fullscreen/IPC/Escape and workspace restoration.
- Alternative linked QA Editor `northstarcpp_editor_qt_p0_batch_qa.exe --embedded-render-smoke`: PASS, WARP rendered PIE/Escape.
- Source `git diff --check`: exit 0, only LF-to-CRLF warnings.
- The primary `northstarcpp_editor_qt.exe` was locked when linking. **Do not kill the user's running Editor**; used the same compiled objects to link a separate QA executable. This is not a deployed release binary.

## Modified project areas

- `engine/northstarcpp/modules/api/include/northstarcpp/scene_component_plugin.h`
- `engine/northstarcpp/modules/host/scene_component_registry.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window.hpp`
- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_setup.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_smoke.cpp`
- `tests/scene_chunked_smoke.cpp`
- `tests/render_provider_device_probe.cpp`
- `engine/northstarcpp/CMakeLists.txt`

## Prioritized next steps

1. P0: Provider-neutral GPU object-ID attachment and asynchronous readback picking in the actual Qt viewport; retain correct CPU triangle picking fallback.
2. P0: Upgrade the scene-component metadata for fields, enum ranges, string/paths, arrays and compound values; pass the host API through plugin lifecycle and verify real `LoadLibrary` / unload.
3. P0: Mixed-value batch Details, multi-object gizmo, multiple ECS component updates, one undoable transaction, robust error reporting.
4. P0: Device-lost, resize, hot-reload and repeated play/stress scenarios; inspect AV `0xC0000005` crash dumps rather than asserting it was eliminated by short tests.
5. P1: World Partition cell authoring overlay, HLOD, Data Layers, NavMesh; chunk and asset indexing benchmarks; complete Pause/Step/Eject.
6. P2: Unified correlated CPU/GPU timelines and frame profiles on real 10,000+ object scenes.

Local source work only; no production executable replacement, Git commit or push. The working tree includes other pre-existing in-progress changes and should not be reset.
