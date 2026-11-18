# NorthStarCpp — Content Browser navigation persistence

Date: 2026-11-17

## Incident
Saving, deleting, copying or externally modifying an asset caused Content
Browser to refresh its catalog and unexpectedly jump to Project Assets root.

## Root cause
`QtContentBrowser::refresh()` previously replaced both catalogs with empty
instances, cleared the visible model and rebuilt the tree **before** the
asynchronous index finished. `populateTree()` then interpreted the opened
virtual folder as missing and cleared `folder_`. The subsequent completion
could no longer recover the previous path, selected file or scroll position.

## Changes
- Keep the last valid catalogs, tree and card model visible during background
  indexing. No placeholder tree rebuild during scan.
- Use the scan generation token to ignore stale, overlapping scan results.
- At completion restore source, virtual folder, expanded branches,
  current item, multi-selection and vertical scroll position if still valid.
- Preserve existing search query, type filter, sort, collection and
  navigation history. No automatic return to the root on save/delete.
- If the opened directory itself was removed, use the nearest existing
  ancestor; if only the selected file disappeared, stay in its folder and
  clear the invalid selection.
- Defer scrollbar restoration to Qt's next layout cycle to avoid restoring
  a position against obsolete scroll limits.
- Explicit changes of project roots still start at the new project's root;
  they are intentional navigation, not catalog refresh.
- Provide read-only `currentFolder()`, `currentSelection()` and
  `catalogReady()` accessors for UI tests.

## Paths
`engine/northstarcpp/modules/editor/qt_content_browser.cpp`
`engine/northstarcpp/modules/editor/qt_content_browser.hpp`
`engine/northstarcpp/modules/editor/qt_content_browser_layout_smoke.hpp`

## QA
```powershell
cmake --build out/build --target northstarcpp_asset_browser --parallel 3
ctest --test-dir out/build -R northstarcpp_content_browser_stable_dock_qt --output-on-failure
```

The Qt navigation regression builds a real temporary nested folder containing
90 assets and verifies:
1. Refresh retains the active folder, selected item and scroll offset.
2. A saved file remains selected, including when two rescans overlap.
3. Deleting a selected file keeps the folder and removes stale selection.
4. Removing the open folder navigates to its surviving parent, not root.
5. The browser remains in its dock and its layout does not resize.

Result on 2026-11-17: `Content Browser preserve folder/selection/scroll on
save/delete: PASS`. The normal layout smoke also passed.

Files modified in the checkout; no commit or push was performed.
