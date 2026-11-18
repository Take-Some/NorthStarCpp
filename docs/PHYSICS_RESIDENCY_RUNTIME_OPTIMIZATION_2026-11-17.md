# NorthStarCpp 0.6.2 — PhysicsScene streaming / allocation optimization

Date: 2026-11-17. Scope: local source changes only; no production replacement, commit, or push.

## Baseline hot path

In `engine/northstarcpp/modules/host/physics.cpp`, `PhysicsScene::sync_scene_residency()` previously:
- Constructed an unreserved temporary `std::vector<NorthStarPhysicsVehiclePose>` on each call.
- For **each authored object and each live vehicle**, recomputed two-second vehicle position and `std::hypot(vx, vz)` to derive a fixed speed-dependent radius.
- Resolved static collision residency using the computed per-vehicle radius and authored bounds.

`PhysicsScene::poll_collision_events()` also allocated a new `std::vector<NorthStarPhysicsContactEvent>` on each call (up to 4,096 contacts).

## Implemented

- Added `modules/host/physics_residency_guards.hpp` with stateless `VehicleResidencyGuard::from_pose()` and `contains()` using the *original* lookahead and radius/clamp formula.
- `PhysicsScene::State` retains `vehicle_residency_guards` scratch. `sync_scene_residency()` clears/reserves it, filling once per live vehicle; each scene object now uses precomputed guard values. A normal stable-vehicle-count pass has no guard-buffer heap allocation; `hypot` calls scale with vehicle count, not objects × vehicles.
- `PhysicsScene::State` retains `contact_event_scratch`, expanding only for larger calls. `poll_collision_events()` passes the caller-requested capacity to the provider, validates the returned count against the requested limit, and returns independent public event records without persistent user-visible aliases.
- Decomposed the original monolithic `physics.cpp` from **836 to 601 lines** using separate implementation sections `physics_scene_residency.inc`, `physics_runtime_entities.inc`, and `physics_vehicle_bindings.inc`. These are still *one translation unit*: a targeted source-organization step, not an ABI or independent DLL split.
- Updated `tests/physics_residency_smoke.cpp` to compare the new vehicle-guard helper against the original formula over 87,808 combinations of speeds, locations, and object radii, before running the provider ABI smoke.
- Added `tools/dev/benchmark_physics_streaming.cpp`, a standalone synthetic CPU benchmark that compares equivalent original and optimized residency computations, checks identical decisions, and reports measured times.

## Validation performed

- CMake Release configured and `northstarcpp_runtime`, `northstarcpp_physics_jolt`, `northstarcpp_physics_residency_smoke`, and `northstarcpp_player` built successfully in `out/qa/_optimization_physics_release_build`.
- `northstarcpp_jolt_residency` CTest: **PASS**.
- Recompiled and directly ran updated physics residency smoke with Jolt DLL: **PASS**.
- Built and launched the newly compiled standalone Player:
  - `Projects/AbbyPistolDemo/main.nscn --check-scene`: **READY**, 0 warnings, 0 failures.
  - `Projects/KaylaNightCity/main.nscn --check-scene`: **READY**, 0 warnings, 0 failures.
  - Actual KaylaNightCity runtime (Jolt + D3D12, default script timeout): **still running after 14 seconds**, then explicitly stopped by the test harness. This does not establish long-term stability.
- `git diff --check` on the modified host physics and physics residency test files: PASS.

## Synthetic CPU microbenchmark

Hardware/toolchain: local Windows build, MinGW-w64 GCC 16.2, C++17 `-O2`.
Workload: 4,000 objects × 32 vehicles × 55 passes.
Measurements:
- Run 1: original **32.655 ms**; optimized **4.718 ms**; speedup **6.92×**.
- Run 2: original **32.681 ms**; optimized **4.718 ms**; speedup **6.93×**.
- Run 3: original **33.058 ms**; optimized **4.715 ms**; speedup **7.01×**.

Both implementations produced 15,510 identical positive residency decisions across the synthetic pass sequence. Note that this test is **not frame time/FPS** and does not establish a 7× overall game speedup. Workloads with few vehicles or fewer authored objects will benefit less.

## Risks and follow-up

- Scratch vectors retain their high-water capacity until `PhysicsScene::State` destruction, trading minimal retained memory for fewer allocations.
- PhysicsScene operations remain single-threaded/non-reentrant as before; scratch buffers are not independently thread-safe.
- Full 286-test CTest, long-duration gameplay/traffic collision tests, frame time p95/p99 and heap-allocation profiler comparisons are **not** complete.
- Concurrent edits in `editor/window.cpp`, `viewport.cpp`, Qt Editor and rendering subsystems were **not** reverted or incorporated into this specific refactoring.
- Remaining monoliths including `editor/window.cpp`, `viewport.cpp`, and Qt Project Browser should be decomposed only with corresponding interface/ABI validation and performance baselines.

No production `dist` was replaced. No Git commit/push was performed.
