# NorthStarCpp — Scene System Parity, Phase 2
**Date:** 2026-11-17
**Repository:** NorthStarCpp (`engine/northstarcpp`)
**Architectural reference:** User-supplied `scene(2).zip`, previously inspected as a 210-entry subsystem reference. No reference source code was imported into NorthStarCpp.

## Integration overview

Phase 2 adds independent components for cross-provider GPU resource lifetime management, an indexed geometry HLOD aggregation baker, camera-aware recursive portal clipping, stable-ID incremental World Outliner rows and measured rolling scene CPU frame statistics. Existing `.nscn` NEF8 schema and Scene V3 entities/IDs are unchanged.

### 1 — GPU residency (policy layer; hardware binding still pending)

New file: `modules/api/include/northstarcpp/scene_gpu_residency.hpp`.

`scene_gpu::SectorGpuResidency` represents vertex/index buffers, textures and materials by `ResourceKey{ResourceKind, asset}`. A backend supplies real GPU allocation and release callbacks; the class does NOT allocate GPU resources itself. Distinct sectors share one key's allocation. `mark_submitted(sector, fence)` retains the latest queue submission fence. `collect(completed_fence)` releases only unreferenced resources whose upload/last-use fences are retired. Pending allocations can be reused without a new upload when a sector returns. Upload errors preserve the previous cell owner set, and successfully staged abandoned resources are retired after their upload fence. Statistics distinguish allocated bytes supplied by backend from pending-release bytes. `sector_stats(name)` reports per-sector referenced, shared and exclusive bytes separately, preventing shared resources from being misreported as exclusive costs.

**Missing:** actual D3D12 `ID3D12Resource` and Vulkan `VkBuffer/VkImage` callbacks, descriptor lifetimes, residency budgets, allocation tracking against driver-reported VRAM, per-cell dependency ownership wiring, shared resource upload queues, and fence/queue GPU integration. The standalone regression test is a mock GPU callback test, NOT a device-level memory eviction test.

### 2 — HLOD geometry aggregation (real mesh, not final reduced LOD)

New file: `modules/api/include/northstarcpp/scene_hlod_baker.hpp`.

`scene_hlod::bake` accepts static native `MeshAsset` inputs with 3×4 affine world transforms and optional material overrides. It emits real indexed world-space triangles with UV preservation, proper inverse-transpose normal transformation, mirrored-winding handling, exact-key welded vertices, deterministic material sections and world AABB bounds. Rejects dynamic meshes, invalid index/section ranges and degenerate transforms. It is real aggregated geometry, not an AABB placeholder.

**Missing:** using this mesh in the existing viewport's HLOD render path (which currently draws AABB proxies), material-aware geometric simplification/decimation, texture atlas baking, DDC artifacts, device mesh uploads and streaming of baked assets. There is no claim that draw calls/FPS have improved in game.

### 3 — Portal-frustum recursion with real FPS camera

New file: `modules/api/include/northstarcpp/scene_camera_frustum.hpp`.

Changed:
- `modules/host/scene_world.cpp`
- `modules/editor/window.cpp`
- `modules/editor/window_runtime_scene_streaming.inc`

Each scene portal is still authored as an AABB. On passage, the recursion approximates a rectangle on the portal's thinnest AABB axis and adds four camera-to-edge planes to the incoming six-plane frustum. It walks alternative distinct portal paths, preserving the parent clip for other exits and refusing repeated edges within one path. A capped traversal fails open using the old broad connectivity result instead of mistakenly hiding geometry in high-complexity scenes.

The FPS Player constructs a genuine six-plane camera frustum from `CameraPose` (yaw, pitch, FOV, near/far, position) and the current Win32 viewport aspect ratio. An invalid camera projection clears frustum culling with a single warning. Non-FPS/editor views remain on legacy portal connectivity until a dedicated camera projection is provided.

