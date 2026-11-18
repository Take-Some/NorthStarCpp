# Sea of Rust — Moving Cities Prototype

The Sea of Rust vertical slice uses project-owned NEF8 YDR/YTD assets, native
engine.vehicle, Jolt Physics, V8 gameplay and an infinite-style seeded
procedural wasteland. Bastion and Rust Wren are two **real moving dynamic
physics bodies**, not scenery being scrolled past a stationary camera.

## Stable passenger movement and long journeys (2026-11-18)

Jolt is the single authoritative source of CityRoot and Pawn transforms.
The Pawn rides a dynamic movement base instead of trying to catch up with
the world-space vehicle. The Jolt controller retains the same CityRoot for
up to 150 ms across harmless contact fluctuations; its inherited velocity
is never double-added on reacquisition. For a standing passenger, small
contact-solver X/Z slip is corrected against the **same fixed-step vehicle
displacement** (not a delayed camera target). Jumping detaches the movement
base while preserving horizontal takeoff inertia. Walking uses relative
character-motor input, and free dynamic cargo uses normal Jolt contacts.

The Sea of Rust project script no longer injects periodic camera shaking.
After each Play-mode Jolt tick, the host performs a zero-time vehicle sync
before deriving the camera and collecting the visual scene, so the YDR city
meshes, animated treads and player share a single frame's pose. This is a
compatibility bridge until engine.vehicle receives a dedicated postPhysics
script event.

The existing SceneWorld authored-cell streamer is bypassed **only while
running this project's CityRoot**, because it would evict parts according
to their old spawn-cell positions. The independent 64 m TerrainAsyncStreamer
continues loading and unloading render meshes and Jolt collision around the
live physical city. The future generic solution is mobile-entity streaming
residency keyed by dynamic world bounds, not a stationary authored cell.

### Verified tests

- Actual project V8+Jolt 30 second drive: Bastion 255.76 m; maximum
  Pawn-to-carrier displacement error **0.006 m**; 81 terrain chunks.
- Actual project V8+Jolt 120 second pursuit: maximum relative error
  **0.009 m**, ready with zero warnings/failures. At about 35 seconds
  the pursuit logic deliberately brakes near Rust Wren; this is not a
  terrain-streaming error.
- Separate **120 second continuously powered 1,554.81 m Jolt drive**,
  with real streamed terrain: peak passenger-local drift **0.0138 m**;
  the Pawn and both dynamic cargo props remain aboard.
- Native Jolt moving-base jump acceptance passes: the Pawn detaches
  from the moving city and retains takeoff momentum.
- D3D12 1.6.0 `--check-scene` passed all 18 GPU startup checks,
  no errors or warnings.
- `python tools/create_moving_cities_prototype.py --verify`,
  `python tests/moving_cities_terrain_smoke.py`, and
  `python tests/rustsea_world_drive_smoke.py` pass.

The project launcher prefers `out/build/northstarcpp_player_carrier_sync.exe`
and preserves previous Player fallbacks. The GPU draw path was checked, but
interactive screenshot/perceptual frame pacing and Vulkan parity still need
visual acceptance. The player-to-vehicle coupling is currently evaluated
for the possessed Pawn; general multiple-passenger attachment/seat ownership
is a future API expansion.

## Physical moving cities

| City | Jolt rigidbody mass | Wheels | Physics |
|---|---:|---:|---|
| Bastion | 25,000,000 kg (25,000 tonnes) | 14 | Jolt dynamic body with deck/hull compound |
| Rust Wren | 4,500,000 kg (4,500 tonnes) | 8 | Jolt dynamic body with deck/hull compound |

Both project rigs live in `assets/vehicles/*.nvehicle` and use
`contact_mode="physics"`. The underlying replaceable `engine.vehicle`
API continues providing throttle, braking, waypoint movement and wheel
telemetry. Ordinary vehicles continue using Jolt's `VehicleConstraint`.

### Collision fix

The original `CITYROOT` was a non-colliding 0.25 m placeholder. Independent
colliding deck cubes were being repositioned outside the dynamic vehicle
body, causing missing support contacts, self-collisions and huge upward
impulses. Now each city owns a single **mass-bearing Jolt rigidbody**
with a compound deck and lower-hull collision shape. The Jolt compound
center-of-mass translation is corrected explicitly; a bounds assertion
prevents reintroducing a deck below the terrain surface.

The separate project-authored `BASTION` scene parts are **visual-only**,
including deck tiles; they no longer self-collide with the physical
chassis. The rigidbody deck supports the possessed Pawn and free dynamic
objects through native Jolt contacts. Exterior industrial superstructures
are currently visual-only and need separately authored convex collision
subshapes if they must block other objects.

