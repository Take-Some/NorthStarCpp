# Camera Director 2.2 — Physics, World Partition and Qt Camera Studio
Date: 2026-11-15

## Scope and constraints

This task extends the current NorthStarCpp native camera, renderer-neutral
physics interface and existing Qt Editor. It does not vendor or copy camera
code from the proprietary `camera(4).zip` reference and does not replace a
runtime engine ABI or any production installation.

## Physics provider integration

The original `tests/camera_physics_smoke.cpp` runs against both providers
through `PhysicsScene::sweep_sphere`, including triangle-mesh collision
unrepresented by a SceneObject. Verified provider outputs:

- Bullet 3.25: triangle sweep fraction 0.458333
- Jolt 5.6.0: triangle sweep fraction 0.458333

Both pass the sphere-sweep collision test and invalid query rejection.

`tests/camera_streaming_physics_smoke.cpp` provides a synthetic
two-sector high-speed highway scenario. A vehicle viewpoint moves
at 95 m/s with camera updates at 120 Hz, the World Partition streaming
lookahead prefetches the approaching sector, and a fast travel to the
800 m sector asserts immediate camera cut. Dynamic traffic keeps its
Scene V3 stable ID while its effective cell is rehomed; the player
remains resident according to the current player-pinning contract.
600 additional camera updates check numeric stability and wall avoidance.
Both provider-specific CTests pass:

- northstarcpp_camera_streaming_physics_bullet
- northstarcpp_camera_streaming_physics_jolt

These tests are synthetic. Actual KaylaNightCity graphics and streaming
payload latency have not yet been measured in Player.

## Camera Director arbitration

`CameraShot` and `CameraDirector::push_shot/clear_shot/clear_shots`
provide a bounded set (16 concurrent requests) with deterministic
highest-priority selection. On equal priority the most recently
updated source wins; the same source updates in place. When one request
ends, control passes to the next source or the gameplay camera.
Pose transitions use the selected shot's configured blend.

Direct FPS aim returns immediately, and Free and Cinematic poses
do not inherit third-person follow inertia. The existing cinematic
track retains its timeline position while temporarily overridden.
Reset releases all active shots, preventing source leaks across games.
Shot IDs use validated ASCII characters and length, priority is
limited to [-100,100], and blending to [0,10] seconds.

`engine/northstarcpp/modules/host/camera_presets.cpp` implements
the engine-owned JSON preset codec (schema 1, 32 entries, 64 KiB).
The project `Projects/NorthStarFirstPersonPlatform/assets/gameplay/
camera_presets.json` contains two illustrative native presets.
The same codec is used from VFS by Player and Qt Editor.

JavaScript SDK and V8 host now support:

```javascript
import { Camera } from "@northstar/engine";
Camera.applyPreset("cinematic.intro_wide");
Camera.pushShot("mission.cutscene", {
    position: {x: 8, y: 5, z: -12},
    yaw: -0.3, pitch: -0.15, fovY: 65
}, 80, 0.5);
Camera.activeShot();
Camera.clearShot("mission.cutscene");
Camera.clearShots();
```

Runtime JavaScript angles are in radians; serialized JSON preset
rotations use degrees. Invalid source IDs, nonfinite coordinates
and out-of-range priorities are rejected before native host calls.

## Qt Editor integration

In **View > Camera Studio...**, the modeless dialog exposes project
presets and authoring with current Viewport camera capture, preview,
xyz, yaw/pitch/roll, FOV/near/far, priorities, blend duration, add,
update/delete and reload. Project file saves use `QSaveFile` and
file-change checks to avoid silently overwriting external edits.

**Gameplay Camera Tuning...** opens a dedicated form for 16 camera
settings: third-person distance/height/shoulder/lead limits, vehicle
distance/height/look-ahead, follow smoothing and spring frequency,
lead damping, teleport threshold, collision radius/padding/recovery,
and default vertical FOV. Original unknown JSON keys are preserved,
the complete document is validated with the native
`parse_camera_settings()`, and changes are atomically saved to
`assets/gameplay/camera.json`.

A real Qt Editor test `--camera-studio-smoke` exercises the actual
viewport, modeless Studio, project preset save/reload and gameplay
tuning field edits and native JSON round-trip.

## Critical stabilization fix

The original `northstarcpp_camera_director` regression initially
failed intermittently with NaN camera height and erroneous follow
distance during first-person to third-person mode switches.
`math::float3` does not guarantee zero-valued components when
constructed/reset with an empty brace assignment. Motion-state
resets (`follow_velocity_`, `lookahead_offset_`) and predicted
lead are now explicitly initialized to `{0.f,0.f,0.f}`.

After this fix, the legacy smoke ran **120/120 consecutive times**
without any nondeterministic failure. The camera smoke also has
more precise stage/finiteness failure diagnostics.

## Acceptance and build evidence

QA directory: `out/material-functions-qa-20261115`.

- Qt Editor and Player were built successfully from source in QA,
  without replacing production files.
- Actual Qt Studio and Gameplay Tuning UI smoke: PASS.
- Cinematic Director priority, ties, release and VFS preset
  parsing/roundtrip: PASS.
- Bullet + Jolt original camera physics: PASS.
- Bullet + Jolt streaming camera + high-speed fast travel: PASS.
- World Partition Scene V3 regression: PASS.
- Native camera motion and original camera director: PASS.
- JavaScript camera and SDK regressions: PASS.
- Total last targeted CTest group: 12/12 passed.

## Pending acceptance

- Live scene rendering in the full Night City map, source asset
  streaming latency under fast driving and real vehicle steering.
- GPU profiling across rendering providers (Vulkan/D3D12) for
  integrated camera+physics frame-time p50/p95/p99.
- Multi-camera timeline visualization and cinematic easing curves
  (current priority source blending is bounded and deterministic but
  does not replace the complete visual camera timeline editor).
- No production `dist` deployment, commit, or push.
