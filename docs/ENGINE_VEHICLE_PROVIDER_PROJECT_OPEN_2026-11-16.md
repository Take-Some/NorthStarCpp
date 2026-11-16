# NorthStarCpp 0.5.0 — engine.vehicle/native project-open fix

**Date:** 2026-11-16  
**Issue:** Project Manager displays "Selected engine provider unavailable: engine.vehicle uses 'native'".

## Root cause
The complete replacement-capable engine.vehicle implementation already existed in `engine/northstarcpp/modules/vehicle/native/native.cpp`, and Project Manager knew how to validate `NorthStarVehicleApi`. However, `engine/northstarcpp/CMakeLists.txt` omitted registration of `vehicle/native` as a built and installed provider. As a result, `vehicle_native.dll` was absent from the engine's plugin directory and the selected provider could not be resolved.

## Code changes
- Register `northstarcpp_vehicle_native` through the existing `northstarcpp_module(... vehicle native ...)` CMake mechanism, preserving the replaceable ABI and CMake install/plugin inventory rules.
- Add `NorthStarVehicleApi` to `modules/host/provider_inspector.cpp`, which was missing engine.vehicle even though the public ABI and Qt provider inventory already included it.
- Make the Qt Project Browser rescan providers immediately before opening a project; newly installed ABI-compatible plugins are recognized without restarting the UI.
- Extend Project Browser smoke: when `vehicle_native.dll` exists, it must appear as validated `engine.vehicle/native`.
- Add a relative-path one-click QA launcher `tools/Start-NorthStar-0.5.0-vehicle-QA.cmd`.

## Build and verification
- Release CMake regenerate and Ninja all: PASS.
- `vehicle_native.dll` built and installed. Registry: version `0.5.0`, 18 registered provider records; external `engine.vehicle/native` points to `bin/vehicle_native.dll`.
- CTest focused: 8/8 PASS (`vehicle_native`, `vehicle_dynamics`, `vehicle_cabin`, `vehicle_rig_xml`, `provider_catalog_sync`, `project_manager_window`, `project_manager_catalog`, `player_provider_inspection`).
- Installed QA package: Qt deployment of 33 DLLs/plugins and four Windows launcher EXEs; plugin registry parser PASS.
- Packaged public and private Project Manager `--smoke` on the Windows GUI backend: PASS (exit 0). `QT_QPA_PLATFORM=offscreen` is not supported in the packaged Qt runtime; use the normal Windows desktop backend.
- Packaged public Editor `--content-dock-smoke` and `--place-actors-smoke`: PASS (exit 0).
- Packaged Player `--check-scene` for `Projects/AbbyPistolDemo/main.nscn` with `NORTHSTARCPP_VEHICLE_PROVIDER=native`: PASS, renderer and materials loaded without error.

## Run
Close the older Project Manager window and launch:

`C:\Users\Kayla\Documents\NorthStar\NorthStarCpp\tools\Start-NorthStar-0.5.0-vehicle-QA.cmd`

or directly:

`C:\Users\Kayla\Documents\NorthStar\NorthStarCpp\out\NorthStarCpp-0.5.0-vehicle-native-fix-QA\bin\northstarcpp_project_manager.exe`

The associated provider resides at `...\bin\vehicle_native.dll`.

## Release limitation
This is a separate working **QA** installation, not a promoted production release. The larger NorthStarCpp 0.5.0 CTest suite had previously reported 29 failures out of 283 across unrelated subsystems. Do not overwrite an existing installed engine or claim that full regression has passed. No Git commit or push was performed.