### Heavy-track solver

Jolt's standard wheel-based `VehicleConstraint` was unstable at 25 million
kg on triangle-mesh heightfield terrain, producing first-step vertical
velocities near +500 m/s. For **massive tracked city rigs only**, Jolt now
steps a specialized multi-wheel tracked-contact solver at its fixed physics
step (120 Hz):

- Cast each wheel against actual streamed collision meshes.
- Apply bounded, mass-scaled spring/damper support forces to the dynamic body.
- Apply traction-limited forward propulsion, rolling/lateral resistance,
  and velocity-opposing braking forces.
- Keep gravity and physical mass/inertia active; no teleport-based city motion.
- Give Pawn motor the support velocity of the dynamic moving deck, rather
  than resetting its horizontal speed to zero.

The currently authored city visuals and static-local geometry do not support
rigid-body rotations; **large cities currently permit translation only**.
Full physical pitch, roll, differential yaw and compound collision rotation
remain separate work. Standard-sized road vehicles retain their own Jolt
6-DOF physics.

## Wasteland streaming

- Seed: `20261117`
- Chunk width: 64 m
- Resident radius: 4 chunks (9 x 9 = 81 resident)
- Base terrain elevation: **0.0 m**
- Procedural vertical scale: 5 m with continuous low-relief noise
- Render LOD: 0–3; collision mesh: 16 quads per chunk
- Terrain generation: initial 81 chunks prepared synchronously, subsequent
  mesh batches generated on a background worker.
- Jolt streamed collision creation and commit remain on the simulation
  owner thread; latency/FPS are not yet independently benchmarked.
- Terrain is stationary in world coordinates and regenerates identically
  on return to the same chunk coordinates.

Configuration is project-owned:
`assets/streaming/procedural_terrain.json`.

## Native project assets

- `assets/models/rustsea/`: project-owned NEF8 YDR models.
- `assets/textures/rustsea/`: project-owned NEF8 YTD dictionaries.
- `assets/materials/rustsea.ymat.xml`: authoritative XML YMAT material source.
- `assets/scripts/main.js`: V8 city pursuit, mission HUD, tread animation.
- `assets/vehicles/`: Jolt/native vehicle profiles for both cities.
- `assets/ui/chase_hud.nui`: in-game HUD.

Project and template source of truth:
`tools/create_moving_cities_prototype.py` and
`tools/templates/moving_cities/`. The game scene `main.nscn` is binary
NEF8; project assets are not placed in the shared engine asset directory.

## Launch

```bat
cd C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
Projects\MovingCitiesPrototype\run.cmd
```

The game launcher prefers the independently linked, tested
`out/build/northstarcpp_player_cityphysics.exe`, then falls back to
the earlier streaming and standard Player executables. A provided
`NORTHSTARCPP_PLAYER_EXE` override takes precedence.

Both cities engage drive at project start. The player can use I/K for
throttle/braking and E at the helm for mission interactions.

## Verified regression tests (2026-11-18)

- `python tools/create_moving_cities_prototype.py --verify`: PASS
- `python tests/moving_cities_terrain_smoke.py`: PASS (347 entities,
  354 records; CityRoot collider enabled, independent visual deck disabled)
- `python tests/rustsea_world_drive_smoke.py`: PASS
- `out/build/northstarcpp_physics_smoke.exe --city-native-drive`: PASS,
  Bastion drives ~41.95 m in 8s and physically carries a Pawn plus two
  independent Jolt dynamic cargo crates.
- `out/build/northstarcpp_physics_smoke.exe --city-native-terrain`: PASS,
  with real streamed terrain, bounded heave and the same Pawn/cargo behavior.
- `out/build/northstarcpp_physics_smoke.exe --city-carrier`: PASS
- `out/build/northstarcpp_physics_smoke.exe --streamed-terrain`: PASS
- `NORTHSTARCPP_RUSTSEA_DRIVE_QA=1` with the cityphysics Player's
  `--runtime-init-smoke --explore`: PASS, 8 seconds in the real project;
  Bastion moved 41.796 m, Pawn moved 41.989 m, Rust Wren moved 47.030 m;
  READY, 12 passed, zero startup warnings/errors.
- `northstarcpp_player_cityphysics.exe --check-scene` on D3D12:
  PASS, READY, 18 passed, zero errors.

The **interactive screenshot and FPS acceptance are still outstanding**;
the tests prove physics/scripting/GPU initialization, not finished artistic
quality. Floating Origin is implemented only as coordinate-conversion
foundation and has not been activated across the complete runtime.

See `docs/CITY_COMPOUND_COLLISION_DRIVE_2026-11-18.md` for a detailed
diagnosis and scope of changes.
