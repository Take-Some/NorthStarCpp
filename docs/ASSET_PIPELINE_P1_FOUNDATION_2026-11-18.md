# NorthStarCpp P1 — asset identity, dependencies and NSP packaging

Date: 2026-11-18. Build-time authoring and CI layer for shared engine resources.
Does not replace AssetStore runtime VFS, Qt AssetCatalog/AssetDependencyRegistry,
C++ derived_data_cache, or the existing binary NSP pack format.

## Implemented
- tools/asset_pipeline.py inventories assets with pack_nsp exclusions; disallows
  case-folded path collisions, symlinks and files exceeding the pack size limit.
- UUIDv5 asset IDs persist across rebuilds. Explicit --move OLD=NEW preserves
  IDs after a file move; --prune acknowledges intentional deletions.
- XML-first NEF8 kind-4 YMAT named member IDs and member slot order are checked.
  Anonymous mat_N names, duplicate members and unapproved slot renames fail.
- Non-XML YMAT is rejected unconditionally; the canonical format,
  migrated files, and proof of conversion are documented once in
  [XML YMAT Migration](YMAT_XML_ONLY_MIGRATION_2026-11-18.md).
- Dependencies from bounded quoted text/JSON/XML/script/shader sources include
  named material references and relative shader includes. Missing named YMAT
  members and dependency cycles are errors.
- Content-derived SHA-256, transitive dependency keys, stable IDs, target and
  options produce deterministic cook keys and atomic DDC metadata records.
- pack_nsp.py --asset-registry checks source identity, dependency integrity and
  hashes before deterministic archive publication.
- Root CMake now publishes registry + DDC metadata prior to northstar_shared.nsp,
  and registers northstarcpp_asset_pipeline_identity in CTest.

## Outputs
Inside the selected CMake build directory:
  asset-registry/northstar_shared.json
  asset-registry/northstar_shared_cook.json
  derived-data/shared/<prefix>/<content-key>.json
  archive/northstar_shared.nsp

Registry/DDC records live outside assets and are not packaged. Qt-generated
RCC accounts for one extra archive entry not present in source inventory.

## Commands (from repo root)
  python tests/asset_pipeline_smoke.py
  python tools/asset_pipeline.py --root engine/northstarcpp/resources/sharedAssets --state out/qa/asset-pipeline-isolated/asset-registry/northstar_shared.json --namespace northstar-engine --target shared-nsp-v1 --verify
  cmake --build out/qa/asset-pipeline-isolated --target northstarcpp_shared_nsp
  ctest --test-dir out/qa/asset-pipeline-isolated --output-on-failure -R "^northstarcpp_(nsp_verify|asset_pipeline_identity)$"

Example custom project inventory:
  python tools/asset_pipeline.py --root Projects/KaylaNightCity/assets --namespace KaylaNightCity --target win64 --options release --state out/qa/kayla-asset-registry.json --apply --cache out/qa/kayla-ddc --manifest out/qa/kayla-cook.json

Renaming a source file is a separate filesystem operation; migrate its ID
with --move OLD=NEW, never by assigning a new ID to an existing asset.
Do not write registry, cook manifest or DDC beneath the scanned assets root.

## 2026-11-18 acceptance

Numbers below describe an **earlier isolated QA inventory** and are not
permanent asset counts. For current release readiness consult [Current Status](CURRENT_STATUS.md).
- MT28 source material identity: 47 files, 430 slots, 30 glass slots, 0 repairs.
- MT28 material-only P1 preflight: 47 assets.
- Shared source preflight: 224 assets, 21 quoted-reference edges, 0 legacy YMAT.
- The legacy material migration and source-wide 160-file format audit
  are recorded in [XML YMAT Migration](YMAT_XML_ONLY_MIGRATION_2026-11-18.md).
- Isolated CMake northstarcpp_shared_nsp: PASS. 225 archive entries,
  310,472,625 bytes on latest build (310,472,641 on first pass).
  Initial DDC population: 224 misses; incremental rebuild: 222 hits,
  2 misses; the cause was not isolated from other concurrent work.
- Registry --verify after rebuild: PASS.
- Python smoke: 7/7 PASS; XML-only runtime/editor regression 12/12 CTest PASS.
- CTest northstarcpp_nsp_verify and northstarcpp_asset_pipeline_identity:
  2/2 PASS.
- Existing out/build CMake regeneration was blocked by Ninja permission
  denial during recompaction. Isolated build succeeded, no production dist
  files were replaced.

## Explicit unfinished P1 scope
- Material Instance/Function are distinct JSON-based asset types; see
  [XML YMAT Migration](YMAT_XML_ONLY_MIGRATION_2026-11-18.md) for the
  format boundary. Converting them is outside this build-time registry stage.
- Build registry and Qt AssetDependencyRegistry are NOT yet one C++ runtime
  identity service. Native AssetStore and Editor need common registry ABI.
- Binary prefab/scene and NEF8 YDD/YTD/YCD/YMT member dependencies require
  format-native decoders; current quoted-reference graph is incomplete.
- External/shared/project reference resolution and mount-shadowing semantics
  are not yet represented in the build dependency graph.
- Shader permutation compilation, PSO precache and Vulkan/D3D12 variant DDC
  need provider integration. Currently options/target only affect cook keys.
- DDC entries are cook *metadata*, not cooked GPU shader binaries, textures,
  audio, collision shapes or prefab runtime payloads.
- Need cache integrity validation, multi-process locking, eviction/GC,
  full CI matrix, large-scale benchmarks and runtime/Editor/Player GPU QA.
- The author's current uncommitted working tree was preserved.
  No commit, push, or production distribution replacement was performed.
