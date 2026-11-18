# NorthStarCpp — Character self-collision, Abby grip and weapon attachment
Date: 2026-11-17
Workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
Canonical build: out/build, Release; user production dist unchanged.

## Input/observed bug
Abby previously walked back-to-front (imported YDD mesh basis), then after the basis correction clasped wrists almost at the same point and sometimes placed hands/pistol inside herself in FPS. Real imported YMT+YCD has 1033 joints and skin-weighted helper/roll branches: solutions must retain the imported bone mapping, joints, topology and both scene views.

## Work completed in this pass
1. `modules/viewport/viewport_character_self_collision.hpp` — generic allocation-free humanoid proximity representation:
   - chest/torso capsule and head sphere from evaluated pelvis/spine/head and shoulder width;
   - front-directed projection for wrist/elbow IK collision constraints;
   - three-sample near-barrel proxy (grip/midpoint/tip) for equipped-object clearance;
   - supports arbitrary body yaw; disabled for nonhumanoid or invalid joints;
   - no actor-name / character-name checks and no Jolt rigid-body writes.
2. `modules/viewport/viewport_skeletal_animation.cpp`:
   - preserves previous YDD/YMT canonical facing correction and derives anatomical lateral sign from evaluated shoulders;
   - bounds elbow poles away from the torso; if an evaluated elbow intersects the torso/head, performs at most two additional IK pole corrections without moving the shoulder;
   - checks primary wrist and gun proxy, moving target forward only if needed;
   - separates supporting left hand from the dominant wrist: socket-relative lateral ~0.120 m and forward ~0.075 m instead of old nearly overlapped 0.055 m lateral +0.010 m forward;
   - stores body collision proxy per parent with actual rendered bone/socket matrices during `publish_sockets`; applies final safety clearance to the resulting `camera_forward` attachment transform.
   - Ordinary socket mode (clothing, holsters, backpacks) is intentionally not forcibly moved.
3. `modules/viewport/viewport_skeletal_animation.hpp`: holds cached per-parent collision for the synchronous frame; lifetime matches socket table.
4. `tests/viewport_character_self_collision_smoke.cpp`: torso/head clearance, downward gun sweep, yaw rotation, and invalid humanoid.
5. `tests/viewport_skeletal_pipeline_smoke.cpp`: real Abby YDD/YMT, mirrored helper continuity, real pistol grip and ADS, wrist separation, elbow/front-side constraints, frame-local collider penetration for wrists/elbows/gun across yaw/pitch including ±1.10 radians, final camera-forward weapon socket and adversarial intentionally embedded attachment.
6. `engine/northstarcpp/CMakeLists.txt`: registers `northstarcpp_viewport_character_self_collision` smoke.
7. Test fixture `tests/player_pistol_runtime_smoke.py`: repair reference to moved bundled asset and create transient scripts folder; still fails later with existing V8 `Dynamic bodies must use physics` when positioning a dynamic prefab. NOT validated.

## Numeric acceptance
Previously `r_hand_prop_attachment` to left wrist ~0.0594 m in FPS; updated camera-eye test gives ~0.1437 m, while allowing left-hand support on the weapon. 100% real-rig target reach tests in selected yaw/pitch, including ±1.10 rad, with no reported wrist/elbow or proxy-gun overlap against the authored torso/head volumes.

Focused CTest after changes, `out/build`: **5/5 PASS**:
- northstarcpp_character_facing
- northstarcpp_skeletal_asset_interop
- northstarcpp_viewport_character_self_collision
- northstarcpp_viewport_skeletal_forward
- northstarcpp_viewport_skeletal_pipeline

`northstarcpp_player.exe` rebuilt and linked with the modified viewport. C++ Qt Editor sources compiled; editor exe was not replaced because another Windows process locked `out/build/northstarcpp_editor_qt.exe` (linker: Permission denied). Repeated Ninja warning `premature end of file; recovering` indicates concurrent modifications to the single build tree.

## Limits
- This is animation-space self-collision against bounded geometric proxies, NOT exact animated triangle-mesh collision or full Jolt per-bone physics.
- Modelled gun length/radius are conservative generic defaults (~0.24 m and 0.048 m); long weapons require asset-authored collision bounds, capsule/convex colliders and clip-specific grip targets, preferably in the weapon and character XML descriptors.
- No real on-screen camera capture was accepted for FPS/TPS after latest changes; numerical tests and CPU pose diagnostics cannot guarantee every visible skinned vertex is non-penetrating.
- Existing project migration removed `Projects/NorthStarFirstPersonPlatform/scripts/main.js`, causing old `northstarcpp_first_person_protocol_logic` fixture to fail. `northstarcpp_player_pistol_runtime` now proceeds past missing paths but fails at V8 moveRuntime for a dynamic object: `Dynamic bodies must use physics`. These are still release blockers, independent of focused pose tests.
- Production dist not modified, no commit/push.