**Missing:** freely oriented authored polygon portals instead of AABB rectangles, complete editor/cinematic-camera hookup, GPU occlusion queries and in-game visual artifact validation.

### 4 — Incremental Qt World Outliner

Changed:
- `modules/editor/qt_editor_window.cpp`
- `modules/editor/qt_editor_window_smoke.cpp`

On a structural change `updateOutliner` detaches old actor rows, clears no-longer-needed layer folders and reuses `QTreeWidgetItem` instances by stable entity ID. The new hierarchy receives existing rows where possible; removed and filtered-out actor rows are destroyed, and new actors allocate new rows. The already-present signature-based transform-only fast path remains. Smoke regression covers both transform-only changes and detach/reparent/restore preserving pointer identity, and preserves the subsequent NEF8 persistence expectations.

**Missing:** a fully incremental `QAbstractItemModel` for large structural changes; layer folders and visible-index/filter evaluation are still rebuilt on presentation changes.

### 5 — Scene Cost Profiler

New file: `modules/api/include/northstarcpp/scene_cost_profiler.hpp`.

`SceneCostProfiler` stores rolling, fixed-capacity scene updates (1024 by default), reports CPU p50/p95/p99 with deterministic nearest-rank percentiles, and permits optional GPU timings/VRAM bytes for a matching frame sequence from provider measurements. CSV export includes explicit blank cells for measurements not available. `SceneWorld::update` automatically records measured scene CPU time; the Win32 Streaming Inspector reports CPU quantiles and per-cell CPU-side inventory, marking provider GPU measurements unavailable when absent.

**Missing:** end-to-end aligned asynchronous GPU timestamp queries from both D3D12/Vulkan, actual VRAM memory aggregation per cell, full 1000+ frame KaylaNightCity camera/vehicle profile, cross-provider p95/p99 benchmarking, and persistent CSV capture UI/CLI.

## Test and build evidence

- New targeted CTests: `northstarcpp_scene_gpu_residency`, `northstarcpp_scene_hlod_baker`, `northstarcpp_scene_cost_profiler` — individually passed (3/3).
- Updated `northstarcpp_scene_world_v3` passes including recursive portal aperture occlusion and FPS camera projection from the real `CameraPose`.
- Previously run six Scene/NEF8 regression tests were green; rerun is recommended after other agents finish concurrent edits.
- `northstarcpp_player` compiled and linked with camera-frustum integration and profiler inspector changes in the local build.
- `northstarcpp_editor_qt` compiled and linked; `northstarcpp_qt_editor_authoring` passed after new row-reparent regression and NEF8 state restoration.
- No full clean Release QA, GPU readback/screenshot comparison, actual device-memory eviction, 1000-frame performance capture, distribution replacement or Git commit/push was performed.

## Recommended next acceptance sequence

1. Bind concrete GPU resources and actual queue-completion fences to `SectorGpuResidency` independently in D3D12 and Vulkan. Track resource keys by each cell's mesh/material/texture dependency graph; include always-loaded and shared assets. Test no premature `Release`/`vkDestroy*` under cross-cell sharing and repeated camera teleport.
2. Plug `scene_hlod::bake` into HLOD content generation and a content-hashed DDC. Add simplification plus material/UV/texture validation, register baked assets with NEF8 cells, and replace the runtime AABB draw path only after baked meshes are confirmed GPU resident. Verify pixel output.
3. Expose polygon portal authoring and update all camera paths. Test nested portals, multiple routes into one room, camera inside door AABB, closed/open door and path cap.
4. Replace QTreeWidget's remaining full structural rebuild with model-level insert/remove/move notifications after stable-ID diffing, retaining selection/expanded state.
5. Record aligned CPU/GPU timings and VRAM snapshots for >=1000 frames in KaylaNightCity, compare D3D12/Vulkan, cold/warm travel, p50/p95/p99 and device-memory peaks. Do not infer speedups from unit tests.
