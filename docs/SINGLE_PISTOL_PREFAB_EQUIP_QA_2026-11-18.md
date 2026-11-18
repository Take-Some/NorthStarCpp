# NorthStarCpp — One physical pistol prefab, two runtime states
Date: 2026-11-18

## Decision
Only `prefabs/weapon/pistol.nprefab` is the canonical pistol authoring asset. The redundant
`prefabs/weapon/pistol_attached.nprefab` was removed. There are no remaining active textual
references to the removed prefab under engine, projects, tests, tools, or docs.

The source prefab remains a physical mesh and body at **0.65 kg, collision enabled**.
The new `Prefab.instantiate(path, {position, physics})` contract provides
`physics:"authored"` (default: authored mass/collision) and `physics:"visual"` (instance
mass=0 and collision=false; same mesh, transform, materials, stable scene entity lifetime).
`physics:"visual"` combined with `collision:true` is invalid.
The choice is per instance, not a duplicate serialized prefab.

### Example
```javascript
// Ground pickup: authored 0.65 kg and world collision.
const ground = Factory.prefab("prefabs/weapon/pistol.nprefab", {
  position: {x:2, y:1, z:3}
});
// Character socket: static mesh under skeletal attachment.
const equipped = Factory.prefab("prefabs/weapon/pistol.nprefab", {
  position: Player.eye(),
  physics: "visual"
});
Scene.attachToBone(equipped.root.index, holder.index,
  "r_hand_prop_attachment", {x:0,y:0,z:0}, {x:0,y:0,z:0}, "camera_forward");
```
`WeaponController` uses this mode, and `NorthStarFirstPersonPlatform` uses it
for its legacy project-local pistol logic. Neither project declares `equippedPrefab`.

## Changes
- Native-scene SDK `api/scene/Prefab.js`: per-instance physics override.
- Canonical `api/weapon/weapon.js`: single source prefab plus visual instance.
- Both project `playerConfig.js` configurations and FirstPersonPlatform `main.js`.
- FirstPersonPlatform now refreshes weapon HUD immediately when a round is fired.
- The TypeScript `Prefab.instantiate` and `Factory.prefab` options include
  `physics?: "authored" | "visual"`.
- Native prefab/composition test references and the shared weapon prefab documentation
  have been updated.
- NEF8 registry deletion intentionally applied with `--prune`; the shared NSP
  was rebuilt without the removed asset.
- A tool-created backup under the shared assets source was moved outside packaged
  resources to `.northstarcpp/backups/2026-11-18/weapon-prefab-migration/`.
- Rebuilt the **Player embedded API**, not only the NSP. The resulting executable
  contains the new Prefab validation; the old validation is absent.

## Verified QA
- 7/7 Node weapon/prefab/camera smoke tests PASS:
  `prefab_root_oop`, `abby_pistol_demo`, `abby_pistol_gameplay`,
  `weapon_controller_aim`, `weapon_effects_shared`,
  `weapon_shell_contacts`, `player_look_ads`.
- 3/3 modified project JS source files: `node --check` PASS.
- Native `northstarcpp_editor_prefab`: CTest PASS after updating canonical list.
- Shared `northstar_shared.nsp` packed successfully with 224 entries after controlled
  registry prune (223 registered source assets plus generated Qt RCC).
- Real Player launched **AbbyPistolDemo** and logged:
  `[engine.weapon] Equipped Pistol`;
  `bone_attachment=PASS ... joint=r_hand_prop_attachment`.
- Real Player launched **NorthStarFirstPersonPlatform** and logged:
  `[Pistol] Abby equipped. Magazine=12, reserve=72`.
- The earlier startup errors `Only an owned static visual can attach to a bone`
  and `Prefab anchor requires finite x/y/z` were absent in those post-rebuild runs.

## Known limits
- CTest `northstarcpp_prefab_composition` currently fails before reaching pistol
  coverage with `An actor must reference a material, not a texture`. This is a
  separate material-reference fixture failure, not evidence of missing pistol prefab.
- Test simulations verify firing, recoil, NAC, magazine and shell events; a human-triggered
  interactive click and GPU visual hold of the actual sight picture were not performed.
- The project-local FirstPersonPlatform PistolWeapon remains a legacy second gameplay
  implementation separate from shared WeaponController; further consolidation could
  remove code duplication without requiring more prefab files.
- The production `dist` was not overwritten, no commit or push.
