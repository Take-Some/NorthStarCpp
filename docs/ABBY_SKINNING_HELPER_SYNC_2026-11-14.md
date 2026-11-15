# Abby skinning deformation — helper-rig synchronization

Date: 2026-11-14
Status: main distortion fixed in CPU skinning; residual secondary deformation requires visual QA.

## Root cause

Abby's 1,033-joint YMT contains a second, bind-identical set of helper joints.
YCD locomotion clips key the primary skeleton, not the helper chain.
51 bind-identical *_helper joints stayed at rest while primary bones moved.
Adjacent vertices reference both hierarchies, causing stretched triangles
around sleeves, pants, hair, and gear.

## Fix

modules/animation/skeleton.cpp builds Skeleton.mirrored_helpers at YMT
import, matching the joint name suffix and strict 1e-4 bind equivalence.
SkeletonPose::sample propagates primary keyed local transforms to those
validated helper joints before global pose evaluation. The source YMT/YDD/YCD
and original engine row-vector math remain unchanged. The cached mapping
eliminates per-frame name-map construction.

A regression test now asserts the mapping is present and that skeletal
triangle elongation remains below the measured acceptance bounds.

## Measured result, real Abby, 377,655 triangle edges per animation

| Evaluation at 0.2 seconds | Previous | Updated |
|---|---:|---:|
| Excessive edge stretch, walk | 2,030 | 281 |
| Excessive edge stretch, idle | 419 | 28 |
| Excessive edge stretch in jacket arms, walk | 1,796 | 0 |

The tests require under 400 stretched edges for walk and under 50 for idle.

## Textures

The existing BC3/BC7 correction recognizes legacy BC3 alpha blocks
mislabelled BC7, restoring skin albedo. It does not edit the original YTD.

## Verification and limits

- Release Player built with the new pose evaluator.
- skeletal_asset_interop: PASS, including mesh-edge bounds.
- abby_character_warp: PASS.
- shared_assets: PASS.
- The *new screenshot* after these skeletal changes has not been reviewed.
  WARP success is not a claim of exact visual parity.
- Remaining stretch is localized mostly to hair, backpack, trousers and shoes.
  These require proper authored secondary bone constraints, not indiscriminate
  vertex-position clamping.
- The previously packaged QA and production dist are unchanged.


## Follow-up — 2026-11-14, detached head and inflated arms

The next user screenshot still showed an incorrect upper-body silhouette,
despite prior single-frame edge reductions. Additional inspection confirmed:
- Abby has a 1,033-joint YMT with two roots, including a secondary hierarchy
  of procedural neck, deltoid, twist, roll and joint-offset bones.
- The YCD locomotion clips key primary bones, leaving secondary bones without
  their original constraint controller.
- Primary YCD translations match bind offsets and their rotations are
  finite; deleting the YDD mesh-to-skeleton basis makes the pose *worse*.
- The previous one-frame test underestimated visible defects.
  Walking exhibited up to 494 excessively stretched triangle edges at t=5s.
- Naively assigning all secondary joints to their nearest primary bone
  increased the defects; the experiment was discarded.

A conservative secondary constraint map is now constructed once when
importing a skeleton. It derives driven arm/forearm, upper-leg/knee/ankle,
and neck auxiliary palette matrices from the evaluated YCD joints. It
shares the native row-vector math and does not modify original resources.

Measured against 377,655 triangle edges per animation:
| t=0.2 seconds | Before secondary constraints | After |
|---|---:|---:|
| Walk elongated edges | 281 | 121 |
| Idle elongated edges | 28 | 0 |

The representative walk scan at t=0, 0.2, 0.5, 1, 2, 3, 5, 8s yielded:
37, 121, 85, 5, 291, 53, 449, 5 edges. Idle yielded:
0, 0, 0, 0, 2, 2, 4, 2 edges.
Residual tearing remains, especially backpack and shoe edges.
The current test now checks multiple times, with explicit limits,
rather than reporting success for just the 0.2-second frame.

Release Player and 4 focused tests passed (skeletal_animation,
skeletal_asset_interop, abby_character_warp and shared_assets).
No production dist or previously packaged QA release was overwritten.

**Limit:** An actual visual capture of the newly updated Player rendering
has not been obtained. The screenshot supplied by the user predates this
latest set of constraints. Exact visual correction of the head, arms and
feet cannot yet be asserted. Completing original procedural twist/roll
constraints and a frame-by-frame pixel comparison remain acceptance blockers.
