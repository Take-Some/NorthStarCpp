# NorthStarCpp — IK reference review and procedural IK integration
Date: 2026-11-17
Source: user-provided `ik.zip` (42 ZIP entries; ~847 KB uncompressed).

## 1. What the reference archive contains

The archive is a Rockstar Games RAGE-specific IK subsystem, not a standalone
runtime library. It contains a request/manager system and dedicated solvers:

| Source | Capability |
| --- | --- |
| `ik/IkManager.*`, `IkRequest.*` | solver registration, request priorities, lifecycle, pool and LOD budget |
| `ik/solvers/ArmIkSolver.*` | arm reach and rotation, two-hand weapon cooperation, recoil targets |
| `ik/solvers/LegSolver.*` | stance, terrain probes, pelvis correction, foot height/orientation, stairs |
| `ik/solvers/BodyLookSolver.*`, `HeadSolver.*` | distributed look-at: torso/neck/head/eyes, limits and smoothing |
| `ik/solvers/TorsoSolver.*` | torso-driven weapon aiming |
| `ik/solvers/TorsoVehicleSolver.*` | vehicle-relative lean, seat reach and arm adjustment |
| `ik/solvers/BodyRecoilSolver.*`, `TorsoReactSolver.*` | layered recoil and physical reaction |
| `ik/solvers/RootSlopeFixupSolver.*` | procedural root alignment on slopes |
| `ik/solvers/QuadLegSolver.*`, `QuadrupedReactSolver.*` | quadruped IK / reactions |
| `ik/solvers/*Proxy.*` | optional solver lifetime/allocation barriers |

The original implementation depends on proprietary RAGE skeleton types,
physics shape tests, ped/vehicle entities and animation job scheduling.
We **did not copy or compile its proprietary code**. The implementation
below is an independent NorthStarCpp design informed by the reference's
architectural separation of request, pre-update, solve and post-update.

## 2. Implemented in this pass

- Public API: `engine/northstarcpp/modules/api/include/northstarcpp/procedural_ik.hpp`
- Runtime: `engine/northstarcpp/modules/animation/procedural_ik.cpp`
- Viewport bridge: `engine/northstarcpp/modules/viewport/viewport_procedural_ik.cpp`
- Scene extraction: `viewport_skeletal_animation.cpp/.hpp`
- Animation/skin path: `viewport.cpp`
- Tests: `tests/procedural_ik_smoke.cpp` and
  `tests/viewport_skeletal_pipeline_smoke.cpp`

### Generic chain setup

Each chain has explicit `root`, `hinge` and `effector` bone names.
No skeleton name is hardcoded by the solver. Configuration validates:
existing bones, joint ancestry, unique chain name, finite numerical limits,
and a maximum of **16 chains per actor**.

```cpp
ProceduralIkController ik(rig);
ik.register_chain({
    "left_hand", "l_shoulder", "l_elbow", "l_wrist",
    1.55f, 1.80f, 0.10f, 0.16f
});

IkTargetRequest goal;
goal.position = {0.2f, 1.1f, 0.4f}; // skeleton-local
goal.pole = {0.35f, 1.0f, 0.2f};
goal.weight = 1.0f;
goal.priority = 20;
ik.submit("left_hand", goal);

// Run after the graph sampled the pose, before weapon-specific grip IK.
ik.evaluate(pose, delta_time, 8);
```

### Request and solve stage

- Multiple requests for one chain resolve by **highest priority**; ties keep
  the first submitted request for the frame.
- Target positions, poles and weights use bounded temporal smoothing and
  fade out when the request is not refreshed.
- Solver scheduling is capped at 8 active chains per frame in the Viewport
  (override via C++ API); inactive chains do not touch the pose.
- `SkeletonPose::pose_two_bone` is reused with the imported joint hierarchy.
- `SkeletonPose::orient_wrist` is reused for optional grounded effector
  orientation; it works with any authored effector bone, not just wrists.
- `IkSolveReport` returns attempts, success, effective weight, and residual.
- Invalid numeric inputs and missing chains return false; they do not terminate
  the engine or modify original YCD samples.

### Ground contact

`submit_ground` accepts an external probe's contact point and surface normal
in skeleton-local coordinates. It clamps foot elevation correction and blends
the effector's up axis toward the ground normal, while preserving horizontal
facing as far as possible. Probes with nonfinite data, invalid deltas or
unsuitable normals are discarded.

