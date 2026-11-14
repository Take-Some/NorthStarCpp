# Jolt Physics in NorthStarCpp

## Two physics providers without rebuilding the engine

NorthStarCpp supports **Bullet Physics 3.25** and **Jolt Physics 5.6.0**. Both implement the `NorthStarPhysicsApi` contract (ABI 1), exposing world creation, simulation stepping, pose/orientation queries, and world destruction.

| Provider | DLL | Role |
| --- | --- | --- |
| Bullet | `physics_bullet.dll` | Default provider during automatic discovery |
| Jolt | `physics_jolt.dll` | Alternative provider |

Both modules are installed as separate DLLs. The Jolt 5.6.0 sources are pinned to tag `v5.6.0` under `sdk/vendor/sources/JoltPhysics/`. Its MIT license is preserved in the SDK.

## Selecting a provider in the native Project Manager

Launch `northstarcpp.cmd`, select **Bullet** or **Jolt** from the **PHYSICS ENGINE** drop-down, then start the editor or scene.

The selection applies when a new process starts. Restart the project to change the provider for an active scene.

## Selecting a provider from the command line

```powershell
# Launch from the NorthStarCpp root without changing or deleting DLLs.
python -B -m tools.dev.launcher --start-editor --physics jolt
python -B -m tools.dev.launcher --start-runtime --physics bullet

# To launch the built executable directly:
$env:NORTHSTARCPP_PHYSICS_PROVIDER = "jolt"
.\out\gcc-debug\northstarcpp_player.exe --scene .\examples\first_scene.nscene

# Restore automatic provider discovery:
Remove-Item Env:NORTHSTARCPP_PHYSICS_PROVIDER
```

All DLLs are found in one directory beside the EXE (or at `plugins.path` from `bootstrap.json`). If `NORTHSTARCPP_PHYSICS_PROVIDER` is set, the host searches for a matching provider name and compatible ABI, ignoring case. If no match is found, it reports the requested provider name as an error. Without the variable, automatic discovery scans the module directory and validates the ABI.

Physics selection does **not** use the older `NORTHSTARCPP_PHYSICS_PLUGIN` environment variable or an `engine.physics` entry in `northstarcpp.modules`; the current implementation ignores both for this capability.

## Build

```powershell
.\northstarcpp.cmd configure
.\northstarcpp.cmd build --target northstarcpp_physics_bullet
.\northstarcpp.cmd build --target northstarcpp_physics_jolt
.\northstarcpp.cmd build --target northstarcpp_physics_smoke
ctest --test-dir out/gcc-debug --output-on-failure -R "northstarcpp_(bullet|jolt)_physics"
```

Jolt builds with GNU GCC / MinGW-w64. The current adapter supports CPU simulation, a fixed 120 Hz timestep, gravity, static and dynamic cubes, plane collisions, continuous collision detection (CCD), and player-body rotation constraints.

Ragdolls, vehicle dynamics, complex mesh shapes, and migration of active simulation state between provider DLLs are not yet exposed through the common ABI 1 contract.

Original author notices, the MIT license, and Jolt's source history are preserved.
