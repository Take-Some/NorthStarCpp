# Jolt Physics — NorthStarCpp

## Current configuration

**Jolt Physics 5.6.0 is the only physics provider built and registered by default.** It implements the replaceable `engine.physics` API, including the existing physics extension interfaces.

| Provider | Runtime module | Build status |
| --- | --- | --- |
| Jolt 5.6.0 | `physics_jolt.dll` | Enabled by default |
| Bullet 3.25 | `physics_bullet.dll` | Disabled by default |

Bullet source code remains in `engine/northstarcpp/modules/physics/bullet/` and the vendor SDK in `sdk/vendor/sources/bullet-3.25/`. They are not compiled in the standard build. Jolt is vendored in `sdk/vendor/sources/JoltPhysics/`.

## Build and verify

```powershell
cmake -S . -B out/editor-ue-parity-qa-20261115 -DNORTHSTARCPP_BUILD_BULLET_PROVIDER=OFF
cmake --build out/editor-ue-parity-qa-20261115 --target northstarcpp_physics_jolt northstarcpp_physics_smoke
ctest --test-dir out/editor-ue-parity-qa-20261115 -R 'northstarcpp_(jolt_selected|jolt_physics|jolt_residency|scene_runtime_jolt|camera_physics_jolt|camera_streaming_physics_jolt|capability_swap)' --output-on-failure
```

`NORTHSTARCPP_BUILD_BULLET_PROVIDER` defaults to `OFF`. To restore the optional Bullet provider in a separate experimental build, pass `-DNORTHSTARCPP_BUILD_BULLET_PROVIDER=ON`. This restores its original target and vendor dependencies; no source files need to be recovered.

**Important:** disabling a CMake target does not delete an old DLL from a reused build directory. The old `physics_bullet.dll` in this development build was moved to `out/disabled-physics-providers/physics_bullet.dll`. Make sure no other stale Bullet DLL is present beside executables or in the configured provider folder.

## Runtime selection

The normal runtime discovers the installed Jolt provider. It is selected by default when it is the only compatible physics module in the search directory. To explicitly require Jolt:

```powershell
$env:NORTHSTARCPP_PHYSICS_PROVIDER = 'jolt'
# Start the editor or Player in this environment.
Remove-Item Env:NORTHSTARCPP_PHYSICS_PROVIDER
```

The Qt Project Manager enumerates installed provider DLLs. Its physics list contains only Jolt when no Bullet DLL is deployed. The game packaging CLI defaults to `--physics jolt`.

The plugin architecture remains generic; compatible external or future builtin providers can still implement `engine.physics`. The historical Bullet-only CTest cases remain in CMake but are registered only with the optional Bullet build enabled.

Original third-party licenses and notices are retained.
