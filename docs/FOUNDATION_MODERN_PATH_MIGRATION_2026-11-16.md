# NorthStarCpp — active foundation migration (2026-11-16)

## Scope

Move actively used historical C++ foundation dependencies to maintained module
locations without deleting historical source archives or changing engine ABI.

Historical archive: `engine/northstarcpp/modules/core/northstarcpp/`
(2,976 tracked files, approximately 23.97 MiB). This archive remains intact.

## Completed

1. Copied **64 reachable foundation headers**, including their transitive
   include closure, into `engine/northstarcpp/modules/api/include/northstarcpp/`.
   The files include math, geometry, compiler/platform macros, basic strings,
   paths, and the standalone math compatibility header.
2. Moved the active **Shader Generator** build source to
   `tools/shader_generator/` (`shader_compiler.cpp`,
   `shader_generator_entry_point.cpp`, `pch.h`).
   Historical originals remain available for provenance.
3. Updated the root `CMakeLists.txt` to compile the tool from its maintained
   location, resolve active foundation headers via the public API path and
   **stop installing the entire historical archive** into the SDK.
4. Removed the historic `modules/core` include directory from
   `northstarcpp_math`, `northstarcpp_core` and the portability smoke.
   The three source files under `modules/geometry` and `modules/time`
   that need the active foundation PCH now explicitly include
   `../core/pch.h`; this is not a reference to the historical archive.
5. Updated layout tests and foundation inventory checks. Added the
   `northstarcpp_foundation_header_migration` CTest, backed by
   `tools/migrate_active_foundation_headers.py --verify`.
6. Updated SDK-layout documentation.

## Verification

- `python tools/migrate_active_foundation_headers.py --verify`: PASS,
  **64 reachable headers, 0 missing, 0 conflicts**.
- `python tools/verify_foundation_runtime.py --json`: PASS.
- `cmake -DROOT=<repository> -P tests/engine_layout_smoke.cmake`: PASS.
- Clean `cmake -S . -B build/foundation-migration -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF`: PASS (GCC 16.2).
- Clean-build targets `northstarcpp_math`, `northstarcpp_core`,
  `northstarcpp_portability_smoke`, `shader_generator`: PASS.
- `northstarcpp_portability_smoke.exe`: PASS.
- Clean-build targets `northstarcpp_models`, `northstarcpp_animation`,
  `northstarcpp_blake3`: PASS.
- Clean-build D3D11 render provider: PASS.
- Examined generated command graph for foundation and Shader Generator:
  **0 command references** `modules/core/northstarcpp/`.

## Release follow-up verification — 2026-11-16

The previously pending build, install and SDK consumer checks have been completed
in an isolated Ninja/GCC 16.2 Release build:

- `northstarcpp_runtime`: **PASS**, `libruntime.a` linked.
- `northstarcpp_viewport`, `northstarcpp_projects`, `northstarcpp_player`:
  **PASS**.
- `northstarcpp_editor_qt`: **PASS**, all 58 target build steps complete.
- `northstarcpp_editor_qt.exe --smoke` with `QT_QPA_PLATFORM=offscreen`:
  **PASS**, process exit 0; interactive desktop rendering remains unverified.
- Reconfigured with `BUILD_TESTING=ON`; removed the duplicate
  `northstarcpp_scene_script_sdk` CTest registration.
- Seven selected CTests: **7/7 PASS** (legacy_random32, engine_layout,
  architecture_layout, foundation_inventory, foundation_header_migration,
  math, foundation_original).
- Installed SDK: `cmake --install build/foundation-migration --component SDK
  --prefix build/foundation-sdk-test`: **PASS**, 162 installed headers and
  nine static libraries.
- External standalone consumer: existing `tools.build_dist.sdk_smoke`
  compiles, links and executes against the installed SDK: **PASS**.
- `NorthStarCppConfig.cmake.in` now recreates the exported pkg-config
  FreeType/HarfBuzz imported targets if font shaping is enabled. The external
  consumer test initially caught this real packaging defect and passed after
  the fix.
- Generated Ninja graph, `BUILD_TESTING=ON`: 1,062 commands, **0**
  references to `modules/core/northstarcpp/`.
- Removed leftover `northstarcpp_engine_sources` include search paths in
  engine CMake; the current runtime, shader tool, editor and provider targets
  consume maintained paths.

## Historical archive retirement: NOT YET SAFE

The original `engine/northstarcpp/modules/core/northstarcpp/` is kept
unchanged. Although compiled targets no longer use its files, maintenance
tests and scripts still refer to that directory, including:

- `tests/engine_layout_smoke.cmake`
- `tests/test_northstarcpp_branding.py`
- `tools/verify_architecture.py`
- `tools/verify_foundation_runtime.py`
- `tools/migrate_compat_to_engine.py`
- `tools/organize_engine_layout.py`
- `tools/migrate_active_foundation_headers.py`

These archival assumptions must be replaced by maintained-header validation
or retired explicitly before removal. The history/provenance and relevant
third-party notices must also be preserved. This follow-up intentionally
did **not** delete the 2,976 tracked archive files.

The existing `_qa_editor_mixed` Ninja tree had an unrelated
`failed recompaction: Permission denied` error; all successful isolated
checks use `build/foundation-migration`. No production executable was
replaced, and no Git commit/push was performed.

The clean SDK install was a test install under `build/foundation-sdk-test`,
not deployment into a system-wide or production SDK directory.

## Repeatability

To refresh copied header dependencies after active code changes:

```powershell
python tools/migrate_active_foundation_headers.py --copy --json
python tools/migrate_active_foundation_headers.py --verify
```

The migration utility never overwrites a divergent maintained API header.
Treat any conflict as a code-review event, not an automatic replacement.
