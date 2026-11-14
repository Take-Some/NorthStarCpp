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

Four tests passing:
- northstarcpp_asset_catalog
- northstarcpp_content_browser_qt
- northstarcpp_nef8_content_browser_qt
- northstarcpp_shared_assets

The tests verify 5 YTD children and 1 YDD child, correct @entry
selection and data length, backing-file resolution, rejection of
physical resolve for embedded items, and Qt folder navigation.

No running Project Manager was forcibly closed. The first build
directory was locked by a live executable; a separate QA build directory
was used instead. No production dist or installed native DLL was replaced.

## Remaining acceptance

For production installation, rebuild and package the updated Qt Content
Browser and runtime in the approved release pipeline. This targeted
QA verification does not by itself approve the unrelated V8/Qt global
release blockers or entire engine integration QA.
