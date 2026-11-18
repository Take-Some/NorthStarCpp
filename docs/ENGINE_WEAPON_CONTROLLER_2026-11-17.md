> Paths reflect the grouped OOP engine SDK layout. The canonical load order is api/manifest.json.

# NorthStarCpp — Engine-owned weapon lifecycle (2026-11-17)

## Ownership

All standard firearm behavior is provided by the root `@northstar/engine` SDK. Projects supply the profile (prefab, YCD animations, NAC, socket points, cadence and recoil tuning), a character holder accessor, and optional hit gameplay policy. No project duplicates the shot/reload state machine.

- `engine/northstarcpp/resources/sharedAssets/api/weapon/weapon.js` — `WeaponController extends Script`: equip/attach, fire, reload, clip state, magazine/reserve, shot timing, sound, HUD, hit impulses and missing-socket recovery.
- `.../modules/physics.js` — `WeaponBallistics.solve` and `WeaponEffects.fire/kick`: camera/barrel mode, muzzle origin, collision, native VFX, angular recoil and persisted physical shell prefabs.
- `.../modules/03_camera_api.js` with native `CameraDirector::recoil` — render-only recoil impulse with decay; the projectile is solved before recoil moves the sight image.
- `engine/northstarcpp/modules/editor/window_runtime_frame_loop.inc` — the first-person runtime passes `script_weapon_fx.get()` to `Viewport::draw`. Without this argument, emitted FX was never visible in scripted FPS.
- `.../modules/sdk.js`, `.../manifest.json`, `engine/northstarcpp/modules/host/scripting.cpp` — register, compose and permit the root named import.
- `Projects/AbbyPistolDemo/assets/scripts/main.js` — project-specific Abby body pose and optional hit reaction only; no duplicated PistolWeapon.
- The template under `tools/templates/abby_pistol_demo/scripts/` mirrors the project.

## How to use

```js
import { WeaponController } from "@northstar/engine";

new WeaponController({
    // Optional, defaults to Player.config.weapon:
    profile: () => Player.config.weapon,
    // Character model is project-owned; weapon attachment is engine-owned:
    holder: () => character.holder,
    onHit: ({hit, target}) => { /* project damage/hit reaction */ }
}).install();
```

For each weapon, supply `prefab`, `grip.bone`, `muzzleLocal`, `ejectionLocal`, `animations.dictionary` with `idle`, `fire`, `reload`, and appropriate `audio`. Other fields configure `magazineSize`, `reserveAmmo`, `roundsPerSecond`, `fireMode` (`semi` or `auto`), `reloadSeconds`, `range`, casing prefab, recoil, and optional HUD asset. The root controller uses the real attachment point, not `Player.eye`, for ballistics and VFX.

The gun is the **same instance** in FPS, third person, hip-fire and ADS. Native socket attachment and YCD animation are not duplicated. Firing animation/recoil still execute if the last attached-frame sample is missing. In that case, shot origin-dependent VFX and projectile deliberately do not use an inaccurate eye-origin fallback; a warning is emitted.

## Verification

- Bootstrap compose + Node syntax: PASS.
- `tests/abby_pistol_demo_smoke.cjs`: PASS (YCD fire/reload, ADS, HUD, NAC, hit response, missing-socket recoil).
- `tests/weapon_effects_shared_smoke.cjs`: PASS (VFX, camera recoil and physical shell; failure isolation).
- `tests/weapon_ballistics_shared_smoke.cjs`: PASS (camera target, true muzzle, obstruction).
- `python tools/create_abby_pistol_demo.py --verify`: PASS (56 scene records, 45 entities and 29 NAC tracks).
- Native `northstarcpp_player`: BUILD PASS; script compiler whitelist and native recoil are linked.
- Real Player `AbbyPistolDemo` with Jolt/D3D12: survived 16 seconds with no project script exception after rebuilding embedded API/NSP. Bone attachment reports PASS.

## Remaining validation

An automated headless startup is not the same as visibly firing and reloading with human input. GPU readback / frame-by-frame acceptance of muzzle flash, true YCD pistol deformation, ADS recoil silhouette, casing trajectory and particle quality remains outstanding. The current native VFX is procedural proxy geometry, not a full transparent GPU particle renderer. `pistol_shell.ydd` still has an unresolved YMAT slot using a neutral fallback. No Git commit/push or production `dist` replacement was performed.
