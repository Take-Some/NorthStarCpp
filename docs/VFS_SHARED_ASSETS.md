# NorthStarCpp VFS, sharedAssets, and project scripts

Copyright (C) 2026 Kayla's Systems — Kayla Verner.

## Runtime contract

The `engine.assets` provider is the single virtual filesystem (VFS) resolver.
Resource consumers must use `AssetStore` (alias `AssetManager`) rather than
reading source directories directly. A project owns its content while the engine
supplies default resources shared by all projects.

The standard `AssetStore::mount_project_assets(project_root, shared_root)`
mounts three overlays under one virtual namespace:

| Priority | Native directory | Virtual path examples |
| ---: | --- | --- |
| -100 | `NorthStarCpp/sharedAssets/` | `models/player.ydd`, `scripts/northstar/bootstrap.js` |
| 0 | `Projects/MyGame/` | `scripts/main.js`, `main.nscn` |
| +100 | `Projects/MyGame/assets/` | `models/player.ydd`, `textures/ui.ytd`, `gameplay/camera.json` |

The highest-priority matching file wins. For example, when both the project
and `sharedAssets` contain `models/player.ydd`, the project model is
selected. If the project does not contain it, the shared engine model is
used. An absent resource in all layers remains an error. Source files and
archives are resolved through the same asset provider.

When the explicitly supplied `shared_root` is physically the same directory
as the project or its `assets` folder, the engine avoids duplicate mounts.
Asset paths are **virtual** paths; no project needs to copy engine resources.

### C++ API

```cpp
#include <northstarcpp/assets.hpp>

northstarcpp::AssetManager assets;
assets.mount_project_assets("Projects/MyGame", "sharedAssets");
auto playerModelBytes = assets.read("models/player.ydd");
```

Projects can explicitly mount archives instead of the project directory.
Archive mounts use the project overlay priority while the shared engine layer
remains available at the lower priority.

## Root JavaScript API

The scripting provider owns the low-level `engine.*` host bridge. Its
native methods remain ABI-backed and plugin-overridable. The shared script
`engine/northstarcpp/resources/api/northstar/bootstrap.js` installs the public `NorthStar`
JavaScript helper namespace. This is project-independent SDK code.

For every active project script context the Player executes, in this order:

1. The scripting provider initializes `engine.*` and its native bindings.
2. The Asset Manager mounts `sharedAssets` as a low-priority VFS layer.
3. The compiled root script `scripts/northstar/bootstrap.js` runs via the immutable embedded API registry (not through VFS).
4. The project entrypoint `scripts/main.js` runs through the same VFS.
5. Scene-attached project scripts run. Afterwards the `start` event is emitted.

Only project gameplay scripts are loaded from VFS. The NorthStar SDK is
compiled into the Player and Editor; API bootstrap, events, input enums,
scene helpers, UI root and character base never resolve from writable
project or shared-assets files. Optional gameplay Player/Camera roots may
be replaced explicitly per project. Source and archived games use the same
engine-owned baseline without requiring loose SDK scripts.

### Project-owned camera policy

```js
const view = NorthStar.camera.createViewController({
    initialMode: "third_person"
    // Optional overrides: toggleKey, transitionSeconds, minDistance,
    // maxDistance, wheelStep. The default toggle key is V (86).
});

engine.on("start", () => view.start());
engine.on("update", () => view.update());
```

By default, **V** switches between first and third person with a 0.45-second
animated camera move. While in third person, wheel up moves closer and wheel
down moves farther, 0.7 metres per notch, clamped to 1.5–12 metres.
First-person scrolling is ignored. Returning to third person restores the
previous distance. Holding V does not repeatedly switch the view.
Projects may override all these defaults when constructing the shared controller.

`engine.input.wheelDelta()` returns the signed vertical wheel movement in
notches (120 native units per notch, fractional values supported).
`engine.camera.followDistance()` reads the camera's configured distance;
`engine.camera.setFollowDistance(metres)`, `engine.camera.setMode(name, seconds)`
and `engine.camera.mode()` expose native camera operations.
Input policy remains in shared JavaScript. Smooth positional interpolation,
line-of-sight collision and rendering remain native.

## Relevant tests

```powershell
cmake --build out/fps-release-qa-20261114 --target northstarcpp_player northstarcpp_assets_smoke
ctest --test-dir out/fps-release-qa-20261114 -R "northstarcpp_(assets_vfs|shared_camera_script|camera_director|player_bundled_scripting)" --output-on-failure
```

The VFS smoke checks shared fallback, project override, script visibility and
duplicate mount avoidance. The bundled V8 test checks that the shared API is
defined before `scripts/main.js` executes from both a source tree and a
FAT/DB archive.

## Scope

The default script bootstrap is an engine SDK layer, not a project game mode.
`engine/northstarcpp/resources/api/newviso/*.ysc` are existing different-format scripts;
they are not silently executed as JavaScript. A project without a JavaScript
entrypoint or attached scripts does not start a needless V8 gameplay context.
All project scripts that do run receive the shared API by default.
