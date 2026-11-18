# NorthStarCpp Gameplay Framework — navigation, camera GPU and NPC acceptance
Date: 2026-11-18
Workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
Target: NorthStarCpp 0.7.0 local working tree

## Changes actually made

### Navigation
- Added provider-neutral `navigation::RuntimeNavMesh` in
  `engine/northstarcpp/modules/api/include/northstarcpp/navigation_navmesh.hpp`.
- Physics-rasterized layered **walkability lattice**, not a Recast-style polygon navmesh:
  floor height and surface normals sampled from Jolt physics, walkable slopes,
  step-height and adjacency clearance tests, A* over XZ neighbors, vertical
  separation and disallowing unsupported off-mesh jumps.
- Bounded local coverage and partial route status. A tile can be rasterized
  incrementally via `begin_build/advance_build`, and NPCs share one 72-cell
  per-frame raster budget (up to 24 per NPC per frame).
- `update_ai_motors` now advances A* waypoints with physical Jolt multi-agent
  motor velocities. Real collision sweep checks and stall-triggered replanning.
  No per-frame teleportation.
- AIController's arrival requires both XZ proximity and vertical proximity.

**Limits**: this lattice is not a polygon mesh/Detour graph; there is no global
shared tile cache, fast incremental path search, link-based elevator/ladder/
stairs traversal, crowd RVO avoidance, static/dynamic collision revision system,
or full streaming-cell synchronization. A* edge sweeps remain synchronous
and may exceed acceptable frame budget on dense maps. This is an experimental
Gameplay Framework subsystem, NOT a production-complete city NavMesh.

### Real KaylaNightCity NPC migration
- The existing KaylaNightCity script previously advanced Ellie and Isaac
  using `setPosition()`, bypassing Jolt's physical NPC motion.
- `tools/dev/migrate_nightcity_npc_ai.py` safely migrated actual
  `Projects/KaylaNightCity/main.nscn` and
  `Projects/KaylaNightCity/assets/scripts/main.js`.
- Preserved authored entity IDs (155: Ellie, 156: Isaac) and source asset
  paths, collision records, model/animation components and other NEF8 records.
- For both added versioned `northstar.gameplay.pawn` and
  `northstar.gameplay.character_motor` components, with dynamic mass 72 kg,
  no auto-possession.
- New project script `engine.ai.navigation.moveTo/stop` calls the native
  navigation/Jolt motor. Direct transforms were removed from patrol movement.
  Existing interaction and YCD animation selection remain project-owned.
- Source backups at:
  `Projects/KaylaNightCity/migration/npc_ai_before_20261118-181632/main.nscn`
  `Projects/KaylaNightCity/migration/npc_ai_before_20261118-181632/main.js`
- Migration `--verify` PASS, JS `node --check` PASS.
- Player real scene `--runtime-init-smoke` PASS (156 objects, 15 ECS
  components; Jolt 5.6, V8 15.8.62, Flecs 4.2). A separate existing
  MT28 scripted vehicle startup warning (`Error: stod`) remains.
- **Open**: full interactive city NPC walking and visually confirmed
  skeleton clip playback still require timed/frame-capture play-mode QA.
  The NPC project script still handles animation state via its own
  `Scene.playAnimation` calls. Motor yaw/turning and source clip blend quality
  have not been evaluated visually.

### Real asset skeleton/clip validation
- Added `tests/nightcity_npc_ycd_tracks_acceptance.py` (read-only).
- Parsed actual Ellie/Isaac YMT and native NEF8 YCD clips:
  each has 35 joint tracks; idle 21 animated tracks, 501 frames at 30 Hz;
  walk 23 animated tracks, 106 frames at 30 Hz; finite transforms, no
  out-of-range or duplicate joint IDs.
- These are retargeted native YCD assets originally sourced from other
  animations, not decoded Ellie/Isaac original ICE tracks.
- Animation Graph, Skeletal Animation and Viewport Skeletal Pipeline unit
  tests passed. This does not imply actual in-game skinned GPU acceptance.

### Camera switching: actual rendered GPU frames
- New `tests/camera_view_target_gpu_smoke.cpp` and CMake/CTest target
  `northstarcpp_camera_view_target_gpu`.
- Constructed genuine PhysicsScene + player blueprint + independent camera
  and geometry. Changed camera View Target while preserving PlayerController
  possession, then returned to Pawn.
- Initial GDI BitBlt capture of GPU swapchain was unreliable (read white).
  Replaced it with actual renderer-owned `Viewport::request_scene_frame_capture`
  and `read_scene_frame_capture` RGBA8 ABI.
- **D3D12** capture: 72,636 changed pixels; **Vulkan**: 72,548;
  return transition changes correspondingly. Both PASS.
- CTest `northstarcpp_camera_view_target_gpu`: PASS.

## QA completed
- `northstarcpp_navigation_navmesh`: PASS
- `northstarcpp_gameplay_framework`: PASS
- `northstarcpp_animation_graph`: PASS
- `northstarcpp_skeletal_animation`: PASS
- `northstarcpp_viewport_skeletal_pipeline`: PASS
- `northstarcpp_camera_view_target_gpu`: PASS
- `northstarcpp_player` full CMake build: PASS
- `python tests/nightcity_npc_ycd_tracks_acceptance.py`: PASS
- `python tools/dev/migrate_nightcity_npc_ai.py --verify`: PASS
- `node --check Projects/KaylaNightCity/assets/scripts/main.js`: PASS
- `northstarcpp_player.exe --runtime-init-smoke --scene
    Projects/KaylaNightCity/main.nscn`: PASS

## Shipping policy
Production dist binaries not replaced. No commit or push.
The working tree was already heavily modified before this phase.
