# NorthStar Editor — Native Qt Content Browser

## Purpose

The new Content Browser is a custom-styled Qt 6 Widgets application written in C++. It does not use Win32 `HWND` child controls, Windows TreeView/ListView, `SHGetFileInfoW`, or Windows native file dialogs. Its reusable UI is `QtContentBrowser : QWidget`; its catalog `AssetCatalog` has no Qt/Win32 dependency.

This is an initial Unreal-style editor asset workspace, **not feature parity with Unreal Engine Content Browser**. The legacy Win32 scene editor still exists; the Qt browser currently opens in its own window. Embedding it as a dock/tab inside a wholly Qt-based editor shell requires the remaining editor UI migration.

## Available UI

- Sources/folders tree and project-scoped Content path.
- Resizable asset grid, switchable compact list, asset-type icons drawn by the editor, zoom slider, and selected-asset details.
- Global search over indexed relative asset paths, folder navigation, Back/Forward/Up, category filters, Refresh.
- Native Qt import dialog (not the Windows native file dialog), new folder, rename, duplicate, nonrecursive delete with confirmation, folder reveal via cross-platform QDesktopServices.
- Virtual asset references copied to clipboard; drag uses the MIME type `application/x-northstar-asset-reference`.
- Asynchronous catalog indexing, image previews for ordinary images, and provider-based preview generation for decoded YTD/YDD/YMAT assets using `engine.assets`.

## Asset backend

`include/northstarcpp/editor_asset_catalog.hpp` and `engine/northstarcpp/modules/editor/asset_catalog.cpp` provide an independent C++17 asset index. Folder symlinks are never traversed, project-relative path traversal is rejected, and scan count is bounded to 100,000 records. The widget explicitly reports truncated indexes.

Thumbnails are generated off the GUI thread through QtConcurrent with a bounded pool. The widget limits source preview files to 128 MiB and caches no more than approximately 480 previews in memory. The existing `editor_tools` YTD/YDD/YMAT decoders are retained; Windows system icons are not used.

This is a local-content index, not yet a persistent hashed Asset Registry, Derived Data Cache, dependency database, or cooked asset browser.

## Starting it

From the **Win32 compatibility editor**, choose **View > Open Native Content Browser** or press **Ctrl+Shift+B**.

Direct invocation:

```powershell
.\northstarcpp_asset_browser.exe --project "C:\path\to\project\main.nscene"
.\northstarcpp_asset_browser.exe --content "C:\path\to\project\assets"
```

With no project selected, use the **Open Content** toolbar button. The process can be used standalone during the Qt editor migration. The legacy Win32 asset pane is hidden by default; its compatibility toggle remains available for Bind Asset / Place Mesh operations that have not yet been wired to the Qt browser.

## Build and verification

```powershell
cmake --build out/native-content-final-release-20261114 --target northstarcpp_asset_browser northstarcpp_asset_catalog_smoke
ctest --test-dir out/native-content-final-release-20261114 -R "^northstarcpp_(asset_catalog|content_browser_qt)$" --output-on-failure
```

Both tests passed. The packaged developer runtime was also tested with a restricted PATH excluding MSYS2, including `Qt6Concurrent.dll` and the Qt Windows platform plugin. A screenshot of the sample project's real asset directory was generated through the Qt host.

## Portability and remaining work

Qt widgets and the catalog are platform-neutral at the source level; however, the current NorthStar editor host and its D3D11 render backend remain Windows-specific. This is not a verified Linux/macOS editor build.

To achieve the Unreal-style in-editor UX, the next architecture milestone is a Qt editor shell with the Content Browser docked into the same `QMainWindow` as Scene View, World Outliner, Inspector, and tool panels; drag/drop must then bind, instantiate or import assets through current engine contracts. The current standalone Qt browser does **not** yet perform direct actor binding across the process boundary. GPU material preview, dependency graph, thumbnails DDC, asset cooking, and undoable content operations are also pending.

The old Win32 editor binary must be fully rebuilt and smoke-tested to verify menu integration. Do not describe the whole engine or distribution as cross-platform or released until that happens.
