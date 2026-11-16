# Abby — two-hand pistol pose and first-person skinning correction

Date: 2026-11-16
Build: NorthStarCpp **0.6.0** (current source version at final packaging)
Status: QA, not production.

## Defects
1. A recent experiment reversed quaternion composition in the imported rig's CCD solver, causing severe rotations and knots. This experiment was reverted.
2. Legacy weapon IK had no elbow pole or angular constraints. It targeted the left wrist ~13 cm from the right, using a generic shoulder/elbow solver that could take the elbow through unnatural trajectories.
3. Abby YMT contains 1,033 joints, including independent helper and twist branches. In first person the old skin-weight filter allowed mixed torso/shoulder triangles into the arm viewmodel, producing floating/stretched patches.

## Implementation
- New `modules/animation/skeleton_ik.cpp`: `SkeletonPose::pose_two_bone`, an anatomically constrained two-bone pose solver using calibrated elbow poles, reach limits, and bounded shoulder/elbow rotations. The solver measures the actual row-vector FK result before accepting quaternion rotations and updates imported secondary skin constraints.
- `modules/viewport/viewport_skeletal_animation.cpp`: right wrist is aimed at the weapon-holding position, with both elbows kept down/out; the support wrist is targeted ~5.5 cm to the left of the right wrist at the real pistol socket. Both hands are solved in the same camera/body coordinate space.
- `modules/viewport/viewport.cpp`: for advanced multi-helper humanoid rigs, **FPS arm-only visibility masking** now excludes mixed chest/shoulder influence vertices (threshold 0.975). This is a viewmodel-specific anatomy crop, **not geometry simplification or a reduction of full-model LOD**. Full authored meshes, original YDD materials, UVs, and 8 skin weights remain unchanged in world/TPS rendering.
- Restored the prior known-good multiplication in the generic CCD function; new pistol IK is separate and explicit.
- `tests/viewport_skeletal_pipeline_smoke.cpp`: synthetic rotation/segment-length regression plus a real Abby YDD/YMT/YCD test with wrist distance, support-hand socket proximity and residual constraints. The test runs with the real sharedAssets provider.

## QA
- Full Ninja Release `all`: PASS.
- CTest `northstarcpp_viewport_skeletal_pipeline`: PASS with full Abby assets.
- Additional CTests `provider_catalog_sync`, `player_provider_inspection`, `player_blueprint`: 4/4 PASS.
- Real Player V8/Jolt/D3D11 runtime (temporary QA copy with HUD disabled): PASS, including 5 frames, pistol socket, native animation and a single authored Abby.
- Real Abby forward-facing FPS joint positions in mesh space:
  - right wrist (0.098, 1.122, 0.342)
  - left wrist (0.043, 1.110, 0.357)
  - wrist separation approximately **0.058 m**, compared to approximately **0.17 m** with the prior unconstrained IK.
  - left-hand support residual ~0.000 m; right-hand target residual ~0.047 m.
- Under the viewmodel skin selection, approximately **25,335 high-detail source triangles** remain visible. Diagnostic stretched-edge count is ~30 under the new selection versus 270 with the previous 0.48 weight threshold. Some elbow twist/cloth transitions still require visual and skinning QA.
- Packaged QA Player `--check-scene`: PASS.
- Packaged Project Manager `--smoke`: PASS.
- Packaged Qt Editor `--content-dock-smoke` and `--place-actors-smoke`: PASS.

## Known limits
- Full graphical review of the corrected character at different camera pitches and animation frames has NOT been completed. An attempted automated desktop image capture did not return a reliable screenshot. Do not claim exact final finger grip pose is visually accepted.
- `northstarcpp_shared_assets` full smoke remains blocked by an unrelated pre-existing malformed `prefabs/tree_animate/textures/tree_animate.ytd` NEF8 fixture.
- The D3D11 GPU UI compositor is still unavailable; runtime test disabled HUD only in a temporary fixture.
- Source CMake and plugin manifest were changed to version 0.6.0 by other concurrent workspace work before this package was generated. No downgrade to 0.5.0 was performed.
- Production Engine and distribution NOT replaced. No commit or push.

## QA location
`C:\Users\Kayla\Documents\NorthStar\NorthStarCpp\out\NorthStarCpp-0.6.0-AbbyPistol-PoseFix-QA\bin\northstarcpp_project_manager.exe`

Launch via `tools\Start-Abby-PoseFix-0.6.0-QA.cmd` from the repository.
