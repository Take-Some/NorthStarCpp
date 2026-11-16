# NorthStarCpp — P0 Async GPU Object Picking & Material Graph Editor

**Date:** 2026-11-16
**Workspace:** `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
**Isolated Release QA:** `_qa_editor_mixed`
**Source control:** changes local; no Git commit/push, no production DLL/EXE replacement

## 1. Additive renderer picking ABI v3

`engine/northstarcpp/modules/api/include/northstarcpp/render_pick_api.h`

`NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI = 0x52504b03` preserves render ABI 5 and v1/v2 object-picking APIs. A request carries unique request ID, scene generation, physical HWND pixel X/Y; a result carries original request ID, scene generation, stable 64-bit Entity ID and status `PENDING / READY / STALE / UNKNOWN`. Interfaces: `enqueue`, `poll`, `invalidate_before`. Host's `module_contracts.hpp` validates the full table.

### D3D12 implementation

Files: `modules/render/d3d12/renderer_pick_async.cpp` (new); `renderer_private.hpp`, `renderer_resources.cpp`, `d3d12.cpp`, CMake.

- Dedicated Direct GPU command queue and fence, **three persistent slots**, each with command allocator/list, snapshot of uploaded vertex data and camera matrix, RGBA8 object-ID target, D32 depth target, descriptors and 256-byte-aligned staging buffer.
- Work enqueued without CPU `wait()`; `poll` reads `GetCompletedValue()` and accesses readback mapping only after the fence completes. Bound to three concurrent requests; busy submission returns failure for caller's geometry-raycast fallback.
- Generation invalidation is logical; in-flight GPU resources remain alive until the fence signals. Slot reuse and swapchain-size changes reallocate only safe completed slots; destruction can wait after cleanup.
- Native adapter: NVIDIA Quadro RTX 4000 (not WARP).

### Vulkan implementation

Files: `modules/render/vulkan/renderer_pick_async.cpp` (new); `renderer_private.hpp`, `renderer_device.cpp`, `vulkan.cpp`, CMake.

- **Three slots** with independent command pools/command buffers, VkFence, persistent host-coherent uploaded vertices, RGBA8 UNORM and depth images and their views/memory, four-byte host readback.
- GPU submitted via `vkQueueSubmit`; `poll` checks `vkGetFenceStatus` (no waiting). Same request/generation/invalidation semantics.
- Device shutdown only destroys resources **after** `vkDeviceWaitIdle`. Presentation and normal rendering remain provider-owned.
- Native adapter: NVIDIA Quadro RTX 4000 (not llvmpipe/software).

### Editor / scene integration

Files: `modules/viewport/viewport.hpp`, `viewport.cpp`, `modules/editor/qt_editor_viewport.hpp`, `qt_editor_viewport.cpp`.

- Host tracks monotonically increasing scene generations and request IDs.
- After a GPU viewport click, Qt translates logical pointer position to physical client pixels, calls nonblocking enqueue, and polls with the existing ~16ms Qt GPU timer; selection update occurs only for ready results with current scene generation and still-existing Entity IDs.
- Scene change, camera redraw, selection programmatic updates, draw-mode switch and resize discard pending selections and/or invalidate provider generation. Busy slots, incomplete opaque ID mesh coverage or unsupported providers revert to accurate geometric raycast, **never a blocking GPU wait on a supported async click**.
- The earlier v2 stable ECS Entity ID stream for OBJ/YDD/YDR/CPU-mirrored animated geometry and transparent-material fallback remains intact.

### Hardware QA

Both `render_vulkan.dll` and `render_d3d12.dll` passed direct GPU probe with:
`gpu_pick_async_supported=true`, `gpu_pick_async_verified=true`,
`gpu_pick_async_samples=69`, `software_adapter=false`.
Per-provider 69-result hardware series: overlapped 64-bit object IDs, updated geometry/pose, stale generation suppression, repeated slot reuse. Full extended CTest:
- 9/9 PASS in 46.91 seconds: D3D11 device, D3D12 hardware + 1000-frame stress, Vulkan hardware + 1000-frame stress, Qt Editor authoring, Qt GPU Viewport, Play-in-Editor WARP render, ECS DLL lifecycle.
- Total 2000 GPU provider stress frames; these stress tests do NOT represent 2000 async pick operations and are not Night City benchmarks.

## 2. Material Graph Editor — authoring ergonomics

Files: `modules/editor/qt_material_graph_editor.hpp`, `qt_material_graph_editor.cpp`, `qt_editor_main.cpp` (extended smoke).

- QGraphicsScene canvas now has scale-aware **16-unit minor / 128-unit major analytical grid**, Fit Graph and Focus Selection, F and Home navigation, 16-unit optional snap on drag commit for nodes/groups.
- Split view: graph canvas and persistent, scrollable **Node Details** inspector; updates with selection. Operators expose scalar constants, RGB(A), vector2/3 channels, parameter names, texture slots, component extraction, explicit cast destination, function output references.
- Edits go through `commit()`, existing undo/redo and actual material graph compiler; UI data changes deferred one event turn to prevent destroying Qt input controls inside their signal callback. Node selection restored on graph re-render; searchable canvas and typed wires retained.
- Toolbar/status now displays counts of nodes, connections and comment groups. Validation text reports **CPU compiler validation**, not unmeasured GPU compilation.
- **Bug discovered and fixed:** when MaterialGraphEditor was destroyed, Qt child `QGraphicsScene` emitted `selectionChanged` after graph's C++ maps had already been destroyed, causing `0xC0000374` / GDB SIGSEGV in `updateNodeDetails()`. The explicit destructor disconnects the scene from the editor and clears items with blocked signals before derived members are destroyed.

Graph's native YMAT XML authoring path is preserved; existing legacy JSON / Material Function entry points remain in code and require a dedicated migration audit for strict XML-only source-of-truth policy. No Unreal source, graphics or code copied.

### Material QA

- `northstarcpp_qt_material_graph_authoring`: PASS — now includes an actual Qt Node Details scalar edit to 0.625, graph serialization and compiler validation.
- `northstarcpp_qt_material_function_gpu`: PASS
- `northstarcpp_qt_material_instance_authoring`: PASS
- **12/12 sequential clean process starts** of `--material-graph-smoke`: PASS after lifetime fix; zero observed `0xC0000374`.

## 3. Workspace integration

Editor and Player re-linked in isolated Release QA. Concurrent changes to Asset Browser introduced missing linkage of new collection module. A necessary CMake source-list integration fix adds `modules/editor/qt_content_browser_collections.cpp`; `qt_audio_preview_dialog.cpp` was already included.

`git diff --check`: exit 0 (pre-existing LF->CRLF warnings).

## 4. Remaining work — do NOT claim full UE parity

**GPU P0 acceptance:** 69 async picks per provider prove the mechanics, but not sustained 10,000+ actor Night City usage. Need resize/Alt-Tab/device-lost/hot-reload and many in-flight simultaneous picks; readback p50/p95/p99, memory residency/leak instrumentation, real YDD/YDR/YCD click acceptance, GPU skin-native ID shader and masked alpha textures. Vulkan uses the same queue as normal rendering so a readback may still consume GPU bandwidth though the Qt thread is not blocking.

**Material Editor UE-level parity:** needs significantly expanded compiler-supported node library, per-node real GPU thumbnail previews, breakpoints/debug shading, graph material attributes and material layers, real-time asynchronous shader permutation/PSO feedback, shader instruction counts and texture samples, Material Function dependency-driven recompilation, robust large-graph layout, GPU visual comparisons with expected shading, editable advanced material settings, and strict XML-only audit of older material/Material Function entrypoints. Current work is improved authoring and stability, not full equivalence.

**Production and Git:** intentionally unchanged; isolated `_qa_editor_mixed/northstarcpp_editor_qt.exe` plus updated Vulkan/D3D12 provider DLLs are QA builds only.
