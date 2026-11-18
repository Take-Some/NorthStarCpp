# Abby pistol socket / skinned-mesh basis fix
Date: 2026-11-17. Build root: out/build.

## Verified cause
NorthStarCpp previously applied the 180-degree imported YDD/YMT presentation rotation to GPU skinning and published sockets, but CPU skinned material/triangle staging still read the original unrotated render cube. Consequently the gun could render ~0.95m away from the visible hand although the authored right-hand socket was only 0.064m from the right wrist. The old TPS authored pistol orientation also pointed backwards: barrel-forward dot = -0.620.

## Source and project changes
- engine/northstarcpp/modules/viewport/viewport_skeletal_basis.hpp: added synchronize_skeletal_render_cube, restoring equivalence of the CPU render cube rotation and the same presented world matrix used by sockets and GPU skinning.
- engine/northstarcpp/modules/viewport/viewport.cpp: called that helper exactly once after applying the authored presentation basis.
- Projects/AbbyPistolDemo/assets/scripts/playerConfig.js and tools/templates/abby_pistol_demo/scripts/playerConfig.js: recalibrated third-person socket rotation to x=-107.270 y=50.242 z=124.884 degrees; FPS camera-forward rotation stays at zero.
- tests/viewport_skeletal_forward_smoke.cpp: compares CPU vs GPU/attachment world transforms of imported-model vertices.
- tests/viewport_skeletal_pipeline_smoke.cpp: validates the real Abby YDD/YMT/YCD socket-to-wrist distance, muzzle direction and TPS heading sweeps plus FPS grip and ADS.
- tests/abby_pistol_demo_smoke.cjs and tests/abby_pistol_demo_runtime_smoke.py: migrated the QA fixture to actual assets/scripts and replaced the wrong assumption of a positive world-X right hand with a measured wrist-to-socket test.

## Measured regression results
TPS muzzle/body forward dot: -0.620 before -> +1.000 after.
TPS gun pivot to right wrist: 0.064 m. Tested yaw headings -0.85, 0, +0.25, +0.65, +1.15 rad.
FPS actual Player socket: (-0.116,2.123,-7.571), solved right wrist (-0.088,2.088,-7.617), ~0.064 m apart; weapon muzzle axis=(0,0,1).
5/5 related CTest PASS.
node --check of both playerConfig.js copies PASS; node tests/abby_pistol_demo_smoke.cjs PASS.
python tests/abby_pistol_demo_runtime_smoke.py out/build/northstarcpp_player.exe PASS (actual WARP D3D12 / V8 / Jolt scene; lifecycle and skydome passed).
northstarcpp_player and northstarcpp_editor_qt builds PASS in out/build; Qt linker emitted pre-existing boost::any duplicate-section warnings, and Ninja concurrent-build recovery warnings occurred.

No final on-screen pixel comparison was carried out. The self-collision system uses geometric proxies rather than exact Jolt contact per polygon. No production package replaced, no commit or push.
