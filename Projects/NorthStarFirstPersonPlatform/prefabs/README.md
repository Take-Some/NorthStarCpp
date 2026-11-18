# NorthStarCpp - Project prefabs

These project-owned Scene V3 `.nprefab` files are editable with the
Qt Editor's Export Selection as Prefab / Instantiate Prefab commands.
JavaScript instancing uses the plugin-neutral engine VFS and physics/ECS.

## Pistol: first runtime game prop

| Purpose | Project-relative path |
| --- | --- |
| Runtime prefab | `prefabs/weapon/pistol.nprefab` |
| Low-poly model | `sharedAssets/models/weapon/pistol/pistol.ydd` |
| VFS reference | `models/weapon/pistol/pistol.ydd` |
| OOP script | `assets/scripts/examples/PistolSpawner.example.js` |

This prefab uses the original native binary YDD from sharedAssets.
The source model contains 8 mesh parts and 21,902 triangles, with
a runtime 0.65 kg dynamic box rigid body. Every instance has a stable ID.

### JavaScript usage

Copy this into `assets/scripts/main.js` or use the OOP script example. The
example script is not loaded automatically.

```js
import { Prefab, Events } from "@northstar/engine";
let pistol = null;
Events.on("start", () => {
    pistol = Prefab.instantiate("prefabs/weapon/pistol.nprefab", {
        position: {x:2, y:0.75, z:2}
    });
});
Events.on("stop", () => {
    pistol?.destroy();
    pistol = null;
});
```

### Scope and limitations

- Native YDD render mesh, ECS entity, and dynamic provider-owned physics
  body. The collider remains a simple box, not an exact convex mesh.
- The original material slots and rig need fuller Material/Animation
  integration; the generic triangle renderer currently uses a fallback tint.
- The prefab uses translation-only transforms. Rotation and scale
  require a future Physics ABI shape extension.
- Runtime objects persist while authored World Partition cells stream;
  they are released on destruction, stopped Play, and hot reload.
- The Qt Editor can author the prefab, but live script-spawned entities
  are not yet mirrored into its Scene Hierarchy / Inspector.
- Runtime .nprefab supports one root, at most 128 objects, no embedded
  scripts/cells or external component blobs.
- Equipping, firing, reloads, inventory, and visual effects are future
  gameplay work, not implemented by this game prop.

### Regression

`northstarcpp_player_pistol_runtime` runs the actual Player and V8
provider with the original project files. It verifies VFS prefab loading,
physics collision/gravity, and removal before shutdown.
