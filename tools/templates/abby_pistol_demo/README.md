# Abby Pistol Demo РІР‚вЂќ NorthStarCpp

An example **composition**, not a special type of player. The scene contains
a generic possessed `Demo Pawn` (separate PlayerController, CharacterMotor
 and Camera entities), while
Abby is an ordinary reusable skinned character prefab. The pistol is an
optional, separate weapon prefab.

## Shared character prefabs

- `sharedAssets/prefabs/characters/abby.nprefab`: independent Abby YDD visual, YMT
  skeleton resolved from the model, materials and `northstar.animation.default`
  V3 prefab component containing the authored `mm-explore.ycd@abby-mm-explore-idle`
  clip. No Pawn, camera, physics motor or weapon component.
- First-person arms use `Scene.setVisualMode(entity, "first_person_arms")`
  on the **same** Abby Entity. No FPS-specific prefab or second model exists.
- `Projects/AbbyPistolDemo/prefabs/weapon/pistol.nprefab`: a separate, collision-free
  weapon model. Weapon selection, ammunition, audio and bone attachment
  belong to project scripts and can be omitted or replaced.

Engine `Prefab.instantiate()` now accepts the generic scene V3
`northstar.animation.default` component and starts the YCD clip when the
visual entity is spawned. The implementation is format-general: other
animated characters and weapons can use the exact same prefab contract.

An independent Abby can be instantiated in any project:

```js
import { Prefab, Scene } from "@northstar/engine";

const npc = Prefab.instantiate("prefabs/characters/abby.nprefab", {
    position: { x: 4, y: 0, z: 9 }
});
// Not a Player; can be an NPC, decorative actor or cinematic character.
Scene.playAnimation(npc.root.index,
    "characters/abby/animations/mm-explore.ycd@abby-mm-explore-walk-loop-fw");
// Later: npc.destroy()
```

No `Player.configure` or pistol is needed for this use.

## Player is a separate concern

`main.nscn` is generated with 42 Scene Entities, including independent
`PlayerController` (ID 40), `CharacterMotor` (ID 41) and `Camera` (ID 42)
associated with `Demo Pawn` (ID 1). Camera and Motor components belong to
their respective Scene entities, not to Pawn. Pawn does not
reference Abby's mesh or pistol. Any compatible entity can receive
player-control components in a project.

For this demo only, `assets/scripts/playerConfig.js` associates that generic Pawn
with the optional `character.prefab` only; camera mode changes rendering, not entity count.
`CharacterVisual` in `assets/scripts/main.js` instantiates and animates Abby
independently of `PistolWeapon`. If `weapon.enabled: false`, Abby remains.
If `character.prefab` is changed to a different animated model, the
visual system uses that prefab. An NPC prefab does not require either
controller.

To equip a different weapon, point `weapon.prefab` to its own `.nprefab`
and supply its own animation/audio/grip configuration. All weapon rules are
script-owned, not engine- or character-specific. A new weapon may use an
entirely different project script.

## Resources

- Abby: `sharedAssets/characters/abby/` (YDD/YMT/YCD/YMAT/YTD)
- Weapon: `sharedAssets/models/weapon/pistol/pistol.ydd`
- Weapon animation: `sharedAssets/animations/weapon/pistol/military-pistol.ycd`
- Weapon audio: `sharedAssets/audio/weapon/pistol/pistol.nac` (29 clips)
- Sky: project-local `assets/models/skydome.ydd`,
  `assets/materials/skydome.ymat`, `assets/textures/skydome.ytd`

## Gameplay and controls

WASD movement; mouse look; Shift sprint; Space jump; left mouse fire;
R reload; V toggles first/third person; E interact; Escape pause; F10 exit.

Weapon script: 12 rounds, 72 reserve, 5 shots/sec, 1.8-second reload,
physics hit testing, recoil and NAC playback. Pistol YCD clips are
`idle`, `p-fire`, `p-reload`, `p-semiauto-empty`.

## Launch and tests

Run the project's `run.cmd` with a current Player. The environment
variable `NORTHSTARCPP_PLAYER_EXE` overrides the player binary. The launcher
uses Jolt and D3D12; it never silently substitutes legacy engine providers.

```bat
python tools\create_abby_pistol_demo.py --verify
node tests\abby_pistol_demo_smoke.cjs
node tests\character_prefab_animation_smoke.cjs
python tests\abby_pistol_demo_runtime_smoke.py path\to\northstarcpp_player.exe
python tests\abby_independent_prefab_runtime_smoke.py path\to\northstarcpp_player.exe
```

Native regression confirms both compositions: a possessed generic Pawn
with Abby and pistol, and a separately spawned animated Abby NPC with
**no Player and no weapon**. The full-body model is 377,655 material
vertices, so the bounded renderer budget was increased to 500,000.

## Known incomplete visual work

The current first-person presentation is **not yet visually accepted**:
the user-observed camera angle, natural two-hand gun-hold pose, and
weapon pivot/orientation need correction and new matching screenshots.
The runtime bone socket `r_hand_prop_attachment` resolves, but that
alone does not prove an ergonomically correct grip.

Existing firearm-grip animation assets must be identified and wired to
Abby's current rig; creating a new grip animation is not a prerequisite
where a compatible source already exists. Exact magazine manipulation,
finger alignment, IK and animation blending remain unverified.

The D3D12 provider still lacks the optional standalone Character Rendering
ABI; native YDD skinning is used instead. Material data are cached during
the load transaction, but p95/p99 performance on real GPU and broader
world-level visual acceptance are outstanding. The earlier Qt Editor crash
is tracked separately.
