# NorthStarCpp Qt Editor — UE5.3.2 parity P0/P2 implementation progress

Date: 2026-11-15
Workspace: C:/Users/Kayla/Documents/NorthStar/NorthStarCpp
Build tree: out/editor-ue-parity-qa-20261115
Reference audit: docs/UE532_EDITOR_PARITY_AUDIT_2026-11-15.md

## Delivered in this iteration

### P0 — Reproducible editor build and GPU smoke

- Built the current Qt 6 Editor and native Player using GCC/MinGW64, CMake and Ninja in an isolated build directory.
- The original V8 runtime staging was blocked by libcurl TLS certificate error 77. Retrieved the official V8 15.8.62 archive with the Windows system TLS client and verified the pinned SHA-256 **9924fe95ae8a05b35f728da8258bd1d72a8810edc0179d65fd3018c7845489e3**. Cached archive: `out/v8-win64-rel-15.8.62.zip`. No certificate/hash validation bypass was used.
- Reconfigured CMake after upstream checkout edits added `northstarcpp_crash` to the editor link target. Final editor build completed with exit code 0.
- The native Qt GPU Surface smoke and editor authoring tests passed. This does not establish Vulkan/D3D12 GPU parity or eliminate all causes of the previously reported access violation; exhaustive stability and cross-provider tests remain necessary.

### P0 — Typed ECS component property inspector (partial)

- Extended `SceneComponentType` and `SceneComponentSchemaInfo` with owned, optional property metadata (`UInt32` / `Float32`, little-endian 4-byte offsets).
- Added bounded typed read/write functions for component payloads with scalar type, finite-float and payload bounds checks.
- Reflected descriptors are validated at registration: <=64 properties, unique labels/offsets, 4-byte alignment, offset bounds.
- The registered built-in `engine.geometry.primitive` schema now reports Kind, Shape, Major Radius, Minor Radius, Segments U/V.
- The Qt component dialog renders numeric controls when metadata and payload version match. Editing a control updates raw hexadecimal bytes, while raw changes update the numeric controls; normal scene validation and Undo/Redo transaction semantics remain authoritative.
- Original unknown/unregistered plugin payloads remain opaque, lossless and editable as raw hex.
- The existing native external-plugin C ABI has not been broken, but the stable C metadata registration extension for external DLLs is **not implemented yet**.
- Multi-actor Details editing, typed arrays/structs/enums, field constraints, mixed values and nested property trees are **not implemented yet**.

### P2 — CPU-side editor frame submission quantiles (partial)

- Added `modules/editor/viewport_frame_profile.hpp` with a rolling window of 1024 valid samples; p50, p95 and p99 use linearly interpolated sorted quantiles.
- `QtEditorViewport::drawGpu()` records CPU wall-time around native `Viewport::draw()`.
- The viewport's asset summary shows `CPU draw p50/p95/p99` when data is present.
- A standalone 1000-frame deterministic test checks quantiles, finite-sample gating, rolling eviction and reset.
- **These are CPU draw-call wall times, not GPU timestamps or total frame times.** Vulkan/D3D12 timestamp queries, CPU/GPU event correlation and 1000+ live-frame sampling still remain.

## Changed code

- `engine/northstarcpp/modules/api/include/northstarcpp/scene_component_registry.hpp`
- `engine/northstarcpp/modules/host/scene_component_registry.cpp`
- `engine/northstarcpp/modules/host/geometry_scene.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_viewport.hpp`
- `engine/northstarcpp/modules/editor/qt_editor_viewport.cpp`
- `engine/northstarcpp/modules/editor/viewport_frame_profile.hpp` (new)
- `tests/scene_chunked_smoke.cpp`
- `tests/viewport_frame_profile_smoke.cpp` (new)
- `engine/northstarcpp/CMakeLists.txt` (new test target)
- Prior continuation: Qt Details search and inspector smoke refinements from the previous iteration.

## Verification

After the final build, the following nine test targets passed in the isolated build tree:

| CTest target | Result |
| --- | --- |
| northstarcpp_viewport_frame_quantiles | PASS |
| northstarcpp_inspector_scroll_components | PASS |
| northstarcpp_qt_editor_authoring | PASS |
| northstarcpp_qt_editor_gpu_surface | PASS |
| northstarcpp_scene_chunked | PASS |
| northstarcpp_play_in_editor_qt | PASS |
| northstarcpp_play_in_editor_render_qt | PASS |
| northstarcpp_qt_editor_mouse_look | PASS |
| northstarcpp_qt_editor_native_ui | PASS |

Command used for the first group:
```powershell
C:/msys64/mingw64/bin/ctest.exe --test-dir out/editor-ue-parity-qa-20261115 -R '^(northstarcpp_inspector_scroll_components|northstarcpp_qt_editor_authoring|northstarcpp_qt_editor_gpu_surface|northstarcpp_scene_chunked|northstarcpp_viewport_frame_quantiles)$' --output-on-failure
```

The Play-In-Editor and camera/native UI tests were run separately. `git diff --check` returned exit code 0 (with line ending conversion warnings). Full workspace CTest, long-run GPU profiling and product release package checks have not been completed.

## Remaining scope by priority

- P0 GPU: Vulkan vs D3D12 hardware parity, frame graph, real materials, lights, post-processing and ID buffer picking. Reproduce previous `0xC0000005` under sustained load.
- P0 Details: registered external-plugin C metadata ABI, typed layout coverage, multi-selection mixed values, safe batch transactions.
- P1 World: streaming cells/World Partition authoring overlay, HLOD, Data Layers, collision and NavMesh visualization.
- P1 Content Browser: measurable large-volume NEF8 indexing, dependency graphs, asynchronous virtualization and cache profiling.
- P1 PIE: edit-world isolation, in-editor Pause/Step/Eject, stop/restart restoration with crash isolation.
- P2: real hardware GPU timestamps for Vulkan/D3D12, 1000+ live frame p50/p95/p99, memory and large-scene benchmarks.

## Release discipline

All changes are local to the current working tree, which contains unrelated in-progress modifications. No user-running Editor or production executable was replaced, and no Git commit or push was performed. The isolated QA executable is `out/editor-ue-parity-qa-20261115/northstarcpp_editor_qt.exe`. Promote only after full regression, hardware smoke and packaging acceptance.
