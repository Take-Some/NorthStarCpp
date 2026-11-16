# NorthStar C++ — Vehicle subsystem / Jolt integration

Updated 2026-11-15. Current scope: isolated KaylaNightCity project and native engine implementation.

## Current acceptance status

| Capability | Status |
| --- | --- |
| Native replaceable `engine.vehicle` plugin, stable ABI v1 and Dynamics ABI v2 | Implemented and regression-tested |
| Optional Cabin ABI v3: door opening/closing states, time-based motor, driving lockout | Implemented; source-game door meshes not yet imported |
| 8 independently configured MT28 wheel positions; springs/dampers, grip, ABS/TCS | Implemented and tested |
| Physical six-DOF Jolt `VehicleConstraint` and dynamic rigid chassis | Implemented and tested with road-proxy contacts |
| Body pitch/yaw/roll, wheel suspension/rpm/steering telemetry | Integrated into scene and V8 |
| Jolt impulse transfer to another dynamic rigid body | Tested |
| Safe native vehicle dynamic/static lifecycle with stable scene index | Tested |
| Sector collision residency protection around moving vehicles | Implemented, requires stress test on partitioned city |
| XML wheel visual bindings and per-frame transforms | Implemented; eight debug wheel nodes, NOT original meshes |
| Seat occupancy and physical world-space seat sockets | Implemented as reservation/socket calculation; real boarding NOT implemented |
| Basic autonomous waypoint following and forward obstacle braking | Implemented; no lane graph, traffic laws or detour planner |
| Real Highway 101 geometry, collisions, texture/material bindings | NOT imported into native renderer/physics |
| Rendered original MT28 wheel meshes, interior and doors | NOT converted to compatible native YDR |
| Player capsule attach/detach, animated entry/exit, camera handoff and NPC boarding | NOT implemented |
| Native timed door hinge/interlocks | Implemented via Cabin ABI v3; debug scene nodes only |
| Complete 3-km drive, full Night City streaming, GPU frames and p95/p99 profiling | NOT verified |

## Code and plugin contracts

- `engine/northstarcpp/modules/api/include/northstarcpp/vehicle_plugin.h` — stable API v1
- `engine/northstarcpp/modules/api/include/northstarcpp/vehicle_dynamics_plugin.h` — optional wheel/seat/tyre API v2
- `engine/northstarcpp/modules/api/include/northstarcpp/vehicle_cabin_plugin.h` — optional ABI v3 for native door hinge motors and safety interlock
- `engine/northstarcpp/modules/api/include/northstarcpp/physics_vehicle_plugin.h` — optional `engine.physics` ABI (0x50485601), Jolt dynamic vehicle adapter
- `engine/northstarcpp/modules/physics/jolt/jolt_vehicle_methods.inc` — actual `JPH::VehicleConstraint`, `JPH::WheeledVehicleController`, raycast wheel contact, physics world ownership
- `engine/northstarcpp/modules/host/physics.cpp` — provider-neutral `PhysicsScene` wrapper and streamed collision residency
- `engine/northstarcpp/modules/vehicle/native/native.cpp` — vehicle plugin dynamics and seat state, separate world per session
- `engine/northstarcpp/modules/host/vehicle_rig_xml.cpp` — strict `.nvehicle` XML parser with `visual` per-wheel scene bindings
- `engine/northstarcpp/modules/editor/window_vehicle_runtime.inc` — V8/native provider dispatch, Jolt source-of-truth motion, waypoint/obstacle controls, world socket and debug wheel visual transforms

Jolt's 6DoF provider is optional. Other physics providers do not claim full vehicle physics; the older `engine.vehicle` kinematic fallback remains available, but a project with `contact_mode="physics"` requires a provider that explicitly supports the Jolt vehicle extension. The original engine.physics ABI 4 is unchanged.

## Project MT28

- `Projects/KaylaNightCity/vehicles/mahir_mt28.nvehicle`: authorable 8-wheel / 21-seat prototype
- `Projects/KaylaNightCity/main.nscn`: canonical NEF8, 116 scene entities, 1 authored MT28 chassis collider
- `Projects/KaylaNightCity/migration/native_bootstrap.scene.v3`: editable scene source; 104 *synthetic* road collider boxes
- `Projects/KaylaNightCity/migration/...`: original project data plus pre-Jolt and pre-wheel-visual backups
- `Projects/KaylaNightCity/scripts/main.js`: rig attach, door open/close demonstration at a stop, waypoint activation only after closing
- Two additional `MT28 Door Debug Left/Right` scene objects are bound to native door motor angles, not original YDR meshes.

