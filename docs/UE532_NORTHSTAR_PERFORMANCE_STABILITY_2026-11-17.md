# NorthStarCpp — UE 5.3.2 architecture and performance audit
Date: 2026-11-17

## Reference and legal boundary

Reference repository: https://github.com/Mccrae123/Unreal-Engine-5.3.2
Reviewed public repository overview and Epic documentation for Render Dependency Graph, Tasks System, and Derived Data Cache. This is an architecture-level review, not a complete line-by-line source audit. The fork's moving release branch is not an immutable proof of the original 5.3.2 tag.

UE Engine Code is governed by Epic EULA even if a mirror is public. No UE code or headers were copied into NorthStarCpp.

Sources:
- https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/tasks-systems-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/using-derived-data-cache-in-unreal-engine
- https://www.unrealengine.com/ue-on-github/

## NorthStar findings and prioritization

P0: RenderGraph and provider synchronization. NorthStar already has `render/shared/render_graph.hpp`, a transient slot planner, D3D12 aliasing, and a compiled graph-plan cache. Extend it with Vulkan parity, per-resource lifetime visibility and GPU validation rather than replacing working code.

P0: Hot-path staging allocations. `Viewport::draw()` previously reconstructed material geometry vectors, picking geometry, v1/v2 batch arrays, and shader material-name copies per frame. This was the first target of the present implementation.

P0: Runtime monoliths. The Player `App` is organized into `.inc` fragments but remains one translation unit with a large shared mutable state. Qt Editor already has separately compiled workspace/Outliner/signals modules. The next meaningful split requires `RuntimeServices`/`AppState`, a state-lifetime contract, and a single owned frame loop.

P1: Task dependencies. Existing Qt V-HACD cooking already uses async execution, progress, cancellation, and `collision_cook_cache`. Extend dependency-based jobs to asset decode, streaming and GPU upload while respecting renderer thread-affinity.

P1: Unified Derived Data Cache. Reuse the existing collision-cook cache concept for NEF8 asset derivatives and shader variants, keyed by source hash, compiler/importer version, options and platform ABI.

P1: Profiling. Compare the same scene with CPU frame p50/p95/p99, GPU frame times, allocations per frame and memory watermark. No performance percentage is claimed without A/B data.

## Implemented: reusable per-Viewport frame scratch

New `engine/northstarcpp/modules/viewport/viewport_frame_scratch.hpp` owns persistent capacity for:
- pick triangles, cube pick entity IDs
- material geometry buckets
- GPU v1 and v2 material batches
- material-name pointers used by shader-reload submission

`Viewport::draw()` now resets active sizes without discarding allocations. A frame epoch in the material-index lookup avoids repetitive hash-node creation for stable material names. Historical lookup growth is bounded with a periodic cache clear; this does not cap visible material count or geometry detail. `reset_project()` is invoked when remounting project assets so previous-project buffers and names are dropped.

The material-reference pointer list refers to stable host-owned names during the synchronous provider draw call, removing per-frame copies of material-name strings. No changes to public engine.render provider ABI and no mesh decimation or proxy substitution.

New `tests/viewport_frame_scratch_smoke.cpp` checks stable material vertex storage, stable hash-node address for recurring materials, zero stale active batches on frame reset, retained picking and GPU batch vector capacities, and full project-remount invalidation. Registered as `northstarcpp_viewport_frame_scratch` in CMake/CTest.

## Verification

QA configuration: `_qa_editor_split` (Release / Ninja / MinGW-w64).

Build succeeded (exit 0) for `northstarcpp_player`, `northstarcpp_editor_qt` and `northstarcpp_viewport_frame_scratch_smoke`.

Last selected regression run: **11/11 PASS** — PIE command-state, Qt Native UI, Qt authoring, Qt GPU surface, runtime frame scheduler, viewport frame scratch, provider inspection (including override/ABI), bundled V8 scripting, Player scene runtime, Player prefab runtime, and Qt Editor launching Player.

Build reproducibility is not fully settled: Ninja emitted `premature end of file; recovering`, and CMake noticed new unrelated .bak assets during build. Other workspace files were modified concurrently; those changes were not intentionally altered by this patch.

Not verified: clean isolated build, full CTest suite, 1000-frame A/B benchmark, D3D12/Vulkan frame-image parity, memory leak soak. Previous Pistol Runtime failure has not been fixed. Production dist was not replaced. No Git commit or push.

## Next technical milestones

1. Use a dedicated build directory without concurrent writers, remove Ninja-state races and establish reproducible baseline.
2. Split `Viewport::draw()` into independent material staging, picking, skeletal submission and provider upload modules; test lifetimes and rendering parity.
3. Move App `.inc` methods to independent units with explicit `AppState` and `RuntimeServices` ownership; preserve a single frame clock and PIE state machine.
4. Measure and compare frame allocations and CPU/GPU p95/p99 in identical high-detail scenes, including material hot reload and project changes.
5. Unify DDC over collision, imports and shaders, then add a dependency-aware cancellable task graph.
