# MT28 native YMAT identity restoration — QA record

Date: 2026-11-18
Scope: `Projects/KaylaNightCity` and `Projects/NorthStarFirstPersonPlatform`.
All changes are local to the current working tree.

## Root cause, with evidence

The native `NEF8` YDR model contains an `NVRM` JSON metadata field
`material_slots[]`, with original material names and blend/surface
types in stable slot order. The corresponding YMAT XML body contains
the same number and order of materials and the same blend types, but
its `Material/@name` values were serialized as anonymous `mat_0`,
`mat_1`, and so on. The prefab compositor resolves named references
`materials/mahir_mt28/<model>.ymat@<original_slot_name>`, so the
references failed even though the files existed.

Examples restored from the model itself:
- `mat_0` -> `vehicle_branding`
- `mat_2` -> `light_covers_glass` (alpha/glass)
- `mat_7` -> `stickers`
- `mat_16` -> `windows_glass` (alpha/glass)
- Original multi-layer names such as
  `ml_v_mahir_mt28_coach_ext_masksset.newviso_ml.00` are preserved
  exactly, including the numeric suffix.

No substitute materials, placeholder textures, or inferred
shader properties were used.

## Repair

Tool: `tools/repair_mt28_material_slots.py`.

The migration:
1. Extracts source material identity from each corresponding YDR's
   original `NVRM.material_slots[]`.
2. Validates slot count and order, unique name, and alpha/opaque/masked
   consistency against the current YMAT XML.
3. Replaces **only** XML `Material name` attributes, checking complete
   XML tree equality after a name-only normalization.
4. Writes canonical NEF8 v1 kind 4 with XML material library payload,
   raw-DEFLATE compression and verified BLAKE3 digest.
5. Creates byte-identical verified original backups *outside* project
   `assets`, keeps source-file/repaired-file SHA256 hashes in a manifest,
   and performs atomic replacement after a full dry-run preflight.

Commands:

```powershell
python tools/repair_mt28_material_slots.py
python tools/repair_mt28_material_slots.py --apply
python tools/repair_mt28_material_slots.py
```

Repeat check result:
`files=94 identity_changes=0 model_slots=860 glass_slots=60`.

## Material data preservation proof

| Metric | Value |
| --- | ---: |
| Material files restored | 94 |
| Projects | 2 |
| Original YDR / YMAT pairs | 47 per project |
| Material slots retained | 860 |
| Glass-domain alpha slots retained | 60 |
| Texture references retained | 826 |
| XML property changes other than `Material/@name` | **0** |
| Verified SHA256 checks against saved originals | 94/94 |
| Canonical NEF8 BLAKE3 roundtrips | 94/94 |

Backup/evidence location:
`.northstarcpp/backups/mt28-names-20261118T123156870145Z/manifest.json`

Each manifest item includes relative project path and hashes of the
source model, original YMAT and repaired YMAT. Backups must not be
copied back into `assets` without a controlled rollback.

## Fresh C++ prefab-composition QA

Built the target from the working tree:

```powershell
cmake --build out/build --target northstarcpp_prefab_composition_smoke --parallel 3
ctest --test-dir out/build --output-on-failure --timeout 45 -R "^northstarcpp_prefab_composition$"
ctest --test-dir out/build -V --timeout 50 -R "^northstarcpp_prefab_composition$"
```

Result: **PASS**, 1/1, about 3.1 seconds.

Key CTest evidence:

```text
MT28_PREFAB project=NorthStarFirstPersonPlatform entities=65 axle_visuals=16 valid_mesh_paths=65 roundtrip=PASS
MT28_PREFAB project=KaylaNightCity entities=65 axle_visuals=16 valid_mesh_paths=65 roundtrip=PASS
MT28_GPU_ELIGIBILITY opaque=85 masked=93 translucent=472 graph=0 shaders=0
MT28_PREVIEW modeled=64 missing=0 proxy=0 triangles=2039310 warnings=0
MT28_PREVIEW_FRAME_MS full=184.913 interactive=66.801 transparent_sections=30
MT28_PREVIEW_RASTER dimensions=192x192 pixels=36864
```

The C++ test further verifies a minimum transparent-section count,
actual rasterized alpha blending against an opaque background, and
texture dictionary decoding; a completely empty warning list is
mandatory. All assertions remained unchanged.

The CPU software preview is **not** a claim of a visually certified
D3D12/Vulkan GPU presentation. Real hardware captures of bus windows
and reflective/light-cover surfaces are still needed for production QA.

## Scope boundaries

- The overall NorthStarCpp 1.0 milestone, full CTest suite and final
  shipped game have **not** been certified by this targeted repair.
- Source project backups and existing uncommitted changes were
  preserved.
- No `git commit`, `push`, or production `dist` replacement was
  requested or performed.
