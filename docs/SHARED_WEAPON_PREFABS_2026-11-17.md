# NorthStarCpp — Shared weapon and casing prefabs
Date: 2026-11-17

## Source of truth
Engine-owned Scene V3 prefabs reside under
`engine/northstarcpp/resources/sharedAssets/prefabs/`.
All relative paths below are standard root VFS paths, resolved by
`AssetStore::mount_project_assets(project_root, shared_root)` and by
the runtime `engine.prefab.read` / `Prefab.instantiate` API.

| VFS path | Mesh (engine sharedAssets) | Placement & physics |
| --- | --- | --- |
| `prefabs/weapon/pistol.nprefab` | `models/weapon/pistol/pistol.ydd` | Scene origin, mass 0.65 kg, visible, collision on |
| `prefabs/weapon/pistol_shell.nprefab` | `models/weapon/pistol/pistol_shell.ydd` | size 0.05 m, mass 0.006 kg, physical "Weapon debris" |
| `prefabs/weapon/rifle_shell.nprefab` | `models/weapon/pistol/pistol_shell.ydd` | provisional variant size 0.062 m, mass 0.011 kg; same underlying pistol mesh |

**Important:** The rifle shell is currently a *placeholder variant*: there is
no distinct rifle-shell mesh in sharedAssets. Replace the virtual mesh
reference with an authored rifle casing when its dedicated YDD/YDR exists.
The runtime prefab API enforces unrotated, unit-scaled templates, therefore
the variant changes `size` and `mass` but does not pretend to include
an elongated, distinct model.

Scene V3 has a minimum authoring `size` of 0.05 metres; the pistol
casing prefab uses this validated minimum. The legacy pistol source files
use the fifth numeric value after the color for `mass` (0.65 kg), not
`position.y`.

## Migration completed

- `Projects/AbbyPistolDemo/assets/scripts/playerConfig.js` now uses
  `prefabs/weapon/pistol.nprefab`, using the visual instance mode (mass 0, collision off during attachment).
- `Projects/NorthStarFirstPersonPlatform/assets/scripts/playerConfig.js`
  uses `prefabs/weapon/pistol.nprefab`.
- Two `PistolSpawner.example.js` scripts in NorthStarFirstPersonPlatform
  and KaylaNightCity use `prefabs/weapon/pistol.nprefab`.

Three pre-existing `Projects/*/prefabs/weapon/pistol.nprefab` copies remain
unchanged for compatibility and rollback. Current active script references
are migrated to the canonical `prefabs/weapon/` namespace to avoid a
project-local filename shadowing the engine-owned default. The original
project copies have not been deleted.

## Runtime use

```js
import { Prefab } from "@northstar/engine";

const worldPickup = Prefab.instantiate(
  "prefabs/weapon/pistol.nprefab",
  {position: {x: 2, y: 0.75, z: 2}}
);

const handVisual = Prefab.instantiate(
  "prefabs/weapon/pistol.nprefab",
  {position: {x: 0, y: 0, z: 0}, physics: "visual"}
);
```

`prefabs/weapon/pistol_shell.nprefab` and
`prefabs/weapon/rifle_shell.nprefab` are authored reusable templates for
the future common `WeaponCasings` API. Prefab existence alone does NOT
make each weapon eject a casing: weapon events and the shared spawn/impulse
API still need their final end-to-end wiring. Physically spawned casings
should never be removed on a particle-emitter timeout.

## Validation / acceptance

- Added regression coverage in `tests/editor_prefab_smoke.cpp` for all
  five engine-owned prefab files, Scene V3 parsing, mesh existence,
  mass/size/collision, and VFS resolution under each of the three
  project overlays.
- Due to unrelated CMake export-set failure
  (`northstarcpp_runtime` requires `northstarcpp_world_foundation`
  which is not exported), the normal CMake reconfigure target failed.
- Compiled the changed existing smoke-test object and linked a distinct
  QA executable using the existing Ninja compile/link commands, without
  changing the production Editor or Player:
  `out/build/northstarcpp_shared_weapon_qa.exe`.
- Executed that QA test with a disposable prefab path in `out/build`:
  **SHARED_WEAPON_VFS_QA_EXIT=0**, including three real project VFS mounts.

No commit, push, production distribution replacement, project file removal,
or gameplay visual acceptance is claimed.
