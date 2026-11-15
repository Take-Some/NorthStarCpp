# Native Camera 2.1 — Stabilization and Gameplay Motion
Date: 2026-11-15

## Source analysis

User-supplied reference: `camera(4).zip` (281 code/header files,
approximately 4.4 MB unpacked). The archive contains a proprietary
Rockstar Games camera subsystem and is not a standalone library.
It references RAGE-specific entity, collision, timer, metadata,
pool and camera-director infrastructure and includes Rockstar copyright
notices. **No source code from the archive was copied, built, vendored
or relicensed.** Only general concepts informed a new, original
NorthStarCpp implementation:

- Free camera, first person, third person, vehicle chase and cinematic
  direction as separate behavior families;
- Time-domain damping and frame interpolation;
- Predictive movement framing and obstacle-aware camera placement;
- Explicit cut transitions, viewport-specific controls and camera shake.

## Changes

### Existing CameraDirector (no engine ABI replacement)

Files: `modules/api/include/northstarcpp/camera.hpp` and
`modules/host/camera.cpp`.

- Added configurable, closed-form critically damped positional spring.
  The spring integrates from the authoritative previous position, with
  numerical consistency at 30/60/120 Hz. Default frequency 0 retains
  legacy exponential following.
- Added bounded third-person predictive framing based on target
  velocity. Independent smoothing prevents instantaneous lead snapping.
  Vehicle look-ahead now uses the same filtered state, keeping its
  original speed-responsive FOV, steering bank, reverse view and distance.
- Added asymmetric collision response: pull inward immediately on a
  new obstruction and restore orbit distance smoothly when clearance
  returns. Both real PhysicsScene sphere sweeps and the fallback
  scene collision path use the updated response.
- Added conservative bounding-sphere rejection before fallback
  ray-versus-AABB intersection. Scene V3 invisible and non-collidable
  authoring actors no longer block the fallback camera.
- Teleport and mode changes clear stale spring/collision/lead state.
  Added `CameraDirector::cut()` for instant camera cuts without
  rewinding the active cinematic camera track.
- Retained exact first-person aim orientation and eye position,
  independent of spring smoothing; only the aim FOV interpolates.
  Existing cinematic camera tracks, shake, attachment sockets,
  orbit and zoom remain functional.
- Added finite/range validation of every new camera setting; settings
  remain optional and are read from `gameplay/camera.json` through
  engine.assets VFS.

### New optional tuning keys

```json
{
  "schema": 1,
  "third_person": {
    "lookahead_seconds": 0.12,
    "lookahead_max": 1.5
  },
  "smoothing": {
    "seconds": 0.14,
    "spring_frequency_hz": 2.6,
    "lookahead_seconds": 0.12
  },
  "collision": {
    "radius": 0.22,
    "padding": 0.08,
    "restore_seconds": 0.10
  }
}
```

Existing keys and schema 1 remain valid; fields omitted from other
projects keep their defaults. The sample
`Projects/NorthStarFirstPersonPlatform/assets/gameplay/camera.json`
enables the spring and these settings.

### Scripting

- `Camera.cut()` -> `engine.camera.cut()` V8 host binding;
  resets transient camera state without changing/rewinding tracks.
- `Camera.shake(amplitudeMetres, seconds)` exposes the existing native
  shake call in the class-based script SDK and type declarations.
- Existing `Camera.setMode`, `Camera.setZoom`, `Camera.setAngles`,
  `CameraViewController` remain compatible; project scripts still
  determine the active gameplay camera mode.

## Verification

QA directory: `out/asset-warnings-qa-20261115`.

- `northstarcpp_camera_director`: PASS (existing legacy regression).
- `northstarcpp_camera_motion`: PASS (new regression) includes
  spring response at 30/60/120 FPS, lead smoothing and bounds,
  obstruction entry/recovery, V3 noncollidable/invisible filtering,
  cinematic cuts, FPS aim exactness, optional JSON validation,
  1000-frame continuous motion and fallback collision stress.
- `northstarcpp_shared_camera_script`: PASS.
- `northstarcpp_scene_script_sdk`: PASS (including Camera.cut and shake).
- `northstarcpp_input_typings_sync`: PASS.
- Player `modules/editor/window.cpp` standalone syntax-only C++ check:
  PASS.
- Existing `tests/camera_motion_smoke.cpp` stress: 12,000 far-away
  scene objects, 1,000 frames; observed in one QA run:
  p50=317.4 microseconds, p95=629.3 microseconds,
  p99=2480.9 microseconds. CPU-only fallback scene broadphase;
  NOT a GPU/Vulkan or player frame-time profile. These times
  include loop-level camera updates and machine variability.
- `git diff --check`: exit 0 (unrelated line-ending notices only).

## Remaining

- Real Bullet/Jolt provider camera-physics test, camera placement
  against live streamed sectors and in-Player verification are not
  part of the completed scoped QA run.
- Full performance profiling still requires actual map workloads,
  representative occluder distribution, a profiling timeline and
  frame-time p50/p95/p99 measured in Player.
- No wholesale proprietary camera port; no new physics or rendering
  provider was introduced.
- Production `dist` not replaced; no commit or push.
