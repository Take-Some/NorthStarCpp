# NEF8 World Partition / VFS Integration Verification

Date: 2026-11-14
Workspace: C:\Users\AidenFox\Documents\xray2
Validation build: out/nef8-range-validation-20261114 (Release / Ninja / MinGW)
Production dist: NOT replaced

## Implemented

- Optional engine.assets range ABI 3, maintaining ABI 1 and ABI 2 compatibility.
- Directory VFS native positioned reads, including FAT/DB uncompressed entries.
- AssetStore::read_range() with bounded offsets and a compatibility fallback.
- SceneChunkReader with once-validated NEF8 v2 index, local/VFS input,
  and per-cell CRC32/BLAKE3 validation.
- SceneCellAsyncResidency with bounded in-flight background jobs,
  world-residency reconciliation and stale-completion discard.
- SceneCellDiskResidency now reuses a cached index.
- Runtime Editor/Player SceneWorld update polls asynchronous NEF8 cells.
- Streaming Inspector shows queued/resident decoded block counts.
- Unsaved Editor changes disable stale on-disk index usage until saved.

## Verification

Fresh isolated CMake/Ninja Release build configured successfully.
Five native CTests passed from that independent build:

1. northstarcpp_assets_vfs
2. northstarcpp_scene_async_vfs
3. northstarcpp_scene_chunk
4. northstarcpp_scene_binary
5. northstarcpp_scene_nef8_crosslang

Python tests: 37/37 PASS (game export, project catalog, launcher).
Player and Editor window.cpp compilation units PASS with real Release
CMake/Ninja flags in the fresh validation build.

Tests cover combined/split FAT/DB ranges; an NEF8 v2 cell read from inside
FAT/DB; async Alpha/Beta streaming, unloading, cached index and corruption
isolation. GPU/physics instantiation is not covered by these checks.

## Release and remaining work

- Full Editor/Player link requires roughly 487 further build actions,
  including Qt, Flecs, Bullet, Jolt and render providers. Full clean build,
  installed Player smoke and complete standalone game export are NOT proven.
- Earlier validation folder had multiple concurrent Ninja instances and
  a failed-recompaction file lock. Work continued in the independent folder;
  active parallel builds were not killed or overwritten.
- Initial complete Scene/ECS/physics proxies are still loaded eagerly.
  The NEF8 adapter asynchronously stages component bytes but does not yet
  instantiate/despawn actual ECS, Bullet/Jolt, navigation or render resources
  based on block readiness. This is the next major integration gate.
- Directory VFS implements native ABI 3 ranges; third-party ABI 1 providers
  use buffered fallback. PPMd-compressed FAT/DB entries remain unsupported.
- No production dist or installed DLLs were replaced. Release deployment
  requires plugin/V8 validation, full clean build, package verification,
  installed smoke and rollback-ready atomic replacement.

## Key files

- include/northstarcpp/assets_plugin.h
- include/northstarcpp/assets.hpp
- include/northstarcpp/scene_chunk_index.hpp
- engine/northstarcpp/modules/host/assets.cpp
- engine/northstarcpp/modules/host/scene_chunk_streaming.cpp
- engine/northstarcpp/modules/host/scene_nef8_chunked.cpp
- engine/northstarcpp/modules/assets/directory/directory.cpp
- engine/northstarcpp/modules/assets/directory/fat_archive.cpp
- engine/northstarcpp/modules/editor/window.cpp
- tests/assets_smoke.cpp
- tests/scene_async_vfs_smoke.cpp
- docs/BINARY_SCENES.md


## NEF8 payload-driven runtime lifecycle — implementation and verification

The runtime now separates logical WorldPartition cell requests from verified
NEF8 chunk readiness. SceneWorld::enable_payload_gate() and
set_cell_payload_ready() control static residency. A cell can be logically
loaded but cannot activate static entities until its independently compressed
chunk is present and validated. Persistent world objects, player and dynamic
traffic remain pinned. Near-player static collision uses the existing safety
proximity rule and may be retained while data is pending.

PhysicsScene::sync_scene_residency() now reconciles its internal Flecs ECS
runtime against the gated SceneWorld. Static cell ECS entities are actually
destroyed on eviction and recreated by stable authored scene ID on return,
while source-indexed physics poses remain valid. Sparse ECS position syncing
was added to preserve physics index mapping. Bullet/Jolt static body residency
is controlled through the existing provider extension; when missing, old
physics providers retain their conservative eager collision fallback.

SceneViewport drops CPU OBJ mesh-cache entries that are no longer visible in
the active cell set. The current renderer submits a fresh per-frame geometry
list; this does not mean that arbitrary GPU resource kinds or textures are
evicted across all providers.

SceneNavigationCells is a separate, coarse cell-boundary connectivity graph
that activates/deactivates on payload readiness. It intentionally does not
implement or claim a polygon NavMesh, terrain walkability, path corridor,
dynamic collision avoidance or asynchronous NavMesh tile cooking. There is
currently no engine.navigation/NavMesh runtime provider to integrate.

### Validation

Independent Release/Ninja build:
out/cell-runtime-validation-20261114

All 10 selected native tests passed: scene/runtime Bullet, runtime Jolt,
Bullet physics, Bullet provider selection, Jolt provider selection,
SceneWorld, coarse cell navigation, async VFS, NEF8 binary codec and
Python/C++ cross-language NEF8 interoperability. The runtime lifecycle
tests use a real file saved as NEF8 ListFile, async disk reads, payload
readiness transitions, actual Flecs entity counts, fast travel and
reloading previously unloaded sectors, as well as a synthetic readiness
stress loop. Python project/launcher/export regression: 37/37 pass.

Viewport.cpp, Player window.cpp and Editor window.cpp compile with their
CMake/Ninja Release flags. A missing ListFile dictionary index source in
the Asset Browser build was registered in the runtime CMake target.
Full GUI target linking / installed package smoke is still NOT confirmed.

A separate FPS demo generator --verify reports an unrelated missing/stale
models/skydome.ydd resource. This is not evidence of a lifecycle regression,
but blocks claiming that the FPS demo artifact is fully verified.

### Outstanding release and lifecycle gates

- Full standalone Editor/Player link, dist package, deployment smoke,
  clean-system test, atomic replacement and rollback remain unfinished.
- Initial Scene is still loaded eagerly for editor/physics authoring metadata;
  full disk-to-runtime sparse scene construction without a whole-file DTO
  remains unfinished.
- The script_ecs world, entity script initialization and script resource
  lifetime remain eager; the source-index-preserving PhysicsScene ECS runtime
  is the subset with actual create/destroy residency support.
- Advanced GPU textures/meshes, resource fences, shared residency leases and
  NavMesh tiles need explicit provider-level lifecycle interfaces and tests.
- Streaming cancellation is stale-result discard; running decode jobs cannot
  be forcibly interrupted. Stable IDs are maintained, runtime ECS handles may
  change after recreation.
- The production dist/DLLs have not been replaced.
