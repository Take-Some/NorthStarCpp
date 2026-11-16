# Native Camera & Cutscene System — implementation status

Date: 2026-11-16
Reference: user-supplied `camera(6).zip`.
Only general design relationships were used as a guide. The source code of the
third-party reference has not been imported or linked into NorthStarCpp.

## Architecture

Existing NorthStar:
- `CameraDirector`: engine-owned modes (FPS/TPS, orbit, free, vehicle chase,
  cinematic), priority `CameraShot` requests, spring/damped follow,
  obstruction collision, FOV, socket attachment, camera shake.
- `CameraTrack`: versioned keyframes, Hermite position interpolation,
  shortest-arc yaw and roll, deterministic sampling.
- `CameraReplayRecorder`, Camera Timeline Editor, Camera Studio.

Added:
- `CameraCutscene` (`camera_cutscene.hpp`, `camera_cutscene.cpp`):
  sequence of 1..256 named shots, each with a bounded local `CameraTrack`,
  shot length, independent transition length and timeline position.
- Hard cut: `transition_seconds: 0`. Soft hand-off: positive transition with
  quintic easing (smootherstep) from the previous shot's final camera pose.
- One active cutscene per `CameraDirector`, enter/exit interpolation to the
  gameplay camera. During playback: pause, seek/cut, looping, progress and
  current shot name. At the last frame, non-looping sequences return to the
  previous gameplay mode with a separate outgoing blend.
- Optional space `world` or `target`. `target` captures the player eye
  (or animated attachment) and facing/vehicle heading on the first active
  frame. Shot positions/yaw are transformed into world coordinates from this
  frozen origin, so the actor can move without dragging the cinematic rail.
- TPS-to-FPS blend collision now clips the actual intermediate camera, not
  the FPS destination at zero spring-arm length. Gameplay follow state is
  stored separately from the rendered transition state to prevent blending
  the previous blend result into its own filter.

## File format

`*.camera-cutscene.json` is a VFS asset. Schema v1:

```json
{
  "schema": 1,
  "space": "target",
  "shots": [
    {
      "name": "establishing",
      "duration": 2,
      "transition_seconds": 0,
      "track": {
        "schema": 1,
        "keyframes": [
          { "time": 0, "position": [0, 2, -6],
            "yaw": 0, "pitch": 0, "fov_y_degrees": 65 },
          { "time": 2, "position": [1, 2, -4],
            "yaw": 0.2, "pitch": -0.1, "fov_y_degrees": 55 }
        ]
      }
    },
    {
      "name": "closeup",
      "duration": 1.5,
      "transition_seconds": 0.5,
      "track": {
        "schema": 1,
        "keyframes": [
          { "time": 0, "position": [0, 2, -2],
            "yaw": 0, "pitch": 0, "fov_y_degrees": 42 },
          { "time": 1.5, "position": [0, 2, -1.5],
            "yaw": 0, "pitch": 0, "fov_y_degrees": 42 }
        ]
      }
    }
  ]
}
```

All angles in radians. Keyframe times start at the beginning of each shot.
Per-shot `duration` may exceed the track's duration (hold last frame).
`space: "world"` is the default. The first shot has no incoming transition;
later shots may use a soft hand-off within their own duration.
The existing shared `camera_track` validator enforces keyframe range,
strictly ascending times and valid projection parameters.

Bundled example in NSP:
`engine/northstarcpp/resources/sharedAssets/cinematics/northstar_intro.camera-cutscene.json`.
Built by the existing shared-asset packer into `archive/northstar_shared.nsp`.

## V8 script API

Native provider-neutral `engine.camera` methods:
- `playCutscene(vfsPath, loop=false, entrySeconds=0.55, exitSeconds=0.55)`
- `stopCutscene(exitSeconds=0.55)`
- `pauseCutscene(paused)`
- `seekCutscene(seconds)`
- `cutsceneState()` → `{ active, time, shot }`
- `playTrack(vfsPath, loop=false, blendSeconds=0.5)`
- Existing `setMode`, `pushShot`, `applyPreset`, `cut` remain supported.

The `@northstar/engine` `Camera` class has equivalent convenience methods
and updated TypeScript declarations under `api/types/northstar-engine.d.ts`.

```js
import { Camera } from "@northstar/engine";

Camera.playCutscene("cinematics/northstar_intro.camera-cutscene.json", {
    loop: false,
    entrySeconds: 0.65,
    exitSeconds: 0.55
});

// Optional controls during playback:
Camera.pauseCutscene(true);
Camera.seekCutscene(2.5);
Camera.pauseCutscene(false);
const { active, time, shot } = Camera.cutsceneState();
```

Every path resolves through the active asset provider. No direct filesystem
read was introduced into project scripts. The engine owns camera gameplay
semantics; scripting owns *when* story scenes play.

## Verification

- Build: `northstarcpp_player` Release — PASS.
- Build: `northstarcpp_camera_smoke`,
  `northstarcpp_camera_motion_smoke`,
  `northstarcpp_camera_cutscene_smoke` — PASS.
- CTest: `northstarcpp_camera_director`,
  `northstarcpp_camera_motion`,
  `northstarcpp_camera_cutscene` — 3/3 PASS.
- Other targeted CTest:
  `northstarcpp_camera_dynamics`,
  `northstarcpp_camera_director_priority`,
  `northstarcpp_camera_physics_jolt` — PASS.
- `northstarcpp_camera_streaming_physics_jolt`: FAIL in separate Jolt
  Pawn setup, `Invalid Jolt CharacterMotor parameters`. Native Jolt DLL
  reported up-to-date. This failure occurs before the new cutscene can run.
- Node.js `--check` on updated bootstrap JS — PASS.
- Asset pack: `northstar_shared.nsp` rebuilt with 110 assets, including
  the new camera sample.

## Remaining to reach feature parity

- Visual GPU check of authored sequences in the actual game and 30/60/120
  FPS comparisons; no production executables installed.
- Native multi-shot Camera Studio timeline and keyframe/shot graph editor,
  rather than only the existing single-track editor.
- Animation/story timeline event synchronization, audio cues, subtitles,
  input lock and skip/branch policies.
- Cinematic lens controls (depth of field, focus distance, aperture, motion
  blur, exposure) and optional letterbox/blacks synchronized with renderer.
- Camera collision/culling across streaming sectors during teleport, plus
  high-load Night City regression.
- Jolt `CharacterMotor` streaming Pawn regression is independent and open.
- Full Release CTest, physical GPU visual acceptance, commit/push are not done.


## Follow-up: multi-track Sequencer and GPU acceptance

The subsequent implementation added a native Qt multi-track cutscene editor,
typed camera/audio/animation/dialogue/event/overlay cues, a lens timeline,
and a V8 camera.cues event stream. It also performs native D3D12 and Vulkan
GPU render acceptance with 420 and 1100 frames each, and valid GPU timestamps.

The former "no GPU verification" limitation above is now superseded for the
synthetic cinematic test scene. Actual Night City visuals remain unverified.
Only exposure EV is a GPU-backed lens parameter. Depth-of-field, physical
aperture, motion blur and letterbox are currently authored metadata.

For measured p50/p95/p99 and the full scope, see
[CINEMATIC_SEQUENCER_GPU_QA_2026-11-16.md](CINEMATIC_SEQUENCER_GPU_QA_2026-11-16.md).
