> Paths reflect the grouped OOP engine SDK layout. The canonical load order is api/manifest.json.

# NorthStarCpp — unified engine API and modularity audit

Date: 2026-11-17. Scope: root JavaScript SDK, module composer, source-level regression tests, CMake test registration. Local working-tree changes only; no commit or push.

## Authoritative runtime layout

- Source manifest: `engine/northstarcpp/resources/sharedAssets/api/manifest.json`.
- Public entry: `northstar/bootstrap.js`, deterministically composed in manifest order. Modules are **not** separately evaluated.
- The verified manifest contains **23 focused modules**: contracts, camera, scene, physics, collision, ballistics, particles, weapon effects, vehicle, input, events, UI, player, audio, factory, and the remaining SDK bootstrap components.
- Old numbered module fragments may remain in the dirty working tree but are **not** listed in the current manifest and must not be embedded as independent scripts.
- All project code uses `NorthStar.modules.engine` / `@northstar/engine`. Native `engine.*` providers retain authority for scene objects, physics, camera, vehicles and rendering.

## Consolidation applied

1. `Factory` uses **one built-in recipe registry** for both `kinds()` and `create()`. Custom `project.*` factories remain supported. Own-property checks reject collisions with inherited JavaScript properties such as `constructor`, `__proto__` and `toString`.
2. `Contracts.identifier()` unifies camera shot-source and preset identifier validation.
3. `Scene.spawn()` caches its allowed descriptor-key set **once per V8 isolate**, avoiding a `new Set()` allocation on each entity spawn. Unknown fields are still rejected.
4. The canonical vehicle smoke test runs the complete SDK and validates the public `API.vehicle` identity, rather than evaluating a detached class without its required contracts.
5. The weapon-effects smoke mock now implements the optional native surface-material capability. This prevents a legitimate provider-fallback warning from being mistaken for an unexpected execution event.
6. Gameplay test resolution is **project `assets/` first, engine `sharedAssets/` second**. A portable AnimationLab fixture lives in `tests/fixtures/animation_lab/assets/`, without reliance on a retired sample project.

## Automated regression gate

`tests/engine_api_modularity_smoke.cjs` checks:

- Manifest uniqueness and safe, consistently named fragments.
- Per-module 32 KiB source budget.
- Duplicate top-level SDK classes and duplicate method declarations within classes.
- Byte-exact composed bootstrap and JavaScript syntax.
- Presence of the canonical `Factory`, `Scene`, `Camera`, `Vehicle`, `Physics`, `WeaponBallistics`, `WeaponEffects`, `Particles`, and `Session` API owners.

Registered in CMake as `northstarcpp_engine_api_modularity`, alongside the existing factory smoke test. Check regeneration before running the Node test:

```powershell
python tools/dev/compose_northstar_bootstrap.py --root engine/northstarcpp/resources/sharedAssets/api
node tests/engine_api_modularity_smoke.cjs
node tests/engine_api_factory_smoke.cjs
node tests/vehicle_sdk_smoke.cjs
node tests/weapon_ballistics_shared_smoke.cjs
node tests/weapon_effects_shared_smoke.cjs
node tests/gameplay_scripts_smoke.cjs
```

## Remaining native monoliths

The preliminary local size scan (excluding generated shader bytecode and third-party code) still identifies several major C++ sources for extraction:

| Native source | Approximate source size | Proposed boundary |
| --- | ---: | --- |
| `modules/editor/window.cpp` | 242 KiB | Win32 frame/bootstrap/input/editor bridge |
| `modules/viewport/viewport.cpp` | 195 KiB | frame builder, animation, rendering submission |
| `modules/project_manager/qt_project_browser.cpp` | 95 KiB | asset model, preview, project navigation |
| `modules/editor/qt_editor_window.cpp` | 75 KiB | dock workspace, inspector, outliner, signals |

Existing `.inc` and new `.cpp` extraction work is present in these areas; do not copy functionality into alternate implementations. Move one cohesive responsibility at a time, update CMake target sources, and run native smoke tests after each change. Renderer provider implementations should share common policies, not duplicate GPU resource ownership.

## Verification limits

The targeted JavaScript smoke tests pass after the SDK composition and fixture updates. The CMake test registration was edited and Git whitespace validation passed. **A complete Windows C++ build, runtime scene evaluation and before/after FPS or memory benchmarks were not performed in this pass.** No runtime performance uplift is claimed without measurements. The repository contains numerous unrelated local changes, which were intentionally left intact.
