# NorthStarCpp: Canonical XML YMAT Migration — 18 Nov 2026

## Contract

All authored YMAT files use NEF8 kind 4 with XML YmatMaterialLibrary v2. NEF8 kind 30 with a JSON material library is rejected by the runtime loader, asset inspection, Qt Material Editor, and Asset Pipeline gate. The old JSON-kind-30 intermediate authoring path was deleted: material creation now produces kind-4 XML directly.

The Qt UI may exchange QJsonObject descriptors and compile typed JSON graph ASTs in memory; those are not the stored YMAT format. The separate Material Instance (.material-instance.json) and Material Function (.material-function.json) asset types are still JSON and are **not** included in this YMAT-only migration.

## Converted assets

| Asset | Old size | New size | Original material identities |
| --- | ---: | ---: | --- |
| engine/northstarcpp/resources/sharedAssets/materials/weapon_rifle_mini14.ymat | 416 | 557 | m00, m01 |
| engine/northstarcpp/resources/sharedAssets/materials/weapon_shell.ymat | 416 | 572 | brass |
| engine/northstarcpp/resources/sharedAssets/models/weapon/weapon_shell.ymat | 416 | 572 | brass |
| tests/fixtures/materials/MultiMaterialDemo.ymat | 518 | 500 | Copper Panel, Blue Ceramic |

Converted with the existing C++ migration tool in dry-run and apply modes. The original/converted shaders, blend modes, sidedness, parameters, texture bindings, member order and counts were checked. The NEF8 output header, BLAKE3 body digest and canonical XML schema were also verified.

Byte-for-byte originals are held outside packaged resource roots at:

.northstarcpp/backups/ymat-xml-p1-20261118/

The test fixture backup is located under that directory's test-fixture subdirectory.

## Source changes

- host/shared_asset_models.cpp and host/shared_assets.cpp: removed legacy JSON YMAT parsing and inspector exception.
- host/material_ymat_authoring.cpp: direct XML kind-4 authoring; rejects kind 30 on read.
- host/material_ymat_xml.cpp and host/material_graph_xml.cpp: removed kind-30 YMAT migration and orphaned JSON-library graph import.
- editor/qt_material_library_edit.cpp and editor/qt_material_authoring.cpp: XML-only YMAT editing/creation and preview.
- editor/qt_editor_window_materials.cpp: editor New Material action saves NEF8 XML .ymat rather than .material.json.
- editor/qt_editor_window_setup.cpp, editor/qt_material_instances.cpp: instances can inherit from native .ymat.
- editor/asset_catalog.cpp, editor/editor_tools.cpp and material/material.cpp: no longer accept bare .material.json as a material.
- material/material_precache.cpp: material inputs are XML YMAT (material-instance metadata remains its own format).
- tools/asset_pipeline.py: hard failure for non-kind4/non-XML source YMAT, including kind 30.
- Tests: changed old kind-30 fixture to XML, introduced explicit kind-30 rejection tests, and adapted native editor/instance tests.

## Validation status

- CMake out/q7 native material loader, Material Editor and full Qt Editor built successfully.
- Python asset_pipeline smoke: 7/7 PASS.
- CTest nsp_verify, asset_pipeline_identity, material_resources, qt_material_library_edit: 4/4 PASS.
- CTest precache, functions, typed graph, HLSL, material_resources, qt library edit and asset dialog: 7/7 PASS.
- Qt Material Instance inheritance/cycle smoke passed after changing its floating-point checks to tolerances consistent with native float storage.

## Remaining independent work

The XML-only assertion in this document refers to persisted YMAT material libraries. Separate Material Instance inheritance metadata and Material Function graph assets still use JSON and need their own format/version migrations if all material-adjacent assets must be XML. The script/data JSON elsewhere in the engine is not affected.

Original project edits remain local. No commit, push or production dist replacement was requested.
