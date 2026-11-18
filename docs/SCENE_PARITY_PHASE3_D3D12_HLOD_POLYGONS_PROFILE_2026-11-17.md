# Scene System — Phase 3: D3D12 eviction, real HLOD rendering, authored portal polygons, profiling
Date: 2026-11-17
Repository: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
Scope: incrementally close the remaining gaps against the supplied scene/streaming architecture. Existing NEF8 `.nscn` file format is retained.

## Implemented

### Actual D3D12 mesh eviction
- New optional C ABI: `modules/api/include/northstarcpp/render_rigid_residency_api.h`, version 1, `NORTHSTARCPP_RENDER_RIGID_RESIDENCY_MODULE_ABI`.
- D3D12 provider: `modules/render/d3d12/d3d12.cpp`, `renderer_private.hpp`, `renderer_rigid_instances.cpp`.
- `release_rigid_meshes(keys,count)` validates all keys against currently submitted draw instances; refuses to release a currently referenced mesh; issues a queue fence, synchronously waits for completion once per batch, then destroys the actual GPU DEFAULT-heap vertex/index `ID3D12Resource` allocations and decrements `rigid_mesh_bytes`.
- Viewport: `modules/viewport/viewport.cpp` discovers the extension if available, collects at most 32 stale meshes after submitting the new frame, and removes GPU allocations that are no longer drawn, with a 60-frame grace period to avoid eviction/re-upload churn. A confirmed sector-unload transition bypasses that grace period. ABI v1 and non-D3D12 providers retain existing behavior. Errors do not silently mark a GPU mesh as unloaded.
- An extended **D3D12 WARP test** (actual provider, actual GPU allocations) verifies refusal to release active draw meshes, successful release after `set_instances(nullptr,0)`, and GPU-reported resident mesh count/bytes dropping to zero.

**Remaining:** explicit Vulkan persistent indexed mesh API (Vulkan currently has no equivalent to D3D12 `NorthStarRenderRigidInstancesApi`), material/texture residency and descriptor retirement, shared resource refcounts hooked to actual provider allocations across sectors, and asynchronous non-blocking retirements. Current D3D12 retirement uses a correctness-first **synchronous fence wait**, which can produce unload hitches. The provider-neutral `SectorGpuResidency` from Phase 2 remains a tested ownership contract, not yet the owner of all D3D12/Vulkan allocations.

### Actual HLOD geometry in Viewport
- New helper `modules/viewport/viewport_hlod_geometry.hpp`: converts static SceneObject Euler/scale/size/position into the exact 3×4 bake matrix used by native scene rendering; caches baked indexed `MeshAsset` meshes by source scene, entity IDs, asset references, transform and material overrides.
- Viewport now attempts `scene_hlod::bake` for the active 64m/256m clusters, submits material-preserving UV-indexed triangles to existing native triangle/material batch streams, and counts `baked_hlod_clusters` in `ViewportAssetStats`.
- Native YDD/YDR sources not already cached are requested on the existing asynchronous preloader, with at most eight new enqueue requests per frame. Completed static model/texture-material dependencies are added to the existing asset caches; skinned assets are not merged into static geometry. Incomplete/unsupported clusters remain AABB fallback.
- Baked cache is bounded to 128 entries and 128 MiB (vertex/index payload), prevents repeating the bake on camera-only frames, and is cleared on VFS/project mount changes. Original ECS and physics actors remain untouched; original source meshes of selected HLOD clusters remain CPU resident while used for baking.

**Remaining:** real triangle decimation/simplification, content-addressed **Derived Data Cache**, streaming of prebuilt HLOD binaries, cold-start OBJ async loading, scene-specific GPU upload of baked mesh buffers, comprehensive material transparency/texture validation, and screenshot-based visual QA. The current baked mesh is a real merged mesh, but not necessarily lower triangle count; no FPS or memory improvement is claimed. The 128 MiB cap is memory for cached HLOD geometry, not an overall engine memory limit.

### Authored planar convex portal polygons
- `modules/host/scene_portal_polygon.hpp` parses `engine.scene.portal_polygon` scene-level XML components:
  `<NorthStarPortalPolygon version="1" name="PortalName"><Vertex x="..." y="..." z="..."/>...</NorthStarPortalPolygon>`.
- `SceneWorld` validates name/vertex count/finite values/planarity/convexity and maps optional vertices to the existing ScenePortal. The recursive camera aperture clip uses the authored polygon if present; otherwise the legacy AABB rectangle.
- Malformed optional XML warns and falls back to AABB without crashing. The component survives NEF8 scene binary serialization without schema migration.
- Test `northstarcpp_scene_portal_polygon` verifies narrow authored aperture occlusion, broad AABB fallback, malformed optional data behavior and full NEF8 XML preservation.

**Remaining:** concave outline decomposition/triangulation, nonplanar/mesh portal authoring, live Qt inspector graph UI, editor and cinematic camera frustum binding, portal-aware GPU occlusion with visual validation.

