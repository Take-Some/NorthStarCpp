# NorthStar First Person Platform

Standalone C++ NorthStarCpp first-person exploration sandbox, powered by the current runtime and provider-neutral APIs.

## Scene

- 60 x 60 metre solid platform made from 36 static 10 m physics tiles (top at Y=0).
- First-person dynamic player with gravity, collision and grounded jumps.
- Three movable rigid-body crates. The remainder of the platform is open.
- Textured skydome sourced from YDD/YMAT/YTD via engine.assets; D3D11 background pass.
- Asset-driven camera tuning (assets/gameplay/camera.json).
- No enemies, weapons, score, mission objectives or required targets.

## Controls

| Input | Action |
| --- | --- |
| WASD / arrows | Move relative to the camera |
| Mouse / right stick | Look around |
| Space / gamepad A | Jump |
| E / gamepad X | Push a nearby dynamic rigid body with a physical impulse |
| Walk into crates | Push objects through actual rigid-body collision |
| Escape / gamepad Start | Pause / restart / exit |

Physics interactions use the versioned provider-neutral PhysicsScene API (Bullet or Jolt).

## Run

Build the current NorthStarCpp player and its plugins, then from the repository root:

~~~powershell
.\out\qa-fps-dist-20261114\NorthStarCpp-0.4.0-windows-x64-Release-QA\bin\northstarcpp_player.exe --explore --scene Projects\NorthStarFirstPersonPlatform\main.nscn
~~~

Or run Projects\NorthStarFirstPersonPlatform\run.cmd.

## Test

~~~powershell
.\out\qa-fps-dist-20261114\NorthStarCpp-0.4.0-windows-x64-Release-QA\bin\northstarcpp_player.exe --warp --check-scene --explore --scene Projects\NorthStarFirstPersonPlatform\main.nscn
ctest --test-dir out\fps-release-qa-20261114 -R "northstarcpp_exploration|northstarcpp_.*physics" --output-on-failure
~~~

## Packaging

The game exporter recognizes game_mode=explore and passes --explore to the packaged native player; its asset folder and scene are included in the package.

## Limitations

The platform is made from rigid boxes, not imported terrain. Both physics providers retain an implicit infinite Y=0 floor, so leaving the visual platform does not produce a bottomless fall. The test verifies startup/rendering but not subjective input feel or performance over thousands of frames.


## Project-owned gameplay

Movement, first-person look, controller mappings, jump, push interaction,
pause and exit are authored in `scripts/main.js`, not in the NorthStarCpp
FPS arena implementation. The engine only exposes input, camera, physics,
ECS and asset APIs. Gameplay changes do not require a C++ rebuild.

The verified QA package includes the tested V8 sidecar development provider.
A public release requires the independently approved native V8 build and
clean Windows machine validation.


## Verified QA launcher (2026-11-14)

Run run.cmd for the playable first-person example and verify.cmd
for a headless scene-check plus real runtime initialization. Both scripts
select the verified QA Player in
out/qa-fps-dist-20261114/NorthStarCpp-0.4.0-windows-x64-Release-QA/bin.
They fail explicitly rather than silently using an unverified build.

The QA package includes a tested V8 sidecar development provider; public
release still requires signed V8 security approval, independent clean Windows
acceptance and intellectual-property/licensing review.
