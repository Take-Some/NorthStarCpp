# NorthStarCpp — Asset Streaming performance and NEF8 decoder stabilization

Date: 2026-11-14
Status: source changes and focused tests completed; production distribution unchanged.

## Motivation and modifications

Two changes address actual performance and stability debt without modifying public engine.assets ABI:

1. Cache eviction previously scanned the complete record map once per evicted entry (approximately O(R * E) for R records and E evictions). The revised asset_streaming.cpp collects resident/unpinned candidates once, uses an oldest-first heap, and evicts oldest-first while respecting active pins and both residency budgets. Approximate cost: O(R + E log R).
2. The NEF8 v1 and v2 decoders duplicated raw DEFLATE code. asset_list_file.cpp now uses a single inflate_body helper with RAII-controlled inflateEnd cleanup, preserving existing length validation and version-specific errors.

## Performance measurement

Local Windows x64, GCC 15.2, C++17 -O2. Load 1024 logical 32-byte resources serially, release handles, then set streaming budget to 0; measure cache trim only. Original source from Git HEAD and updated source were compiled identically; five separate runs each.

| Metric | Original | Updated |
| --- | ---: | ---: |
| Median bulk eviction | 4856 microseconds | 1358 microseconds |
| Relative result | 1.0x | 3.58x faster |

This is a localized microbenchmark, not an FPS, renderer or end-to-end asset streaming benchmark.

## Test coverage

New CMake/CTest target: northstarcpp_asset_streaming_internal.
File: tests/asset_streaming_internal_smoke.cpp.

- Cache reuse and LRU order.
- Re-request after eviction and generation invalidation.
- Priority promotion and queued-request deduplication.
- Pinning while the cache has a zero-byte budget.
- Error propagation and successful retry of failed loads.
- Clean shutdown, cancelling pending work while allowing the in-flight request to finish.
- Bulk cache eviction with consistent residency counters.

## Verification

Isolated build: out/asset-streaming-qa-20261114.

- CMake configure, GCC 15.2, Ninja, Release: PASS.
- Standalone cache regression build and test: PASS.
- Main runtime, assets_directory provider and shared-assets test executable build: PASS (88 steps).
- CTest northstarcpp_asset_streaming_internal: PASS.
- CTest northstarcpp_assets_vfs: PASS (including existing NEF8 scene/archive checks).
- GCC syntax check of shared NEF8 decoder: PASS.
- Git diff --check on touched production sources and CMake: PASS.

## Separate integration blocker

The broader northstarcpp_shared_assets CTest fails on a missing dependency of the newly added pistol model:

    models/weapon/pistol/pistol.ydd
    -> models/weapon/pistol/pistol.ymt
    -> Cannot locate asset

The missing .ymt is a skeletal rig dependency, not a .ymat material. The authoring tree contains pistol.skeleton.ymt and common-pistol.ymt, but not pistol.ymt; the inspector expects a same-stem skeleton.

This is a current source-asset reference issue, not a NEF8 decoder failure. Do not silently fabricate the missing asset or modify a binary source model. Resolve its actual skeleton association, then rerun full shared-assets regression tests.

## Remaining acceptance

Run full clean Release and complete CTest after concurrent model/scene development stabilizes; verify real gameplay cold/warm streams, scene transitions, active physics/ECS, frame-time and memory p50/p95/p99, and production installation. No production dist or installed DLL was replaced. No Git commit/push was made by this task.
