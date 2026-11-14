# NEF8 YDD/YTD ListFile browsing — NorthStarCpp
Date: 2026-11-14
Maintainer: Kayla's Systems — Kayla Verner

## Defect

The Qt Content Browser's AssetCatalog scanned YTD/YDD dictionaries as a
single physical file. Thumbnail generation called load_textures() or
load_drawables() and unconditionally rendered list.front(), ignoring
the selected internal name. Large multi-entry asset dictionaries therefore
looked like one resource and every preview could show the first asset.

## Corrected model

- A valid NEF8 YTD or YDD container is represented as a virtual folder
  with the original filename and extension (e.g., textures/skydome.ytd).
- Each named dictionary record is indexed as a read-only virtual child,
  with the original internal entry name as its display filename.
- The reference used by scripts, drag-and-drop and assetSelected is the
  existing engine contract: <container>.ytd@<entry> or
  <container>.ydd@<entry>.
- Its physical backing file remains the original dictionary; children
  are NOT extracted to disk.
- Thumbnail rendering resolves the selected @entry via AssetStore, no
  longer defaulting to the first element.
- UI navigation, tree, search, type filters, breadcrumb and metadata
  recognize virtual folders and children.
- Changes to the underlying dictionary remain atomic: embedded items
  cannot be renamed/deleted/duplicated or used as writable filesystem
  directories through Content Browser.
- Malformed and non-NEF8 files remain ordinary files in the catalog.
  Integrity checks include decompression size, BLAKE3 and dictionary
  index validation. The indexer reads NEF8 directly from the physical
  Content root without requiring a dynamically loaded asset provider.

## Real example

The engine's sharedAssets/textures/skydome.ytd contains five internal
textures:
- baseperlinnoise3channel
- dither
- moon_new
- noise16_p
- starfield

The sharedAssets/models/skydome.ydd contains skydome_high.

The Qt browser shows them under the corresponding virtual folders.
One example stable reference is:
textures/skydome.ytd@moon_new

## Changed implementation

- include/northstarcpp/editor_asset_catalog.hpp
- engine/northstarcpp/modules/editor/asset_catalog.cpp
- include/northstarcpp/shared_assets.hpp
- engine/northstarcpp/modules/host/asset_list_file.hpp
- engine/northstarcpp/modules/host/asset_list_file.cpp
- engine/northstarcpp/modules/host/shared_asset_dictionary_index.cpp (new)
- engine/northstarcpp/modules/editor/qt_content_browser.hpp
- engine/northstarcpp/modules/editor/qt_content_browser.cpp
- engine/northstarcpp/modules/editor/qt_asset_browser_main.cpp
- engine/northstarcpp/CMakeLists.txt
- tests/asset_catalog_smoke.cpp
- tests/shared_assets_smoke.cpp

## Verification

QA build: out/nef8-browser-qa-20261114
Targets: northstarcpp_asset_browser, northstarcpp_asset_catalog_smoke,
         northstarcpp_shared_assets_smoke.

Six tests passing:
- northstarcpp_asset_catalog
- northstarcpp_content_browser_qt
- northstarcpp_nef8_content_browser_qt
- northstarcpp_shared_assets
- northstarcpp_editor_asset_thumbnails
- northstarcpp_editor_asset_pipeline

The tests verify 5 YTD children and 1 YDD child, correct @entry
selection and data length, backing-file resolution, rejection of
physical resolve for embedded items, and Qt folder navigation.

The thumbnail pipeline also verifies that explicitly selected dither
(BC1/NETD 101) produces a valid thumbnail differing from the first
dictionary entry. NETD BC7 (format 104) is indexed and selectable, but
its thumbnail is not decoded by the existing software preview engine;
the correct type icon is used rather than the wrong first-entry image.

The six selected CTests passed in the separate QA build after rebuilding
the native Qt Asset Browser and thumbnail pipeline. This is targeted
validation, not whole-workspace CTest or production release acceptance.

No running Project Manager was forcibly closed. The first build
directory was locked by a live executable; a separate QA build directory
was used instead. No production dist or installed native DLL was replaced.

## Remaining acceptance

For production installation, rebuild and package the updated Qt Content
Browser and runtime in the approved release pipeline. This targeted
QA verification does not by itself approve the unrelated V8/Qt global
release blockers or entire engine integration QA.


## Physical vs virtual folder artwork

NorthStarCpp Qt Content Browser embeds four original project PNG images as
resource northstar_folder_icons, without requiring loose icons at runtime.

- Physical directories: icons/folder.png
- Virtual YTD ListFile directories: icons/texturesFolder.png
- Virtual YDD ListFile directories: icons/modelsFolder.png
- Virtual YMAT ListFile directories: icons/materialsFolder.png

The icon selection depends on AssetRecord.is_list_file and the container
extension, not directory names. Tree, grid, and Asset Details agree.
Resource initialization is in the global namespace for static Qt linkage.

QA build:
out/nef8-ue-preview-qa-20261114/northstarcpp_asset_browser.exe

northstarcpp_folder_icons_qt PASS: 6/6 folder fixtures, pixel comparisons of
tree/grid/detail images and four distinct embedded image assets.
Relevant CTest suite: 7/7 PASS (catalog, Content Browser, NEF8 browser,
modal, folder icons, thumbnails, shared assets).

Production dist and running DLLs were not replaced.

## 2026-11-14 color correction

A legacy NETD YTD moon texture was marked BC7 but its payload is BC3.
A conservative runtime metadata compatibility check now selects BC3
for the characteristic block signature; legitimate BC7 is preserved.
The Moon preview displays grayscale craters rather than red/blue
noise. Material color-space sampling is corrected and a material
preview concurrency hazard is contained.
See NEF8_YTD_TEXTURE_COLOR_FIX_2026-11-14.md.
