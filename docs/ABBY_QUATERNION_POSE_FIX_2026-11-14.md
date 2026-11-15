# Abby — YMT/YCD Quaternion Convention and Anatomical Pose Fix

Date: 2026-11-14
Status: rebuilt development Release Player validated; final visual confirmation still needed. Frozen QA/dist not replaced.

## Cause

Original imported YMT bind and YCD keyframe quaternions used an opposite rotation handedness to NorthStarCpp row-vector matrix construction. Direct use resulted in wrong bind joint pivot locations: wrists, elbows, knees and ankles rotated around places far from their corresponding YDD vertex clusters, giving the character a swollen scarecrow-like silhouette.

Fixed at the common YMT/YCD format boundary in engine/northstarcpp/modules/animation/skeleton.cpp by conjugating source quaternions before creating bind/animated local transforms. Mesh and original source assets were not altered. Both Player and Editor use the common animation module.

## Actual 1033-joint skeleton to weighted YDD bind alignment

| Joint | Pivot-to-weighted-vertex-centroid distance |
|---|---:|
| Left wrist | 0.0301 m |
| Right wrist | 0.0282 m |
| Right knee | 0.0921 m |
| Left knee | 0.1008 m |
| Right ankle | 0.0644 m |
| Left ankle | 0.0671 m |

Idle sampled at 0.2 s (YDD coordinates): wrist X left/right -0.281 / +0.306 m; elbow Y left/right 1.112 / 1.097 m. Import regression tests assert correct locations and symmetry, rather than printing logs alone.

## Deformation validation

Using 377,655 triangular mesh edges per time sample and the genuine clips abby-mm-explore-idle and abby-mm-explore-walk-loop-fw:

- At t=0, 0.2, 0.5, 1, 2, 3, 5, and 8 seconds: zero severe stretched edges for both idle and walk.
- With viewport mesh_to_skeleton conversion: zero severe stretched edges at 0.2 s for both clips.
- Before the quaternion convention correction, sampled walking frames reached 449 severe edges.

The enhanced tests now require zero detected severe stretch and correct anatomical joint placement. No procedural geometry shrink, skin-weight masking, or arbitrary bone limiting was used.

## Build and test status

- Full Release build: PASS (Ninja / MinGW, parallelism 2).
- 4 focused CTests: PASS (skeletal_animation, skeletal_asset_interop, abby_character_warp, shared_assets).
- Full workspace CTest: 110/114 PASS, not a complete pass.

Four failures stem from missing independent test fixtures in the concurrently changing working tree:
1. QA reporter: Projects/NorthStarEngineQA/qa.manifest.json missing.
2. Scene OBJ mesh: OBJ lab asset missing.
3. Scene animation: AnimationLab startup scene missing.
4. Gameplay scripts: NorthStarAnimationLab/animations/lift.anim.json missing.

Those other project files were not restored or altered by this work. Production and previously frozen QA dist were not replaced.

## Reproduction

From the NorthStarCpp repository root, stop the old Player before re-launching:

    Projects\NorthStarFirstPersonPlatform\run-abby.cmd

Development Player: out/fps-release-qa-20261114/northstarcpp_player.exe.

The corrected math is verified numerically and via WARP, but a fresh screenshot of the newly restarted game remains required for final visual acceptance.
