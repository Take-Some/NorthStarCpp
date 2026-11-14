# NEF8 Scene ListFile v2 — implementation and QA

Date: 2026-11-14
Project: NorthStarCpp / xray2
Status: QA candidate verified; public release not approved

## Implemented

- Default NSCN writer now emits NEF8 content kind 6, schema 2, compression mode 2.
- NSCK index with independently compressed raw-DEFLATE blocks for persistent
  scene data and each populated streaming cell.
- NSLR per-block record table includes kind, version, stable Entity ID,
  authored ordinal, byte range and per-record CRC32.
- BLAKE3 across the whole expanded payload, per-cell BLAKE3 and compressed
  block CRC32; strict bounds and directory validation.
- Scene::components retains custom typed ECS payloads; Scene::opaque_records
  keeps unknown kinds and unknown future record versions unchanged.
- C++ component validator registry and optional stable C ABI callback table.
- index_scene_chunks and load_scene_chunk read only the needed disk ranges.
- SceneCellDiskResidency follows SceneWorld load/unload transitions using
  synchronous selective reads and staged updates.
- Python v2 writer, v1/v2 reader, binary exporter dependency extraction,
  native/Python cross-language byte-identity migration tests.
- Legacy NSCN v1 and NEF8 Scene schema 1 remain readable.

## Verified

- Full CMake Release all-target build succeeded on Windows/MinGW-w64.
- Complete CTest run: 81 of 81 passed.
- Python NEF8/project/export tests: 43 of 43 passed.
- Stress test: 768 cells; 24 sampled; expanded 11,333 of 412,148
  bytes (~2.75% of expanded data) through the selective disk API.
- C++ smoke: opaque plugin data, future record versions, C ABI provider
  validation, SceneWorld disk-residency transitions and corruption isolation.
- Python-produced NEF8 scene and native C++ reserialization byte-identical.
- QA package manifest: 200 files, 9 plugin providers, verification successful.
- Installed QA scene converter and Player accepted a NEF8 v2 scene.

## QA package (not shipping)

out/qa-nef8-listfile-v2-20261114/
  NorthStarCpp-0.4.0-windows-x64-Release-QA

The production dist was not replaced. The QA package is not release-approved.
Release promotion requires independent signed clean-Windows acceptance and the
required security-approved V8 SDK/provider attestation under build_dist.py.

## Remaining architecture

The built-in SceneObject v1 base payload remains grouped in one Entity record.
It has not yet been normalized into independently versioned Transform, Render,
Physics and Script components. Automatic discovery of component-schema
registrations from every loaded external plugin also remains incomplete.

The SceneWorld transition disk adapter is synchronous, and ECS/entity creation,
physics collision residency, GPU resources and metadata-only cold start are
not yet driven by streamed chunks. Before production use, add asynchronous
I/O, cancellation/priority, resident-resource lifecycle, and 1,000+ frame
CPU/GPU/streaming profiling in real game scenes.
