# NorthStarCpp — compiled, read-only engine API script resources

Date: 2026-11-16

## Objective

Move the engine-owned JavaScript API from mutable `sharedAssets/scripts`
into `engine/northstarcpp/resources/api`, compile the resources into native
applications, and retain read-only visibility through both Qt Asset Browsers.
Game-owned scripts and third-party `engine.scripting` providers remain separate.

## Implementation

1. **Canonical engine source** — 19 current `.js` / `.d.ts` files
   (127,418 bytes on the final inventory) are staged under
   `engine/northstarcpp/resources/api`. No `.bak` files are embedded.
   Documentation is copied alongside source files.
2. **Deterministic resource compilation** —
   `tools/dev/embed_engine_api.py` generates
   `build/.../generated/northstar_embedded_api_scripts.cpp` with literal
   UTF-8 byte arrays and a read-only index.
   Invalid UTF-8, missing files and files outside the 1 MiB source limit
   fail the build. `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` tracks the
   actual source set. The runtime static library includes the generated
   translation unit and exports a read-only
   `northstarcpp::embedded_api::{entries,count,find}` interface.
3. **Native script loading** — Player initializes the engine's SDK
   bootstrap, Events, Input enums, Entity/Prefab helpers, UI root and
   Character base from `embedded_api::find`, never the project VFS.
   Default player/camera/audio scripts fall back to embedded resources;
   explicit grouped or flat project gameplay overrides remain supported.
   Project `scripts/main.js` and entity scripts still use VFS.
   Archive overrides are detected using a separate archive-only AssetStore, so loose shared scripts cannot masquerade as project-specific overrides. Missing external `sharedAssets` no longer blocks project scripting.
4. **Read-only Asset Browser** — an engine-only
   `AssetCatalog::scan(..., include_embedded_engine_api=true)` synthesizes
   `scripts/` folders and 19 file records from the compiled resource index.
   Real on-disk `sharedAssets/scripts` entries are deliberately suppressed
   on the engine side to avoid showing obsolete writable copies or backups.
   Each virtual file has an `engine-api://scripts/...` reference,
   cannot be resolved/revealed as a disk file, and opens a code preview
   in a read-only `QPlainTextEdit` dialog. Engine mount authoring actions
   are disabled, including rename, duplicate, delete and import.
5. **Packaging** — CMake Runtime installation excludes
   `sharedAssets/scripts` entirely (`PATTERN "scripts" EXCLUDE`).
   Standalone project script and asset packaging continues unchanged.
   The code is embedded in binary contents; this is **not encryption or
   copy protection**, and developers can still edit source and recompile.
6. **Tooling and typing paths** — updated script regression harnesses,
   `tools/dev/generate_input_typings.py`,
   `Projects/NorthStarFirstPersonPlatform/jsconfig.json`, and SDK docs
   to reference the canonical `engine/northstarcpp/resources/api` path.

## Verification

- `northstarcpp_player`: CMake/Ninja Release build **PASS**.
- `northstarcpp_editor_qt`: Release build **PASS**.
- `northstarcpp_asset_browser`: Release build **PASS**.
- `northstarcpp_editor_qt.exe --content-dock-smoke`,
  with `QT_QPA_PLATFORM=offscreen`: **PASS**; now explicitly checks
  `engine-api://scripts/northstar/bootstrap.js` and
  `engine-api://scripts/ui/UI.js`.
- `northstarcpp_asset_browser.exe --smoke` (offscreen): **PASS**.
- `northstarcpp_embedded_engine_api`: C++ smoke **PASS**. Checks 19
  resource records, byte-for-byte equality with current source,
  virtual catalog visibility, read-only filesystem refusal, and
  exclusion of historical backup entries.
- Script regression CTests: **7/7 PASS** for embedded SDK, input enums,
  Scene SDK, shared roots, prefabs, player footsteps and UI root.
- `tools/dev/generate_input_typings.py --check`: **PASS**.
- Test SDK install followed by `tools.build_dist.sdk_smoke`: **PASS**.
- `git diff --check`: **PASS**.

## Remaining / not claimed

- Existing source copies under `sharedAssets/scripts` are retained for
  rollback (the connected workspace currently disallows destructive
  operations); they are **not** consulted for core API execution, visible
  as ordinary engine assets or installed into the Runtime distribution.
  Final physical removal/migration of these 19 tracked source copies,
  along with their backups, is a separate repository cleanup.
- A legacy `tests/gameplay_scripts_smoke.cjs` harness still lacks native
  prefab support required by the now-expanded first-person pistol demo;
  it is not included among the passing tests above.
- The Qt code preview has been compiled and asset revelation tested in
  offscreen mode; interactive visual acceptance of the modal is pending.
- No production executable replacement, Git commit or push was performed.

## Reproduction

```powershell
python tools/dev/embed_engine_api.py --root engine/northstarcpp/resources/api --out build/embedded.cpp
python tools/dev/generate_input_typings.py --check
cmake -S . -B build/foundation-migration
cmake --build build/foundation-migration --target northstarcpp_player northstarcpp_editor_qt northstarcpp_asset_browser northstarcpp_embedded_engine_api_smoke
ctest --test-dir build/foundation-migration --output-on-failure -R northstarcpp_embedded_engine_api
```
