# NorthStarCpp 0.6.2 — True First Person Body + Weapon Grip Target

Date: 2026-11-17
Project: Projects/AbbyPistolDemo
Reference: C:\Users\Kayla\Documents\NorthStar\Projects\WeaponLab
Status: local Release QA build; not promoted to production, no Git commit/push.

## Scope
The previous FPS view displaced the original skinned Abby mesh in front of
the camera and rendered an elbows-to-fingers submesh. Although two-bone IK
solved wrist *positions*, hands appeared disconnected from the torso, and
the third-person camera pitch could send arms behind the character.

## Changes
- Added Scene visual presentation mode `first_person_body` to the shipped
  embedded JavaScript API, mirrored sharedAssets API, typings, host V8
  validation, and viewport decoder.
- Abby now reuses her existing authored skeletal mesh in both views, keeps
  its Pawn-following world position and heading, and no longer inherits the
  FPS camera pitch as an entire-body rotation.
- FPS body skinning uses a cached vertex mask excluding only head/face
  influenced surfaces (the rest of the original high-detail YDD is retained).
  The GPU body sections and CPU skin mask have separate caches from the
  legacy arms-only mode. No second character instance or LOD reduction.
- Camera Entity is already authored at y=2.35, SkeletalMesh at y=.70,
  so the eye is 1.65m above the body origin, matching Abby's head
  region. Camera ownership stays independent of the Pawn and renderer.
- Added GripSolveTarget (wrist position + quaternion orientation + elbow
  pole) and SkeletonPose::orient_wrist() to apply a bounded wrist rotation
  *after* shoulder/elbow positional IK without altering segment lengths.
- Wrist frame uses the calibrated WeaponLab pistol grip axes
  (forward, -right, -up). TPS grip uses body yaw and zero camera pitch;
  TPS elbow poles bend ahead of the torso instead of behind it.
- Grip orientation updates compatible imported wrist helper nodes and the
  secondary palette constraints; the earlier unsafe procedural finger-curl
  experiment remains disabled.
- Both authored project and new demo template select first_person_body.
  Existing first_person_arms remains a supported optional visual mode.
- Updated Node and native runtime tests to check the correct FPS/TPS
  visual mode, preserve authored identity, validate physical hand socket,
  camera-relative natural reach and muzzle direction.

## Validation
- CMake Release configure: PASS (MinGW/GCC 16.2, Qt, V8, Jolt).
- Ninja northstarcpp_player + northstarcpp_editor_qt + native viewport
  skeletal pipeline + AssetManager + render_d3d11 + physics_jolt: PASS.
- CTest northstarcpp_viewport_skeletal_pipeline: PASS. Wrist alignment
  with calibrated WeaponLab weapon forward: 0.990-1.000 for tested yaw/pitch.
  TPS checks require elbows and wrists ahead of the respective shoulders.
- Native V8/Jolt/D3D11 Abby runtime (QA-only HUD disabled): PASS.
  45 game objects = 44 authored + one runtime pistol, no second Abby.
  FPS visual logs report head-hidden vertices 50,007 / 89,628.
  The real bone attachment resolves r_hand_prop_attachment, gun +Z
  forward axis=(0,0,1), gameplay frame count=5, clean shutdown.
- Node tests/abby_pistol_demo_smoke.cjs: PASS for FPS/TPS toggles.
- python tools/create_abby_pistol_demo.py --verify: PASS, 56 records
  and 45 author entities.

## Not yet accepted
- A clean Windows GPU image for palm/finger contact and first-person
  downward view of torso/legs is still pending. Automated desktop
  capture has occasionally captured unrelated foreground windows.
- The imported Abby finger/roll chains were previously damaged by a
  procedural curl. That algorithm remains disabled; a safe authored
  hand-pose animation and its skin/helper compatibility need separate QA.
- Full 283+ system-wide regression is not claimed, no production package
  overwritten, no changes committed or pushed.

## Run
C:\Users\Kayla\Documents\NorthStar\NorthStarCpp\tools\Start-Abby-TrueFPS-0.6.2-QA.cmd

QA executables:
C:\Users\Kayla\Documents\NorthStar\NorthStarCpp\out\build-fps-body-062\northstarcpp_player.exe
C:\Users\Kayla\Documents\NorthStar\NorthStarCpp\out\build-fps-body-062\northstarcpp_editor_qt.exe
