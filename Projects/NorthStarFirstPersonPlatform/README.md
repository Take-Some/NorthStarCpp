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
| V | Switch first-/third-person view with an animated camera move |
| Mouse wheel (third-person) | Smoothly zoom in/out, 1.5–12 m |
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

Run run.cmd for the playable example and verify.cmd for a headless
scene-check plus real runtime initialization. Both scripts use the current
development Player at out/fps-release-qa-20261114/northstarcpp_player.exe,
matching the class-based @northstar/engine SDK and project scripts.
They fail explicitly when the development Player has not been built.
The earlier QA package is frozen and does not include the newer SDK exports.

The QA package includes a tested V8 sidecar development provider; public
release still requires signed V8 security approval, independent clean Windows
acceptance and intellectual-property/licensing review.


## Abby — native skeletal playable character (development build)

The project-local \`scripts/playerConfig.js\` selects the namespaced
\`characters/abby\` bundle (model, skeleton, material, textures and animation).
To launch the current
development Player with the complete original resource chain:

    Projects\NorthStarFirstPersonPlatform\run-abby.cmd

To run a headless eight-frame skinned and textured WARP rendering test:

    Projects\NorthStarFirstPersonPlatform\verify-abby.cmd

The Player loads Abby's 1,033-joint YMT skeleton, weighted YDD meshes,
YCD idle/walk/jump/fall clips, YMAT character materials and YTD textures
through engine.assets. The original FPS/first-person game handles movement,
camera and physics. The skeletal pose is evaluated on CPU and uploaded as
deformed vertices via the optional engine.render character ABI. The current
D3D12 path falls back to D3D11 when the selected provider does not expose
this character extension. Native D3D12 GPU skinning is not yet implemented.

The standalone QA package assembled earlier on 2026-11-14 is immutable and
does not automatically receive these later development changes.


## Default player camera root script

The engine evaluates these VFS scripts for camera-enabled game scenes:

1. `scripts/northstar/bootstrap.js`: shared script SDK primitives.
2. `scripts/playerCamera.js`: default engine-owned camera behavior.
3. `scripts/main.js`: optional project gameplay and look controls.

`sharedAssets/scripts/playerCamera.js` provides **V** for switching between
first-person and third-person, a 0.45-second view transition and mouse-wheel
third-person zoom from 1.5 to 12 metres (0.7 metres per wheel notch).
The view starts in third-person by default, remembering the selected distance
when switching back and forth. Fractional wheel notches are supported.

To customize it, create `Projects/<YourProject>/scripts/playerCamera.js`
and register your own camera event handlers. Its contents replace the
engine script automatically through VFS priority, not in addition to it.
The project may use `NorthStar.camera.createViewController` with different
initial view, toggle key, transition and zoom limits, or use the underlying
`engine.camera`/`engine.input` API. No C++ or DLL rebuild required.

Example project override (replace entire root script):

```js
(() => {
    const view = NorthStar.camera.createViewController({
        initialMode: "first_person",
        toggleKey: 67, // C
        transitionSeconds: 0.3,
        minDistance: 2,
        maxDistance: 9,
        wheelStep: 1
    });
    engine.on("start", () => {
        if (engine.camera.available()) view.start();
    });
    engine.on("update", () => {
        if (engine.camera.available()) view.update();
    });
})();
```

Project source directories and exported FAT/DB archives use the same
virtual path. New/changed project overrides trigger hot reload in
development. Scripts without their own main.js still receive the
engine camera controller when running in a player-camera-enabled game.

The development build resides in
`out/fps-release-qa-20261114/northstarcpp_player.exe`, while previously
frozen QA/production packages remain unchanged.

## First runtime prefab: Pistol

The project references the original native binary NorthStar game asset at
`prefabs/pistol.nprefab` and its OBJ mesh at
`sharedAssets/models/weapon/pistol/pistol.ydd`. It has real ECS identity and a
physics-backed collider. Use `Prefab.instantiate("prefabs/pistol.nprefab",
{position: {x:2, y:0.75, z:2}})` to spawn it in JavaScript.

See `prefabs/README.md` and
`scripts/examples/PistolSpawner.example.js`. This example is not
automatically loaded. Equip/fire/reload behavior is not yet implemented.
