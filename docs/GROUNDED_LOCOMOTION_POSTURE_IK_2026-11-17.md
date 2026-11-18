# NorthStarCpp — Grounded Locomotion / Look-at / Vehicle IK
Date: 2026-11-17

## Runtime architecture

The existing YMT/YCD animation and XML Animation Graph are unchanged.
The optional post-animation chain is:

1. SkeletalClip / Animation Graph pose evaluation
2. Grounded Locomotion (two heel/toe probes per foot, world-space foot
   anchors, stance/release gating, pelvis height compensation, slope tilt)
3. Priority-aware Procedural IK for feet and seat-relative reaches
4. Authored distributed Look-at across torso, neck, head and eyes
5. Existing two-handed weapon grip IK, sockets and GPU skinning

The source modules are:
- `modules/api/include/northstarcpp/grounded_locomotion.hpp`
- `modules/api/include/northstarcpp/posture_ik.hpp`
- `modules/viewport/viewport_grounded_locomotion.cpp`
- `modules/viewport/viewport_posture_ik.cpp`
- `modules/animation/posture_ik.cpp`
- `modules/viewport/viewport_procedural_ik.cpp`
- `modules/host/physics.cpp` (deferred query queue)
- `modules/animation/skeleton.cpp` (bounded joint translation/rotation)

### Physics queue

`PhysicsScene::enqueue_ground_probe` creates a ticket and queues a ray.
The mutable physics world is only queried after `PhysicsScene::step()`;
a later animation frame uses `consume_ground_probe`. Jolt channel raycasts
are preferred, with a compatible sphere-sweep fallback. Bounded capacities:
96 outstanding per physics tick, 192 completed responses. The queue is
thread-safe; it does **not** launch a Jolt worker concurrently with simulation.
This is a one-or-more-tick deferred pipeline, not a background raycast API.

### Authored scene components (whitespace-separated payloads)

```text
engine.model.ik_chain:
  left_leg l_thigh l_calf l_foot
engine.model.grounded_foot:
  left_leg l_foot 0 -0.04 -0.11 0 -0.04 0.16
engine.model.grounded_body:
  pelvis torso
engine.model.look_joint:
  neck 0.4 0.12 0 0 1
engine.model.look_joint:
  head 0.7 0.15 0 0 1
engine.model.look_target:
  2.5 1.8 3.0 1
engine.model.vehicle_seat_pose:
  12.0 0.8 4.0 0.5 12345
engine.model.vehicle_reach:
  right_hand r_elbow 0.24 0.38 0.50 1 50
```

Foot config: `chain ankle heelX heelY heelZ toeX toeY toeZ` (ankle
bone-local metres). Body: `pelvis torso`; `-` disables either bone.
Look joint: `bone influence maxRadians forwardX forwardY forwardZ`;
target: `worldX worldY worldZ weight`. Seat pose:
`worldX worldY worldZ yawRadians occupantEntityId` (same data shape as
`VehicleService::seat`); reach:
`chain elbow seatLocalX seatLocalY seatLocalZ weight priority`.

Vehicle seat ID must match the character entity ID. The Player now
refreshes matching seat snapshots automatically from VehicleService after
engine.vehicle.occupy()/release() and supports explicit
engine.model.vehicle_seat_binding for pre-occupied NPC seats. This is
not yet a full enter/exit montage or steering-wheel animation controller. Chains must be explicitly authored;
missing bones are rejected, not replaced by character-specific hardcoding.

## Verification

`northstarcpp_player`, `northstarcpp_viewport` and Qt Viewport static
target compiled/linked. Seven targeted CTest cases passed:
`northstarcpp_grounded_locomotion`, `northstarcpp_procedural_ik`,
`northstarcpp_animation_graph`, `northstarcpp_skeletal_animation`,
`northstarcpp_skeletal_asset_interop`, `northstarcpp_scene_animation`,
`northstarcpp_viewport_skeletal_pipeline`.

Grounded QA includes real physics ground-plane hit after a physics tick,
heel/toe contact, foot planting, anchor persistence, release on lost contact,
pelvis bounds, authored look-at and vehicle-seat occupant gating.

## Limitations

This is not yet complete gameplay visual QA: no live humanoid scene
recorded on rough terrain or moving platform, no automatic Character Motor
consumption of extracted Root Motion, no end-to-end vehicle boarding and
exit montages, and no IK-specific Vulkan/D3D12 screenshot comparison.
General Vulkan/D3D12 GPU renderer parity tests passed; they are not proof
of animated-pose visual correctness.
A full Qt Editor EXE and production distribution were not replaced.
Changes are local; no commit/push.

## Production hardening pass — 17 November 2026

### Moving support bodies

