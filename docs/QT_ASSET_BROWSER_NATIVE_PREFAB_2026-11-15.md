# Qt Asset Browser — Native Prefab Support
Date: 2026-11-15

## Scope

The current Qt6 NorthStarCpp Content Browser now treats `*.nprefab` as a
dedicated `Prefab` asset, not a generic scene. It supports the existing
Scene V3 authoring format and the existing editor prefab instantiation
contract. No legacy Python browser or new engine ABI is involved.

## Discovery and metadata

- AssetCatalog recognizes prefabs inside project Content/asset subfolders
  and the sibling project/prefabs directory (shown as virtual `prefabs/`).
- The catalog index worker reads and validates bounded Scene V3 prefab files
  and records their actor count and validity. Broken prefabs stay visible
  with a clear invalid status.
- Prefab cards use a dedicated type, colored accent, accurate actor-count
  subtitle, filter `Prefabs`, and readable hierarchy/collision/material
  references in the details pane.
- Canonical project-relative resolution, no traversal or symlink escapes,
  capped 4 MiB source files; safe physical folder operations.

## Thumbnail and preview

- Asynchronous prefab card thumbnails display actual native YDD/YDR geometry
  when a one-actor prefab references a readable drawable; otherwise they
  show a schematic isometric composition preview.
- The read-only modal Prefab Preview displays actor/child hierarchy,
  mesh references, collision counts, layers, cells and components.
- For a single native drawable the existing interactive 3D orbit/zoom
  canvas is used, with original per-section YMAT/YTD material resolution.
  Missing external assets and multi-actor prefabs fall back to a composition
  schematic, without changing the source prefab.

## Placement

- Drag `*.nprefab` from the integrated Qt Content Browser into the Scene
  Viewport to place its root at the raycast/ground hit position, preserving
  original child entities, asset paths, layers, components and collision.
- The context menu exposes `Instantiate Prefab in Scene` in the editor.
- Instantiation uses `editor_tools::instantiate_prefab` inside
  `QtEditorWindow::editScene`; IDs are remapped and Undo/Redo works.
- Standalone Asset Browser is read-only with respect to the scene unless
  hosted by an editor that handles the activation signal.

## Acceptance tests

QA build directory: `out/material-editor-v3-p0-20261115`.

- `northstarcpp_prefab_browser` checks the real
  `Projects/NorthStarFirstPersonPlatform/prefabs/weapon/pistol.nprefab`:
  original YDD reference, mass, collision, actor count, preview, anchored
  placement, traversal resistance. Also checks nested Content prefabs
  and invalid asset quarantine.
- `northstarcpp_prefab_cards_qt` exercises the actual Qt grid card,
  asynchronous real YDD thumbnail, modal interactive 3D preview and source
  actor metadata on the Windows Qt platform.
- Existing catalog, content browser, card rendering, NEF8 dictionary,
  folder icon and editor prefab tests remain green.
- Latest full targeted regression: **8/8 CTests passed**.
- Latest separately linked editor executable:
  `out/material-editor-v3-p0-20261115/northstarcpp_editor_qt_prefab_qa.exe`
  was compiled/linked and `--smoke` passed.
- `git diff --check`: passed; unrelated working-tree line-ending warnings.

## Release status and limitations

The production `dist` was not modified. No commit or push was made.
A running Windows Qt Editor locked the default executable, so the latest
editor was linked and tested under the isolated `_prefab_qa.exe` name.
Standalone Asset Browser was safely relinked in the QA build.

The complex, multi-object preview is currently a schematic composition,
not a single fully GPU-rendered multi-entity scene. That remains the next
visual acceptance improvement. The unrelated in-flight D3D12 renderer
refactor has build defects; it was not modified as part of prefab work.
