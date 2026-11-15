# ContentBrowser — Engine-Owned Qt Editor Dock
Date: 2026-11-15

## Architecture

ContentBrowser is an engine editor component, not a separate authoring application.

Existing implementation retained and strengthened:
- `northstarcpp_content_browser_qt` remains the reusable C++/Qt6 library.
- `QtEditorWindow` owns exactly one `QtContentBrowser` widget as
  `QDockWidget("Content Browser")`, with `contentDock` identity.
- It is displayed in the Editor's bottom dock area, tabbed with Output Log,
  and can be moved to left/right but cannot become a floating top-level window.
- The Editor's **Assets → Show Content Browser** shortcut is
  **Ctrl+Shift+B**. **Find Selected Actor Asset** reveals the selected
  actor's asset in the integrated ContentBrowser.
- Project Manager launches `northstarcpp_editor_qt.exe --scene ...`
  and therefore uses the integrated ContentBrowser, not a separate process.

## Lifecycle, indexing and project ownership

- On scene load/save-as, the dock is bound to the scene's sibling
  `assets/` directory with `setProjectScene()`.
- The Editor owns the root. Its integrated ContentBrowser disables and
  hides the arbitrary root selector, retaining search, browse, grid,
  filtering, material editing, thumbnails and drag/drop.
- Loading a different project immediately invalidates previous catalogs
  and thumbnails before an asynchronous rescan. Old results are rejected
  using generation tokens.
- A scene without a project clears the catalog. An empty path never
  resolves to the process working directory and cannot accidentally index
  the whole source checkout.
- `revealAsset()` resolves paths and NEF8 references, clears filters and
  selects/scolls to the asset. Requests during async indexing are delayed
  until the new project catalog is ready.
- Legacy floating ContentBrowser state is migrated back to an embedded
  dock on workspace restoration and on explicit panel activation.

## Compatibility

- The legacy Win32 Editor's old "Show Content" command is routed to
  `northstarcpp_editor_qt.exe --focus-content-browser`, optionally
  forwarding the active scene.
- Direct interactive launch of `northstarcpp_asset_browser.exe`
  is a compatibility launcher that opens the integrated Qt Editor.
  Direct `--project` forwards the scene; `--content` searches for a
  project scene under the containing folder rather than opening arbitrary
  independent content.
- The standalone Qt Asset Browser host remains executable only in its
  explicit smoke/asset-card/folder-icon/prefab-preview/screenshot QA modes.
  Packaging still includes the legacy executable for compatibility;
  removing that executable requires a separate packaging and deployment
  contract migration.

## Verified acceptance

QA directory: `out/ymat-entry-editor-qa-20261115`.

- Built `northstarcpp_editor_qt` from source (PASS).
- Built `northstarcpp_asset_browser` compatibility/QA host (PASS).
- The editor and browser code also passed standalone C++ syntax checks.
- `northstarcpp_content_browser_dock_qt`: PASS. Opens two project scenes
  with distinct scripts, proves root isolation, dock ownership, no floating,
  deferred reveal after async index, and empty scene cleanup.
- `northstarcpp_content_browser_qt`: PASS.
- `northstarcpp_nef8_content_browser_qt`: PASS.
- `northstarcpp_camera_studio_qt`: PASS.
- Targeted CTest suite: 4/4 tests passed, 0 failed.
- `git diff --check`: exit code 0 (only unrelated line-ending warnings).
- No production dist install, Git commit or push.

## Not claimed

No full release packaging test was run. All GPU material preview cases and
large external project catalogs have not been benchmarked in this scope.
The legacy compatibility executable still exists in current package layouts,
but normal authoring no longer opens it as a second asset management window.
