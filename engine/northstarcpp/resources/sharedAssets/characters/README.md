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
