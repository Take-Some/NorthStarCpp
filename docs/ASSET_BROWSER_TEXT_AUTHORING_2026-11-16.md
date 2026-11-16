# Asset Browser: Markdown preview and JavaScript/GLSL source editor

Date: 2026-11-16
Workspace: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`

## Integrated behavior

The native `QtContentBrowser` now routes physical `.md` / `.markdown` assets to a Markdown preview dialog with **Preview** and **Markdown Source** tabs. Qt's Markdown renderer is used, with a dark theme and local-relative image base URL. The source tab is read-only, and links are not opened automatically.

Physical `.js`, `.vert` and `.frag` files open in the unified Qt code dialog. Project assets support editing and saving; Engine Shared Assets are read-only. Embedded Engine API script entries continue to use their existing immutable-source viewer.

Editor MVP:
- Monospace source view with line numbers, four-space tab stops, current line highlighting and no wrapping.
- Lexical syntax highlighting for JavaScript and GLSL, including block/line comments, strings, numeric literals and common keywords.
- Source dirty state, reload, undo/redo through Qt's text document, Ctrl+S and Save.
- UTF-8 input validation, UTF-8 BOM preservation and original CRLF/LF style on save.
- Atomic save using `QSaveFile`; refuses silent overwrite if the underlying file was changed externally.
- Rejects binary inputs and files exceeding 8 MiB.

The Asset Catalog recognizes `.vert` and `.frag` as Shader assets. Card and Details icons come from bundled Qt resources:
- `.md` / `.markdown`: `fileMd.png`
- `.js`: `fileJs.png`
- `.vert`: `vertShader.png`
- `.frag`: `fragShader.png`

`fileJs.png` was normalized from 1254×1254 to 256×256 RGBA with transparency. An original copy was stored in `_qa_content_browser_p2/fileJs_original_1254.png`.

## Relevant files

- `engine/northstarcpp/modules/editor/qt_text_asset_editor.hpp`
- `engine/northstarcpp/modules/editor/qt_text_asset_editor.cpp`
- `engine/northstarcpp/modules/editor/qt_content_browser.cpp`
- `engine/northstarcpp/modules/editor/asset_catalog.cpp`
- `engine/northstarcpp/CMakeLists.txt`
- `tests/qt_text_asset_editor_smoke.cpp`

## Verification

QA build `_qa_content_browser_p2`:
- `northstarcpp_content_browser_qt` — built.
- `northstarcpp_qt_text_asset_smoke` — built; offscreen UI smoke **PASS (0 failures)**. Checks catalog classification, Qt resource icons, live asset-card icons, Markdown view, JS atomic save, BOM/CRLF, and shared resource read-only behavior.
- `northstarcpp_asset_browser_cards_qa` — built.
- `northstarcpp_asset_browser_cards_qa --category-icons-smoke` — **PASS** (grid/list/details).
- `git diff --check` on changed tracked engine files — passed (line-ending warnings only).

The main Qt Editor build did not complete. Linking `northstarcpp_player.exe`, a dependency of `northstarcpp_editor_qt`, failed with `Permission denied` because the executable is locked. No production binary was replaced; no commit or push was made. The working tree contains substantial unrelated pre-existing modifications, which were not reverted.

## Next increments

1. Move the text editor from a modal into dockable, multiple-document Qt editor tabs, with an unsaved-file navigation guard.
2. Add indentation and bracket matching, search/replace, completion, API symbol navigation and diagnostics.
3. Connect GLSL validation to `glslc` and output compiler diagnostics without replacing the saved file.
4. Add a Markdown/HTML source-preview synchronization option and safe local-asset links.
5. Verify desktop rendering and DPI scaling, rebuild the main editor once the locked Player executable is released, then run Release/regression tests.
