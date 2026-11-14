# NorthStarCpp camera system

## Scope and provenance

The supplied `camera(3).zip` includes 139 C++ source files and 140 headers from a Rockstar Games camera subsystem (copyright notices in the original files). It relies on game-specific entity, ped, vehicle, physics, RTTI and render services that do not exist in NorthStarCpp. The archive is used only to identify camera behaviors and coverage; **none of its proprietary code is included in the engine**.

This project implements an independent camera director compatible with NorthStarCpp C++17 and the engine's existing `Scene`, FPS and render contracts.

## Implemented features

* `CameraMode`: first-person, third-person, vehicle chase, orbit, free and cinematic.
* `CameraPose`: world position, yaw, pitch, roll, vertical FOV, near/far planes.
* `CameraDirector`: mode transitions with easing, exponential smoothing, shortest-angle yaw transitions, configurable follow/orbit offsets, aim FOV, teleport snapping and deterministic decaying camera impulses.
* Geometry-aware third-person collision: a line-of-sight test against expanded scene-object AABBs prevents camera movement through the represented cube scene. The implementation skips the tracked player entity. This is a conservative approximation, **not** a continuous capsule/sphere sweep through arbitrary triangle meshes.
* `CameraTrack`: AssetManager-loaded cinematic keyframes, Hermite position interpolation, shortest-arc rotations, FOV blending, seek, pause, loop and deterministic playback.
* Camera input validation: finite state and times, supported FOV/clip ranges and safe asset sizes.
* The D3D11 provider consumes an explicit camera pose, FOV, roll and clipping distances via `NorthStarRenderFrame` ABI 4. The editor's legacy view remains unchanged unless a camera override is supplied.
* FPS integration: camera position and orientation follow authoritative gameplay aim, RMB controls smoothly interpolated aim FOV, and shot events produce small visual-only translation impulses. The reticle/hitscan orientation stays exact.
* Project-specific settings are read from `assets/gameplay/camera.json` through the active `engine.assets` AssetManager provider. Missing camera settings remain optional for older FPS projects. Invalid present settings produce an error.

## Example C++ usage

```cpp
#include <northstarcpp/camera.hpp>
#include <northstarcpp/assets.hpp>

northstarcpp::AssetManager assets;
assets.mount_directory("", "MyGame/assets");
auto settings = northstarcpp::load_camera_settings(assets);

northstarcpp::CameraDirector director(settings);
director.set_mode(northstarcpp::CameraMode::third_person, 0.35f);

northstarcpp::CameraTarget player;
player.eye = {0.f, 1.6f, 0.f};
player.yaw = 0.f;
player.ignore_object = 0;  // tracked entity in Scene::objects

northstarcpp::Scene world;
northstarcpp::CameraPose view = director.update(1.f / 60.f, player, &world);
```

Use `set_free_pose` and `set_cinematic_pose` for externally authored poses.
The `load_camera_track` API reads camera rails from the VFS, e.g.
`cinematics/intro.camera.json`. Call `director.set_track(track, true)`
and switch to `CameraMode::cinematic` to play a looping rail.
Use `seek_track` for timeline scrubbing. The modes are accessible to C++ integrators, but only first-person is wired to the FPS user-facing input.

## Game asset

`Projects/NorthStarFPSArena/assets/gameplay/camera.json` is the current configuration. Fields cover lens FOV, near/far clipping, follow smoothing, third-person/vehicle/orbit offsets, obstacle radius and camera teleport threshold. Edit it without rebuilding the engine.
A test cinematic is bundled as `Projects/NorthStarFPSArena/assets/cinematics/intro.camera.json`.

## Build and test

```powershell
cmake --build out/camera-native-release --target northstarcpp_camera_smoke northstarcpp_camera_render_smoke
ctest --test-dir out/camera-native-release -R "camera_director|camera_d3d11|fps_arena|fps_assets" --output-on-failure
```

The render plugin contract was intentionally incremented from ABI 3 to **ABI 4**. Custom render providers must update their frame layout and ABI reporting before being used with the new runtime. The change is not binary-compatible.

## Remaining gaps against the reference

The archive contains far more than six modes. NorthStarCpp does **not** yet replicate the full cinematic director, replay recording, visual timeline/editor, camera banks, triangle-mesh occlusion, advanced vehicle/animation context handling, shoulder swaps, post-processing/DOF/lens distortion, or VR. The new cinematic tracks are data-driven, but the visual authoring tools do not exist. Full equivalence is not claimed.

For production parity, add engine.physics mesh sweeps, occlusion and shoulder selection, advanced cinematic event sequencing, replay recording and playback, animation-linked camera offsets, easing/velocity curve editors, and frame-by-frame acceptance comparisons.
