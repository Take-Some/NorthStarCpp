# NorthStarCpp — UE 5.3.2 architecture reference & performance audit
Date: 2026-11-16
Scope: independent NorthStarCpp implementation; NO Unreal Engine source code copied.

## Reference and legal boundary

- Public mirror: https://github.com/Mccrae123/Unreal-Engine-5.3.2
- UE engine EULA: https://www.unrealengine.com/eula/unreal
- Architectural reference: https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine
- Source redistribution of UE Engine Code is restricted even when a mirror is public. Reuse general algorithms and high-level system design, not source files, implementation fragments, embedded assets or Epic headers. Do not add UE source as a vendor dependency to NorthStarCpp.

## Findings from the local NorthStarCpp workspace

The engine already contains a backend-neutral RenderGraph, transient allocation planner, per-frame CPU/memory profiling, a D3D12 placed-resource aliasing path, a Vulkan Sync2 executor contract, and a modular engine.* provider architecture. Replace duplicated behavior with these existing contracts rather than introducing a parallel engine.

Scoped module-tree inspection (file sizes are approximate, not lines of code):
- modules/editor/window.cpp: 302 KiB — major Win32 runtime/editor monolith.
- modules/editor/qt_editor_window_setup.cpp: 112 KiB — Qt editor assembly and stateful wiring.
- modules/editor/qt_editor_window.cpp: 68 KiB — Qt orchestration.
- modules/editor/qt_content_browser.cpp: 65 KiB — asset-browser responsibilities.
- modules/render/d3d12/renderer_device.cpp: 43 KiB — GPU device-related responsibilities.
- modules/host/physics.cpp: 42 KiB — physics host coordination.
- modules/host/camera.cpp: 37 KiB — camera-related host functionality.

These are candidates for decomposition, not proof that their individual methods allocate excessively. Profile before changing performance-critical code.

## Changes implemented in this phase

1. `engine/northstarcpp/modules/render/shared/transient_resource_planner.hpp`
   - Replaced temporary unordered_set duplicate detection with indexed occupancy bits, avoiding per-key hash allocation.
   - Replaced O(passes * slots * requests) peak-live-memory scanning with one event array and an O(passes + requests) sweep after allocation slots are assigned.
   - Preserved inclusive lifetime semantics, strict reuse policy, committed-byte accounting, and existing public structures.
   - Added lifetime range validation and overflow checks.

2. `engine/northstarcpp/modules/render/d3d12/renderer_transient_alias.cpp`
   - Reserved known capacities for allocation and member arrays before request iteration, reducing growth reallocations during configuration.

3. `tests/render_transient_resource_planner_smoke.cpp`
   - Asserted exact 16 KiB physical peak in the baseline case.
   - Added an alias handoff test (8 KiB shared physical slot + 2 KiB concurrent slot).
   - Added rejection of invalid externally supplied lifetime bounds.

4. `tests/render_d3d12_transient_alias_smoke.cpp`
   - Enabled Bloom in the frame-graph fixture. Default Bloom was disabled in the current graph, making the prior alias-lifetime assertion invalid.

## Verification

- CMake target `northstarcpp_render_transient_resource_planner_smoke`: built successfully.
- Test executable for CPU transient planner: exit code 0 (including new exact-peak, handoff and bounds tests).
- CMake target `northstarcpp_d3d12_transient_alias_smoke`: built successfully.
- D3D12 WARP runtime smoke: exit code 0. Output: '1 heap / 2 slots, 5 logical resources, 5 alias barriers'.
- Git diff --check on planner + CPU test: exit code 0; test file emitted only line-ending normalization warning.
- No production dist replacement, Git commit, or push.
- Full engine build, editor visual QA, and matched before/after frame-time profiling NOT performed.

## Proposed follow-up refactor sequence

### P0: Strictly preserve existing working architecture
- Preserve plugin provider ABI, runtime/Editor separation, existing RenderGraph semantics and project asset paths.
- Keep `engine.*` APIs replaceable by built-in and external providers.
- Do not remove detail/LOD support to improve frame rate; optimize scheduling, visibility, streaming and GPU submission instead.

### P0: Split window.cpp (302 KiB), with behavioral equivalence tests
- Extract startup/options parsing into `runtime_bootstrap`.
- Extract Win32 input, window and message dispatch into `win32_platform_host`.
- Extract scene/prefab/vehicle setup into `runtime_world_bootstrap`.
- Extract Play/Stop/PIE lifecycle into `runtime_play_session`.
- Keep one minimal composition root and avoid duplicate frame loops.
- After each extraction: compile player, run command-line smoke, runtime startup and input acceptance.

### P0: Separate Qt editor setup (112 KiB)
- Independent wiring for dock layout, menus/actions, asset browser, inspector/property widgets, PIE surface and render viewport.
- Move asset-browser shared data loading to one registry with explicit ownership.
- Prevent duplicate widget/signal connections and duplicate asset preview loads.

### P0: Frame-local allocation reduction
- Instrument malloc/realloc and vector growth by stage: scripting, animation, physics, scene streaming, rendering, asset streaming.
- Use frame arenas and per-thread reusable scratch only for data whose lifetime ends within the frame; never store pointers to scratch across frames/queues.
- Reserve high-water capacity for command/instance/palette buffers, retain bounded memory limits, and measure peak resident cost.
- Avoid per-frame UTF-8/UTF-16 conversions, repeated path normalization, repeated mesh decode and synchronous disk reads.
- Never share a mutable arena across threads without explicit synchronization.

### P1: RenderGraph and GPU
- Generalize placed-resource aliasing to N compatible targets; validate heap alignment, formats, usage and per-subresource states at the backend.
- Avoid rebuilding stable graphs every frame; invalidate on material/pass configuration changes.
- Use cached descriptor sets/heaps, upload rings fenced per frame, and stable PSO cache keys.
- Add Vulkan parity via the existing backend-neutral RenderGraph; do not fork planning logic.
- Move independent passes onto parallel recording only after correct resource-state ownership and fence validation.

### P1: Asset/DDC and scheduling
- Coalesce identical concurrent asset requests into one in-flight job.
- Hash source + importer version + platform settings into derived-data cache keys.
- Build asynchronous import/cook pipelines with cancellation and bounded queues.
- Maintain a single dependency/registry model for NEF8 assets, materials and runtime.

### P1: ECS/Physics/Animation
- Avoid repeated FLECS world scans per system; cache queries/archetype filters where safe.
- Batch transform and animation palette updates; use dirty-versioning, avoid re-uploading unchanged buffers.
- Profile Jolt temporary allocator lifetimes and cap frame scratch sizes.
- Avoid duplicate Pawn/prefab instantiation and duplicated skeletal skinning work.

## Acceptance criteria for broader optimization

Use identical project, scene, camera, GPU, graphics settings, driver and warm-up. Capture at least 1000 frames before and after. Report CPU frame p50/p95/p99, GPU timestamp p50/p95/p99, draw submissions, shader/PSO cache misses, heap allocations/frame, transient peak, GPU residency, asset load p95 and crash/validation warnings. Use functionality and visual parity as hard release gates; never infer speedup from fewer allocations alone.

Version and release status: no engine version bump or production replacement was attempted during this audit.
