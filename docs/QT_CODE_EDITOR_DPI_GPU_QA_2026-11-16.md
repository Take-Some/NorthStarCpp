# Qt Editor DPI / large-document acceptance and Material Function GPU QA

Date: 2026-11-16  
Workspace: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`  
Isolated Release tree: `build/foundation-migration`.

## 1. Native Windows text editor QA

A new test source, `tests/qt_code_editor_dpi_scroll_smoke.cpp`, creates
the **real shared `text_assets::createSourceEditor` control** on Qt's
**Windows QPA platform**, not `offscreen`. It loads 50,000 lines of
JavaScript (50,001 text blocks due to the final newline) and verifies:

- Editor creates one syntax highlighter and a five-digit line-number gutter.
- Native wheel event advances the scroll position.
- `Ctrl+End` brings the last text block into the visible viewport.
- `Ctrl+Home` returns to the beginning.
- The gutter resizes correctly after changing the editor font.
- The rendered images contain actual horizontal-rule pixels and visible
  painted gutter characters, not merely enabled widget properties.
- Captured screenshots retain device-pixel dimensions at scaled DPI.
- 128 scroll movements are followed by actual Qt event processing and
  QWidget viewport capture. Values are recorded as submission / Qt paint
  elapsed milliseconds, **not** renderer GPU frame times.

The `CodeEditor` implementation in
`modules/editor/qt_text_asset_editor.cpp` now invalidates and recomputes
its gutter width, geometry and tab stop width on font/application-font/
style changes.

### Recorded runs (Windows QPA)

| `QT_SCALE_FACTOR` | DPR | Result | Load (ms) | Scroll p50 (ms) | Scroll p95 (ms) | Scroll p99 (ms) |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| 1 | 1.0 | PASS | 254 | 2.77 | 4.07 | 5.07 |
| 1.5 | 1.5 | PASS | 254 | 3.32 | 5.65 | 5.88 |
| 2 | 2.0 | PASS | 254 | 3.99 | 5.17 | 5.30 |

The reported values reflect three specific QA runs and are not a
reproducible performance comparison across hardware.

The test saved **12 screenshots** (top, bottom, large-font and home states
at each scale) to:

`build/foundation-migration/qa/code-editor-dpi/`

The screen display on a human-operated monitor and multi-monitor DPI
transitions were not manually inspected. Native Windows screenshot raster
pixels, Ctrl+Home/End and simulated wheel events **were** checked.

## 2. Material Function GPU QA

Root cause of the original `GPU material function surface unavailable`
result: `QT_QPA_PLATFORM=offscreen` cannot create the Win32 native HWND /
GPU swapchain needed by `QtEditorViewport`. Running the original test
with `QT_QPA_PLATFORM=windows` succeeded.

While relinking the newer QA editor, an independent background asset
indexing fault also surfaced. GDB backtrace showed a SIGSEGV in
`boost::property_tree::json_parser::read_json_internal` from:

`shared_assets::detail::parse_json` ->
`shared_assets::detail::parse_list_file_v2` ->
`AssetCatalog::scan` ->
`QtContentBrowser::refresh` in a QtConcurrent worker.

The **GPU smoke should not initialize the entire QtEditorWindow** for
its isolated GPU material fixture. Two changes now enforce that:

1. `qt_editor_main.cpp` handles `--material-function-smoke` immediately
   after Qt argument parsing, before creating `QtEditorWindow`.
2. The official CTest `northstarcpp_qt_material_function_gpu` now
   launches **`northstarcpp_material_function_gpu_smoke.exe`**, built
   separately from `tests/qt_material_function_gpu_smoke_main.cpp`,
   `qt_material_function_smoke.cpp` and `qt_material_gpu_preview.cpp`.
   It sets `QT_QPA_PLATFORM=windows` and `RUN_SERIAL=TRUE`.

This preserves all real GPU acceptance steps: compiled function, staging,
native GPU viewport draw, rejected invalid function change while the
existing material stays active, and a successful recompile/recovery.

### Verification

From the isolated Release build:

- `northstarcpp_qt_text_asset` — PASS.
- `northstarcpp_code_editor_smoke` — PASS.
- `northstarcpp_code_editor_dpi_scroll` — PASS.
- `northstarcpp_qt_material_function_gpu` — PASS.
- `ctest --repeat until-fail:3 -R '^northstarcpp_qt_material_function_gpu$'`
  — three consecutive PASS results.

The outer CTest process was deliberately configured with
`QT_QPA_PLATFORM=offscreen` to demonstrate that its dedicated GPU test
sets the correct native Qt platform independently.

## 3. Follow-up / residual issue

- **Separate Asset Browser fault:** the latest non-isolated QA editor
  still has a reproducible background NEF8/list-file scan crash, described
  above. It is not a Material Function GPU-surface failure. The current
  editor `--smoke` also reported an `NEF8 actor parent roundtrip`
  failure in the latest isolated QA build. These deserve dedicated P0
  crash and serialization regression investigation.
- Interactive manual visual inspection on multiple physical monitors,
  with DPI changes *while dragging an already-open editor window*,
  remains to be performed. Tests cover 100/150/200% process-level DPI.
- No Night City GPU-frame p50/p95/p99 or total repository regression
  is claimed by this specialized Qt test suite.
- The running editor executable was left untouched. No production
  install/replacement or Git commit/push was performed.

## Reproduce

```powershell
cmake --build build/foundation-migration --target northstarcpp_code_editor_dpi_scroll_smoke northstarcpp_material_function_gpu_smoke
ctest --test-dir build/foundation-migration --output-on-failure -R "^(northstarcpp_qt_text_asset|northstarcpp_code_editor_smoke|northstarcpp_code_editor_dpi_scroll|northstarcpp_qt_material_function_gpu)$"
ctest --test-dir build/foundation-migration --repeat until-fail:3 -R "^northstarcpp_qt_material_function_gpu$"
```
