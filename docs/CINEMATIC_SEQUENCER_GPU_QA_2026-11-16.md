# NorthStar Cinematic Sequencer + Native GPU Acceptance

Date: 2026-11-16
Build: `build/foundation-migration` (Release, Windows/Qt6)
References: `camera(6).zip` and `Cutscene.zip` (architecture and subsystem organization only; original reference code not imported).

## Implementation

The existing single-camera `CameraDirector` and `CameraTrack` remain authoritative.

**Core:** `camera_cutscene.hpp/cpp` now supports an optional `tracks` array in backward-compatible schema v1 `*.camera-cutscene.json`:
- Camera shots with Hermite motion, smooth transition or immediate cut.
- `animation`, `audio`, `dialogue`, `event`, `overlay` tracks with named lanes and global-time cues.
- `lens` keyframe track: focus in metres, aperture f-number, exposure in EV, motion-blur amount, letterbox, DoF strength.
- Strict bound checks (max 256 shots, 64 tracks, 8192 cues, 4096 lens keys) and validation of time and numeric ranges; deterministic stable event sorting.
- Full parse/serialize roundtrip. Existing files without `tracks` remain valid.

**Runtime:** `CameraDirector` collects cues crossed by each update, including time 0 and loop boundaries. Pausing does not emit duplicates; seeking clears pending cues and does not fire skipped cues. `stop_cutscene` and `reset` clear pending cues. The Player dispatches each batch on the game thread as `camera.cues` to V8. Project script listeners can subscribe to individual records through `Camera.onCue(handler)` (returns a disposer).

The engine does **not** directly play a specific sound, animation or subtitle based on an arbitrary cue; game scripts use typed VFS keys and own dispatch policy. This preserves application-specific assets and the replaceable `engine.*` API.

**GPU lens:** the Player applies per-frame cutscene `exposure_ev` to the existing provider-owned Filmic PostFX ABI, restoring the previous look afterward. Both Vulkan Deferred and D3D12 accept and render these updates. The separate focus, aperture, DoF strength, motion blur, letterbox fields are validated, animated and available through `Camera.lensState()`, but dedicated depth-based DOF, velocity motion blur and letterbox GPU/compositor passes are **not** implemented; do not advertise them as visual effects yet.

**Qt editing:** `qt_cutscene_timeline.hpp/.cpp` implements a modeless visual sequencer accessed from Qt Editor > Camera Studio > Multi-track Cutscene Sequencer. Its interactive time canvas displays shot bars, transition intervals, cue markers and lens exposure keys; four editable table tabs expose shots, camera keyframes, cue lanes and cinematic lens data. There are Play/Pause, Stop, timeline scrubbing, Camera Preview in the editor viewport, validation, New/Open, and QSaveFile atomic Save / Save As with external-change checks.

**Script SDK:** `@northstar/engine` Camera has `playCutscene`, `pauseCutscene`, `seekCutscene`, `stopCutscene`, `cutsceneState`, `lensState`, `onCue`, with matching TypeScript definitions.

The bundled template contains a six-lane sample:
`engine/northstarcpp/resources/sharedAssets/cinematics/northstar_intro.camera-cutscene.json`.
It is regenerated into `build/foundation-migration/archive/northstar_shared.nsp`.

## Example project orchestration

```js
import { Camera } from "@northstar/engine";

const disposeCues = Camera.onCue(cue => {
    if (cue.type === "animation") {
        // Look up the project-defined actor and validated clip.
    } else if (cue.type === "audio") {
        // Dispatch through project audio resources/provider.
    } else if (cue.type === "dialogue") {
        // Send dialogue/subtitle ID to game UI.
    } else if (cue.type === "event") {
        // Advance the user-authored narrative state.
    }
});

Camera.playCutscene("cinematics/northstar_intro.camera-cutscene.json", {
    entrySeconds: 0.55, exitSeconds: 0.55, loop: false
});

// Release the listener during the owning script's stop lifecycle.
disposeCues();
```

No hardcoded story missions, characters, voice files or visual widgets were added to engine C++.

## QA / measured results