### Scene Profiler: real GPU samples (opt-in) and project CPU baseline
- Existing `SceneCostProfiler` still records `SceneWorld::update` CPU time continuously.
- `Viewport::draw` now conditionally samples the actual `NorthStarRenderTimingApi` after the submitted frame when **`NORTHSTARCPP_SCENE_GPU_PROFILE=1`**; correlates the sample with the latest `SceneWorld` sequence. It can also record the D3D12 rigid indexed geometry bytes when that provider exposes `NorthStarRigidInstanceStats`.
- This synchronous provider timing API can introduce measurement-induced stalls. Consequently it is explicitly disabled in production by default. Rigid indexed geometry bytes do **not** represent total device VRAM; materials, textures, G-buffer and transient allocations are missing.
- New `northstarcpp_scene_project_profile` CLI can profile 1000+ actual scene CPU updates; optionally generates a *separately labeled synthetic* tiled workload based on the original authored objects.

#### Actual 1000-frame KaylaNightCity baseline
Run: `northstarcpp_scene_project_profile.exe Projects/KaylaNightCity/main.nscn 1000 out/build/kaylanightcity_scene_cpu_1000.csv`
- Actual scene: **154 authored entities, 0 streaming cells, 0 portals, 7 HLOD clusters**.
- SceneWorld CPU p50/p95/p99 = **1/1/1 µs**, 1000 CPU samples, 0 GPU samples. These values represent only the lightweight SceneWorld update, not the complete frame.
- The current `main.nscn` cannot be called a large streamed city.

#### Synthetic 8x8 scale regression (NOT the original scene)
Run: `northstarcpp_scene_project_profile.exe Projects/KaylaNightCity/main.nscn 1000 out/build/kaylanightcity_synthetic_8x8_scene_cpu_1000.csv 8`
- Derived by tiling authored objects without modifying project files: **9856 entities, 64 streaming cells, 432 HLOD clusters**.
- SceneWorld CPU p50/p95/p99 = **38/42/50 µs**, 1000 samples, 0 GPU samples.
- This is a useful CPU selection/visibility scale check, but not a 1000-frame GPU benchmark of a real large city; no physics/IO/render workload was measured.

### Qt World Outliner
- Phase 2 already preserves stable QTreeWidgetItem identity on transform-only edits and reuses existing actor rows across reparenting and structural scene changes. No new `QAbstractItemModel` replacement was completed in Phase 3.
- Still necessary: a true model/view hierarchy with row-level insert/remove/move notifications and persistent index/selection ownership, for large edit batches.

## Validation and release blockers

- `northstarcpp_render_d3d12` compiled and linked after new per-mesh retirement ABI.
- `northstarcpp_viewport` compiled with post-frame GPU resource eviction, real HLOD triangle submission, async cold YDD/YDR prefetch and opt-in GPU sampling.
- `northstarcpp_player` compiled and linked after viewport/portal changes in `out/build`.
- `northstarcpp_d3d12_rigid_instances_warp` PASSED on actual D3D12 WARP GPU allocations.
- `northstarcpp_viewport_hlod_geometry` PASSED: merged UV/material geometry, cache reuse, invalidation on transform, fallback for missing and skinned source.
- `northstarcpp_scene_portal_polygon` PASSED: authored XML polygon, frustum narrowing, malformed fallback and NEF8 roundtrip.
- `northstarcpp_scene_world_v3`, `northstarcpp_scene_hlod_baker`, `northstarcpp_scene_gpu_residency`, `northstarcpp_scene_cost_profiler` PASSED.
- Latest Qt Editor *link* attempt was blocked by a locked `northstarcpp_editor_qt.exe` (Windows Permission denied). Do not forcibly terminate the running app; this is not a completed current clean Qt link.
- The scene project profiler target was manually compiled/linked against the already built runtime libraries when CMake's shared NSP packing failed due a locked archive output; CLI runs returned exit code 0. The project source was not altered.
- There is **no clean standalone Release QA**, Vulkan mesh residency implementation, full device-memory/descriptor life-cycle benchmark, 1000-frame GPU capture of full KaylaNightCity, or measured reference-parity visual acceptance.

No production `dist` binaries were installed or replaced, and no Git commit/push was requested or performed.

## Next strict acceptance gates

1. Vulkan immutable indexed mesh provider parity (upload/update/draw/evict) with timeline or binary fence-safe deletion; batch D3D12 retirement nonblocking; unify resource ownership in SectorGpuResidency.
2. HLOD content-hash DDC, asynchronous simplified mesh cooker, material/UV atlas tests, and streaming prebuilt mesh assets to GPU.
3. Authoring/persistence of polygon portal contours in Qt Editor, concave clipping, actual editor/cinematic camera attachment, optional GPU occlusion.
4. Replace QTreeWidget backing with a QAbstractItemModel and stable-ID insert/remove/move transactions.
5. Build a real partitioned large-city benchmark scene and run **>=1000 full hardware-rendered frames** on both Vulkan and D3D12, reporting CPU/GPU p50/p95/p99, VRAM (geometry vs textures vs transient), upload/eviction costs, peak allocation, asset DDC hit rates and pixel-image regressions.