`PhysicsScene::body_transform(body_index)` provides the current collision
body world transform. A planted foot hit records the support body index and
the anchor point in the body's local space. Every animation frame
reconstructs the world anchor using that body's latest transform, including
rotating platforms. A missing body, support teleport larger than 1 m/frame
or animated foot teleport larger than 2 m invalidates the planted state.
Contact results that have not refreshed in 0.35 s are discarded; no stale
ground lock may survive indefinitely. Per-foot diagnostics now expose
`anchor_world` and `support_body`.

### Animation Graph foot phases and toes

A graph may declare clip-relative events (in XML `<Events>`):

```xml
<Notify name="ik.foot.plant:left_leg" ref="animations/walk.ycd@walk" at="0.08"/>
<Notify name="ik.foot.lift:left_leg" ref="animations/walk.ycd@walk" at="0.63"/>
```

The authored `left_leg` is a registered IK chain name, not a hardcoded bone.
The stance switch is received by the already-instantiated Grounded IK
controller and takes effect on the next evaluation (up to one animation-frame
latency). When no phase events are provided, the controller retains velocity-
based stance detection.

The ninth optional `engine.model.grounded_foot` payload field names a
metatarsal/toe joint descended from the ankle (maximum six ancestry hops).
When both contact probes are valid, `finish_toes()` applies a bounded
post-leg-IK toe rotation (up to 0.16 rad) before sockets and GPU skinning.

```text
engine.model.grounded_foot
left_leg l_foot 0 -0.04 -0.11 0 -0.04 0.16 l_toe
```

### Physics-owned world motion / Root Motion

An opt-in policy prevents a clip's root translation from visually moving
a skeleton twice on top of the Player's physics capsule:

```text
engine.model.root_motion_policy
capsule root
```

`SkeletonPose::consume_visual_root_motion()` restores the authored root
bind translation and returns the suppressed local displacement for a
future motor handoff. This currently implements **physics-authoritative
in-place presentation**, not physical consumption of root motion.
Full root-motion-to-Character-Motor displacement with collision sweeps,
rejection and network reconciliation remains a release blocker.

The pelvis correction remains a bounded visual-pose change; it is not
applied as a second physical body translation.

### VehicleService-driven seat binding

After a successful V8 `engine.vehicle.occupy(vehicle,seat,entity)`, the
Player records the binding and refreshes the actual
`VehicleService::seat()` snapshot on each rendered frame.
`release`, `detach` and script reset remove the binding and
stale seat IK pose, allowing existing reach goals to fade out.
Native-Jolt vehicle seats reuse the same wheel/seat physics transform
already used by `engine.vehicle.seatState()`, without duplicated math.

Projects may also author an explicit pre-bound seat for an NPC:

```text
engine.model.vehicle_seat_binding
vehicleSceneIndex seatOrdinal

engine.model.vehicle_reach
right_hand r_elbow 0.24 0.38 0.50 1 50
```

Every frame refreshes or deletes the generated
`engine.model.vehicle_seat_pose` component. Only a seat whose authoritative
`occupant` equals the entity ID produces IK goals. The binding does not
yet select or play an enter/exit montage, retarget the body to a seat,
or author a dynamic steering-wheel attachment.

### Performance / provider QA

A CPU-only synthetic 128-actor workload with two grounded feet, toe
articulation and pelvis correction, over 90 sequential animation frames,
produced one local run of `p50=0.7795 ms`,
`p95=0.7845 ms`, `p99=0.796 ms` per 128-actor batch.
This is not a portable FPS claim, includes no simultaneous GPU submission
or Jolt terrain raycasts, and is not a night-city gameplay benchmark.

The existing hardware tests
`northstarcpp_renderer_gbuffer_parity_hardware` and
`northstarcpp_renderer_parity_fixture_hardware` **passed**. The six
non-WeaponLab targeted animation tests, as well as
`northstarcpp_vehicle_cabin`, `northstarcpp_vehicle_dynamics`,
and `northstarcpp_player_blueprint_physics`, passed when tested.
A separate WeaponLab/Viewport regression was reproduced:
at `yaw=-1.0`, `pitch=-0.55`, elbow collision penetration was
approximately `0.041 m` versus an acceptance threshold
`< 0.035 m`. Do **not** relax the threshold or claim the full
skeletal pipeline has passed until this unrelated grip/limb collision
issue is resolved.

The final Player source compiled, but Windows prevented replacing the
already-in-use production exe (`Permission denied`). A separately
linked QA executable, `out/build/northstarcpp_grounded_qa.exe`,
was produced successfully without modifying the running application.
Linker warnings regarding duplicate `boost::any` sections still require
a clean build check. Full Qt Editor EXE and production dist were not
replaced; no commit or push was requested or performed.
