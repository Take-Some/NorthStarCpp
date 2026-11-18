> Paths reflect the grouped OOP engine SDK layout. The canonical load order is api/manifest.json.

# NorthStarCpp — shared script weapon ballistics (2026-11-17)

## Ownership
- Core implementation: `engine/northstarcpp/resources/sharedAssets/api/weapon/ballistics.js` (`WeaponBallistics.solve`).
- Root camera/physics/scene facades: `modules/03_camera_api.js`, `06_scene.js`, `07_physics_session.js`; registered in `11_sdk_entry.js`.
- Deterministic generated entrypoint: `api/northstar/bootstrap.js`.
- Native world-space transform service: `Viewport::attached_local_point` + `engine.scene.attachmentPoint` host binding. Transforms a weapon-local muzzle point through the same skinned bone attachment matrix used for visible gun geometry.
- Game-owned scripts (`Projects/AbbyPistolDemo` and `tools/templates/abby_pistol_demo`) now call `WeaponBallistics.solve` with metadata, not hand-built camera rays.

## Solver
1. Read camera eye and direction from root Camera/Player APIs, with test-time explicit override.
2. Sweep camera ray for aim target (or use range endpoint).
3. Query real rendered attachment world transform for the author-specified weapon-local muzzle point.
4. Sweep from muzzle toward camera aim target, respecting occlusion and player collision ignore.
5. Return `muzzle`, `aimPoint`, `direction`, `hit`, and diagnostic `cameraRay`/`shotRay`.
6. If no evaluated attachment is available, return null; **never** fall back to spawning from Player.eye.

## Source and QA
- Project and template weapon metadata: `muzzleLocal: {x:0,y:0.043,z:0.200}`. This is a preliminary project-authored model-space exit point requiring visual YDD calibration. It is NOT a measured physical muzzle attachment.
- `node tests/weapon_ballistics_shared_smoke.cjs`: PASS — muzzle-origin, sight convergence, close obstruction, camera defaults.
- Native `northstarcpp_player` build: PASS, local `out/build/northstarcpp_player.exe`.
- `northstarcpp_viewport_pistol_ads_smoke`: PASS; `northstarcpp_viewport_skeletal_pipeline_smoke`: PASS.
- `node --check` of project/template script: PASS.
- Bootstrap composition and `--verify`: PASS.
- `git diff --check`: PASS on touched paths.

## Remaining gates
- The query uses the last evaluated/rendered attachment frame, not an on-demand fresh post-animation pose at the firing instant. Camera and actor can move between that sample and script firing.
- Validate project `muzzleLocal` against visible gun muzzle in an actual Engine GPU/Play-In-Editor frame; tune if needed.
- Final animation silhouette acceptance: elbows and clavicles must be inspected with skinned meshes, not Proxy View.
- Add rendered dual debug lines for camera sight ray and muzzle-origin shot ray if desired.
- No commit/push and no production dist replacement.
