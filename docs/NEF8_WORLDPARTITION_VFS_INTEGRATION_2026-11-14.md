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
