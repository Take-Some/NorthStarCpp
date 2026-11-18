> Paths reflect the grouped OOP engine SDK layout. The canonical load order is api/manifest.json.

# Engine-owned WeaponBallistics — 2026-11-17

## Contract
- All FPS ballistic computations are owned by the shared root SDK (`@northstar/engine`) rather than an individual project's `main.js`.
- Native `engine.scene.attachmentPoint(childIndex,x,y,z)` resolves a weapon-local point from the *last published real skeletal attachment transform* produced by the viewport. The source is the same skinned rig/socket pipeline used to draw the weapon; no arbitrary camera-origin fallback.
- Generic `WeaponBallistics.solve({ weaponIndex, muzzleLocal, range, radius, adsRadius?, aimRadius?, ignoreIndex?, eye?, aimDirection? })` obtains camera state when explicit overrides are absent; optional overrides support NPC/cinematic/test calls.
- Camera sweep resolves the aim point; muzzle sweep starts from the transformed weapon-local point toward that aim point. Occluders along the muzzle ray can block a camera-visible target. An intersection behind the muzzle blocks the shot rather than reversing its direction.
- The shared SDK returns `muzzle`, `aimPoint`, `direction`, `hit`, `blocked`, `cameraRay`, `shotRay`; the project may decide damage, ammo, UI, sound and animations.
- The configured `muzzleLocal` is weapon asset metadata and still needs physical YDD-geometry verification. It must not be treated as an engine-hardcoded default.

## Changed sources
- `engine/northstarcpp/modules/viewport/viewport.hpp`
- `engine/northstarcpp/modules/viewport/viewport.cpp`
- `engine/northstarcpp/modules/editor/window.cpp`
- `engine/northstarcpp/resources/sharedAssets/api/scene/scene.js`
- `engine/northstarcpp/resources/sharedAssets/api/weapon/ballistics.js`
- `engine/northstarcpp/resources/sharedAssets/api/core/sdk.js`
- generated `engine/northstarcpp/resources/sharedAssets/api/northstar/bootstrap.js`
- `engine/northstarcpp/resources/sharedAssets/api/types/northstar-engine.d.ts`
- `Projects/AbbyPistolDemo/assets/scripts/main.js` and `playerConfig.js`
- `tools/templates/abby_pistol_demo/scripts/main.js` and `playerConfig.js`
- `tests/weapon_ballistics_shared_smoke.cjs`

## Verification
- `node tests/weapon_ballistics_shared_smoke.cjs` — PASS (two-stage muzzle origin; sight convergence; near-cover; missing attachment returns null; optional camera inputs).
- SDK bootstrap deterministic composition and verification — PASS.
- `node --check` on both running project and template script — PASS.
- `cmake --build out/build --target northstarcpp_player -j 4` — PASS.
- `northstarcpp_viewport_pistol_ads_smoke.exe` — PASS.
- `northstarcpp_viewport_skeletal_pipeline_smoke.exe` — PASS.

## Remaining limitations
- Weapon-local `muzzleLocal={x:0,y:.043,z:.200}` in the demo/template is a preliminary authored offset. Exact alignment with the rendered pistol barrel has not been visually accepted against the original mesh.
- Native FpsArena / FpsWeaponEffects sample mode still uses its own camera-origin and approximate FX muzzle; it has not been migrated to this shared script API.
- The attachmentPoint query reflects the last rendered frame. A definitive zero-frame-lag design would publish a pre-fire animation/weapon transform snapshot at the start of the simulation tick.
- No proof yet that muzzle flash, tracers, shell ejection and the project weapon's actual on-screen impact point all coincide in a GPU live scene.
- ADS elbow visual acceptance has not been demonstrated; geometry smoke tests passing are not a substitute for side-view screenshots of the skinned arms.
- No commit/push or production dist replacement.
