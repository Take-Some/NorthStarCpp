# Pistol aiming / real muzzle convergence — 2026-11-17

## Root causes
1. `WeaponBallistics.solve(mode: "auto")` selected *barrel* for first-person hip fire, so the shot followed the animated gun axis rather than the visual centre of the camera view. RMB/ADS selected a different path.
2. The player weapon controller ignored the separate skeletal holder (Abby visual entity) during physics sweeps instead of the Jolt-controlled Pawn capsule; the camera could therefore contact the owner's body.
3. The demo used the same relatively large camera kick and look sensitivity with and without iron-sight ADS.

## Corrective changes
- **Ballistics**: for first-person `mode:"auto"`, resolve a camera sight target for both hip and ADS, then sweep from `Scene.attachmentPoint(weaponIndex,muzzleLocal)` through that point. Non-FPS auto and explicit `mode:"barrel"` still fire along the actual world-space barrel axis.
- **Muzzle**: retain actual pistol YDD calibration `muzzleLocal=(0,0.0738,0.238)`. Flash and smoke remain at this transform and face the physical gun axis. Never substitute `Player.eye()` for muzzle origin.
- **Precision**: hip fire preserves normal sweep radius; ADS uses its authored fine sweep radius. Direct aim solve runs before camera kick.
- **Owner collision**: when possessed by a player, ignore `Player.index` (the actual Jolt character capsule) during both camera and muzzle sweeps, not the detached skeletal visual index.
- **Aim controls**: `Player.config.look.adsSensitivityMultiplier` (0.1..1, default 1) scales first-person right-mouse input for mouse and gamepad. Abby demo and prefab template use 0.52.
- **ADS recoil**: optional `WeaponProfile.recoil.aimDownSights` overrides camera kick only while holding RMB in first person. Abby profile uses pitch 0.024 rad, yaw 0.001 rad, recovery 0.18 s, compared with hip pitch 0.055 rad and recovery 0.28 s.
- The authored real YDD/YMT/YCD rear sight IK, first-person weapon socket and the casing ejection port are not replaced or faked.

## Verification
- `tests/player_look_ads_smoke.cjs`: PASS (ADS sensitivity, third-person unchanged).
- `tests/weapon_controller_aim_smoke.cjs`: PASS (Pawn collider ignore, independent model index, ADS recoil).
- `tests/weapon_ballistics_shared_smoke.cjs`: PASS (camera-to-muzzle convergence, hip radius, barrel-only NPC, near obstruction, no guessed muzzle).
- `tests/weapon_effects_shared_smoke.cjs`: PASS.
- `tests/abby_pistol_demo_smoke.cjs`: PASS.
- `northstarcpp_viewport_pistol_ads_smoke` built and passed; native samples report 0.0000m rear-sight error and camera-forward gun axis across seven yaw/pitch cases.
- The SDK `api/northstar/bootstrap.js` was recomposed successfully.

## Post-fix build and native distance acceptance — 2026-11-17
- The earlier `WinError 5` NSP lock cleared when the old Player process was no longer running. `cmake --build out/build --target northstarcpp_player northstarcpp_viewport_pistol_ads_smoke -j 4` completed, exit code 0; the new `northstar_shared.nsp` contained 200 resources.
- The linker emitted nonfatal Boost RTTI duplicate-section warnings. These require separate clean-build investigation.
- An existing `tests/abby_pistol_demo_runtime_smoke.py` run did not detect the required first-person body log line; its old smoke contract is not considered green.
- The new `tests/weapon_live_aim_acceptance.py` starts an **actual Player process with D3D12 WARP, V8, Scene and Jolt** using a temporary QA-only copy of AbbyPistolDemo. The normal `WeaponController.fire()` performs each shot. Its `WeaponBallistics.solve` result is observed to verify the true muzzle point, camera hit and authoritative muzzle-origin Jolt hit.
- The first native run hit targets at 2 and 5 m, but at 20/50 m legitimately hit authored `Torus` entity index 39 at (0,1.5113,0). The torus was moved laterally **only in the ephemeral QA scene**; the original game scene and its assets were not altered.
- The unobstructed run completed with `status=PASS`, Player exit code 0, render initialized, four shots fired, and exact target index matches for BOTH the camera and the muzzle ray:

| Camera distance | Target index | Camera hit | Muzzle hit | Center-to-shot-ray deviation |
|---|---:|---:|---:|---:|
| 2 m | 46 | 46 | 46 | 52.22 mm |
| 5 m | 48 | 48 | 48 | 14.56 mm |
| 20 m | 50 | 50 | 50 | 3.17 mm |
| 50 m | 52 | 52 | 52 | 1.24 mm |

The deviation is the 3D perpendicular separation of the infinite muzzle-origin bullet line from the *geometric center* of the target. Because the camera targets the *front contact surface*, near-target deviation is expected. It is **not** pixel-space reticle error, spread, or physical bullet impact offset.

The QA logs confirm `renderer.skydome=PASS`, true first-person head mask, `fps_grip=SOLVED`, and a real `r_hand_prop_attachment` bone attachment. One existing pistol-shell YMAT slot remains unresolved, with neutral PBR fallback. Data saved in `out/qa_pistol_aim/last_report.json` and `player_stderr.log`.

### Remaining visual acceptance
This verifies the native game runtime and actual Jolt hit resolution at four distances but does **not** include screenshots, GPU readback, subjective mouse feel, or a frame-by-frame D3D12/Vulkan comparison of reticle, muzzle flash and visible impact decals. An actual RMB-held input test and ADS frame capture remain useful before asserting full visual UX parity.

### Final current-Player check
The current `out/build/northstarcpp_player.exe` (10,423,267 bytes; SHA-256 `2C5882491DA10058EA450071DD452B9AB5A2DB5B8AEA25875BA95FD8BE8C6408`, timestamp 2026-11-17 23:04:12) was run directly with `tests/weapon_live_aim_acceptance.py`: PASS for all four distances, renderer initialized, exit code 0. Subsequent concurrent CMake rebuild attempts encountered intermittent `WinError 5` archive replacement locks; this is not a clean-build assertion for every target.

No production `dist` replacement, Git commit or push was performed.
