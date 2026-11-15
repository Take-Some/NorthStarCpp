# NorthStarCpp Qt Editor — Material assignment and authoring

Date: 2026-11-14
Status: editor authoring feature integrated; production dist not replaced.

## Actor inspector

Every selected scene actor has a "Материал" (Material) row in
Details | Properties with a dropdown. It contains:

- None (clear assignment)
- Existing editable project *.material.json files
- Existing project .ymat/.material/.mtl materials
- Common material resources from sharedAssets (project VFS override wins)

The dropdown shows material preview spheres generated asynchronously and
an Edit... action for editable project *.material.json descriptors.
Refresh rescans workspace materials.

## Material, never a direct texture

Actors store only a reference to a material asset, serialized in an NSCN V3
component:
  type = northstarcpp.editor.material
  version = 1
  payload = project/shared virtual material path

The schema validates that the component references a material suffix.
A direct texture reference is rejected.

Dragging a texture over a scene actor:
  1. Resolve and validate the existing texture asset in project content.
  2. Create Materials/<name>_Material.material.json with a unique filename,
     and set base_color_texture to the original texture virtual path.
  3. Assign the resulting material to the actor.
  4. Refresh Content Browser and Material selector.

Dragging a material over an actor only assigns that existing material.
Dragging mesh assets onto scene geometry uses the existing engine.collision
ray narrowphase to determine a surface hit and target actor; it creates a
scene object rather than assigning a material.

## Material Editor

Editable JSON materials support:
- Standard lit / unlit shader
- RGB Base Color picker
- Metallic [0..1]
- Roughness [0..1]
- Texture slots: Base Color, Normal, Roughness, Metallic, Emissive
- Inline thumbnail preview when supported by Qt image readers
- Atomic writes and protected project-relative paths

Compiled YMAT resources remain read-only; they are selectable but not
silently converted to or overwritten by JSON. Editing shared resources
requires making a project-local copy. Native engine.material shader
compilation and live GPU texture/UV assignment are separate remaining
renderer tasks. The current feature guarantees correct authoring data and
preview/UI state, not full UE5 PBR rendering parity.

## Integration and safety

- Material records are kept independently of renderer and ECS provider.
- Undo/Redo, scene save/reopen, and unknown component preservation apply.
- Material references also appear in the Asset References dependency panel.
- AssetKind recognizes compound *.material.json extension.
- Safe virtual path, symlink/junction and bounded material descriptor
  validation protect project assets.
- Shader and numerical parameters are validated before atomic saving.

## Verification

Release QA development build: northstarcpp_editor_qt compiled successfully.
Tests passed:
- northstarcpp_asset_catalog
- northstarcpp_content_browser_qt
- northstarcpp_nef8_content_browser_qt
- northstarcpp_qt_editor_authoring
- northstarcpp_editor_authoring

The Qt authoring smoke exercises texture drop, material creation, inspector
selection, modal Roughness editing, project/shared selection, Undo/Redo and
NEF8/NSCN serialization.

Production dist and prior frozen QA release remain unchanged.
