# Abby motion-derived gait speeds — NorthStarCpp (2026-11-18)

## Scope and provenance

Read-only measurement from imported The Last of Us Part II Remastered Abby:
- `engine/northstarcpp/resources/sharedAssets/characters/abby/animations/mm-explore.ycd`
  (NEF8 raw-DEFLATE, 259 clips; source name `anim-abby-player-mm-explore.pak`)
- `engine/northstarcpp/resources/sharedAssets/characters/abby/models/abby.ymt`
  (1033-joint hierarchy; coordinates in model metres)
- sampled at the animation source rate **30 Hz**.

These clips are **in-place**, not authored trajectory/root-motion captures:
the start/end horizontal displacement of root joint 0 is 0 metres in the
walk and sprint loops and less than a millimetre for run. Thus zero root
displacement must **not** be mistaken for zero gameplay movement, and the
numbers below are **foot-contact-based inferred animation travel speeds,
NOT a runtime readout of the original Naughty Dog game controller**.

## Method

1. Inflate the native NEF8/YCD clip dictionary using raw DEFLATE.
2. Decode per-frame joint translations and quaternions and reconstruct world
   poses through the imported YMT parent hierarchy.
3. Sample r/l heels, balls, toes and ankles at 30 Hz. Source foot-ground
   planes agree to within centimetres of model-space Y=0.
4. Mark candidate stance intervals when heel/ball/toe height remains within
   0.06 m of its clip minimum. Measure median horizontal foot velocity in
   these intervals; backward planted-foot velocity is the hypothetical
   actor's forward traversal speed required to prevent sliding.
5. Compare left/right heel, ball and toe medians, avoid toe-roll outliers,
   round to 0.01 m/s. No optical-flow/game-world benchmark was performed.

| YCD clip | Clip length | Right heel stance | Left heel stance | Foot-based target |
|---|---:|---:|---:|---:|
| abby-mm-explore-walk-loop-fw | 3.500 s | 1.487 m/s | 1.478 m/s | **1.48 m/s** |
| abby-mm-explore-run-loop-fw | 2.933 s | 2.987 m/s | 2.773 m/s | **2.95 m/s** (toe medians: 3.031 / 2.996; ball medians: 2.941 / 2.838) |
| abby-mm-explore-sprint-loop-fw | 4.167 s | 4.428 m/s | 4.437 m/s | **4.44 m/s** |

Converted speeds: walk 5.33 km/h, run 10.62 km/h, sprint 15.98 km/h.
These estimates are bound to these **specific clips at playbackRate=1.0**.
Different animation dictionaries, characters and locomotion states require
their own foot-motion measurement or animation speed warping.

Reproduction:
```text
python tools/analysis/measure_abby_clips.py
python tools/analysis/estimate_abby_gait.py
```

## Integration

- Blueprint Motor defaults: run/ordinary `walk_speed=2.95`, Shift sprint
  `run_speed=4.44`; independent jump, gravity, air control, acceleration
  unchanged. These field names follow the existing two-speed physics ABI.
- Generic V8 Player movement: Ctrl+WASD multiplies ordinary travel by
  `1.48/2.95`; full WASD = 2.95, Shift+WASD = 4.44. Gamepad analogue
  magnitude is preserved; Ctrl wins when pressed with Shift.
- Native Jolt fallback follows the same 2.95/4.44 run/sprint values.
- Generic project-neutral CharacterController now selects optional running
  and sprinting clips using the same physical thresholds and gait hysteresis;
  rigs without those clips retain their existing walking animation.
- AbbyPistolDemo `character.locomotion` names the three physically measured
  targets and midpoint thresholds (2.215 / 3.695); the animation playback
  rate adjusts by `measuredHorizontalSpeed/referenceSpeed` within
  [0.65, 1.5]. Existing native animation-per-entity override remains active.
- Character footstep gait classification thresholds use 2.215 / 3.695;
  authored step distances remain as before pending actual footfall-event QA.
- Existing active CharacterMotor scene components migrated: AbbyPistolDemo,
  NorthStarFirstPersonPlatform, KaylaNightCity, MovingCitiesPrototype.
  Only the two speed floats changed; every other NEF8 record and component
  payload round-tripped without differences. Original .nscn files backed up
  alongside the scenes with `.before-abby-gait.bak` suffix. Historic backups
  and migration scenes were intentionally left untouched.
- Engine interfaces remain project-configurable; this is a new human
  baseline, **not a universal hardcoded movement law** for animals, vehicles,
  robots or custom character archetypes.

## Validation (2026-11-18)

- `python tools/analysis/apply_abby_locomotion_speeds.py --verify`:
  **PASS** all four active scenes (NEF8 integrity and component equality).
- `node tests/abby_measured_movement_controls_smoke.cjs`:
  **PASS**, 1.48 / 2.95 / 4.44 input-to-motor target mapping.
- `node tests/abby_locomotion_sync_smoke.cjs`:
  **PASS** both demo and generator template, walk/run/sprint/idle switching.
- `node tests/abby_pistol_demo_smoke.cjs`: **PASS** FPS/TPS and weapon.
- `node tests/character_three_gait_smoke.cjs`: **PASS** shared CharacterController,
  three authored gaits and backwards-compatible walk-only rigs.
- `ninja -C out/build engine/northstarcpp/CMakeFiles/northstarcpp_physics_jolt.dir/modules/physics/jolt/jolt.cpp.obj`: **PASS**.
- `cmake --build out/build --target northstarcpp_viewport --parallel 4`:
  **PASS** (native viewport static library); engine API scripts embedded.

Still required for end-to-end acceptance: clean Player/Editor link and
GPU/physics runtime test, real in-scene p50/p95 frame-time and per-gait
displacement over a marked distance, foot sliding metric in both D3D12 and
Vulkan. The original game's PlayerController speed constants have not been
read or independently measured from live gameplay.
