# Abby locomotion facing — NorthStarCpp 2026

Date: 2026-11-14
Status: implemented in the current development Player, built and regression-tested.
Previously packaged QA / production dist has NOT been replaced.

## Root cause

The Player's update_abby() forwarded the FPS camera yaw to the character
renderer every frame. The camera and body therefore shared a heading even when
the physics-controlled player was travelling sideways or backwards.

In addition, the YDD model orientation path used a 180-degree extra rotation
and row-vector yaw with a sign inconsistent with the positive FPS movement
convention, causing Abby to look towards the camera rather than in the
direction of travel.

## Implementation

First-class module:
- engine/northstarcpp/modules/api/include/northstarcpp/character_facing.hpp
- engine/northstarcpp/modules/animation/character_facing.cpp

The new CharacterFacing component computes its desired orientation using
atan2(world_horizontal_displacement_x, world_horizontal_displacement_z).
It turns by the shortest angular path, uses frame-time-aware exponential
smoothing with an upper turn-rate bound, and retains its last heading when
there is no meaningful movement. Input to the controller is the resolved
scene movement, not camera yaw, and therefore responds to collision-corrected
physics. Small idle corrections and teleports are ignored. Its reset
heading is read from the initial camera, but further camera orbit does
not rotate the body.

Player update_abby() now owns that facing state, resets it on FPS respawn,
and follows world-space movement for FPS/exploration and scripted movement
when the third-person model is shown. In first-person mode the body stays
hidden while heading can still track travel.

Viewport uses create_rotation_y(-heading), because the native row-vector
math rotation has the opposite sign from FpsArena::movement's yaw convention,
and the authored YDD mesh already points forward along +Z. The previous
unnecessary +pi adjustment has been removed.

The YMT/YCD animation clips, skinned vertex influences, original assets,
camera control API and renderer ABI are unchanged.

## Behavior

- Moving forward/backward/left/right and diagonally makes the body turn
  towards world travel.
- Smooth shortest-path turning on direction reversal, no sudden 360 spins.
- Stopping retains the previous facing, without auto-following the camera.
- Pausing preserves heading.
- First-person camera selection remains independent of animation logic.
- WASD/gamepad remains camera-relative via FpsArena::movement; avatar facing
  uses the actual movement after physics.

## Verification

- New tests/character_facing_smoke.cpp (registered as
  northstarcpp_character_facing).
- Cardinal/diagonal direction changes, idle hold, turn interpolation,
  wraparound +/-pi, pause, low-motion jitter, teleport and invalid inputs.
- Row-vector renderer convention validated: create_rotation_y(-pi/2)
  maps the model's +Z front to +X walking.
- Full current Release target all: PASS (Ninja, two jobs).
- Eight relevant CTests: PASS (character facing, skeletal asset
  interoperability, Abby WARP render, camera director, D3D11 camera,
  exploration Player, FPS arena, Qt editor Player launch).

Full workspace CTest at this stage: 110/115 PASS, with four missing
independent test fixtures and a separate new weapon-YDD skeleton-reference
error in sharedAssets inspection. Neither changes the dedicated Abby/movement
test results, and neither is claimed resolved. General production release
is still blocked by full QA and independent acceptance.

## Run

Close any older instance of NorthStarCpp Player, then from the repo root:

    Projects\NorthStarFirstPersonPlatform\run-abby.cmd

Development Player: out/fps-release-qa-20261114/northstarcpp_player.exe.
A fresh interactive third-person playtest is still needed to confirm
camera feel and visual facing across eight directions.
