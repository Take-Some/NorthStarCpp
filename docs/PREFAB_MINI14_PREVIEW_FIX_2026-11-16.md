# Mini-14 Prefab Preview — 2026-11-16

## Root cause
The Qt Prefab modal and Content Browser thumbnail workers mounted the Content
directory at priority 100, then mounted the nearest engine sharedAssets directory.
When browsing sharedAssets itself, these were the same physical directory and the
engine.assets provider rejected the duplicate with "Asset source already mounted".

## Fix
- Added `prefab_preview::mount_preview_assets` for a common, single-mount
  mechanism. It uses `std::filesystem::equivalent` to avoid duplicate roots.
- Updated both Qt modal preview and thumbnail generation to use the common
  mount helper.
- Added a real Mini-14 regression to prefab_composition_smoke.cpp.
- Added `--mini14-prefab-smoke` to the standalone Qt Asset Browser QA mode.

## Source texture integrity
The source `sharedAssets/textures/weapon/rifle_mini14.ytd` also had invalid
NETD offsets for small (2x2 and 1x1) BC4 mipmap levels. The corrected YTD
preserves the referenced compressed mip block bytes; repacks all six texture
payloads without gaps; rewrites NTDX offsets, entry lengths, NETD payload
length, compressed NEF8 lengths and BLAKE3 body digest.
Original backup:
`_qa_content_browser_p2/rifle_mini14.original_before_mip_repair.ytd`.

## Verification
- C++ prefab composition smoke: PASS (Mini-14: 16,966 triangles, 3 mesh
  sections, 3/3 material diffuse textures, no warnings, no proxy cubes).
- Existing two-pistol prefab composition smoke: PASS.
- Both modified Qt preview translation units: compiled successfully.
- Qt `--mini14-prefab-smoke`: PASS in offscreen mode, confirms the modal
  shows a native 3D composition instead of the schematic cube.
- Separate QA browser and Qt Editor executables were linked and the editor
  executable passed its `--help` startup check.

## Deployment limits
The active production Editor and Asset Browser were not replaced because the
normal build was blocked by a locked `render_d3d12.dll`.
The separate QA executables are under `_qa_content_browser_p2/`:
`northstarcpp_asset_browser_mini14_qa.exe` and
`northstarcpp_editor_qt_mini14_qa.exe`.
On-screen GUI visual acceptance remains to be completed; the Qt modal smoke
was conducted using the offscreen platform.

No commit or push was performed.
