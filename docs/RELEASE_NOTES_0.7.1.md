# NorthStarCpp 0.7.1 — Change log

**Version:** 0.7.1
**Branch:** `main`
**Release type:** source milestone (not a certified production binary release)

## Included

- Provider-neutral rendering, including D3D12/Vulkan improvements, shadows, atmosphere and post-processing work.
- Multi-level JavaScript API across render, audio, physics, scene, camera, input, player, ECS, UI and vehicle domains.
- V8 bootstrap and embedded SDK updates, TypeScript declarations, project script migration and smoke-test coverage.
- Character motor, skeletal animation, root motion, AI/navigation and gameplay framework improvements.
- Editor asset browser, prefab and material authoring, Qt UI and performance/refactoring work.
- Canonical shared weapon prefab: the physical pistol and attached visual now use a single `pistol.nprefab` with per-instance physics.
- Input focus routing from child GPU viewport to the gameplay Input Provider.
- NEF8, YMAT, audio asset pipeline, Material and Physics improvements.
- Updated samples, documentation, tooling and targeted regression tests.

## Release QA scope and known limitations

This commit represents the current development milestone, **not a claim of a fully production-ready 1.0 engine**. Confirmed prior to tagging:

- NorthStarCpp Player was rebuilt after embedding the updated `Prefab.js` API.
- First-person examples successfully logged equipped pistols, and Abby's hand-bone attachment was confirmed on runtime startup.
- Targeted JavaScript tests for input, prefab lifecycle, weapon grip, ADS, firing, NAC and shell effects passed.
- `northstarcpp_editor_prefab` CTest passed.
- `northstarcpp_prefab_composition` remains failing at an unrelated material-reference assertion.
- Several GPU/Editor interactive acceptance gates, complete cross-provider parity and the full CTest suite remain open.

## Source and asset policy

Transient build trees, derived caches, GPU debug captures, asset extraction sources and local backups are not part of this source release. Project-specific local content remains separate where excluded by the repository's `.gitignore`.

See `docs/ENGINE_API_HIERARCHY_2026-11-18.md`, `docs/SINGLE_PISTOL_PREFAB_EQUIP_QA_2026-11-18.md`, `docs/MT28_YMAT_IDENTITY_RESTORATION_2026-11-18.md`, and `docs/PLAYER_VIEWPORT_KEYBOARD_FOCUS_QA_2026-11-18.md` for further technical detail.
