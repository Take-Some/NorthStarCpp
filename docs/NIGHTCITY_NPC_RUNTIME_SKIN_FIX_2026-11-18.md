# KaylaNightCity — NPC deformation and sliding diagnosis / repair

**Date**: 2026-11-18
**Scope**: Ellie / Isaac, native NVRM YDD + YMT + YCD, Jolt AI Pawn / NavMesh
**Location**: NorthStarCpp working tree (not a release)

## Reproduction
In the original scene, Ellie and Isaac physically moved their capsule bodies
with Jolt AI motor and the scripts changed `npc-idle` and `npc-walk` YCD.
The on-screen mesh showed gross deformations and barely changed its pose.

Two independent root causes were verified:

1. Original Naughty Dog PAK skin-stream IDs were exported in an NVRM YDD as
   `northstar.source_skin.inert_streams.v1` with
   `skin_binding.runtime_skinning_enabled=false`. These source IDs were
   never established to be the global target YMT bone IDs. The NVRM loader
   nonetheless treated them as verified global YMT indices. Reproduction:
   vertices in the Ellie hair-cloth sections were skinned by
   `r_knee`, `r_ankle`, `r_ball`. On YCD walk t=0.45 s, 801 out of
   100,038 mesh vertices moved more than 1.0 m from bind position and
   the peak displacement was 1.522 m.
2. When a rig was too large for current provider GPU skinning, CPU skinning
   correctly evaluated and updated animated vertex positions, but
   `viewport_draw.cpp::submit_range` erroneously selected
   `northstarcpp_render_rigid_instances` **immutable GPU meshes** for the
   resulting `play.cpu_mesh`. The first sampled pose stayed cached on the
   GPU while Jolt moved the Pawn. It thus appeared to skate / translate
   without walking, despite YCD changing CPU rotations.

## Source changes

- `engine/northstarcpp/modules/models/native_skin_reconstruction.hpp`
  Adds an explicit, bounded approximation for **inert** NVRM skin streams.
  Maps each per-submesh palette ID to a nearby canonical YMT joint from
  source-weighted mesh vertex centroids; a spatial fallback limits bones
  whose centroid is implausibly far from the skinned vertex. Original
  per-vertex weights and mesh materials/topology are kept. This is not
  true original PAK bone palette decoding or artistic skin painting.
- `engine/northstarcpp/modules/models/character.cpp`
  Honors `skin_binding.runtime_skinning_enabled`; for inert source streams
  builds the inferred mapping. Adds a once-per-character warning marking
  the remapping as approximate/inferred.
- `engine/northstarcpp/modules/viewport/viewport_draw.cpp`
  Immutable GPU rigid-mesh path is now guarded by
  `selected_mesh == &found->second`. A CPU-deformed
  `play.cpu_mesh` is **always submitted as transient vertex geometry**.
  Optional `NORTHSTARCPP_TRACE_NPC_POSE` logs sample time/ankle rotation
  for a small diagnostic frame budget.
- No edits to original YDD/YMT/YCD assets, model names, Pawn IDs 155/156,
  collision channels or physical NPC transforms.

## Evidence / regression tests

### Native CPU skinning
- `tests/nightcity_native_skin_acceptance.cpp`
- Test executable: `northstarcpp_nightcity_native_skin_acceptance`
- CTest name: `northstarcpp_nightcity_native_skin`
- Real project YCD, original YDD, original YMT and every source vertex.
- Ellie walk t=0.45: **peak 1.522 m before, 0.554 m after**.
- Vertices displaced more than 1 meter: **801 before, 0 after**.
- Strict assertion now fails if any sample peak >0.9 m or >1 m vertices.
- PASS for both actual characters across sampled idle/walk timestamps.

### Real D3D12 GPU frames, not synthetic pose tests
- `tests/nightcity_npc_gpu_smoke.cpp`
- CTest names: `northstarcpp_nightcity_npc_gpu` (Ellie),
  `northstarcpp_nightcity_isaac_npc_gpu` (Isaac).
- Native provider's RGBA framebuffer is read back with
  `Viewport::request_scene_frame_capture/read_scene_frame_capture`.
- Before CPU mesh cache fix Ellie walk-frame changes:
  365 / 503 / 449 / 495 pixels for phases 6→16 / 16→27 /
  27→38 / 38→53.
- After fix Ellie: 5,093 / 4,313 / 5,132 / 5,054 pixels respectively.
- Isaac after fix: 1,361 / 1,532 / 1,487 / 3,914 pixels.
- Test thresholds now reject unchanged/frozen poses.
- Screenshots (renderer raw PPM):
  `out/build/npc_gpu_qa/{ellie,isaac}_walk_{6,16,27,38,53}.ppm`.
- Both D3D12 real-GPU CTests PASS.

### Real KaylaNightCity gameplay integration
- `tests/nightcity_gameplay_live_acceptance.py`, 520-frame
  isolated project test; uses temporary hardlink-based asset copy and
  copy-on-write JS instrumentation.
- Compiled isolated QA Player executable
  `out/build/northstarcpp_player_qa.exe`, separate from actively opened
  Qt Editor and Player processes.
- PASS, exit 0, frame 520. NPC horizontal net displacement:
  Ellie **1.5977 m**, Isaac **1.5067 m**; idle/walk clip transitions
  recorded for both, no engine.ai or YCD errors.
- `MT28 ready` present; no `Error: stod`.

## Known limitations
- Reconstruction from unmapped source skin is **an approximation**, not
  the original Naughty Dog per-submesh bone palette, cloth simulation or
  production-quality character skinning. Source bone remapping and proper
  pose/foot-contact QA remain next milestones.
- The existing YCD locomotion clips are **retargeted from Abby**, not
  Ellie/Isaac's native source ICE tracks.
- Root displacement is deliberately stripped from these retargeted clips;
  Jolt AI motor owns world-space motion. For production foot locking,
  match playback rate to world travel and add final foot-plant correction.
- Current NVRM rigs have 757–1074 joints, above the initial GPU skin
  palette supported by current providers; CPU skinning has higher
  per-frame vertex upload cost. Future GPU skinning needs a suitably sized
  or split joint palette.
- No claim of complete polygonal NavMesh/crowd solution.

## Deployment
No original source asset overwritten. Running
`northstarcpp_editor_qt.exe` / `northstarcpp_player.exe`
in another project were left untouched; their loaded DLLs/binaries
cannot safely be replaced while open. QA Player is separate.
No production distribution replacement, commit or push.
