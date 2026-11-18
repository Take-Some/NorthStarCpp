# NorthStar Qt UI icons in NorthStar Pak

The `assetBrowser` and `playIcons` PNG assets are provided by the shared engine
NSP archive, not embedded in the Qt Editor or Asset Browser binaries.

Authoritative source artwork:
- `engine/northstarcpp/resources/sharedAssets/icons/assetBrowser/*.png` (20 files)
- `engine/northstarcpp/resources/sharedAssets/icons/assetBrowser/fileNui.svg` (editable source for `.nui`)
- `engine/northstarcpp/resources/sharedAssets/icons/playIcons/*.png` (9 files)

Older artwork in `engine/northstarcpp/resources/icons/` is legacy and is not authoritative.

## Size convention

Every Asset Browser PNG has a **256 x 256 transparent canvas**.
The visible artwork should fit a **225 px maximum dimension**, allowing for
anti-aliasing tolerance (approximately 222–227 px at 15% alpha threshold).
File-shaped icons are centered vertically; folder-shaped icons have the same
maximum dimension on the horizontal axis. Preserve the aspect ratio, shadows,
and alpha channel; do not stretch file icons to the folder proportions.

As of 2026-11-17, `fileNui.png` and `fileJs.png` were resized to the
same visible scale as the rest of the icon family. `fileNui.svg` uses a
larger viewBox to preserve this normalization on regeneration.

## Build and runtime contract

`tools/pack_nsp.py` invokes Qt `rcc --binary` to generate
`ui/northstar_ui_icons.rcc` automatically from the 29 source PNGs.
The RCC and individual images are packaged in `northstar_shared.nsp`,
typically deployed beneath `bin/archive/`.

`ensure_northstar_qt_icons()` loads and registers the RCC from the layered
`engine.assets` VFS. Existing Qt resource URLs remain stable:

- `:/northstar/icons/assetBrowser/folder.png`
- `:/northstar/icons/assetBrowser/materialFunc.png`
- `:/northstar/icons/assetBrowser/fileNui.png`
- `:/northstar/play/play.png`
- `:/northstar/play/pause-active.png`

The package also includes reusable `.nui` files under `ui/*.nui`,
including `ui/default_menu.nui`; project `assets/ui/*.nui` files may
override them through the VFS.

The icon loader logs warnings and falls back to placeholders when optional
artwork is unavailable. Windows PE application `.ico` resources remain
embedded separately, so they show correctly before engine startup.

To refresh icons, update source artwork, run the `northstarcpp_shared_nsp`
CMake target, deploy the refreshed archive, and restart the Qt application.
Icon updates do not need C++ relinking; Qt source changes do.

## Regression test

`northstarcpp_qt_nsp_icons` opens the built NSP, registers and decodes
all 20 Asset Browser icons and all 9 Play icons, and validates
`ui/default_menu.nui` through AssetStore.

```powershell
cmake --build <build-dir> --target northstarcpp_qt_nsp_icons_smoke
ctest --test-dir <build-dir> --output-on-failure -R northstarcpp_qt_nsp_icons
```
