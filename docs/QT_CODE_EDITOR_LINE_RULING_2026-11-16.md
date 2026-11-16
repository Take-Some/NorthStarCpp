# Qt text source editors — syntax highlighting, line numbers, row ruling

Date: 2026-11-16
Scope: NorthStarCpp Qt Editor / Asset Browser text authoring surfaces.

## Root cause

The regular source editor in `qt_text_asset_editor.cpp` already contained
`CodeEditor` (a `QPlainTextEdit` with a line-number gutter) and
`CodeHighlighter`. However, the embedded Engine API viewer in
`qt_content_browser.cpp` created an unrelated plain `QPlainTextEdit`.
The Material Function JSON source editor used another `QTextEdit`, and
Git status/diff and ECS hex payload used separate unstyled widgets.

## Implemented

- Exposed `text_assets::createSourceEditor(parent,format,readOnly)` as a
  single reusable source-control factory. It installs the same
  `CodeEditor` gutter and `CodeHighlighter` in editable and read-only
  contexts. The optional NorthStar API IntelliSense remains enabled for
  editable JavaScript/TypeScript only.
- Source formats:
  - JavaScript/JSX, TypeScript/TSX, Vertex GLSL, Fragment GLSL.
  - JSON/JSONC, Markdown (source and preview), plain TXT/hex text.
- Current-line gutter emphasis and a subtle full-width horizontal line
  after each visible text block (`#273543` on the dark editor theme).
  Rendering loops through **visible** blocks only, not the entire file.
  Plain text also has line numbers and ruling, without false color tokens.
- Converted all applicable text editors/views:
  - main source editors and multi-tab Qt workspace;
  - embedded engine API JavaScript and TypeScript declaration preview,
    still read-only;
  - Material Function JSON source tab;
  - Git diff/status text dialog, read-only;
  - editable ECS component hexadecimal payload.
- `Format::TypeScript`, `Format::Json`, and `Format::Plain` added
  to the text editor format classifier. No engine scripting ABI changed.
- New standalone `northstarcpp_code_editor_smoke` CTest checks all
  seven source format modes, active gutter, syntax format ranges,
  rendered horizontal-rule pixels, and read-only operation.

## Build / verification

- Target `northstarcpp_code_editor_smoke`: build PASS.
- Qt tests `northstarcpp_code_editor_smoke` and
  `northstarcpp_qt_text_asset`: **2/2 PASS** on Qt offscreen.
- Modified C++ source files in the Qt Editor and Asset Browser
  compiled successfully.
- The existing `build/foundation-migration/northstarcpp_editor_qt.exe`
  was locked by a **running process**, so normal Ninja linking could not
  overwrite it (`Permission denied`). It was deliberately left alone.
- Re-ran the generated link command using the same compiled objects with
  an alternate output file:
  `build/foundation-migration/northstarcpp_editor_qt_code_editor_qa.exe`:
  link PASS; `--smoke` PASS; `--content-dock-smoke` PASS.
- Compiled standalone Asset Browser main and linked separately as
  `build/foundation-migration/northstarcpp_asset_browser_code_editor_qa.exe`;
  `--smoke --content sharedAssets` PASS.
- `git diff --check`: PASS (only expected CRLF normalization warnings).

## Acceptance follow-up, 2026-11-16

The former DPI/large-document and GPU-smoke limitations have received
dedicated native Windows QA:

- 50,000 JS lines, 100% / 150% / 200% DPI, native wheel input, Ctrl+End,
  Ctrl+Home, line gutter/font resizing, screenshot pixel checks: PASS.
- Native GPU material-function staging/draw, rejected edit, hot reload and
  recovery: PASS using the dedicated GPU CTest executable.
- The four focused Qt tests pass; three repeated GPU executions also pass.

See [QT_CODE_EDITOR_DPI_GPU_QA_2026-11-16.md](QT_CODE_EDITOR_DPI_GPU_QA_2026-11-16.md)
for evidence, measured p50/p95/p99 GUI scroll timings, and screenshots.

Residual: physical multi-monitor/manual DPI-drag inspection is not
performed. A separate Asset Browser NEF8 background indexing crash was
identified during the newer Editor QA and remains open. Neither production
binary replacement nor Git commit/push was performed.
