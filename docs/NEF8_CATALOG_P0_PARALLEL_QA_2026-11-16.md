# P0: NEF8 catalog parallel indexing + Qt Editor scene-parent QA

Date: 2026-11-16
Project: NorthStarCpp, isolated `build/foundation-migration`
No Git commit/push, no production binary installation or running-editor termination.

## Reproduction and diagnosis

A reproducible crash was isolated from the Qt Editor with
`tests/asset_catalog_nef8_parallel_smoke.cpp`.

The test reads real shared engine and NorthStarFirstPersonPlatform assets,
performs repeated scans from multiple concurrent workers, and tests an
isolated NEF8 dictionary with deliberately invalid optional JSON metadata.
No original project assets are modified.

Before the parser fix:
- 1 worker, six scan rounds: PASS.
- 2 workers: Windows SIGSEGV (`0xC0000005`).
- 4 workers: Windows SIGSEGV.
- GDB confirmed the stack:
  `boost::property_tree::json_parser::read_json_internal` →
  `shared_assets::detail::parse_json` →
  `shared_assets::detail::parse_materials` →
  `shared_assets::list_dictionary_entries` →
  `editor::detail::inspect_dictionary` →
  `AssetCatalog::scan`.

This crash was not caused by the original scene serialization code.

## Changes

1. `engine/northstarcpp/modules/host/asset_list_file.hpp` and
   `asset_list_file.cpp`: internal `parse_list_file(..., include_metadata)`
   retains the full JSON metadata behavior by default. Indexing callers can
   parse the verified envelope and body without decoding auxiliary metadata.
   File range checks, raw DEFLATE, and BLAKE3 validation remain enabled.
2. `engine/northstarcpp/modules/host/shared_asset_dictionary_index.cpp`:
   virtual dictionary listing invokes `parse_list_file(source,false)`.
   It still validates and interprets the typed dictionary body. This avoids
   an unnecessary parse for catalog-only reads.
3. `asset_list_file.cpp`: synchronized the shared Boost.PropertyTree
   JSON entry point with a process-local mutex, addressing the observed
   concurrent parser crash. Independent input streams and trees remain
   local; the guard covers only the parser call. The guarded path remains
   needed by actual material JSON and other full-load callers.
4. `tests/asset_catalog_nef8_parallel_smoke.cpp` plus CMake target/CTest:
   deterministic parallel regression (default four workers and six rounds,
   override workers via `NORTHSTAR_CATALOG_STRESS_WORKERS`).
5. `engine/northstarcpp/modules/editor/qt_editor_window_smoke.cpp`:
   split ambiguous `saveScene() || openScene() || parent` failure
   into explicit save, reload, actor index, and parent ID diagnostics.

## Results after fix

| Workers | Six rounds per worker | Result |
| ---: | ---: | --- |
| 1 | 6 | PASS |
| 2 | 12 | PASS |
| 4 | 24 | PASS |
| 8 | 48 | PASS |

The eight-worker run was repeated three times: 144 total scan rounds,
all PASS. The full Qt Editor `--smoke` was run three times on a coherent
QA link: all three PASS, including NEF8 parent ID roundtrip.

Seven targeted CTest cases: **7/7 PASS**:
- `northstarcpp_qt_text_asset`
- `northstarcpp_code_editor_smoke`
- `northstarcpp_code_editor_dpi_scroll`
- `northstarcpp_asset_catalog`
- `northstarcpp_asset_catalog_nef8_parallel`
- `northstarcpp_qt_material_function_gpu`
- `northstarcpp_scene_chunked`

The main editor binary in `build/foundation-migration` remained locked
by an existing process. A separate QA build
`build/foundation-migration/northstarcpp_editor_qt_nef8_parent_qa.exe`
was linked from the updated objects after building the matching runtime,
viewport, and Qt libraries. The earlier NEF8 parent roundtrip failure
could not be reproduced with this coherent build. We do not claim a
scene codec change was necessary.

## Residual scope

- No physical multi-monitor DPI dragging/monitor-transfer QA: detected
  system configuration reports an AOC 1920x1080 monitor only.
  Existing synthetic/native Windows process DPI tests (100/150/200%)
  do not constitute manual cross-monitor inspection.
- This is a focused P0 scenario regression, not whole-engine Release CTest
  or a long-duration interactive editor stability guarantee.
- The separate `northstarcpp_scene_binary` legacy format regression was
  observed to return exit 5 (compatibility fixture check) when run in this
  environment; it is distinct from NEF8 hierarchy serialization.
