# Canonical Skeleton / Weapon Grip / ADS — 2026-11-17

## Scope

Incremental, source-only repair of humanoid bind-pose detection, wrist basis retargeting, authored support grip targeting, and regression coverage. Existing unrelated worktree changes were preserved. No commit, push, production binaries or dist replacements.

## Modified

- `modules/animation/skeleton.cpp`: determine face-forward from a **pair of bind-space eyes** instead of the laterally biased right eye. Infer anatomical side using pelvis-to-head up and orthogonal face-forward; repair both complete arm chains and prop sockets atomically. When side data is inconclusive, retain the name mapping.
- `modules/viewport/viewport_skeletal_animation.cpp`: convert canonical right/left wrist rotations to the imported hand's **finger/thumb bind axes** in first-person mode. Preserve the historical third-person wrist calibration because current TPS scene attachments still carry authored fixed socket rotations; TPS now targets the actual `support_grip` from the right-hand prop socket when the profile supplies it, with a lateral anti-crossing guard. ADS continues to align the sight using the actual primary socket after primary-hand IK.
- `tests/viewport_pistol_ads_smoke.cpp`: synthetic unswapped/swapped rig regression, paired-eye side check, world-space canonical hand/elbow separation measured against the actual shoulder axis, and seven ADS view directions.
- `tests/viewport_skeletal_pipeline_smoke.cpp`: measure the canonical finger axis and canonical primary/support grip joints rather than presuming hand local +X and raw `r_*/l_*` names remain anatomically correct after repair.

## Verified

| Check | Result |
| --- | --- |
| Build: `northstarcpp_viewport_pistol_ads_smoke` | PASS |
| Run: pistol ADS smoke + canonical arm swaps | PASS |
| ADS camera ray vs rear/front sight in tested poses | PASS (reported residual 0.0000 m) |
| Canonical wrist/elbow lateral separation in ADS smoke | PASS, using body-relative lateral axis |
| Build and run: `northstarcpp_weaponlab_grip_retarget_smoke` | PASS |
| WeaponLab finger bends / mirrored helpers / tearing | PASS (0 new torn edges) |
| Build: `northstarcpp_viewport_skeletal_pipeline_smoke` | PASS |
| Run: full skeletal pipeline | **FAIL** on high downward camera pitch |

## Remaining blocker (do not waive)

`viewport_skeletal_pipeline_smoke` at first-person yaw 0, pitch -1.10 rad reports left elbow collision correction approximately **0.039–0.040 m**, exceeding its strict < 0.035 m threshold. The positional wrist and gun checks are green in that case; the elbow is not. Simply increasing corrective pole/IK iterations did not resolve it and was reverted to avoid increasing frame cost.

To finish: collision-aware elbow/clavicle IK that can find a feasible clearance without displacing sight-aligned primary socket, plus whole-model visual QA for hands/finger wrap in editor and frame-by-frame fire/sight ray agreement. Third-person arbitrary rig socket orientation remains profile-dependent and has not been generalized to imported hand bases.

## Notes

Build linker emits duplicate Boost `boost::any::placeholder` section size warnings in this toolchain; the smoke executables nevertheless link and run. Full production/editor clean build, in-editor preview, live skinned mesh collision inspection and actual projectile-path verification were not performed.
