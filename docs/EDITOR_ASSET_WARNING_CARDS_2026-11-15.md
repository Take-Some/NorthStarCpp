# NorthStarCpp Qt Editor — Missing Asset Warning Cards

Date: 2026-11-15
Status: implemented and tested in isolated developer QA.
Production dist not replaced.

## Problem

The asset preview mixed unresolved dependency errors into the plain-text
"ASSET DETAILS" metadata, for example:
`Missing texture: textures/skydome.ytd@moon_new`.
Missing models, materials and textures were difficult to distinguish from
normal asset properties.

## Implementation

Added reusable, first-class Qt editor components:

- `engine/northstarcpp/modules/editor/qt_asset_warnings.hpp`
- `engine/northstarcpp/modules/editor/qt_asset_warnings.cpp`

`AssetWarningPanel` creates individually styled warning cards containing:

- System warning icon (`QStyle::SP_MessageBoxWarning`)
- Categorized title: Missing texture / Missing material / Missing model /
  Prefab unavailable / Asset warning
- Full, selectable resource reference
- Diagnostics describing why it could not be resolved
- Copy button for the exact resource reference

Warnings use a scrollable, bounded-height pane to prevent long dependency
lists from overrunning the preview layout. The pane hides itself when there
are no warnings and removes stale cards on successful reload.

Integrated into:

1. Asset Preview — unresolved YMAT/YTD texture entries, missing material
   sections, and fatal texture/model/material load errors.
2. Prefab Preview — missing model/material composition dependencies and
   invalid prefab files. A usable 3D composition or schematic preview is
   preserved whenever possible.
3. Material Editor — unavailable texture slots, unsupported texture
   previews, and missing source files no longer use a plain
   `Texture missing: ...` QLabel message.

Structured warnings are distinct from technical `ASSET DETAILS`
metadata. UI is English, consistent with the project's localization.

## Verification

New tests:

- `northstarcpp_qt_asset_warnings` validates three categorized cards,
  icon presence, full selectable path, Copy action, and clearing warnings.
- `northstarcpp_qt_asset_preview_warnings` opens the actual modal
  Asset Preview three times, against unavailable texture/model/material
  references, and checks the rendered warning cards.

In isolated build `out/asset-warnings-qa-20261115`:

- `northstarcpp_content_browser_qt` static library: built successfully.
- `qt_asset_preview_dialog.cpp`: compiler syntax check passed.
- `qt_editor_window.cpp`: compiler syntax check passed.
- Both new CTest tests: **2/2 passed**.
- `git diff --check`: passed.

The complete Editor/Player binaries and production `dist` were **not**
replaced during this change. The isolated QA directory is distinct from
production releases.
