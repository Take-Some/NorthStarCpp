# NorthStarCpp — YMAT kind 30 loading and Win32 error dialog stabilization

> **Document status:** Historical incident (2026-11-14). The JSON-kind-30 loader described below was subsequently removed. Current canonical material format and regression evidence: [XML YMAT Migration](YMAT_XML_ONLY_MIGRATION_2026-11-18.md).

Date: 2026-11-14
Status: fixed in C++ source and rebuilt the isolated Release QA Player; installed production distribution unchanged.

## Reported failure

Running a NorthStarCpp game produced many identical nested modal errors:

    Expected a native YMAT material library (NEF8 kind 4)

## Verified root causes

1. `shared_asset_models.cpp` previously parsed only NEF8 kind 4 XML material libraries in shared_assets::detail::parse_materials. Imported character and weapon materials use NEF8 kind 30 with JSON schema `northstar.ymat.v1`. The shared-assets inventory already recognized kind 30, while the native material loader, virtual dictionary enumeration and material preview did not.
2. Win32 `App::procedure` displayed the error dialog before stopping the 16 ms frame timer. The modal MessageBox pumps Win32 messages; subsequent WM_TIMER callbacks could reenter the same failing frame and display another nested dialog before the first returned.

Verified source files:

- sharedAssets/materials/character_abby.ymat — NEF8 v2 kind 30, 35 materials
- sharedAssets/materials/weapon_pistol.ymat — NEF8 v2 kind 30, 4 materials
- sharedAssets/materials/skydome.ymat — NEF8 v1 kind 4, native XML material library

## Implementation

### Common material loader

File: engine/northstarcpp/modules/host/shared_asset_models.cpp

- Added support for JSON YMAT kind 30 to the existing native material reader.
- Validates schema, entry count, unique material names, shader, blend, sidedness and alpha cutoff.
- Decodes texture slot references and material parameters, including scalar and array values (float, float2/3/4, color), preserving texture references as authored.
- Keeps the original kind 4 XML parser and checks intact, with no rewrite of original NEF8 binary files.
- This enables AssetStore::load_material, asset directory enumeration, material selection and editor previews to recognize both native formats.

### Modal dialog recursion guard

File: engine/northstarcpp/modules/editor/window.cpp

- On a fatal Win32 procedure exception, marks the runtime faulted and kills the frame timer before opening a modal report.
- WM_TIMER no longer dispatches frame() during a fatal fault or while an error dialog is active.
- report() suppresses nested GUI errors, but continues writing diagnostics to stderr/debug output.
- Normal command error reporting and successful gameplay remain unchanged.

### Regression tests

- tests/material_resources_smoke.cpp now exercises real Abby and pistol YMAT files: kind 30 parse, @entry selection, Content Browser dictionary enumeration and source-bytes decoding. Original kind 4 YMAT checks remain.
- tests/editor_asset_thumbnails_smoke.cpp checks editor preview material source loading and a valid material sphere preview for both kind 30 libraries.
- engine/northstarcpp/CMakeLists.txt links the actual material preview and asset catalog source files into that isolated editor regression target.

## Verified QA (Windows x64; CMake + Ninja Release)

Isolated test tree: out/asset-streaming-qa-20261114.

| Check | Result |
| --- | --- |
| northstarcpp_material_resources | PASS |
| northstarcpp_editor_asset_pipeline | PASS |
| northstarcpp_shared_assets | PASS |
| northstarcpp_assets_vfs | PASS |
| Compiled updated Win32 Player window.cpp object | PASS |

Actual Player rebuild: out/fps-release-qa-20261114/northstarcpp_player.exe, 36/36 build operations PASS.

Real project tests, NorthStarFirstPersonPlatform:

- Projects/NorthStarFirstPersonPlatform/verify.cmd — PASS, exit code 0 (scene check and runtime asset initialization).
- QA Player --warp --character-smoke --explore --scene ... --shared-assets ... — PASS, exit code 0; output: `Project-defined textured skeletal draw: PASS`.

## Outstanding imported-resource dependencies

All YMAT binary inputs remain unchanged. Some native kind 30 material texture refs refer to packages not currently found in the checked-in shared asset tree:

- `materials/character_abby.ymat` references `textures/characters/abby.ytd`; the available existing texture dictionary is `textures/abby.ytd`. The standalone skeletal appearance path already accepts an explicitly supplied dictionary path.
- `materials/weapon_pistol.ymat` references `textures/weapon/pistol.ytd`; that source dictionary is not currently present under sharedAssets.

The editor preview degrades gracefully when source textures are unavailable; GPU material assignment that requires missing textures may still report an asset-resolution error. Reconcile these references with the original authoring/import pipeline rather than fabricating substitute YTD content.

## Deployment

- The isolated QA Player was rebuilt successfully; `Projects/NorthStarFirstPersonPlatform/run.cmd` already targets this QA Player executable.
- The installed production `dist` was not overwritten; existing running processes still need to be restarted to load new code.
- No Git commit or push was performed, preserving concurrent uncommitted project work.
- A live interactive reproduction of the former modal error cascade has not been executed after the fix; the Win32 message handler was compiled and real Player smoke tests passed.
