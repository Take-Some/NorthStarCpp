# NorthStarCpp — CPU Frame Timeline, V8 Event Bridge, and Runtime Integration

Date: 2026-11-14
Status: targeted source integration and QA; production distribution unchanged.

## Scope

This pass instruments the actual Windows Player frame path across scripting, animation, physics, scene/world streaming, rendering and asset-zone streaming, and reduces avoidable per-frame JavaScript and skeletal-animation overhead. Current engine.* provider ABI contracts remain unchanged.

## Opt-in frame profiling

Implementation: engine/northstarcpp/modules/shared/frame_profiler.hpp and engine/northstarcpp/modules/editor/window.cpp.

Set NORTHSTARCPP_PROFILE_FRAMES to an integer between 1000 and 1000000. When absent, the profiler stays disabled, and no sample buffer is allocated. Once the requested number of rendered frame callbacks is observed, the application writes one JSON line to stderr with event northstarcpp.frame_profile and per-metric p50/p95/p99/max in milliseconds.

PowerShell example (after building the current Player):

    $env:NORTHSTARCPP_PROFILE_FRAMES = '1000'
    .\northstarcpp.cmd run --target player 2> out\frame-profile-stderr.txt
    Remove-Item Env:NORTHSTARCPP_PROFILE_FRAMES

Extract the JSON line with event northstarcpp.frame_profile from stderr; other diagnostics may share the stream.

Captured metrics:

| Metric | What is timed |
| --- | --- |
| raw_frame_interval | The raw elapsed interval returned by engine.time/FrameClock, before the 50 ms FPS simulation-delta clamp. Includes waiting and previous Present time. |
| frame_cpu_wall | Wall time spent in App::frame, including renderer submission and possible Present/vsync waits. |
| scripting | Script input copying, source hot-reload check, global update dispatch, and entity event batches; includes any synchronous engine.ecs callbacks. |
| animation | Script animation advancement, physics transform writes and ECS position updates triggered by animation. |
| physics | Calls to PhysicsScene::step in active game paths. |
| scene_streaming | Calls to update_scene_world. |
| rendering | Calls to Viewport::draw, including CPU draw preparation, renderer submission and Present. |
| asset_streaming | Asset zone update/scheduling on the App frame thread, not background worker time. |

These are CPU-side or callback interval wall-clock measurements, NOT GPU timestamps, GPU occupancy or total asset decode time. Each metric is independently percentiled and percentiles must not be added together. Rendering, scripting and animation may perform synchronous work owned by other subsystems; these metrics are high-level boundaries, not a complete causal trace. A separate explicit ECS timing stage or GPU timestamp query would require further instrumentation.

The new test tests/frame_profiler_smoke.cpp verifies percentile interpolation, the difference between interval and CPU-wall metrics, stage accounting, exception behavior, and the 1000-frame output contract. Synthetic timing samples in the test are not real Player frame measurements.

## V8 / C++ entity events

Previously, the Player batched entity events but submitted a dynamically constructed JavaScript for-loop for each batch. This required repeated Realm.eval compilation in the developer V8 sidecar.

Now the Player registers one JavaScript listener for northstar._entityBatch in each fresh script context (including hot reload) and sends event data as bounded JSON through ScriptRuntime::emit_event. The existing V8 sidecar recognizes the exact emit_event source form and dispatches the event without reparsing the dynamically generated for-loop. The listener re-emits individual entity.* callbacks in original order; no public JavaScript API or plugin ABI change is required.

Registration deliberately ends with the undefined completion value: the scripting ABI JSON serializer cannot return a JavaScript unsubscribe function. This issue was caught by the real V8 regression and fixed.

New tests:

- tests/entity_event_batch_smoke.cjs: checks production C++ listener source and ordered delivery for 513 entities plus independent fresh context via Node VM.
- tests/v8_entity_event_abi_smoke.cpp: actual native C++ ScriptRuntime -> scripting_v8_sidecar.dll -> V8 event path, 513 entity updates, order preservation, context reset and event delivery after re-registration.

The sidecar is developer QA-only. A quantitative end-to-end V8 event-speed improvement was not measured in this pass.

## Skeletal animation and streaming

- Character pose no longer computes the same inverse skeleton-to-model matrix every frame; it is prepared when the character loads.
- The temporary bone palette matrix vector is retained and reused instead of repeatedly allocating/reserving each frame.
- AssetStreaming LRU trim now uses an oldest-first heap of unpinned resident candidates, giving approximately O(R + E log R) for R cached records and E evictions rather than repeatedly scanning the map.
- Original Git HEAD vs updated C++ -O2 local benchmark, 1024 32-byte records, five runs each: original median 4856 us; revised median 1358 us; ratio 3.58x on bulk cache eviction only, not an FPS result.
- Within a single AssetStore, duplicate streaming requests already share records and drawable resource loaders cache dictionaries for materials/textures within one load. Cross-store/global caching was not added because mount-generation boundaries and eviction ownership need a coherent contract first.

## Build and QA evidence

Isolated CMake/Ninja GCC 15.2 Release tree: out/asset-streaming-qa-20261114.

- Standalone frame-profiler target, asset-streaming target and Node event bridge: built and passed CTest.
- Native C++ scripting provider with real developer V8 sidecar and entity ABI event delivery: built; passed after correcting the listener return value.
- Player window.cpp and Viewport viewport.cpp objects: compiled with the new profiling, scripting and animation changes.
- Existing skeletal animation, character facing, skeletal-asset interoperability, scripting ABI, script-service override, and archive/VFS smoke tests: passed focused CTests.
- Repeated final aggregate CTest: 11/11 PASS (skeletal animation, character facing, skeletal asset interop, Node event batch, native V8 event batch, scripting service override, scripting ABI, scripting ABI rejection, frame profiler, asset streaming cache and VFS/assets).
- Final Player window.cpp object compiled with the event registration serialization fix; Viewport viewport.cpp object also compiled successfully.
- Git diff --check on the touched production paths: PASS.
- Full Player link, actual sustained 1000-frame gameplay capture, hardware GPU timestamps, and clean-machine Release acceptance: NOT verified in this pass.

## Existing asset blocker

The broader northstarcpp_shared_assets integration CTest currently fails because models/weapon/pistol/pistol.ydd references a missing same-stem skeletal rig at models/weapon/pistol/pistol.ymt. The source tree has pistol.skeleton.ymt and common-pistol.ymt, but no pistol.ymt. This is a skeleton-reference naming problem, not a missing material. Do not fabricate or silently remap authoritative asset content; resolve the naming contract and rerun the full test.

## Remaining work

1. Complete an isolated full Player/Qt Editor Release build and run representative gameplay scenes with NORTHSTARCPP_PROFILE_FRAMES=1000 or more. Collect raw frame interval and CPU-stage p50/p95/p99, not clamped dt.
2. Add GPU timestamp-query profiling per renderer backend and distinguish CPU/GPU Present and queue wait time.
3. Add explicit ECS span or event counters for C++ scene/physics sync, including provider call counts and component write rates.
4. Compare before/after V8 event latency and throughput under identical gameplay loads on actual provider configurations.
5. Implement asset dictionary single-flight/shared cache only after versioned VFS mount-generation and cross-store ownership contracts are specified and tested.
6. Fix pistol skeletal reference and rerun complete shared-assets and full CTest matrix.

All changes remain in the working source tree; no Git commit/push or production DLL/dist replacement was performed.