The dedicated `PrologueRoadDebug` lane remains a **test collision corridor**, with flat collision boxes whose top is approximately world Y=33.55. It is not the original Highway 101 mesh. The Jolt MT28 now has a lowered centre of mass and anti-roll bars per axle. The prototype uses an authored approximate 2.48 × 3.42 × 9.8 m chassis box and **real Jolt wheel contact mode**, not the previous `flat` fallback. Wheel placements reflect interpreted original scene positions, but bus mass, seat positions, suspension and drive parameters remain estimated values requiring visual/physical calibration.

The eight additional scene objects `MT28 Wheel Debug 0..7` are individually transform-driven **diagnostic wheel proxies**. Their positions, steering and spin are calculated from the Jolt chassis basis and each wheel state. Once the engine imports the original MT28 wheel/tire YDR resources, author the same `visual="scene-node-name"` attributes to bind converted nodes without further vehicle physics changes. No assertion of original-game visual parity is made.

## Runtime examples

```js
engine.vehicle.attach(busIndex, 6.2, 18);
engine.vehicle.configureRig(busIndex, "vehicles/mahir_mt28.nvehicle");
engine.vehicle.waypoint(busIndex, 847.5, 2250, 6, 2.5);
const motion = engine.vehicle.motion(busIndex);
const wheel = engine.vehicle.wheelState(busIndex, 0);
const wheelPivot = engine.vehicle.wheelPose(busIndex, 0);
const seat = engine.vehicle.seatState(busIndex, 0);
engine.vehicle.occupy(busIndex, 0, occupantSceneEntityId);
engine.vehicle.release(busIndex, 0, occupantSceneEntityId);
engine.vehicle.setDoor(busIndex, 0, 1); // open at rest
const door = engine.vehicle.doorState(busIndex, 0);
engine.vehicle.setDoor(busIndex, 0, 0); // close before driving
engine.vehicle.detach(busIndex);
```

Seat occupancy tracks stable live entities and refuses multiple occupation; world positions follow the full physical chassis orientation. Actual physically attached passengers and cameras are **not** yet supported. `wheelPose` exposes world position, heading, rotation and suspension state to gameplay/renderer consumers.

Automatic waypoint motion currently uses conservative heading control and a swept-sphere forward obstacle sensor to reduce speed and brake before obstacles. It does **not** change lanes, navigate a road graph, respect signals, or search detours.

## QA evidence

Built in isolated Release QA directory `C:\Users\Kayla\Documents\NorthStar\_qa_character_fix`:
- `northstarcpp_vehicle_native`: v1 compatibility, deterministic controls/braking
- `northstarcpp_vehicle_dynamics`: 8 wheels, 21 seats, tyre dynamics, ABS/TCS and validation
- `northstarcpp_vehicle_cabin`: optional Cabin ABI v3, two powered doors, time animation, opening lockout during driving
- `northstarcpp_vehicle_rig_xml`: strict XML and authored MT28 rig including wheel/door binding names
- `northstarcpp_jolt_vehicle_abi`: Jolt-native constraint, driving/turning/raycast suspension and brakes
- `northstarcpp_native_vehicle_scene`: actual new project's NSCN and Jolt; 8-wheel contact for 500 simulation frames, 87.21 m driving in 8.33 s, 14.11 m/s peak, dynamic obstacle collision transfer and static-body restoration

All six targeted CTests have passed. The scene/rig were rebuilt into `main.nscn`, and `northstarcpp_player --runtime-init-smoke` returned READY, 116 scene objects, 11/11 startup checks and 0 warnings/failures. These are isolated smoke/integration results, **not** proof of shipped-game drive quality or real-road fidelity.

## Remaining implementation gates

1. Decode/convert original NewViso `.ydr`/`.ytd` assets to active NorthStar native codecs. Import actual model skeleton, wheel meshes, door geometry and verified interior/seat sockets.
2. Translate original Night City road polygons/collision sectors to native material + PhysicsScene triangle meshes. Test slopes, road friction, sector edges and fast travel while maintaining wheel collision residency.
3. Implement authentic character boarding, animated entry/exit, player/driver input handoff, safe passenger attachment and camera transitions. Link seats to model attachment sockets.
4. Bind original door meshes and authoritative hinge sockets, door collision/lock interlocks, and boarding animations to native timed Cabin ABI v3; two debug hinges currently work.
5. Implement lane-level traffic graph, signals, speed limits, obstacle predictions and route replanning; validate avoiding collisions without unsafe crossings.
6. Visual frame-by-frame QA of moving wheels, suspension, body pitch/roll; driving stability at 30/60/120 FPS and full 3-km original route. Measure frame p50/p95/p99 and memory growth over 1000+ frames.

No distribution DLL replacement, Git commit or push performed.
