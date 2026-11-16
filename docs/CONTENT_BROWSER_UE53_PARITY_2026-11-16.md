# NorthStar Content Browser — UE 5.3.2 functional parity pass
Date: 2026-11-16
Project root: C:\\Users\\Kayla\\Documents\\NorthStar\\NorthStarCpp

## Source reference and compatibility

Source reviewed from the exact repository supplied by the project owner:
- https://github.com/Mccrae123/Unreal-Engine-5.3.2
- `Engine/Source/Editor/ContentBrowser/Private/SContentBrowser.cpp`
- `Engine/Source/Editor/ContentBrowser/Private/SAssetView.cpp`
- `Engine/Source/Editor/ContentBrowser/Private/SPathView.cpp`
- `Engine/Source/Runtime/Launch/Resources/Version.h` confirms 5.3.2.

UE separates content navigation, asset tiles, sources, favorites, filters,
search and view settings. NorthStar reproduces selected behaviors with
**its own Qt6 C++ implementation**. No UE proprietary code has been
copied into the GPLv3 NorthStar engine.

## P0: Dock layout stability

- `qt_content_browser.cpp`: asset metadata and preview controls are
  contained in `assetDetailsScroll`, a vertically independent
  `QScrollArea`. Long material-function and prefab metadata scroll within
  the inspector, instead of adding minimum height to the entire editor dock.
- Preview image has stable height; asset name height is bounded and exposes
  the complete name as a tooltip. Parent Content Browser remains manually
  resizable/dockable via QMainWindow.
- No forced `contentDock_->setFixedHeight()`: a user can resize the dock,
  while changing selection or waiting for asynchronous previews cannot
  spontaneously resize it.

## Features added or refined

| Capability | NorthStar implementation |
| --- | --- |
| Navigation / breadcrumb | Clickable root and intermediate folders in the current source |
| Favorites | Dedicated tree section; project + engine paths; persistent QSettings keyed per source root; optional Ctrl+D shortcut |
| Sort | Natural, numeric-aware Name ascending/descending, type, size ascending/descending; folders first |
| Search | AND-combined free-text and `name:`, `path:`, `type:`, `ext:` terms |
| View options | Show/hide sources and details, reset pane widths; visibility persisted |
| Keyboard | Ctrl+F search, F5 refresh, F2 rename, Ctrl+D favorite, Alt+Left/Right history |
| Asset safety | Read-only shared engine mount, NEF8 embedded children unchanged; project-only file mutations preserved |
| Selection height | Long metadata remains scrollable in the inspector with invariant QDockWidget height |

## Regression verification

Build target:
```powershell
ninja -C _qa_editor_mixed -j 4 northstarcpp_asset_browser
```
The build passed and linked `northstarcpp_editor_qt.exe` as a dependency.

Targeted QA:
```powershell
ctest --test-dir _qa_editor_mixed -R 'northstarcpp_(content_browser_stable_dock_qt|collision_icons_qt|nac_icons_qt|embedded_ycd_ymt_icons_qt|asset_category_icons_qt|folder_icons_qt|asset_catalog_index|nef8_content_browser_qt)' --output-on-failure
```
**8 of 8 passed** (2026-11-16). The new dock smoke creates an ordinary
file and a Material Function with 100 parameters plus a project folder,
then checks stable dock and browser heights, internal metadata scrolling,
folder-first natural sort, structured search and persisted Favorites
toggle through its actual shortcut.

## Remaining UE 5.3.2 parity work

This is an incremental functional-parity pass, **not full Unreal Engine
Content Browser equivalence**. Missing or incomplete functionality includes:
UE-style Collections, full advanced query language and suggestion engine,
source control status/actions, asynchronous asset registry metadata,
multi-selection and large-batch workflows, asset migration/dependency
repair, Content Drawer vs permanently docked browser, multiple independent
browser instances, and equivalent user-level Save All behavior.

These must use NorthStar's asset/project/provider contracts, not directly
port proprietary Unreal code. Runtime asset providers and engine source
code remain under their own licenses.

No production distribution binaries were replaced and no Git
commit/push was made.
