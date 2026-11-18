# Character asset namespace conventions

NorthStarCpp character bundles use a stable VFS namespace: characters/<id>/.
Names are ASCII letters, digits, underscores and hyphens (maximum 72 chars).

## Recommended layout

    characters/
      abby/
        abby.character.json
        models/
          abby.ydd
          abby.ymt
        materials/
          abby.ymat
        textures/
          abby.ytd
        animations/
          mm-explore.ycd
          traversal.ycd
          fall-medium.ycd

## Ellie (staged)

`characters/ellie/ellie.character.json` and `prefabs/characters/ellie.nprefab`
are registered for engine-wide reuse. The direct Python PAK importer now
extracts real Ellie native YDD geometry previews (15 independent part files,
47 submeshes with original twelve-source-influence streams), a partially
reconstructed 1074-joint YMT, a native 397-texture YTD, and native 42-material
XML YMAT. Original PAK, GPU and ICE animation buffers are retained locally
in `characters/ellie/source/` (excluded from NSP archive and Git).

The existing C++ NVRM parser can resolve the canonical shared YMAT and load
source skin influences into the eight-weight CPU skinning path. Full linking,
GPU skinning, the remaining 503 helper bind transforms, original ICE-to-YCD
conversion, and viewport validation remain incomplete. The regular
`ellie.nprefab` is **not** ready: it still targets the future `ellie.ydd`.
Use `prefabs/characters/ellie_preview.nprefab` for geometry inspection.
QA: `python tests/ellie_native_extraction_smoke.py` and
`python tests/ellie_native_material_skin_smoke.py`.
Full report: `characters/ellie/README.md`.

The character descriptor uses schema northstar.character.v1. It lists the
model, skeleton, appearance material and texture dictionary, and named
animation clip references. Each character has its own resources; no fake
stand-in texture packs are created.

The Qt Content Browser categorizes *.character.json as Character, includes
a Characters category filter, and displays the source paths and animation
count in the asset Inspector. Gameplay code selects which character to use
through project-specific configuration (e.g. playerConfig.js).

## Imported source references

Original binary YDD/YMAT files remain byte-for-byte unchanged. Some imported
YDDs refer to materials/character_<id>.ymat@entry, while imported YMATs refer
to textures/characters/<id>.ytd@entry. For a matching canonical source path,
the native C++ loader maps these refs to
characters/<id>/materials/<id>.ymat@entry and
characters/<id>/textures/<id>.ytd@entry.

Only the matching source convention is remapped, with strict ID validation.
Unrelated or unsafe paths remain unchanged. No plugin C ABI changes.

## Migration and validation

    python tools/migrate_character_assets.py
    python tools/migrate_character_assets.py --apply
    python tools/migrate_character_assets.py --verify
    python tools/migrate_character_assets.py --finalize

The utility checks SHA-256 of seven known imported resources. If a file is
locked by a running Windows process, the migration can stage a verified copy
and retain an identical legacy alias until it can be removed safely.
It never kills Player or Editor processes.

Regression gates:
- northstarcpp_asset_catalog
- northstarcpp_character_namespace_rebase
- northstarcpp_skeletal_asset_interop
- northstarcpp_material_resources
- northstarcpp_editor_asset_pipeline