**Not implemented:** automatic raycast/sweep submission to Jolt/Bullet,
asynchronous probe completion, pelvis/root height compensation, toe and heel
multi-contact, foot-lock continuity and stance-event detection. The separate
physics provider or animation controller must supply fresh contact results.
The current ground-contact component is a transport for that result, not a
complete terrain-scanning system.

## 3. Scene component contract

Three optional component types are recognized by `SkeletalSceneFrame`.
Each payload is a single whitespace-delimited record. Targets and normal
vectors are **skeleton-local, metres, Y-up**.

```text
engine.model.ik_chain
    left_leg l_thigh l_calf l_foot

engine.model.ik_target
    left_leg 0.15 0.04 0.0 0.18 0.65 0.11 1.0 10

engine.model.ik_ground_contact
    left_leg 0.15 0.00 0.0 0 1 0 0.02 0.20 0.25 1.0 20
```

The bone names above are an **example only**; use names from the actual rig.
A chain payload has `name root hinge effector`. A target has
`name x y z poleX poleY poleZ weight priority`. A ground contact has
`name pointX pointY pointZ normalX normalY normalZ clearance maxRaise maxDrop weight priority`.

The graph/single-YCD pose is sampled first, the generic IK controller applies
its constraints second, and weapon-specific hand grip IK is applied last.
Existing skinning, collision, bone attachment and both GPU render providers
continue to consume the original `SkeletonPose` palette.

The authored IK configuration is cached per entity until it changes. Clearing
the chain components destroys the controller and its old targets.
First-person and third-person characters without these components keep their
existing animation path.

## 4. Verification

Run using the single repository build root:

```powershell
cmake --build out/build --target northstarcpp_procedural_ik_smoke
ctest --test-dir out/build --output-on-failure -R "northstarcpp_procedural_ik|northstarcpp_animation_graph|northstarcpp_viewport_skeletal_pipeline"
```

The following six tests passed together on 2026-11-17:

- `northstarcpp_procedural_ik` — synthetic chain, priority, fade,
  solver budget, ground contact; imported Abby YMT/YCD
- `northstarcpp_animation_graph` — XML, Blend1D, state transition, layers,
  markers and asynchronous YCD
- `northstarcpp_skeletal_animation`
- `northstarcpp_skeletal_asset_interop`
- `northstarcpp_scene_animation`
- `northstarcpp_viewport_skeletal_pipeline` — including the new scene IK
  bridge, stale-definition removal and existing two-hand weapon IK

The new test source was compiled as an individual Ninja object and linked
against the existing built libraries when a new full rebuild was blocked by
an **unrelated** vehicle-service error:
`Module::validate<NorthStarVehiclePowertrainApi>` lacks a matching
`valid_api(NorthStarVehiclePowertrainApi const&)` overload. That
vehicle API contract was not changed as part of IK.
The Player compiled/linked earlier after the IK bridge changes, but a clean
full build of all targets and complete on-screen visual acceptance remain
unverified. No production distribution was replaced, and there was no
commit or push in this pass.

## 5. Performance characteristics and next stages

The new controller keeps chain definitions and reports in capped per-actor
storage; frame requests are resolved without filesystem/physics I/O.
Target arbitration and solver ordering involve at most 16 chains per actor.
`pose_two_bone` still performs multiple full skeleton FK evaluations and
should be profiled before claiming a measurable FPS improvement.
The Scene component string parsing also runs per frame; a future typed ECS
component cache would avoid that overhead.

Next priorities:
1. Jolt/Bullet provider-driven **asynchronous ground probes**, foot plant
   state machine, toe/heel support and pelvis/root compensation.
2. Rig-authored look-at chains for torso/neck/head/eyes with independent
   swing/twist constraints and eye/neck weights.
3. Additive body recoil and vehicle-relative torso/arm reach solvers.
4. IK constraints and timeline diagnostics in the XML Animation Graph
   editor, plus V8 script API for targets and event dispatch.
5. CPU p50/p95/p99 and allocation profiling for 100+ actors, plus
   Vulkan/D3D12 visual acceptance across uneven terrain and weapon gameplay.
