# NorthStarCpp — Recoverable Asset Source Failures

Date: 2026-11-14
Status: corrected in local sources and checked in isolated Release QA. Production installation unchanged.

## Runtime contract

Invalid, missing and unsupported *individual* source assets must not terminate the game, stop all scene rendering or open a modal error dialog.
Validate at the resource boundary; warn with source path and cause; skip the specific resource; continue all other simulation and rendering; quarantine repeated failures to avoid warnings or decompression on every frame.
Never silently fabricate a mesh, texture or native material to hide bad source content.
Keep low-level parse/load functions strict so regression tests, editor validation and content pipelines still detect bad input.
Do not swallow actual engine failures such as an incompatible renderer/plugin ABI or a missing mandatory core provider.

## Changes

engine/northstarcpp/modules/viewport/viewport.cpp:
- Quarantine failed source meshes and native materials by virtual path for the active VFS mount; reset on asset source remount.
- Skip malformed YDD/YDR/OBJ meshes rather than failing the entire draw.
- Skip broken material GPU batches while preserving healthy batches and actors.
- Validate the optional world_surfaces_runtime.ytd dictionary completely before submitting surface textures. A damaged YTD is ignored with a warning. Renderer-provider failures remain explicit.

engine/northstarcpp/modules/editor/window.cpp:
- Ignore malformed optional FPS camera/tuning data, streaming zone manifests and authored skydome with distinct one-time warnings.
- Mark synchronous or asynchronous streaming drawable failures, avoiding repeated requests for the same failed asset.
- Ignore invalid scripted character source/pose without disabling the rest of the game or generating recursive modal windows.

engine/northstarcpp/modules/host/module_contracts.hpp:
- Added missing validation of optional NorthStarRenderLightsApi v1 (ABI and set_lights pointer); this addressed a concurrent Render Graph API build failure.

## Regression tests

tests/optional_asset_recovery_smoke.cpp and tests/optional_asset_recovery_smoke.py:
- Real WARP Viewport, temporary project containing a corrupt YDD, YMAT and optional YTD next to valid sibling objects.
- Three successful draw calls; exactly one WARN line for each bad resource.

tests/player_optional_assets_smoke.py:
- Copies NorthStarFirstPersonPlatform into a temporary directory and damages its optional skydome, camera and zones data only.
- Requires real WARP QA Player to exit zero, render the project-defined textured skeletal character, and emit precisely three expected warnings.
- Original project and sharedAssets are not modified.

## Results

Isolated GCC/CMake/Ninja Release test build: PASS.
Focused CTest: 6/6 PASS (material_resources, editor_asset_pipeline, shared_assets, asset_streaming_internal, assets_vfs, optional_asset_recovery).
Actual Player QA Release build: PASS.
NorthStarFirstPersonPlatform verify.cmd: PASS, exit 0.
Actual Player --warp --character-smoke on clean project: PASS.
Same character smoke with three corrupted optional sources: PASS, exit 0 and three distinct WARN messages.

## Limits and follow-up

An invalid mandatory startup scene or broken renderer provider still produces a fatal error by design. This protects runtime invariants and avoids presenting corrupted engine state as a working game.
Failed resource cache entries are retried after VFS source remount/recreation; watching source timestamps for live retry is not yet implemented.
Full clean multi-target build, whole test suite, and installed production dist replacement were not performed.
No Git commit/push; changes remain local and preserve concurrent uncommitted work.
