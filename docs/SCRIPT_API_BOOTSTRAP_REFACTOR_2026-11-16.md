# Script API bootstrap: modularization, observability and bounded work

Date: 2026-11-16
Scope: current embedded JavaScript API and project-script loading in NorthStarCpp.

## What changed

- Extracted native SDK ordering, sealed module enforcement, VFS/DB override
  selection and main project entrypoint evaluation from the 5,800+ line
  `engine/northstarcpp/modules/editor/window.cpp` into:
  - `engine/northstarcpp/modules/host/engine_script_bootstrap.hpp`
  - `engine/northstarcpp/modules/host/engine_script_bootstrap.cpp`
- Integrated that module into `northstarcpp_runtime` in CMake. Windows Player
  retains only entity-script lifecycle and validation callback wiring.
- Consolidated the loading order into one immutable table. Protected SDK
  resources always evaluate from compiled bytes, never from project or
  `sharedAssets/scripts` VFS. Optional Player/Camera/Audio defaults use
  project-only override resolution: grouped name, then flat compatibility
  alias, then embedded fallback. Source paths are validated before execution.
  Archived projects probe a dedicated project archive, not the combined VFS.
- Centralized script-load telemetry under `engine.scripting.bootstrap`.
  Structured fields:
  `phase`, `status`, `source`, `role`, `path`, `bytes`,
  `duration_us`, `embedded`, `overrides`, `entrypoints`,
  `entity_scripts`, `total_source_bytes`, `reason`.
  Per-file PASS goes to DEBUG; begin/end aggregate to INFO; failures to ERROR.
  Telemetry errors are nonfatal to the original scripting operation.
  A rejected hot reload now emits an ERROR reason and duration.
- Reduced repeated work in each V8 context:
  - `ScriptRuntime::evaluate` receives embedded `string_view` directly,
    with no intermediate heap copy of the static source.
  - Generated `embedded_api::find` uses `std::lower_bound` over sorted
    names rather than scanning the table.
  - The Python resource generator reads each script once per regeneration.
  - Entity-script tracking is an `unordered_set`, initially reserved for a
    bounded number of distinct script paths instead of every scene object.
  - Entity IIFE wrapping uses a single capacity-reserved output string.
  - Only the selected source override is validated and evaluated.
  - Virtual Asset Catalog assembly deduplicates embedded API paths only,
    without allocating an additional map of all engine assets.
- The editor still displays the compiled API under the read-only Engine
  Shared Assets branch; project scripts remain editable. No new exposed
  `engine.*` native method or ABI compatibility change.

## Reproducible validation

Build directory: `build/foundation-migration`, isolated Ninja/GCC Release.

- Runtime and Player: **PASS**, rebuilt and linked after the extraction.
- Qt Editor and standalone Asset Browser: **PASS**, rebuilt after virtual
  catalog indexing changes.
- CTest selected Script API suite: **9/9 PASS** (input enums, Scene SDK,
  embedded API integrity, embedded usability, root OOP, Prefab OOP,
  footsteps events, UI root and scriptless Player lifecycle).
- `northstarcpp_player_bundled_scripting`: **PASS** (real V8: source and
  archive defaults plus project camera and configuration overrides).
- Qt Editor `--content-dock-smoke`: **exit 0**, Qt offscreen.
- Standalone Asset Browser `--smoke`: **PASS**, Qt offscreen.
- Isolated SDK component installation and external SDK consumer: **PASS**.

### Runtime trace evidence

With `NORTHSTARCPP_LOG_DIRECTORY=build/script-api-trace`, the test produced
`NorthStarCpp-ProjectScripts-11656.log` and `.ulog`.
In this one run the diagnostics showed:

- `provider=V8 mode=project embedded_count=19` (available resources).
- `status=pass embedded=15 overrides=4 entrypoints=1` (evaluated modules).
- `source_bytes=109923 duration_us=48435`.
- `entity_scripts=0 entity_bytes=0` for that test scene.

This is **one observed run**, not a before/after performance benchmark or a
claim about p95/p99 engine frame times. DEBUG per-file telemetry requires the
corresponding configured log level.

## Known limitations / next gates

- The entire `window.cpp` and unrelated editor/renderer/physics monoliths
  have NOT been split in this pass. Further candidates: script service binding
  registration, scene/ECS control logic and Win32 UI dispatch.
- CTest `northstarcpp_project_gameplay_scripts` currently **fails** in its
  Node-only mock: `engine.prefab.read` is undefined when the authored
  first-person pistol script instantiates a Prefab. The real Player/V8
  integration above **passes**; the old test harness requires its prefab/ECS
  mock to be updated separately. No claim of a completely green repository.
- No p50/p95/p99 profiling under Night City streaming, GPU load or heavy
  multiplayer/NPC scenes; the algorithmic changes require a dedicated
  measured benchmark.
- Qt offscreen smoke does not replace manual visual, DPI, interaction and
  hot-reload acceptance testing.
- Production executable/DLL files were NOT deployed or replaced. No
  Git commit or push was performed.

## Re-run

```powershell
cmake --build build/foundation-migration --target northstarcpp_player northstarcpp_editor_qt northstarcpp_asset_browser northstarcpp_embedded_engine_api_smoke
ctest --test-dir build/foundation-migration --output-on-failure -R "northstarcpp_(embedded_engine_api|embedded_api_usability|scene_script_sdk|shared_roots_oop|prefab_root_oop|player_footsteps_events|ui_root_script|input_enum_classes)"
ctest --test-dir build/foundation-migration --output-on-failure -R "^northstarcpp_player_bundled_scripting$"
```
