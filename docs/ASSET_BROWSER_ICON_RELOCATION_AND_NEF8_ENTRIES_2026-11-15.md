# Asset Browser PNG relocation and YMT/YCD entry icons

Date: 2026-11-15
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp

## Asset layout
All 11 PNG icon files formerly in
engine/northstarcpp/resources/icons/
were moved byte-for-byte to
engine/northstarcpp/resources/icons/assetBrowser/:

- folder.png
- texturesFolder.png
- modelsFolder.png
- materialsFolder.png
- soundsFolder.png
- skeletalFolder.png
- animFolder.png
- skelFile.png
- animFile.png
- otherFile.png
- materialFunc.png

Existing application .ico icons were not moved by this task.
SHA-256 values of PNGs were checked before and after each move.

## CMake / resource contracts
All 11 PNGs are embedded by qt_add_resources(northstarcpp_content_browser_qt)
with URLs following
:/northstar/icons/assetBrowser/<filename>.png.
This applies to both the docked Qt Editor Content Browser and QA launcher.
No production assets need loose copies of these PNGs.

## Rendering
QtContentBrowser::iconForRecord is the central icon resolver for
asset grid/list decoration and Asset Details previews:
- Physical directories: folder.png
- Valid NEF8 YMT dictionary: skeletalFolder.png
- Virtual child of YMT: skelFile.png
- Valid NEF8 YCD dictionary: animFolder.png
- Virtual child of YCD: animFile.png
- Other, Material Function: otherFile.png, materialFunc.png
- Existing texture/model/material dictionary icons unchanged
The icon resolver uses the virtual child's *container_path*, not the
child file suffix or name. It retains the original asset reference
like container.ymt@EntryName for selection and editing.

## Regression
Isolated build:
out/asset-browser-folders-and-entries-qa-20261115/
Target northstarcpp_asset_browser_cards_qa: linked successfully.

CTest: 3/3 PASS
- northstarcpp_embedded_ycd_ymt_icons_qt:
  checks actual common-pistol.ymt / military-pistol.ycd dictionaries in
  sharedAssets, folder art, all child icons, Asset Details and list mode.
- northstarcpp_asset_category_icons_qt:
  verifies Other and Material Function icon selection and details.
- northstarcpp_folder_icons_qt:
  validates existing folder art in grid, tree and Asset Details.

Additional checks:
- cmake engine_layout_smoke.cmake: PASS
- tools/verify_architecture.py: PASS
- git diff --check of changed CMake, Qt editor and smoke files: PASS

Production dist unchanged; no Git commit or push.
Full northstarcpp_editor_qt executable was not rebuilt in this task.
