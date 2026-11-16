# NorthStar Qt UI icons in NorthStar Pak

The `assetBrowser` and `playIcons` PNG resources are engine assets, not compiled Qt blobs.
Authoritative editable files:

- `engine/northstarcpp/resources/sharedAssets/icons/assetBrowser/*.png` (19 files)
- `engine/northstarcpp/resources/sharedAssets/icons/playIcons/*.png` (9 files)

Older files under `engine/northstarcpp/resources/icons/assetBrowser` and
`engine/northstarcpp/resources/icons/playIcons` are not used by the new build.
They remain as legacy copies and are not authoritative.

## Build and runtime contract

During compilation, `tools/pack_nsp.py` invokes the toolchain's
`rcc --binary` to generate `ui/northstar_ui_icons.rcc` from these PNGs.
The binary RCC and the 28 source PNGs are packaged in `northstar_shared.nsp`
and installed beneath `bin/archive/`. These icons are not compiled into
the Qt Editor or Asset Browser executables.

The common `ensure_northstar_qt_icons()` function uses the `engine.assets`
VFS provider to discover NSP archives beside the runtime and load the RCC
resource from the active VFS. The RCC bytes persist until process exit.
The existing Qt resource URLs are unchanged:

- `:/northstar/icons/assetBrowser/folder.png`
- `:/northstar/icons/assetBrowser/materialFunc.png`
- `:/northstar/play/play.png`
- `:/northstar/play/pause-active.png`

If the archive or artwork is invalid, the loader logs a warning instead
of terminating the editor. Qt can use normal placeholder icons.

To change the artwork, update the PNGs under `sharedAssets/icons`, rebuild
the `northstarcpp_shared_nsp` CMake target and restart the Qt application.
The .exe and native DLLs do not need to be relinked.

**Windows exception:** the application `.ico` inside PE resources must stay
embedded so the executable has an icon in Explorer before the engine starts.
This does not apply to the Qt UI PNG artwork.

## Regression tests

The `northstarcpp_qt_nsp_icons` test opens the real NSP, registers RCC
and decodes all 28 PNGs through unchanged Qt URLs. Related regression tests
cover collisions, NAC/category icons, and the editor Content Browser dock.

```powershell
cmake --build out/nsp-ui-qa-20261116 --target northstarcpp_qt_nsp_icons_smoke
ctest --test-dir out/nsp-ui-qa-20261116 --output-on-failure -R northstarcpp_qt_nsp_icons
```
