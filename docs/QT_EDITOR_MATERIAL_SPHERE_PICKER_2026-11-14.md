# NorthStarCpp Qt Editor — Material Sphere Picker

Date: 2026-11-14
Status: built and tested in the isolated NorthStarCpp Release QA tree.
Production dist is unchanged.

## Behavior

Selecting any actor in the Qt Editor Inspector shows the **Материал** field.
The QComboBox material picker now displays each entry as a **52×52 studio-lit
PBR sphere** with a readable material filename and project-relative
reference. Selected materials retain their sphere icon in the collapsed
field. Entries are still ordinary material references; textures cannot
be assigned directly to actors.

Editable project `*.material.json` assets produce material-specific
preview spheres using the already existing first-party
`editor_tools::preview_material()` rendering implementation:

- Base Color hex tint;
- Base Color texture where decodable (PNG/JPG, etc., plus first texture in
  standalone YTD);
- Metallic;
- Roughness;
- other authored texture slots (normal/roughness/metallic/emissive), when
  decodable by the existing texture pipeline.

For native `*.ymat` material files, previews reuse
`loadMaterialPreviewSource()` and the native material/texture decoder.

The preview is an **editor approximation**, not a live preview of the final
GPU shader or scene lighting. Unsupported formats use the neutral sphere
until a supported preview source can be decoded.

## Performance and safety

- The dropdown can be used immediately; initial icons are neutral spheres.
- Preview generation runs in a QtConcurrent background worker, never inside
  the Inspector's refresh or selection callback.
- Work is queued in bounded, sequential batches around the selected /
  highlighted item to avoid saturating material decoding workers.
- Material sphere images are cached by project virtual reference.
- Editing or creating a material invalidates that preview cache.
- Switching project roots or reloading an override invalidates in-flight
  worker results using a revision number.
- The Qt GUI thread owns all icon creation and widget mutation.
- Decoded images are bounded, checked against project-root path traversal,
  and copied into owned QImage storage.

## Modified first-party modules

- `modules/editor/qt_editor_window.cpp`: preview-bearing Material picker,
  asynchronous completion, and expanded Qt smoke regression.
- `modules/editor/qt_editor_window.hpp`: per-window preview cache and
  revision/pending state.
- `modules/editor/qt_material_authoring.hpp/.cpp`: generation of material
  spheres from descriptor/native material + textures via the existing engine
  PBR preview, and neutral loading sphere.

No new DLL, renderer backend, provider ABI, or alternative rendering
architecture was introduced.

## Verification

- CMake Release build target `northstarcpp_editor_qt`: PASS.
- Editor `--smoke`: PASS.
- Expanded GUI smoke:
  - Create project-local red and blue materials.
  - Assert both previews contain different, correct sphere colors.
  - Create a third material from a checkerboard texture.
  - Assert the textured material generates a different sphere.
  - List all three in the actor Material picker.
  - Choose a material through the actual Qt selection signal.
  - Verify Undo / Redo and NSCN persistence.
  - Verify the asynchronously rendered sphere icon replaces the pending
    neutral icon for the named material.
- Focused CTest suite: **6/6 PASS** — Qt editor authoring, Content Browser,
  card previews, folder icons, NEF8 browsing and asset catalog.

A monitor-level/manual visual check of dropdown scaling and text legibility
under the user's actual DPI and theme is still recommended; smoke exercises
the real Qt widget but does not independently certify all display settings.

## Scope

The selector is material-only. Dropping a texture onto a surface creates
an authored material and assigns that material, while dropping an existing
material directly assigns it. Persisted assignments remain versioned
scene component records with Undo/Redo.

The production `dist` package has not been replaced; the new Editor exists
in `out/fps-release-qa-20261114/northstarcpp_editor_qt.exe`.
