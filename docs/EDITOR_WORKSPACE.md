# NorthStar Editor — Workspace and Content Browser

## Workspace

The editor currently has two GUI frontends during migration. The existing
Win32 scene editor supplies World Outliner, native D3D11 viewport, Details,
undo/redo, and gameplay authoring. The **new native Qt 6 Content Browser**
opens from **View > Open Native Content Browser** or **Ctrl+Shift+B**. This
cross-platform Qt/C++ component replaces the Windows Shell asset-navigation
window as the default browsing experience; the legacy Win32 asset pane is
hidden by default and available temporarily under
**View > Legacy Asset Panel (Compatibility)**.

The new Content Browser supports project folder browsing, an Unreal-style
icon grid and list view, global search, asset type filtering, preview panel,
background thumbnails for YTD/YDD/YMAT, category-specific icons, import,
rename, copy, create folder, and guarded deletion. File references are
project-relative paths (not raw absolute OS paths). See
[Native Qt Content Browser](QT_CONTENT_BROWSER.md) for architecture,
validation, and limitations.

A full Qt editor shell with docked Content Browser, scene viewport, and
World Outliner is not finished. Asset binding and drag-to-place into the
existing Win32 viewport still use compatibility tools. Qt is portable, but
the current D3D11 editor/runtime is Windows-only.

## Gizmo

Select an actor in World Outliner or by clicking its projected position in
the Viewport. Drag red X, green Y, or blue Z axes. W selects Translate, E
selects Rotate and R selects Scale when the Viewport has keyboard focus.

Local toggles world vs authored local translation axes. Snap quantizes the
delta, and Step changes the increment for the selected mode.
Mouse drag uses a temporary scene; releasing commits a validated edit and
one undo checkpoint through Scene/ECS. Ctrl+Z and Ctrl+Y undo/redo changes.

Hit testing uses the current third-person camera projection. Mesh-precise
picking, free camera navigation, persistent customizable panel layouts,
asset previews and more complex Unreal-like authoring remain future work.

## Verification

The editor target builds content_browser.cpp and links comctl32. The MinGW-w64
Release verification build resides in out/editor-ui-validation-20261114.
Editor --warp --smoke exited with code 0 and saved its scene. The
northstarcpp_editor_authoring regression test passed. A local Ninja build
graph snapshot was used because the standard Ninja regeneration encountered
a Windows permission denied error during log recompaction.
