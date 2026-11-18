# NorthStarCpp scene parity — sector telemetry, portal culling, Qt Outliner
Date: 2026-11-17. Reference: user-provided `scene(2).zip` (210 entries; scene/portals, scene/lod, scene/streamer and scene/debug/SceneCostTracker are reference architecture only).

## Implemented in current C++ working tree

### SceneWorld per-sector telemetry
- Public `SceneCellTelemetry` and `SceneWorld::cell_telemetry()` expose authored entities, distinct authored asset references, runtime effective ownership, resident and visible counts, HLOD-suppressed counts and active HLOD proxy counts, plus logical cell loaded/verified status.
- The counters are refreshed during `SceneWorld::update` with linear sweeps and no per-frame string copies, while authored asset counts are computed once at construction.
- `SceneStreamingDiagnostics` now has `last_update_cpu_us` and `portals_frustum_rejected`. `last_update_cpu_us` measures SceneWorld update on CPU; it does NOT measure the GPU or entire frame, and there are no fabricated VRAM numbers.

### Optional conservative portal-frustum gate
- `SceneViewFrustum` consists of six finite, nondegenerate half-space planes; `set_view_frustum` validates them, `clear_view_frustum` restores legacy behavior.
- Portal AABB outside the supplied camera frustum does not propagate room reachability during that update.
- Room-to-portal adjacency is precomputed at construction, avoiding repeated O(rooms × portals) full-list scans during BFS.
- This is deliberately only conservative volume/frustum rejection; it is NOT recursive portal polygon clipping, occlusion queries or a full interior PVS. Camera injection at the window/viewport callsite remains pending: current callers not providing a frustum retain the existing connectivity-only behavior.

### Qt World Outliner incremental fast-path
- `QtEditorWindow::updateOutliner` hashes only scene presentation fields (IDs/parent/name/layer/visibility/asset, component types and relevant light data), filter and sort state. Transform-only edits use the existing QTreeWidgetItems rather than destroying/recreating them.
- Stable-ID item index allows selection changes without rebuilding the tree.
- Structural changes, renames, visibility, type changes, filtering and sort trigger a full refresh; invalid inline renames explicitly invalidate the cache.
- `qt_editor_window_smoke.cpp` now asserts tree-item identity survives a transform-only authoring transaction.

## Verification
- Native `northstarcpp_scene_world_smoke` compiled and passed, including new per-cell telemetry and conservative portal-view tests.
- All six targeted Scene/NEF8/streaming CTests passed: `scene_world_v3`, `scene_binary`, `scene_chunk`, `scene_async_vfs`, `scene_chunked`, `scene_chunked_stress`.
- `northstarcpp_editor_qt` previously compiled and linked with Outliner changes, and `northstarcpp_qt_editor_authoring` passed after the transform-only regression was added.
- A subsequent build had to accommodate concurrent new `northstarcpp_world_foundation` target: it is now part of the `NorthStarCppTargets` export set, enabling CMake regeneration and runtime compilation.
- Latest attempted full Qt/Player rebuild was blocked at **linking** an in-use `northstarcpp_player.exe` (Windows Permission denied); the running executable was not terminated. This attempt does not constitute a clean full Release build/installation.

## Not yet complete — reference-parity release gates

| Requirement | Status and next engineering gate |
| --- | --- |
| GPU streaming/VRAM eviction per cell | NOT DONE: integrate shared mesh/material/texture usage reference counting and D3D12/Vulkan fence-safe retirement, shader/material dependency traversal, and measured VRAM statistics |
| Baked HLOD meshes | NOT DONE: current HLOD still uses AABB cube proxies; build material-aware static mesh aggregation/simplification, texture atlas/material preservation, DDC keying and LOD switching with no visual mismatch |
| Recursive portal-frustum culling | PARTIAL: optional portal AABB-to-camera-frustum rejection works; actual camera/frustum binding and recursive clipped portal polygons are pending |
| Qt World Outliner | PARTIAL: unchanged presentation now preserves item identity; structural edits still rebuild all rows rather than applying a minimal tree diff |
| SceneCostTracker parity | PARTIAL: per-cell CPU/entity/asset counters implemented; GPU memory, per-sector render timing and per-frame p50/p95/p99 over >=1000 frames not measured |

Do not state an FPS, memory or loading-speed improvement until an identical large KaylaNightCity scene is profiled before/after on each provider. Do not replace distributed binaries or claim reference parity before a clean Player/Editor Release build, actual camera hookup, GPU residency tests and in-game visual acceptance.
