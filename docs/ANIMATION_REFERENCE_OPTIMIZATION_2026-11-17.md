# NorthStarCpp Animation — reference review and runtime optimization

Date: 2026-11-17
Reference: user-provided `animation(1).zip` (102 entries, approximately 2.1 MiB uncompressed).

## Reference architecture reviewed

The archive contains a game-specific RAGE animation system. Relevant source components:

- `animation/AnimManager.cpp`: service initialization and staged lifecycle
- `animation/ClipDictionaryStoreInterface.cpp`: clip-dictionary residency notifications and reference ownership
- `animation/AnimScene/AnimScene.cpp` and `AnimSceneEvent.cpp`: animation scene sections, time-range events, preload/enter/update/exit phases
- `animation/AnimScene/events/AnimScenePlayAnimEvent.cpp`: clip availability checks before event playback
- `animation/MovePed.cpp`: separate animation channels and motion/secondary/additive networks

These sources depend on unavailable RAGE runtime components. We reused the design principles, **not the proprietary implementation**. They are not a drop-in replacement for NorthStarCpp YMT/YCD and GPU skinning.

## Changes delivered

### 1. Stable, fast joint resolution

- `Skeleton::joint_name_order` is built **once** by the YMT importer from the validated joint names.
- `Skeleton::joint_index(std::string_view)` uses allocation-free binary search over the index.
- Hand IK, wrist orientation, procedural hand placement, and viewport bone socket resolution now share the lookup.
- Manually assembled rigs without an index retain the linear lookup fallback.
- The imported Abby rig tests every name against its original bone index and checks missing-name behavior.

The typical imported-rig lookup changes from O(number of joints) to O(log number of joints). This targets the previously repeated 1000+ bone name scans during IK/weapon-grip evaluation.

### 2. No keyed-mask allocation per pose sample

- `SkeletonPose` owns a persistent `keyed_` scratch array sized at construction.
- Every `sample()` reuses that memory, keeping mirrored helper evaluation and secondary palette constraints intact.
- Untrusted/manual clip buffers receive additional bounds/finite-time checks before indexed sampling.
- Sample timestamps are promoted to double before multiplication and modulo, avoiding overflow for large finite timestamps.

The expensive FK evaluation still evaluates the complete hierarchy; this change removes an avoidable temporary allocation rather than claiming to eliminate per-bone math.

### 3. Clip dictionary filtering

- `load_skeletal_clips` constructs a requested-name hash set once, instead of searching the entire requested-name list for every dictionary entry.
- Duplicate requested clip names are rejected up front and result storage is reserved.
- YCD binary parsing, clip layout, coordinate conventions, and emitted animation data are unchanged.

### 4. Scene animation scheduling and frame storage

- `SceneAnimationPlayer` keeps `scene_order_`, sorted when animations are started/replaced rather than on each frame.
- `advance_into(delta, output)` writes samples into reusable caller-owned storage.
- The existing `advance(delta)` API remains available and preserves returned ordering.
- Player's `advance_scene_animations()` now uses its existing persistent `script_animation_samples` vector directly.
- Stop and clear update the order cache.
- Added regression tests for out-of-order activation, hot replacement, stop, clear, stable buffer allocation, and output order.

This removes one per-tick sort and the previous new result vector allocation from the active Player path.

## Verification

Built using the required single output root `out/build`:

- `northstarcpp_skeletal_asset_interop_smoke`: **PASS**, including actual Abby YMT/YCD geometry/palette and joint-index regressions
- `northstarcpp_scene_animation_smoke`: **PASS**, including reuse and deterministic-order checks
- `northstarcpp_viewport_skeletal_pipeline_smoke`: **PASS**, including IK, attachments and the skin cache
- Player source compilation `window.cpp.obj`: **PASS**

Adjusted CTest fixture paths after the repository build/layout migration:
- viewport skeletal smoke uses `engine/northstarcpp/resources/sharedAssets`
- scene animation smoke runs without optional `Projects/NorthStarAnimationLab` input when that project is not installed

**Not yet verified:** complete Player link, final Vulkan/D3D12 visual parity, frame-time before/after benchmarks. A full Player target build failed in `renderer_deferred.cpp` due to missing Vulkan shader symbol `shaders::deferred_velocity_fragment`; this is outside the animation changes and was not modified.

## Next performance measurements

Profile 100+ Abby rigs and active animated attachments with the same camera and rig/clip set before reporting performance benefits. Record:
- CPU time for YCD decode and `SkeletonPose::sample`, FK, IK and socket publishing separately
- allocator count/bytes by frame, especially sample buffers
- animation update p50/p95/p99, GPU bone palette uploads, and skeletal draw time
- pose correctness, seam stretching, animation loop continuity and detached attachments

Further larger architecture changes are a central clip residency/prefetch service and a deterministic scene-event scheduler with explicit preload, start, update and finish lifecycle. They should be implemented independently using NorthStarCpp providers; they are not included in this bounded optimization pass.