Qt:
- Dedicated `northstarcpp_cutscene_sequencer` native Windows CTest: **PASS**. Opens actual QDialog, validates 4 tabs, camera preview and time controls, mutates cue table, writes and reopens the file through native parser, checks painted pixels and saves screenshot at:
  `build/foundation-migration/qa/cinematic-sequencer/northstar_cutscene_sequencer.png`.
- Isolated `northstarcpp_editor_qt_cutscene_qa.exe`: **PASS** `--camera-studio-smoke`, `--content-dock-smoke` and `--material-function-smoke` in focused runs.
- General `--smoke` passed earlier, but after the final rebuild its repeated executions were **intermittent**: 1/3 PASS and 2/3 FAIL, reporting `hierarchy roundtrip saveScene() failed` and `NEF8 opaque ECS component roundtrip failed`. A separate run reported `material binding was not serialized`. These are independent editor scene-persistence/NEF8 regressions and **must not be described as fully resolved** by the cinematic work.
- Player and all changed Qt Editor objects compiled. Main Qt Editor EXE remained locked by the running process and was not replaced.

CPU regression:
- `northstarcpp_camera_cutscene`, `northstarcpp_camera_director`, `northstarcpp_camera_motion`, `northstarcpp_camera_dynamics`, `northstarcpp_camera_director_priority`, `northstarcpp_camera_physics_jolt`, `northstarcpp_cutscene_sequencer`, `northstarcpp_qt_text_asset`, `northstarcpp_code_editor_smoke`: **9/9 PASS**.
- JS `node --check` on bootstrap SDK: **PASS**.

GPU (actual provider + HWND + actual frame draw, nonblack readback, exposure ABI set/get + provider GPU timestamps):

| Provider, scope | Valid GPU timestamps | GPU p50 (ms) | GPU p95 (ms) | GPU p99 (ms) | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| D3D12, 420 frames | 420 | 0.2947 | 3.1068 | 3.7315 | PASS |
| Vulkan, 420 frames | 420 | 0.8563 | 2.2673 | 4.4759 | PASS |
| D3D12, 1100 frames | 1100 | 0.4444 | 2.7669 | 3.0194 | PASS |
| Vulkan, 1100 frames | 1100 | 1.2178 | 1.3058 | 1.9301 | PASS |

The 1100-frame stress loops the timeline, performs 1100 lens updates, and enters eight shot segments. Frame submit + pixel verification p95: D3D12 17.331 ms, Vulkan 17.222 ms. This is separate from provider GPU timestamp duration.

Vulkan initially failed once after source changes with `Incomplete Vulkan graph execution contract`; rebuilding `render_vulkan.dll` coherently with the current RenderGraph restored success. Added detailed plan/bindings diagnostic to `renderer_frame.cpp` for future failures. These GPU tests do not compare before/after luminance at identical camera pose and they do not represent Night City performance.

D3D12 originally hit an unrelated PSO compiler error in dirty existing source `renderer_materials.cpp`: local `v2` wasn't captured in `std::async` lambda. Added the missing lambda capture. D3D12 DLL and Player build then passed.

## Remaining for full cinematic parity

1. GPU depth-based DOF and aperture bokeh; motion-vector motion blur; film-safe true letterbox. Requires render graph resource/pipeline work and independent Vulkan/D3D12 pixel validation.
2. Sample-accurate animation/audio sync across timeline seeks, pause/branch/skip and dialogue lip sync. Current event delivery is deterministic on the game thread but project scripts own actual playback.
3. Better native keyframe handles/drag-resizing/snapping, undo/redo, waveforms, audio/video playback and visual preview of final scene in Sequencer. Current Qt authoring is a modeless multi-tab dialog, not a docked Unreal-like full Sequencer.
4. E2E full GPU visual acceptance for Night City, real 3D character rigs and transport, frame comparisons under Vulkan/D3D12, and ≥1000 GPU timestamps in that **game** scene.
5. Independent `northstarcpp_camera_streaming_physics_jolt` regression previously failed on `Invalid Jolt CharacterMotor parameters`. This is not caused by the new cutscene implementation.

No Git commit/push, no deployment or replacement of a running Editor executable.
