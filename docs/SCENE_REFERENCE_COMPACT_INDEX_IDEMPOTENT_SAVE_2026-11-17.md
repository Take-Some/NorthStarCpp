# Scene architecture and persistence: targeted optimization — 2026-11-17

## Input and architectural scope

Reference archive: `scene(2).zip` (210 ZIP entries), containing scene source areas `loader/`, `streamer/`, `lod/`, `portals/`, `volume/`, `world/` and `debug/`. It is an architectural reference, **not** a serialized game-scene asset. It contains proprietary, platform-specific dependencies and is not compiled or copied into NorthStarCpp.

NorthStarCpp already has independent equivalents: Scene V3 authoring DTO, Flecs-backed runtime, `SceneWorld` spatial queries, stream budgets, portal/layer masks, provisional HLOD, NEF8 `.nscn` chunked ListFile, asynchronous cell decoding via VFS ranges, collision residency and asset requests. See `SCENE_V3_STREAMING.md` and `BINARY_SCENES.md`.

## Changes applied

### 1. Compact scene spatial index

Files:
- `engine/northstarcpp/modules/api/include/northstarcpp/scene_world.hpp`
- `engine/northstarcpp/modules/host/scene_world.cpp`

Previously, `SceneSpatialIndex` duplicated the full `Scene::objects` vector, including names and `SceneNode` strings that spatial queries never use. It now retains an `ObjectBounds` record per source entity with stable ID, position and precomputed half extent. The authored `Scene` remains the authoritative owner of hierarchy, layers, assets, scripts, and component metadata. Entity order and query indices are preserved.

Cell-bucket membership now stores each object's slot. Moving an entity uses swap-erase, O(1) bucket removal, rather than a linear scan/erase of source cell members. Query output continues to sort by original source index. The prevalidation cell-key array is reused between frames to avoid repeated allocation. Entire updates still validate all stable IDs and positions before changing bucket membership.

Also removed a duplicated `scene_pawn.hpp` include.

### 2. Idempotent, atomic scene persistence

File: `engine/northstarcpp/modules/host/scene.cpp`.

`save_scene` still validates and emits the canonical NEF8 binary representation and maintains Windows temporary-file + atomic replace semantics. Before writing, a bounded 16 KiB blockwise comparison checks whether the existing output bytes already match the canonical serialized bytes.

- Unchanged bytes: no rewrite, no file timestamp change; avoids downstream content-watcher invalidation and unnecessary SSD writes.
- Changed bytes: closes the old read handle, writes a temporary file, flushes it, and atomically replaces the original.
- Missing/unreadable destination: normal write path.
- No changes to NEF8 schema, document IDs, compression, plugin records, or migration contracts.

This optimization avoids duplicate *disk writes*; serialization and the comparison read are still performed. Further gains require incremental chunk reuse or dirty-record tracking.

### 3. Tests and compatibility

Files:
- `tests/scene_world_smoke.cpp`
- `tests/scene_binary_smoke.cpp`

Additional tests exercise 256 independently indexed objects through 12 rounds of dense-bucket movements, verifying exact ascending original indices for both near/far queries and detecting missing/duplicate entries. Scene save tests verify that the same file is not rewritten (using an intentionally changed timestamp), whereas changed metadata is saved correctly and the original scene roundtrips deterministically.

The binary-scene regression had an obsolete expectation that importing a legacy text scene retained V2. The current runtime upgrades legacy text to the component-based V3 representation. The test now verifies V3 migration and binary roundtrip, matching the existing implementation.

Verified via the local `out/build` tree:
- `northstarcpp_scene_world_v3`: PASS
- `northstarcpp_scene_binary`: PASS
- `northstarcpp_scene_chunk`: PASS
- `northstarcpp_scene_async_vfs`: PASS
- `northstarcpp_scene_chunked`: PASS
- `northstarcpp_scene_chunked_stress`: PASS

Six of six selected CTests passed. Targeted CMake compile/link completed. No full clean Release Editor/Player build, visual in-editor acceptance, FPS improvement, GPU-memory or frame-time benchmark has been claimed.

## Current canonical scene file contract

Authoring state: `Scene` + persistent stable entity IDs, component payloads, layers, cells, portals and stream settings.

Disk: NEF8 `.nscn`, schema 2 with a typed global chunk, independently compressed sector chunks, checksums and retained unknown component records. Legacy `.nscene` remains accepted as import input; explicit save writes NEF8 binary, not a second competing canonical text format.

Runtime: `SceneWorld` decides desired visibility and residency; the async VFS chunk reader verifies independently addressed payloads. GPU, physics and ECS residency are managed by the respective providers, not by the file serializer.

## Reference parity: remaining work

1. **Full World Partition:** use metadata-only cold start for large scenes, instantiate/destroy all relevant ECS/plugin component state lazily, and preserve persistent/dynamic entities across cell hand-off.
2. **Renderable HLOD:** replace AABB proxy geometry with material-aware mesh simplification, texture aggregation and DDC-backed generated HLOD assets.
3. **GPU sector residency:** track and evict GPU mesh, material and texture resources by referenced sectors, with provider-safe fences and shared-resource reference counts.
4. **Portal visibility:** add portal polygon/frustum clipping and conservative occlusion rather than reachability-only connectivity.
5. **Diagnostics:** expose per-sector CPU/GPU memory, streaming backlog, object/triangle budgets, HLOD transitions, p50/p95/p99 frame-time telemetry.
6. **Editor scene presentation:** retain a cached hierarchy/outliner model with incremental changes, spatial/layer/sector overlays, selection-safe streamed entities and per-cell authoring actions.

Do not claim full parity to the supplied reference until these features are implemented and tested against large real scenes such as KaylaNightCity, including a clean Release build and a measured >=1000-frame camera/vehicle traversal.
